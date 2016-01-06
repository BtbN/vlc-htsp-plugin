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
#include <climits>
#include <queue>
#include <atomic>

#include "access.h"
#include "helper.h"
#include "htsmessage.h"
#include "sha1.h"

#include <vlc_common.h>
#include <vlc_demux.h>
#include <vlc_access.h>
#include <vlc_url.h>
#include <vlc_network.h>
#include <vlc_epg.h>
#include <vlc_meta.h>
#include <vlc_input.h>

#define DEMUX_EOF 0
#define DEMUX_OK 1
#define DEMUX_ERROR -1

struct hts_stream
{
    hts_stream()
        :index(0)
        ,es(0)
        ,lastDts(0)
        ,lastPts(0)
        ,ignoreTime(false)
    {}

    uint32_t index;
    es_out_id_t *es;
    es_format_t fmt;
    mtime_t lastDts;
    mtime_t lastPts;
    bool ignoreTime;
};

struct demux_sys_t : public sys_common_t
{
    demux_sys_t()
        :lastPcr(0)
        ,ptsDelay(300000)
        ,currentPcr(0)
        ,tsOffset(0)
        ,tsStart(0)
        ,tsEnd(0)
        ,timeshiftPeriod(0)
        ,streamCount(0)
        ,stream(0)
        ,audioOnly(false)
        ,host("")
        ,port(0)
        ,username("")
        ,password("")
        ,channelId(0)
        ,hadIFrame(false)
        ,drops(0)
        ,epg(0)
        ,thread(0)
        ,requestSpeed(INT_MIN)
        ,requestSeek(-1)
        ,doDisable(false)
    {
        vlc_mutex_init(&queueMutex);
        vlc_cond_init(&queueCond);
        vlc_mutex_init(&disableMutex);
    }

    ~demux_sys_t()
    {
        if(stream)
            delete[] stream;

        vlc_UrlClean(&url);

        vlc_mutex_destroy(&queueMutex);
        vlc_cond_destroy(&queueCond);
        vlc_mutex_destroy(&disableMutex);

        if(epg)
            vlc_epg_Delete(epg);
    }

    mtime_t lastPcr;
    mtime_t ptsDelay;
    mtime_t currentPcr;

    std::atomic<mtime_t> tsOffset;
    std::atomic<mtime_t> tsStart;
    std::atomic<mtime_t> tsEnd;

    uint32_t timeshiftPeriod;

    uint32_t streamCount;
    hts_stream *stream;

    bool audioOnly;

    vlc_url_t url;

    std::string host;
    uint16_t port;
    std::string username;
    std::string password;
    int channelId;

    std::string serverName;
    std::string serverVersion;
    int32_t protoVersion;

    bool hadIFrame;

    uint32_t drops;

    vlc_epg_t *epg;

    vlc_mutex_t queueMutex;
    vlc_cond_t queueCond;
    vlc_thread_t thread;
    std::queue<HtsMessage> msgQueue;
    std::atomic<int> requestSpeed;
    std::atomic<int64_t> requestSeek;

    std::atomic<bool> doDisable;
    vlc_mutex_t disableMutex;
    std::list<int64_t> disables;
};

int SpeedHTSP(demux_t *demux, int state);
int SeekHTSP(demux_t *demux, int64_t time, bool precise);
void * RunHTSP(void *obj);

/***************************************************
 ****       Initialization Functions            ****
 ***************************************************/

