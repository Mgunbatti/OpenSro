#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>

#include "GlobalManagerSvcFwd.h"
#include "BSLib/ChunkAllocator.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerFramework/OverlapJob.h"

// Overlap jobs of the service manager (and the module data job of the main process). Most of
// them run one query on the overlap process and answer the SMC with a massive message: the
// answer starts with BYTE 1, every row follows as BYTE 1 + record, BYTE 0 ends the rows; a
// failed query answers {2} (some {2, error}). The allocation and setup functions are declared
// in GlobalManagerSvcFwd.h; the pools are named after the classes.

using ServerFramework::CMassiveMsg;

// COverlapJob_ModuleVersionData (0x39C bytes): the valid module versions and files (0xA306) to
// a GatewayServer or DownloadServer after a patch was recorded.
class COverlapJob_ModuleVersionData : public ServerFramework::COverlapJob_Local
{
public:
    // ctor (inlined in the pool's element construction)
    COverlapJob_ModuleVersionData() {}

    virtual void ReturnToPool();                                // 3  (0x412c60)
    // vf05 (0x412ce0): the session must be open (else the reply is freed).
    virtual bool Prepare();                                     // 5
    // vf06 (0x412d10): the module versions, then the files.
    virtual bool Execute();                                     // 6
    // vf07 (0x412ed0): the reply to the session (freed when it closed: false).
    virtual bool OnComplete();                                  // 7
    // vf08 (0x412f10): the reply is freed. True.
    virtual bool OnFail();                                      // 8

    int OnModuleRow(DWORD dwQuery, DWORD dwUnused);             // sub_412F20
    int OnFileRow(DWORD dwQuery, DWORD dwUnused);               // sub_412F90

public:
    DWORD               m_dwSessionID;  // +20
    CMassiveMsg*        m_pReply;       // +24
    MODULE_VERSION      m_Module;       // +28  row
    MODULE_VERSION_FILE m_File;         // +174 row
};

C_ASSERT(FIELD_OFFSET(COverlapJob_ModuleVersionData, m_Module) == 0x28);
C_ASSERT(FIELD_OFFSET(COverlapJob_ModuleVersionData, m_File) == 0x174);
C_ASSERT(sizeof(COverlapJob_ModuleVersionData) == 0x39C);

// COverlapJob_SM_ModuleVersion (0x174 bytes): 0x7003, the valid module versions (0xB003).
class COverlapJob_SM_ModuleVersion : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x424cc0)
    virtual bool Prepare();                                     // 5  (0x424e00) true
    virtual bool Execute();                                     // 6  (0x424e10)
    // vf07 (0x424f40): the reply.
    virtual bool OnComplete();                                  // 7
    // vf08 (0x424f60): a new reply {2}.
    virtual bool OnFail();                                      // 8

    int OnModuleRow(DWORD dwQuery, DWORD dwUnused);             // sub_424FB0

public:
    CMassiveMsg*        m_pReply;       // +20
    DWORD               m_dwSessionID;  // +24
    MODULE_VERSION      m_Module;       // +28
};

C_ASSERT(sizeof(COverlapJob_SM_ModuleVersion) == 0x174);

// COverlapJob_SM_ModuleVersionFile (0x250 bytes): 0x7004, the valid files of a module
// (0xB004).
class COverlapJob_SM_ModuleVersionFile : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x424d30)
    virtual bool Prepare();                                     // 5  (0x425060) true
    virtual bool Execute();                                     // 6  (0x425070)
    virtual bool OnComplete();                                  // 7  (0x4251b0)
    virtual bool OnFail();                                      // 8  (0x4251d0) {2}

    int OnFileRow(DWORD dwQuery, DWORD dwUnused);               // sub_425220

public:
    CMassiveMsg*        m_pReply;       // +20
    DWORD               m_dwSessionID;  // +24
    MODULE_VERSION_FILE m_File;         // +28
    BYTE                m_byModuleID;   // +24E
    BYTE                m_byContentID;  // +24F
};

C_ASSERT(FIELD_OFFSET(COverlapJob_SM_ModuleVersionFile, m_byModuleID) == 0x24E);
C_ASSERT(sizeof(COverlapJob_SM_ModuleVersionFile) == 0x250);

