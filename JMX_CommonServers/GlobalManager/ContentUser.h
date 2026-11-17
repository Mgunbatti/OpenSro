#pragma once

#include <winsock2.h>
#include <windows.h>
#include <hash_set>
#include <map>
#include <string>

#include "BSLib/ChunkAllocator.h"
#include "BSLib/thread/SynchObject.h"

class CMsg;
class CDBQuery;

namespace ServerFramework
{
class CMassiveMsg;
}

// The users logged in to the division (content users). An AgentServer registers a user with
// 0x6201 after the client's login (CGlobalManagerMainProcess::OnMsg_6201) and removes it with
// 0x6202. Users are kept by JID and grouped by client address; the number of users per address
// may be limited (Server.cfg "MaxUserForNonePCBangIP", unlimited for the franchise addresses of
// SK_gamebang_ip and for two official networks).

#pragma pack(push, 1)
// Play time of a user (6 bytes): the time the count was last restarted and the play time of
// the day before that (seconds, from the AgentServer's login message).
struct CONTENT_USER_PLAYTIME
{
    // sub_401200
    void Clear()
    {
        dwTime = 0;
        wPlayTime = 0;
    }

    // sub_401290: restarts the count now.
    void SetNow();
    // sub_4012B0: seconds since dwTime plus wPlayTime.
    int GetPlayTime() const;

    DWORD   dwTime;         // +0 __time32_t
    WORD    wPlayTime;      // +4
};
#pragma pack(pop)

C_ASSERT(sizeof(CONTENT_USER_PLAYTIME) == 6);

// sContentUser (0x80 bytes, from g_ContentUserPool). The AgentServer's login message is read
// into a temporary object that CContentUserManager::Register copies.
struct sContentUser
{
    // sub_401080 (the pool's element constructor)
    sContentUser()
    {
        PlayTime.Clear();
        bInUse = 0;
    }

    // sContentUser::vf00 (0x4015a0): back to g_ContentUserPool (only once, see bInUse).
    virtual void __stdcall Release();                               // 0

    DWORD               dwIP;           // +4   client address (network order)
    DWORD               dwJID;          // +8
    std::string         strID;          // +C   account name
    BYTE                byContentID;    // +28
    BYTE                b29;            // +29
    WORD                wShardID;       // +2A
    WORD                wAgentBodyID;   // +2C  AgentServer the user came through
    BYTE                byIPType;       // +2E  3 franchise address, 4 other
    BYTE                b2F;            // +2F
    SYSTEMTIME          stLogin;        // +30  local time of the registration
    std::string         strMAC;         // +40  "XX-XX-XX-XX-XX-XX"
    BYTE                b5C;            // +5C  billing data (CStandardBillingMgr)
    BYTE                b5D;            // +5D
    WORD                w5E;            // +5E
    DWORD               dw60;           // +60
    DWORD               dw64;           // +64
    DWORD               dwTick68;       // +68
    DWORD               dwRelayID;      // +6C  of the last 0x6201 / 0x6900 (sub_40EB60)
    DWORD               dwSessionID;    // +70  AgentServer session of that message
    CONTENT_USER_PLAYTIME PlayTime;     // +74
    BYTE                b7A[2];         // +7A
    BYTE                bInUse;         // +7C  set by ContentUserPool_Alloc
};

C_ASSERT(FIELD_OFFSET(sContentUser, dwIP) == 0x4);
C_ASSERT(FIELD_OFFSET(sContentUser, strID) == 0xC);
C_ASSERT(FIELD_OFFSET(sContentUser, byContentID) == 0x28);
C_ASSERT(FIELD_OFFSET(sContentUser, wAgentBodyID) == 0x2C);
C_ASSERT(FIELD_OFFSET(sContentUser, stLogin) == 0x30);
C_ASSERT(FIELD_OFFSET(sContentUser, strMAC) == 0x40);
C_ASSERT(FIELD_OFFSET(sContentUser, b5C) == 0x5C);
C_ASSERT(FIELD_OFFSET(sContentUser, dwTick68) == 0x68);
C_ASSERT(FIELD_OFFSET(sContentUser, PlayTime) == 0x74);
C_ASSERT(FIELD_OFFSET(sContentUser, bInUse) == 0x7C);
C_ASSERT(sizeof(sContentUser) == 0x80);

