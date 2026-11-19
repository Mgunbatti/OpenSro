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