// COverlapJob_RecordPatchData (0x2C bytes): an uploaded patch to _ModuleVersionFile and
// _ModuleVersion (old rows invalidated); progress 0x3101 {2, 2, percent} and the result 0x3102
// to the SMC; on success the module data is sent again to every GatewayServer and
// DownloadServer.
class COverlapJob_RecordPatchData : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x4252b0)
    virtual bool Prepare();                                     // 5  (0x425320) true
    // vf06 (0x425330): false when a query fails or the SMC's session closed (then the session
    // is forgotten).
    virtual bool Execute();                                     // 6
    // vf07 (0x425690): ModuleVersionData jobs, 0x3102 {1}.
    virtual bool OnComplete();                                  // 7
    // vf08 (0x4257d0): 0x3102 {2, 4}.
    virtual bool OnFail();                                      // 8

public:
    const MODULE_VERSION*   m_pModule;      // +20
    ModuleVersionFileList*  m_pFiles;       // +24
    DWORD                   m_dwSessionID;  // +28  0: no SMC to tell
};

C_ASSERT(sizeof(COverlapJob_RecordPatchData) == 0x2C);

// COverlapJob_RefreshSecurityData_n_SendToSM (0x24 bytes): reloads the security descriptions
// and the privileged addresses, then sends them (0x3306) to the SMC.
class COverlapJob_RefreshSecurityData_n_SendToSM : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x425870)
    virtual bool Prepare();                                     // 5  (0x4258e0) true
    virtual bool Execute();                                     // 6  (0x4258f0)
    virtual bool OnComplete();                                  // 7  (0x425be0) {1, data}
    virtual bool OnFail();                                      // 8  (0x425c30) {2}

    // sub_425C70: the record type is the query's m_dwParam (0 setting, 1 group, 2 group
    // address), the list its m_pUserData.
    int OnSecurityRow(DWORD dwQuery, DWORD dwUnused);

public:
    DWORD   m_dwSessionID;      // +20
};

C_ASSERT(sizeof(COverlapJob_RefreshSecurityData_n_SendToSM) == 0x24);

// COverlapJob_QueryUser (0x44 bytes): 0x7400, an SMC account (0xB400, SM_USER_RECORD rows).
class COverlapJob_QueryUser : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x425df0)
    virtual bool Prepare();                                     // 5  (0x425ea0) true
    virtual bool Execute();                                     // 6  (0x425eb0)
    virtual bool OnComplete();                                  // 7  (0x425fe0)
    virtual bool OnFail();                                      // 8  (0x426010) {2, 1}


    int OnRow(DWORD dwQuery, DWORD dwUnused);                   // sub_426070
public:
    DWORD           m_dwSessionID;  // +20
    std::string     m_strUserID;    // +24
    CMassiveMsg*    m_pReply;       // +40
};

C_ASSERT(sizeof(COverlapJob_QueryUser) == 0x44);

// COverlapJob_SMLog (0x5C bytes): one line of the SMC log (_SMCLog).
class COverlapJob_SMLog : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x426100)
    virtual bool Prepare();                                     // 5  (0x426190) true
    // vf06 (0x4261a0): the result is not checked. True.
    virtual bool Execute();                                     // 6
    virtual bool OnComplete();                                  // 7  (0x426240) true
    virtual bool OnFail();                                      // 8  (0x426250) true

public:
    std::string     m_strUserID;    // +20
    BYTE            m_byType;       // +3C
    std::string     m_strText;      // +40
};

C_ASSERT(FIELD_OFFSET(COverlapJob_SMLog, m_byType) == 0x3C);
C_ASSERT(sizeof(COverlapJob_SMLog) == 0x5C);

// COverlapJob_QuerySMLog (0x80 bytes): 0x7404, lines of the SMC log (0xB404).
class COverlapJob_QuerySMLog : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x426280)
    virtual bool Prepare();                                     // 5  (0x426370) true
    virtual bool Execute();                                     // 6  (0x426380)
    virtual bool OnComplete();                                  // 7  (0x4264e0)
    virtual bool OnFail();                                      // 8  (0x426510) {2}


    int OnRow(DWORD dwQuery, DWORD dwUnused);                   // sub_426550
public:
    DWORD           m_dwSessionID;  // +20
    std::string     m_strUserID;    // +24
    BYTE            m_byType;       // +40
    std::string     m_strBegin;     // +44
    std::string     m_strEnd;       // +60
    CMassiveMsg*    m_pReply;       // +7C
};