bool ConnectHTSP(demux_t *demux)
{
    demux_sys_t *sys = demux->p_sys;

    sys->netfd = net_ConnectTCP(demux, sys->host.c_str(), sys->port);

    if(sys->netfd < 0)
    {
        msg_Err(demux, "net_ConnectTCP failed!");
        return false;
    }

    HtsMap map;
    map.setData("method", "hello");
    map.setData("clientname", "VLC media player");
    map.setData("htspversion", HTSP_PROTO_VERSION);

    HtsMessage m = ReadResult(demux, sys, map.makeMsg());
    if(!m.isValid())
    {
        msg_Err(demux, "ReadResult failed!");
        return false;
    }

    uint32_t chall_len;
    void * chall;

    sys->serverName = m.getRoot()->getStr("servername");
    sys->serverVersion = m.getRoot()->getStr("serverversion");
    sys->protoVersion = m.getRoot()->getU32("htspversion");
    m.getRoot()->getBin("challenge", &chall_len, &chall);

    msg_Info(demux, "Connected to HTSP Server %s, version %s, protocol %d", sys->serverName.c_str(), sys->serverVersion.c_str(), sys->protoVersion);
    if(sys->protoVersion < HTSP_PROTO_VERSION)
    {
        msg_Warn(demux, "TVHeadend is running an older version of HTSP(v%d) than we are(v%d). No effort was made to keep compatible with older versions, update tvh before reporting problems!", sys->protoVersion, HTSP_PROTO_VERSION);
    }
    else if(sys->protoVersion > HTSP_PROTO_VERSION)
    {
        msg_Info(demux, "TVHeadend is running a more recent version of HTSP(v%d) than we are(v%d). Check if there is an update available!", sys->protoVersion, HTSP_PROTO_VERSION);
    }

    if(sys->username.empty())
        return true;

    msg_Info(demux, "Starting authentication...");

    map = HtsMap();
    map.setData("method", "authenticate");
    map.setData("username", sys->username);

    if(sys->password != "" && chall)
    {
        msg_Info(demux, "Authenticating as '%s' with a password", sys->username.c_str());

        HTSSHA1 *shactx = (HTSSHA1*)malloc(hts_sha1_size);
        uint8_t d[20];
        hts_sha1_init(shactx);
        hts_sha1_update(shactx, (const uint8_t *)(sys->password.c_str()), sys->password.length());
        hts_sha1_update(shactx, (const uint8_t *)chall, chall_len);
        hts_sha1_final(shactx, d);

        std::shared_ptr<HtsBin> bin = std::make_shared<HtsBin>();
        bin->setBin(20, d);
        map.setData("digest", bin);

        free(shactx);
    }
    else
        msg_Info(demux, "Authenticating as '%s' without a password", sys->username.c_str());

    if(chall)
        free(chall);

    msg_Info(demux, "Sending authentication...");

    bool res = ReadSuccess(demux, sys, map.makeMsg(), "auth");
    if(res)
        msg_Info(demux, "Successfully authenticated!");
    else
        msg_Err(demux, "Authentication failed!");

    return res;
}

void PopulateEPG(demux_t *demux)
{
    demux_sys_t *sys = demux->p_sys;

    HtsMap map;
    map.setData("method", "getEvents");
    map.setData("channelId", sys->channelId);

    HtsMessage res = ReadResult(demux, sys, map.makeMsg());
    if(!res.isValid())
        return;

    sys->epg = vlc_epg_New(0);

    std::shared_ptr<HtsList> events = res.getRoot()->getList("events");
    for(uint32_t i = 0; i < events->count(); i++)
    {
        std::shared_ptr<HtsData> tmp = events->getData(i);
        if(!tmp->isMap())
            continue;
        std::shared_ptr<HtsMap> event = std::static_pointer_cast<HtsMap>(tmp);

        if(event->getU32("channelId") != (uint32_t)sys->channelId)
            continue;

        int64_t start = event->getS64("start");
        int64_t stop = event->getS64("stop");
        int duration = stop - start;

#if CHECK_VLC_VERSION(2,1)
        vlc_epg_AddEvent(sys->epg, start, duration, event->getStr("title").c_str(), event->getStr("summary").c_str(), event->getStr("description").c_str(), 0);
#else
        vlc_epg_AddEvent(sys->epg, start, duration, event->getStr("title").c_str(), event->getStr("summary").c_str(), event->getStr("description").c_str());
#endif

        int64_t now = time(0);
        if(now >= start && now < stop)
            vlc_epg_SetCurrent(sys->epg, start);
    }
}

