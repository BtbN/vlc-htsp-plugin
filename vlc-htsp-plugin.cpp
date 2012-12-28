/*****************************************************************************
 * Copyright (C) 2012
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#define __STDC_CONSTANT_MACROS 1

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_demux.h>
#include <vlc_access.h>
#include <vlc_url.h>
#include <vlc_network.h>

#include <libavcodec/avcodec.h>

#include <deque>
#include <string>

extern "C"
{
#include <libhts/sha1.h>
#include <libhts/htsmsg.h>
#include <libhts/htsmsg_binary.h>
}

static int OpenHTSP(vlc_object_t *);
static void CloseHTSP(vlc_object_t *);
static int DemuxHTSP(demux_t *demux);
static int ControlHTSP(demux_t *access, int i_query, va_list args);

vlc_module_begin ()
	set_shortname( "HTSP Protocol" )
	set_description( "TVHeadend HTSP Protocol" )
	set_capability( "access_demux", 0 )
	set_category( CAT_INPUT )
	set_subcategory( SUBCAT_INPUT_ACCESS )
	set_callbacks( OpenHTSP, CloseHTSP )
	add_shortcut( "hts", "htsp" )
vlc_module_end ()

struct hts_stream
{
	hts_stream()
		:es(0)
		,lastDts(0)
		,lastPts(0)
	{}

	es_out_id_t *es;
	es_format_t fmt;
	mtime_t lastDts;
	mtime_t lastPts;
};

struct demux_sys_t
{
	demux_sys_t()
		:start(0)
		,pcrStream(0)
		,lastPcr(0)
		,netfd(0)
		,streamCount(0)
		,stream(0)
		,host("")
		,port(0)
		,username("")
		,password("")
		,channelId(0)
		,nextSeqNum(0)
		,hadIFrame(false)
	{}
	
	~demux_sys_t()
	{
		if(stream)
			delete[] stream;
	
		for(std::deque<htsmsg_t*>::iterator it = queue.begin(); it != queue.end(); it++)
			htsmsg_destroy(*it);
		queue.clear();
			
		if(netfd >= 0)
			net_Close(netfd);

		vlc_UrlClean(&url);
	}

	mtime_t start;
	uint32_t pcrStream;
	mtime_t lastPcr;

	int netfd;

	uint32_t streamCount;
	hts_stream *stream;

	vlc_url_t url;

	std::string host;
	uint16_t port;
	std::string username;
	std::string password;
	int channelId;

	std::string serverName;
	std::string serverVersion;
	int32_t protoVersion;

	uint32_t nextSeqNum;
	std::deque<htsmsg_t*> queue;
	
	bool hadIFrame;
};

#define DEMUX_EOF 0
#define DEMUX_OK 1
#define DEMUX_ERROR -1

#define PTS_DELAY (INT64_C(100000))

#define MAX_QUEUE_SIZE 1000
#define READ_TIMEOUT 10

/***************************************************
 ****       HTS Protocol Helper Functions       ****
 ***************************************************/

std::string htsmsg_get_stdstr(htsmsg_t *m, const char *n)
{
	const char* r = htsmsg_get_str(m, n);
	if(r == 0)
		return std::string();
	return r;
}

uint32_t HTSPNextSeqNum(demux_sys_t *sys)
{
	uint32_t res = sys->nextSeqNum++;
	if(sys->nextSeqNum > 2147483647)
		sys->nextSeqNum = 0;
	return res;
}

bool TransmitMessage(demux_t *demux, htsmsg_t *m)
{
	demux_sys_t *sys = demux->p_sys;

	if(!sys || sys->netfd < 0)
		return false;
	
	void *buf;
	size_t len;

	if(htsmsg_binary_serialize(m, &buf, &len, -1) < 0)
	{
		htsmsg_destroy(m);
		return false;
	}
	htsmsg_destroy(m);

	if(net_Write(demux, sys->netfd, NULL, buf, len) != (ssize_t)len)
		return false;

	free(buf);
		
	return true;
}

