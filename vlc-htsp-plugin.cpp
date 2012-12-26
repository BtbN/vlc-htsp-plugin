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
	{}

	es_out_id_t *es;
};

struct demux_sys_t
{
	demux_sys_t()
		:start(0)
		,netfd(0)
		,streamCount(0)
		,stream(0)
		,host("")
		,port(0)
		,username("")
		,password("")
		,channelId(0)
		,nextSeqNum(0)
	{}
	
	~demux_sys_t()
	{
		for(std::deque<htsmsg_t*>::iterator it = queue.begin(); it != queue.end(); it++)
			htsmsg_destroy(*it);
		queue.clear();
			
		if(netfd >= 0)
			net_Close(netfd);

		vlc_UrlClean(&url);
	}

	mtime_t start;

	int netfd;

	int streamCount;
	hts_stream **stream;

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
};

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

	void *buf;
	uint32_t len;
	
	if(sys->queue.size())
	{
		htsmsg_t *res = sys->queue.front();
		sys->queue.pop_front();
		return res;
	}
	
	if(net_Read(demux, sys->netfd, NULL, &len, sizeof(len), false) != sizeof(len))
		return 0;

	len = ntohl(len);
	
	if(len == 0)
		return htsmsg_create_map();
		
	buf = malloc(len);
	
	if(net_Read(demux, sys->netfd, NULL, buf, len, false) != len)
	{
		free(buf);
		return 0;
	}
	
	return htsmsg_binary_deserialize(buf, len, buf);
}

#define MAX_QUEUE_SIZE 1000
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
			*va_arg(args, int64_t*) = INT64_C(1000) *var_InheritInteger(demux, "network-caching");
			return VLC_SUCCESS;
		case DEMUX_GET_TIME:
			*va_arg(args, int64_t*) = mdate() - sys->start;
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
		for(int i = 0; i < sys->streamCount; i++)
		{
			es_out_Del(demux->out, sys->stream[i]->es);
			delete sys->stream[i];
		}
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
	
	htsmsg_print(msg);
	printf("\n\n");
	htsmsg_print(streams);
	
	return false;
}
	
bool ParseSubscriptionStop(demux_t *demux, htsmsg_t *msg)
{
	return true;
}

bool ParseSubscriptionStatus(demux_t *demux, htsmsg_t *msg)
{
	return true;
}

bool ParseQueueStatus(demux_t *demux, htsmsg_t *msg)
{
	return true;
}

bool ParseSignalStatus(demux_t *demux, htsmsg_t *msg)
{
	return true;
}

bool ParseMuxPacket(demux_t *demux, htsmsg_t *msg)
{
	return true;
}
 
#define DEMUX_EOF 0
#define DEMUX_OK 1
#define DEMUX_ERROR -1
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
