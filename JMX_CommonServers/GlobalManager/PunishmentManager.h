#pragma once

#include <winsock2.h>
#include <windows.h>
#include <map>
#include <sql.h>

#include "BSLib/ChunkAllocator.h"
#include "GlobalManagerMainFwd.h"

class CTask;

// The active punishments (blocks) of the accounts. The main process loads them at start
// ("exec _QueryActivePunishmet" on the account DB, sub_40F240), COverlapJob_PunishmentJob adds
// new ones, a timer (every 60 s, sub_40DFE0) starts and ends them, and 0x6318 from a shard reads
// the active ones of a user. Every start and end is told to the shard manager of the user's
// shard with 0x6317 {BYTE 0 started / 1 ended, JID, type[, SYSTEMTIME end]}.

#pragma pack(push, 1)
// Punishment_Info_Internal (0x1B bytes, from g_PunishmentInfoPool).
struct Punishment_Info_Internal
{
    // pool element constructor (0x45ab10, inlined)
    Punishment_Info_Internal()
    {
        bInUse = 0;
    }

    // vf00 (0x458d50): back to g_PunishmentInfoPool (only once, see bInUse).
    virtual void __stdcall Release();                               // 0

    DWORD       dwJID;          // +4
    BYTE        byType;         // +8
    BYTE        bActive;        // +9   the begin has passed (0x6317 {0} was sent)
    FILETIME    ftBegin;        // +A   local time
    FILETIME    ftEnd;          // +12  local time
    BYTE        bInUse;         // +1A  set by PunishmentInfoPool_Alloc
};

// The row of _QueryActivePunishmet (0x25 bytes).
struct PUNISHMENT_ROW
{
    DWORD               dwJID;      // +0
    BYTE                byType;     // +4
    TIMESTAMP_STRUCT    tsBegin;    // +5
    TIMESTAMP_STRUCT    tsEnd;      // +15
};

// One entry of the 0x6318 answer: the end of an active punishment of the type.
struct PUNISHMENT_ACTIVE_ENTRY
{
    BYTE        bActive;        // +0
    SYSTEMTIME  stEnd;          // +1
};
#pragma pack(pop)

C_ASSERT(FIELD_OFFSET(Punishment_Info_Internal, dwJID) == 4);
C_ASSERT(FIELD_OFFSET(Punishment_Info_Internal, ftBegin) == 0xA);
C_ASSERT(FIELD_OFFSET(Punishment_Info_Internal, ftEnd) == 0x12);
C_ASSERT(FIELD_OFFSET(Punishment_Info_Internal, bInUse) == 0x1A);
C_ASSERT(sizeof(Punishment_Info_Internal) == 0x1B);
C_ASSERT(FIELD_OFFSET(PUNISHMENT_ROW, tsEnd) == 0x15);
C_ASSERT(sizeof(PUNISHMENT_ROW) == 0x25);
C_ASSERT(sizeof(PUNISHMENT_ACTIVE_ENTRY) == 0x11);

// The size of the buffer the caller of GetActive passes (cleared by GetActive).
const int PUNISHMENT_ACTIVE_BUFFER = 0x110;

typedef std::multimap<DWORD, Punishment_Info_Internal*> PunishmentMap;

// Sets g_pPunishmentManager (0x459820 is this constructor on its own; the dynamic initializer
// 0x458E70 inlines it before the members are built).
class CPunishmentManagerSingleton
{
public:
    CPunishmentManagerSingleton();
};

// CPunishmentManager (0x10 bytes, the static object 0x59B074).
class CPunishmentManager : public CPunishmentManagerSingleton
{
public:
    // dynamic initializer 0x458E70
    CPunishmentManager();
    // atexit destructor 0x458F40: frees the nodes (the punishments are not released) and
    // clears g_pPunishmentManager.
    ~CPunishmentManager();

    // DB_QueryActivePunishmet (0x4590a0): keeps the task, loads the punishments of the account
    // DB and marks the ones whose begin has passed. False (logged) when the query fails.
    bool Load(CTask* pTask);
    // sub_458FE0: row trigger of Load.
    int OnPunishmentRow(DWORD dwQuery, DWORD dwUnused);
    // sub_459400 (PunishmentManager_Add): a punishment of the account replaces the one of the
    // same type; types 1 and 2 are refused (false). 0x6317 is sent for the replaced punishment
    // and for the new one when its begin has passed. True.
    bool Add(DWORD dwJID, DWORD dwType, const TIMESTAMP_STRUCT* pBegin,
             const TIMESTAMP_STRUCT* pEnd);
    // sub_459520: removes and releases the first punishment of the account with the type;
    // false when there is none.
    bool RemoveByType(DWORD dwJID, DWORD dwType);
    // sub_4595D0 (0x6318): clears PUNISHMENT_ACTIVE_BUFFER bytes at pOut and fills the entry
    // of every active punishment of the account (entry type - 3); the number of them.
    BYTE GetActive(DWORD dwJID, BYTE* pOut);
    // sub_4596E0 (timer 0x22): ends the active punishments whose end has passed and starts the
    // others whose begin has passed.
    void Tick();
    // sub_459290: 0x6317 {1, JID, type} to the shard manager of the logged-in user.
    void SendEnded(DWORD dwJID, BYTE byType);
    // sub_459340: 0x6317 {0, JID, type, SYSTEMTIME end} to the shard manager of the logged-in
    // user.
    void SendStarted(DWORD dwJID, BYTE byType, const SYSTEMTIME* pEnd);

public:
    CTask*          m_pTask;        // +0   main task (relays)
    PunishmentMap   m_mapPunishment;    // +4
};

C_ASSERT(sizeof(CPunishmentManager) == 0x10);

// 0x59B018: the pool of the punishments (500 per block, unnamed).
extern CChunkAllocatorST<Punishment_Info_Internal> g_PunishmentInfoPool;
// 0x59B074
extern CPunishmentManager g_PunishmentManager;

// sub_458D40: a punishment from the pool, marked as in use.
Punishment_Info_Internal* PunishmentInfoPool_Alloc();

// sub_458DB0: the local time of the timestamp as FILETIME (fraction ignored). When the date is
// invalid the result keeps the first fields of the SYSTEMTIME the function builds.
FILETIME Punishment_TimestampToFileTime(const TIMESTAMP_STRUCT* pTime);
// sub_458D00: the timestamp as SYSTEMTIME (day of week and milliseconds 0).
SYSTEMTIME* Punishment_TimestampToSystemTime(SYSTEMTIME* pOut, const TIMESTAMP_STRUCT* pTime);
// sub_458E40 (not referenced): the local time as FILETIME.
FILETIME Punishment_GetLocalFileTime();