typedef std::map<DWORD, sContentUser*> ContentUserMap;

// The users of one client address (0x1C bytes, from g_SingleIPContentUserMapPool).
class CSingleIPContentUserMap
{
public:
    // CSingleIPContentUserMap::ctor (0x401100)
    CSingleIPContentUserMap()
    {
        bInUse = 0;
    }

    // CSingleIPContentUserMap::vf00 (0x4015f0): back to the pool (only once, see bInUse).
    virtual void __stdcall Release();                               // 0

    // sub_401570: true when another user may log in from the address.
    bool CanAddUser() const
    {
        if (m_nLimit == -1)
            return true;
        return m_mapUser.size() < (DWORD)m_nLimit;
    }
    // sub_401B60: asserts that the limit is not below the current number of users.
    void SetLimit(int nLimit);
    // sub_401470: the user with the JID, NULL if none.
    sContentUser* Find(DWORD dwJID);
    // sub_4014D0
    void Insert(sContentUser* pUser);
    // sub_401500: removes and releases the user with the JID (asserts that it is there).
    void Remove(DWORD dwJID);
    // sub_4013C0: releases every user and empties the map.
    void ReleaseAll();

public:
    ContentUserMap  m_mapUser;      // +4
    BYTE            bInUse;         // +10  set by SingleIPContentUserMapPool_Alloc
    int             m_nLimit;       // +14  -1 unlimited
    BOOL            m_bFranchise;   // +18  address of SK_gamebang_ip
};

C_ASSERT(FIELD_OFFSET(CSingleIPContentUserMap, bInUse) == 0x10);
C_ASSERT(FIELD_OFFSET(CSingleIPContentUserMap, m_nLimit) == 0x14);
C_ASSERT(sizeof(CSingleIPContentUserMap) == 0x1C);

typedef std::map<DWORD, CSingleIPContentUserMap*> ContentUserIPMap;

// CContentUserManager (0x599E58, g_ContentUserManager).
class CContentUserManager
{
public:
    // sub_401630 (dynamic initializer)
    CContentUserManager();
    // CContentUserManager::dtor (0x4017c0)
    virtual ~CContentUserManager();                                 // 0

    // sub_401A20: the user map of the address, NULL if none. Not locked.
    CSingleIPContentUserMap* FindIP(DWORD dwIP);
    // sub_401A80: the user map of the address; a new one gets the limit of g_nMaxUserPerIP
    // (unlimited for franchise and official addresses). Not locked.
    CSingleIPContentUserMap* AddIP(DWORD dwIP, BOOL bFranchise);
    // sub_401B70: 0 when the user may log in; 1 JID logged in already, 2 too many users from
    // the address, 5 JID registered for the address.
    BYTE CheckLogin(DWORD dwJID, DWORD dwIP);
    // sub_401BD0: registers a copy of pUser (its byIPType is set); returns 0 or the error of
    // CheckLogin.
    BYTE Register(sContentUser* pUser);
    // sub_401ED0: removes and releases the user (its play time goes to the account DB); false
    // when the JID is not registered. *pdwIP gets the user's address (0 if none).
    bool Unregister(DWORD dwJID, DWORD* pdwIP);
    // sub_402060: removes every user of the AgentServer body; returns their number.
    int UnregisterByAgent(WORD wAgentBodyID);
    // sub_402220: the user with the JID, NULL if none.
    sContentUser* Find(DWORD dwJID);
    // sub_402340: the first user (by JID) whose account name equals pszID (CompareStringA,
    // case-sensitive), NULL if none.
    sContentUser* FindByID(LPCSTR pszID);
    // sub_4024C0: every user whose account name matches the pattern ('*' and '?'; "*" alone
    // matches all, compared ignoring case) as BYTE 1, DWORD IP, DWORD JID, BYTE IP type,
    // WORD length, name, BYTE content, WORD shard, SYSTEMTIME; then BYTE 0. True.
    bool WriteUserQuery(ServerFramework::CMassiveMsg* pMsg, LPCSTR pszPattern);
    // sub_402830: when users were registered or removed since the start, counts the users of
    // each shard again (CERT_SHARD::w557).
    void RecountShardUsers();
    // sub_4029B0: BYTE shard count, then WORD shard, WORD users for every shard.
    void WriteShardUserCount(CMsg* pMsg);
    // sub_402D30: the users for the billing server.
    void WriteBillingUserList(ServerFramework::CMassiveMsg* pMsg);
    // sub_402F30: restarts the play time count of every user. Not locked.
    void ResetPlayTimes();
    // sub_401870 (called by CGlobalManager::OnShutdown): releases every user and address map
    // and empties the visit sets.
    void RemoveAll();