htsmsg_t * ReadMessage(demux_t *demux)
{
	demux_sys_t *sys = demux->p_sys;

	char *buf;
	uint32_t len;
	
	if(sys->queue.size())
	{
		htsmsg_t *res = sys->queue.front();
		sys->queue.pop_front();
		return res;
	}
	
	if(net_Read(demux, sys->netfd, NULL, &len, sizeof(len), false) != sizeof(len))
	{
		msg_Err(demux, "Error reading from socket: %s", strerror(errno));
		return 0;
	}

	len = ntohl(len);
	
	if(len == 0)
		return htsmsg_create_map();
		
	buf = (char*)malloc(len);
	
	ssize_t read;
	char *wbuf = buf;
	uint32_t tlen = len;
	time_t start = time(0);
	while((read = net_Read(demux, sys->netfd, NULL, wbuf, tlen, false)) < tlen)
	{
		wbuf += read;
		tlen -= read;
		
		if(difftime(start, time(0)) > READ_TIMEOUT)
		{
			msg_Err(demux, "Read timeout!");
			free(buf);
			return 0;
		}
	}
	if(read > tlen)
	{
		msg_Dbg(demux, "WTF");
		free(buf);
		return 0;
	}
	
	return htsmsg_binary_deserialize(buf, len, buf);
}

htsmsg_t * ReadResult(demux_t *demux, htsmsg_t *m, bool sequence = true)
{
	demux_sys_t *sys = demux->p_sys;

	uint32_t iSequence = 0;
	if(sequence)
	{
		iSequence = HTSPNextSeqNum(sys);
		htsmsg_add_u32(m, "seq", iSequence);
	}
	
	if(!TransmitMessage(demux, m))
		return 0;
	
	std::deque<htsmsg_t*> queue;
	sys->queue.swap(queue);
	
	while((m = ReadMessage(demux)))
	{
		uint32_t seq;
		if(!sequence)
			break;
		if(!htsmsg_get_u32(m, "seq", &seq) && seq == iSequence)
			break;
		
		queue.push_back(m);
		if(queue.size() >= MAX_QUEUE_SIZE)
		{
			msg_Dbg(demux, "Max queue size reached!");
			sys->queue.swap(queue);
			return 0;
		}
	}
	
	sys->queue.swap(queue);
	
	if(!m)
		return 0;
	
	const char *error;
	if((error = htsmsg_get_str(m, "error")))
	{
		msg_Err(demux, "HTSP Error: %s", error);
		htsmsg_destroy(m);
		return 0;
	}
	uint32_t noaccess;
	if(!htsmsg_get_u32(m, "noaccess", &noaccess) && noaccess)
	{
		msg_Err(demux, "Access Denied");
		htsmsg_destroy(m);
		return 0;
	}
	
	return m;
}

bool ReadSuccess(demux_t *demux, htsmsg_t *m, const std::string &action, bool sequence = true)
{
	if((m = ReadResult(demux, m, sequence)) == 0)
	{
		msg_Err(demux, "ReadSuccess - failed to %s", action.c_str());
		return false;
	}
	htsmsg_destroy(m);
	return true;
}

/***************************************************
 ****       Initialization Functions            ****
 ***************************************************/