C_ASSERT(FIELD_OFFSET(COverlapJob_QuerySMLog, m_strBegin) == 0x44);
C_ASSERT(sizeof(COverlapJob_QuerySMLog) == 0x80);

// COverlapJob_QueryContentUserAccount (0x48 bytes): 0x7405, game accounts (0xB406).
class COverlapJob_QueryContentUserAccount : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x4265e0)
    virtual bool Prepare();                                     // 5  (0x426690) true
    virtual bool Execute();                                     // 6  (0x4266a0)
    virtual bool OnComplete();                                  // 7  (0x4267e0)
    virtual bool OnFail();                                      // 8  (0x426810) {2}


    int OnRow(DWORD dwQuery, DWORD dwUnused);                   // sub_426850
public:
    DWORD           m_dwSessionID;  // +20
    BYTE            m_byType;       // +24
    std::string     m_strQuery;     // +28
    CMassiveMsg*    m_pReply;       // +44
};

C_ASSERT(FIELD_OFFSET(COverlapJob_QueryContentUserAccount, m_strQuery) == 0x28);
C_ASSERT(sizeof(COverlapJob_QueryContentUserAccount) == 0x48);

// COverlapJob_QueryPunishment (0x48 bytes): 0x7407, an account and its punishments (0xB407).
class COverlapJob_QueryPunishment : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x4268e0)
    virtual bool Prepare();                                     // 5  (0x426990) true
    virtual bool Execute();                                     // 6  (0x4269a0)
    virtual bool OnComplete();                                  // 7  (0x426c10)
    virtual bool OnFail();                                      // 8  (0x426c40) {2}


    int OnRow(DWORD dwQuery, DWORD dwUnused);                   // sub_426C80
public:
    DWORD           m_dwSessionID;  // +20
    std::string     m_strUserID;    // +24
    DWORD           m_dw40;         // +40
    CMassiveMsg*    m_pReply;       // +44
};

C_ASSERT(FIELD_OFFSET(COverlapJob_QueryPunishment, m_pReply) == 0x44);
C_ASSERT(sizeof(COverlapJob_QueryPunishment) == 0x48);

// COverlapJob_PunishmentJob (0x124 bytes): 0x7408, registers (job 0) or updates (job 1) a
// punishment; 0xB408 {1, job} or {2, job, error}. Error: 1 query failed or the procedure
// returned another negative value, 2 / 3 it returned -1 (register / update), 4 unknown job.
class COverlapJob_PunishmentJob : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x426d10)
    virtual bool Prepare();                                     // 5  (0x426f00) true
    virtual bool Execute();                                     // 6  (0x426f10)
    // vf07 (0x4274f0): the answer, then the punishment manager gets the punishment.
    virtual bool OnComplete();                                  // 7
    virtual bool OnFail();                                      // 8  (0x4275a0)

public:
    BYTE            m_byJob;        // +20
    DWORD           m_dwSessionID;  // +24
    DWORD           m_dwSerial;     // +28
    DWORD           m_dwJID;        // +2C
    BYTE            m_byType;       // +30
    std::string     m_str1;         // +34
    WORD            m_wShardID;     // +50
    std::string     m_str2;         // +54
    std::string     m_str3;         // +70
    std::string     m_str4;         // +8C
    std::string     m_str5;         // +A8
    std::string     m_str6;         // +C4
    // The four 16-byte dates of the request; they are formatted as TIMESTAMP_STRUCT.
    SYSTEMTIME      m_t1;           // +E0
    SYSTEMTIME      m_t2;           // +F0  begin of the punishment
    SYSTEMTIME      m_t3;           // +100 end of the punishment
    SYSTEMTIME      m_t4;           // +110
    BYTE            m_byError;      // +120
};

C_ASSERT(FIELD_OFFSET(COverlapJob_PunishmentJob, m_str1) == 0x34);
C_ASSERT(FIELD_OFFSET(COverlapJob_PunishmentJob, m_wShardID) == 0x50);
C_ASSERT(FIELD_OFFSET(COverlapJob_PunishmentJob, m_str6) == 0xC4);
C_ASSERT(FIELD_OFFSET(COverlapJob_PunishmentJob, m_t1) == 0xE0);
C_ASSERT(FIELD_OFFSET(COverlapJob_PunishmentJob, m_byError) == 0x120);
C_ASSERT(sizeof(COverlapJob_PunishmentJob) == 0x124);

