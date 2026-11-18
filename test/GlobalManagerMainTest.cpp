// GlobalManager main process and application object (CGlobalManagerMainProcess, CGlobalManager).
// A real engine is connected to itself: the GlobalManager side of the connection is the route
// of every body, so the relay answers (0xA008) and relays (0x6008) of the handlers are checked
// byte by byte at the client. The billing manager is a recording stand-in; CDBQuery's execution
// and CDatabase's sessions are replaced (scripted results); the punishment manager, the user
// validation history, the service manager and the content users are the real ones.

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
#include "LogCapture.h"

#include "GlobalManager/ContentUser.h"
#include "GlobalManager/GlobalManager.h"
#include "GlobalManager/GlobalManagerDB.h"
#include "GlobalManager/GlobalManagerJobs.h"
#include "GlobalManager/GlobalManagerMainFwd.h"
#include "GlobalManager/GlobalManagerMainProcess.h"
#include "GlobalManager/GlobalManagerSlave.h"
#include "GlobalManager/GlobalManagerSvcFwd.h"
#include "GlobalManager/PunishmentManager.h"
#include "GlobalManager/ServiceManagerJobs.h"
#include "GlobalManager/UserValidationHistory.h"
#include "BSLib/BSDebug.h"
#include "BSLib/DB/Database.h"
#include "BSLib/DB/DBQuery.h"
#include "BSLib/Util/TimeSpan.h"
#include "BSNet/NetEngine.h"
#include "BSNet/SockListener.h"
#include "BSNet/security/Security.h"
#include "ServerFramework/CertData.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerConfig.h"
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

static std::string Hex(const Bytes& v)
{
    std::string s;
    char sz[4];
    for (size_t i = 0; i < v.size() && i < 64; ++i)
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
    size_t i = 0;
    while (i < vGot.size() && i < vWant.size() && vGot[i] == vWant[i])
        ++i;
    printf("  differ at %u\n  got:  %s(%u)\n  want: %s(%u)\n", (unsigned)i, Hex(vGot).c_str(),
           (unsigned)vGot.size(), Hex(vWant).c_str(), (unsigned)vWant.size());
    return false;
}

// ---------------------------------------------------------------------------------------------
// Logs

static std::vector<std::string> s_vecLog;

static void CollectLogs()
{
    std::vector<CAPTURED_LOG> v = LogCapture_Drain();
    for (size_t i = 0; i < v.size(); ++i)
        s_vecLog.push_back(v[i].strText);
}

// True when a line starting with pszText was logged (it is removed).
static bool Logged(const char* pszText)
{
    CollectLogs();
    for (std::vector<std::string>::iterator it = s_vecLog.begin(); it != s_vecLog.end(); ++it)
    {
        if (it->compare(0, strlen(pszText), pszText) == 0)
        {
            s_vecLog.erase(it);
            return true;
        }
    }
    printf("  not logged: %s\n", pszText);
    return false;
}

static void ClearLogs()
{
    CollectLogs();
    s_vecLog.clear();
}

// ---------------------------------------------------------------------------------------------
// Tasks: both record every message (the timer ticks of the server task are dropped).

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
        if (packet.wOpcode == SERVER_MSG_TIMER_TICK)
            return;
        EnterCriticalSection(&s_cs);
        s_queIn.push_back(packet);
        LeaveCriticalSection(&s_cs);
    }

    // The service manager posts its jobs through the task's job.
    virtual bool SendOverlapJob(COverlapJob_Local* pJob, CTask*)
    {
        m_vecJobs.push_back(pJob);
        return true;
    }

    std::vector<COverlapJob_Local*> m_vecJobs;
};

BS_IMPLEMENT_DYNCREATE(CTestTask, &CServerProcessBase::classCServerProcessBase)

// The real main process; the jobs are recorded instead of sent.
class CTestMain : public CGlobalManagerMainProcess
{
public:
    virtual bool SendOverlapJob(COverlapJob_Local* pJob, CTask* pTask)
    {
        m_vecJobs.push_back(pJob);
        m_vecJobTasks.push_back(pTask);
        return true;
    }

    std::vector<COverlapJob_Local*> m_vecJobs;
    std::vector<CTask*>             m_vecJobTasks;
};

// The billing manager: records ProcessMsg (opcode and data) and answers from m_nResult.
class CFakeBilling : public IBillingMgr
{
public:
    CFakeBilling() : m_bReady(TRUE), m_nResult(1), m_nStop(0), m_nCleanup(0) {}

    virtual BOOL Initialize(const char*, WORD, WORD) { return TRUE; }
    virtual void Cleanup() { ++m_nCleanup; ++s_nCleanup; }
    virtual BOOL Start() { return TRUE; }
    virtual void Stop() { ++m_nStop; ++s_nStop; }
    virtual void Release() { ++s_nRelease; delete this; }
    virtual BOOL IsReady() { return m_bReady; }
    virtual int ProcessMsg(CMsg* pMsg, DWORD dwSessionID, DWORD dwRelayID)
    {
        Bytes v;
        PutW(v, pMsg->GetOpcode());
        Put(v, pMsg->m_pData + MSG_HEADER_SIZE, pMsg->m_wWritePos - MSG_HEADER_SIZE);
        m_vecMsg.push_back(v);
        m_vecSession.push_back(dwSessionID);
        m_vecRelay.push_back(dwRelayID);
        return m_nResult;
    }

    BOOL                m_bReady;
    int                 m_nResult;
    int                 m_nStop;
    int                 m_nCleanup;
    std::vector<Bytes>  m_vecMsg;
    std::vector<DWORD>  m_vecSession;
    std::vector<DWORD>  m_vecRelay;

    void Clear()
    {
        m_vecMsg.clear();
        m_vecSession.clear();
        m_vecRelay.clear();
    }

    static int          s_nStop;
    static int          s_nCleanup;
    static int          s_nRelease;
};

int CFakeBilling::s_nStop;
int CFakeBilling::s_nCleanup;
int CFakeBilling::s_nRelease;

static CNetEngine s_Engine;
static CTestTask* s_pServerJob;     // the server task's job
static CTask* s_pTask;              // the server task (owner of the main process)
static CTask* s_pClientTask;
static DWORD s_dwClient;            // client side of the connection
static DWORD s_dwServer;            // GlobalManager side
static CTestMain* s_pMain;
static CFakeBilling* s_pBilling;

