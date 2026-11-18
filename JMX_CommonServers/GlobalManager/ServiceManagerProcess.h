#pragma once

#include <winsock2.h>
#include <windows.h>
#include <map>
#include <set>
#include <string>
#include <sql.h>

#include "BSLib/ChunkAllocator.h"
#include "BSLib/thread/Task.h"
#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerFramework/OverlapJob.h"
#include "ServerFramework/ServerBody.h"

// The service manager part of GlobalManager: the SMC (service manager console) sessions. An SMC
// logs in with 0x7001 and then controls the division: module versions and patches, launching
// and stopping server bodies, the service mode, shard settings, the content users, the security
// descriptions, the SMC accounts, articles and punishments. Requests for other bodies are relayed
// (0x6008) with an sSMDefault_RelayMsg_Param as context and answered to the SMC when the answer
// (0xA008) or the relay failure comes back. Requests 0x7xxx are answered with 0xBxxx.
//
// CServiceManagerProcess is a member of the main process (CGlobalManagerMainProcess +0x400CC);
// the main process passes it the messages of its task (Dispatch), the closed sessions, the
// relay failures and the file transfer events.

class __single_inheritance CServiceManagerProcess;

using ServerFramework::CMassiveMsg;

// The account of a logged-in SMC (0x48 bytes, g_ServiceManagerUserPool).
struct sServiceManagerUser
{
    // sub_442DD0 (the pool's element constructor)
    sServiceManagerUser()
    {
        bInUse = 0;
    }

    // sServiceManagerUser::vf00 (0x442e60): back to the pool (only once, see bInUse).
    virtual void __stdcall Release();                               // 0

    DWORD           dwID;           // +4   nID of the account (_CertifyUser)
    std::string     strID;          // +8
    std::string     strPassword;    // +24
    DWORD           dwSessionID;    // +40
    BYTE            bySecurity;     // +44  the byte after nID in the certification row
    BYTE            bInUse;         // +45  set by ServiceManagerUserPool_Alloc
};

C_ASSERT(FIELD_OFFSET(sServiceManagerUser, strID) == 0x08);
C_ASSERT(FIELD_OFFSET(sServiceManagerUser, strPassword) == 0x24);
C_ASSERT(FIELD_OFFSET(sServiceManagerUser, dwSessionID) == 0x40);
C_ASSERT(FIELD_OFFSET(sServiceManagerUser, bySecurity) == 0x44);
C_ASSERT(FIELD_OFFSET(sServiceManagerUser, bInUse) == 0x45);
C_ASSERT(sizeof(sServiceManagerUser) == 0x48);

// Context of a relayed SMC request (0x10 bytes, g_SMRelayParamPool): who asked and about what.
struct sSMDefault_RelayMsg_Param
{
    // sSMDefault_RelayMsg_Param::ctor (the pool's element constructor, inlined in 0x42f820)
    sSMDefault_RelayMsg_Param()
    {
        bInUse = 0;
    }

    // vf00 (0x424d90): back to the pool (only once, see bInUse).
    virtual void __stdcall Release();                               // 0

    DWORD   dwSessionID;    // +4   the SMC session
    DWORD   dw8;            // +8   tick of the request, operation, JID or opcode
    WORD    wBodyID;        // +C   target body
    BYTE    bInUse;         // +E   set by SMRelayParamPool_Alloc
};

C_ASSERT(FIELD_OFFSET(sSMDefault_RelayMsg_Param, wBodyID) == 0x0C);
C_ASSERT(FIELD_OFFSET(sSMDefault_RelayMsg_Param, bInUse) == 0x0E);
C_ASSERT(sizeof(sSMDefault_RelayMsg_Param) == 0x10);

// A DownloadServer that gets an uploaded patch (value of m_mapPatchTarget, by session).
struct SM_PATCH_TARGET
{
    SERVER_BODY*    pBody;          // +0
    DWORD           dwSessionID;    // +4
    WORD            wFilesLeft;     // +8   files it has not confirmed yet
    BYTE            bSent;          // +A   0x6305 was sent
    BYTE            bB;             // +B
};

C_ASSERT(sizeof(SM_PATCH_TARGET) == 12);