// COverlapJob_AutomatedPunishmentJob (0x98 bytes): a punishment registered by the user
// validation (no answer).
class COverlapJob_AutomatedPunishmentJob : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x427640)
    virtual bool Prepare();                                     // 5  (0x4276f0) true
    // vf06 (0x427700): the result is logged when the query or the procedure fails. True.
    virtual bool Execute();                                     // 6
    virtual bool OnComplete();                                  // 7  (0x427870) true
    virtual bool OnFail();                                      // 8  (0x427880) true

public:
    int             m_nResult;      // +20  the procedure's result when not negative
    std::string     m_strUserID;    // +24
    std::string     m_str2;         // +40
    std::string     m_str3;         // +5C
    std::string     m_str4;         // +78
    DWORD           m_dwParam;      // +94
};

C_ASSERT(FIELD_OFFSET(COverlapJob_AutomatedPunishmentJob, m_str4) == 0x78);
C_ASSERT(sizeof(COverlapJob_AutomatedPunishmentJob) == 0x98);

// COverlapJob_QueryCharOwnedUser (0x44 bytes): 0x7409, the account of a character (0xB409).
// COverlapJob_QueryUserByUserID (0x44 bytes): 0x7410, an account by name (also 0xB409).
class COverlapJob_QueryCharOwnedUser : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x4278b0)
    virtual bool Prepare();                                     // 5  (0x427960) true
    virtual bool Execute();                                     // 6  (0x427970)
    virtual bool OnComplete();                                  // 7  (0x427b30)
    virtual bool OnFail();                                      // 8  (0x427b60) {2}


    int OnRow(DWORD dwQuery, DWORD dwUnused);                   // sub_427AC0
public:
    DWORD           m_dwSessionID;  // +20
    std::string     m_strName;      // +24
    CMassiveMsg*    m_pReply;       // +40
};

C_ASSERT(sizeof(COverlapJob_QueryCharOwnedUser) == 0x44);

class COverlapJob_QueryUserByUserID : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x427bc0)
    virtual bool Prepare();                                     // 5  (0x427c70) true
    virtual bool Execute();                                     // 6  (0x427c80)
    virtual bool OnComplete();                                  // 7  (0x427dd0)
    virtual bool OnFail();                                      // 8  (0x427e00) {2}


    int OnRow(DWORD dwQuery, DWORD dwUnused);                   // sub_427E40
public:
    DWORD           m_dwSessionID;  // +20
    std::string     m_strUserID;    // +24
    CMassiveMsg*    m_pReply;       // +40
};

C_ASSERT(sizeof(COverlapJob_QueryUserByUserID) == 0x44);

// COverlapJob_PreparePatch (0x30 bytes): 0x7104, the latest version and files of a module
// (0xB104 {1, version rows, 0, file rows, 0}; {2, 3} on failure).
class COverlapJob_PreparePatch : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x427ed0)
    // vf05 (0x427f80): the session must still be open.
    virtual bool Prepare();                                     // 5
    virtual bool Execute();                                     // 6  (0x427fa0)
    virtual bool OnComplete();                                  // 7  (0x4281f0)
    virtual bool OnFail();                                      // 8  (0x428210)

    // sub_428270: m_pUserData 0 module version, 1 file; m_dwParam is the row buffer.
    int OnPatchRow(DWORD dwQuery, DWORD dwUnused);

public:
    DWORD           m_dwSessionID;  // +20
    BYTE            m_byModuleID;   // +24
    BYTE            m_byContentID;  // +25
    CMassiveMsg*    m_pReply;       // +28
    BOOL            m_bFound;       // +2C  a row was written
};

C_ASSERT(sizeof(COverlapJob_PreparePatch) == 0x30);

// COverlapJob_ArticleJob (0x68 bytes): the articles (_Notice) of a content: type 0 query
// (massive 0xB206), 1 add (0xB207), 2 delete (0xB208), 3 edit (0xB209).
class COverlapJob_ArticleJob : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x428350)
    virtual bool Prepare();                                     // 5  (0x428440) true
    virtual bool Execute();                                     // 6  (0x428450)
    virtual bool OnComplete();                                  // 7  (0x428640)
    virtual bool OnFail();                                      // 8  (0x428700)

    int OnArticleRow(DWORD dwQuery, DWORD dwUnused);            // sub_4287E0

    // The answer of types 1..3 (shared code of vf07 / vf08).
    void Answer(BYTE byResult);

