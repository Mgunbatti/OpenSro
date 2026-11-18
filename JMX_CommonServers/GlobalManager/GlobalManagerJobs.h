#pragma once

#include <winsock2.h>
#include <windows.h>
#include <list>
#include <map>
#include <string>
#include <sql.h>

#include "BSLib/ChunkAllocator.h"
#include "BSLib/DB/DBQuery.h"
#include "ServerFramework/OverlapJob.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerProcessMain.h"

class CMsg;

// Overlap jobs of the GlobalManager main process (run on the overlap process, mostly database
// work). Every class has its own pool (500 jobs per block, named after the class); the jobs are
// taken with a job ID, set up with the main process' task and sent with SendOverlapJob(job,
// NULL). The service manager's jobs are in ServiceManagerJobs.h.

// 0x558C98: the main process, set by its Initialize (GlobalManagerMainProcess.h).
class CGlobalManagerMainProcess;
extern CGlobalManagerMainProcess* g_pGlobalManagerProcess;

// sub_422570: "%d-%d-%d %d:%d:%d" of the time (StringCchPrintf into 256 characters; asserts
// instead of assigning when it does not fit).
void GM_FormatTimestamp(std::string* pstrOut, const TIMESTAMP_STRUCT* pTime);
// sub_422620 (not referenced): the local time with fraction 0.
void GM_GetLocalTimestamp(TIMESTAMP_STRUCT* pTime);

// ---------------------------------------------------------------------------------------------
// COverlapJob_CheckCharName (0x4C bytes): 0x620B from a shard, "exec _CheckCharName" on the
// division DB; the answer 0xA20B {1} or {2, error} goes back through the relay.
class COverlapJob_CheckCharName : public ServerFramework::COverlapJob_Local
{
public:
    // COverlapJob_CheckCharName::ctor (0x41a9a0)
    COverlapJob_CheckCharName() {}

    virtual void ReturnToPool();                                // 3  (0x4129f0)
    // vf05 (0x412ac0): the session must still be open.
    virtual bool Prepare();                                     // 5
    // vf06 (0x412ae0): the result byte of the procedure; 2 when the query fails. True.
    virtual bool Execute();                                     // 6
    // vf07 (0x412b90): false when the session is closed, else the answer.
    virtual bool OnComplete();                                  // 7
    // vf08 (0x412c30): asserts. True.
    virtual bool OnFail();                                      // 8

    // sub_412A40: Init, then BYTE, BYTE, BYTE and the name from the message.
    void Setup(CTask* pTask, CMsg* pMsg, DWORD dwSessionID, DWORD dwRelayID);

public:
    BYTE        m_by20;         // +20  first argument of the procedure
    BYTE        m_by21;         // +21
    BYTE        m_by22;         // +22
    std::string m_strName;      // +24
    DWORD       m_dwSessionID;  // +40
    DWORD       m_dwRelayID;    // +44
    BYTE        m_byResult;     // +48  row of the query
};

C_ASSERT(FIELD_OFFSET(COverlapJob_CheckCharName, m_strName) == 0x24);
C_ASSERT(FIELD_OFFSET(COverlapJob_CheckCharName, m_dwSessionID) == 0x40);
C_ASSERT(FIELD_OFFSET(COverlapJob_CheckCharName, m_byResult) == 0x48);
C_ASSERT(sizeof(COverlapJob_CheckCharName) == 0x4C);

// ---------------------------------------------------------------------------------------------
// A row of SK_SilkChange_BY_Web (0x18 bytes, from g_CUCashChangedPool): the 18-byte row follows
// the vtable.
#pragma pack(push, 1)
struct CU_CASH_CHANGED_ROW
{
    DWORD   dwID;           // +0
    DWORD   dwJID;          // +4
    DWORD   dw8;            // +8
    DWORD   dwC;            // +C
    BYTE    by10;           // +10
    BYTE    by11;           // +11
};
#pragma pack(pop)

C_ASSERT(sizeof(CU_CASH_CHANGED_ROW) == 18);

struct sCU_Cash_Changed
{
    // sub_4129A0 (the pool's element constructor)
    sCU_Cash_Changed()
    {
        bInUse = 0;
    }

    // sCU_Cash_Changed::vf00 (0x413010): back to the pool (only once, see bInUse).
    virtual void __stdcall Release();                               // 0

    CU_CASH_CHANGED_ROW Row;        // +4
    BYTE                bInUse;     // +16  set by CUCashChangedPool_Alloc
};

C_ASSERT(FIELD_OFFSET(sCU_Cash_Changed, Row) == 4);
C_ASSERT(FIELD_OFFSET(sCU_Cash_Changed, bInUse) == 0x16);
C_ASSERT(sizeof(sCU_Cash_Changed) == 0x18);

typedef std::list<sCU_Cash_Changed*> CUCashChangedList;

