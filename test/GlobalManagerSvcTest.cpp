// GlobalManager service manager (CServiceManagerProcess): the SMC handlers. A real engine is
// connected to itself: the "SMC" is a client task, the GlobalManager side of the connection is
// the session the handlers answer to and the route of every server body, so relayed requests
// (0x6008) arrive at the client too. CDBQuery's execution is replaced (scripted results), the
// SM log and the overlap jobs of the other GlobalManager parts are recording fakes.

#include "LoopbackBind.h"

#include <winsock2.h>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>

#include "TestCheck.h"

#include "GlobalManager/ContentUser.h"
#include "GlobalManager/GlobalManagerDB.h"
#include "GlobalManager/GlobalManagerSvcFwd.h"
#include "GlobalManager/ServiceManagerProcess.h"
#include "BSLib/BSDebug.h"
#include "BSLib/DB/DBQuery.h"
#include "BSNet/NetEngine.h"
#include "BSNet/SockListener.h"
#include "BSNet/security/Security.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/CertData.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerProcess.h"

using namespace ServerFramework;

typedef std::vector<BYTE> Bytes;

static CRITICAL_SECTION s_cs;

// ---------------------------------------------------------------------------------------------
// Byte helpers

static void Put(Bytes& v, const void* p, size_t n)
{
    v.insert(v.end(), (const BYTE*)p, (const BYTE*)p + n);
}

static void PutB(Bytes& v, BYTE b)
{
    v.push_back(b);
}

static void PutW(Bytes& v, WORD w)
{
    Put(v, &w, 2);
}

static void PutD(Bytes& v, DWORD d)
{
    Put(v, &d, 4);
}

static void PutS(Bytes& v, const char* psz)
{
    PutW(v, (WORD)strlen(psz));
    Put(v, psz, strlen(psz));
}

// "01 02 0a" -> bytes
static Bytes B(const char* pszHex)
{
    Bytes v;
    for (const char* p = pszHex; *p;)
    {
        if (*p == ' ')
        {
            ++p;
            continue;
        }
        unsigned n = 0;
        sscanf_s(p, "%2x", &n);
        v.push_back((BYTE)n);
        p += 2;
    }
    return v;
}

static Bytes Cat(const Bytes& a, const Bytes& b)
{
    Bytes v = a;
    v.insert(v.end(), b.begin(), b.end());
    return v;
}

static std::string Hex(const Bytes& v)
{
    std::string s;
    char sz[4];
    for (size_t i = 0; i < v.size() && i < 80; ++i)
    {
        sprintf_s(sz, "%02x ", v[i]);
        s += sz;
    }
    return s;
}

static bool Same(const Bytes& vGot, const Bytes& vWant)
{
    if (vGot == vWant)
        return true;
    printf("  got:  %s(%u)\n  want: %s(%u)\n", Hex(vGot).c_str(), (unsigned)vGot.size(),
           Hex(vWant).c_str(), (unsigned)vWant.size());
    return false;
}

// ---------------------------------------------------------------------------------------------
// Tasks: both record every message; the server task's job is also the main process the
// service manager relays and posts its jobs through.

enum
{
    TEST_TASK_SERVER    = 0x01000000,
    TEST_TASK_CLIENT    = 0x01000100,
};

struct PACKET
{
    DWORD   dwTask;
    DWORD   dwSession;
    WORD    wOpcode;
    Bytes   data;
};

static std::deque<PACKET> s_queIn;

class CTestTask : public CServerProcessBase
{
    BS_DECLARE_DYNCREATE(CTestTask)

public:
    virtual void InitMsgHandlers()
    {
    }

    virtual void OnUnhandledMsg(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
    {
        PACKET packet;
        packet.dwTask = (DWORD)this;
        packet.dwSession = pMsg->m_dwSessionID;
        packet.wOpcode = pMsg->GetOpcode();
        packet.data.assign(pMsg->m_pData + pMsg->m_wReadPos, pMsg->m_pData + pMsg->m_wWritePos);
        pMsg->SkipRemaining();
        EnterCriticalSection(&s_cs);
        s_queIn.push_back(packet);
        LeaveCriticalSection(&s_cs);
    }

    // The service manager's jobs are recorded instead of being run.
    virtual bool SendOverlapJob(COverlapJob_Local* pJob, CTask* pTask)
    {
        m_vecJobs.push_back(pJob);
        return pTask == NULL && m_bAcceptJobs;
    }

    std::vector<COverlapJob_Local*> m_vecJobs;
    bool                            m_bAcceptJobs;
};

BS_IMPLEMENT_DYNCREATE(CTestTask, &CServerProcessBase::classCServerProcessBase)

static bool TakePacket(DWORD dwSession, WORD wOpcode, Bytes* pData, DWORD dwTimeout = 3000,
                       bool bReport = true)
{
    DWORD dwStart = GetTickCount();
    for (;;)
    {
        EnterCriticalSection(&s_cs);
        for (std::deque<PACKET>::iterator it = s_queIn.begin(); it != s_queIn.end(); ++it)
        {
            if (it->dwSession == dwSession && it->wOpcode == wOpcode)
            {
                if (pData)
                    *pData = it->data;
                s_queIn.erase(it);
                LeaveCriticalSection(&s_cs);
                return true;
            }
        }
        LeaveCriticalSection(&s_cs);
        if (GetTickCount() - dwStart > dwTimeout)
        {
            if (bReport)
                printf("  no packet 0x%04x on session %u\n", wOpcode, dwSession);
            return false;
        }
        Sleep(5);
    }
}

static bool NoPacket(DWORD dwSession, WORD wOpcode, DWORD dwWait = 200)
{
    return !TakePacket(dwSession, wOpcode, NULL, dwWait, false);
}

static bool NoPackets(DWORD dwWait = 200)
{
    Sleep(dwWait);
    EnterCriticalSection(&s_cs);
    bool b = s_queIn.empty();
    for (size_t i = 0; i < s_queIn.size(); ++i)
        printf("  unexpected packet 0x%04x\n", s_queIn[i].wOpcode);
    s_queIn.clear();
    LeaveCriticalSection(&s_cs);
    return b;
}

// The data of the next massive message with the inner opcode on the session.
static bool TakeMassive(DWORD dwSession, WORD wOpcode, Bytes* pData)
{
    DWORD dwStart = GetTickCount();
    for (;;)
    {
        EnterCriticalSection(&s_cs);
        size_t nHeader = s_queIn.size();
        size_t nParts = 0;
        std::vector<size_t> vecParts;
        for (size_t i = 0; i < s_queIn.size(); ++i)
        {
            const PACKET& packet = s_queIn[i];
            if (packet.dwSession != dwSession || packet.wOpcode != MASSIVE_MSG_OPCODE
                || packet.data.empty())
            {
                continue;
            }
            if (nHeader == s_queIn.size())
            {
                if (packet.data[0] == 1 && packet.data.size() == 5
                    && *(const WORD*)&packet.data[3] == wOpcode)
                {
                    nHeader = i;
                    nParts = *(const WORD*)&packet.data[1];
                }
            }
            else if (packet.data[0] == 0 && vecParts.size() < nParts)
            {
                vecParts.push_back(i);
            }
        }
        if (nHeader != s_queIn.size() && vecParts.size() == nParts)
        {
            pData->clear();
            for (size_t i = 0; i < vecParts.size(); ++i)
            {
                const Bytes& part = s_queIn[vecParts[i]].data;
                pData->insert(pData->end(), part.begin() + 1, part.end());
            }
            for (size_t i = vecParts.size(); i > 0; --i)
                s_queIn.erase(s_queIn.begin() + vecParts[i - 1]);
            s_queIn.erase(s_queIn.begin() + nHeader);
            LeaveCriticalSection(&s_cs);
            return true;
        }
        LeaveCriticalSection(&s_cs);
        if (GetTickCount() - dwStart > 3000)
        {
            printf("  no massive 0x%04x on session %u\n", wOpcode, dwSession);
            return false;
        }
        Sleep(5);
    }
}

static CNetEngine s_Engine;
static CTestTask* s_pMain;      // the server task's job
static DWORD s_dwClient;        // the SMC's session
static DWORD s_dwServer;        // GlobalManager's session of the same connection

static CMsg* Req(WORD wOpcode, const Bytes& data, DWORD dwSession = 0)
{
    CMsg* p = g_pBSNet->AllocMsg(FALSE);
    p->SetOpcode(wOpcode);
    if (!data.empty())
        p->WriteBytes(&data[0], (WORD)data.size());
    p->m_dwSessionID = dwSession ? dwSession : s_dwServer;
    return p;
}

static void Done(CMsg* pMsg)
{
    g_pBSNet->ReleaseMsg(pMsg);
}

// The next reply to the SMC.
static bool Reply(WORD wOpcode, const Bytes& want)
{
    Bytes got;
    if (!TakePacket(s_dwClient, wOpcode, &got))
        return false;
    return Same(got, want);
}

// The next 0x6008 relay: its job ID, target, inner opcode and data.
static bool Relay(WORD wTarget, WORD wOpcode, const Bytes& want, DWORD* pdwRelayID = NULL)
{
    Bytes got;
    if (!TakePacket(s_dwClient, SERVER_MSG_RELAY_REQ, &got))
        return false;
    if (got.size() < 8)
        return false;
    if (pdwRelayID)
        *pdwRelayID = *(const DWORD*)&got[0];
    bool b = *(const WORD*)&got[4] == wTarget && *(const WORD*)&got[6] == wOpcode;
    if (!b)
        printf("  relay to %u of 0x%X\n", *(const WORD*)&got[4], *(const WORD*)&got[6]);
    return Same(Bytes(got.begin() + 8, got.end()), want) && b;
}

// The context of the relay job (the job stays registered with the main process).
static DWORD RelayContext(DWORD dwRelayID)
{
    OverlapJobMap& map = s_pMain->m_OverlapJobManager.m_mapJob;
    OverlapJobMap::iterator it = map.find(dwRelayID);
    if (it == map.end())
        return 0xFFFFFFFF;
    return ((COverlapJob_RelayMsg*)it->second)->m_dwContext;
}

// ---------------------------------------------------------------------------------------------
// Fakes: SM log and the overlap jobs of the other GlobalManager parts.

static std::vector<std::string> s_vecLog;

static void AddLog(const char* pszFormat, ...)
{
    char sz[1024];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf_s(sz, sizeof(sz), _TRUNCATE, pszFormat, args);
    va_end(args);
    s_vecLog.push_back(sz);
}

static bool TakeLog(const char* pszWant)
{
    for (size_t i = 0; i < s_vecLog.size(); ++i)
    {
        if (s_vecLog[i] == pszWant)
        {
            s_vecLog.erase(s_vecLog.begin() + i);
            return true;
        }
    }
    printf("  missing SM log: %s\n", pszWant);
    for (size_t i = 0; i < s_vecLog.size(); ++i)
        printf("    have: %s\n", s_vecLog[i].c_str());
    return false;
}

void CServiceManagerProcess::WriteLog(DWORD dwSessionID, BYTE byType, const char* pszFormat,
                                      ...)
{
    char sz[1024];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf_s(sz, sizeof(sz), _TRUNCATE, pszFormat, args);
    va_end(args);
    AddLog("log %u %u %s", dwSessionID == s_dwServer ? 1 : dwSessionID, byType, sz);
}

void CServiceManagerProcess::WriteQueryLog(DWORD dwSessionID, const char* pszFormat, ...)
{
    char sz[1024];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf_s(sz, sizeof(sz), _TRUNCATE, pszFormat, args);
    va_end(args);
    AddLog("query %u %s", dwSessionID == s_dwServer ? 1 : dwSessionID, sz);
}

void CServiceManagerProcess::WriteAnswerLog(std::string* pstrText, DWORD dwSessionID,
                                            BYTE byType)
{
    AddLog("answer %u %u %s", dwSessionID == s_dwServer ? 1 : dwSessionID, byType,
           pstrText->c_str());
}

void CServiceManagerProcess::WriteUserLog(std::string* pstrText, std::string* pstrUserID)
{
    AddLog("user %s %s", pstrText->c_str(), pstrUserID->c_str());
}

BYTE SMLog_GetAnswerType(WORD wOpcode)
{
    return wOpcode == 0x8001 ? 0x14 : 0xFF;
}

class CFakeJob : public COverlapJob_Local
{
public:
    explicit CFakeJob(const char* pszName) : m_strDesc(pszName) {}
    virtual void ReturnToPool() {}
    virtual bool Prepare() { return true; }
    virtual bool Execute() { return true; }
    virtual bool OnComplete() { return true; }
    virtual bool OnFail() { return true; }