bool SubscribeHTSP(demux_t *demux)
{
    demux_sys_t *sys = demux->p_sys;

    HtsMap map;
    map.setData("method", "subscribe");
    map.setData("channelId", sys->channelId);
    map.setData("subscriptionId", 1);
    map.setData("queueDepth", 5*1024*1024);
    map.setData("timeshiftPeriod", (uint32_t)~0);
    map.setData("normts", 1);

    if(var_InheritBool(demux, CFG_PREFIX"useprofile"))
    {
        char *s;

        s = var_InheritString(demux, CFG_PREFIX"profile");
        if(s && *s)
            map.setData("profile", s);
        if(s)
            free(s);
    }

    if(var_InheritBool(demux, CFG_PREFIX"transcode"))
    {
        char *s;
        int64_t i;

        s = var_InheritString(demux, CFG_PREFIX"vcodec");
        if(s && *s)
            map.setData("videoCodec", s);
        if(s)
            free(s);

        s = var_InheritString(demux, CFG_PREFIX"acodec");
        if(s && *s)
            map.setData("audioCodec", s);
        if(s)
            free(s);

        s = var_InheritString(demux, CFG_PREFIX"scodec");
        if(s && *s)
            map.setData("subtitleCodec", s);
        if(s)
            free(s);

        s = var_InheritString(demux, CFG_PREFIX"tlanguage");
        if(s && *s)
            map.setData("language", s);
        if(s)
            free(s);

        i = var_InheritInteger(demux, CFG_PREFIX"tresolution");
        if(i)
            map.setData("maxResolution", i);

        i = var_InheritInteger(demux, CFG_PREFIX"tchannels");
        if(i)
            map.setData("channels", i);

        i = var_InheritInteger(demux, CFG_PREFIX"tbandwidth");
        if(i)
            map.setData("bandwidth", i);
    }

    HtsMessage res = ReadResult(demux, sys, map.makeMsg());
    if(!res.isValid())
        return false;

    sys->timeshiftPeriod = res.getRoot()->getU32("timeshiftPeriod");

    msg_Info(demux, "Successfully subscribed to channel %d", sys->channelId);

    return true;
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

    if(url->psz_path == 0 || *(url->psz_path) == '\0' || *(url->psz_path + 1) == '\0')
        sys->channelId = 0;
    else
        sys->channelId = atoi(url->psz_path + 1); // Remove leading '/'

    return true;
}