// COverlapJob_RefreshCommercialUser_Cash (0x2C bytes): reads the silk changes made on the web
// (silk DB), deletes them and tells the shard of every logged-in user (0x6A00 relayed to the
// shard manager).
class COverlapJob_RefreshCommercialUser_Cash : public ServerFramework::COverlapJob_Local
{
public:
    // COverlapJob_RefreshCommercialUser_Cash::ctor (0x41abf0)
    COverlapJob_RefreshCommercialUser_Cash() {}

    // vf01 (0x4130c0): Init; asserts the list is empty.
    virtual void Init(CTask* pOwner);                           // 1
    virtual void ReturnToPool();                                // 3  (0x413070)
    // vf05 (0x4130e0): true.
    virtual bool Prepare();                                     // 5
    // vf06 (0x4130f0): "select * from SK_SilkChange_BY_Web" into the list, then the IDs from
    // the first to the last row are deleted. False when a query fails.
    virtual bool Execute();                                     // 6
    // vf07 (0x4132f0): 0x6A00 {JID, DWORD, DWORD, BYTE, BYTE} to the shard manager of every
    // row's user that is logged in; then the rows are released. True.
    virtual bool OnComplete();                                  // 7
    // vf08 (0x413450): the rows are released. True.
    virtual bool OnFail();                                      // 8

    // sub_413460: row trigger.
    int OnCashRow(DWORD dwQuery, DWORD dwUnused);
    // sub_413270: releases the rows and empties the list.
    void ReleaseRows();

public:
    CUCashChangedList   m_listRow;      // +20
};

C_ASSERT(FIELD_OFFSET(COverlapJob_RefreshCommercialUser_Cash, m_listRow) == 0x20);
C_ASSERT(sizeof(COverlapJob_RefreshCommercialUser_Cash) == 0x2C);

// ---------------------------------------------------------------------------------------------
// COverlapJob_RefreshShardStatus (0x2C bytes): the state and user ratio of every shard to the
// account DB (_Shard), every 10 seconds.
struct SHARD_STATUS_ITEM
{
    CERT_SHARD* pShard;     // +0
    BYTE        byStatus;   // +4   0 no body up, 1 some, 2 all bodies up (state 4 or 5)
    BYTE        byRatio;    // +5   100 * users / limit
};

C_ASSERT(sizeof(SHARD_STATUS_ITEM) == 8);

class COverlapJob_RefreshShardStatus : public ServerFramework::COverlapJob_Local
{
public:
    // COverlapJob_RefreshShardStatus::ctor (0x41ad30)
    COverlapJob_RefreshShardStatus() {}

    // vf01 (0x413560): Init, then the state of every shard of the certification data.
    virtual void Init(CTask* pOwner);                           // 1
    virtual void ReturnToPool();                                // 3  (0x413510)
    virtual bool Prepare();                                     // 5  (0x4136c0) true
    // vf06 (0x4136d0): "update _Shard ..." for every shard; false (list kept) when one fails,
    // else the list is emptied.
    virtual bool Execute();                                     // 6
    virtual bool OnComplete();                                  // 7  (0x4137c0) true
    virtual bool OnFail();                                      // 8  (0x4137d0) true

public:
    std::list<SHARD_STATUS_ITEM>    m_listShard;    // +20
};

C_ASSERT(sizeof(COverlapJob_RefreshShardStatus) == 0x2C);

// ---------------------------------------------------------------------------------------------
// COverlapJob_ManageShardCharacterData (0x68 bytes): 0x620E / 0x620F / 0x6210 from a shard
// (character created, deleted, renamed), "exec _ManageShardCharName" on the account DB; answers
// 0xA20E / 0xA20F / 0xA210 {1} or {2} through the relay.
class COverlapJob_ManageShardCharacterData : public ServerFramework::COverlapJob_Local
{
public:
    // COverlapJob_ManageShardCharacterData::ctor (0x41ae70)
    COverlapJob_ManageShardCharacterData() {}

    virtual void ReturnToPool();                                // 3  (0x413800)
    virtual bool Prepare();                                     // 5  (0x4129b0) true
    // vf06 (0x4138f0): true unless the query fails.
    virtual bool Execute();                                     // 6
    // vf07 / vf08 (0x4139a0 / 0x413a40): the answer {1} / {2}; for an unknown type the message
    // is released. True.
    virtual bool OnComplete();                                  // 7
    virtual bool OnFail();                                      // 8

    // sub_413850: Init and the data: DWORD and name from the message, for type 2 also the
    // second name. False when a name is empty or the type is not 0..2.
    bool Setup(CTask* pTask, CMsg* pMsg, DWORD dwRelayID, BYTE byType, WORD wShardID);

