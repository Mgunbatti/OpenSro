// GlobalManager billing (CStandardBillingMgr, CBaseBillingMgr, CThreadExMgr, CIOCP). The
// network and timer libraries are the stand-in DLLs AlphegaNet.dll / AlphegaTimer.dll next to
// the exe; the billing server is a local TCP server of the test (127.0.0.1). A real engine is
// connected to itself: the client task records what the manager relays and answers to the
// AgentServers and shard managers.

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
#include "GlobalManager/GlobalManagerJobs.h"
#include "GlobalManager/GlobalManagerMainFwd.h"
#include "GlobalManager/GlobalManagerSvcFwd.h"
#include "GlobalManager/ServiceManagerJobs.h"
#include "GlobalManager/ServiceManagerProcess.h"
#include "GlobalManager/Billing/StandardBillingMgr.h"
#include "BSLib/BSDebug.h"
#include "BSLib/DB/DBQuery.h"
#include "BSNet/NetEngine.h"
#include "BSNet/SockListener.h"
#include "BSNet/security/Security.h"
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

// A record of n bytes: (seed + i) & 0xFF.
static Bytes Pattern(size_t n, BYTE bySeed)
{
    Bytes v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = (BYTE)(bySeed + i);
    return v;
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
// Tasks: both record every message; the server task's job records the jobs sent to it.

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

    virtual bool SendOverlapJob(COverlapJob_Local* pJob, CTask* pTask)
    {
        m_vecJobs.push_back(pJob);
        m_vecJobTasks.push_back(pTask);
        return true;
    }

    std::vector<COverlapJob_Local*> m_vecJobs;
    std::vector<CTask*>             m_vecJobTasks;
};

BS_IMPLEMENT_DYNCREATE(CTestTask, &CServerProcessBase::classCServerProcessBase)

// Stands in for g_pGlobalManagerProcess (its owner task is NULL).
class CTestMainProcess : public CServerProcessMain
{
};

static CNetEngine s_Engine;
static CTestTask* s_pMain;      // the server task's job
static CTask* s_pTask;          // the server task
static DWORD s_dwClient;        // client side of the connection
static DWORD s_dwServer;        // GlobalManager side
static const DWORD CLOSED_SESSION = 40;    // in the session table (50), never used

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

// The next packet with the opcode at the client.
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

static CMsg* Req(const Bytes& data)
{
    CMsg* p = g_pBSNet->AllocMsg(FALSE);
    p->SetOpcode(0x1234);
    if (!data.empty())
        p->WriteBytes(&data[0], (WORD)data.size());
    p->m_dwSessionID = s_dwServer;
    return p;
}

// ---------------------------------------------------------------------------------------------
// Stand-in for the main process' setter (GlobalManagerJobs.obj calls it).

void GlobalManagerMain_SetLastPlayTimeReset(DWORD)
{
}

// ---------------------------------------------------------------------------------------------
// Query double: ExecuteFormat formats like the original, then answers from the script (rows
// go through the trigger when there is one).

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

static CDBSessionMgr* const DEFAULT_DB = (CDBSessionMgr*)0x1111;
static CDBSessionMgr* const ACCOUNT_DB = (CDBSessionMgr*)0x2222;
static CDBSessionMgr* const SILK_DB = (CDBSessionMgr*)0x3333;