    std::string m_strDesc;
};

static std::vector<CFakeJob*> s_vecFakeJobs;
static CTask* s_pExpectTask;

static SM_JOB* NewJob(const char* pszName)
{
    CFakeJob* pJob = new CFakeJob(pszName);
    s_vecFakeJobs.push_back(pJob);
    return pJob;
}

static void Describe(SM_JOB* pJob, CTask* pTask, const char* pszFormat, ...)
{
    char sz[1024];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf_s(sz, sizeof(sz), _TRUNCATE, pszFormat, args);
    va_end(args);
    CFakeJob* p = (CFakeJob*)pJob;
    p->m_strDesc += sz;
    if (pTask != s_pExpectTask)
        p->m_strDesc += " (other task)";
}

// The last job the service manager posted and its description.
static bool PostedJob(const char* pszWant)
{
    if (s_pMain->m_vecJobs.empty())
    {
        printf("  no job posted (%s)\n", pszWant);
        return false;
    }
    CFakeJob* pJob = (CFakeJob*)s_pMain->m_vecJobs.back();
    s_pMain->m_vecJobs.pop_back();
    if (pJob->m_strDesc == pszWant)
        return true;
    printf("  job: %s\n  want: %s\n", pJob->m_strDesc.c_str(), pszWant);
    return false;
}

static DWORD S(DWORD dwSessionID)
{
    return dwSessionID == s_dwServer ? 1 : dwSessionID;
}

SM_JOB* OverlapJob_AllocSM_ModuleVersion() { return NewJob("ModuleVersion"); }
void OverlapJob_SetupSM_ModuleVersion(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID)
{
    Describe(pJob, pTask, " %u", S(dwSessionID));
}
SM_JOB* OverlapJob_AllocSM_ModuleVersionFile() { return NewJob("ModuleVersionFile"); }
void OverlapJob_SetupSM_ModuleVersionFile(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID,
                                          BYTE byModuleID, BYTE byParam)
{
    Describe(pJob, pTask, " %u %u %u", S(dwSessionID), byModuleID, byParam);
}
SM_JOB* OverlapJob_AllocRecordPatchData() { return NewJob("RecordPatchData"); }
void OverlapJob_SetupRecordPatchData(CTask* pTask, SM_JOB* pJob, const MODULE_VERSION* pModule,
                                     ModuleVersionFileList* pFiles, DWORD dwSessionID)
{
    Describe(pJob, pTask, " %u %u %u", pModule->byModuleID, (unsigned)pFiles->size(),
             S(dwSessionID));
}
SM_JOB* OverlapJob_AllocModuleVersionData() { return NewJob("ModuleVersionData"); }
void OverlapJob_SetupModuleVersionData(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID)
{
    Describe(pJob, pTask, " %u", S(dwSessionID));
}
SM_JOB* OverlapJob_AllocRefreshSecurityData() { return NewJob("RefreshSecurityData"); }
void OverlapJob_SetupRefreshSecurityData(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID)
{
    Describe(pJob, pTask, " %u", S(dwSessionID));
}
SM_JOB* OverlapJob_AllocQueryUser() { return NewJob("QueryUser"); }
void OverlapJob_SetupQueryUser(CTask* pTask, SM_JOB* pJob, const char* pszUserID,
                               DWORD dwSessionID)
{
    Describe(pJob, pTask, " %s %u", pszUserID, S(dwSessionID));
}
SM_JOB* OverlapJob_AllocQuerySMLog() { return NewJob("QuerySMLog"); }
void OverlapJob_SetupQuerySMLog(CTask* pTask, SM_JOB* pJob, const char* pszBegin,
                                const char* pszEnd, DWORD dwSessionID,
                                std::string* pstrUserID, BYTE byType)
{
    Describe(pJob, pTask, " %s %s %u [%s] %u", pszBegin, pszEnd, S(dwSessionID),
             pstrUserID->c_str(), byType);
}
SM_JOB* OverlapJob_AllocQueryContentUserAccount() { return NewJob("QueryContentUserAccount"); }
void OverlapJob_SetupQueryContentUserAccount(CTask* pTask, SM_JOB* pJob, const char* pszQuery,
                                             DWORD dwSessionID, BYTE byType)
{
    Describe(pJob, pTask, " [%s] %u %u", pszQuery, S(dwSessionID), byType);
}
SM_JOB* OverlapJob_AllocQueryPunishment() { return NewJob("QueryPunishment"); }
void OverlapJob_SetupQueryPunishment(CTask* pTask, SM_JOB* pJob, const char* pszUserID,
                                     DWORD dwSessionID)
{
    Describe(pJob, pTask, " %s %u", pszUserID, S(dwSessionID));
}
SM_JOB* OverlapJob_AllocPunishmentJob() { return NewJob("Punishment"); }
void OverlapJob_SetupPunishmentJob(CTask* pTask, SM_JOB* pJob, const char* psz1,
                                   const char* psz2, DWORD dwSessionID, BYTE byJob,
                                   DWORD dwSerial, DWORD dwJID, BYTE byType, WORD wShardID,
                                   const char* psz3, const char* psz4, const char* psz5,
                                   const char* psz6, SYSTEMTIME t1, SYSTEMTIME t2,
                                   SYSTEMTIME t3, SYSTEMTIME t4)
{
    Describe(pJob, pTask, " %s %s %u %u %u %u %u %u %s %s %s %s %u/%u %u/%u %u/%u %u/%u", psz1,
             psz2, S(dwSessionID), byJob, dwSerial, dwJID, byType, wShardID, psz3, psz4, psz5,
             psz6, t1.wYear, t1.wSecond, t2.wYear, t2.wSecond, t3.wYear, t3.wSecond, t4.wYear,
             t4.wSecond);
}
SM_JOB* OverlapJob_AllocQueryCharOwnedUser() { return NewJob("QueryCharOwnedUser"); }
void OverlapJob_SetupQueryCharOwnedUser(CTask* pTask, SM_JOB* pJob, const char* pszUserID,
                                        DWORD dwSessionID)
{
    Describe(pJob, pTask, " %s %u", pszUserID, S(dwSessionID));
}
SM_JOB* OverlapJob_AllocQueryUserByUserID() { return NewJob("QueryUserByUserID"); }
void OverlapJob_SetupQueryUserByUserID(CTask* pTask, SM_JOB* pJob, const char* pszUserID,
                                       DWORD dwSessionID)
{
    Describe(pJob, pTask, " %s %u", pszUserID, S(dwSessionID));
}
SM_JOB* OverlapJob_AllocPreparePatch() { return NewJob("PreparePatch"); }
void OverlapJob_SetupPreparePatch(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID,
                                  BYTE byModuleID, BYTE byParam)
{
    Describe(pJob, pTask, " %u %u %u", S(dwSessionID), byModuleID, byParam);
}
SM_JOB* OverlapJob_AllocArticleJob() { return NewJob("Article"); }
void OverlapJob_SetupArticleJob(CTask* pTask, SM_JOB* pJob, BYTE byType, const char* pszTitle,
                                DWORD dwSessionID, BYTE byParam, DWORD dwArticleID,
                                const char* pszText)
{
    Describe(pJob, pTask, " %u [%s] %u %u %u [%s]", byType, pszTitle ? pszTitle : "(null)",
             S(dwSessionID), byParam, dwArticleID, pszText ? pszText : "(null)");
}
SM_JOB* OverlapJob_AllocConcurrentUserLogJob() { return NewJob("ConcurrentUserLog"); }
void OverlapJob_SetupConcurrentUserLogJob(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID,
                                          WORD wShardID, BYTE byType, std::string* pstrBegin,
                                          std::string* pstrEnd)
{
    Describe(pJob, pTask, " %u %u %u %s %s", S(dwSessionID), wShardID, byType,
             pstrBegin->c_str(), pstrEnd->c_str());
}
SM_JOB* OverlapJob_AllocAutomatedPunishmentJob() { return NewJob("AutomatedPunishment"); }
void OverlapJob_SetupAutomatedPunishmentJob(CTask* pTask, SM_JOB* pJob,
                                            std::string* pstrUserID, DWORD dwJID,
                                            DWORD dwShardID, DWORD dwType, DWORD dwParam)
{
    Describe(pJob, pTask, " %s %u %u %u %u", pstrUserID->c_str(), dwJID, dwShardID, dwType,
             dwParam);
}

// ---------------------------------------------------------------------------------------------
// Query double: CDBQuery::ExecuteFormat formats like the original, then answers from the script
// (rows go through the trigger when there is one).

struct QUERY_ANSWER
{
    int                 nResult;
    std::vector<Bytes>  rows;
};

struct QUERY_CALL
{
    std::string     strSQL;
    CDBSessionMgr*  pSession;
    int             nSession;
    int             nRowSize;
};

static std::deque<QUERY_ANSWER> s_deqAnswer;
static std::vector<QUERY_CALL> s_vecCall;

static void Answer(int nResult, const Bytes& row = Bytes())
{
    QUERY_ANSWER a;
    a.nResult = nResult;
    if (!row.empty())
        a.rows.push_back(row);
    s_deqAnswer.push_back(a);
}

static void AnswerRows(int nResult, const std::vector<Bytes>& rows)
{
    QUERY_ANSWER a;
    a.nResult = nResult;
    a.rows = rows;
    s_deqAnswer.push_back(a);
}

int CDBQuery::Execute()
{
    QUERY_CALL c;
    c.strSQL = m_szQuery;
    c.pSession = m_pSession;
    c.nSession = m_nSession;
    c.nRowSize = m_nRowSize;
    s_vecCall.push_back(c);
    if (s_deqAnswer.empty())
    {
        printf("  unexpected query: %s\n", m_szQuery);
        ++g_nFailed;
        return -2;
    }
    QUERY_ANSWER a = s_deqAnswer.front();
    s_deqAnswer.pop_front();
    for (size_t i = 0; i < a.rows.size(); ++i)
    {
        if (m_pTrigger)
        {
            m_bRowFetched = 0;
            m_pTrigger->OnTrigger2((DWORD)this, 0);
        }
        CHECK((int)a.rows[i].size() <= m_nRowSize);
        memcpy(m_pBuffer, &a.rows[i][0], a.rows[i].size());
        if (m_pTrigger)
        {
            m_bRowFetched = 1;
            m_pTrigger->OnTrigger2((DWORD)this, 0);
        }
    }
    return a.nResult;
}

int CDBQuery::ExecuteFormat(const char* pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf_s(m_szQuery, sizeof(m_szQuery), sizeof(m_szQuery) - 1, pszFormat, args);
    va_end(args);
    return Execute();
}

static CDBSessionMgr* const DEFAULT_DB = (CDBSessionMgr*)0x1111;
static CDBSessionMgr* const ACCOUNT_DB = (CDBSessionMgr*)0x2222;

static bool Query(const char* pszSQL, CDBSessionMgr* pSession, int nRowSize)
{
    if (s_vecCall.empty())
    {
        printf("  no query (%s)\n", pszSQL);
        return false;
    }
    QUERY_CALL c = s_vecCall.front();
    s_vecCall.erase(s_vecCall.begin());
    bool b = c.strSQL == pszSQL && c.pSession == pSession && c.nSession == 1
          && c.nRowSize == nRowSize;
    if (!b)
        printf("  query: %s (%p %d %d)\n  want:  %s\n", c.strSQL.c_str(), c.pSession,
               c.nSession, c.nRowSize, pszSQL);
    return b;
}

// ---------------------------------------------------------------------------------------------
// Architecture: body 1 is this GlobalManager (parent 2), 2 a GatewayServer, 3 a DownloadServer,
// 4 a GatewayServer that is not reachable, 5 the MachineManager of machine 1, 6 a body on
// machine 2 whose MachineManager (7) is not reachable.

static CERT_MODULE s_aModule[4];
static CERT_MACHINE s_aMachine[2];
static SERVER_BODY s_aBody[7];
static SERVER_CORD s_CordUp;
static SERVER_CORD s_CordDown;
static CERT_SHARD s_Shard;
static ServerBodyList s_listBody;
static CertShardList s_listShard;

static void AddModule(int i, BYTE byID, const char* pszName, const char* pszLower)
{
    s_aModule[i].byID = byID;
    strcpy_s(s_aModule[i].szName, pszName);
    g_mapCertModuleByName[pszLower] = &s_aModule[i];
}

static void AddBody(int i, WORD wID, int nModule, int nMachine, WORD wParentID, int nState,
                    SERVER_CORD* pRoute)
{
    SERVER_BODY& b = s_aBody[i];
    b.wID = wID;
    b.byModuleID = s_aModule[nModule].byID;
    b.pModule = &s_aModule[nModule];
    b.pMachine = &s_aMachine[nMachine];
    b.wParentID = wParentID;
    b.nState = nState;
    s_listBody.push_back(&b);
    g_mapServerBody[wID] = &b;
    if (pRoute)
        g_hmapServerRoute[wID] = pRoute;
}

static void SetupArchitecture()
{
    AddModule(0, 1, "GatewayServer", "gatewayserver");
    AddModule(1, 2, "DownloadServer", "downloadserver");
    AddModule(2, 3, "MachineManager", "machinemanager");
    AddModule(3, 4, "GlobalManager", "globalmanager");
    s_aMachine[0].dwID = 1;
    s_aMachine[0].wMachineManagerID = 5;
    s_aMachine[1].dwID = 2;
    s_aMachine[1].wMachineManagerID = 7;
    s_CordUp.dwSessionID = s_dwServer;
    s_CordDown.dwSessionID = 0;
    AddBody(0, 1, 3, 0, 2, 5, NULL);
    AddBody(1, 2, 0, 0, 1, 5, &s_CordUp);
    AddBody(2, 3, 1, 0, 1, 5, &s_CordUp);
    AddBody(3, 4, 0, 0, 1, 4, &s_CordDown);
    AddBody(4, 5, 2, 0, 0, 5, &s_CordUp);
    AddBody(5, 6, 1, 1, 9, 3, &s_CordUp);
    g_hmapServerRoute[7] = &s_CordDown;
    g_pServerBodyList = &s_listBody;
    g_pSelfBody = &s_aBody[0];
    s_Shard.wID = 10;
    strcpy_s((char*)s_Shard.ab3 + 1, 32, "Alpha");
    s_Shard.w548 = 1000;
    g_mapCertShard[10] = &s_Shard;
    s_listShard.push_back(&s_Shard);
    g_pCertShardList = &s_listShard;
}

static CServiceManagerProcess* s_pSM;

// A logged-in SMC on the test connection.
static sServiceManagerUser* AddUser(DWORD dwSessionID, DWORD dwID, const char* pszID)
{
    sServiceManagerUser* pUser = ServiceManagerUserPool_Alloc();
    pUser->dwID = dwID;
    pUser->strID = pszID;
    pUser->dwSessionID = dwSessionID;
    s_pSM->m_mapUser[dwSessionID] = pUser;
    return pUser;
}

static sSMDefault_RelayMsg_Param* Param(WORD wBodyID, DWORD dw8 = 0)
{
    sSMDefault_RelayMsg_Param* p = SMRelayParamPool_Alloc();
    p->dwSessionID = s_dwServer;
    p->wBodyID = wBodyID;
    p->dw8 = dw8;
    return p;
}

// ---------------------------------------------------------------------------------------------
// Tests

static void TestPools()
{
    sServiceManagerUser* pUser = ServiceManagerUserPool_Alloc();
    CHECK(pUser->bInUse == 1 && g_ServiceManagerUserPool.m_nInUse == 1);
    pUser->Release();
    CHECK(pUser->bInUse == 0 && g_ServiceManagerUserPool.m_nInUse == 0);
    pUser->Release();       // only once
    CHECK(g_ServiceManagerUserPool.m_nInUse == 0);
    sSMDefault_RelayMsg_Param* pParam = SMRelayParamPool_Alloc();
    CHECK(pParam->bInUse == 1 && g_SMRelayParamPool.m_nInUse == 1);
    pParam->Release();
    pParam->Release();
    CHECK(g_SMRelayParamPool.m_nInUse == 0);

    std::string str;
    TIMESTAMP_STRUCT ts = { 2011, 1, 24, 9, 5, 7, 0 };
    SM_FormatDateTime(&str, &ts);
    CHECK(str == "2011-01-24 09:05:07");

    CHECK(s_pSM->m_aHandler[0x7005] == &CServiceManagerProcess::OnMsg_7005);
    CHECK(s_pSM->m_aHandler[0xA300] == &CServiceManagerProcess::OnMsg_A300);
    CHECK(s_pSM->m_aHandler[0x7006] != NULL && s_pSM->m_aHandler[0x7100 - 1] == NULL);
    CHECK(s_pSM->m_nPatchStep == -1 && s_pSM->m_pTask == g_pBSNet->GetProactor(TEST_TASK_SERVER));
}

static void TestDispatch()
{
    // A registered opcode: 0x7005 relays 0x6300 to the body.
    Bytes v;
    PutW(v, 2);
    CMsg* p = Req(0x7005, v);
    CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
    Done(p);
    DWORD dwRelayID = 0;
    CHECK(Relay(2, 0x6300, Bytes(), &dwRelayID));
    sSMDefault_RelayMsg_Param* pParam = (sSMDefault_RelayMsg_Param*)RelayContext(dwRelayID);
    CHECK((DWORD)pParam != 0xFFFFFFFF && pParam->dwSessionID == s_dwServer);
    CHECK(pParam->wBodyID == 2 && GetTickCount() - pParam->dw8 < 3000);
    pParam->dw8 -= 50;

    // The answer 0xA300 {1}: 0xB005 {1, body, elapsed}.
    v.clear();
    PutB(v, 1);
    p = Req(0xA300, v, 77);
    CHECK(s_pSM->Dispatch(p, NULL, dwRelayID, (DWORD)pParam));
    Done(p);
    Bytes got;
    CHECK(TakePacket(s_dwClient, 0xB005, &got) && got.size() == 7);
    if (got.size() == 7)
    {
        DWORD dwElapsed = *(DWORD*)&got[3];
        CHECK(got[0] == 1 && *(WORD*)&got[1] == 2 && dwElapsed >= 50 && dwElapsed < 3000);
    }
    CHECK(g_SMRelayParamPool.m_nInUse == 0);


    // Without handler: an answer of a 0x7203 relay goes to the SMC as 0xB203 {1, opcode, data
    // after the leading string}; logged when the result is not 2 and the string not empty.
    pParam = Param(2, 0x8001);
    v.clear();
    PutS(v, "acc");
    PutB(v, 1);
    PutD(v, 0x11223344);
    p = Req(0x8001, v, 77);
    CHECK(s_pSM->Dispatch(p, NULL, 9, (DWORD)pParam));
    CHECK(p->GetUnreadSize() == 0);
    Done(p);
    CHECK(Reply(0xB203, B("01 01 80 01 44 33 22 11")));
    CHECK(TakeLog("answer 1 20 acc"));
    CHECK(g_SMRelayParamPool.m_nInUse == 0);

    // 0x8004 has no string; result 2 is not logged.
    pParam = Param(2);
    p = Req(0x8004, B("02 05"), 77);
    CHECK(s_pSM->Dispatch(p, NULL, 9, (DWORD)pParam));
    Done(p);
    CHECK(Reply(0xB203, B("01 04 80 02 05")));
    CHECK(s_vecLog.empty());

    // Requests are left to the main process.
    pParam = Param(2);
    p = Req(0x2005, Bytes(), 77);
    CHECK(!s_pSM->Dispatch(p, NULL, 9, (DWORD)pParam));
    Done(p);
    p = Req(0x3105, Bytes(), 77);
    CHECK(!s_pSM->Dispatch(p, NULL, 9, (DWORD)pParam));
    Done(p);
    pParam->Release();

    // A massive answer: 0xB203 (massive) {1, opcode, data after the string}, logged unless the
    // byte after the string is 2.
    pParam = Param(2);
    CMassiveMsg* pMassive = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pMassive->SetOpcode(0x8001);
    pMassive->WriteString("xyz");
    BYTE abData[] = { 1, 9, 8 };
    pMassive->Write(abData, 3);
    pMassive->m_nMode = MASSIVE_MODE_READ;
    pMassive->BeginRead();
    CHECK(s_pSM->Dispatch(NULL, pMassive, 9, (DWORD)pParam));
    pMassive->Free();
    CHECK(TakeMassive(s_dwClient, 0xB203, &got) && Same(got, B("01 01 80 01 09 08")));
    CHECK(TakeLog("answer 1 20 xyz"));
    CHECK(g_SMRelayParamPool.m_nInUse == 0);
    CHECK(NoPackets());
}

// sub_444050: the failure answers of the relayed requests.
static void TestRelayFailed()
{
    struct CASE
    {
        WORD        wOpcode;
        DWORD       dw8;
        WORD        wReply;
        const char* pszWant;
    };
    static const CASE s_aCase[] =
    {
        { 0x620D, 0, 0xB201, "02 02" },
        { 0x6300, 0, 0xB005, "02 03 00" },
        { 0x6301, 0, 0xB006, "02 03 03 00" },
        { 0x6302, 0, 0xB007, "02 03 03 00" },
        { 0x631D, 0, 0xB308, "02 03 03 00" },
        { 0x6303, 4, 0xB008, "02 01 04 03 00" },
        { 0x6304, 0, 0xB009, "02 02 03 00" },
        { 0x6307, 0, 0xB202, "03 00 02" },
        { 0x4400, 0x4400, 0xB203, "02 03 00 00 44" },
    };
    for (size_t i = 0; i < sizeof(s_aCase) / sizeof(s_aCase[0]); ++i)
    {
        sSMDefault_RelayMsg_Param* pParam = Param(3, s_aCase[i].dw8);
        CServiceManagerProcess::OnRelayFailed(s_aCase[i].wOpcode, pParam);
        CHECK(Reply(s_aCase[i].wReply, B(s_aCase[i].pszWant)));
        CHECK(g_SMRelayParamPool.m_nInUse == 0);
    }
    // 0x6310: the context is the session.
    CServiceManagerProcess::OnRelayFailed(0x6310, (sSMDefault_RelayMsg_Param*)s_dwServer);
    CHECK(Reply(0xB010, B("02")));
    // No answer (and the context is kept) for 0x6308, 0x6312 and other requests.
    sSMDefault_RelayMsg_Param* pParam = Param(3);
    CServiceManagerProcess::OnRelayFailed(0x6308, pParam);
    CServiceManagerProcess::OnRelayFailed(0x6312, pParam);
    CServiceManagerProcess::OnRelayFailed(0x2400, pParam);
    CHECK(pParam->bInUse == 1);
    pParam->Release();
    CHECK(NoPackets());
}

static void TestSessionClosed()
{
    sServiceManagerUser* pUser = AddUser(55, 7, "smc");
    g_setNotifySession.insert(55);
    s_pSM->OnSessionClosed(54);
    CHECK(s_pSM->m_mapUser.size() == 1 && s_vecLog.empty());
    CHECK(s_pSM->FindUser(55) == pUser && s_pSM->FindUser(54) == NULL);

    s_pSM->m_bPreparingPatch = 1;
    s_pSM->m_dwPrepareSession = 55;
    s_pSM->m_setPrepareBody.insert(3);
    s_pSM->m_bPatchUploading = 1;
    s_pSM->m_pPatchUser = pUser;
    s_pSM->m_nPatchStep = 0;
    s_pSM->m_wPatchFileCount = 2;
    s_pSM->OnSessionClosed(55);
    CHECK(s_pSM->m_mapUser.empty() && g_setNotifySession.empty());
    CHECK(TakeLog("log 55 9 Logout : smc"));
    CHECK(!s_pSM->m_bPreparingPatch && s_pSM->m_setPrepareBody.empty());
    CHECK(!s_pSM->m_bPatchUploading && s_pSM->m_pPatchUser == NULL);
    CHECK(s_pSM->m_nPatchStep == -1 && s_pSM->m_wPatchFileCount == 0);
    CHECK(g_ServiceManagerUserPool.m_nInUse == 0);

    // Broadcast: to every SMC; the message is released.
    AddUser(s_dwServer, 1, "a");
    CMsg* p = Req(0x1234, B("aa"));
    s_pSM->BroadcastToUsers(p);
    CHECK(Reply(0x1234, B("aa")));
    s_pSM->OnSessionClosed(s_dwServer);
    s_vecLog.clear();
    CHECK(NoPackets());
}

// 0x7006 / 0x7007 / 0x7308 and their answers.
static void TestBodyCommands()
{
    struct CASE
    {
        WORD        wRequest;
        WORD        wRelay;
        WORD        wReply;
        WORD        wAnswer;
        const char* pszLog;
        bool        bToMachine;
    };
    static const CASE s_aCase[] =
    {
        { 0x7006, 0x6301, 0xB006, 0xA301, "Launch Server REQ : %d", true },
        { 0x7007, 0x6302, 0xB007, 0xA302, "Shutdown Server REQ : %d", true },
        { 0x7308, 0x631D, 0xB308, 0xA31D, "Shutdown Server REQ : %d", false },
    };
    char szLog[128];
    char szWant[128];
    for (size_t i = 0; i < sizeof(s_aCase) / sizeof(s_aCase[0]); ++i)
    {
        const CASE& c = s_aCase[i];
        sprintf_s(szLog, "log 1 13 %s", c.pszLog);
        // unknown body
        CMsg* p = Req(c.wRequest, B("63 00"));
        CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
        Done(p);
        CHECK(Reply(c.wReply, B("02 04 63 00")));
        sprintf_s(szWant, szLog, 0x63);
        CHECK(TakeLog(szWant));
        // relayed: to the MachineManager of the body's machine (or to the body itself)
        p = Req(c.wRequest, B("03 00"));
        CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
        Done(p);
        DWORD dwRelayID = 0;
        if (c.bToMachine)
            CHECK(Relay(5, c.wRelay, B("02"), &dwRelayID));
        else
            CHECK(Relay(3, c.wRelay, Bytes(), &dwRelayID));
        sprintf_s(szWant, szLog, 3);
        CHECK(TakeLog(szWant));
        sSMDefault_RelayMsg_Param* pParam = (sSMDefault_RelayMsg_Param*)RelayContext(dwRelayID);
        CHECK(pParam->wBodyID == 3 && pParam->dw8 == 0 && pParam->dwSessionID == s_dwServer);
        // answer {2, error}
        p = Req(c.wAnswer, B("02 07"), 77);
        CHECK(s_pSM->Dispatch(p, NULL, dwRelayID, (DWORD)pParam));
        Done(p);
        CHECK(Reply(c.wReply, B("02 07 03 00")));
        // answer {1}
        pParam = Param(3);
        p = Req(c.wAnswer, B("01"), 77);
        CHECK(s_pSM->Dispatch(p, NULL, dwRelayID, (DWORD)pParam));
        Done(p);
        CHECK(Reply(c.wReply, B("01 03 00")));
        CHECK(g_SMRelayParamPool.m_nInUse == 0);
        // no route: body 6 is on machine 2 (MachineManager 7), body 4 has no session
        p = Req(c.wRequest, c.bToMachine ? B("06 00") : B("04 00"));
        CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
        Done(p);
        CHECK(Reply(c.wReply, c.bToMachine ? B("02 03 06 00") : B("02 03 04 00")));
        s_vecLog.clear();
        CHECK(g_SMRelayParamPool.m_nInUse == 0);
    }
    CHECK(NoPackets());
}

// 0x7008 / 0xA303, 0x7009 / 0xA304.
static void TestServiceCommands()
{
    CMsg* p = Req(0x7008, B("05 63 00"));
    s_pSM->OnMsg_7008(p, 0, 0, NULL);
    Done(p);
    CHECK(Reply(0xB008, B("02 02 05 63 00")));
    CHECK(TakeLog("log 1 13 Operate Service REQ : operation[5], server_id[99]"));
    p = Req(0x7008, B("05 02 00"));
    s_pSM->OnMsg_7008(p, 0, 0, NULL);
    Done(p);
    DWORD dwRelayID = 0;
    CHECK(Relay(2, 0x6303, B("05"), &dwRelayID));
    s_vecLog.clear();
    sSMDefault_RelayMsg_Param* pParam = (sSMDefault_RelayMsg_Param*)RelayContext(dwRelayID);
    CHECK(pParam->dw8 == 5 && pParam->wBodyID == 2);
    p = Req(0xA303, B("01 00 04"), 77);
    CHECK(s_pSM->Dispatch(p, NULL, dwRelayID, (DWORD)pParam));
    Done(p);
    CHECK(Reply(0xB008, B("01 00 05 02 00")));
    p = Req(0x7008, B("05 04 00"));
    s_pSM->OnMsg_7008(p, 0, 0, NULL);
    Done(p);
    CHECK(Reply(0xB008, B("02 01 05 04 00")));
    s_vecLog.clear();

    // 0x7009: only bodies in state 5
    p = Req(0x7009, B("04 00"));
    s_pSM->OnMsg_7009(p, 0, 0, NULL);
    Done(p);
    CHECK(Reply(0xB009, B("02 01 04 00")));
    p = Req(0x7009, B("02 00"));
    s_pSM->OnMsg_7009(p, 0, 0, NULL);
    Done(p);
    CHECK(Relay(2, 0x6304, Bytes(), &dwRelayID));
    pParam = (sSMDefault_RelayMsg_Param*)RelayContext(dwRelayID);
    p = Req(0xA304, B("01 aa bb"), 77);
    CHECK(s_pSM->Dispatch(p, NULL, dwRelayID, (DWORD)pParam));
    CHECK(p->GetUnreadSize() == 0);
    Done(p);
    CHECK(Reply(0xB009, B("01 02 00 aa bb")));
    pParam = Param(2);
    p = Req(0xA304, B("02 aa"), 77);
    s_pSM->OnMsg_A304(p, 0, (DWORD)pParam, NULL);
    Done(p);
    CHECK(Reply(0xB009, B("02 03 02 00")));
    CHECK(g_SMRelayParamPool.m_nInUse == 0);
    CHECK(NoPackets());
}

// 0x7202 / 0xA307 and 0x7203.
static void TestGatewayRelays()
{
    CMsg* p = Req(0x7202, Bytes());
    s_pSM->OnMsg_7202(p, 0, 0, NULL);
    Done(p);
    DWORD dwRelayID = 0;
    CHECK(Relay(2, 0x6307, Bytes(), &dwRelayID));
    // gateway 4 has no session: 0xB202 {4, 2}; its context is not released (GM-001)
    CHECK(Reply(0xB202, B("04 00 02")));
    CHECK(g_SMRelayParamPool.m_nInUse == 2);
    sSMDefault_RelayMsg_Param* pParam = (sSMDefault_RelayMsg_Param*)RelayContext(dwRelayID);
    p = Req(0xA307, Bytes(), 77);
    CHECK(s_pSM->Dispatch(p, NULL, dwRelayID, (DWORD)pParam));
    Done(p);
    CHECK(Reply(0xB202, B("02 00 01")));
    CHECK(pParam->bInUse == 1);
    pParam->Release();
    g_SMRelayParamPool.m_nInUse = 0;

    // 0x7203 {body, opcode, data}: <opcode> {account, name, data} to the body
    sServiceManagerUser* pUser = AddUser(s_dwServer, 0x01020304, "op");
    p = Req(0x7203, B("02 00 01 64 ee ff"));
    s_pSM->OnMsg_7203(p, 0, 0, NULL);
    CHECK(p->GetUnreadSize() == 0);
    Done(p);
    CHECK(Relay(2, 0x6401, B("04 03 02 01 02 00 6f 70 ee ff"), &dwRelayID));
    pParam = (sSMDefault_RelayMsg_Param*)RelayContext(dwRelayID);
    CHECK(pParam->dw8 == 0x6401 && pParam->wBodyID == 2);
    pParam->Release();
    p = Req(0x7203, B("04 00 01 64 ee"));
    s_pSM->OnMsg_7203(p, 0, 0, NULL);
    CHECK(p->GetUnreadSize() == 0);
    Done(p);
    CHECK(Reply(0xB203, B("02 04 00 01 64")));
    CHECK(g_SMRelayParamPool.m_nInUse == 0);
    s_pSM->m_mapUser.clear();
    pUser->Release();
    CHECK(NoPackets());
}

// 0x7010 / 0xA310: shard name and user limit.
static void TestShardSettings()
{
    Bytes v;
    PutW(v, 10);
    PutB(v, 0);
    PutS(v, "0123456789abcdefg");      // 17 characters
    CMsg* p = Req(0x7010, v);
    s_pSM->OnMsg_7010(p, 0, 0, NULL);
    Done(p);
    CHECK(Reply(0xB010, B("02")));
    v.clear();
    PutW(v, 11);
    PutB(v, 0);
    PutS(v, "Beta");
    p = Req(0x7010, v);
    s_pSM->OnMsg_7010(p, 0, 0, NULL);
    Done(p);
    CHECK(Reply(0xB010, B("02")));
    v[0] = 10;
    p = Req(0x7010, v);
    s_pSM->OnMsg_7010(p, 0, 0, NULL);
    Done(p);
    DWORD dwRelayID = 0;
    CHECK(Relay(2, 0x6310, B("0a 00 00 04 00 42 65 74 61"), &dwRelayID));
    CHECK(RelayContext(dwRelayID) == s_dwServer);
    CHECK(TakeLog("log 1 13 Change Shard Name 10 : Alpha -> Beta"));
    p = Req(0x7010, B("0a 00 01 d0 07"));
    s_pSM->OnMsg_7010(p, 0, 0, NULL);
    Done(p);
    CHECK(Relay(2, 0x6310, B("0a 00 01 d0 07")));
    CHECK(TakeLog("log 1 13 Change Shard Max User 10[Alpha] : 1000 -> 2000"));
    p = Req(0x7010, B("0b 00 01 d0 07"));
    s_pSM->OnMsg_7010(p, 0, 0, NULL);
    Done(p);
    CHECK(Reply(0xB010, B("02")));

    // The answers: 0x2311 to the bodies certified here (2, 3; 4 has no session), 0xB010 to
    // every SMC.
    AddUser(s_dwServer, 1, "a");
    p = Req(0xA310, B("01 0a 00 01 d0 07"), 77);
    s_pSM->OnMsg_A310(p, 0, s_dwServer, NULL);
    Done(p);
    CHECK(s_Shard.w548 == 2000);
    CHECK(Reply(0x2311, B("0a 00 01 d0 07")) && Reply(0x2311, B("0a 00 01 d0 07")));
    CHECK(Reply(0xB010, B("01 0a 00 01 d0 07")));
    v.clear();
    PutB(v, 1);
    PutW(v, 10);
    PutB(v, 0);
    PutS(v, "Gamma");
    p = Req(0xA310, v, 77);
    s_pSM->OnMsg_A310(p, 0, s_dwServer, NULL);
    Done(p);
    CHECK(strcmp((char*)s_Shard.ab3 + 1, "Gamma") == 0);
    Bytes want = B("0a 00 00");
    PutS(want, "Gamma");
    CHECK(Reply(0x2311, want) && Reply(0x2311, want));
    CHECK(Reply(0xB010, Cat(B("01"), want)));
    p = Req(0xA310, B("01 0b 00 01 d0 07"), 77);
    s_pSM->OnMsg_A310(p, 0, s_dwServer, NULL);
    CHECK(p->GetUnreadSize() == 0);
    Done(p);
    CHECK(Reply(0xB010, B("02")));
    p = Req(0xA310, B("02"), 77);
    s_pSM->OnMsg_A310(p, 0, s_dwServer, NULL);
    Done(p);
    CHECK(Reply(0xB010, B("02")));
    s_pSM->m_mapUser.begin()->second->Release();
    s_pSM->m_mapUser.clear();
    CHECK(NoPackets());
}

// 0x7011 / 0x7012: _ShardService.
static void TestShardService()
{
    AddUser(s_dwServer, 1, "a");
    Answer(0);
    CMsg* p = Req(0x7011, B("0a 00 03"));
    s_pSM->OnMsg_7011(p, 0, 0, NULL);
    Done(p);
    CHECK(Query("update _ShardService set ServiceType = 3 where ShardID = 10", DEFAULT_DB, 0));
    CHECK(TakeLog("log 1 13 Change Shard Service Type : 10, 3"));
    CHECK(Relay(2, 0x6312, B("0a 00")));
    CHECK(Reply(0xB011, B("01 0a 00 03")));
    Answer(-2);
    p = Req(0x7011, B("0a 00 03"));
    s_pSM->OnMsg_7011(p, 0, 0, NULL);
    Done(p);
    CHECK(Query("update _ShardService set ServiceType = 3 where ShardID = 10", DEFAULT_DB, 0));
    CHECK(Reply(0xB011, B("02")));
    s_vecLog.clear();

    std::vector<Bytes> rows;
    rows.push_back(B("0a 00 01"));
    rows.push_back(B("0b 00 02"));
    AnswerRows(0, rows);
    p = Req(0x7012, Bytes());
    s_pSM->OnMsg_7012(p, 0, 0, NULL);
    Done(p);
    CHECK(Query("select * from _ShardService", DEFAULT_DB, 3));
    CHECK(Reply(0xB012, B("01 01 0a 00 01 01 0b 00 02 00")));
    AnswerRows(-1, rows);
    p = Req(0x7012, Bytes());
    s_pSM->OnMsg_7012(p, 0, 0, NULL);
    Done(p);
    CHECK(Query("select * from _ShardService", DEFAULT_DB, 3));
    CHECK(Reply(0xB012, B("02")));
    s_pSM->m_mapUser.begin()->second->Release();
    s_pSM->m_mapUser.clear();
    CHECK(NoPackets());
}

// ---------------------------------------------------------------------------------------------
// Accounts, architecture, content users, security descriptions, queries

static CERT_DIVISION s_Division;
static CERT_DATA_SETTING s_SettingLogin;
static CertDataGroupList s_listGroup;
static CertDataSettingList s_listSetting;
static CertDataGroupIPList s_listGroupIP;
static PrivIPSet s_setPrivIP;

static void SetupCertData()
{
    s_Division.byID = 3;
    s_aBody[0].byDivisionID = 7;
    s_aBody[0].pDivision = &s_Division;
    s_SettingLogin.dwValue = 0x77;
    strcpy_s(s_SettingLogin.szName, "SMC_Login");
    s_listSetting.push_back(&s_SettingLogin);
    g_pCertDataGroupList = &s_listGroup;
    g_pCertDataSettingList = &s_listSetting;
    g_pCertDataGroupIPList = &s_listGroupIP;
    s_setPrivIP.insert(0x77);
    g_hmapPrivIP[3] = &s_setPrivIP;
}

static CMsg* LoginRequest(DWORD dwVersion, bool bVersion = true)
{
    Bytes v;
    PutS(v, "smc");
    PutS(v, "pw");
    PutS(v, "key");
    if (bVersion)
        PutD(v, dwVersion);
    return Req(0x7001, v);
}

static void TestLogin()
{
    CMsg* p = LoginRequest(0, false);
    s_pSM->OnMsg_7001(p, 0, 0, NULL);
    Done(p);
    Bytes got;
    CHECK(TakeMassive(s_dwClient, 0xB001, &got) && Same(got, B("02")));
    p = LoginRequest(23);
    s_pSM->OnMsg_7001(p, 0, 0, NULL);
    Done(p);
    CHECK(TakeMassive(s_dwClient, 0xB001, &got) && Same(got, B("02 01")));
    CHECK(s_vecCall.empty());

    struct CASE
    {
        int         nResult;
        const char* pszRow;
        const char* pszWant;
    };
    static const CASE s_aCase[] =
    {
        { -2, "05 00 00 00 03 00 00 00", "02 03" },
        { 0, "00 00 00 00 03 00 00 00", "02 02" },
        { 0, "05 00 00 00 04 00 00 00", "02 04" },
    };
    for (size_t i = 0; i < sizeof(s_aCase) / sizeof(s_aCase[0]); ++i)
    {
        Answer(s_aCase[i].nResult, B(s_aCase[i].pszRow));
        p = LoginRequest(24);
        s_pSM->OnMsg_7001(p, 0, 0, NULL);
        Done(p);
        CHECK(Query("exec _CertifyUser 'smc','pw','key'", DEFAULT_DB, 8));
        CHECK(TakeMassive(s_dwClient, 0xB001, &got) && Same(got, B(s_aCase[i].pszWant)));
    }
    CHECK(s_pSM->m_mapUser.empty() && s_vecLog.empty());

    // Logged in: {1, security, division, certification data (groups, settings, addresses)}.
    s_aBody[0].byDivisionID = 29;
    Answer(0, B("05 00 00 00 03 00 00 00"));
    p = LoginRequest(24);
    CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
    Done(p);
    s_aBody[0].byDivisionID = 7;
    CHECK(Query("exec _CertifyUser_For_SMC 'smc','pw','key'", DEFAULT_DB, 8));
    Bytes want = B("01 03 03 00 02 00 01");
    Put(want, &s_SettingLogin, sizeof(s_SettingLogin));
    Put(want, "\x02\x00\x02", 3);
    CHECK(TakeMassive(s_dwClient, 0xB001, &got) && Same(got, want));
    CHECK(TakeLog("log 1 9 Login : smc"));
    sServiceManagerUser* pUser = s_pSM->FindUser(s_dwServer);
    CHECK(pUser && pUser->dwID == 5 && pUser->strID == "smc" && pUser->strPassword == "pw");
    CHECK(pUser && pUser->bySecurity == 3 && pUser->dwSessionID == s_dwServer);
    CHECK(g_setNotifySession.count(s_dwServer) == 1);
    g_setNotifySession.clear();
    CHECK(NoPackets());
}

static void TestArchitecture()
{
    CertModuleList modules;
    modules.push_back(&s_aModule[0]);
    modules.push_back(&s_aModule[1]);
    CertContentList contents;
    CertDivisionList divisions;
    divisions.push_back(&s_Division);
    CertFarmList farms;
    CertFarmContentList farmContents;
    CertShardList shards;
    shards.push_back(&s_Shard);
    CertMachineList machines;
    machines.push_back(&s_aMachine[0]);
    ServerCordList cords;
    cords.push_back(&s_CordUp);
    g_pCertModuleList = &modules;
    g_pCertContentList = &contents;
    g_pCertDivisionList = &divisions;
    g_pCertFarmList = &farms;
    g_pCertFarmContentList = &farmContents;
    g_pCertShardList = &shards;
    g_pCertMachineList = &machines;
    g_pServerCordList = &cords;
    CMsg* p = Req(0x7002, Bytes());
    CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
    Done(p);
    Bytes want;
    for (int i = 0; i < 2; ++i)
    {
        PutB(want, 1);
        Put(want, &s_aModule[i], 65);
    }
    want.push_back(0);
    want.push_back(0);
    PutB(want, 1);
    Put(want, &s_Division, 291);
    want.push_back(0);
    want.push_back(0);
    want.push_back(0);
    PutB(want, 1);
    Put(want, &s_Shard, 559);
    want.push_back(0);
    PutB(want, 1);
    Put(want, &s_aMachine[0], 71);
    want.push_back(0);
    for (ServerBodyList::iterator it = s_listBody.begin(); it != s_listBody.end(); ++it)
    {
        PutB(want, 1);
        Put(want, *it, 40);
    }
    want.push_back(0);
    PutB(want, 1);
    Put(want, &s_CordUp, 17);
    want.push_back(0);
    Bytes got;
    CHECK(TakeMassive(s_dwClient, 0xB002, &got) && Same(got, want));
    g_pCertModuleList = NULL;
    g_pCertContentList = NULL;
    g_pCertDivisionList = NULL;
    g_pCertFarmList = NULL;
    g_pCertFarmContentList = NULL;
    g_pCertShardList = &s_listShard;
    g_pCertMachineList = NULL;
    g_pServerCordList = NULL;
    CHECK(NoPackets());
}

// The handlers that only post a job.
static void TestJobHandlers()
{
    struct CASE
    {
        WORD        wOpcode;
        const char* pszData;
        const char* pszJob;
        const char* pszLog;
    };
    static const CASE s_aCase[] =
    {
        { 0x7003, "", "ModuleVersion 1", NULL },
        { 0x7004, "04 09", "ModuleVersionFile 1 4 9", NULL },
        { 0x7400, "02 00 61 62", "QueryUser ab 1", "log 1 12 Query User REQ : ab" },
        { 0x7404, "01 00 2d 02 01 00 62 01 00 65", "QuerySMLog b e 1 [] 2",
          "log 1 23 Query SMLog REQ : User_ID[] Begin[b] End[e]" },
        { 0x7404, "01 00 78 02 01 00 62 01 00 65", "QuerySMLog b e 1 [x] 2",
          "log 1 23 Query SMLog REQ : User_ID[x] Begin[b] End[e]" },
        { 0x7405, "01 01 00 71", "QueryContentUserAccount [q] 1 1",
          "log 1 24 Query Content User Account REQ : Query String [q]" },
        { 0x7405, "02", "QueryContentUserAccount [] 1 2",
          "log 1 24 Query Content User Account REQ : Query Blocked User" },
        { 0x7407, "01 00 75", "QueryPunishment u 1",
          "log 1 24 Query Punishment REQ : User ID[ u]" },
        { 0x7409, "01 00 75", "QueryCharOwnedUser u 1", "query 1 Query Char Owned By User [u]" },
        { 0x7410, "01 00 75", "QueryUserByUserID u 1", "query 1 Query User By User ID [u]" },
        { 0x7412, "0a 00 03 01 00 62 01 00 65", "ConcurrentUserLog 1 10 3 b e", NULL },
        { 0x7206, "05", "Article 0 [(null)] 1 5 0 [(null)]", NULL },
        { 0x7207, "05 01 00 74 01 00 78", "Article 1 [t] 1 5 0 [x]", "log 1 11 Add Article t" },
        { 0x7208, "09 00 00 00", "Article 2 [(null)] 1 0 9 [(null)]",
          "log 1 11 Delete Article 9" },
        { 0x7209, "09 00 00 00 01 00 74 01 00 78", "Article 3 [t] 1 0 9 [x]",
          "log 1 11 Edit Article" },
    };
    for (size_t i = 0; i < sizeof(s_aCase) / sizeof(s_aCase[0]); ++i)
    {
        const CASE& c = s_aCase[i];
        CMsg* p = Req(c.wOpcode, B(c.pszData));
        CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
        CHECK(p->GetUnreadSize() == 0);
        Done(p);
        CHECK(PostedJob(c.pszJob));
        if (c.pszLog)
            CHECK(TakeLog(c.pszLog));
        CHECK(s_vecLog.empty());
    }

    // 0x7408 (massive): the punishment job.
    CMassiveMsg* pMassive = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pMassive->SetOpcode(0x7408);
    Bytes v = B("02 07 00 00 00 64 00 00 00 03");
    PutS(v, "s1");
    PutW(v, 10);
    PutS(v, "s2");
    PutS(v, "s3");
    PutS(v, "s4");
    PutS(v, "s5");
    PutS(v, "s6");
    SYSTEMTIME at[4];
    memset(at, 0, sizeof(at));
    for (int i = 0; i < 4; ++i)
    {
        at[i].wYear = (WORD)(2011 + i);
        at[i].wSecond = (WORD)i;
    }
    Put(v, at, sizeof(at));
    pMassive->Write(&v[0], (int)v.size());
    pMassive->m_nMode = MASSIVE_MODE_READ;
    pMassive->BeginRead();
    pMassive->m_dw36 = s_dwServer;
    CHECK(s_pSM->Dispatch(NULL, pMassive, 0, 0));
    pMassive->Free();
    CHECK(PostedJob("Punishment s1 s2 1 2 7 100 3 10 s3 s4 s5 s6 2011/0 2012/1 2013/2 2014/3"));
    CHECK(TakeLog("log 1 24 Punishment Job REQ : Job[ 2], Serial No[ 7], User ID[ 100]"));

    // Automated punishments; the posting result is returned.
    std::string str("victim");
    CHECK(s_pSM->RegisterAutomatedPunishment(&str, 5, 10, 2, 9));
    CHECK(PostedJob("AutomatedPunishment victim 5 10 2 9"));
    s_pMain->m_bAcceptJobs = false;
    s_pSM->RegisterAutomatedPunishment(&str, 5, 10, 2, 9);
    s_pMain->m_bAcceptJobs = true;
    CHECK(PostedJob("AutomatedPunishment victim 5 10 2 9"));
    CHECK(s_pMain->m_vecJobs.empty());
    CHECK(NoPackets());
}

static void TestSecurityDescriptions()
{
    struct CASE
    {
        WORD        wOpcode;
        const char* pszData;
        const char* pszSQL;
        const char* pszLog;
    };
    static const CASE s_aCase[] =
    {
        { 0x7300, "01 00 6e 01 00 64", "insert _SecurityDescription values( 'n', 'd')",
          "log 1 12 Add Security Description REQ : Name[n], Desc[d]" },
        { 0x7301, "08 00 00 00", "delete _SecurityDescription where nID = 8",
          "log 1 12 Delete Security Description REQ : ID[8] Name[SMC_Login]" },
        { 0x7302, "01 00 6e 01 00 64", "insert _SecurityDescriptionGroup values( 'n', 'd')",
          "log 1 12 Add Security Description Group REQ : Name[n] Description[d]" },
        { 0x7303, "09", "delete _SecurityDescriptionGroup where nID = '9'", NULL },
        { 0x7304, "03 08 00 00 00", "insert _SecurityDescriptionGroupAssign values( '3', '8')",
          "log 1 12 Add Security Descrition Group Assign : Group_ID[3] Description_ID[8]" },
        { 0x7305, "03 08 00 00 00",
          "delete _SecurityDescriptionGroupAssign where nGroupID = 3 and nDescriptionID = 8",
          "log 1 12 Delete Security Description Group Assign : Group_ID[3] Description_ID[8]" },
        { 0x7307, "08 00 00 00 01 00 64",
          "update _SecurityDescription set szDesc = 'd' where nID = 8",
          "log 1 12 Modify Security Description : ID[8] Description[d]" },
    };
    g_mapCertDataSetting[8] = &s_SettingLogin;
    for (size_t i = 0; i < sizeof(s_aCase) / sizeof(s_aCase[0]); ++i)
    {
        const CASE& c = s_aCase[i];
        Answer(0);
        CMsg* p = Req(c.wOpcode, B(c.pszData));
        CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
        Done(p);
        CHECK(Query(c.pszSQL, DEFAULT_DB, 0));
        CHECK(Reply((WORD)(c.wOpcode + 0x4000), B("01")));
        CHECK(PostedJob("RefreshSecurityData 1"));
        if (c.pszLog)
            CHECK(TakeLog(c.pszLog));
        Answer(-2);
        p = Req(c.wOpcode, B(c.pszData));
        CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
        Done(p);
        CHECK(Query(c.pszSQL, DEFAULT_DB, 0));
        CHECK(Reply((WORD)(c.wOpcode + 0x4000), B("02 00")));
        CHECK(s_pMain->m_vecJobs.empty());
        s_vecLog.clear();
    }
    // a known group is logged with the address of its name
    CERT_DATA_GROUP group;
    group.byID = 9;
    g_mapCertDataGroup[9] = &group;
    Answer(0);
    CMsg* p = Req(0x7303, B("09"));
    s_pSM->OnMsg_7303(p, 0, 0, NULL);
    Done(p);
    CHECK(Query("delete _SecurityDescriptionGroup where nID = '9'", DEFAULT_DB, 0));
    CHECK(Reply(0xB303, B("01")) && PostedJob("RefreshSecurityData 1"));
    char sz[128];
    sprintf_s(sz, "log 1 12 Delete Security Description Group REQ : ID[9] Name[%d]", group.ab1);
    CHECK(TakeLog(sz));
    g_mapCertDataGroup.clear();
    g_mapCertDataSetting.clear();
    CHECK(NoPackets());
}

static void TestUserAccounts()
{
    SM_USER_RECORD rec;
    memset(&rec, 0, sizeof(rec));
    rec.dwID = 12;
    strcpy_s(rec.szUserID, "user");
    memset(rec.szPassword, 'p', 32);
    strcpy_s(rec.szName, "name");
    memset(rec.szA, 'a', 32);
    memset(rec.szB, 'b', 32);
    strcpy_s(rec.szC, "c");
    strcpy_s(rec.szD, "d");
    rec.by1F4 = 4;
    rec.by1F5 = 5;
    strcpy_s(rec.sz1F6, "z");
    std::string strP(32, 'p');
    std::string strA(32, 'a');
    std::string strB(32, 'b');
    std::string strFields = "'" + strP + "','name','" + strA + "','" + strB + "','c','d',4,5,'z'";
    Bytes v;
    Put(v, &rec, sizeof(rec));

    Answer(0);
    CMsg* p = Req(0x7401, v);
    CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
    Done(p);
    CHECK(Query(("exec _AddUser 'user'," + strFields).c_str(), DEFAULT_DB, 0));
    CHECK(Reply(0xB401, B("01")));
    CHECK(TakeLog("log 1 12 Add User REQ : user"));
    Answer(-2);
    p = Req(0x7401, v);
    CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
    Done(p);
    CHECK(Query(("exec _AddUser 'user'," + strFields).c_str(), DEFAULT_DB, 0));
    CHECK(Reply(0xB401, B("02 01")));
    s_vecLog.clear();

    Answer(0);
    p = Req(0x7403, v);
    CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
    Done(p);
    CHECK(Query(("exec _EditUser 12," + strFields).c_str(), DEFAULT_DB, 0));
    CHECK(Reply(0xB403, B("01")));
    CHECK(s_vecLog.size() == 1
          && s_vecLog[0].compare(0, 42, "log 1 12 Modify User REQ : ID[12] User_ID[") == 0);
    s_vecLog.clear();

    Answer(0);
    p = Req(0x7402, B("0c 00 00 00"));
    CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
    Done(p);
    CHECK(Query("exec _DeleteUser 12", DEFAULT_DB, 0));
    CHECK(Reply(0xB402, B("01")));
    CHECK(TakeLog("log 1 12 Delete User REQ : ID[12]"));
    Answer(-2);
    p = Req(0x7402, B("0c 00 00 00"));
    CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
    Done(p);
    CHECK(Query("exec _DeleteUser 12", DEFAULT_DB, 0));
    CHECK(Reply(0xB402, B("02 01")));
    s_vecLog.clear();

    // 0x7411: the JID of a game account; the query depends on the division.
    struct CASE
    {
        BYTE        byDivision;
        const char* pszSQL;
    };
    static const CASE s_aCase[] =
    {
        { 1, "select sr_code from sr_member1 with (nolock) where sr_id = 'acc'" },
        { 2, "select sr_code from sr_member1 with (nolock) where sr_id = 'acc'" },
        { 3, "select JID from TB_User with (nolock) where strUserID = 'acc'" },
        { 4, "select nID from sr_member1 with (nolock) where sr_id = 'acc'" },
        { 18, "select nID from _User with (nolock) where szUserID = 'acc'" },
        { 19, "select JID from TB_User with (nolock) where strUserID = 'acc'" },
    };
    for (size_t i = 0; i < sizeof(s_aCase) / sizeof(s_aCase[0]); ++i)
    {
        s_Division.byID = s_aCase[i].byDivision;
        Answer(0, B("21 43 00 00"));
        p = Req(0x7411, B("03 00 61 63 63"));
        CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
        Done(p);
        CHECK(Query(s_aCase[i].pszSQL, ACCOUNT_DB, 4));
        CHECK(Reply(0xB411, B("01 21 43 00 00")));
    }
    Answer(0);
    p = Req(0x7411, B("03 00 61 63 63"));
    s_pSM->OnMsg_7411(p, 0, 0, NULL);
    Done(p);
    CHECK(Query("select JID from TB_User with (nolock) where strUserID = 'acc'", ACCOUNT_DB, 4));
    CHECK(Reply(0xB411, B("02 01")));
    Answer(-1);
    p = Req(0x7411, B("03 00 61 63 63"));
    s_pSM->OnMsg_7411(p, 0, 0, NULL);
    Done(p);
    CHECK(Query("select JID from TB_User with (nolock) where strUserID = 'acc'", ACCOUNT_DB, 4));
    CHECK(Reply(0xB411, B("02 00")));
    s_Division.byID = 3;
    CHECK(NoPackets());
}

static void TestContentUsers()
{
    sContentUser tmp;
    tmp.dwIP = 0x0100000A;
    tmp.dwJID = 0x11;
    tmp.strID = "player";
    tmp.byContentID = 22;
    tmp.wShardID = 10;
    tmp.wAgentBodyID = 2;
    tmp.strMAC = "00-11-22-33-44-55";
    tmp.PlayTime.Clear();
    CHECK(g_ContentUserManager.Register(&tmp) == 0);

    // 0x7200: 0xB200 (massive) {1, users}
    CMsg* p = Req(0x7200, B("01 00 2a"));
    CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
    Done(p);
    CMassiveMsg* pWant = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pWant->SetOpcode(0xB200);
    g_ContentUserManager.WriteUserQuery(pWant, "*");
    pWant->m_nMode = MASSIVE_MODE_READ;
    pWant->BeginRead();
    Bytes want(1, 1);
    int nSize = pWant->GetUnreadSize();
    want.resize(1 + nSize);
    pWant->Read(&want[1], nSize);
    pWant->Free();
    Bytes got;
    CHECK(TakeMassive(s_dwClient, 0xB200, &got) && Same(got, want));
    CHECK(TakeLog("log 1 15 Online User Query REQ : *"));

    // 0x7201: ban; 0x620D {JID} to the user's AgentServer (body 2)
    p = Req(0x7201, B("06 00 70 6c 61 79 65 72"));
    CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
    Done(p);
    DWORD dwRelayID = 0;
    CHECK(Relay(2, 0x620D, B("11 00 00 00"), &dwRelayID));
    CHECK(TakeLog("log 1 14 Online User Ban REQ : player"));
    sSMDefault_RelayMsg_Param* pParam = (sSMDefault_RelayMsg_Param*)RelayContext(dwRelayID);
    CHECK(pParam->dw8 == 0x11 && pParam->wBodyID == 2);
    // the answer {1} keeps the user, {2} removes it
    p = Req(0xA20D, B("01"), 77);
    CHECK(s_pSM->Dispatch(p, NULL, dwRelayID, (DWORD)pParam));
    Done(p);
    CHECK(Reply(0xB201, B("01")));
    CHECK(g_ContentUserManager.Find(0x11) != NULL);
    pParam = Param(2, 0x11);
    p = Req(0xA20D, B("02"), 77);
    CHECK(s_pSM->Dispatch(p, NULL, dwRelayID, (DWORD)pParam));
    Done(p);
    CHECK(Reply(0xB201, B("01")));
    CHECK(g_ContentUserManager.Find(0x11) == NULL);
    CHECK(g_SMRelayParamPool.m_nInUse == 0);
    p = Req(0x7201, B("06 00 70 6c 61 79 65 72"));
    s_pSM->OnMsg_7201(p, 0, 0, NULL);
    Done(p);
    CHECK(Reply(0xB201, B("02 01")));
    tmp.wAgentBodyID = 4;
    CHECK(g_ContentUserManager.Register(&tmp) == 0);
    p = Req(0x7201, B("06 00 70 6c 61 79 65 72"));
    s_pSM->OnMsg_7201(p, 0, 0, NULL);
    Done(p);
    CHECK(Reply(0xB201, B("02 02")));
    CHECK(g_SMRelayParamPool.m_nInUse == 1);         // GM-001
    g_SMRelayParamPool.m_nInUse = 0;
    s_vecLog.clear();

    // 0x7204: {division, users, daily, all, shard counts}
    p = Req(0x7204, Bytes());
    CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
    Done(p);
    want = B("07");
    PutD(want, g_ContentUserManager.m_nUserCount);
    PutD(want, g_ContentUserManager.m_nDailyVisited);
    PutD(want, g_ContentUserManager.m_nTotalVisited);
    CMsg* pCount = g_pBSNet->AllocMsg(FALSE);
    g_ContentUserManager.WriteShardUserCount(pCount);
    want.insert(want.end(), pCount->m_pData + pCount->m_wReadPos,
                pCount->m_pData + pCount->m_wWritePos);
    g_pBSNet->ReleaseMsg(pCount);
    CHECK(Reply(0xB204, want));

    // 0x6313 {JID, text} relayed from an AgentServer: logged, answered 0xA313 {1}
    p = Req(0x6313, B("11 00 00 00 02 00 68 69"));
    CHECK(s_pSM->Dispatch(p, NULL, 0x55, 0));
    Done(p);
    CHECK(TakeLog("user hi player"));
    CHECK(Reply(0xA008, B("01 55 00 00 00 13 a3 01")));
    p = Req(0x6313, B("12 00 00 00 02 00 68 69"));
    s_pSM->OnMsg_6313(p, 0x56, 0, NULL);
    Done(p);
    CHECK(s_vecLog.empty());
    CHECK(Reply(0xA008, B("01 56 00 00 00 13 a3 01")));
    g_ContentUserManager.Unregister(0x11, NULL);

    // 0x7205: the franchise addresses are loaded again
    Answer(0);
    p = Req(0x7205, Bytes());
    CHECK(s_pSM->Dispatch(p, NULL, 0, 0));
    Done(p);
    CHECK(s_vecCall.size() == 1);
    s_vecCall.clear();
    CHECK(Reply(0xB205, B("01 00 00 00 00")));
    CHECK(TakeLog("log 1 13 Launch Server REQ"));
    Answer(-1);
    p = Req(0x7205, Bytes());
    s_pSM->OnMsg_7205(p, 0, 0, NULL);
    Done(p);
    s_vecCall.clear();
    CHECK(Reply(0xB205, B("02")));
    s_vecLog.clear();
    CHECK(NoPackets());
}

// 0x4023 (massive) to a body and the answer 0x8023.
static void TestRecordRelay()
{
    BYTE abRecord[2443];
    for (int i = 0; i < 2443; ++i)
        abRecord[i] = (BYTE)i;
    CMassiveMsg* pMassive = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pMassive->SetOpcode(0x4023);
    WORD wBody = 3;
    pMassive->Write(&wBody, 2);
    BYTE b = 1;
    pMassive->Write(&b, 1);
    pMassive->Write(abRecord, 2443);
    pMassive->Write(&b, 1);
    pMassive->Write(abRecord, 2443);
    b = 0;
    pMassive->Write(&b, 1);
    pMassive->m_nMode = MASSIVE_MODE_READ;
    pMassive->BeginRead();
    pMassive->m_dw36 = s_dwServer;
    CHECK(s_pSM->Dispatch(NULL, pMassive, 0, 0));
    pMassive->Free();
    Bytes got;
    CHECK(TakeMassive(s_dwClient, SERVER_MSG_RELAY_REQ, &got) && got.size() == 8 + 2 * 2444 + 1);
    if (got.size() == 8 + 2 * 2444 + 1)
    {
        CHECK(*(WORD*)&got[4] == 3 && *(WORD*)&got[6] == 0x4023);
        Bytes want;
        for (int i = 0; i < 2; ++i)
        {
            PutB(want, 1);
            Put(want, abRecord, 2443);
        }
        PutB(want, 0);
        CHECK(Same(Bytes(got.begin() + 8, got.end()), want));
        // the context of the massive relay (framework 0x48de30)
        sSMDefault_RelayMsg_Param* pParam =
            (sSMDefault_RelayMsg_Param*)RelayContext(*(DWORD*)&got[0]);
        CHECK((DWORD)pParam != 0xFFFFFFFF && pParam->dw8 == 0x4023 && pParam->wBodyID == 3);
        CMsg* p = Req(0x8023, Bytes(), 77);
        CHECK(s_pSM->Dispatch(p, NULL, 0, (DWORD)pParam));
        Done(p);
        CHECK(Reply(0x8023, B("01")));
    }
    // no route: 0x8023 {0}
    pMassive = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pMassive->SetOpcode(0x4023);
    wBody = 4;
    pMassive->Write(&wBody, 2);
    pMassive->Write(&b, 1);
    pMassive->m_nMode = MASSIVE_MODE_READ;
    pMassive->BeginRead();
    pMassive->m_dw36 = s_dwServer;
    s_pSM->OnMsg_4023(NULL, 0, 0, pMassive);
    pMassive->Free();
    CHECK(Reply(0x8023, B("00")));
    CHECK(g_SMRelayParamPool.m_nInUse == 0);
    CHECK(NoPackets());
}

static void Startup()
{
    BSNET_INIT_PARAM param;
    param.dwFlags = BSNET_FLAG_SERVER;
    param.nAcceptCount = 4;
    param.aSocket[0].dwSessionParam = TEST_TASK_SERVER;
    param.aSocket[1].dwSessionParam = TEST_TASK_CLIENT;
    CHECK(s_Engine.Initialize(param));
    g_pBSNet = &s_Engine;
    CHECK(s_Engine.StartProactorWorkers(NETENGINE_TASK_LISTEN, 1, 0, 0, 0, NULL));
    CHECK(s_Engine.StartProactorWorkers(NETENGINE_TASK_IO, 1, 0, 0, 0, NULL));
    CHECK(s_Engine.RegisterProactor(TEST_TASK_SERVER, NULL, &CTestTask::classCTestTask, NULL));
    CHECK(s_Engine.RegisterProactor(TEST_TASK_CLIENT, NULL, &CTestTask::classCTestTask, NULL));
    CHECK(s_Engine.StartProactorWorkers(TEST_TASK_SERVER, 1, 0, 0, 0, NULL));
    CHECK(s_Engine.StartProactorWorkers(TEST_TASK_CLIENT, 1, 0, 0, 0, NULL));
    s_pMain = (CTestTask*)s_Engine.GetProactor(TEST_TASK_SERVER)->GetJob();
    s_pMain->m_bAcceptJobs = true;
    CHECK(s_Engine.Listen("127.0.0.1", 0));
    sockaddr_in local;
    s_Engine.m_pListener->GetLocalAddr(&local);
    s_dwClient = s_Engine.Connect("127.0.0.1", ntohs(local.sin_port), NULL, TEST_TASK_CLIENT, 0);
    CHECK(s_dwClient != 0);
    // The server task gets 0x1001 {session, role} for the accepted session.
    DWORD dwStart = GetTickCount();
    while (GetTickCount() - dwStart < 3000 && !s_dwServer)
    {
        EnterCriticalSection(&s_cs);
        for (std::deque<PACKET>::iterator it = s_queIn.begin(); it != s_queIn.end(); ++it)
        {
            if (it->dwTask == (DWORD)s_pMain && it->wOpcode == 0x1001 && it->data.size() >= 4)
                s_dwServer = *(const DWORD*)&it->data[0];
        }
        LeaveCriticalSection(&s_cs);
        Sleep(5);
    }
    CHECK(s_dwServer != 0 && s_dwServer != s_dwClient);
    // the handshake of CServerProcessMain::OnSessionCreated
    SECURITY_HANDSHAKE_PARAM hs;
    memset(&hs, 0, sizeof(hs));
    hs.bOptions = 6;
    hs.dwCountSeed = 7;
    hs.dwCrcSeed = 9;
    for (int i = 0; i < 8; ++i)
        hs.BlowFishKey[i] = (BYTE)(i + 1);
    CHECK(s_Engine.StartHandshake(s_dwServer, &hs));
    CHECK(TakePacket(s_dwClient, 0x1003, NULL));
    Sleep(300);
    EnterCriticalSection(&s_cs);
    s_queIn.clear();
    LeaveCriticalSection(&s_cs);

    SetupArchitecture();
    SetupCertData();
    g_pDefaultDBSession = DEFAULT_DB;
    g_pAccountDBSession = ACCOUNT_DB;
    s_pSM = new CServiceManagerProcess;
    s_pExpectTask = s_Engine.GetProactor(TEST_TASK_SERVER);
    s_pSM->Init(s_pExpectTask);
}

int main()
{
    CHECK(LoopbackBind_Install() == 1);
    setvbuf(stdout, NULL, _IONBF, 0);
    BSDebugSetFlags(0, 0);
    InitializeCriticalSection(&s_cs);

    Startup();
    TestPools();
    TestDispatch();
    TestRelayFailed();
    TestSessionClosed();
    TestBodyCommands();
    TestServiceCommands();
    TestGatewayRelays();
    TestShardSettings();
    TestShardService();
    TestLogin();
    TestArchitecture();
    TestJobHandlers();
    TestSecurityDescriptions();
    TestUserAccounts();
    TestContentUsers();
    TestRecordRelay();

    CHECK(s_deqAnswer.empty() && s_vecCall.empty());
    delete s_pSM;
    s_Engine.Shutdown();
    g_pBSNet = NULL;
    DeleteCriticalSection(&s_cs);
    return TEST_RESULT("GlobalManagerSvcTest");
}