bool ConnectHTSP(demux_t *demux)
{
	demux_sys_t *sys = demux->p_sys;

	sys->netfd = net_ConnectTCP(demux, sys->host.c_str(), sys->port);

	if(sys->netfd < 0)
		return false;

	htsmsg_t *m;

	m = htsmsg_create_map();
	htsmsg_add_str(m, "method", "hello");
	htsmsg_add_str(m, "clientname", "VLC media player");
	htsmsg_add_u32(m, "htspversion", 7);

	if((m = ReadResult(demux, m)) == 0)
		return false;

	size_t chall_len;
	const void * chall;
		
	sys->serverName = htsmsg_get_stdstr(m, "servername");
	sys->serverVersion = htsmsg_get_stdstr(m, "serverversion");
	htsmsg_get_s32(m, "htspversion", &(sys->protoVersion));
	htsmsg_get_bin(m, "challenge", &chall, &chall_len);
	
	void * lchall = 0;
	if(chall && chall_len)
		lchall = memcpy(malloc(chall_len), chall, chall_len);
	
	msg_Info(demux, "Connected to HTSP Server %s, version %s, protocol %d", sys->serverName.c_str(), sys->serverVersion.c_str(), sys->protoVersion);
	
	htsmsg_destroy(m);
	
	if(sys->username.empty())
		return true;
	
	m = htsmsg_create_map();
	htsmsg_add_str(m, "method"  , "authenticate");
	htsmsg_add_str(m, "username", sys->username.c_str());
	
	if(sys->password != "" && lchall)
	{
		msg_Info(demux, "Authenticating as '%s' with a password", sys->username.c_str());

		HTSSHA1 *shactx = (HTSSHA1*)malloc(hts_sha1_size);
		uint8_t d[20];
		hts_sha1_init(shactx);
		hts_sha1_update(shactx, (const uint8_t *)(sys->password.c_str()), sys->password.length());
		hts_sha1_update(shactx, (const uint8_t *)lchall, chall_len);
		hts_sha1_final(shactx, d);
		htsmsg_add_bin(m, "digest", d, 20);
		free(shactx);
	}
	else
		msg_Info(demux, "Authenticating as '%s' without a password", sys->username.c_str());
	
	if(lchall)
		free(lchall);

	bool res = ReadSuccess(demux, m, "authenticate");
	if(res)
		msg_Info(demux, "Successfully authenticated!");
	else
		msg_Err(demux, "Authentication failed!");
	return res;
}

bool SubscribeHTSP(demux_t *demux)
{
	demux_sys_t *sys = demux->p_sys;
	
	htsmsg_t *m = htsmsg_create_map();
	htsmsg_add_str(m, "method"         , "subscribe");
	htsmsg_add_s32(m, "channelId"      , sys->channelId);
	htsmsg_add_s32(m, "subscriptionId" , 1);
	htsmsg_add_u32(m, "timeshiftPeriod", (uint32_t)~0);
	//htsmsg_add_u32(m, "90khz", 1);
	//htsmsg_add_u32(m, "normts", 1);
	
	bool res = ReadSuccess(demux, m, "subscribe to channel");
	if(res)
		msg_Info(demux, "Successfully subscribed to channel %d", sys->channelId);
	return res;
}

bool parseURL(demux_t *demux)
{
	demux_sys_t *sys = demux->p_sys;
	const char *path = demux->psz_location;

	if(path == 0 || *path == 0)
		return false;

	vlc_url_t *url = &(sys->url);
	vlc_UrlParse(url, path, 0);

	if(url->psz_host == 0 || *url->psz_host == 0)
		return false;
	else
		sys->host = url->psz_host;

	if(url->i_port <= 0)
		sys->port = 9982;
	else
		sys->port = url->i_port;

	if(url->psz_username)
		sys->username = url->psz_username;
	if(url->psz_password)
		sys->password = url->psz_password;

	if(url->psz_path == 0 || *(url->psz_path) == '\0')
	{
		msg_Err(demux, "Missing Channel ID!");
		return false;
	}
	else
		sys->channelId = atoi(url->psz_path + 1); // Remove leading '/'

	if(sys->channelId <= 0)
		return false;
		
	return true;
}

static int OpenHTSP(vlc_object_t *obj)
{
	demux_t *demux = (demux_t*)obj;

	demux_sys_t *sys = new demux_sys_t;
	if(unlikely(sys == NULL))
		return VLC_ENOMEM;
	demux->p_sys = sys;

	demux->pf_demux = DemuxHTSP;
	demux->pf_control = ControlHTSP;

	msg_Info(demux, "HTSP plugin loading...");
	
	if(!parseURL(demux))
	{
		msg_Dbg(demux, "Parsing URL failed!");
		CloseHTSP(obj);
		return VLC_EGENERIC;
	}

	if(!ConnectHTSP(demux))
	{
		msg_Dbg(demux, "Connecting to HTS source failed!");
		CloseHTSP(obj);
		return VLC_EGENERIC;
	}
	
	if(!SubscribeHTSP(demux))
	{
		msg_Dbg(demux, "Subscribing to channel failed");
		CloseHTSP(obj);
		return VLC_EGENERIC;
	}

	sys->start = mdate();

	return VLC_SUCCESS;
}

