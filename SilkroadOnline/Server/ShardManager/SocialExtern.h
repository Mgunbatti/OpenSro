#pragma once

#include <winsock2.h>
#include <windows.h>
#include <list>
#include <string>
#include <vector>

#include "BSLib/BSDebug.h"
#include "BSLib/DB/Database.h"
#include "BSNet/Msg.h"
#include "ServerCommon/PartyInfo.h"
#include "ServerFramework/ServerProcessMain.h"

// What the social classes of SR_ShardManager (guilds, messenger, party manager, training camps,
// votes) use from other subsystems. Only the members they call are declared; the owning groups
// define them (the tests link stand-ins). The guild code adds its own in GuildExtern.h.

class CUser;

// ---------------------------------------------------------------------------------------------
// Provided by the transaction group (transaction, trs_messenger_job, trs_party_job,
// 0x41fcc0..0x425bf0)

// Pending request that waits for the answer of another user (COverlappedJobMgr).
class transaction
{
public:
    virtual ~transaction();                                                     // 0
    // transaction::vf01 (0x4206f0)
    virtual void Init(DWORD dwRequesterJID, DWORD dwOpcode, DWORD dwTargetJID, float fTimeout,
                      DWORD dw5, DWORD dw6);                                    // 1
    virtual void vf02();                                                        // 2
    // Returns the job to its pool (trs_*_job::vf03).
    virtual void Release();                                                     // 3

    // transaction::AddParticipantJID (0x420750)
    void AddParticipantJID(DWORD dwJID);

public:
    DWORD   m_dw04;                 // +0x04
    DWORD   m_dwRequesterJID;       // +0x08
    DWORD   m_dw0C;                 // +0x0C
    DWORD   m_dwTargetJID;          // +0x10
    BYTE    m_pad14[0x24];          // +0x14
    DWORD   m_dwTargetSessionID;    // +0x38
};

C_ASSERT(FIELD_OFFSET(transaction, m_dwRequesterJID) == 0x08);
C_ASSERT(FIELD_OFFSET(transaction, m_dwTargetJID) == 0x10);
C_ASSERT(FIELD_OFFSET(transaction, m_dwTargetSessionID) == 0x38);

class trs_messenger_job : public transaction
{
};

class trs_party_job : public transaction
{
public:
    sPartyInfo          m_Info;         // +0x3C
    sPartyMemberInfo    m_Master;       // +0xB0
    BYTE                m_pad114[0x0C]; // +0x114
    DWORD               m_dw120;        // +0x120
};

C_ASSERT(FIELD_OFFSET(trs_party_job, m_Info) == 0x3C);
C_ASSERT(FIELD_OFFSET(trs_party_job, m_Master) == 0xB0);
C_ASSERT(FIELD_OFFSET(trs_party_job, m_dw120) == 0x120);

// trs_messenger_job_PoolAlloc_420430 / trs_party_job_PoolAlloc_420530
trs_messenger_job* trs_messenger_job_Alloc();
trs_party_job* trs_party_job_Alloc();

// ---------------------------------------------------------------------------------------------
// Provided by SM_main (COverlappedJobMgr, CMainProcess, CShardDB)

class COverlappedJobMgr
{
public:
    // COverlappedJobMgr::StartJob (0x422150)
    BOOL StartJob(transaction* pJob, CUser* pRequester, CMsg* pMsg);
};

// GetSingleton_COverlappedJobMgr (0x40baf0, g_pOverlappedJobMgr 0x8a8910)
COverlappedJobMgr* GetSingleton_COverlappedJobMgr();

class CMainProcess : public ServerFramework::CServerProcessMain
{
public:
    // CMainProcess::BroadcastToGameServers (0x412120): to every game server but the session
    // nExcept.
    int BroadcastToGameServers(CMsg* pMsg, int nExcept);
    // CMainProcess::BroadcastToAllGameServers (0x4121e0)
    int BroadcastToAllGameServers(CMsg* pMsg);
    // The CREST_FTP address of the configuration: the std::string at +0x42318 (the original
    // reads the member directly).
    const std::string& GetCrestFtpAddress() const;
};

// g_pMainProcess (0x8a88f8)
extern CMainProcess* g_pMainProcess;

class CShardDB
{
public:
    // +0x08
    CDBSessionMgr* GetSession();
    // +0x18: the instance tables (index = slot; _Friend 0x14, _Memo 0x15)
    std::vector<CDBTable*>& GetInstanceTables();
    // CDBConnPool_GetConnAt (0x4013d0, also inlined): table nIndex of the table vector at
    // +0x18; asserts and returns NULL when the vector is shorter.
    CDBTable* GetTable(int nIndex);
};

// GetSingleton_CShardDB (0x401c10) asserts g_pShardDB (0x8a8948).
extern CShardDB* g_pShardDB;
CShardDB* GetSingleton_CShardDB();

// CDBObjectList_DetachAndClear: frees every record of the list to pTable and clears it.
void CDBObjectList_DetachAndClear(CDBRecordList* pList, CDBTable* pTable);

enum
{
    SHARD_TABLE_FRIEND  = 0x14,
    SHARD_TABLE_MEMO    = 0x15,
    SHARD_TABLE_TRAINING_CAMP           = 0x16,
    SHARD_TABLE_TRAINING_CAMP_MEMBER    = 0x17,
    SHARD_TABLE_TRAINING_CAMP_BUFF      = 0x19,
};

// ---------------------------------------------------------------------------------------------
// Provided by SM_world (CUser, CClientManager)

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
};

// GetSingleton_CClientManager (0x417c00, g_pClientManager 0x85884c, asserted)
CClientManager* GetSingleton_CClientManager();
extern CClientManager* g_pClientManager;            // 0x85884C

// ---------------------------------------------------------------------------------------------
// Provided by the guild war group (PackedDateTime_GetLocal 0x43f8c0 / PackedDateTime_Compare
// 0x43f830): the local time packed into a DWORD and the comparison of two of them.

void PackedDateTime_GetLocal(DWORD* pdwOut);
int PackedDateTime_Compare(const DWORD* pdwLeft, const DWORD* pdwRight);
