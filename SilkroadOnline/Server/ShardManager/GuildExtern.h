#pragma once

#include <winsock2.h>
#include <windows.h>

#include <string>

#include "BSLib/DB/Database.h"
#include "ServerFramework/ServerProcessMain.h"
#include "ServerCommon/InstanceRecords.h"

// The parts of other SR_ShardManager groups that the guild, alliance and crest uploader code
// (SM_social1) calls. Only declarations; the owning groups supply the real classes (with these
// members at the offsets given) and the definitions.

// ---------------------------------------------------------------------------------------------
// Provided by SM_main: the main process (0x8A88F8).
class CMainProcess : public ServerFramework::CServerProcessMain
{
public:
    // CMainProcess::BroadcastToGameServers (0x412120)
    int BroadcastToGameServers(CMsg* pMsg, int nExcept);
    // CMainProcess::BroadcastToAllGameServers (0x4121e0)
    int BroadcastToAllGameServers(CMsg* pMsg);
    // The CREST_FTP address of the configuration: the std::string at +0x42318 (the original
    // reads the member directly).
    const std::string& GetCrestFtpAddress() const;
};

extern CMainProcess* g_pMainProcess;                // 0x8A88F8

// ---------------------------------------------------------------------------------------------
// Provided by SM_main: the shard DB (0x8A8948).
class CShardDB
{
public:
    // The session pool at +8.
    CDBSessionMgr* GetSession();
    // CDBConnPool_GetConnAt (0x4013d0, also inlined): table nIndex of the table vector at
    // +0x18; asserts and returns NULL when the vector is shorter.
    CDBTable* GetTable(int nIndex);
};

extern CShardDB* g_pShardDB;                        // 0x8A8948
// GetSingleton_CShardDB (0x401c10): g_pShardDB, asserted.
CShardDB* GetSingleton_CShardDB();

enum
{
    SHARD_DB_TABLE_GUILD            = 0x0B,
    SHARD_DB_TABLE_GUILDMEMBER      = 0x0C,
    SHARD_DB_TABLE_ALLIEDCLANS      = 0x0D,
    SHARD_DB_TABLE_GUILDWAR         = 0x0E,
    SHARD_DB_TABLE_GPHISTORY        = 0x0F,
};

// ---------------------------------------------------------------------------------------------
// Provided by SM_main: the shard's own server data (0x8588B4); the shard ID is the WORD at +4.
struct SHARD_SELF_INFO
{
    DWORD   dw00;
    WORD    wShardID;       // +4
};

extern SHARD_SELF_INFO* g_pShardSelfInfo;           // 0x8588B4

// ---------------------------------------------------------------------------------------------
// Provided by SM_main: users and characters.
class CUser
{
public:
    // The DWORD at +0x78 (account JID).
    DWORD GetJID() const;
    // The DWORD at +0x6C (guild ID of the user; cleared when the member leaves).
    void SetGuildID(DWORD dwGuildID);
    DWORD GetGuildID() const;
    // The DWORD at +0x7C (CharID of the selected character). [SM_social2]
    DWORD GetCharID() const;
};

class CClientManager
{
public:
    // CClientManager::FindUserByCharName (0x4043f0)
    CUser* FindUserByCharName(const std::string& strName);
    // CClientManager::FindUserByJID (0x404380) [SM_social2]
    CUser* FindUserByJID(DWORD dwJID);
};

// Provided by SM_main [SM_social2]: CharName_IsValid (0x40f200), the name filter of the main
// process (g_pMainProcess is asserted before the call).
BOOL CharName_IsValid(const std::string& strName);

// ---------------------------------------------------------------------------------------------
// Provided by the transaction group [SM_social2]: the guild job of the hostile guild request
// (trs_guild_job, pool 0x4204b0) and the overlapped job manager.
class trs_guild_job
{
public:
    // trs_guild_job_PoolAlloc_4204B0 (0x4204b0)
    static trs_guild_job* Alloc();
    // Virtual slot 1: the job's parameters.
    void Setup(DWORD dwJID, WORD wOpcode, DWORD dw0, float fTimeout, DWORD dwWarID,
               DWORD dwTargetGuildID);
    // Virtual slot 3: back to the pool.
    void Release();
};

class COverlappedJobMgr
{
public:
    // COverlappedJobMgr::StartJob (0x422150)
    BOOL StartJob(trs_guild_job* pJob, CUser* pUser, CMsg* pMsg);
};

// GetSingleton_COverlappedJobMgr (0x40baf0)
COverlappedJobMgr* GetSingleton_COverlappedJobMgr();

extern CClientManager* g_pClientManager;            // 0x85884C

class CCharacter
{
public:
    // The record at +4.
    CInstancePC* GetPCRecord() const;
};

// transaction::AddParticipantJID (0x420750): the call user / broadcast target list.
class transaction
{
public:
    void AddParticipantJID(DWORD dwJID);
};

// ---------------------------------------------------------------------------------------------
// Provided by the vote group: CVote::AddChoice (0x461640); the BYTE at +0x0C is the vote kind.
class CVote
{
public:
    void AddChoice(DWORD dwCharID);
    BYTE GetKind() const;
    // The DWORD at +0x08 (vote ID, compared by CGuildCommon::RemoveMasterCandidate).
    // [SM_social2]
    DWORD GetID() const;
    // Virtual slot 8 (vf+0x20): the vote into a message. [SM_social2]
    void WriteToMsg(CMsg* pMsg);
};

// [SM_social2] The vote manager (0x8A8934).
class CVoteManager
{
public:
    // CVoteManager::CreateVote (0x461d60): a master vote of the guild.
    CVote* CreateVote(class CGuild* pGuild, DWORD dwGuildID);
    // CVoteManager::RemoveVote (0x462120)
    void RemoveVote(CVote* pVote);
};

extern CVoteManager* g_pVoteManager;                // 0x8A8934

// ---------------------------------------------------------------------------------------------
// Provided by the training camp group.
class CTrainingCampCommon
{
public:
    // Guild_GetID_45E4F0 (0x45e4f0, the same code as CGuildCommon::GetID): the ID of the camp
    // record at +4.
    DWORD GetID() const;
};

class CTrainingCampManager;
extern CTrainingCampManager* g_pTrainingCampManager;    // 0x8A8930
// TrainingCampMgr_FindCampByChar_wrap (0x459980)
CTrainingCampCommon* TrainingCampMgr_FindCampByChar(DWORD dwCharID,
                                                    CTrainingCampManager* pManager);

// ---------------------------------------------------------------------------------------------
// Provided by the guild war group: packed local date-times (0x43f8c0 / 0x43f830).
void PackedDateTime_GetLocal(DWORD* pdwPacked);
int PackedDateTime_Compare(const DWORD* pdwLeft, const DWORD* pdwRight);