static void CloseHTSP(vlc_object_t *obj)
{
	demux_t *demux = (demux_t*)obj;
	demux_sys_t *sys = demux->p_sys;

	if(!sys)
		return;
	
	delete sys;
	sys = demux->p_sys = 0;
}

static int ControlHTSP(demux_t *demux, int i_query, va_list args)
{
	demux_sys_t *sys = demux->p_sys;

	switch(i_query)
	{
		case DEMUX_CAN_PAUSE:
		case DEMUX_CAN_SEEK:
		case DEMUX_CAN_CONTROL_PACE:
			*va_arg(args, bool*) = false;
			return VLC_SUCCESS;
		case DEMUX_GET_PTS_DELAY:
			*va_arg(args, int64_t*) = INT64_C(1000) * var_InheritInteger(demux, "network-caching") + PTS_DELAY;
			return VLC_SUCCESS;
		case DEMUX_GET_TIME:
			*va_arg(args, int64_t*) = sys->lastPcr - sys->start;
		default:
			return VLC_EGENERIC;
	}
}

/***************************************************
 ****       Actual Demuxing Work Functions      ****
 ***************************************************/

bool ParseSubscriptionStart(demux_t *demux, htsmsg_t *msg)
{
	demux_sys_t *sys = demux->p_sys;

	if(sys->stream != 0)
	{
		for(uint32_t i = 0; i < sys->streamCount; i++)
			es_out_Del(demux->out, sys->stream[i].es);
		delete[] sys->stream;
		sys->stream = 0;
		sys->streamCount = 0;
	}

	htsmsg_t *streams;
	htsmsg_field_t *f;
	if((streams = htsmsg_get_list(msg, "streams")) == 0)
	{
		msg_Err(demux, "Malformed SubscriptionStart!");
		return false;
	}
	
	sys->streamCount = 0;
	HTSMSG_FOREACH(f, streams)
		sys->streamCount++;
	msg_Dbg(demux, "Found %d elementary streams", sys->streamCount);
	
	sys->stream = new hts_stream[sys->streamCount];
	sys->hadIFrame = false;
	sys->pcrStream = 0;
	
	HTSMSG_FOREACH(f, streams)
	{
		uint32_t index;
		std::string type;
		htsmsg_t *sub;
		
		if(f->hmf_type != HMF_MAP)
			continue;
		sub = &f->hmf_msg;
		
		type = htsmsg_get_stdstr(sub, "type");
		if(type.empty())
			continue;
		
		if(htsmsg_get_u32(sub, "index", &index))
			continue;
		int i = index - 1;
		
		es_format_t *fmt = &(sys->stream[i].fmt);
		
		if(type == "AC3")
		{
			es_format_Init(fmt, AUDIO_ES, VLC_CODEC_A52);
		}
		else if(type == "EAC3")
		{
			es_format_Init(fmt, AUDIO_ES, VLC_CODEC_EAC3);
		}
		else if(type == "MPEG2AUDIO")
		{
			es_format_Init(fmt, AUDIO_ES, VLC_CODEC_MPGA);
		}
		else if(type == "AAC")
		{
			es_format_Init(fmt, AUDIO_ES, VLC_CODEC_MP4A);
		}
		else if(type == "AACLATM")
		{
			es_format_Init(fmt, AUDIO_ES, VLC_CODEC_MP4A);
		}
		else if(type == "MPEG2VIDEO")
		{
			es_format_Init(fmt, VIDEO_ES, VLC_CODEC_MP2V);
		}
		else if(type == "H264")
		{
			es_format_Init(fmt, VIDEO_ES, VLC_CODEC_H264);
		}
		else if(type == "DVBSUB")
		{
			es_format_Init(fmt, SPU_ES, VLC_CODEC_DVBS);
		}
		else if(type == "TEXTSUB")
		{
			es_format_Init(fmt, SPU_ES, VLC_CODEC_TEXT);
		}
		else if(type == "TELETEXT")
		{
			es_format_Init(fmt, SPU_ES, VLC_CODEC_TELETEXT);
		}
		else
		{
			sys->stream[i].es = 0;
			continue;
		}
		
		if(fmt->i_cat == VIDEO_ES)
		{
			if(sys->pcrStream == 0)
				sys->pcrStream = index;
		
			uint32_t tmp;
			if(!htsmsg_get_u32(sub, "width", &tmp) && tmp != 0)
				fmt->video.i_width = tmp;
			if(!htsmsg_get_u32(sub, "height", &tmp) && tmp != 0)
				fmt->video.i_height = tmp;
		}
		else if(fmt->i_cat == AUDIO_ES)
		{
			uint32_t tmp;
			if(!htsmsg_get_u32(sub, "channels", &tmp) && tmp != 0)
				fmt->audio.i_physical_channels = tmp;
			if(!htsmsg_get_u32(sub, "rate", &tmp) && tmp != 0)
				fmt->audio.i_rate = tmp;
		}
		
		std::string lang = htsmsg_get_stdstr(sub, "language");
		if(!lang.empty())
		{
			fmt->psz_language = (char*)malloc(lang.length()+1);
			strncpy(fmt->psz_language, lang.c_str(), lang.length());
			fmt->psz_language[lang.length()] = 0;
		}

		sys->stream[i].es = es_out_Add(demux->out, fmt);
		
		msg_Dbg(demux, "Found elementary stream id %d, type %s", index, type.c_str());
	}
	
	if(sys->pcrStream == 0)
		for(uint32_t i = 0; i < sys->streamCount; i++)
			if(sys->stream[i].fmt.i_cat == AUDIO_ES)
				sys->pcrStream = i+1;
	if(sys->pcrStream == 0)
		sys->pcrStream = 1;
	
	es_out_Control(demux->out, ES_OUT_SET_PCR, VLC_TS_0);
	sys->lastPcr = mdate();
	
	return true;
}
	