int OpenHTSP(vlc_object_t *obj)
{
    demux_t *demux = (demux_t*)obj;

    demux_sys_t *sys = new demux_sys_t;
    if(unlikely(sys == NULL))
        return VLC_ENOMEM;
    demux->p_sys = sys;

    demux->pf_demux = DemuxHTSP;
    demux->pf_control = ControlHTSP;

    sys->audioOnly = var_InheritBool(demux, CFG_PREFIX"audio-only");

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

    if(sys->channelId == 0)
    {
        msg_Err(demux, "HTSP ChannelID 0 is invalid!");
        return VLC_EGENERIC;
    }

    PopulateEPG(demux);

    if(!SubscribeHTSP(demux))
    {
        msg_Dbg(demux, "Subscribing to channel failed");
        CloseHTSP(obj);
        return VLC_EGENERIC;
    }

    if(vlc_clone(&sys->thread, RunHTSP, demux, VLC_THREAD_PRIORITY_INPUT))
    {
        delete sys;
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}

void CloseHTSP(vlc_object_t *obj)
{
    demux_t *demux = (demux_t*)obj;
    demux_sys_t *sys = demux->p_sys;

    if(!sys)
        return;

    if(sys->thread)
    {
        vlc_cancel(sys->thread);
        vlc_join(sys->thread, 0);
        sys->thread = 0;
    }

    delete sys;
    sys = demux->p_sys = 0;
}

int ControlHTSP(demux_t *demux, int i_query, va_list args)
{
    demux_sys_t *sys = demux->p_sys;

    bool tb = false;
    int64_t ti = 0;
    int tint = 0;
    double td = 0.0;
    double totalTime = sys->tsEnd - sys->tsStart;

    switch(i_query)
    {
        case DEMUX_CAN_PAUSE:
        case DEMUX_CAN_SEEK:
        case DEMUX_CAN_CONTROL_RATE:
            *va_arg(args, bool*) = (sys->timeshiftPeriod > 0);
            return VLC_SUCCESS;
        case DEMUX_CAN_CONTROL_PACE:
            *va_arg(args, bool*) = false;
            return VLC_SUCCESS;
        case DEMUX_SET_PAUSE_STATE:
            msg_Dbg(demux, "SET_PAUSE_STATE Queried");
            if(sys->timeshiftPeriod <= 0)
                return VLC_EGENERIC;
            tb = (bool)va_arg(args, int);
            return SpeedHTSP(demux, (tb?0:100));
        case DEMUX_SET_TIME:
            msg_Dbg(demux, "SET_TIME Queried");
            if(sys->timeshiftPeriod <= 0)
                return VLC_EGENERIC;
            ti = va_arg(args, int64_t) + sys->tsStart;
            tb = (bool)va_arg(args, int);
            return SeekHTSP(demux, ti, tb);
        case DEMUX_SET_RATE:
            msg_Dbg(demux, "SET_RATE Queried");
            if(sys->timeshiftPeriod <= 0)
                return VLC_EGENERIC;
            tint = *va_arg(args, int*);
            tint = (100 * INPUT_RATE_DEFAULT) / tint;
            msg_Dbg(demux, "Rate queried to value of %d", tint);
            return SpeedHTSP(demux, tint);
        case DEMUX_GET_LENGTH:
            if(sys->currentPcr == 0 || sys->timeshiftPeriod <= 0)
                return VLC_EGENERIC;
            *va_arg(args, int64_t*) = totalTime;
            return VLC_SUCCESS;
        case DEMUX_GET_POSITION:
            if(sys->currentPcr == 0 || sys->timeshiftPeriod <= 0)
                return VLC_EGENERIC;
            *va_arg(args, double*) = (sys->currentPcr - sys->tsStart) / totalTime;
            return VLC_SUCCESS;
        case DEMUX_SET_POSITION:
            msg_Dbg(demux, "SET_POSITION Queried");
            if(sys->timeshiftPeriod <= 0)
                return VLC_EGENERIC;
            td = va_arg(args, double);
            tb = (bool)va_arg(args, int);
            return SeekHTSP(demux, td * totalTime + sys->tsStart, tb);
        case DEMUX_GET_PTS_DELAY:
            *va_arg(args, int64_t*) = INT64_C(1000) * var_InheritInteger(demux, "network-caching");
            return VLC_SUCCESS;
        case DEMUX_GET_TIME:
            if(sys->currentPcr == 0)
                return VLC_EGENERIC;
            *va_arg(args, int64_t*) = sys->currentPcr - sys->tsStart;
            return VLC_SUCCESS;
        default:
            return VLC_EGENERIC;
    }
}

/***************************************************
 ****       Actual Demuxing Work Functions      ****
 ***************************************************/

void ParseTimeshiftStatus(demux_t *demux, HtsMessage &msg)
{
    demux_sys_t *sys = demux->p_sys;

    sys->tsOffset = msg.getRoot()->getS64("shift");
    sys->tsStart = msg.getRoot()->getS64("start");
    sys->tsEnd = msg.getRoot()->getS64("end");
}

void * RunHTSP(void *obj)
{
    demux_t *demux = (demux_t*)obj;
    demux_sys_t *sys = demux->p_sys;
    std::list<int64_t> oldDisable;

    for(;;)
    {
        HtsMessage msg = ReadMessage(demux, sys);
        if(!msg.isValid())
        {
            vlc_mutex_lock(&sys->queueMutex);
            sys->msgQueue.push(HtsMessage());
            vlc_cond_signal(&sys->queueCond);
            vlc_mutex_unlock(&sys->queueMutex);
            return 0;
        }

        std::string method = msg.getRoot()->getStr("method");
        uint32_t subs = msg.getRoot()->getU32("subscriptionId");

        if(method == "timeshiftStatus" && subs == 1)
        {
            ParseTimeshiftStatus(demux, msg);
        }
        else
        {
            vlc_mutex_lock(&sys->queueMutex);
            sys->msgQueue.push(msg);
            vlc_cond_signal(&sys->queueCond);
            vlc_mutex_unlock(&sys->queueMutex);
        }

        if(sys->requestSpeed != INT_MIN)
        {
            HtsMap map;
            map.setData("method", "subscriptionSpeed");
            map.setData("subscriptionId", 1);
            map.setData("speed", (int)sys->requestSpeed);

            ReadSuccess(demux, sys, map.makeMsg(), "set speed");

            sys->requestSpeed = INT_MIN;
        }

        if(sys->requestSeek >= 0)
        {
            HtsMap map;
            map.setData("method", "subscriptionSeek");
            map.setData("subscriptionId", 1);
            map.setData("time", (int64_t)sys->requestSeek);
            map.setData("absolute", 1);

            ReadSuccess(demux, sys, map.makeMsg(), "seek");

            sys->requestSeek = -1;
        }

        if(sys->doDisable)
        {
            vlc_mutex_lock(&sys->disableMutex);

            std::shared_ptr<HtsList> enable = std::make_shared<HtsList>();
            for(auto it = oldDisable.begin(); it != oldDisable.end(); ++it)
                enable->appendData(std::make_shared<HtsInt>(*it));

            std::shared_ptr<HtsList> disable = std::make_shared<HtsList>();;

            for(auto it = sys->disables.begin(); it != sys->disables.end(); ++it)
                disable->appendData(std::make_shared<HtsInt>(*it));

            HtsMap map;
            map.setData("method", "subscriptionFilterStream");
            map.setData("subscriptionId", 1);
            map.setData("enable", enable);
            map.setData("disable", disable);

            if(!oldDisable.empty() || !sys->disables.empty())
                ReadSuccess(demux, sys, map.makeMsg(), "filterStream");

            sys->doDisable = false;
            oldDisable = sys->disables;
            vlc_mutex_unlock(&sys->disableMutex);
        }
    }

    return 0;
}

int SeekHTSP(demux_t *demux, int64_t time, bool precise)
{
    VLC_UNUSED(precise);

    demux_sys_t *sys = demux->p_sys;
    if(sys->timeshiftPeriod == 0)
        return VLC_EGENERIC;

    if(sys->requestSeek >= 0)
        return VLC_EGENERIC;

    sys->requestSeek = time;

    return VLC_SUCCESS;
}

int SpeedHTSP(demux_t *demux, int speed)
{
    demux_sys_t *sys = demux->p_sys;
    if(sys->timeshiftPeriod == 0)
        return VLC_EGENERIC;

    if(sys->requestSpeed != INT_MIN)
        return VLC_EGENERIC;

    sys->requestSpeed = speed;

    return VLC_SUCCESS;
}

bool ParseSubscriptionStart(demux_t *demux, HtsMessage &msg)
{
    demux_sys_t *sys = demux->p_sys;

    if(sys->stream != 0)
    {
        for(uint32_t i = 0; i < sys->streamCount; i++)
            if(sys->stream[i].es != 0)
                es_out_Del(demux->out, sys->stream[i].es);
        delete[] sys->stream;
        sys->stream = 0;
        sys->streamCount = 0;
    }

    if(msg.getRoot()->contains("sourceinfo") && sys->epg != 0)
    {
        std::shared_ptr<HtsMap> srcinfo = msg.getRoot()->getMap("sourceinfo");

        vlc_meta_t *meta = vlc_meta_New();
        vlc_meta_SetTitle(meta, srcinfo->getStr("service").c_str());
        es_out_Control(demux->out, ES_OUT_SET_GROUP_META, (int)sys->channelId, meta);
        vlc_meta_Delete(meta);

        es_out_Control(demux->out, ES_OUT_SET_GROUP_EPG, (int)sys->channelId, sys->epg);
        vlc_epg_Delete(sys->epg);
        sys->epg = 0;
    }

    std::shared_ptr<HtsList> streams = msg.getRoot()->getList("streams");
    if(streams->count() <= 0)
    {
        msg_Err(demux, "Malformed SubscriptionStart!");
        return false;
    }

    sys->streamCount = streams->count();
    msg_Dbg(demux, "Found %d elementary streams", sys->streamCount);

    sys->stream = new hts_stream[sys->streamCount];
    sys->hadIFrame = false;
    sys->lastPcr = 0;
    sys->currentPcr = 0;
    sys->tsOffset = 0;

    vlc_mutex_lock(&sys->disableMutex);
    sys->disables.clear();

    for(uint32_t jj = 0; jj < streams->count(); jj++)
    {
        std::shared_ptr<HtsData> sub = streams->getData(jj);
        if(!sub->isMap())
            continue;
        std::shared_ptr<HtsMap> map = std::static_pointer_cast<HtsMap>(sub);

        std::string type = map->getStr("type");
        if(type.empty())
            continue;

        if(!map->contains("index"))
            continue;

        uint32_t index = map->getU32("index");
        sys->stream[jj].index = index;

        es_format_t *fmt = &(sys->stream[jj].fmt);

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
        else if(type == "VORBIS")
        {
            es_format_Init(fmt, AUDIO_ES, VLC_CODEC_VORBIS);
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
            sys->stream[jj].ignoreTime = true;
        }
        else if(type == "TEXTSUB")
        {
            es_format_Init(fmt, SPU_ES, VLC_CODEC_TEXT);
            sys->stream[jj].ignoreTime = true;
        }
        else if(type == "TELETEXT")
        {
            es_format_Init(fmt, SPU_ES, VLC_CODEC_TELETEXT);
            sys->stream[jj].ignoreTime = true;
        }
        else
        {
            sys->stream[jj].es = 0;
            sys->stream[jj].ignoreTime = true;
            continue;
        }

        if(fmt->i_cat == VIDEO_ES)
        {
            if(sys->audioOnly)
            {
                sys->stream[jj].es = 0;
                sys->disables.push_back(index);
                continue;
            }

            fmt->video.i_width = map->getU32("width");
            fmt->video.i_height = map->getU32("height");
        }
        else if(fmt->i_cat == AUDIO_ES)
        {
            fmt->audio.i_physical_channels = map->getU32("channels");
            fmt->audio.i_rate = map->getU32("rate");
        }

        void *meta = 0;
        uint32_t metalen = 0;
        map->getBin("meta", &metalen, &meta);

        if(meta)
        {
            fmt->i_extra = metalen;
            fmt->p_extra = meta;
        }

        std::string lang = map->getStr("language");
        if(!lang.empty())
        {
            fmt->psz_language = (char*)malloc(lang.length()+1);
            strncpy(fmt->psz_language, lang.c_str(), lang.length());
            fmt->psz_language[lang.length()] = 0;
        }

        fmt->i_group = sys->channelId;

        sys->stream[jj].es = es_out_Add(demux->out, fmt);

        msg_Dbg(demux, "Found elementary stream id %d, type %s", index, type.c_str());
    }

    sys->doDisable = true;
    vlc_mutex_unlock(&sys->disableMutex);

    return true;
}

bool ParseSubscriptionStop(demux_t *demux, HtsMessage &msg)
{
    msg_Info(demux, "HTS Subscription Stop: subscriptionId: %d, status: %s", msg.getRoot()->getU32("subscriptionId"), msg.getRoot()->getStr("status").c_str());
    return false;
}

bool ParseSubscriptionStatus(demux_t *demux, HtsMessage &msg)
{
    msg_Dbg(demux, "HTS Subscription Status: subscriptionId: %d, status: %s", msg.getRoot()->getU32("subscriptionId"), msg.getRoot()->getStr("status").c_str());
    return true;
}

bool ParseQueueStatus(demux_t *demux, HtsMessage &msg)
{
    demux_sys_t *sys = demux->p_sys;

    uint32_t drops = msg.getRoot()->getU32("Bdrops") + msg.getRoot()->getU32("Pdrops") + msg.getRoot()->getU32("Idrops");
    if(drops > sys->drops)
    {

        msg_Warn(demux, "Can't keep up! HTS dropped %d frames!", drops - sys->drops);
        msg_Warn(demux, "HTS Queue Status: subscriptionId: %d, Packets: %d, Bytes: %d, Delay: %lld, Bdrops: %d, Pdrops: %d, Idrops: %d",
            msg.getRoot()->getU32("subscriptionId"),
            msg.getRoot()->getU32("packets"),
            msg.getRoot()->getU32("bytes"),
            (long long int)msg.getRoot()->getS64("delay"),
            msg.getRoot()->getU32("Bdrops"),
            msg.getRoot()->getU32("Pdrops"),
            msg.getRoot()->getU32("Idrops"));

        sys->drops += drops;
    }
    return true;
}

bool ParseSignalStatus(demux_t *demux, HtsMessage &msg)
{
    VLC_UNUSED(demux);
    VLC_UNUSED(msg);
    return true;
}

bool ParseMuxPacket(demux_t *demux, HtsMessage &msg)
{
    demux_sys_t *sys = demux->p_sys;

    uint32_t index = msg.getRoot()->getU32("stream");

    vlc_mutex_lock(&sys->disableMutex);
    for(auto it = sys->disables.begin(); it != sys->disables.end(); ++it)
    {
        if(*it == index)
        {
            vlc_mutex_unlock(&sys->disableMutex);
            return true;
        }
    }
    vlc_mutex_unlock(&sys->disableMutex);

    void *bin = 0;
    uint32_t binlen = 0;
    msg.getRoot()->getBin("payload", &binlen, &bin);

    int64_t pts = 0;
    int64_t dts = 0;

    uint32_t frametype = 0;

    if(bin == 0)
    {
        msg_Err(demux, "Malformed Mux Packet!");
        return false;
    }

    if(index == 0 || binlen == 0)
    {
        free(bin);
        msg_Err(demux, "Malformed Mux Packet!");
        return false;
    }

    if(index == 0)
    {
        free(bin);
        msg_Err(demux, "Invalid stream index detected: %d with %d streams", index, sys->streamCount);
        return false;
    }

    int streamIndex = -1;
    for(uint32_t i = 0; i < sys->streamCount; i++)
    {
        if (index == sys->stream[i].index)
        {
            streamIndex = i;
            break;
        }
    }

    if (streamIndex == -1)
    {
        msg_Err(demux, "Unknown stream index %u!", index);
        return false;
    }

    if(sys->stream[streamIndex].es == 0)
    {
        free(bin);
        return true;
    }

    block_t *block = block_Alloc(binlen);
    if(unlikely(block == 0))
    {
        free(bin);
        return false;
    }

    memcpy(block->p_buffer, bin, binlen);
    free(bin);
    bin = 0;

    pts = block->i_pts = VLC_TS_INVALID;
    if(msg.getRoot()->contains("pts"))
        pts = block->i_pts = msg.getRoot()->getS64("pts");

    dts = block->i_dts = VLC_TS_INVALID;
    if(msg.getRoot()->contains("dts"))
        dts = block->i_dts = msg.getRoot()->getS64("dts");

    int64_t duration = msg.getRoot()->getS64("duration");
    if(duration != 0)
        block->i_length = duration;

    if(pts > 0 && !sys->stream[streamIndex].ignoreTime)
        sys->stream[streamIndex].lastPts = pts;
    if(dts > 0 && !sys->stream[streamIndex].ignoreTime)
        sys->stream[streamIndex].lastDts = dts;

    frametype = msg.getRoot()->getU32("frametype");
    if(sys->stream[streamIndex].fmt.i_cat == VIDEO_ES && frametype != 0)
    {
        char ft = (char)frametype;

        if(!sys->hadIFrame && ft != 'I')
        {
            block_Release(block);
            free(bin);
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

    mtime_t pcr = 0;
    for(uint32_t i = 0; i < sys->streamCount; i++)
    {
        if(sys->stream[i].lastDts > 0 && (sys->stream[i].lastDts < pcr || pcr == 0))
        {
            pcr = sys->stream[i].lastDts;
        }
    }

    if(pcr > sys->currentPcr)
        sys->currentPcr = pcr;

    if(pcr > 0)
    {
        if(sys->lastPcr == 0)
        {
            sys->lastPcr = pcr;
        }
        else if(pcr > sys->lastPcr + sys->ptsDelay && pcr > 0)
        {
            es_out_Control(demux->out, ES_OUT_SET_PCR, VLC_TS_0 + pcr);
            sys->lastPcr = pcr;
        }
    }

    es_out_Send(demux->out, sys->stream[streamIndex].es, block);

    return true;
}

bool ParseSubscriptionSkip(demux_t *demux, HtsMessage &msg)
{
    demux_sys_t *sys = demux->p_sys;

    if(msg.getRoot()->contains("error") || msg.getRoot()->contains("size") || !msg.getRoot()->contains("time"))
        return true;

    int64_t newTime = msg.getRoot()->getS64("time");

    if(!msg.getRoot()->getU32("absolute"))
        newTime += sys->currentPcr;

    msg_Info(demux, "SubscriptionSkip: newTime: %lld, base: %s", (long long int)newTime, (msg.getRoot()->getU32("absolute"))?"abs":"rel");

    es_out_Control(demux->out, ES_OUT_RESET_PCR);

    msg_Info(demux, "PCR Reset done");

    sys->lastPcr = 0;
    sys->currentPcr = 0;

    sys->tsOffset = 0;

    return true;
}

int ParseSubscriptionSpeed(demux_t *demux, HtsMessage &msg)
{
    VLC_UNUSED(demux);
    VLC_UNUSED(msg);
    return true;
}

int DemuxHTSP(demux_t *demux)
{
    demux_sys_t *sys = demux->p_sys;
    if(sys->channelId == 0)
        return DEMUX_EOF;

    vlc_mutex_lock(&sys->queueMutex);
    if(sys->msgQueue.size() == 0)
        vlc_cond_wait(&sys->queueCond, &sys->queueMutex);
    if(sys->msgQueue.size() == 0)
    {
        vlc_mutex_unlock(&sys->queueMutex);
        return DEMUX_OK;
    }
    HtsMessage msg = sys->msgQueue.front();
    sys->msgQueue.pop();
    vlc_mutex_unlock(&sys->queueMutex);
    if(!msg.isValid())
        return DEMUX_EOF;

    std::string method = msg.getRoot()->getStr("method");
    if(method.empty())
        return DEMUX_ERROR;

    uint32_t subs = msg.getRoot()->getU32("subscriptionId");
    if(subs != 1)
        return DEMUX_OK;

    if(method == "muxpkt")
    {
        if(!ParseMuxPacket(demux, msg))
        {
            return DEMUX_ERROR;
        }
    }
    else if(method == "subscriptionStart")
    {
        if(!ParseSubscriptionStart(demux, msg))
        {
            return DEMUX_ERROR;
        }
    }
    else if(method == "subscriptionStop")
    {
        if(!ParseSubscriptionStop(demux, msg))
        {
            return DEMUX_ERROR;
        }
    }
    else if(method == "subscriptionStatus")
    {
        if(!ParseSubscriptionStatus(demux, msg))
        {
            return DEMUX_ERROR;
        }
    }
    else if(method == "queueStatus")
    {
        if(!ParseQueueStatus(demux, msg))
        {
            return DEMUX_ERROR;
        }
    }
    else if(method == "signalStatus")
    {
        if(!ParseSignalStatus(demux, msg))
        {
            return DEMUX_ERROR;
        }
    }
    else if(method == "subscriptionSkip")
    {
        if(!ParseSubscriptionSkip(demux, msg))
        {
            return DEMUX_ERROR;
        }
    }
    else if(method == "subscriptionSpeed")
    {
        if(!ParseSubscriptionSpeed(demux, msg))
        {
            return DEMUX_ERROR;
        }
    }
    else
    {
        msg_Warn(demux, "Ignoring packet of unknown method \"%s\"", method.c_str());
    }

    return DEMUX_OK;
}