    // The answer (shared code of vf07 / vf08).
    void Answer(BYTE byResult);

public:
    DWORD       m_dwSessionID;  // +20
    DWORD       m_dwRelayID;    // +24
    BYTE        m_byType;       // +28  0 create, 1 delete, 2 rename
    BYTE        m_by29;
    WORD        m_wShardID;     // +2A
    DWORD       m_dw2C;         // +2C  from the message (JID)
    std::string m_strName;      // +30
    std::string m_strName2;     // +4C  type 2 only
};

C_ASSERT(FIELD_OFFSET(COverlapJob_ManageShardCharacterData, m_byType) == 0x28);
C_ASSERT(FIELD_OFFSET(COverlapJob_ManageShardCharacterData, m_wShardID) == 0x2A);
C_ASSERT(FIELD_OFFSET(COverlapJob_ManageShardCharacterData, m_strName) == 0x30);
C_ASSERT(FIELD_OFFSET(COverlapJob_ManageShardCharacterData, m_strName2) == 0x4C);
C_ASSERT(sizeof(COverlapJob_ManageShardCharacterData) == 0x68);

// ---------------------------------------------------------------------------------------------
// COverlapJob_RefreshShardUser (0x2C bytes): the user count of every shard to the division DB
// ("exec _AddCurrentUserCount" with the local time).
struct SHARD_USER_ITEM
{
    WORD    wShardID;       // +0
    WORD    wUserCount;     // +2   CERT_SHARD::w557
};

C_ASSERT(sizeof(SHARD_USER_ITEM) == 4);

class COverlapJob_RefreshShardUser : public ServerFramework::COverlapJob_Local
{
public:
    // COverlapJob_RefreshShardUser::ctor (0x41afe0)
    COverlapJob_RefreshShardUser() {}

    // vf01 (0x413b50): Init, then the user count of every shard.
    virtual void Init(CTask* pOwner);                           // 1
    virtual void ReturnToPool();                                // 3  (0x413b00)
    virtual bool Prepare();                                     // 5  (0x413c30) true
    // vf06 (0x413c40): false (list kept) when a query fails, else the list is emptied.
    virtual bool Execute();                                     // 6
    virtual bool OnComplete();                                  // 7  (0x413e40) true
    virtual bool OnFail();                                      // 8  (0x413e50) true

public:
    std::list<SHARD_USER_ITEM>  m_listShard;    // +20
};

C_ASSERT(sizeof(COverlapJob_RefreshShardUser) == 0x2C);

// ---------------------------------------------------------------------------------------------
// COverlapJob_UpdatePlayTimeOfUser (0x28 bytes): writes the play time of a user that logged out
// to TB_USER.
class COverlapJob_UpdatePlayTimeOfUser : public ServerFramework::COverlapJob_Local
{
public:
    // COverlapJob_UpdatePlayTimeOfUser::ctor (0x41b120)
    COverlapJob_UpdatePlayTimeOfUser() {}

    // vf03 (0x413e80): releases the ID and goes back to its pool (jobs with an ID only).
    virtual void ReturnToPool();                                // 3
    // vf05 (0x413f00): true.
    virtual bool Prepare();                                     // 5
    // vf06 (0x413f10): "update TB_USER set AccPlayTime = ..., LatestUpdateTime_ToPlayTime = <now>
    // where JID = ..." on the account DB (connection 3); true unless the query fails.
    virtual bool Execute();                                     // 6
    // vf07 / vf08 (0x413f90 / 0x413fa0): true.
    virtual bool OnComplete();                                  // 7
    virtual bool OnFail();                                      // 8

    // sub_413ED0: Init with the main process' task, then the data.
    void Setup(DWORD dwJID, WORD wPlayTime);

public:
    DWORD   m_dwJID;        // +20
    WORD    m_wPlayTime;    // +24
};

C_ASSERT(FIELD_OFFSET(COverlapJob_UpdatePlayTimeOfUser, m_dwJID) == 0x20);
C_ASSERT(sizeof(COverlapJob_UpdatePlayTimeOfUser) == 0x28);

// ---------------------------------------------------------------------------------------------
// COverlapJob_ResetPlayTimeOfGlobalServer (0x2868 bytes): reads (type 0) or writes (type 1) the
// time of the last daily play time reset (_WriteOutResetPlayTime, account DB, connection 3). The
// query is a member, prepared by Setup.
class COverlapJob_ResetPlayTimeOfGlobalServer : public ServerFramework::COverlapJob_Local
{
public:
    // COverlapJob_ResetPlayTimeOfGlobalServer::ctor (0x41b230)
    COverlapJob_ResetPlayTimeOfGlobalServer() {}