bool ParseSubscriptionStop(demux_t *demux, htsmsg_t *msg)
{
	VLC_UNUSED(demux);
	VLC_UNUSED(msg);
	return false;
}

bool ParseSubscriptionStatus(demux_t *demux, htsmsg_t *msg)
{
	VLC_UNUSED(demux);
	VLC_UNUSED(msg);
	return true;
}

bool ParseQueueStatus(demux_t *demux, htsmsg_t *msg)
{
	VLC_UNUSED(demux);
	VLC_UNUSED(msg);
	return true;
}

bool ParseSignalStatus(demux_t *demux, htsmsg_t *msg)
{
	VLC_UNUSED(demux);
	VLC_UNUSED(msg);
	return true;
}

bool ParseMuxPacket(demux_t *demux, htsmsg_t *msg)
{
	demux_sys_t *sys = demux->p_sys;

	uint32_t index = 0;
	const void *bin = 0;
	size_t binlen = 0;
	int64_t pts = 0;
	int64_t dts = 0;
	int64_t duration = 0;
	uint32_t frametype = 0;
	
	if(htsmsg_get_u32(msg, "stream", &index) || htsmsg_get_bin(msg, "payload", &bin, &binlen))
	{
		msg_Err(demux, "Malformed Mux Packet!");
		return false;
	}
	
	if(index > sys->streamCount || index == 0)
	{
		htsmsg_print(msg);
		msg_Err(demux, "Invalid stream index detected: %d with %d streams", index, sys->streamCount);
		return false;
	}
	
	if(sys->stream[index - 1].es == 0)
		return true;
	
	block_t *block = block_Alloc(binlen);
	if(unlikely(block == 0))
		return false;
	
	memcpy(block->p_buffer, bin, binlen);
	
	block->i_pts = VLC_TS_INVALID;
	if(!htsmsg_get_s64(msg, "pts", &pts) && pts != 0)
		block->i_pts = pts;
	
	block->i_dts = VLC_TS_INVALID;
	if(!htsmsg_get_s64(msg, "dts", &dts) && dts != 0)
		block->i_dts = dts;
	
	if(!htsmsg_get_s64(msg, "duration", &duration) && duration != 0)
	{
		block->i_length = duration;
	}

	if(pts)
		sys->stream[index - 1].lastPts = pts;
		
	if(dts)
		sys->stream[index - 1].lastPts = dts;
	
	if(sys->stream[index - 1].fmt.i_cat == VIDEO_ES && !htsmsg_get_u32(msg, "frametype", &frametype) && frametype != 0)
	{
		char ft = (char)frametype;
		
		if(!sys->hadIFrame && ft != 'I')
		{
			block_Release(block);
			return true;
		}
		
		if(ft == 'I')
		{
			sys->hadIFrame = true;
			block->i_flags = BLOCK_FLAG_TYPE_I;
		}
		else if(ft == 'B')
			block->i_flags = BLOCK_FLAG_TYPE_B;
		else if(ft == 'P')
			block->i_flags = BLOCK_FLAG_TYPE_P;
	}
	
	if(index == sys->pcrStream && mdate() > sys->lastPcr + PTS_DELAY)
	{
		mtime_t pcr = dts;
		
		for(uint32_t i = 0; i < sys->streamCount; i++)
			if(sys->stream[i].lastDts > 0 && sys->stream[i].lastDts < pcr)
			{
				pcr = sys->stream[i].lastDts;
				sys->pcrStream = i + 1;
				msg_Dbg(demux, "Using Stream %d instead of %d for dts!", i + 1, index);
			}
		
		es_out_Control(demux->out, ES_OUT_SET_PCR, VLC_TS_0 + pcr - PTS_DELAY);
		sys->lastPcr = mdate();
	}

	es_out_Send(demux->out, sys->stream[index - 1].es, block);
	
	return true;
}