#pragma pack(push, 1)
// An SMC account as sent with 0x7401 / 0x7403 (0x206 bytes).
struct SM_USER_RECORD
{
    DWORD   dwID;               // +0
    char    szUserID[128];      // +4
    char    szPassword[32];     // +84  not terminated in the message
    char    szName[16];         // +A4
    char    szA[32];            // +B4  not terminated in the message
    char    szB[32];            // +D4  not terminated in the message
    char    szC[128];           // +F4
    char    szD[128];           // +174
    BYTE    by1F4;              // +1F4
    BYTE    by1F5;              // +1F5
    char    sz1F6[16];          // +1F6
};
#pragma pack(pop)

C_ASSERT(FIELD_OFFSET(SM_USER_RECORD, szPassword) == 0x84);
C_ASSERT(FIELD_OFFSET(SM_USER_RECORD, by1F4) == 0x1F4);
C_ASSERT(sizeof(SM_USER_RECORD) == 0x206);

typedef std::map<DWORD, sServiceManagerUser*>   SMUserMap;
typedef std::map<DWORD, SM_PATCH_TARGET>        SMPatchTargetMap;

// Handler of an SMC request or of the answer to a relayed request. Requests come as pMsg (or
// pMassive for massive messages); answers carry their sSMDefault_RelayMsg_Param as dwContext.
typedef void (CServiceManagerProcess::*PFN_SM_HANDLER)(CMsg* pMsg, DWORD dwRelayID,
                                                        DWORD dwContext, CMassiveMsg* pMassive);
C_ASSERT(sizeof(PFN_SM_HANDLER) == 4);

class CServiceManagerProcess
{
public:
    // sub_442EA0: empty containers and table, then the handlers.
    CServiceManagerProcess();
    // CServiceManagerProcess::dtor (0x443290; deleting dtor 0x443180): releases the users.
    virtual ~CServiceManagerProcess();                              // 0

    // sub_443470 (CGlobalManagerMainProcess::vf06): the main process' task; no patch upload
    // and no patch preparation in progress.
    void Init(CTask* pTask);
    // sub_4434A0 (CGlobalManagerMainProcess::vf09): calls the handler of the opcode of pMsg
    // (or of pMassive when pMsg is NULL); true when one is registered, else the result of
    // OnUnhandledAnswer.
    bool Dispatch(CMsg* pMsg, CMassiveMsg* pMassive, DWORD dwRelayID, DWORD dwContext);
    // sub_446930: an answer without handler to a request relayed for an SMC (0x7203): 0xB203
    // {1, WORD opcode, data after the leading string} to the SMC; logged when the answer's
    // result byte is not 2. False for requests (opcode 0x2xxx or 0x3xxx) and without message.
    bool OnUnhandledAnswer(CMsg* pMsg, CMassiveMsg* pMassive, sSMDefault_RelayMsg_Param* pParam);
    // sub_444050 (CGlobalManagerMainProcess::vf17): a request relayed for an SMC failed; the
    // SMC gets the failure answer of its request.
    static void OnRelayFailed(WORD wOpcode, sSMDefault_RelayMsg_Param* pParam);
    // sub_4444D0 (CGlobalManagerMainProcess::vf44): an SMC session was closed.
    void OnSessionClosed(DWORD dwSessionID);
    // sub_445BB0 (CGlobalManagerMainProcess::vf47): a DownloadServer received a patch file.
    void OnFileDone(DWORD dwSessionID);
    // sub_445F50 (CGlobalManagerMainProcess::vf48): receiving the patch from the SMC failed.
    void OnFileFailed();
    // sub_4461B0 (CGlobalManagerMainProcess::vf42): a cord was closed while the patch is sent
    // to the DownloadServers.
    void OnCordClosed();

    // sub_445AE0: the user of the session, NULL if none.
    sServiceManagerUser* FindUser(DWORD dwSessionID);
    // sub_4445E0: sends pMsg to every logged-in SMC and releases it.
    void BroadcastToUsers(CMsg* pMsg);
    // CServiceManagerProcess_PostJob_443520: the job to the main process' overlap task.
    bool PostJob(ServerFramework::COverlapJob_Local* pJob);
    // sub_445750: ends the patch upload and its file transfer; logs "patch upload is canceled".
    void CancelPatchUpload();
    // sub_4457C0: 0x3103 {1} to the session (another patch upload is in progress).
    static void SendPatchBusy(DWORD dwSessionID);
    // sub_445B40: true when every DownloadServer confirmed all files.
    bool IsPatchDelivered();
    // sub_445FE0: 0x6305 {WORD count, files} to every online DownloadServer; false (nothing
    // sent) when one is not online.
    bool SendPatchToDownloadServers();
    // sub_4479D0: a RefreshSecurityData_n_SendToSM job for the session.
    void RefreshSecurityData(DWORD dwSessionID);
    // sub_44A960: an AutomatedPunishmentJob (the account and three texts of the validation
    // history, then a number); logged when it cannot be posted.
    bool RegisterAutomatedPunishment(std::string* pstrUserID, std::string* pstr2,
                                     std::string* pstr3, std::string* pstr4, DWORD dwParam);

