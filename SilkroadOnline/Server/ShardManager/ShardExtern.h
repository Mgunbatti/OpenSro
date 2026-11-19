#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>

#include "ServerCommon/PartyInfo.h"
#include "ServerFramework/ServerProcessMain.h"

// The stub declarations every SR_ShardManager group shares: the classes of SM_main and SM_world1
// (CMainProcess, CUser, CClientManager) and the packed local time. Only the members the ported
// groups call are declared; the owning groups define them. Group specific declarations stay in
// SocialExtern.h, GuildExtern.h and ShardWorldExtern.h.

class CMsg;
class CUser;

// ---------------------------------------------------------------------------------------------
// Provided by SM_main (CMainProcess)

class CMainProcess : public ServerFramework::CServerProcessMain
{
public:
    // CMainProcess::CountReadyGameServers (0x4142c0): the SR_GameServer bodies of this shard
    // with a bound session.
    static int CountReadyGameServers();
    // CMainProcess::BroadcastToGameServers (0x412120): to every SR_GameServer of this shard but
    // the session nExcept (0: to all). The three do not use the object.
    static int BroadcastToGameServers(CMsg* pMsg, int nExcept);
    // CMainProcess::BroadcastToAllGameServers (0x4121e0)
    static int BroadcastToAllGameServers(CMsg* pMsg);
    // The CREST_FTP address of the configuration: the std::string at +0x42318 (the original
    // reads the member directly).
    const std::string& GetCrestFtpAddress() const;
};

// g_pMainProcess (0x8a88f8)
extern CMainProcess* g_pMainProcess;

// ---------------------------------------------------------------------------------------------
// Provided by SM_world1 / SM_main (CUser, CClientManager)

class CUser
{
public:
    sPartyMemberInfo& GetPartyMemberInfo();     // +0x08 (its m_dwPartyID is the user's party)
    DWORD GetGuildID() const;                   // +0x6C (cleared when the member leaves)
    void SetGuildID(DWORD dwGuildID);
    DWORD GetTrainingCampID() const;            // +0x70
    void SetTrainingCampID(DWORD dwCampID);
    DWORD GetCampRegID() const;                 // +0x74 training camp recruit entry
    void SetCampRegID(DWORD dwRegID);
    DWORD GetJID() const;                       // +0x78
    DWORD GetCharID() const;                    // +0x7C
    const std::string& GetCharNameStr() const;  // +0x1A4
    DWORD GetSessionID() const;                 // +0x1C4
    DWORD GetDw1C8() const;                     // +0x1C8
    WORD GetState() const;                      // +0x234 (2: in game)
    DWORD GetRefObjID() const;                  // +0x248
    DWORD GetMatchEntryID() const;              // +0x24C
    void SetMatchEntryID(DWORD dwID);
    BYTE GetByte250() const;                    // +0x250
    // +0x254: the name a user without party registers on the party matching board with.
    const std::string& GetMatchName() const;
    void SetMatchName(const std::string& strName);

    // Character_GetNameCStr_404360
    const char* GetCharName() const
    {
        return GetCharNameStr().c_str();
    }
};

class CClientManager
{
public:
    // 0x404380 / 0x4043f0
    CUser* FindUserByJID(DWORD dwJID);
    CUser* FindUserByCharName(const std::string& strName);
    // The char ID -> user hash_map at +0x10C (filled by CClientManager::RegisterCharacter),
    // inlined in the original. NULL when not found.
    CUser* FindUserByCharID(DWORD dwCharID);
};

// GetSingleton_CClientManager (0x417c00, g_pClientManager 0x85884c, asserted)
CClientManager* GetSingleton_CClientManager();
extern CClientManager* g_pClientManager;            // 0x85884C

// ---------------------------------------------------------------------------------------------
// Provided by the guild war group (PackedDateTime_GetLocal 0x43f8c0 / PackedDateTime_Compare
// 0x43f830): the local time packed into a DWORD and the comparison of two of them.

void PackedDateTime_GetLocal(DWORD* pdwOut);
int PackedDateTime_Compare(const DWORD* pdwLeft, const DWORD* pdwRight);
