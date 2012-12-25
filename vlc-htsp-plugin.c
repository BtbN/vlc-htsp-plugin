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
#include <vlc_cpu.h>

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

struct demux_sys_t
{
	mtime_t start;
};

int OpenHTSP(vlc_object_t *obj)
{
	demux_t *demux = (demux_t*)obj;

	demux_sys_t *sys = (demux_sys_t*)malloc(sizeof(demux_sys_t));
	if(unlikely(sys == NULL))
		return VLC_ENOMEM;
	demux->p_sys = sys;

	printf("I'm here!\n");

	demux->pf_demux = DemuxHTSP;
	demux->pf_control = ControlHTSP;

	sys->start = mdate();

	return VLC_SUCCESS;
}

void CloseHTSP(vlc_object_t *obj)
{
	demux_t *demux = (demux_t*)obj;
	demux_sys_t *sys = demux->p_sys;

	printf("No longer here\n");

	free(sys);
}

#define DEMUX_EOF 0
#define DEMUX_OK 1
#define DEMUX_ERROR -1
int DemuxHTSP(demux_t *demux)
{
	demux_sys_t *sys = demux->p_sys;
	printf("I'm demuxing!\n");

	return DEMUX_ERROR;
}

int ControlHTSP(demux_t *demux, int i_query, va_list args)
{
	demux_sys_t *sys = demux->p_sys;
	printf("I'm controling!\n");

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

