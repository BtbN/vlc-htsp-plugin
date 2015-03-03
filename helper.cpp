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

#include <ctime>

#include "helper.h"
#include "htsmessage.h"

#include <vlc_common.h>
#include <vlc_network.h>


sys_common_t::~sys_common_t()
{
    if(netfd >= 0)
        net_Close(netfd);
}

uint32_t HTSPNextSeqNum(sys_common_t *sys)
{
    uint32_t res = sys->nextSeqNum++;
    if(sys->nextSeqNum > 2147483647)
        sys->nextSeqNum = 1;
    return res;
}

bool TransmitMessageEx(vlc_object_t *obj, sys_common_t *sys, HtsMessage m)
{
    if(sys->netfd < 0)
    {
        msg_Dbg(obj, "Invalid netfd in TransmitMessage");
        return false;
    }

    void *buf;
    uint32_t len;

    if(!m.Serialize(&len, &buf))
    {
        msg_Dbg(obj, "Serialising message failed");
        return false;
    }

    if(net_Write(obj, sys->netfd, NULL, buf, len) != (ssize_t)len)
    {
        msg_Dbg(obj, "net_Write failed");
        return false;
    }

    free(buf);

    return true;
}

HtsMessage ReadMessageEx(vlc_object_t *obj, sys_common_t *sys)
{
    char *buf;
    uint32_t len;
    ssize_t readSize;

    if(sys->queue.size())
    {
        HtsMessage res = sys->queue.front();
        sys->queue.pop_front();
        return res;
    }

    if(sys->netfd < 0)
    {
        msg_Dbg(obj, "ReadMessage on closed netfd");
        return HtsMessage();
    }

    if((readSize = net_Read(obj, sys->netfd, NULL, &len, sizeof(len), true)) != sizeof(len))
    {
        net_Close(sys->netfd);
        sys->netfd = -1;

        if(readSize == 0)
        {
            msg_Err(obj, "Size Read EOF!");
            return HtsMessage();
        }
        else if(readSize < 0)
        {
            msg_Err(obj, "Data Read ERROR!");
            return HtsMessage();
        }

        msg_Err(obj, "Error reading size: %m");
        return HtsMessage();
    }

    len = ntohl(len);
    if(len == 0)
        return HtsMessage();

    buf = (char*)malloc(len);

    if((readSize = net_Read(obj, sys->netfd, NULL, buf, len, true)) != (ssize_t)len)
    {
        net_Close(sys->netfd);
        sys->netfd = -1;

        if(readSize == 0)
        {
            msg_Err(obj, "Data Read EOF!");
            return HtsMessage();
        }
        else if(readSize < 0)
        {
            msg_Err(obj, "Data Read ERROR!");
            return HtsMessage();
        }

        msg_Err(obj, "Error reading data: %m");
        return HtsMessage();
    }

    HtsMessage result = HtsMessage::Deserialize(len, buf);
    free(buf);
    return result;
}

HtsMessage ReadResultEx(vlc_object_t *obj, sys_common_t *sys, HtsMessage m, bool sequence)
{
    uint32_t iSequence = 0;
    if(sequence)
    {
        iSequence = HTSPNextSeqNum(sys);
        m.getRoot()->setData("seq", iSequence);
    }

    if(!TransmitMessageEx(obj, sys, m))
    {
        msg_Err(obj, "TransmitMessage failed!");
        return HtsMessage();
    }

    std::deque<HtsMessage> queue;
    sys->queue.swap(queue);

    while((m = ReadMessageEx(obj, sys)).isValid())
    {
        if(!sequence)
            break;
        if(m.getRoot()->contains("seq") && m.getRoot()->getU32("seq") == iSequence)
            break;

        queue.push_back(m);
        if(queue.size() >= MAX_QUEUE_SIZE)
        {
            msg_Err(obj, "Max queue size reached!");
            sys->queue.swap(queue);
            return HtsMessage();
        }
    }

    sys->queue.swap(queue);

    if(!m.isValid())
    {
        msg_Err(obj, "ReadMessage failed!");
        return HtsMessage();
    }

    if(m.getRoot()->contains("error"))
    {
        msg_Err(obj, "HTSP Error: %s", m.getRoot()->getStr("error").c_str());
        return HtsMessage();
    }
    if(m.getRoot()->getU32("noaccess") != 0)
    {
        msg_Err(obj, "Access Denied");
        return HtsMessage();
    }

    return m;
}

bool ReadSuccessEx(vlc_object_t *obj, sys_common_t *sys, HtsMessage m, const std::string &action, bool sequence)
{
    if(!ReadResultEx(obj, sys, m, sequence).isValid())
    {
        msg_Err(obj, "ReadSuccess - failed to %s", action.c_str());
        return false;
    }
    return true;
}
