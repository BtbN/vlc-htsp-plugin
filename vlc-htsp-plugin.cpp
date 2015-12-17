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

#include "discovery.h"
#include "access.h"
#include "helper.h"

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_demux.h>
#include <vlc_access.h>
#include <vlc_services_discovery.h>


VLC_SD_PROBE_HELPER( "htsp", "Tvheadend HTSP", SD_CAT_LAN )

vlc_module_begin ()
    set_shortname( "HTSP Protocol" )
    set_description( "TVHeadend HTSP Protocol" )
    set_capability( "access_demux", 0 )
    set_category( CAT_INPUT )
    set_subcategory( SUBCAT_INPUT_ACCESS )
    set_callbacks( OpenHTSP, CloseHTSP )
    set_section("Profile", NULL)
    add_bool( CFG_PREFIX"useprofile", false, "Use Profile", "Enable use of streaming profile, fill \"Stream Profile\" with profile name.", false )
    add_string( CFG_PREFIX"profile", "pass", "Stream Profile", "Select stream profile (Added in version 16).", false )
    set_section("Audio", NULL)
    add_bool( CFG_PREFIX"audio-only", false, "Audio Only", "Discards all video streams, if the server supports it.", false )
    set_section("Transcoding", NULL)
    add_bool( CFG_PREFIX"transcode", false, "Enable Transcoding", "Enabled stream transcoding.", false )
    add_string( CFG_PREFIX"vcodec", "", "Video Codec", "Transcode target video codec", false )
    add_string( CFG_PREFIX"acodec", "", "Audio Codec", "Transcode target audio codec", false )
    add_string( CFG_PREFIX"scodec", "", "Subtitle Codec", "Transcode target subtitle codec", false )
    add_string( CFG_PREFIX"tlanguage", "", "Transcode Language", "3 letter language code for transcoder", false )
    add_integer( CFG_PREFIX"tresolution", 0, "Transcoding Resolution", "Target resolution(height) for transcoder, keeping aspect ration", false)
    add_integer( CFG_PREFIX"tchannels", 0, "Transcoding Channels", "Target audio channels for transcoder", false)
    add_integer( CFG_PREFIX"tbandwidth", 0, "Transcoding Bandwidth", "Target stream bandwidth for transcoder", false)
    add_shortcut( "hts", "htsp" )

    add_submodule()
    set_shortname( "HTSP Protocol Discovery" )
    set_description( "TVHeadend HTSP Protocol Discovery" )
    set_category( CAT_PLAYLIST )
    set_subcategory ( SUBCAT_PLAYLIST_SD )
    set_section("Connection", NULL)
    add_integer_with_range( CFG_PREFIX"port", 9982, 1, 65536, "HTSP Server Port", "The port of the HTSP server to connect to", false )
    add_string( CFG_PREFIX"host", "localhost", "HTSP Server Address", "The IP/Hostname of the HTSP server to connect to", false )
    add_string( CFG_PREFIX"user", "", "HTSP Username", "The username for authentication with HTSP Server", false )
    add_password( CFG_PREFIX"pass", "", "HTSP Password", "The password for authentication with HTSP Server", false )
    set_section("Discovery", NULL)
    add_bool( CFG_PREFIX"disconnect", true, "Disconnect Discovery", "Disconnect from hts after initial channel download.", false )
    set_capability ( "services_discovery", 0 )
    set_callbacks ( OpenSD, CloseSD )
    add_shortcut( "hts", "htsp" )

    VLC_SD_PROBE_SUBMODULE
vlc_module_end ()

const char *const cfg_options[] =
{
    "port",
    "host",
    "user",
    "pass",
    NULL
};