static bool TakePacket(DWORD dwSession, WORD wOpcode, Bytes* pData, DWORD dwTimeout = 3000,
                       bool bReport = true)
{
    DWORD dwStart = GetTickCount();
    for (;;)
    {
        EnterCriticalSection(&s_cs);
        for (std::deque<PACKET>::iterator it = s_queIn.begin(); it != s_queIn.end(); ++it)
        {
            if ((dwSession == 0xFFFFFFFF || it->dwSession == dwSession) && it->wOpcode == wOpcode)
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

static bool Packet(WORD wOpcode, const Bytes& want)
{
    Bytes got;
    if (!TakePacket(s_dwClient, wOpcode, &got))
        return false;
    return Same(got, want);
}

// The data of the next massive message with the inner opcode at the client.
static bool Massive(WORD wOpcode, const Bytes& want)
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
            if (packet.dwSession != s_dwClient || packet.wOpcode != MASSIVE_MSG_OPCODE
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
            Bytes got;
            for (size_t i = 0; i < vecParts.size(); ++i)
            {
                const Bytes& part = s_queIn[vecParts[i]].data;
                got.insert(got.end(), part.begin() + 1, part.end());
            }
            for (size_t i = vecParts.size(); i > 0; --i)
                s_queIn.erase(s_queIn.begin() + vecParts[i - 1]);
            s_queIn.erase(s_queIn.begin() + nHeader);
            LeaveCriticalSection(&s_cs);
            return Same(got, want);
        }
        LeaveCriticalSection(&s_cs);
        if (GetTickCount() - dwStart > 3000)
        {
            printf("  no massive 0x%04x\n", wOpcode);
            return false;
        }
        Sleep(5);
    }
}

// 0xA008 {1, relay ID, opcode, data} at the client.
static bool RelayReply(DWORD dwRelayID, WORD wOpcode, const Bytes& data)
{
    Bytes want;
    PutB(want, 1);
    PutD(want, dwRelayID);
    PutW(want, wOpcode);
    want.insert(want.end(), data.begin(), data.end());
    return Packet(SERVER_MSG_RELAY_ACK, want);
}

// The data of the next relay (0x6008) at the client: target body, opcode, data.
static bool Relayed(WORD wTarget, WORD wOpcode, const Bytes& data)
{
    Bytes got;
    if (!TakePacket(s_dwClient, SERVER_MSG_RELAY_REQ, &got))
        return false;
    Bytes want;
    PutW(want, wTarget);
    PutW(want, wOpcode);
    want.insert(want.end(), data.begin(), data.end());
    return got.size() > 4 && Same(Bytes(got.begin() + 4, got.end()), want);
}

static CMsg* Req(WORD wOpcode, const Bytes& data)
{
    CMsg* p = g_pBSNet->AllocMsg(FALSE);
    p->SetOpcode(wOpcode);
    if (!data.empty())
        p->WriteBytes(&data[0], (WORD)data.size());
    p->m_dwSessionID = s_dwServer;
    return p;
}

static void Done(CMsg* pMsg)
{
    g_pBSNet->ReleaseMsg(pMsg);
}

static CMassiveMsg* MassiveReq(WORD wOpcode, const Bytes& data)
{
    CMassiveMsg* pMassive = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pMassive->SetOpcode(wOpcode);
    if (!data.empty())
        pMassive->Write(&data[0], (int)data.size());
    pMassive->m_nMode = MASSIVE_MODE_READ;
    pMassive->BeginRead();
    pMassive->m_dw36 = s_dwServer;
    return pMassive;
}

// ---------------------------------------------------------------------------------------------
// Query and database doubles

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

static bool Query(const char* pszSQL, CDBSessionMgr* pSession)
{
    if (s_vecCall.empty())
    {
        printf("  no query (%s)\n", pszSQL);
        return false;
    }
    QUERY_CALL c = s_vecCall.front();
    s_vecCall.erase(s_vecCall.begin());
    bool b = c.strSQL == pszSQL && c.pSession == pSession;
    if (!b)
        printf("  query: %s (%p)\n  want:  %s (%p)\n", c.strSQL.c_str(), c.pSession, pszSQL,
               pSession);
    return b;
}

static bool NoQueries()
{
    bool b = s_vecCall.empty() && s_deqAnswer.empty();
    for (size_t i = 0; i < s_vecCall.size(); ++i)
        printf("  extra query: %s\n", s_vecCall[i].strSQL.c_str());
    s_vecCall.clear();
    s_deqAnswer.clear();
    return b;
}

static CDBSessionMgr* const DEFAULT_DB = (CDBSessionMgr*)0x1111;
static CDBSessionMgr* const ACCOUNT_DB = (CDBSessionMgr*)0x2222;
static CDBSessionMgr* const SILK_DB = (CDBSessionMgr*)0x3333;

static std::vector<std::string> s_vecDB;    // "init <text>", "open <text>", "close <p>"
static BOOL s_bInitResult = TRUE;
static std::deque<CDBSessionMgr*> s_deqSession;

BOOL CDatabase::InitEnvironment(const char* pszDefaultConnect)
{
    s_vecDB.push_back(std::string("init ") + pszDefaultConnect);
    return s_bInitResult;
}

CDBSessionMgr* CDatabase::CreateSession(const char* pszConnect)
{
    s_vecDB.push_back(std::string("open ") + (pszConnect ? pszConnect : "(null)"));
    if (s_deqSession.empty())
        return NULL;
    CDBSessionMgr* p = s_deqSession.front();
    s_deqSession.pop_front();
    return p;
}

void CDatabase::DestroySession(CDBSessionMgr* pSession)
{
    char sz[32];
    sprintf_s(sz, "close %p", pSession);
    s_vecDB.push_back(sz);
}

static bool DB(const char* pszWant)
{
    if (s_vecDB.empty())
    {
        printf("  no DB call (%s)\n", pszWant);
        return false;
    }
    std::string s = s_vecDB.front();
    s_vecDB.erase(s_vecDB.begin());
    if (s == pszWant)
        return true;
    printf("  DB: %s\n  want: %s\n", s.c_str(), pszWant);
    return false;
}

// ---------------------------------------------------------------------------------------------
// Architecture: body 1 this GlobalManager (parent 9), 2 a GatewayServer, 5 an AgentServer, 7 the
// shard manager of shards 10 and 11, 8 a FarmManager, 9 the parent; every route is the
// connection. The cords of body 1 are kept in s_aCord.

static CERT_MODULE s_aModule[6];
static SERVER_BODY s_aBody[6];
static SERVER_CORD s_CordUp;
static SERVER_CORD s_aCord[2];
static CERT_SHARD s_aShard[2];
static CERT_DIVISION s_Division;
static ServerBodyList s_listBody;
static ServerCordList s_listCord;
static CertShardList s_listShard;

static void AddModule(int i, BYTE byID, const char* pszName, const char* pszLower)
{
    s_aModule[i].byID = byID;
    strcpy_s(s_aModule[i].szName, pszName);
    g_mapCertModuleByName[pszLower] = &s_aModule[i];
}

static void AddBody(int i, WORD wID, int nModule, int nState)
{
    SERVER_BODY& b = s_aBody[i];
    b.wID = wID;
    b.byModuleID = s_aModule[nModule].byID;
    b.pModule = &s_aModule[nModule];
    b.nState = nState;
    b.byDivisionID = 3;
    b.pDivision = &s_Division;
    s_listBody.push_back(&b);
    g_mapServerBody[wID] = &b;
    if (i)
        g_hmapServerRoute[wID] = &s_CordUp;
}

static void SetupArchitecture()
{
    AddModule(0, 1, "GatewayServer", "gatewayserver");
    AddModule(1, 2, "AgentServer", "agentserver");
    AddModule(2, 3, "GlobalManager", "globalmanager");
    AddModule(3, 4, "ShardManager", "shardmanager");
    AddModule(4, 5, "FarmManager", "farmmanager");
    AddModule(5, 6, "Certification", "certification");
    s_CordUp.dwSessionID = s_dwServer;
    s_aShard[0].wID = 10;
    s_aShard[0].ab3[0] = 0x21;
    strcpy_s((char*)&s_aShard[0].ab3[1], 16, "Shard10");
    s_aShard[0].w557 = 25;
    s_aShard[0].w548 = 1000;        // a limit of 0 divides by zero (GM-010)
    s_aShard[1].w548 = 200;
    s_aShard[0].wManagerID = 7;
    s_aShard[1].wID = 11;
    s_aShard[1].w557 = 3;
    s_aShard[1].wManagerID = 7;
    s_Division.byID = 3;
    strcpy_s((char*)s_Division.ab1 + 32, 64, "DSN=Division");
    AddBody(0, 1, 2, 3);
    AddBody(1, 2, 0, 5);
    AddBody(2, 5, 1, 5);
    AddBody(3, 7, 3, 5);
    AddBody(4, 8, 4, 5);
    AddBody(5, 9, 5, 5);
    s_aBody[0].wParentID = 9;
    g_pServerBodyList = &s_listBody;
    g_pSelfBody = &s_aBody[0];
    g_mapCertShard[10] = &s_aShard[0];
    g_mapCertShard[11] = &s_aShard[1];
    s_listShard.push_back(&s_aShard[0]);
    s_listShard.push_back(&s_aShard[1]);
    g_pCertShardList = &s_listShard;
    s_aCord[0].dwID = 1;
    s_aCord[0].wFromID = 1;
    s_aCord[0].wToID = 7;
    s_aCord[0].dwStatus = 2;
    s_aCord[1].dwID = 2;
    s_aCord[1].wFromID = 8;
    s_aCord[1].wToID = 1;
    s_aCord[1].dwStatus = 3;
    s_listCord.push_back(&s_aCord[0]);
    s_listCord.push_back(&s_aCord[1]);
    g_pServerCordList = &s_listCord;
}

// ---------------------------------------------------------------------------------------------

static void Startup()
{
    // g_GlobalManager's engine was constructed after s_Engine and took g_pNetEngine.
    g_pNetEngine = &s_Engine;
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
    s_pTask = s_Engine.GetProactor(TEST_TASK_SERVER);
    s_pClientTask = s_Engine.GetProactor(TEST_TASK_CLIENT);
    s_pServerJob = (CTestTask*)s_pTask->GetJob();
    CHECK(s_Engine.Listen("127.0.0.1", 0));
    sockaddr_in local;
    s_Engine.m_pListener->GetLocalAddr(&local);
    s_dwClient = s_Engine.Connect("127.0.0.1", ntohs(local.sin_port), NULL, TEST_TASK_CLIENT, 0);
    CHECK(s_dwClient != 0);
    DWORD dwStart = GetTickCount();
    while (GetTickCount() - dwStart < 3000 && !s_dwServer)
    {
        EnterCriticalSection(&s_cs);
        for (std::deque<PACKET>::iterator it = s_queIn.begin(); it != s_queIn.end(); ++it)
        {
            if (it->dwTask == (DWORD)s_pServerJob && it->wOpcode == 0x1001
                && it->data.size() >= 4)
            {
                s_dwServer = *(const DWORD*)&it->data[0];
            }
        }
        LeaveCriticalSection(&s_cs);
        Sleep(5);
    }
    CHECK(s_dwServer != 0 && s_dwServer != s_dwClient);
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
    g_pDefaultDBSession = DEFAULT_DB;
    g_pAccountDBSession = ACCOUNT_DB;
    g_pSilkDBSession = SILK_DB;

    // The main process as the main task's job would be after Initialize (without the timer
    // queue), with the recording billing manager.
    s_pMain = new CTestMain;
    s_pMain->m_pOwner = s_pTask;
    s_pMain->m_OverlapJobManager.SetOwner(s_pTask);
    g_pGlobalManagerProcess = s_pMain;
    s_pBilling = new CFakeBilling;
    s_pMain->m_pBillingMgr = s_pBilling;
    // The slave's task is the client task: what is posted to it arrives there.
    CGlobalManagerSlave* pSlave = new CGlobalManagerSlave;
    pSlave->m_pOwner = s_pClientTask;
    LogCapture_Start();
}

static void RegisterUser(DWORD dwJID, DWORD dwIP, WORD wShardID, WORD wAgent, const char* pszID)
{
    sContentUser user;
    user.dwIP = dwIP;
    user.dwJID = dwJID;
    user.strID = pszID;
    user.wShardID = wShardID;
    user.wAgentBodyID = wAgent;
    user.PlayTime.Clear();
    CHECK(g_ContentUserManager.Register(&user) == 0);
}

static COverlapJob_Local* TakeJob()
{
    if (s_pMain->m_vecJobs.empty())
    {
        printf("  no job\n");
        return NULL;
    }
    COverlapJob_Local* p = s_pMain->m_vecJobs.front();
    CHECK(s_pMain->m_vecJobTasks.front() == NULL);
    s_pMain->m_vecJobs.erase(s_pMain->m_vecJobs.begin());
    s_pMain->m_vecJobTasks.erase(s_pMain->m_vecJobTasks.begin());
    return p;
}

// Returns the jobs posted so far (the play time jobs of the logouts) to their pools.
static void DropJobs()
{
    for (size_t i = 0; i < s_pMain->m_vecJobs.size(); ++i)
        s_pMain->m_vecJobs[i]->ReturnToPool();
    s_pMain->m_vecJobs.clear();
    s_pMain->m_vecJobTasks.clear();
}

// ---------------------------------------------------------------------------------------------
// Layout, handlers and timers registered by InitMsgHandlers

static void TestInitMsgHandlers()
{
    CHECK(sizeof(CGlobalManagerMainProcess) == 0x80268);
    CHECK(CGlobalManagerMainProcess::classCGlobalManagerMainProcess.m_nObjectSize == 0x80268);
    s_pMain->InitMsgHandlers();
    CHECK(s_pMain->m_aHandler[0x6201] == static_cast<PFN_MSG_HANDLER>(
                                             &CGlobalManagerMainProcess::OnMsg_6201));
    CHECK(s_pMain->m_aHandler[0x2327] == static_cast<PFN_MSG_HANDLER>(
                                             &CGlobalManagerMainProcess::OnMsg_2327));
    CHECK(s_pMain->m_aHandler[0xA31C] == static_cast<PFN_MSG_HANDLER>(
                                             &CGlobalManagerMainProcess::OnMsg_A31C));
    CHECK(s_pMain->m_aHandler[0xAA00] == static_cast<PFN_MSG_HANDLER>(
                                             &CGlobalManagerMainProcess::OnMsg_AA00));
    CHECK(s_pMain->m_aHandler[0x2001] == static_cast<PFN_MSG_HANDLER>(
                                             &CServerProcessMain::OnMsg_2001));
    CHECK(s_pMain->m_SM.m_pTask == s_pTask);
    static const BYTE s_abTimer[] = { 0x1E, 0x1F, 0x20, 0x21, 0x23, 0x28 };
    static const DWORD s_adwPeriod[] = { 1000, 5000, 10000, 300000, 0, 1000 };
    for (int i = 0; i < 6; ++i)
    {
        ServerProcessTimerMap::iterator it = s_pMain->m_mapTimer.find(s_abTimer[i]);
        CHECK(it != s_pMain->m_mapTimer.end());
        if (it != s_pMain->m_mapTimer.end() && s_adwPeriod[i])
            CHECK(it->second.dwPeriod == s_adwPeriod[i]);
    }
    // 0x23: the time to midnight.
    CDateTime dtNow = CDateTime::GetCurrent();
    DWORD dwToMidnight = 1000 * (86400 - (dtNow.GetHour() * 3600 + dtNow.GetMinute() * 60
                                          + dtNow.GetSecond()));
    DWORD dwPeriod = s_pMain->m_mapTimer[0x23].dwPeriod;
    CHECK(dwPeriod <= dwToMidnight + 2000 && dwPeriod + 2000 >= dwToMidnight);
    CHECK(s_pMain->m_mapTimer.find(0x22) == s_pMain->m_mapTimer.end());
    DWORD dwTick = GetTickCount();
    CHECK(dwTick - s_pMain->m_dwCashTick < 1000 && dwTick - s_pMain->m_dwShardStatusTick < 1000);
    // The rescheduling changes the period of the existing entry.
    s_pMain->m_mapTimer[0x23].dwPeriod = 5;
    s_pMain->SchedulePlayTimeReset(TRUE);
    CHECK(s_pMain->m_mapTimer[0x23].dwPeriod > 1000);
}

// ---------------------------------------------------------------------------------------------
// Content users

static Bytes LoginData(DWORD dwIP, DWORD dwJID, const char* pszID, WORD wPlayTime, DWORD dwTime)
{
    Bytes v;
    PutD(v, dwIP);
    PutD(v, dwJID);
    PutS(v, pszID);
    PutB(v, 22);
    PutW(v, 10);
    PutW(v, 5);
    BYTE abMAC[6] = { 0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0xEF };
    Put(v, abMAC, 6);
    PutW(v, wPlayTime);
    PutD(v, dwTime);
    return v;
}

static void TestLogin()
{
    DWORD dwNow = CDateTime::GetCurrent().GetTime();
    // Billing answers 1: {JID, 1, IP type, play time of today}.
    s_pBilling->m_nResult = 1;
    CMsg* p = Req(0x6201, LoginData(0x0100000A, 100, "alpha", 77, dwNow));
    s_pMain->OnMsg_6201(p, 31, 0, NULL);
    CHECK(p->GetUnreadSize() == 0);
    Done(p);
    Bytes want;
    PutD(want, 100);
    PutB(want, 1);
    PutB(want, 4);
    PutW(want, 77);
    CHECK(RelayReply(31, 0xA201, want));
    Bytes bill;
    PutW(bill, 0x6201);
    PutD(bill, 0x0100000A);
    PutD(bill, 100);
    PutS(bill, "alpha");
    PutB(bill, 22);
    PutW(bill, 10);
    PutW(bill, 5);
    PutS(bill, "00-1A-2B-3C-4D-EF");
    CHECK(s_pBilling->m_vecMsg.size() == 1 && Same(s_pBilling->m_vecMsg[0], bill));
    CHECK(s_pBilling->m_vecSession[0] == s_dwServer && s_pBilling->m_vecRelay[0] == 31);
    sContentUser* pUser = g_ContentUserManager.Find(100);
    CHECK(pUser && pUser->strID == "alpha" && pUser->strMAC == "00-1A-2B-3C-4D-EF");
    CHECK(pUser && pUser->dwSessionID == s_dwServer && pUser->dwRelayID == 31);
    CHECK(pUser && pUser->PlayTime.wPlayTime == 77 && pUser->PlayTime.dwTime == 0);
    CHECK(pUser && pUser->byContentID == 22 && pUser->wShardID == 10 && pUser->wAgentBodyID == 5);

    // Again: duplicate, {JID, 2, 1}, nothing for billing.
    p = Req(0x6201, LoginData(0x0100000A, 100, "alpha", 77, dwNow));
    s_pMain->OnMsg_6201(p, 32, 0, NULL);
    Done(p);
    want.clear();
    PutD(want, 100);
    PutB(want, 2);
    PutB(want, 1);
    CHECK(RelayReply(32, 0xA201, want));
    CHECK(s_pBilling->m_vecMsg.size() == 1);

    // A play time of another day restarts the count; billing 0: {JID, 2, 6}.
    s_pBilling->m_nResult = 0;
    p = Req(0x6201, LoginData(0x0200000A, 101, "beta", 50, dwNow - 3 * 86400));
    s_pMain->OnMsg_6201(p, 33, 0, NULL);
    Done(p);
    want.clear();
    PutD(want, 101);
    PutB(want, 2);
    PutB(want, 6);
    CHECK(RelayReply(33, 0xA201, want));
    pUser = g_ContentUserManager.Find(101);
    CHECK(pUser && pUser->PlayTime.wPlayTime == 0 && pUser->PlayTime.dwTime >= dwNow);

    // Billing 2 takes the answer.
    s_pBilling->m_nResult = 2;
    p = Req(0x6201, LoginData(0x0300000A, 102, "gamma", 1, dwNow));
    s_pMain->OnMsg_6201(p, 34, 0, NULL);
    Done(p);
    CHECK(s_pBilling->m_vecMsg.size() == 3);
    CHECK(NoPackets());

    // 0x2320 restarts the count of the user.
    Bytes v;
    PutD(v, 100);
    p = Req(0x2320, v);
    s_pMain->OnMsg_2320(p, 0, 0, NULL);
    Done(p);
    CHECK(g_ContentUserManager.Find(100)->PlayTime.dwTime >= dwNow);
    v.clear();
    PutD(v, 999);
    p = Req(0x2320, v);
    s_pMain->OnMsg_2320(p, 0, 0, NULL);
    Done(p);
    CHECK(NoPackets(50));
}

static void TestCheck()
{
    Bytes v;
    PutD(v, 0x0100000A);
    PutD(v, 200);
    PutB(v, 9);
    // Billing not ready: {JID, 2, 6}.
    s_pBilling->m_bReady = FALSE;
    CMsg* p = Req(0x6200, v);
    s_pMain->OnMsg_6200(p, 41, 0, NULL);
    CHECK(p->GetUnreadSize() == 0);
    Done(p);
    Bytes want;
    PutD(want, 200);
    PutB(want, 2);
    PutB(want, 6);
    CHECK(RelayReply(41, 0xA200, want));
    s_pBilling->m_bReady = TRUE;
    p = Req(0x6200, v);
    s_pMain->OnMsg_6200(p, 42, 0, NULL);
    Done(p);
    want.clear();
    PutD(want, 200);
    PutB(want, 1);
    CHECK(RelayReply(42, 0xA200, want));
    // A logged-in JID: {JID, 2, 1}.
    v.clear();
    PutD(v, 0x0100000A);
    PutD(v, 100);
    PutB(v, 0);
    p = Req(0x6200, v);
    s_pMain->OnMsg_6200(p, 43, 0, NULL);
    Done(p);
    want.clear();
    PutD(want, 100);
    PutB(want, 2);
    PutB(want, 1);
    CHECK(RelayReply(43, 0xA200, want));
}

static void TestLogout()
{
    s_pBilling->Clear();
    s_pBilling->m_nResult = 1;
    // One JID: 0x6202 {1, JID} to billing when it was registered.
    Bytes v;
    PutW(v, 1);
    PutD(v, 101);
    CMassiveMsg* pMassive = MassiveReq(0x6202, v);
    s_pMain->OnMsg_6202(NULL, 51, 0, pMassive);
    pMassive->Free();
    Bytes want;
    PutB(want, 1);
    PutD(want, 51);
    PutW(want, 0xA202);
    PutW(want, 1);
    PutD(want, 101);
    CHECK(Massive(SERVER_MSG_RELAY_ACK, want));
    CHECK(g_ContentUserManager.Find(101) == NULL);
    Bytes bill;
    PutW(bill, 0x6202);
    PutB(bill, 1);
    PutD(bill, 101);
    CHECK(s_pBilling->m_vecMsg.size() == 1 && Same(s_pBilling->m_vecMsg[0], bill));
    CHECK(s_pBilling->m_vecSession[0] == 0 && s_pBilling->m_vecRelay[0] == 0);
    // An unknown single JID: nothing for billing.
    s_pBilling->Clear();
    pMassive = MassiveReq(0x6202, v);
    s_pMain->OnMsg_6202(NULL, 52, 0, pMassive);
    pMassive->Free();
    want[1] = 52;
    CHECK(Massive(SERVER_MSG_RELAY_ACK, want));
    CHECK(s_pBilling->m_vecMsg.empty());

    // 1002 JIDs: parts of 1000 and 2; every JID goes to billing, the reply has them all.
    RegisterUser(300, 0x0500000A, 10, 5, "u300");
    v.clear();
    PutW(v, 1002);
    for (DWORD i = 0; i < 1002; ++i)
        PutD(v, 300 + i);
    pMassive = MassiveReq(0x6202, v);
    s_pMain->OnMsg_6202(NULL, 53, 0, pMassive);
    pMassive->Free();
    want.clear();
    PutB(want, 1);
    PutD(want, 53);
    PutW(want, 0xA202);
    want.insert(want.end(), v.begin(), v.end());
    CHECK(Massive(SERVER_MSG_RELAY_ACK, want));
    CHECK(s_pBilling->m_vecMsg.size() == 2);
    if (s_pBilling->m_vecMsg.size() == 2)
    {
        Bytes part;
        PutW(part, 0x6202);
        PutB(part, 2);
        PutW(part, 1000);
        part.insert(part.end(), v.begin() + 2, v.begin() + 2 + 4000);
        CHECK(Same(s_pBilling->m_vecMsg[0], part));
        part.clear();
        PutW(part, 0x6202);
        PutB(part, 2);
        PutW(part, 2);
        part.insert(part.end(), v.begin() + 4002, v.end());
        CHECK(Same(s_pBilling->m_vecMsg[1], part));
    }
    CHECK(g_ContentUserManager.Find(300) == NULL);
    // A count of 0: one empty part.
    s_pBilling->Clear();
    v.clear();
    PutW(v, 0);
    pMassive = MassiveReq(0x6202, v);
    s_pMain->OnMsg_6202(NULL, 54, 0, pMassive);
    pMassive->Free();
    want.clear();
    PutB(want, 1);
    PutD(want, 54);
    PutW(want, 0xA202);
    PutW(want, 0);
    CHECK(Massive(SERVER_MSG_RELAY_ACK, want));
    bill.clear();
    PutW(bill, 0x6202);
    PutB(bill, 2);
    PutW(bill, 0);
    CHECK(s_pBilling->m_vecMsg.size() == 1 && Same(s_pBilling->m_vecMsg[0], bill));
}

static Bytes ShardUser(DWORD dwIP, DWORD dwJID, const char* pszID, WORD wAgent, WORD wPlay)
{
    Bytes v;
    PutD(v, dwIP);
    PutD(v, dwJID);
    PutS(v, pszID);
    PutW(v, wAgent);
    BYTE abMAC[6] = { 1, 2, 3, 4, 5, 6 };
    Put(v, abMAC, 6);
    PutW(v, wPlay);
    return v;
}

static void TestShardUsers()
{
    s_pBilling->Clear();
    ClearLogs();
    DWORD dwNow = CDateTime::GetCurrent().GetTime();
    Bytes v;
    PutW(v, 10);
    PutW(v, 2);
    Bytes u = ShardUser(0x0600000A, 400, "s400", 5, 11);
    v.insert(v.end(), u.begin(), u.end());
    u = ShardUser(0x0700000A, 401, "s401", 6, 12);
    v.insert(v.end(), u.begin(), u.end());
    CMassiveMsg* pMassive = MassiveReq(0x620C, v);
    s_pMain->OnMsg_620C(NULL, 61, 0, pMassive);
    pMassive->Free();
    CHECK(RelayReply(61, 0xA20C, Bytes()));
    CHECK(s_pBilling->m_vecMsg.size() == 1 && s_pBilling->m_vecMsg[0].size() == 2
          && s_pBilling->m_vecMsg[0][0] == 0x0C && s_pBilling->m_vecMsg[0][1] == 0x62);
    sContentUser* pUser = g_ContentUserManager.Find(401);
    CHECK(pUser && pUser->strID == "s401" && pUser->wAgentBodyID == 6 && pUser->wShardID == 10);
    CHECK(pUser && pUser->byContentID == 0x21 && pUser->strMAC == "01-02-03-04-05-06");
    CHECK(pUser && pUser->PlayTime.wPlayTime == 12 && pUser->PlayTime.dwTime >= dwNow);
    CHECK(Logged("shard id(10) count(2)"));
    CHECK(Logged("concurrent shard user 2 added : shard id Shard10"));
    // An unknown shard: logged with the source path, answered, nothing registered.
    v.clear();
    PutW(v, 99);
    PutW(v, 1);
    u = ShardUser(0x0800000A, 402, "s402", 5, 1);
    v.insert(v.end(), u.begin(), u.end());
    pMassive = MassiveReq(0x620C, v);
    s_pMain->OnMsg_620C(NULL, 62, 0, pMassive);
    pMassive->Free();
    CHECK(RelayReply(62, 0xA20C, Bytes()));
    CHECK(g_ContentUserManager.Find(402) == NULL && s_pBilling->m_vecMsg.size() == 1);
    CHECK(Logged("shard id(99) count(1)"));
    CHECK(Logged("\xbe\xc6\xbb\xd4\xbd\xce D:\\WORK2005\\Source\\JMX_CommonServers\\GlobalManager"
                 "\\GlobalManager.cpp(1597)"));
    // GM-027: the billing message of the unknown shard is not released (here: one message).
}

// ---------------------------------------------------------------------------------------------
// Shard requests and jobs

static void TestShardRequests()
{
    DropJobs();
    // 0x620B: a CheckCharName job with the session and relay.
    Bytes v;
    PutB(v, 1);
    PutB(v, 2);
    PutB(v, 3);
    PutS(v, "Name");
    CMsg* p = Req(0x620B, v);
    s_pMain->OnMsg_620B(p, 71, 0, NULL);
    Done(p);
    COverlapJob_CheckCharName* pCheck = dynamic_cast<COverlapJob_CheckCharName*>(TakeJob());
    CHECK(pCheck && pCheck->m_strName == "Name" && pCheck->m_dwSessionID == s_dwServer
          && pCheck->m_dwRelayID == 71 && pCheck->m_pOwner == s_pTask);
    if (pCheck)
        pCheck->ReturnToPool();

    // 0x620E / 0x620F / 0x6210: known shard -> job of the type; unknown shard or an empty name
    // -> {2}.
    static const WORD s_awReq[] = { 0x620E, 0x620F, 0x6210 };
    for (BYTE byType = 0; byType < 3; ++byType)
    {
        v.clear();
        PutW(v, 11);
        PutD(v, 500);
        PutS(v, "Char");
        if (byType == 2)
            PutS(v, "Char2");
        p = Req(s_awReq[byType], v);
        if (byType == 0)
            s_pMain->OnMsg_620E(p, 72, 0, NULL);
        else if (byType == 1)
            s_pMain->OnMsg_620F(p, 72, 0, NULL);
        else
            s_pMain->OnMsg_6210(p, 72, 0, NULL);
        Done(p);
        COverlapJob_ManageShardCharacterData* pJob =
            dynamic_cast<COverlapJob_ManageShardCharacterData*>(TakeJob());
        CHECK(pJob && pJob->m_byType == byType && pJob->m_wShardID == 11
              && pJob->m_dwRelayID == 72 && pJob->m_strName == "Char");
        if (pJob)
            pJob->ReturnToPool();

        v.clear();
        PutW(v, 99);
        p = Req(s_awReq[byType], v);
        if (byType == 0)
            s_pMain->OnMsg_620E(p, 73, 0, NULL);
        else if (byType == 1)
            s_pMain->OnMsg_620F(p, 73, 0, NULL);
        else
            s_pMain->OnMsg_6210(p, 73, 0, NULL);
        Done(p);
        Bytes want;
        PutB(want, 2);
        CHECK(RelayReply(73, (WORD)((s_awReq[byType] & 0x0FFF) | 0xA000), want));

        v.clear();
        PutW(v, 10);
        PutD(v, 500);
        PutS(v, "");
        p = Req(s_awReq[byType], v);
        if (byType == 0)
            s_pMain->OnMsg_620E(p, 74, 0, NULL);
        else if (byType == 1)
            s_pMain->OnMsg_620F(p, 74, 0, NULL);
        else
            s_pMain->OnMsg_6210(p, 74, 0, NULL);
        Done(p);
        CHECK(RelayReply(74, (WORD)((s_awReq[byType] & 0x0FFF) | 0xA000), want));
        CHECK(s_pMain->m_vecJobs.empty());
    }
    CHECK(g_ManageShardCharacterDataJobPool.m_nInUse == 0);

    // 0x6306: only in state 4 or higher.
    p = Req(0x6306, Bytes());
    g_pSelfBody->nState = 3;
    s_pMain->OnMsg_6306(p, 0, 0, NULL);
    CHECK(s_pMain->m_vecJobs.empty());
    g_pSelfBody->nState = 4;
    s_pMain->OnMsg_6306(p, 0, 0, NULL);
    Done(p);
    COverlapJob_ModuleVersionData* pVersion =
        dynamic_cast<COverlapJob_ModuleVersionData*>(TakeJob());
    CHECK(pVersion && pVersion->m_pOwner == s_pTask);
    if (pVersion)
        pVersion->ReturnToPool();
    CHECK(NoPackets(50));
}

static void TestValidation()
{
    // 0x6111: failures of the login check (tolerance 2 here).
    g_pUserValidationHistoryMgr->SetConfig(0, "G", "D", 2, 10);
    g_pUserValidationHistoryMgr->SetPunisher(&s_pMain->m_SM, "Punisher");
    Bytes v;
    PutS(v, "acc");
    PutB(v, 0);
    CMsg* p = Req(0x6111, v);
    s_pMain->OnMsg_6111(p, 81, 0, NULL);
    Done(p);
    Bytes want;
    PutS(want, "acc");
    PutB(want, 0);
    PutB(want, 0);
    PutD(want, 2);
    PutD(want, 1);
    CHECK(RelayReply(81, 0xA111, want));
    // The second failure punishes (the automated punishment job is posted) and clears.
    p = Req(0x6111, v);
    s_pMain->OnMsg_6111(p, 82, 0, NULL);
    Done(p);
    want[want.size() - 9] = 1;
    want[want.size() - 4] = 2;
    CHECK(RelayReply(82, 0xA111, want));
    CHECK(s_pServerJob->m_vecJobs.size() == 1);
    if (!s_pServerJob->m_vecJobs.empty())
        s_pServerJob->m_vecJobs.back()->ReturnToPool();
    s_pServerJob->m_vecJobs.clear();
    // Check 2 uses the tolerance of the MOTP check; an unknown check asserts, 0 / 0.
    g_pUserValidationHistoryMgr->SetConfig(2, "G", "D", 7, 10);
    v.clear();
    PutS(v, "acc2");
    PutB(v, 2);
    p = Req(0x6111, v);
    s_pMain->OnMsg_6111(p, 83, 0, NULL);
    Done(p);
    want.clear();
    PutS(want, "acc2");
    PutB(want, 2);
    PutB(want, 0);
    PutD(want, 7);
    PutD(want, 1);
    CHECK(RelayReply(83, 0xA111, want));
    v.clear();
    PutS(v, "acc3");
    PutB(v, 5);
    p = Req(0x6111, v);
    s_pMain->OnMsg_6111(p, 84, 0, NULL);
    Done(p);
    want.clear();
    PutS(want, "acc3");
    PutB(want, 5);
    PutB(want, 0);
    PutD(want, 0);
    PutD(want, 0);
    CHECK(RelayReply(84, 0xA111, want));
    // 0x6112 clears the account of every check.
    v.clear();
    PutS(v, "acc2");
    p = Req(0x6112, v);
    s_pMain->OnMsg_6112(p, 85, 0, NULL);
    Done(p);
    CHECK(RelayReply(85, 0xA112, v));
    CHECK(g_pUserValidationHistoryMgr->m_aMap[2].empty());
}

static void TestBilling()
{
    s_pBilling->Clear();
    Bytes v;
    PutD(v, 100);
    static const int s_anResult[] = { 0, 1, 2, 5 };
    for (int i = 0; i < 4; ++i)
    {
        s_pBilling->m_nResult = s_anResult[i];
        CMsg* p = Req(0x6900, v);
        s_pMain->OnMsg_6900(p, 91 + i, 0, NULL);
        Done(p);
        if (s_anResult[i] != 2)
        {
            Bytes want;
            PutD(want, 100);
            PutB(want, s_anResult[i] == 1 ? 1 : 2);
            CHECK(RelayReply(91 + i, 0xA900, want));
        }
    }
    CHECK(s_pBilling->m_vecMsg.size() == 4);
    Bytes bill;
    PutW(bill, 0x6900);
    PutD(bill, 100);
    CHECK(Same(s_pBilling->m_vecMsg[0], bill) && s_pBilling->m_vecRelay[3] == 94);
    sContentUser* pUser = g_ContentUserManager.Find(100);
    CHECK(pUser && pUser->dwRelayID == 94 && pUser->dwSessionID == s_dwServer);
    CHECK(NoPackets(50));

    // 0x6904: the message itself goes to billing; {1} when it was taken, {2} otherwise.
    Bytes v2;
    PutD(v2, 7);
    PutB(v2, 8);
    for (int i = 0; i < 2; ++i)
    {
        s_pBilling->m_nResult = i;
        CMsg* p = Req(0x6904, v2);
        s_pMain->OnMsg_6904(p, 95, 0, NULL);
        Done(p);
        Bytes want;
        PutB(want, i ? 1 : 2);
        CHECK(RelayReply(95, 0xA904, want));
    }
    bill.clear();
    PutW(bill, 0x6904);
    bill.insert(bill.end(), v2.begin(), v2.end());
    CHECK(Same(s_pBilling->m_vecMsg.back(), bill));

    // The answers 0xA902 / 0xA903 / 0xA317 are skipped, 0xA905 {2, JID} logs the user out.
    CMsg* p = Req(0xA902, v2);
    s_pMain->OnMsg_A902(p, 0, 0, NULL);
    CHECK(p->GetUnreadSize() == 0);
    Done(p);
    p = Req(0xA903, v2);
    s_pMain->OnMsg_A903(p, 0, 0, NULL);
    CHECK(p->GetUnreadSize() == 0);
    Done(p);
    p = Req(0xA317, v2);
    s_pMain->OnMsg_A317(p, 0, 0, NULL);
    CHECK(p->GetUnreadSize() == 0);
    Done(p);
    Bytes v3;
    PutB(v3, 1);
    PutD(v3, 100);
    p = Req(0xA905, v3);
    s_pMain->OnMsg_A905(p, 0, 0, NULL);
    CHECK(p->GetUnreadSize() == 4);
    Done(p);
    CHECK(g_ContentUserManager.Find(100) != NULL);
    v3[0] = 2;
    p = Req(0xA905, v3);
    s_pMain->OnMsg_A905(p, 0, 0, NULL);
    Done(p);
    CHECK(g_ContentUserManager.Find(100) == NULL);
    // The empty handlers and slot 72 (one byte).
    p = Req(0xAA00, v2);
    s_pMain->OnMsg_AA00(p, 0, 0, NULL);
    s_pMain->OnMsg_6901(p, 0, 0, NULL);
    CHECK(p->GetUnreadSize() == 5);
    s_pMain->OnMsg_SkipByte(p, 0, 0, NULL);
    CHECK(p->GetUnreadSize() == 4);
    Done(p);
    CHECK(NoPackets(50));
}

static void TestPunishmentAndSlave()
{
    // 0x6318: JID and the 0x110 bytes of the active punishments (none: zeros).
    Bytes v;
    PutD(v, 700);
    CMsg* p = Req(0x6318, v);
    s_pMain->OnMsg_6318(p, 101, 0, NULL);
    Done(p);
    Bytes want(v);
    want.resize(4 + 0x110, 0);
    CHECK(RelayReply(101, 0xA318, want));

    // 0x631A {1, body, 14 bytes}: 0x6319 {1, body, 14 bytes} to the slave's task; other values
    // nothing.
    v.clear();
    PutD(v, 1);
    PutW(v, 0x1234);
    for (BYTE i = 0; i < 14; ++i)
        PutB(v, (BYTE)(0xA0 + i));
    p = Req(0x631A, v);
    s_pMain->OnMsg_631A(p, 0, 0, NULL);
    Done(p);
    Bytes got;
    CHECK(TakePacket(0xFFFFFFFF, 0x6319, &got));
    want.clear();
    PutB(want, 1);
    want.insert(want.end(), v.begin() + 4, v.end());
    CHECK(Same(got, want));
    v[0] = 2;
    p = Req(0x631A, v);
    s_pMain->OnMsg_631A(p, 0, 0, NULL);
    CHECK(p->GetUnreadSize() == 0);
    Done(p);
    // 0xA31C {string}: 0x6319 {2, 0x10, string}.
    v.clear();
    PutS(v, "123456");
    p = Req(0xA31C, v);
    s_pMain->OnMsg_A31C(p, 0, 0, NULL);
    Done(p);
    CHECK(TakePacket(0xFFFFFFFF, 0x6319, &got));
    want.clear();
    PutB(want, 2);
    PutB(want, 0x10);
    PutS(want, "123456");
    CHECK(Same(got, want));
    CHECK(NoPackets(50));
}

static void TestBroadcast()
{
    // 0x2327 {WORD opcode, data}: the data with that opcode to the manager of every shard.
    Bytes v;
    PutW(v, 0x3456);
    PutD(v, 0xCAFEBABE);
    PutB(v, 9);
    CMsg* p = Req(0x2327, v);
    s_pMain->OnMsg_2327(p, 0, 0, NULL);
    Done(p);
    Bytes data(v.begin() + 2, v.end());
    CHECK(Relayed(7, 0x3456, data));
    CHECK(Relayed(7, 0x3456, data));
    CHECK(NoPackets(50));
    s_pMain->m_OverlapJobManager.ReleaseAll();
}

// ---------------------------------------------------------------------------------------------
// Process events

static void TestEvents()
{
    DropJobs();
    ClearLogs();
    // vf09: an SMC opcode goes to the service manager; others are discarded by the base.
    CMsg* p = Req(0xA312, Bytes(3, 1));
    s_pMain->OnUnhandledMsg(p, 0, 0, NULL);
    Done(p);
    // (a request opcode: the service manager answers the others for an SMC)
    p = Req(0x2345, Bytes(3, 1));
    s_pMain->OnUnhandledMsg(p, 0, 0, NULL);
    CHECK(p->GetUnreadSize() == 0);
    Done(p);
    ClearLogs();

    // vf17: the billing and punishment relays are ignored, the IBUV answers logged.
    s_pMain->OnRelayFailed(0x6A00, 0);
    s_pMain->OnRelayFailed(0x6318, 0);
    s_pMain->OnRelayFailed(0xA902, 0);
    s_pMain->OnRelayFailed(0xA111, 0);
    CHECK(Logged("IBUV Ack[41233] Relay failed!"));
    s_pMain->OnRelayFailed(0xA112, 0);
    CHECK(Logged("IBUV Ack[41234] Relay failed!"));
    CollectLogs();
    CHECK(s_vecLog.empty());

    // vf41: both cords of body 1 are up -> state 5; the FarmManager peer starts the play time
    // reset when the last one was not today.
    DropJobs();
    s_pMain->m_dwLastPlayTimeReset = CDateTime::GetCurrent().GetTime() - 2 * 86400;
    CHECK(s_pMain->IsPlayTimeResetDue());
    g_pSelfBody->nState = 4;
    s_pMain->OnCordEstablished(&s_aCord[1], &s_aBody[4]);
    CHECK(g_pSelfBody->nState == 5);
    // Checked for each connected cord: the first reset makes the second check false.
    CHECK(!s_pMain->IsPlayTimeResetDue());
    COverlapJob_ResetPlayTimeOfGlobalServer* pReset =
        dynamic_cast<COverlapJob_ResetPlayTimeOfGlobalServer*>(TakeJob());
    char szUpdate[80];
    sprintf_s(szUpdate, "update _WriteOutResetPlayTime set LatestResetTime = %d",
              s_pMain->m_dwLastPlayTimeReset);
    CHECK(pReset && pReset->m_nType == 1 && pReset->m_strQuery == szUpdate);
    CHECK(s_pMain->m_vecJobs.empty());
    if (pReset)
        pReset->ReturnToPool();
    CDateTime dtNow = CDateTime::GetCurrent();
    WORD wElapsed = (WORD)(dtNow.GetHour() * 3600 + dtNow.GetMinute() * 60 + dtNow.GetSecond());
    Bytes got;
    for (int i = 0; i < 2; ++i)
    {
        CHECK(TakePacket(s_dwClient, SERVER_MSG_RELAY_REQ, &got));
        CHECK(got.size() == 10 && *(WORD*)&got[4] == 7 && *(WORD*)&got[6] == 0x231F
              && *(WORD*)&got[8] - wElapsed <= 2);
    }
    // A peer that is not the FarmManager, one cord down -> state 4, no reset.
    s_pMain->m_dwLastPlayTimeReset -= 2 * 86400;
    s_aCord[0].dwStatus = 1;
    s_pMain->OnCordEstablished(&s_aCord[1], &s_aBody[4]);
    CHECK(g_pSelfBody->nState == 4 && s_pMain->m_vecJobs.empty());
    s_aCord[0].dwStatus = 2;
    s_pMain->OnCordEstablished(&s_aCord[0], &s_aBody[3]);
    CHECK(g_pSelfBody->nState == 5 && s_pMain->m_vecJobs.empty());
    // vf42: the state again.
    s_aCord[1].dwStatus = 0;
    s_pMain->OnCordClosed(&s_aCord[1], &s_aBody[4]);
    CHECK(g_pSelfBody->nState == 4);
    s_aCord[1].dwStatus = 3;
    s_pMain->OnCordClosed(&s_aCord[1], &s_aBody[4]);
    CHECK(g_pSelfBody->nState == 5);
    CHECK(NoPackets(50));
    s_pMain->m_OverlapJobManager.ReleaseAll();

    // vf43: an AgentServer leaving state 5 loses its users.
    g_ContentUserManager.UnregisterByAgent(5);
    RegisterUser(800, 0x0900000A, 10, 5, "a800");
    RegisterUser(801, 0x0900000B, 10, 5, "a801");
    RegisterUser(802, 0x0900000C, 10, 2, "a802");
    ClearLogs();
    s_aBody[2].nState = 4;
    s_pMain->OnServerBodyStateChanged(&s_aBody[2], 4);
    CHECK(g_ContentUserManager.Find(800) != NULL);
    s_pMain->OnServerBodyStateChanged(&s_aBody[2], 5);
    CHECK(g_ContentUserManager.Find(800) == NULL && g_ContentUserManager.Find(801) == NULL);
    CHECK(g_ContentUserManager.Find(802) != NULL);
    CHECK(Logged("agent server is broken !!. all associated user is unregistered : 2"));
    s_aBody[1].nState = 4;
    s_pMain->OnServerBodyStateChanged(&s_aBody[1], 5);    // a GatewayServer
    CHECK(g_ContentUserManager.Find(802) != NULL);
    s_aBody[1].nState = 5;
    s_aBody[2].nState = 5;
    // Session and file events go to the service manager (no SMC, no patch: nothing).
    s_pMain->OnSessionClosed(1234);
    s_pMain->OnFileFailed(1234);
    CHECK(NoPackets(50));
}

// ---------------------------------------------------------------------------------------------
// Timers

static void TestTimers()
{
    DropJobs();
    // Nothing without the architecture.
    SERVER_BODY* pSelf = g_pSelfBody;
    g_pSelfBody = NULL;
    s_pMain->m_dwCashTick = GetTickCount() - 6000;
    CHECK(s_pMain->OnCashTimer(0x1F) && s_pMain->OnShardStatusTimer(0x20)
          && s_pMain->OnShardUserTimer(0x21) && s_pMain->OnShardUserCountTimer(0x1E)
          && s_pMain->OnPunishmentTimer(0x22) && s_pMain->OnValidationDayTimer(0x28));
    CHECK(s_pMain->m_vecJobs.empty());
    g_pSelfBody = pSelf;

    // 5 s / 10 s: only when the time since the last job passed.
    s_pMain->m_dwCashTick = GetTickCount();
    CHECK(s_pMain->OnCashTimer(0x1F));
    CHECK(s_pMain->m_vecJobs.empty());
    s_pMain->m_dwCashTick = GetTickCount() - 5000;
    CHECK(s_pMain->OnCashTimer(0x1F));
    CHECK(GetTickCount() - s_pMain->m_dwCashTick < 1000);
    COverlapJob_RefreshCommercialUser_Cash* pCash =
        dynamic_cast<COverlapJob_RefreshCommercialUser_Cash*>(TakeJob());
    CHECK(pCash && pCash->m_pOwner == s_pTask);
    if (pCash)
        pCash->ReturnToPool();
    s_pMain->m_dwShardStatusTick = GetTickCount() - 9000;
    CHECK(s_pMain->OnShardStatusTimer(0x20));
    CHECK(s_pMain->m_vecJobs.empty());
    s_pMain->m_dwShardStatusTick = GetTickCount() - 10000;
    CHECK(s_pMain->OnShardStatusTimer(0x20));
    COverlapJob_RefreshShardStatus* pStatus =
        dynamic_cast<COverlapJob_RefreshShardStatus*>(TakeJob());
    CHECK(pStatus && pStatus->m_pOwner == s_pTask && pStatus->m_listShard.size() == 2);
    if (pStatus)
        pStatus->ReturnToPool();
    CHECK(s_pMain->OnShardUserTimer(0x21));
    COverlapJob_RefreshShardUser* pUser = dynamic_cast<COverlapJob_RefreshShardUser*>(TakeJob());
    CHECK(pUser && pUser->m_pOwner == s_pTask && pUser->m_listShard.size() == 2);
    if (pUser)
        pUser->ReturnToPool();

    // 1 s: the shard user counts to the GatewayServer (body 2).
    CHECK(s_pMain->OnShardUserCountTimer(0x1E));
    Bytes got;
    CHECK(TakePacket(s_dwClient, 0x6105, &got));
    CHECK(got.size() == 9 && got[0] == 2);
    // 60 s and the validation day: the managers run (nothing to do here).
    CHECK(s_pMain->OnPunishmentTimer(0x22));
    CHECK(s_pMain->OnValidationDayTimer(0x28));

    // Midnight: the reset job, the play times restarted, 0x231F to every shard manager.
    RegisterUser(900, 0x0A00000A, 10, 5, "m900");
    g_ContentUserManager.Find(900)->PlayTime.wPlayTime = 99;
    CHECK(s_pMain->OnPlayTimeResetTimer(0x23));
    CHECK(g_ContentUserManager.Find(900)->PlayTime.wPlayTime == 0);
    COverlapJob_ResetPlayTimeOfGlobalServer* pReset =
        dynamic_cast<COverlapJob_ResetPlayTimeOfGlobalServer*>(TakeJob());
    CHECK(pReset && pReset->m_nType == 1);
    if (pReset)
        pReset->ReturnToPool();
    CHECK(TakePacket(s_dwClient, SERVER_MSG_RELAY_REQ, &got));
    CHECK(TakePacket(s_dwClient, SERVER_MSG_RELAY_REQ, &got));
    CHECK(s_pMain->m_mapTimer[0x23].dwPeriod > 0);
    CHECK(NoPackets(50));
    s_pMain->m_OverlapJobManager.ReleaseAll();

    // The monitor graph values.
    MONITOR_COUNTER counter;
    memset(&counter, 0, sizeof(counter));
    counter.dwID = 0;
    CHECK(GlobalManager_ReadContentUserCounter(&counter));
    CHECK(counter.fValue == (float)g_ContentUserManager.m_nUserCount);
    g_ContentUserManager.m_dwRegistered = 6;
    counter.dwID = 1;
    GlobalManager_ReadContentUserCounter(&counter);
    CHECK(counter.fValue == 6.0f && g_ContentUserManager.m_dwRegistered == 0);
    g_ContentUserManager.m_nOccupiedIPCount = 3;
    counter.dwID = 5;
    GlobalManager_ReadContentUserCounter(&counter);
    CHECK(counter.fValue == 3.0f);
    counter.dwID = 9;
    counter.fValue = 1.5f;
    CHECK(GlobalManager_ReadContentUserCounter(&counter) && counter.fValue == 1.5f);
}

// ---------------------------------------------------------------------------------------------
// Billing manager of ./billing.ini

static void TestBillingCreation()
{
    char szOld[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, szOld);
    char szDir[MAX_PATH];
    GetTempPathA(MAX_PATH, szDir);
    char szSub[MAX_PATH];
    sprintf_s(szSub, "%sGlobalManagerMainTest_%u", szDir, GetCurrentProcessId());
    CreateDirectoryA(szSub, NULL);
    SetCurrentDirectoryA(szSub);
    ClearLogs();

    // Without the file: the dummy manager.
    IBillingMgr* pOld = s_pMain->m_pBillingMgr;
    s_pMain->m_pBillingMgr = NULL;
    CHECK(s_pMain->CreateBillingMgr());
    CHECK(dynamic_cast<CDummyBillingMgr*>(s_pMain->m_pBillingMgr) != NULL);
    CHECK(Logged("BILLING SERVER[dummy]"));
    CHECK(Logged("BillingMgr- Created!"));
    s_pMain->DestroyBillingMgr();
    CHECK(s_pMain->m_pBillingMgr == NULL && Logged("BillingMgr- Destroyed!"));
    s_pMain->DestroyBillingMgr();
    CollectLogs();
    CHECK(s_vecLog.empty());
    // Another type is the dummy as well, logged with the address and port.
    WritePrivateProfileStringA("billing", "type", "2", ".\\billing.ini");
    WritePrivateProfileStringA("billing", "ip", "10.1.2.3", ".\\billing.ini");
    WritePrivateProfileStringA("billing", "port", "1234", ".\\billing.ini");
    CHECK(s_pMain->CreateBillingMgr());
    CHECK(dynamic_cast<CDummyBillingMgr*>(s_pMain->m_pBillingMgr) != NULL);
    CHECK(Logged("BILLING SERVER[type: 2, ip: 10.1.2.3, port: 1234]"));
    s_pMain->DestroyBillingMgr();
    DeleteFileA(".\\billing.ini");
    SetCurrentDirectoryA(szOld);
    RemoveDirectoryA(szSub);

    // SignalStop stops and destroys the manager (Stop twice), then the base.
    CFakeBilling* pFake = new CFakeBilling;
    s_pMain->m_pBillingMgr = pFake;
    CFakeBilling::s_nStop = 0;
    CFakeBilling::s_nRelease = 0;
    CFakeBilling::s_nCleanup = 0;
    s_pMain->SignalStop();
    CHECK(CFakeBilling::s_nStop == 2 && CFakeBilling::s_nCleanup == 1
          && CFakeBilling::s_nRelease == 1);
    CHECK(s_pMain->m_pBillingMgr == NULL);
    CHECK(Logged("BillingMgr- Service Terminated!"));
    CHECK(Logged("BillingMgr- Destroyed!"));
    s_pMain->m_pBillingMgr = pOld;
    ClearLogs();
}

// ---------------------------------------------------------------------------------------------
// CGlobalManager

static Bytes SettingRow(DWORD dwValue, const char* pszName)
{
    Bytes v(sizeof(CERT_DATA_SETTING), 0);
    memcpy(&v[0], &dwValue, 4);
    strcpy_s((char*)&v[4], 320, pszName);
    return v;
}

static void ScriptLocalData()
{
    s_deqSession.push_back(DEFAULT_DB);
    s_deqSession.push_back(ACCOUNT_DB);
    s_deqSession.push_back(SILK_DB);
    std::vector<Bytes> rows;
    rows.push_back(SettingRow(5, "Ignore_Shard_MaxUser"));
    AnswerRows(0, rows);                                    // _SecurityDescription
    rows.clear();
    Bytes group(sizeof(CERT_DATA_GROUP), 0);
    group[0] = 4;
    rows.push_back(group);
    AnswerRows(0, rows);                                    // _SecurityDescriptionGroup
    rows.clear();
    Bytes ip;
    PutB(ip, 4);
    PutD(ip, 0x0100007F);
    rows.push_back(ip);
    AnswerRows(0, rows);                                    // _SecurityDescriptionGroupAssign
    Answer(0);                                              // SK_gamebang_ip
    Answer(0);                                              // _QueryActivePunishmet
    Bytes reset;
    PutD(reset, 0x4D000000);
    Answer(0, reset);                                       // _WriteOutResetPlayTime
}

static void TestLocalData()
{
    CGlobalManager& app = g_GlobalManager;
    app.SetDefaultEngineConfig();
    CHECK(app.m_EngineParam.bSecurityMode == 3);

    g_ServerGlobals.mapExtra.clear();
    g_ServerGlobals.mapExtra["MaxUserForNonePCBangIP"] = "4";
    g_ServerGlobals.mapExtra["AccountDBConfig"] = "DSN=Account;UID=u;PWD=;";
    g_ServerGlobals.mapExtra["SilkDBConfig"] = "DSN=Silk";
    g_ServerGlobals.mapExtra["LoginFailureTolerance"] = "6";
    g_ServerGlobals.mapExtra["IBUVPunishmentGuide"] = "IG";
    s_vecDB.clear();
    ClearLogs();
    ScriptLocalData();
    g_pSelfBody->nState = 3;
    g_setNotifyBody.insert(9);
    CHECK(app.OnLocalDataInitialized());
    CHECK(DB("init DSN=Division"));
    CHECK(DB("open (null)"));
    // Not division 2: the configured string as it is.
    CHECK(DB("open DSN=Account;UID=u;PWD=;"));
    CHECK(DB("open DSN=Silk"));
    CHECK(g_pDefaultDBSession == DEFAULT_DB && g_pAccountDBSession == ACCOUNT_DB
          && g_pSilkDBSession == SILK_DB);
    CHECK(g_nMaxUserPerIP == 4);
    CHECK(Query("select * from _SecurityDescription with (NOLOCK)", DEFAULT_DB));
    CHECK(Query("select * from _SecurityDescriptionGroup with (NOLOCK)", DEFAULT_DB));
    CHECK(Query("select * from _SecurityDescriptionGroupAssign with (NOLOCK)", DEFAULT_DB));
    CHECK(Query("select ip, capacity from SK_gamebang_ip", ACCOUNT_DB));
    CHECK(Query("exec _QueryActivePunishmet", ACCOUNT_DB));
    CHECK(Query("select * from _WriteOutResetPlayTime", ACCOUNT_DB));
    CHECK(NoQueries());
    CHECK(CertData_GetIgnoreShardMaxUserValue() == 5);
    CHECK(PrivIPMap_ContainsIP(4, 0x0100007F));
    CHECK(s_pMain->m_mapTimer[0x22].dwPeriod == 60000);
    CHECK(g_pGlobalManagerSlave->m_pMainProcess == s_pMain);
    CHECK(g_pUserValidationHistoryMgr->m_pSM == &s_pMain->m_SM);
    CHECK(g_pUserValidationHistoryMgr->m_strPunisher == "AutomatedPunisher");
    CHECK(g_pUserValidationHistoryMgr->m_aConfig[0].dwTolerance == 6
          && g_pUserValidationHistoryMgr->m_aConfig[0].dwBlockTimeMin == 10
          && g_pUserValidationHistoryMgr->m_aConfig[0].strGuide == "LoginPunishmentGuide"
          && g_pUserValidationHistoryMgr->m_aConfig[0].strDescription
                 == "LoginPunishmentDescription");
    CHECK(g_pUserValidationHistoryMgr->m_aConfig[1].strGuide == "IG");
    CHECK(g_pUserValidationHistoryMgr->m_aConfig[2].strDescription == "MOTPPunishmenDescription"
          && g_pUserValidationHistoryMgr->m_aConfig[2].dwTolerance == 3);
    CHECK(g_pSelfBody->nState == 4);
    CHECK(g_setNotifyBody.find(9) == g_setNotifyBody.end());
    CHECK(s_pMain->m_dwLastPlayTimeReset == 0x4D000000);
    Bytes data;
    PutB(data, 3);
    CHECK(Relayed(9, 0x631C, data));
    // State 4 is broadcast before the parent leaves the notify set: 0x2005 (massive) to it.
    CHECK(TakePacket(s_dwClient, MASSIVE_MSG_OPCODE, NULL));
    CHECK(TakePacket(s_dwClient, MASSIVE_MSG_OPCODE, NULL));
    CHECK(Logged("Successfully DB Initialized!"));
    CHECK(Logged("Max User Count Restriction Per IP: overrided to 4"));
    CHECK(Logged("Account DB Successfully Connected!"));
    CHECK(Logged("account db connected successfully"));
    CHECK(Logged("Silk DB Connected Successfully!!"));
    s_pMain->m_OverlapJobManager.ReleaseAll();

    // Division 2: the build's password goes after "PWD=" (empty here); without "PWD=;" the
    // string is refused.
    s_Division.byID = 2;
    g_ServerGlobals.mapExtra.erase("MaxUserForNonePCBangIP");
    g_ServerGlobals.mapExtra.erase("SilkDBConfig");
    s_vecDB.clear();
    ClearLogs();
    ScriptLocalData();
    s_deqSession.pop_back();
    CHECK(app.OnLocalDataInitialized());
    CHECK(DB("init DSN=Division") && DB("open (null)"));
    CHECK(DB("open DSN=Account;UID=u;PWD=;"));
    CHECK(s_vecDB.empty() && g_pSilkDBSession == ACCOUNT_DB);
    CHECK(Logged("Max User Count Restriction Per IP: system default (infinite)"));
    CHECK(Logged("Silk DB is assigned to Account DB!!"));
    CHECK(s_vecCall.size() == 6 && s_vecCall[3].pSession == ACCOUNT_DB);
    s_vecCall.clear();
    CHECK(s_deqAnswer.empty());
    CHECK(Relayed(9, 0x631C, data));
    s_pMain->m_OverlapJobManager.ReleaseAll();
    g_ServerGlobals.mapExtra["AccountDBConfig"] = "DSN=Account;UID=u";
    s_vecDB.clear();
    ClearLogs();
    s_deqSession.push_back(DEFAULT_DB);
    CHECK(!app.OnLocalDataInitialized());
    CHECK(Logged("invalid account db connection string !!!"));
    CHECK(DB("init DSN=Division") && DB("open (null)") && s_vecDB.empty());
    s_Division.byID = 3;

    // Failures: the environment, the default and account sessions, the security data, the
    // play time reset; without AccountDBConfig the default DB is the account DB.
    s_vecDB.clear();
    s_bInitResult = FALSE;
    CHECK(!app.OnLocalDataInitialized());
    CHECK(Logged("Failed to initialize db"));
    s_bInitResult = TRUE;
    s_deqSession.clear();
    CHECK(!app.OnLocalDataInitialized());
    CHECK(Logged("Can't connect to account DB"));
    s_deqSession.push_back(DEFAULT_DB);
    g_ServerGlobals.mapExtra["AccountDBConfig"] = "DSN=Account";
    CHECK(!app.OnLocalDataInitialized());
    CHECK(Logged("can't connect to global db"));
    g_ServerGlobals.mapExtra.erase("AccountDBConfig");
    s_deqSession.push_back(DEFAULT_DB);
    Answer(-1);
    CHECK(!app.OnLocalDataInitialized());
    CHECK(Logged("security description data loading failed...."));
    CHECK(g_pAccountDBSession == DEFAULT_DB && g_pSilkDBSession == DEFAULT_DB);
    CHECK(Query("select * from _SecurityDescription with (NOLOCK)", DEFAULT_DB));
    CHECK(NoQueries());
    s_deqSession.push_back(DEFAULT_DB);
    AnswerRows(0, std::vector<Bytes>());
    AnswerRows(0, std::vector<Bytes>());
    AnswerRows(0, std::vector<Bytes>());
    Answer(0);
    Answer(-1);                                             // punishments: logged, no timer
    Answer(-1);                                             // the reset time
    s_pMain->m_mapTimer.erase(0x22);
    CHECK(!app.OnLocalDataInitialized());
    CHECK(Logged("Cannot get punishment information"));
    CHECK(Logged("MeterRateSytem data loading failed...."));
    CHECK(s_pMain->m_mapTimer.find(0x22) == s_pMain->m_mapTimer.end());
    CHECK(Relayed(9, 0x631C, data));
    s_vecCall.clear();
    CHECK(NoQueries());
    s_pMain->m_OverlapJobManager.ReleaseAll();

    // OnShutdown: the default session is destroyed and the users removed.
    s_vecDB.clear();
    CHECK(app.OnShutdown());
    char sz[32];
    sprintf_s(sz, "close %p", g_pDefaultDBSession);
    CHECK(DB(sz));
    CHECK(g_ContentUserManager.m_mapUser.empty() && g_ContentUserPool.m_nInUse == 0);
    CHECK(NoPackets(50));
    ClearLogs();
}

// A test that throws (a short read of a handler) is reported and the next one runs.
#define RUN(test)                                                           \
    try                                                                     \
    {                                                                       \
        test();                                                             \
    }                                                                       \
    catch (...)                                                             \
    {                                                                       \
        printf("FAILED: %s threw\n", #test);                                \
        ++g_nFailed;                                                        \
    }

int main()
{
    CHECK(LoopbackBind_Install() == 1);
    setvbuf(stdout, NULL, _IONBF, 0);
    BSDebugSetFlags(0, 0);
    InitializeCriticalSection(&s_cs);

    Startup();
    RUN(TestInitMsgHandlers);
    RUN(TestLogin);
    RUN(TestCheck);
    RUN(TestLogout);
    RUN(TestShardUsers);
    RUN(TestShardRequests);
    RUN(TestValidation);
    RUN(TestBilling);
    RUN(TestPunishmentAndSlave);
    RUN(TestBroadcast);
    RUN(TestEvents);
    RUN(TestTimers);
    RUN(TestBillingCreation);
    RUN(TestLocalData);

    s_Engine.Shutdown();
    g_pBSNet = NULL;
    DeleteCriticalSection(&s_cs);
    return TEST_RESULT("GlobalManagerMainTest");
}