static bool Query(const char* pszSQL, CDBSessionMgr* pSession, int nSession = 0)
{
    if (s_vecCall.empty())
    {
        printf("  no query (%s)\n", pszSQL);
        return false;
    }
    QUERY_CALL c = s_vecCall.front();
    s_vecCall.erase(s_vecCall.begin());
    bool b = c.strSQL == pszSQL && c.pSession == pSession && c.nSession == nSession;
    if (!b)
        printf("  query: %s (%p %d)\n  want:  %s\n", c.strSQL.c_str(), c.pSession, c.nSession,
               pszSQL);
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

// ---------------------------------------------------------------------------------------------
// Architecture: body 1 this GlobalManager, 2 a GatewayServer, 3 a DownloadServer (both
// reachable), 4 a GatewayServer without connection, 5 and 6 shard bodies of shard 10 (states 5
// and 3), 7 the shard manager of shard 11.

static CERT_MODULE s_aModule[4];
static SERVER_BODY s_aBody[7];
static SERVER_CORD s_CordUp;
static SERVER_CORD s_CordDown;
static CERT_SHARD s_aShard[2];
static ServerBodyList s_listBody;
static CertShardList s_listShard;

static void AddModule(int i, BYTE byID, const char* pszName, const char* pszLower)
{
    s_aModule[i].byID = byID;
    strcpy_s(s_aModule[i].szName, pszName);
    g_mapCertModuleByName[pszLower] = &s_aModule[i];
}

static void AddBody(int i, WORD wID, int nModule, int nState, SERVER_CORD* pRoute,
                    CERT_SHARD* pShard)
{
    SERVER_BODY& b = s_aBody[i];
    b.wID = wID;
    b.byModuleID = s_aModule[nModule].byID;
    b.pModule = &s_aModule[nModule];
    b.nState = nState;
    b.pShard = pShard;
    s_listBody.push_back(&b);
    g_mapServerBody[wID] = &b;
    if (pRoute)
        g_hmapServerRoute[wID] = pRoute;
}

static void SetupArchitecture()
{
    AddModule(0, 1, "GatewayServer", "gatewayserver");
    AddModule(1, 2, "DownloadServer", "downloadserver");
    AddModule(2, 3, "GlobalManager", "globalmanager");
    AddModule(3, 4, "ShardManager", "shardmanager");
    s_CordUp.dwSessionID = s_dwServer;
    s_CordDown.dwSessionID = 0;
    s_aShard[0].wID = 10;
    s_aShard[0].w548 = 1000;
    s_aShard[0].w557 = 250;
    s_aShard[0].wManagerID = 7;
    s_aShard[1].wID = 11;
    s_aShard[1].w548 = 200;
    s_aShard[1].w557 = 30;
    s_aShard[1].wManagerID = 7;
    AddBody(0, 1, 2, 5, NULL, NULL);
    AddBody(1, 2, 0, 5, &s_CordUp, NULL);
    AddBody(2, 3, 1, 5, &s_CordUp, NULL);
    AddBody(3, 4, 0, 4, &s_CordDown, NULL);
    AddBody(4, 5, 3, 5, &s_CordUp, &s_aShard[0]);
    AddBody(5, 6, 3, 3, &s_CordUp, &s_aShard[0]);
    AddBody(6, 7, 3, 4, &s_CordUp, &s_aShard[1]);
    g_pServerBodyList = &s_listBody;
    g_pSelfBody = &s_aBody[0];
    g_mapCertShard[10] = &s_aShard[0];
    g_mapCertShard[11] = &s_aShard[1];
    s_listShard.push_back(&s_aShard[0]);
    s_listShard.push_back(&s_aShard[1]);
    g_pCertShardList = &s_listShard;
}

// Runs the overlap side and checks the steps' results.
static bool Run(COverlapJob_Local* pJob, bool bPrepare, bool bExecute)
{
    bool bP = pJob->Prepare();
    bool bE = bP && pJob->Execute();
    if (bP != bPrepare || (bP && bE != bExecute))
    {
        printf("  Prepare %d Execute %d\n", bP, bE);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------

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
    s_pTask = s_Engine.GetProactor(TEST_TASK_SERVER);
    s_pMain = (CTestTask*)s_pTask->GetJob();
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
    g_pGlobalManagerProcess = new CTestMainProcess;
}


// ---------------------------------------------------------------------------------------------
// Relays and answers at the client

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

// ---------------------------------------------------------------------------------------------
// The billing server (a TCP server on 127.0.0.1, frames [WORD size][opcode][data])

static SOCKET s_hListen = INVALID_SOCKET;
static SOCKET s_hPeer = INVALID_SOCKET;
static WORD s_wServerPort;

static void StartServer()
{
    s_hListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    CHECK(bind(s_hListen, (sockaddr*)&addr, sizeof(addr)) == 0);
    CHECK(listen(s_hListen, 4) == 0);
    int nLen = sizeof(addr);
    getsockname(s_hListen, (sockaddr*)&addr, &nLen);
    s_wServerPort = ntohs(addr.sin_port);
}

static bool WaitReadable(SOCKET s, DWORD dwTimeout)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv;
    tv.tv_sec = dwTimeout / 1000;
    tv.tv_usec = (dwTimeout % 1000) * 1000;
    return select(0, &set, NULL, NULL, &tv) == 1;
}

static bool AcceptPeer()
{
    if (!WaitReadable(s_hListen, 3000))
    {
        printf("  no connection to the billing server\n");
        return false;
    }
    if (s_hPeer != INVALID_SOCKET)
        closesocket(s_hPeer);
    s_hPeer = accept(s_hListen, NULL, NULL);
    return s_hPeer != INVALID_SOCKET;
}

static bool RecvAll(SOCKET s, void* p, int n)
{
    char* pc = (char*)p;
    while (n > 0)
    {
        if (!WaitReadable(s, 3000))
            return false;
        int r = recv(s, pc, n, 0);
        if (r <= 0)
            return false;
        pc += r;
        n -= r;
    }
    return true;
}

// The next frame the manager sent: opcode and data.
static bool Frame(WORD wOpcode, const Bytes& want)
{
    WORD wSize;
    if (!RecvAll(s_hPeer, &wSize, 2) || wSize < 2)
    {
        printf("  no frame 0x%04x\n", wOpcode);
        return false;
    }
    Bytes data(wSize);
    if (!RecvAll(s_hPeer, &data[0], wSize))
        return false;
    WORD wGot = *(WORD*)&data[0];
    if (wGot != wOpcode)
    {
        printf("  frame 0x%04x, want 0x%04x\n", wGot, wOpcode);
        return false;
    }
    return Same(Bytes(data.begin() + 2, data.end()), want);
}

static bool NoFrame(DWORD dwWait = 200)
{
    if (!WaitReadable(s_hPeer, dwWait))
        return true;
    char c;
    if (recv(s_hPeer, &c, 1, MSG_PEEK) == 0)
        return true;    // closed, nothing sent
    printf("  unexpected frame\n");
    return false;
}

static void SendFrame(WORD wOpcode, const Bytes& data)
{
    Bytes frame;
    PutW(frame, (WORD)(data.size() + 2));
    PutW(frame, wOpcode);
    frame.insert(frame.end(), data.begin(), data.end());
    CHECK(send(s_hPeer, (const char*)&frame[0], (int)frame.size(), 0) == (int)frame.size());
}

// ---------------------------------------------------------------------------------------------
// The stand-in libraries

typedef void (*PFN_FAKENET_GETINIT)(ALPHEGA_NET_INIT*, ALPHEGA_NET_UNIT*);
typedef void (*PFN_TAKECALLS)(char*, int);
typedef void (*PFN_FAKETIMER_GETINIT)(ALPHEGA_TIMER_INIT*);
typedef BOOL (*PFN_FAKETIMER_INDEX)(DWORD);
typedef DWORD (*PFN_FAKETIMER_INTERVAL)(DWORD);

static PFN_FAKENET_GETINIT s_pfnNetGetInit;
static PFN_TAKECALLS s_pfnNetCalls;
static PFN_FAKETIMER_GETINIT s_pfnTimerGetInit;
static PFN_TAKECALLS s_pfnTimerCalls;
static PFN_FAKETIMER_INDEX s_pfnTimerIsActive;
static PFN_FAKETIMER_INDEX s_pfnTimerFire;
static PFN_FAKETIMER_INTERVAL s_pfnTimerInterval;

static bool LoadFakes()
{
    HMODULE hNet = GetModuleHandleA("AlphegaNet.dll");
    HMODULE hTimer = GetModuleHandleA("AlphegaTimer.dll");
    if (!hNet || !hTimer)
        return false;
    s_pfnNetGetInit = (PFN_FAKENET_GETINIT)GetProcAddress(hNet, "FakeNet_GetInit");
    s_pfnNetCalls = (PFN_TAKECALLS)GetProcAddress(hNet, "FakeNet_TakeCalls");
    s_pfnTimerGetInit = (PFN_FAKETIMER_GETINIT)GetProcAddress(hTimer, "FakeTimer_GetInit");
    s_pfnTimerCalls = (PFN_TAKECALLS)GetProcAddress(hTimer, "FakeTimer_TakeCalls");
    s_pfnTimerIsActive = (PFN_FAKETIMER_INDEX)GetProcAddress(hTimer, "FakeTimer_IsActive");
    s_pfnTimerFire = (PFN_FAKETIMER_INDEX)GetProcAddress(hTimer, "FakeTimer_Fire");
    s_pfnTimerInterval = (PFN_FAKETIMER_INTERVAL)GetProcAddress(hTimer,
                                                                "FakeTimer_GetInterval");
    return s_pfnNetGetInit && s_pfnNetCalls && s_pfnTimerGetInit && s_pfnTimerCalls
           && s_pfnTimerIsActive && s_pfnTimerFire && s_pfnTimerInterval;
}

static std::string NetCalls()
{
    char sz[1024];
    s_pfnNetCalls(sz, sizeof(sz));
    return sz;
}

static std::string TimerCalls()
{
    char sz[1024];
    s_pfnTimerCalls(sz, sizeof(sz));
    return sz;
}

static bool CallsAre(const std::string& strGot, const char* pszWant)
{
    if (strGot == pszWant)
        return true;
    printf("  calls: %s\n  want:  %s\n", strGot.c_str(), pszWant);
    return false;
}

// Waits until the condition of the worker thread holds.
static bool WaitFor(bool (*pfn)(), DWORD dwTimeout = 3000)
{
    DWORD dwStart = GetTickCount();
    while (!pfn())
    {
        if (GetTickCount() - dwStart > dwTimeout)
            return false;
        Sleep(5);
    }
    return true;
}

static CStandardBillingMgr* s_pBilling;

static bool IsActivated()
{
    return s_pBilling->m_nState == BILLING_STATE_ACTIVATED;
}

static bool IsIdle()
{
    return s_pBilling->m_nState == BILLING_STATE_IDLE;
}

static bool AckTimerStopped()
{
    return !s_pfnTimerIsActive(eHeartBeatAckTimer);
}

// ---------------------------------------------------------------------------------------------
// Users

static void RegisterUser(DWORD dwJID, WORD wShardID, DWORD dwRelayID, const char* pszMAC)
{
    sContentUser user;
    user.dwIP = 0x0100000A + dwJID;
    user.dwJID = dwJID;
    user.strID = "user";
    user.byContentID = 2;
    user.wShardID = wShardID;
    user.wAgentBodyID = 5;
    user.strMAC = pszMAC;
    user.PlayTime.Clear();
    CHECK(g_ContentUserManager.Register(&user) == 0);
    sContentUser* p = g_ContentUserManager.Find(dwJID);
    CHECK(p != NULL);
    if (p)
    {
        p->dwSessionID = s_dwServer;
        p->dwRelayID = dwRelayID;
        p->PlayTime.wPlayTime = 1234;
        p->b5C = 0x5C;
        p->dw64 = 0x64;
        p->dwTick68 = 0;
    }
}

// One user of the 0x8007 list.
static void PutListUser(Bytes& v, DWORD dwJID, WORD wShardID, const char* pszMAC)
{
    PutD(v, dwJID);
    PutD(v, 0x0100000A + dwJID);
    PutD(v, 2);
    PutD(v, wShardID);
    PutB(v, 0x5C);
    PutD(v, 0x64);
    PutD(v, 0);
    PutS(v, pszMAC);
}

static CMsg* MainMsg(WORD wOpcode, const Bytes& data, bool bConsumed)
{
    CMsg* p = g_pBSNet->AllocMsg(FALSE);
    p->SetOpcode(wOpcode);
    if (!data.empty())
        p->WriteBytes(&data[0], (WORD)data.size());
    // The main process has read the message already; the manager reads it again.
    if (bConsumed)
        p->SkipRemaining();
    return p;
}

static int Process(WORD wOpcode, const Bytes& data, bool bConsumed = false)
{
    CMsg* p = MainMsg(wOpcode, data, bConsumed);
    int n = s_pBilling->ProcessMsg(p, 11, 22);
    g_pBSNet->ReleaseMsg(p);
    return n;
}

// ---------------------------------------------------------------------------------------------
// Packet, threads

static void TestBillingPacket()
{
    CBillingPacket packet;
    CHECK(packet.m_wReadPos == 4 && packet.m_wWritePos == 4 && *(DWORD*)packet.m_abData == 0);
    CHECK(packet.m_pwSize == (WORD*)packet.m_abData);
    CHECK(packet.m_pwOpcode == (WORD*)(packet.m_abData + 2));
    packet.SetOpcode(0x2001);
    DWORD dw = 0x11223344;
    packet.Write(&dw, 4);
    CHECK(*packet.m_pwSize == 4 && packet.m_wWritePos == 8);
    packet.WriteString("abc");
    CHECK(*packet.m_pwSize == 9 && memcmp(packet.m_abData + 8, "\x03\x00" "abc", 5) == 0);
    packet.WriteString(std::string("de"));
    CHECK(*packet.m_pwSize == 13);
    packet.WriteString((const char*)NULL);
    CHECK(*packet.m_pwSize == 15 && packet.m_abData[17] == 0 && packet.m_abData[18] == 0);
    DWORD dwRead;
    packet.Read(dwRead);
    CHECK(dwRead == 0x11223344 && packet.m_wReadPos == 8);
    // A string is cut to end at byte 4095 without counting its length word, so the cut text
    // does not fit and throws an int (GM-019) after the length was written.
    std::string strLong(5000, 'x');
    bool bThrown = false;
    try
    {
        packet.WriteString(strLong);
    }
    catch (int n)
    {
        bThrown = n == 0;
    }
    CHECK(bThrown && packet.m_wWritePos == 21);
    CHECK(*(WORD*)(packet.m_abData + 19) == 4095 - 19);
    // A string that fits up to byte 4094 is written whole.
    packet.Reset();
    std::string strFit(4090, 'y');
    packet.WriteString(strFit);
    CHECK(packet.m_wWritePos == 4096 && *packet.m_pwSize == 4092);
    bThrown = false;
    try
    {
        packet.Write(&dw, 1);
    }
    catch (int)
    {
        bThrown = true;
    }
    CHECK(bThrown && packet.m_wWritePos == 4096);
    // A received packet: the size word counted the opcode.
    packet.Reset();
    *packet.m_pwSize = 6;
    packet.AdjustReceived();
    CHECK(*packet.m_pwSize == 4 && packet.m_wWritePos == 8);
}

struct TestWork : public sThreadWork
{
    virtual void DoWork(ULONG_PTR dwKey, DWORD dwBytes, LPOVERLAPPED pOverlapped)
    {
        m_dwKey = dwKey;
        m_dwBytes = dwBytes;
        m_pOverlapped = pOverlapped;
        m_dwThread = GetCurrentThreadId();
        InterlockedIncrement(&m_lCount);
    }

    volatile LONG   m_lCount;
    ULONG_PTR       m_dwKey;
    DWORD           m_dwBytes;
    LPOVERLAPPED    m_pOverlapped;
    DWORD           m_dwThread;
};

static TestWork s_Work;

static bool WorkDone()
{
    return s_Work.m_lCount != 0;
}

static void TestThreadEx()
{
    CIOCP iocp;
    CHECK(iocp.m_hIOCP != NULL);
    CThreadExMgr* pMgr = new CThreadExMgr(&s_Work, &iocp);
    CHECK(pMgr->m_pWork == &s_Work && pMgr->m_pIOCP == &iocp && pMgr->m_nPriority == 0);
    CHECK(pMgr->CreateThread() && pMgr->CreateThread());
    CHECK(pMgr->m_vecThread.size() == 2);
    CThreadEx* pThread = pMgr->m_vecThread[0];
    CHECK(pThread->m_pMgr == pMgr && pThread->m_bSuspended == 1 && pThread->m_hThread);
    // Suspended: a completion waits.
    CHECK(iocp.PostStatus(0x1234, 5, (LPOVERLAPPED)0x40));
    Sleep(100);
    CHECK(s_Work.m_lCount == 0);
    CHECK(pMgr->ResumeAll());
    CHECK(pThread->m_bSuspended == 0 && pMgr->m_vecThread[1]->m_bSuspended == 0);
    CHECK(WaitFor(WorkDone));
    CHECK(s_Work.m_dwKey == 0x1234 && s_Work.m_dwBytes == 5
          && s_Work.m_pOverlapped == (LPOVERLAPPED)0x40);
    // Suspend and resume once each.
    CHECK(pMgr->SuspendAll());
    CHECK(pThread->m_bSuspended == 1);
    DWORD dwCount = SuspendThread(pThread->m_hThread);
    CHECK(dwCount == 1);
    ResumeThread(pThread->m_hThread);
    CHECK(pMgr->ResumeAll() && pThread->m_bSuspended == 0);
    dwCount = SuspendThread(pThread->m_hThread);
    CHECK(dwCount == 0);
    ResumeThread(pThread->m_hThread);
    // The workers end on empty completions; StopAll deletes them.
    HANDLE hThread;
    CHECK(DuplicateHandle(GetCurrentProcess(), pThread->m_hThread, GetCurrentProcess(), &hThread,
                          0, FALSE, DUPLICATE_SAME_ACCESS));
    delete pMgr;
    DWORD dwExit = 0;
    CHECK(GetExitCodeThread(hThread, &dwExit) && dwExit == 0);
    CloseHandle(hThread);
    s_Work.m_lCount = 0;
}

// ---------------------------------------------------------------------------------------------
// Billing manager

static void TestBillingInit()
{
    s_pBilling = new CStandardBillingMgr(s_pMain);
    CStandardBillingMgr& mgr = *s_pBilling;
    CHECK(mgr.m_pOwner == s_pMain && mgr.m_nState == BILLING_STATE_IDLE);
    CHECK(mgr.m_dwUnit == (DWORD)-1 && mgr.m_dwSession == (DWORD)-1);
    CHECK(!mgr.m_pINet && !mgr.m_pITimer && !mgr.m_pEventNotifier && mgr.m_IOCP.m_hIOCP);
    CHECK(!mgr.m_bNoActiveUserList && mgr.m_wPort == 0 && mgr.m_szAddress[0] == 0);
    CHECK(mgr.m_aHandler[0x1234] == &CBaseBillingMgr::OnUnhandledPacket);
    CHECK(mgr.m_aHandler[0xFFFE] == &CBaseBillingMgr::OnUnhandledPacket);
    CHECK(mgr.m_aHandler[0x4001] == static_cast<PFN_BILLING_HANDLER>(
                                        &CStandardBillingMgr::OnCertifyAck));
    CHECK(mgr.m_aHandler[0x200B] == static_cast<PFN_BILLING_HANDLER>(
                                        &CStandardBillingMgr::OnOnlineStatusReq));
    IBillingMgr* p = s_pBilling;
    CHECK((BYTE*)p == (BYTE*)s_pBilling + 4);
    CHECK(!p->IsReady());

    CHECK(p->Initialize("127.0.0.1", s_wServerPort, 0x2000));
    CHECK(LoadFakes());
    if (!s_pfnNetGetInit)
        return;
    CHECK(strcmp(mgr.m_szAddress, "127.0.0.1") == 0 && mgr.m_wPort == s_wServerPort);
    ALPHEGA_NET_INIT netInit;
    ALPHEGA_NET_UNIT unit;
    s_pfnNetGetInit(&netInit, &unit);
    CHECK(netInit.hIOCP == mgr.m_IOCP.m_hIOCP && netInit.by4 == 2 && netInit.by5 == 1
          && netInit.by6 == 0);
    CHECK(unit.w0 == 50 && unit.w2 == 0x1000 && unit.w4 == 0x8000 && unit.wPoolSize == 0x2000);
    CHECK(unit.w8 == 0x4000 && unit.wA == 0x4000 && unit.wC == 0x1000 && unit.wE == 0x1000);
    CHECK(unit.by10 == 0 && unit.by11 == 0 && unit.dw14 == 0 && unit.dw18 == 0);
    CHECK(mgr.m_dwUnit == 0);
    ALPHEGA_TIMER_INIT timerInit;
    s_pfnTimerGetInit(&timerInit);
    CHECK(timerInit.hIOCP == mgr.m_IOCP.m_hIOCP && timerInit.by4 == 1 && timerInit.by5 == 3);
    CHECK(CallsAre(NetCalls(), "Initialize;CreateUnit;Start;"));
    CHECK(CallsAre(TimerCalls(), "Initialize;Start 0;CreateTimer 0 0 30000;"
                                 "CreateTimer 0 0 3000;CreateTimer 0 0 10000;"));
    CHECK(mgr.m_dwTimerIndex[eHeartBeatTimer] == 0 && s_pfnTimerInterval(0) == 30000);
    CHECK(mgr.m_dwTimerIndex[eHeartBeatAckTimer] == 1 && s_pfnTimerInterval(1) == 3000);
    CHECK(mgr.m_dwTimerIndex[eReconnectTimer] == 2 && s_pfnTimerInterval(2) == 10000);
    CHECK(mgr.m_pEventNotifier && mgr.m_pEventNotifier->m_vecThread.size() == 1);
    CHECK(mgr.m_pEventNotifier->m_pWork == static_cast<sThreadWork*>(s_pBilling));
    CHECK(mgr.m_pEventNotifier->m_vecThread[0]->m_bSuspended == 1);
    CHECK(!p->IsReady());
}

static void TestBillingConnect()
{
    IBillingMgr* p = s_pBilling;
    RegisterUser(100, 10, 0x77, "00-11-22-33-44-55");
    RegisterUser(101, 11, 0x78, "");
    CHECK(p->Start());
    CHECK(s_pBilling->m_pEventNotifier->m_vecThread[0]->m_bSuspended == 0);
    char szConnect[64];
    sprintf_s(szConnect, "Connect 0 127.0.0.1 %u;", s_wServerPort);
    CHECK(CallsAre(NetCalls(), szConnect));
    CHECK(AcceptPeer());
    // 0x2001 {103}
    Bytes want;
    PutD(want, 103);
    CHECK(Frame(0x2001, want));
    CHECK(s_pBilling->m_nState == BILLING_STATE_CONNECTED && !p->IsReady());

    // 0x4001 {0, 0}: activated, the user list (both users), the heartbeat.
    Bytes ack;
    PutB(ack, 0);
    PutB(ack, 0);
    SendFrame(0x4001, ack);
    want.clear();
    PutW(want, 2);
    PutListUser(want, 100, 10, "00-11-22-33-44-55");
    PutListUser(want, 101, 11, "");
    CHECK(Frame(0x8007, want));
    want.clear();
    PutB(want, 0);
    CHECK(Frame(0x2003, want));
    CHECK(WaitFor(IsActivated));
    CHECK(p->IsReady() && !s_pBilling->m_bNoActiveUserList);
    CHECK(s_pfnTimerIsActive(eHeartBeatTimer) && s_pfnTimerIsActive(eHeartBeatAckTimer));
    CHECK(!s_pfnTimerIsActive(eReconnectTimer));
    // 0x4003 stops the answer timer; the heartbeat timer sends the next heartbeat.
    SendFrame(0x4003, Bytes());
    CHECK(WaitFor(AckTimerStopped));
    CHECK(s_pfnTimerFire(eHeartBeatTimer));
    CHECK(Frame(0x2003, want));
    CHECK(s_pfnTimerIsActive(eHeartBeatAckTimer));
    SendFrame(0x4003, Bytes());
    CHECK(WaitFor(AckTimerStopped));

    // 0x620C: the list in packets of 90 users.
    for (DWORD i = 0; i < 89; ++i)
        RegisterUser(200 + i, 10, 0, "m");
    CHECK(Process(0x620C, Bytes()) == 2);
    want.clear();
    PutW(want, 90);
    PutListUser(want, 100, 10, "00-11-22-33-44-55");
    PutListUser(want, 101, 11, "");
    for (DWORD i = 0; i < 88; ++i)
        PutListUser(want, 200 + i, 10, "m");
    CHECK(Frame(0x8007, want));
    want.clear();
    PutW(want, 1);
    PutListUser(want, 288, 10, "m");
    CHECK(Frame(0x8007, want));
    DWORD dwIP;
    for (DWORD i = 0; i < 89; ++i)
        CHECK(g_ContentUserManager.Unregister(200 + i, &dwIP));
    CHECK(NoFrame() && NoPackets(50));
}

static void TestBillingMainMsgs()
{
    // 0x6201 (read again from the start): 0x2002 {second, first, DWORD byte, DWORD word, MAC}.
    Bytes data;
    PutD(data, 0x01020304);
    PutD(data, 100);
    PutS(data, "account");
    PutB(data, 7);
    PutW(data, 0x1122);
    PutW(data, 0x3344);
    PutS(data, "AA-BB");
    CHECK(Process(0x6201, data, true) == 2);
    Bytes want;
    PutD(want, 100);
    PutD(want, 0x01020304);
    PutD(want, 7);
    PutD(want, 0x1122);
    PutS(want, "AA-BB");
    CHECK(Frame(0x2002, want));
    // 0x6202: one user, or a list.
    data.clear();
    PutB(data, 1);
    PutD(data, 100);
    CHECK(Process(0x6202, data) == 2);
    CHECK(Frame(0x8008, data));
    data.clear();
    PutB(data, 2);
    PutW(data, 3);
    PutD(data, 5);
    PutD(data, 6);
    PutD(data, 7);
    CHECK(Process(0x6202, data) == 2);
    CHECK(Frame(0x8008, data));
    // 0x6900 (read again): 0x2006 {JID} of a logged-in user only.
    data.clear();
    PutD(data, 100);
    CHECK(Process(0x6900, data, true) == 2);
    CHECK(Frame(0x2006, data));
    data.clear();
    PutD(data, 999);
    CHECK(Process(0x6900, data) == 2);
    CHECK(NoFrame());
    // 0x6904: 0x800A {DWORD, BYTE, DWORD}.
    data.clear();
    PutD(data, 100);
    PutD(data, 0x1234567);
    PutD(data, 0xAABBCCDD);
    CHECK(Process(0x6904, data) == 2);
    want.clear();
    PutD(want, 100);
    PutB(want, 0x67);
    PutD(want, 0xAABBCCDD);
    CHECK(Frame(0x800A, want));
    // Other messages are not taken.
    CHECK(Process(0x6203, data) == 0);
    CHECK(NoFrame() && NoPackets(50));
}

static void TestBillingServerMsgs()
{
    sContentUser* pUser = g_ContentUserManager.Find(100);
    CHECK(pUser != NULL);
    if (!pUser)
        return;
    pUser->byIPType = 3;
    // 0x4002 {JID, 0}: 0xA201 {JID, 1, IP type, WORD play time} through the relay of the user.
    Bytes data;
    PutD(data, 100);
    PutB(data, 0);
    SendFrame(0x4002, data);
    Bytes want;
    PutB(want, 1);
    PutD(want, 0x77);
    PutW(want, 0xA201);
    PutD(want, 100);
    PutB(want, 1);
    PutB(want, 3);
    PutW(want, 1234);
    CHECK(Packet(SERVER_MSG_RELAY_ACK, want));
    CHECK(pUser->b5D == 1);
    // 0x8004 / 0x8005 / 0x8009: 0x6902 / 0x6905 to the user's AgentServer (body 5).
    data.clear();
    PutD(data, 100);
    PutB(data, 7);
    PutD(data, 0x11223344);
    SendFrame(0x8004, data);
    want.clear();
    PutD(want, 100);
    PutB(want, 2);
    PutB(want, 7);
    PutD(want, 0x11223344);
    CHECK(Relayed(5, 0x6902, want));
    CHECK(pUser->b5C == 7 && pUser->dw60 == 0x11223344);
    data.clear();
    PutD(data, 100);
    PutB(data, 8);
    PutD(data, 5);
    SendFrame(0x8005, data);
    want.clear();
    PutD(want, 100);
    PutB(want, 1);
    PutB(want, 8);
    PutD(want, 5);
    CHECK(Relayed(5, 0x6902, want));
    CHECK(pUser->b5C == 7);
    data.clear();
    PutD(data, 100);
    PutB(data, 0);
    SendFrame(0x8009, data);
    want.clear();
    PutD(want, 100);
    CHECK(Relayed(5, 0x6905, want));
    // 0x4006 with the flag: 0xA900 {JID, 1, BYTE}, 0x6905.
    data.clear();
    PutD(data, 100);
    PutB(data, 1);
    PutB(data, 9);
    SendFrame(0x4006, data);
    want.clear();
    PutB(want, 1);
    PutD(want, 0x77);
    PutW(want, 0xA900);
    PutD(want, 100);
    PutB(want, 1);
    PutB(want, 9);
    CHECK(Packet(SERVER_MSG_RELAY_ACK, want));
    want.clear();
    PutD(want, 100);
    CHECK(Relayed(5, 0x6905, want));
    // Without the flag: 0xA900, 0x6902 {JID, 0, BYTE, DWORD}, 0x6903 to the shard manager.
    data.clear();
    PutD(data, 100);
    PutB(data, 0);
    PutB(data, 2);
    PutB(data, 3);
    PutD(data, 0x55);
    PutB(data, 4);
    SendFrame(0x4006, data);
    want.clear();
    PutB(want, 1);
    PutD(want, 0x77);
    PutW(want, 0xA900);
    PutD(want, 100);
    PutB(want, 0);
    PutB(want, 2);
    PutB(want, 3);
    PutD(want, 0x55);
    PutD(want, 4);
    CHECK(Packet(SERVER_MSG_RELAY_ACK, want));
    want.clear();
    PutD(want, 100);
    PutB(want, 0);
    PutB(want, 3);
    PutD(want, 0x55);
    CHECK(Relayed(5, 0x6902, want));
    want.clear();
    PutD(want, 100);
    PutD(want, 4);
    CHECK(Relayed(7, 0x6903, want));
    CHECK(pUser->b5C == 3 && pUser->dw60 == 0x55 && pUser->dw64 == 4 && pUser->b5D == 2);
    CHECK(pUser->dwTick68 != 0);
    // Type 1 and no extra: only the answer.
    data.clear();
    PutD(data, 100);
    PutB(data, 0);
    PutB(data, 1);
    PutB(data, 6);
    PutD(data, 0x66);
    PutB(data, 0);
    SendFrame(0x4006, data);
    CHECK(TakePacket(s_dwClient, SERVER_MSG_RELAY_ACK, NULL));
    CHECK(NoPackets(100));
    CHECK(pUser->b5C == 6 && pUser->dw64 == 0);
    // 0x200B: 0x400B {JID, 0, IP, DWORD content, DWORD shard, MAC} or {JID, 1}.
    pUser->dwTick68 = 0;
    data.clear();
    PutD(data, 100);
    SendFrame(0x200B, data);
    want.clear();
    PutD(want, 100);
    PutB(want, 0);
    PutD(want, 0x0100000A + 100);
    PutD(want, 2);
    PutD(want, 10);
    PutS(want, "00-11-22-33-44-55");
    CHECK(Frame(0x400B, want));
    data.clear();
    PutD(data, 999);
    SendFrame(0x200B, data);
    want.clear();
    PutD(want, 999);
    PutB(want, 1);
    CHECK(Frame(0x400B, want));
    // Unknown opcodes are logged only; answers for unknown users are dropped.
    SendFrame(0x7777, data);
    data.clear();
    PutD(data, 999);
    PutB(data, 0);
    SendFrame(0x4002, data);
    // A refused login: 0xA201 {JID, 2, 6}, the user is removed.
    data.clear();
    PutD(data, 101);
    PutB(data, 1);
    PutB(data, 0);
    SendFrame(0x4002, data);
    want.clear();
    PutB(want, 1);
    PutD(want, 0x78);
    PutW(want, 0xA201);
    PutD(want, 101);
    PutB(want, 2);
    PutB(want, 6);
    CHECK(Packet(SERVER_MSG_RELAY_ACK, want));
    CHECK(g_ContentUserManager.Find(101) == NULL);
    CHECK(NoFrame() && NoPackets(50));
}

static bool ReconnectArmed()
{
    return s_pBilling->m_nState == BILLING_STATE_IDLE && s_pfnTimerIsActive(eReconnectTimer);
}

static bool HeartBeatTimedOut()
{
    return s_pfnTimerIsActive(eReconnectTimer) && !s_pfnTimerIsActive(eHeartBeatTimer);
}

static void TestBillingReconnect()
{
    // The server closes the session: idle, the reconnect timer runs.
    closesocket(s_hPeer);
    s_hPeer = INVALID_SOCKET;
    CHECK(WaitFor(ReconnectArmed));
    CHECK(!s_pfnTimerIsActive(eHeartBeatTimer));
    CHECK(s_pfnTimerFire(eReconnectTimer));
    CHECK(AcceptPeer());
    Bytes want;
    PutD(want, 103);
    CHECK(Frame(0x2001, want));
    CHECK(!s_pfnTimerIsActive(eReconnectTimer));
    char szConnect[64];
    sprintf_s(szConnect, "Connect 0 127.0.0.1 %u;", s_wServerPort);
    CHECK(CallsAre(NetCalls(), szConnect));
    // 0x4001 {0, 1}: no user list.
    Bytes ack;
    PutB(ack, 0);
    PutB(ack, 1);
    SendFrame(0x4001, ack);
    want.clear();
    PutB(want, 0);
    CHECK(Frame(0x2003, want));
    CHECK(WaitFor(IsActivated));
    CHECK(s_pBilling->m_bNoActiveUserList);
    CHECK(Process(0x620C, Bytes()) == 2);
    CHECK(NoFrame());
    // A heartbeat timeout stops the timers and arms the reconnect, but the session stays
    // (GM-017): nothing is disconnected and the reconnect timer does nothing.
    CHECK(s_pfnTimerFire(eHeartBeatAckTimer));
    CHECK(WaitFor(HeartBeatTimedOut));
    CHECK(!s_pfnTimerIsActive(eHeartBeatAckTimer));
    CHECK(s_pBilling->m_nState == BILLING_STATE_ACTIVATED);
    CHECK(s_pfnTimerFire(eReconnectTimer));
    Sleep(100);
    CHECK(CallsAre(NetCalls(), ""));
    CHECK(NoFrame());
}

static bool PeerClosed()
{
    char c;
    return WaitReadable(s_hPeer, 10) && recv(s_hPeer, &c, 1, 0) == 0;
}

static void TestBillingStop()
{
    IBillingMgr* p = s_pBilling;
    p->Stop();
    CHECK(s_pBilling->m_nState == BILLING_STATE_IDLE);
    char szDisconnect[64];
    sprintf_s(szDisconnect, "Disconnect 0 %u;", s_pBilling->m_dwSession);
    CHECK(CallsAre(NetCalls(), szDisconnect));
    CHECK(WaitFor(PeerClosed));
    CHECK(!s_pfnTimerIsActive(0) && !s_pfnTimerIsActive(1) && !s_pfnTimerIsActive(2));
    CHECK(s_pBilling->m_pEventNotifier->m_vecThread[0]->m_bSuspended == 1);
    HANDLE hThread;
    CHECK(DuplicateHandle(GetCurrentProcess(), s_pBilling->m_pEventNotifier->m_vecThread[0]->
                          m_hThread, GetCurrentProcess(), &hThread, 0, FALSE,
                          DUPLICATE_SAME_ACCESS));
    p->Cleanup();
    CHECK(!s_pBilling->m_pEventNotifier && !s_pBilling->m_pINet && !s_pBilling->m_pITimer);
    CHECK(CallsAre(TimerCalls(), "Shutdown -1;Release;"));
    CHECK(CallsAre(NetCalls(), "Shutdown -1;Cleanup;Release;"));
    DWORD dwExit = 1;
    CHECK(GetExitCodeThread(hThread, &dwExit) && dwExit == 0);
    CloseHandle(hThread);
    p->Release();
    s_pBilling = NULL;
    DWORD dwIP;
    CHECK(g_ContentUserManager.Unregister(100, &dwIP));
    CHECK(NoPackets(50));

    // The dummy manager does nothing and takes every message.
    IBillingMgr* pDummy = new CDummyBillingMgr;
    CHECK(pDummy->Initialize("x", 1, 2) && pDummy->Start() && pDummy->IsReady());
    CMsg* pMsg = MainMsg(0x6201, Bytes(), false);
    CHECK(pDummy->ProcessMsg(pMsg, 0, 0) == 1);
    g_pBSNet->ReleaseMsg(pMsg);
    pDummy->Stop();
    pDummy->Cleanup();
    pDummy->Release();
}

int main()
{
    CHECK(LoopbackBind_Install() == 1);
    setvbuf(stdout, NULL, _IONBF, 0);
    BSDebugSetFlags(0, 0);
    InitializeCriticalSection(&s_cs);

    Startup();
    StartServer();
    TestBillingPacket();
    TestThreadEx();
    TestBillingInit();
    if (s_pfnNetGetInit)
    {
        TestBillingConnect();
        TestBillingMainMsgs();
        TestBillingServerMsgs();
        TestBillingReconnect();
        TestBillingStop();
    }

    if (s_hPeer != INVALID_SOCKET)
        closesocket(s_hPeer);
    closesocket(s_hListen);
    s_Engine.Shutdown();
    g_pBSNet = NULL;
    DeleteCriticalSection(&s_cs);
    return TEST_RESULT("GlobalManagerBillingTest");
}