    virtual void ReturnToPool();                                // 3  (0x413fd0)
    virtual bool Prepare();                                     // 5  (0x414110) true
    // vf06 (0x414120): the prepared query; true unless it fails.
    virtual bool Execute();                                     // 6
    // vf07 (0x414160): type 0 stores the time read in the main process. True.
    virtual bool OnComplete();                                  // 7
    virtual bool OnFail();                                      // 8  (0x414180) true

    // sub_414020: Init with the main process' task and the query of the type; false (asserted)
    // for other types.
    bool Setup(int nType, DWORD dwTime);

public:
    int         m_nType;        // +20
    std::string m_strQuery;     // +24
    DWORD       m_dwTime;       // +40  row of type 0
    CDBQuery    m_Query;        // +44
};

C_ASSERT(FIELD_OFFSET(COverlapJob_ResetPlayTimeOfGlobalServer, m_strQuery) == 0x24);
C_ASSERT(FIELD_OFFSET(COverlapJob_ResetPlayTimeOfGlobalServer, m_dwTime) == 0x40);
C_ASSERT(FIELD_OFFSET(COverlapJob_ResetPlayTimeOfGlobalServer, m_Query) == 0x44);
C_ASSERT(sizeof(COverlapJob_ResetPlayTimeOfGlobalServer) == 0x2868);

// ---------------------------------------------------------------------------------------------
// COverlapJob_CustomLoginJob (0x30 bytes): "exec _AddLoginHistory" for every entry of the map.
// Its pool and allocation exist, but nothing in GlobalManager.exe allocates the job.
typedef std::map<DWORD, DWORD> CustomLoginMap;

class COverlapJob_CustomLoginJob : public ServerFramework::COverlapJob_Local
{
public:
    // COverlapJob_CustomLoginJob::ctor (0x4227c0)
    COverlapJob_CustomLoginJob()
    {
        m_dw20 = 0xFFFFFFFF;
    }

    virtual void ReturnToPool();                                // 3  (0x422770)
    virtual bool Prepare();                                     // 5  (0x422720) true
    // vf06 (0x422940): false when the map is empty or m_dw20 is -1; the results are not
    // checked; the map is emptied. True.
    virtual bool Execute();                                     // 6
    virtual bool OnComplete();                                  // 7  (0x422730) true
    virtual bool OnFail();                                      // 8  (0x422740) true

public:
    DWORD           m_dw20;         // +20  third argument of the procedure
    CustomLoginMap  m_map;          // +24  first and second argument
};

C_ASSERT(FIELD_OFFSET(COverlapJob_CustomLoginJob, m_map) == 0x24);
C_ASSERT(sizeof(COverlapJob_CustomLoginJob) == 0x30);

// ---------------------------------------------------------------------------------------------
// Pools (0x59A550 .. 0x59A8B0) and allocation with a job ID.

extern CChunkAllocatorST<COverlapJob_CheckCharName> g_CheckCharNameJobPool;
extern CChunkAllocatorST<sCU_Cash_Changed> g_CUCashChangedPool;
extern CChunkAllocatorST<COverlapJob_RefreshCommercialUser_Cash>
    g_RefreshCommercialUserCashJobPool;
extern CChunkAllocatorST<COverlapJob_RefreshShardStatus> g_RefreshShardStatusJobPool;
extern CChunkAllocatorST<COverlapJob_ManageShardCharacterData>
    g_ManageShardCharacterDataJobPool;
extern CChunkAllocatorST<COverlapJob_RefreshShardUser> g_RefreshShardUserJobPool;
extern CChunkAllocatorST<COverlapJob_UpdatePlayTimeOfUser> g_UpdatePlayTimeOfUserJobPool;
extern CChunkAllocatorST<COverlapJob_ResetPlayTimeOfGlobalServer> g_ResetPlayTimeJobPool;
extern CChunkAllocatorST<COverlapJob_CustomLoginJob> g_CustomLoginJobPool;

// sub_4129D0, sub_413050, sub_4134F0, sub_4137E0, sub_413AE0, sub_413E60, sub_413FB0, sub_422750
COverlapJob_CheckCharName* OverlapJob_AllocCheckCharName();
COverlapJob_RefreshCommercialUser_Cash* OverlapJob_AllocRefreshCommercialUserCash();
COverlapJob_RefreshShardStatus* OverlapJob_AllocRefreshShardStatus();
COverlapJob_ManageShardCharacterData* OverlapJob_AllocManageShardCharacterData();
COverlapJob_RefreshShardUser* OverlapJob_AllocRefreshShardUser();
COverlapJob_UpdatePlayTimeOfUser* OverlapJob_AllocUpdatePlayTimeOfUser();
COverlapJob_ResetPlayTimeOfGlobalServer* OverlapJob_AllocResetPlayTime();
COverlapJob_CustomLoginJob* OverlapJob_AllocCustomLoginJob();
// sub_413000: a row object from the pool, marked as in use.
sCU_Cash_Changed* CUCashChangedPool_Alloc();