    // sub_402CD0: row trigger of LoadFranchiseIPs.
    int OnFranchiseIPRow(DWORD dwQuery, DWORD dwUnused);

public:
    ContentUserIPMap            m_mapIP;            // +4
    ContentUserMap              m_mapUser;          // +10
    CCriticalSectionBS          m_cs;               // +1C
    BOOL                        m_bChanged;         // +58  users came or went (never cleared)
    stdext::hash_set<DWORD>     m_setDailyJID;      // +5C
    stdext::hash_set<DWORD>     m_setTotalJID;      // +84
    SYSTEMTIME                  m_stLastLogin;      // +AC
    DWORD                       m_dwRegistered;     // +BC  since the monitor graph read it
    DWORD                       m_dwUnregistered;   // +C0  since the monitor graph read it
    int                         m_nDailyVisited;    // +C4
    int                         m_nTotalVisited;    // +C8
    int                         m_nUserCount;       // +CC
    int                         m_nOccupiedIPCount; // +D0
    int                         m_nFranchiseIPCount;    // +D4
};

C_ASSERT(FIELD_OFFSET(CContentUserManager, m_mapUser) == 0x10);
C_ASSERT(FIELD_OFFSET(CContentUserManager, m_cs) == 0x1C);
C_ASSERT(FIELD_OFFSET(CContentUserManager, m_bChanged) == 0x58);
C_ASSERT(FIELD_OFFSET(CContentUserManager, m_setDailyJID) == 0x5C);
C_ASSERT(FIELD_OFFSET(CContentUserManager, m_setTotalJID) == 0x84);
C_ASSERT(FIELD_OFFSET(CContentUserManager, m_stLastLogin) == 0xAC);
C_ASSERT(FIELD_OFFSET(CContentUserManager, m_dwRegistered) == 0xBC);
C_ASSERT(FIELD_OFFSET(CContentUserManager, m_nUserCount) == 0xCC);
C_ASSERT(sizeof(CContentUserManager) == 0xD8);

// 0x599D98 and 0x599DF8: the pools (500 objects per block).
extern CChunkAllocatorST<sContentUser> g_ContentUserPool;
extern CChunkAllocatorST<CSingleIPContentUserMap> g_SingleIPContentUserMapPool;
extern CContentUserManager g_ContentUserManager;

// 0x555E50: Server.cfg "MaxUserForNonePCBangIP", -1 (unlimited) by default.
extern int g_nMaxUserPerIP;

// sub_401590 / sub_4015E0: an object from the pool, marked as in use.
sContentUser* ContentUserPool_Alloc();
CSingleIPContentUserMap* SingleIPContentUserMapPool_Alloc();

// sub_401210: false when the time (__time32_t) is on today's date, true otherwise.
bool ContentUser_IsNotToday(DWORD dwTime);
// sub_401320: wildcard match of pszText against pszPattern ('*' any run, '?' any character,
// case-sensitive).
bool ContentUser_WildcardMatch(const char* pszPattern, const char* pszText);
// sub_402A60: true (and logged) for the two official networks, whose addresses have no user
// limit.
bool ContentUser_IsOfficialIP(DWORD dwIP);
// sub_4021D0: a user with a play time count gets an UpdatePlayTimeOfUser job.
void ContentUser_UpdatePlayTime(sContentUser* pUser);

// DB_Select_SK_gamebang_ip (0x402ab0): loads the franchise addresses and their user limits
// ("select ip, capacity from SK_gamebang_ip" on the account DB). The number of addresses, -1
// when the query fails.
int ContentUser_LoadFranchiseIPs();
// sub_4026F0 (the main process timer, once a second): 0x6105 {shard user counts} to every
// GatewayServer in state 4 or 5.
void ContentUser_BroadcastShardUserCount();

// sub_402A40 / sub_402A50: read and reset the registration counters of the monitor graphs.
DWORD ContentUser_TakeUnregisteredCount();
DWORD ContentUser_TakeRegisteredCount();
