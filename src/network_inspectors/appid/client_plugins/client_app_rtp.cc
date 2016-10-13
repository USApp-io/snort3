//--------------------------------------------------------------------------
// Copyright (C) 2014-2016 Cisco and/or its affiliates. All rights reserved.
// Copyright (C) 2005-2013 Sourcefire, Inc.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------

// client_app_rtp.cc author Sourcefire Inc.

#include "main/snort_debug.h"
#include "protocols/packet.h"
#include "utils/sflsq.h"
#include "utils/util.h"

#include "application_ids.h"
#include "client_app_api.h"
#include "appid_module.h"

enum RTPState
{
    RTP_STATE_CONNECTION,
    RTP_STATE_CONTINUE
};

#define MAX_REMOTE_SIZE    128
#define NUMBER_OF_PACKETS  3

struct ClientRTPData
{
    RTPState state;
    uint8_t pos;
    uint16_t init_seq;
    uint16_t resp_seq;
    uint8_t init_count;
    uint8_t resp_count;
    uint32_t init_timestamp;
    uint32_t resp_timestamp;
    uint32_t init_ssrc;
    uint32_t resp_ssrc;
};

#pragma pack(1)
struct ClientRTPMsg
{
#if defined(WORDS_BIGENDIAN)
    uint8_t vers : 2,
        padding : 1,
        extension : 1,
        count : 4;
    uint8_t marker : 1,
        payloadtype : 7;
#else
    uint8_t count : 4,
        extension : 1,
        padding : 1,
        vers : 2;
    uint8_t payloadtype : 7,
        marker : 1;
#endif
    uint16_t seq;
    uint32_t timestamp;
    uint32_t ssrc;
};
#pragma pack()

struct RTP_CLIENT_APP_CONFIG
{
    int enabled;
};

THREAD_LOCAL RTP_CLIENT_APP_CONFIG rtp_config;

static CLIENT_APP_RETCODE rtp_init(const IniClientAppAPI* const init_api, SF_LIST* config);
static CLIENT_APP_RETCODE rtp_validate(const uint8_t* data, uint16_t size, const int dir,
    AppIdSession* asd, Packet* pkt, struct Detector* userData);

SO_PUBLIC RNAClientAppModule rtp_client_mod =
{
    "RTP",                 // name
    IpProtocol::UDP,           // proto
    &rtp_init,             // init
    nullptr,               // clean
    &rtp_validate,         // validate
    1,                     // minimum_matches
    nullptr,                // api
    nullptr,                // userData
    0,                      // precedence
    nullptr,                // finalize,
    1,                      // provides_user
    0                       // flow_data_index
};

struct Client_App_Pattern
{
    const uint8_t* pattern;
    unsigned length;
    int index;
    unsigned appId;
};

static Client_App_Pattern patterns[] =
{
    { (const uint8_t*)"\x000\x000", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x001", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x002", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x003", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x004", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x005", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x006", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x007", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x008", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x009", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x00a", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x00b", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x00c", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x00d", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x00e", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x00f", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x010", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x011", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x012", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x013", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x019", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x01a", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x01b", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x01c", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x01f", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x020", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x021", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x022", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x080", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x081", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x082", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x083", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x084", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x085", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x086", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x087", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x088", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x089", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x08a", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x08b", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x08c", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x08d", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x08e", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x08f", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x090", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x091", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x092", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x093", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x099", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x09a", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x09b", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x09c", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x09f", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x0a0", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x0a1", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x000\x0a2", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x000", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x001", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x002", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x003", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x004", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x005", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x006", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x007", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x008", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x009", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x00a", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x00b", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x00c", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x00d", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x00e", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x00f", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x010", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x011", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x012", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x013", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x019", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x01a", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x01b", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x01c", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x01f", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x020", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x021", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x022", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x080", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x081", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x082", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x083", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x084", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x085", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x086", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x087", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x088", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x089", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x08a", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x08b", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x08c", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x08d", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x08e", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x08f", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x090", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x091", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x092", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x093", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x099", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x09a", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x09b", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x09c", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x09f", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x0a0", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x0a1", 2, -1, APP_ID_RTP },
    { (const uint8_t*)"\x080\x0a2", 2, -1, APP_ID_RTP },
};

static AppRegistryEntry appIdRegistry[] =
{
    { APP_ID_RTP, 0 }
};

