#pragma once

#include <windows.h>

// What the game world code needs from parts of SR_ShardManager that are ported elsewhere. Only
// the calls and fields it uses are declared here; the owners replace these declarations.

class CMsg;
class CUser;
class CGameWorld;

// ---------------------------------------------------------------------------------------------
// Provided by SM_main (CMainProcess, g_pMainProcess = dword_8A88F8)

class CMainProcess
{
public:
    // CMainProcess::CountReadyGameServers (0x4142c0): the SR_GameServer bodies of this shard
    // with a bound session.
    static int CountReadyGameServers();
    // CMainProcess::BroadcastToGameServers (0x412120): to every SR_GameServer of this shard
    // except pExcept.
    static int BroadcastToGameServers(CMsg* pMsg, void* pExcept);
    // CMainProcess::BroadcastToAllGameServers (0x4121e0).
    static int BroadcastToAllGameServers(CMsg* pMsg);
};

extern CMainProcess* g_pMainProcess;

// PackedDateTime_GetLocal (0x43f8c0): GetLocalTime packed like RefObj_PackTypeID.
void PackedDateTime_GetLocal(DWORD* pdwPacked);

// ---------------------------------------------------------------------------------------------
// Provided by the CUser / CClientManager owner (SM_world1 / SM_main)

// The game world position of a user (CUser + 0x1F0): set by CGameWorld::EnterLayer, read by
// CGameWorldMgr::OnUserLogout.
struct UserGameWorldPos
{
    DWORD           dw0;                // +0
    WORD            wWorldID;           // +4   CUser + 0x1F4
    WORD            wLayerID;           // +6   CUser + 0x1F6
    DWORD           adw8[7];            // +8
    CGameWorld*     pGameWorld;         // +24  CUser + 0x214
};

C_ASSERT(FIELD_OFFSET(UserGameWorldPos, pGameWorld) == 0x24);

// CUser + 0x7C.
DWORD User_GetCharID(const CUser* pUser);
// &CUser + 0x1F0.
UserGameWorldPos* User_GetGameWorldPos(CUser* pUser);
// ShardMgr8A894C_CheckByte141 (0x42ab90): the GM level check of the user's byte +141 on the GM
// level manager (dword_8A894C, asserts when there is none).
BOOL User_HasGMLevelForIP(CUser* pUser);
// CClientManager::FindUserByJID (0x404380) on g_pClientManager (dword_85884C, asserts when there
// is none).
CUser* ClientManager_FindUserByJID(DWORD dwJID);

// The character data CUser::SelectCharacter hands to CGameWorldMgr::IsCharInLayer.
struct GameWorldCharPos
{
    BYTE            ab0[0x20];          // +0
    WORD            wWorldID;           // +20
    WORD            w22;                // +22
    WORD            wLayerID;           // +24
    WORD            w26;                // +26
    WORD            awCreateTime[6];    // +28  packed with RefObj_PackTypeID
};

C_ASSERT(FIELD_OFFSET(GameWorldCharPos, awCreateTime) == 0x28);

// ---------------------------------------------------------------------------------------------
// Used by the flag world, the battle arena and the siege fortress (SM_world4)

// CUser + 0x1C4: the session of the game server the user is on.
DWORD User_GetSessionID(const CUser* pUser);
// CUser + 0x68: the party of the user (NULL without one); CUser + 0x78: the key the party
// manager removes the member by.
void* User_GetParty(const CUser* pUser);
DWORD User_GetPartyMemberKey(const CUser* pUser);

// Provided by the party group: CPartyManager::RemoveMember (0x44f580) on the party manager
// (dword_8A892C, asserts when there is none).
void PartyManager_RemoveMember(void* pParty, DWORD dwMemberKey, int nReason);

// Provided by the guild group: CGuildManager::ResetSiegeAuthority (0x43c5a0) on
// GetSingleton_CGuildManager (0x40bab0).
void GuildManager_ResetSiegeAuthority(DWORD dwGuildID);

// Provided by SM_main: the siege schedule state of the main process (CMainProcess + 0x42310;
// the same declaration as SM_world3's ShardScheduleExtern.h).
struct SHARD_SIEGE_SCHEDULE
{
    BYTE        ab00[0x25];
    BYTE        bSiegeRunning;      // +25 between siege begin and end
    BYTE        bTaxPeriod;         // +26 inside the tax period
    BYTE        b27;
    SYSTEMTIME  stPrevSiegeBegin;   // +28
    SYSTEMTIME  stSiegeBegin;       // +38
    SYSTEMTIME  stSiegeEnd;         // +48
    SYSTEMTIME  stTaxBegin;         // +58 the SCHEDULE_DEF_SIEGE_REQUEST_PERIOD period
    SYSTEMTIME  stTaxEnd;           // +68
};

C_ASSERT(FIELD_OFFSET(SHARD_SIEGE_SCHEDULE, stPrevSiegeBegin) == 0x28);
C_ASSERT(FIELD_OFFSET(SHARD_SIEGE_SCHEDULE, stTaxEnd) == 0x68);

SHARD_SIEGE_SCHEDULE* MainProcess_GetSiegeSchedule();

namespace NewSchedule { class CScheduleManager; }

// Provided by SM_world3: the static CShard_ScheduleManager (0x8A7B24).
NewSchedule::CScheduleManager* ShardScheduleManager_Get();

// Provided by the party group: CPartyManager::FindParty (0x44ead0) on GetSingleton_CPartyManager
// (0x418440); the member count of the party (its member vector +0x10 / +0x14, 0 when empty).
void* PartyManager_FindParty(DWORD dwPartyID);
DWORD Party_GetMemberCount(void* pParty);

// Provided by SM_main: CMainProcess + 0x423B0: one BYTE per battle arena match type (occupy, score,
// flag), 0 while the match type is switched off.
BYTE MainProcess_IsArenaMatchOpen(BYTE byMatch);