    // Requests of the SMC (registered by the constructor).
    void OnMsg_7001(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x443540 login
    void OnMsg_7002(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x443A40 architecture
    void OnMsg_7003(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x443E70 module versions
    void OnMsg_7004(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x443EB0 module version files
    void OnMsg_7005(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x443F50 ping a body
    void OnMsg_7006(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x444830 launch a server
    void OnMsg_7007(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x444A20 shut a server down
    void OnMsg_7008(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x445140 service mode
    void OnMsg_7009(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x445470 body status
    void OnMsg_7010(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x4490D0 shard name / limit
    void OnMsg_7011(CMsg*, DWORD, DWORD, CMassiveMsg*);     // DB_Update_ShardService
    void OnMsg_7012(CMsg*, DWORD, DWORD, CMassiveMsg*);     // DB_Select_ShardService
    void OnMsg_7100(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x445820 patch upload
    void OnMsg_7104(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x448BF0 prepare patch
    void OnMsg_7200(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x446250 online user query
    void OnMsg_7201(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x446370 online user ban
    void OnMsg_7202(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x4465C0 to every gateway
    void OnMsg_7203(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x446770 relay to a body
    void OnMsg_7204(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x446C20 user counts
    void OnMsg_7205(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x449020 franchise addresses
    void OnMsg_7206(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x449A20 article query
    void OnMsg_7207(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x449A90 add article
    void OnMsg_7208(CMsg*, DWORD, DWORD, CMassiveMsg*);     // DB_Delete_Article
    void OnMsg_7209(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x449CA0 edit article
    void OnMsg_7300(CMsg*, DWORD, DWORD, CMassiveMsg*);     // DB_Insert_SecurityDescription
    void OnMsg_7301(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x446FC0
    void OnMsg_7302(CMsg*, DWORD, DWORD, CMassiveMsg*);     // DB_Insert_SecurityDescriptionGroup
    void OnMsg_7303(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x447360
    void OnMsg_7304(CMsg*, DWORD, DWORD, CMassiveMsg*);     // ..GroupAssign insert
    void OnMsg_7305(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x447650
    void OnMsg_7307(CMsg*, DWORD, DWORD, CMassiveMsg*);     // DB_Update_SecurityDescription
    void OnMsg_7308(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x444F70 0x631D to a body
    void OnMsg_7400(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x447A10 query user
    void OnMsg_7401(CMsg*, DWORD, DWORD, CMassiveMsg*);     // DB_AddUser
    void OnMsg_7402(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x447D10 delete user
    void OnMsg_7403(CMsg*, DWORD, DWORD, CMassiveMsg*);     // DB_EditUser
    void OnMsg_7404(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x448020 query SM log
    void OnMsg_7405(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x448240 content accounts
    void OnMsg_7407(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x4483A0 query punishment
    void OnMsg_7408(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x4484C0 punishment job
    void OnMsg_7409(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x4489D0 characters of user
    void OnMsg_7410(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x448AE0 user by user ID
    void OnMsg_7411(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x44A380 JID of an account
    void OnMsg_7412(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x44A7F0 concurrent user log
    void OnMsg_4023(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x44A9D0 (massive) to a body

    // Answers of relayed requests.
    void OnMsg_A300(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x444410
    void OnMsg_A301(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x444C10
    void OnMsg_A302(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x444D30
    void OnMsg_A303(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x445360
    void OnMsg_A304(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x445620
    void OnMsg_A305(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x445A90
    void OnMsg_A307(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x446700
    void OnMsg_A308(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x448E70
    void OnMsg_A20D(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x446CE0
    void OnMsg_A310(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x449520
    void OnMsg_A312(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x44A0F0 empty
    void OnMsg_A31D(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x444E50
    void OnMsg_6313(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x44A6B0 (from an AgentServer)
    void OnMsg_8023(CMsg*, DWORD, DWORD, CMassiveMsg*);     // 0x44AB40

    // Row triggers of the queries (CClassTrigger targets).
    int OnShardServiceRow(DWORD dwQuery, DWORD dwUnused);   // 0x44A320
    int OnUserJIDRow(DWORD dwQuery, DWORD dwUnused);        // 0x44A660

    // Defined with the SM log jobs (0x450B40..0x450F10, outside this part; see
    // GlobalManagerSvcFwd.h).
    void WriteLog(DWORD dwSessionID, BYTE byType, const char* pszFormat, ...);     // 0x450CA0
    void WriteQueryLog(DWORD dwSessionID, const char* pszFormat, ...);             // 0x450DE0
    void WriteAnswerLog(std::string* pstrText, DWORD dwSessionID, BYTE byType);    // 0x450B90
    void WriteUserLog(std::string* pstrText, std::string* pstrUserID);             // 0x450B40

public:
    SMUserMap           m_mapUser;              // +4    by session
    BYTE                m_bPatchUploading;      // +10
    int                 m_nPatchStep;           // +14   -1, 0 receiving, 1 delivering
    sServiceManagerUser* m_pPatchUser;          // +18
    MODULE_VERSION      m_PatchModule;          // +1C
    WORD                m_wPatchFileCount;      // +168
    WORD                m_wPatchFilesLeft;      // +16A  files the SMC still sends
    SMPatchTargetMap    m_mapPatchTarget;       // +16C
    BYTE                m_bPreparingPatch;      // +178
    DWORD               m_dwPrepareSession;     // +17C  SMC of the patch preparation
    BYTE                m_byPrepareModule;      // +180
    BYTE                m_byPrepare181;         // +181
    std::set<WORD>      m_setPrepareBody;       // +184  bodies that did not answer 0x6308 yet
    CTask*              m_pTask;                // +190
    // The table has room for opcodes 0..0xFFFE (0xFFFF reads past the object).
    PFN_SM_HANDLER      m_aHandler[0xFFFF];     // +194
};

C_ASSERT(FIELD_OFFSET(CServiceManagerProcess, m_mapUser) == 0x4);
C_ASSERT(FIELD_OFFSET(CServiceManagerProcess, m_bPatchUploading) == 0x10);
C_ASSERT(FIELD_OFFSET(CServiceManagerProcess, m_pPatchUser) == 0x18);
C_ASSERT(FIELD_OFFSET(CServiceManagerProcess, m_PatchModule) == 0x1C);
C_ASSERT(FIELD_OFFSET(CServiceManagerProcess, m_wPatchFileCount) == 0x168);
C_ASSERT(FIELD_OFFSET(CServiceManagerProcess, m_mapPatchTarget) == 0x16C);
C_ASSERT(FIELD_OFFSET(CServiceManagerProcess, m_bPreparingPatch) == 0x178);
C_ASSERT(FIELD_OFFSET(CServiceManagerProcess, m_dwPrepareSession) == 0x17C);
C_ASSERT(FIELD_OFFSET(CServiceManagerProcess, m_byPrepareModule) == 0x180);
C_ASSERT(FIELD_OFFSET(CServiceManagerProcess, m_setPrepareBody) == 0x184);
C_ASSERT(FIELD_OFFSET(CServiceManagerProcess, m_pTask) == 0x190);
C_ASSERT(FIELD_OFFSET(CServiceManagerProcess, m_aHandler) == 0x194);
C_ASSERT(sizeof(CServiceManagerProcess) == 0x40190);

// 0x59AF70 / 0x59A9D8: the pools (500 objects per block).
extern CChunkAllocatorST<sServiceManagerUser> g_ServiceManagerUserPool;
extern CChunkAllocatorST<sSMDefault_RelayMsg_Param> g_SMRelayParamPool;

// sServiceManagerUser_PoolAlloc_442E50 / sub_424D80: an object from the pool, marked as in use.
sServiceManagerUser* ServiceManagerUserPool_Alloc();
sSMDefault_RelayMsg_Param* SMRelayParamPool_Alloc();

// sub_442C90 (COverlapJob_PunishmentJob::vf06): "%d-%02d-%02d %02d:%02d:%02d" of the time
// into the string (asserts when it does not fit 260 characters).
void SM_FormatDateTime(std::string* pstrOut, const TIMESTAMP_STRUCT* pTime);
