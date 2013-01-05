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

#ifndef H__HELPER_H__
#define H__HELPER_H__

#include <vlc_common.h>
#include <string>
#include <deque>

#include "htsmessage.h"

#define CFG_PREFIX "htsp-"
#define MAX_QUEUE_SIZE 1000
#define READ_TIMEOUT 10

extern const char *const cfg_options[];

class HtsMessage;
struct sys_common_t
{
	sys_common_t()
		:netfd(-1)
		,nextSeqNum(1)
	{}

	virtual ~sys_common_t();

	int netfd;
	uint32_t nextSeqNum;
	std::deque<HtsMessage> queue;
};

bool TransmitMessageEx(vlc_object_t *obj, sys_common_t *sys, HtsMessage m);
HtsMessage ReadMessageEx(vlc_object_t *obj, sys_common_t *sys);
HtsMessage ReadResultEx(vlc_object_t *obj, sys_common_t *sys, HtsMessage m, bool sequence = true);
bool ReadSuccessEx(vlc_object_t *obj, sys_common_t *sys, HtsMessage m, const std::string &action, bool sequence = true);

#define TransmitMessage(a, b, c) TransmitMessageEx(VLC_OBJECT(a), b, c)
#define ReadMessage(a, b) ReadMessageEx(VLC_OBJECT(a), b)
#define ReadResult(a, b, c) ReadResultEx(VLC_OBJECT(a), b, c)
#define ReadSuccess(a, b, c, d) ReadSuccessEx(VLC_OBJECT(a), b, c, d)

#endif