static CLIENT_APP_RETCODE rtp_init(const IniClientAppAPI* const init_api, SF_LIST* config)
{
    unsigned i;

    rtp_config.enabled = 1;

    if (config)
    {
        SF_LNODE* cursor;
        RNAClientAppModuleConfigItem* item;

        for (item = (RNAClientAppModuleConfigItem*)sflist_first(config, &cursor);
            item;
            item = (RNAClientAppModuleConfigItem*)sflist_next(&cursor))
        {
            DebugFormat(DEBUG_LOG,"Processing %s: %s\n",item->name, item->value);
            if (strcasecmp(item->name, "enabled") == 0)
            {
                rtp_config.enabled = atoi(item->value);
            }
        }
    }

    if (rtp_config.enabled)
    {
        for (i=0; i < sizeof(patterns)/sizeof(*patterns); i++)
        {
            DebugFormat(DEBUG_LOG,"registering patterns: %s: %d\n",
                (const char*)patterns[i].pattern, patterns[i].index);
            init_api->RegisterPattern(&rtp_validate, IpProtocol::UDP, patterns[i].pattern,
                patterns[i].length, patterns[i].index);
        }
    }

    unsigned j;
    for (j=0; j < sizeof(appIdRegistry)/sizeof(*appIdRegistry); j++)
    {
        DebugFormat(DEBUG_LOG,"registering appId: %d\n",appIdRegistry[j].appId);
        init_api->RegisterAppId(&rtp_validate, appIdRegistry[j].appId,
            appIdRegistry[j].additionalInfo);
    }

    return CLIENT_APP_SUCCESS;
}

static CLIENT_APP_RETCODE rtp_validate(const uint8_t* data, uint16_t size, const int dir,
    AppIdSession* asd, Packet*, struct Detector*)
{
    ClientRTPData* fd;
    ClientRTPMsg* hdr;

    if (!size)
        return CLIENT_APP_INPROCESS;

    fd = (ClientRTPData*)rtp_client_mod.api->data_get(asd, rtp_client_mod.flow_data_index);
    if (!fd)
    {
        fd = (ClientRTPData*)snort_calloc(sizeof(ClientRTPData));
        rtp_client_mod.api->data_add(asd, fd, rtp_client_mod.flow_data_index, &snort_free);
        fd->state = RTP_STATE_CONNECTION;
    }

    switch (fd->state)
    {
    case RTP_STATE_CONNECTION:
        if (size < sizeof(ClientRTPMsg))
            return CLIENT_APP_EINVALID;
        hdr = (ClientRTPMsg*)data;
        if (hdr->vers > 2 || hdr->payloadtype > 34)
            return CLIENT_APP_EINVALID;
        if (dir == APP_ID_FROM_INITIATOR)
        {
            fd->init_seq = ntohs(hdr->seq);
            fd->init_timestamp = ntohl(hdr->timestamp);
            fd->init_ssrc = ntohl(hdr->ssrc);
            fd->init_count++;
        }
        else
        {
            fd->resp_seq = ntohs(hdr->seq);
            fd->resp_timestamp = ntohl(hdr->timestamp);
            fd->resp_ssrc = ntohl(hdr->ssrc);
            fd->resp_count++;
        }
        fd->state = RTP_STATE_CONTINUE;
        return CLIENT_APP_INPROCESS;

    case RTP_STATE_CONTINUE:
        if (size < sizeof(ClientRTPMsg))
            return CLIENT_APP_EINVALID;
        hdr = (ClientRTPMsg*)data;
        if (hdr->vers > 2)
            return CLIENT_APP_EINVALID;
        if (hdr->payloadtype > 34)
            return CLIENT_APP_EINVALID;
        if (dir == APP_ID_FROM_INITIATOR)
        {
            if ((ntohs(hdr->seq) != ++fd->init_seq) ||
                (ntohl(hdr->ssrc) != fd->init_ssrc) ||
                (ntohl(hdr->timestamp) < fd->init_timestamp))
                return CLIENT_APP_EINVALID;
            fd->init_timestamp = ntohl(hdr->timestamp);
            if (++fd->init_count < NUMBER_OF_PACKETS)
                return CLIENT_APP_INPROCESS;
        }
        else
        {
            if ((ntohs(hdr->seq) != ++fd->resp_seq) ||
                (ntohl(hdr->ssrc) != fd->resp_ssrc) ||
                (ntohl(hdr->timestamp) < fd->resp_timestamp))
                return CLIENT_APP_EINVALID;
            fd->resp_timestamp = ntohl(hdr->timestamp);
            if (++fd->resp_count < NUMBER_OF_PACKETS)
                return CLIENT_APP_INPROCESS;
        }
        break;

    default:
        return CLIENT_APP_INPROCESS;
    }

    rtp_client_mod.api->add_app(asd, APP_ID_RTP, APP_ID_RTP, nullptr);
    asd->set_session_flags(APPID_SESSION_CLIENT_DETECTED);
    appid_stats.rtp_clients++;
    return CLIENT_APP_SUCCESS;
}

