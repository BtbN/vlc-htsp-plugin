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

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_demux.h>
#include <vlc_access.h>
#include <vlc_charset.h>
#include <vlc_url.h>
#include <vlc_cpu.h>

#include <libhts/net.h>
#include <libhts/sha1.h>
#include <libhts/htsmsg.h>
#include <libhts/htsmsg_binary.h>

int OpenHTSP(vlc_object_t *);
void CloseHTSP(vlc_object_t *);
int DemuxHTSP(demux_t *demux);
int ControlHTSP(demux_t *access, int i_query, va_list args);

vlc_module_begin ()
	set_shortname( "HTSP Protocol" )
	set_description( "TVHeadend HTSP Protocol" )
	set_capability( "access_demux", 0 )
	set_category( CAT_INPUT )
	set_subcategory( SUBCAT_INPUT_ACCESS )
	set_callbacks( OpenHTSP, CloseHTSP )
	add_shortcut( "hts", "htsp" )
vlc_module_end ()

typedef struct hts_stream
{
	es_out_id_t *es;
} hts_stream_t;

struct demux_sys_t
{
	mtime_t start;

	socket_t connection;

	int streamCount;
	hts_stream_t **stream;

	vlc_url_t url;

	char *host;
	uint16_t port;
	char *username;
	char *password;
	int channelId;

	int sessionId;

	uint32_t nextSeqNum;
};

bool parseURL(demux_sys_t *sys, const char *path)
{
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

	sys->username = url->psz_username;
	sys->password = url->psz_password;

	if(url->psz_path == 0 || *(url->psz_path) == '\0')
		sys->channelId = 0;
	else
		sys->channelId = atoi(url->psz_path + 1); // Remove leading '/'

	return true;
}

uint32_t HTSPNextSeqNum(demux_sys_t *sys)
{
	return sys->nextSeqNum++;
}

bool TransmitMessage(demux_sys_t *sys, htsmsg_t *m)
{
	if(!sys || sys->connection < 0)
		return false;
	
	void *buf;
	size_t len;

	if(htsmsg_binary_serialize(m, &buf, &len, -1) < 0)
	{
		htsmsg_destroy(m);
		return false;
	}
	htsmsg_destroy(m);

	htsbuf_queue_t q;
	htsbuf_queue_init(&q, 0);
	htsbuf_append_prealloc(&q, buf, len);
	htsp_tcp_write_queue(sys->connection, &q);
	htsbuf_queue_flush(&q);

	return true;
}

htsmsg_t * ReadResult(demux_sys_t *sys, htsmsg_t *m, bool sequence)
{
	if(!TransmitMessage(sys, m))
		return 0;
}

#define ERRBUF_SIZE 4096
bool ConnectHTSP(demux_sys_t *sys)
{
	char *errbuf = (char*)malloc(ERRBUF_SIZE);
	sys->connection = htsp_tcp_connect(sys->host, sys->port, errbuf, ERRBUF_SIZE, 5000);

	if(sys->connection < 0)
		return false;

	htsmsg_t *m, *cap;
	htsmsg_field_t *f;

	m = htsmsg_create_map();
	htsmsg_add_str(m, "method", "hello");
	htsmsg_add_str(m, "clientname", "VLC media player");
	htsmsg_add_u32(m, "htspversion", 7);

	if((m = ReadResult(sys, m, true)) == 0)
		return false;

	return true;
}

int OpenHTSP(vlc_object_t *obj)
{
	demux_t *demux = (demux_t*)obj;

	demux_sys_t *sys = (demux_sys_t*)malloc(sizeof(demux_sys_t));
	if(unlikely(sys == NULL))
		return VLC_ENOMEM;
	demux->p_sys = sys;

	sys->connection = 0;
	sys->streamCount = 0;
	sys->stream = 0;
	sys->host = 0;
	sys->port = 0;
	sys->username = 0;
	sys->password = 0;
	sys->channelId = 0;
	sys->sessionId = 0;
	sys->nextSeqNum = 0;

	demux->pf_demux = DemuxHTSP;
	demux->pf_control = ControlHTSP;

	if(!parseURL(sys, demux->psz_location))
	{
		CloseHTSP(obj);
		return VLC_EGENERIC;
	}

	if(!ConnectHTSP(sys))
	{
		CloseHTSP(obj);
		return VLC_EGENERIC;
	}

	sys->start = mdate();

	return VLC_SUCCESS;
}

void CloseHTSP(vlc_object_t *obj)
{
	demux_t *demux = (demux_t*)obj;
	demux_sys_t *sys = demux->p_sys;

	if(!sys)
		return;

	if(sys->connection >= 0)
		htsp_tcp_close(sys->connection);

	vlc_UrlClean(&(sys->url));

	free(sys);
}

#define DEMUX_EOF 0
#define DEMUX_OK 1
#define DEMUX_ERROR -1
int DemuxHTSP(demux_t *demux)
{
	demux_sys_t *sys = demux->p_sys;

	return DEMUX_ERROR;
}

int ControlHTSP(demux_t *demux, int i_query, va_list args)
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
			*va_arg(args, int64_t*) = INT64_C(1000) *var_InheritInteger(demux, "live-caching");
			return VLC_SUCCESS;
		case DEMUX_GET_TIME:
			*va_arg(args, int64_t*) = mdate() - sys->start;
		default:
			return VLC_EGENERIC;
	}
}