static int DemuxHTSP(demux_t *demux)
{
	htsmsg_t *msg = ReadMessage(demux);
	if(!msg)
		return DEMUX_EOF;
	
	std::string method = htsmsg_get_stdstr(msg, "method");
	if(method.empty())
	{
		htsmsg_destroy(msg);
		return DEMUX_ERROR;
	}
	
	uint32_t subs;
	if(htsmsg_get_u32(msg, "subscriptionId", &subs) || subs != 1)
	{
		htsmsg_destroy(msg);
		return DEMUX_OK;
	}
	
	if(method == "subscriptionStart")
	{
		if(!ParseSubscriptionStart(demux, msg))
		{
			htsmsg_destroy(msg);
			return DEMUX_ERROR;
		}
	}
	else if(method == "subscriptionStop")
	{
		if(!ParseSubscriptionStop(demux, msg))
		{
			htsmsg_destroy(msg);
			return DEMUX_ERROR;
		}
	}
	else if(method == "subscriptionStatus")
	{
		if(!ParseSubscriptionStatus(demux, msg))
		{
			htsmsg_destroy(msg);
			return DEMUX_ERROR;
		}
	}
	else if(method == "queueStatus")
	{
		if(!ParseQueueStatus(demux, msg))
		{
			htsmsg_destroy(msg);
			return DEMUX_ERROR;
		}
	}
	else if(method == "signalStatus")
	{
		if(!ParseSignalStatus(demux, msg))
		{
			htsmsg_destroy(msg);
			return DEMUX_ERROR;
		}
	}
	else if(method == "muxpkt")
	{
		if(!ParseMuxPacket(demux, msg))
		{
			htsmsg_destroy(msg);
			return DEMUX_ERROR;
		}
	}
	else
	{
		msg_Dbg(demux, "Ignoring packet of unknown method \"%s\"", method.c_str());
	}
	
	htsmsg_destroy(msg);
		
	return DEMUX_OK;
}