public:
    DWORD           m_dwSessionID;  // +20
    BYTE            m_byContentID;  // +24
    BYTE            m_byType;       // +25
    DWORD           m_dwArticleID;  // +28
    std::string     m_strTitle;     // +2C
    std::string     m_strText;      // +48
    CMassiveMsg*    m_pReply;       // +64  type 0 only
};

C_ASSERT(FIELD_OFFSET(COverlapJob_ArticleJob, m_strTitle) == 0x2C);
C_ASSERT(FIELD_OFFSET(COverlapJob_ArticleJob, m_pReply) == 0x64);
C_ASSERT(sizeof(COverlapJob_ArticleJob) == 0x68);

// COverlapJob_ConcurrentUserLogJob (0x64 bytes): 0x7412, the concurrent user log of a shard
// (0xB412 {WORD shard, rows of 38 bytes, 0}).
class COverlapJob_ConcurrentUserLogJob : public ServerFramework::COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x4288f0)
    virtual bool Prepare();                                     // 5  (0x4289a0) true
    virtual bool Execute();                                     // 6  (0x4289b0)
    virtual bool OnComplete();                                  // 7  (0x428b20)
    // vf08 (0x428b40): BYTE 0 and the reply as it is.
    virtual bool OnFail();                                      // 8

    int OnLogRow(DWORD dwQuery, DWORD dwUnused);                // sub_428B70

public:
    DWORD           m_dwSessionID;  // +20
    CMassiveMsg*    m_pReply;       // +24
    WORD            m_wShardID;     // +28
    BYTE            m_byType;       // +2A
    std::string     m_strBegin;     // +2C
    std::string     m_strEnd;       // +48
};

C_ASSERT(FIELD_OFFSET(COverlapJob_ConcurrentUserLogJob, m_strBegin) == 0x2C);
C_ASSERT(sizeof(COverlapJob_ConcurrentUserLogJob) == 0x64);

// ---------------------------------------------------------------------------------------------
// Pools (0x59A5B0, 0x59A910 .. 0x59AF10)

extern CChunkAllocatorST<COverlapJob_ModuleVersionData> g_ModuleVersionDataJobPool;
extern CChunkAllocatorST<COverlapJob_SM_ModuleVersion> g_SMModuleVersionJobPool;
extern CChunkAllocatorST<COverlapJob_SM_ModuleVersionFile> g_SMModuleVersionFileJobPool;
extern CChunkAllocatorST<COverlapJob_RecordPatchData> g_RecordPatchDataJobPool;
extern CChunkAllocatorST<COverlapJob_RefreshSecurityData_n_SendToSM> g_RefreshSecurityDataJobPool;
extern CChunkAllocatorST<COverlapJob_QueryUser> g_QueryUserJobPool;
extern CChunkAllocatorST<COverlapJob_SMLog> g_SMLogJobPool;
extern CChunkAllocatorST<COverlapJob_QuerySMLog> g_QuerySMLogJobPool;
extern CChunkAllocatorST<COverlapJob_QueryContentUserAccount> g_QueryContentUserAccountJobPool;
extern CChunkAllocatorST<COverlapJob_QueryPunishment> g_QueryPunishmentJobPool;
extern CChunkAllocatorST<COverlapJob_PunishmentJob> g_PunishmentJobPool;
extern CChunkAllocatorST<COverlapJob_AutomatedPunishmentJob> g_AutomatedPunishmentJobPool;
extern CChunkAllocatorST<COverlapJob_QueryCharOwnedUser> g_QueryCharOwnedUserJobPool;
extern CChunkAllocatorST<COverlapJob_QueryUserByUserID> g_QueryUserByUserIDJobPool;
extern CChunkAllocatorST<COverlapJob_PreparePatch> g_PreparePatchJobPool;
extern CChunkAllocatorST<COverlapJob_ArticleJob> g_ArticleJobPool;
extern CChunkAllocatorST<COverlapJob_ConcurrentUserLogJob> g_ConcurrentUserLogJobPool;

// COverlapJob_SMLog: sub_4260E0 / sub_426150 (Init, user ID, type, text).
SM_JOB* OverlapJob_AllocSMLog();
void OverlapJob_SetupSMLog(CTask* pTask, SM_JOB* pJob, std::string* pstrUserID, BYTE byType,
                           const char* pszText);
