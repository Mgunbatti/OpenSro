#pragma once

#include <winsock2.h>
#include <windows.h>
#include <map>
#include <string>

#include "BSLib/ChunkAllocator.h"
#include "XTrap.h"

// The clients logged in to this AgentServer. A client session becomes a user when its 0x6103
// login carries a valid token (Token.h); its packets are then forwarded to the game server of the
// shard (sUser::wTargetBodyID) with the user's JID appended.

#pragma pack(push, 1)
// The 6 bytes after the content ID of 0x6103 (the client's network adapter address).
struct AGENT_MAC_ADDRESS
{
    DWORD   dwLow;      // +0
    WORD    wHigh;      // +4
};
#pragma pack(pop)

C_ASSERT(sizeof(AGENT_MAC_ADDRESS) == 6);

// sUser (0x190 bytes), from g_UserPool.
struct sUser
{
    // sub_40BA90 (the pool's element constructor)
    sUser()
    {
        dwMacLow = 0;
        wMacHigh = 0;
        bInUse = 0;
    }

    // sUser::vf00 (0x40bad0): back to g_UserPool (only once, see bInUse).
    virtual void __stdcall Release();                               // 0

    // sub_401090: restarts the traffic filter count.
    void ResetTrafficCounter()
    {
        dwRecvCount = 0;
        dwRecvTick = GetTickCount();
    }

    DWORD           dwSessionID;        // +4   client session
    DWORD           dwJID;              // +8   account (from the token)
    BYTE            bySecPrimary;       // +C   security groups of the token
    BYTE            bySecContent;       // +D
    WORD            wHomeBodyID;        // +E   the shard's manager body (game server)
    WORD            wTargetBodyID;      // +10  body the client's packets go to (0xA208 moves it)
    WORD            w12;                // +12
    DWORD           dwIP;               // +14  client address (network order)
    BYTE            byLoginResult;      // +18  second byte of a successful 0xA201
    BYTE            ab19[3];            // +19
    std::string     strID;              // +1C  account name
    DWORD           dwMacLow;           // +38  AGENT_MAC_ADDRESS
    WORD            wMacHigh;           // +3C
    WORD            w3E;                // +3E
    DWORD           dwRecvCount;        // +40  traffic filter: packets since dwRecvTick
    DWORD           dwRecvTick;         // +44
    BYTE            abXTrapSession[XTRAP_SESSION_SIZE];     // +48
    DWORD           dwXTrapTick;        // +188 last challenge (or the session start)
    BYTE            bInUse;             // +18C set by UserPool_Alloc
};

C_ASSERT(FIELD_OFFSET(sUser, dwSessionID) == 0x4);
C_ASSERT(FIELD_OFFSET(sUser, wHomeBodyID) == 0xE);
C_ASSERT(FIELD_OFFSET(sUser, dwIP) == 0x14);
C_ASSERT(FIELD_OFFSET(sUser, strID) == 0x1C);
C_ASSERT(FIELD_OFFSET(sUser, dwMacLow) == 0x38);
C_ASSERT(FIELD_OFFSET(sUser, dwRecvCount) == 0x40);
C_ASSERT(FIELD_OFFSET(sUser, abXTrapSession) == 0x48);
C_ASSERT(FIELD_OFFSET(sUser, dwXTrapTick) == 0x188);
C_ASSERT(FIELD_OFFSET(sUser, bInUse) == 0x18C);
C_ASSERT(sizeof(sUser) == 0x190);

// 0x56B310: the user objects (500 per block).
extern CChunkAllocatorST<sUser> g_UserPool;

// sub_40BAC0 (sweep): a user object from g_UserPool, marked as in use.
sUser* UserPool_Alloc();

typedef std::map<DWORD, sUser*> AgentUserMap;

// CUserManager (0x56B36C): the users by session ID and the traffic filter settings.
class CUserManager
{
public:
    // sub_40BB20 (dynamic initializer): filter off, 100 packets / 60000 ms.
    CUserManager();
    // sub_40BBB0: asserts that the users were removed.
    virtual ~CUserManager();                                        // 0

    // sub_40BCE0: adds the user under its session ID; false (and logged) when the session has
    // one already.
    bool Register(sUser* pUser);
    // sub_40BD40: removes and releases the user of the session; false if there is none.
    bool Unregister(DWORD dwSessionID);
    // ShardMap_Find (0x40bdc0; the carried name is the GatewayServer function of the same
    // bytes): NULL for unknown sessions.
    sUser* Find(DWORD dwSessionID);
    // sub_40BE20: the first user (by session ID) with the JID, NULL if none.
    sUser* FindByJID(DWORD dwJID);
    // sub_40BC20: releases every user, empties the map and frees the pool blocks.
    void RemoveAll();

    // sub_40BE90: Server.cfg "traffic_filter_activity" (case-insensitive "service_on" turns the
    // filter on), "traffic_filter_packet_count" and "traffic_filter_check_perid".
    void LoadTrafficFilterConfig();
    // sub_40BFC0: counts a packet of the user. False (logged) when the user sent
    // m_dwTrafficMaxPackets packets in less than m_dwTrafficPeriod ms; then the caller drops the
    // session. True without filter or user.
    bool CheckTraffic(sUser* pUser);

public:
    AgentUserMap    m_mapUser;              // +4
    BOOL            m_bTrafficFilter;       // +10
    DWORD           m_dwTrafficMaxPackets;  // +14
    DWORD           m_dwTrafficPeriod;      // +18  ms
};

C_ASSERT(FIELD_OFFSET(CUserManager, m_bTrafficFilter) == 0x10);
C_ASSERT(sizeof(CUserManager) == 0x1C);

extern CUserManager g_UserManager;
