// CNetEngine end to end on 127.0.0.1 through IBSNet. The engine is a process-wide singleton in
// the original (g_pNetEngine, the CSystem task map with fixed proactor IDs, Shutdown releasing
// the CSystem subsystems), so one instance plays both sides: it listens and connects to itself,
// and every connection gives an accepted (server) and a connected (client) session.

#include <winsock2.h>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "BSLib/BSDebug.h"
#include "BSLib/file/FileSystem.h"
#include "BSLib/thread/ServiceObject.h"
#include "BSLib/thread/System.h"
#include "BSLib/thread/Task.h"
#include "BSNet/Msg.h"
#include "BSNet/MsgPool.h"
#include "BSNet/NetEngine.h"
#include "BSNet/Session.h"
#include "BSNet/SessionTable.h"
#include "BSNet/SockListener.h"
#include "BSNet/security/Security.h"
#include "ServerFramework/MassiveMsg.h"

#include "TestCheck.h"

using namespace ServerFramework;

// ---------------------------------------------------------------------------------------------
// Application side

enum
{
    APP_TASK        = 0x10000010,   // receives every session's messages and events
    APP_TASK_EXTRA  = 0x10000011,
    OP_TEST         = 0x7001,
    OP_REPLY        = 0x7002,
};

// Job of the application task. The test pops the task's queue itself; the job only has to
// exist (sessions post events only to tasks with a job).
class CAppJob : public CServiceObject
{
    BS_DECLARE_DYNCREATE(CAppJob)

public:
    virtual DWORD Run(volatile LONG* pbStop, int)
    {
        while (!*pbStop)
            Sleep(10);
        return 0;
    }

    virtual int SignalStop()
    {
        return 1;
    }
};

BS_IMPLEMENT_DYNCREATE(CAppJob, &CServiceObject::classCServiceObject)

static CNetEngine* g_pEng = NULL;
static CTask* g_pAppTask = NULL;
static std::vector<CMsg*> g_vecBacklog;

static CRITICAL_SECTION g_csLog;
static std::vector<std::string> g_vecLog;

static int TestLog(DWORD, const char* pszFormat, ...)
{
    char sz[2048];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf_s(sz, sizeof(sz), _TRUNCATE, pszFormat, args);
    va_end(args);
    EnterCriticalSection(&g_csLog);
    g_vecLog.push_back(sz);
    LeaveCriticalSection(&g_csLog);
    return 0;
}

static BOOL HasLog(const char* pszText)
{
    EnterCriticalSection(&g_csLog);
    BOOL bFound = FALSE;
    for (size_t i = 0; i < g_vecLog.size(); ++i)
    {
        if (g_vecLog[i].find(pszText) != std::string::npos)
            bFound = TRUE;
    }
    LeaveCriticalSection(&g_csLog);
    return bFound;
}

static std::vector<std::pair<DWORD, DWORD> > g_vecDump;

static void __cdecl TestDump(DWORD dwOpcode, DWORD dwCount)
{
    g_vecDump.push_back(std::make_pair(dwOpcode, dwCount));
}

static BOOL MatchMsg(CMsg* p, WORD wOpcode, DWORD dwSessionID)
{
    return p->GetOpcode() == wOpcode && (!dwSessionID || p->m_dwSessionID == dwSessionID);
}

// Next message with wOpcode (from dwSessionID unless 0); everything else is kept for later.
static CMsg* WaitMsg(WORD wOpcode, DWORD dwSessionID, DWORD dwTimeout = 5000)
{
    for (size_t i = 0; i < g_vecBacklog.size(); ++i)
    {
        CMsg* p = g_vecBacklog[i];
        if (MatchMsg(p, wOpcode, dwSessionID))
        {
            g_vecBacklog.erase(g_vecBacklog.begin() + i);
            return p;
        }
    }
    DWORD dwStart = GetTickCount();
    while (GetTickCount() - dwStart < dwTimeout)
    {
        CMsg* p = NULL;
        g_pAppTask->m_pQueue->Pop(&p, 50, FALSE);
        if (!p)
            continue;
        if (MatchMsg(p, wOpcode, dwSessionID))
            return p;
        g_vecBacklog.push_back(p);
    }
    return NULL;
}

// Waits for a session event and returns its parameter in *pdwParam.
static BOOL WaitEvent(WORD wEvent, DWORD dwSessionID, DWORD* pdwParam, DWORD* pdwID = NULL,
                      DWORD dwTimeout = 5000)
{
    CMsg* p = WaitMsg(wEvent, dwSessionID, dwTimeout);
    if (!p)
        return FALSE;
    DWORD dwID = 0;
    DWORD dwParam = 0;
    *p >> dwID >> dwParam;
    CHECK(dwID == p->m_dwSessionID);
    CHECK(p->GetUnreadSize() == 0);
    if (pdwParam)
        *pdwParam = dwParam;
    if (pdwID)
        *pdwID = dwID;
    g_pEng->ReleaseMsg(p);
    return TRUE;
}

// Releases the kept messages and everything still queued.
static void DrainQueue()
{
    for (size_t i = 0; i < g_vecBacklog.size(); ++i)
        g_pEng->ReleaseMsg(g_vecBacklog[i]);
    g_vecBacklog.clear();
    for (;;)
    {
        CMsg* p = NULL;
        g_pAppTask->m_pQueue->Pop(&p, 300, FALSE);
        if (!p)
            break;
        g_pEng->ReleaseMsg(p);
    }
}

static BOOL WaitCondition(BOOL (*pfn)(void*), void* pArg, DWORD dwTimeout = 5000)
{
    DWORD dwStart = GetTickCount();
    while (!pfn(pArg))
    {
        if (GetTickCount() - dwStart > dwTimeout)
            return FALSE;
        Sleep(10);
    }
    return TRUE;
}

static BOOL SessionGone(void* pArg)
{
    DWORD dwID = (DWORD)(DWORD_PTR)pArg;
    return g_pEng->m_SessionTable.FindSession(dwID) == NULL;
}

static BOOL NoSessions(void*)
{
    return g_pEng->m_SessionTable.m_SessionPool.m_nInUse == 0
        && CClassLink<CSession>::ms_nCount == 0;
}

// Connects to the engine's own listener; returns the client ID and the matching server ID.
static BOOL ConnectPair(u_short wPort, DWORD* pdwClient, DWORD* pdwServer)
{
    DWORD dwClient = g_pEng->Connect("127.0.0.1", wPort, NULL, 0, 0);
    *pdwClient = dwClient;
    *pdwServer = 0;
    if (!dwClient)
    {
        printf("ConnectPair: connect failed\n");
        return FALSE;
    }

    DWORD dwRole = 99;
    if (!WaitEvent(SESSION_EVENT_CONNECTED, dwClient, &dwRole))
    {
        printf("ConnectPair: no connect event for %u\n", dwClient);
        return FALSE;
    }
    CHECK(dwRole == SOCK_ROLE_CONNECTED);

    // the accepted side: the connect event with role 0 for a session whose peer is the client
    sockaddr_in local;
    g_pEng->m_SessionTable.FindSession(dwClient)->m_pSocket->GetLocalAddr(&local);
    DWORD dwStart = GetTickCount();
    while (GetTickCount() - dwStart < 5000)
    {
        DWORD dwID = 0;
        if (!WaitEvent(SESSION_EVENT_CONNECTED, 0, &dwRole, &dwID, 200))
            continue;
        if (dwRole != SOCK_ROLE_ACCEPTED)
            continue;
        // the listener creates the session (and its connect event) before it reads the peer
        // address from the AcceptEx buffer
        u_short wPeerPort = 0;
        for (int i = 0; i < 100 && !wPeerPort; ++i)
        {
            if (!g_pEng->GetSessionPeerAddress(dwID, NULL, NULL, &wPeerPort))
                break;
            if (!wPeerPort)
                Sleep(10);
        }
        if (wPeerPort == ntohs(local.sin_port))
        {
            *pdwServer = dwID;
            return TRUE;
        }
        printf("ConnectPair: accepted %u peer port %u, expected %u\n", dwID, wPeerPort,
               ntohs(local.sin_port));
    }
    printf("ConnectPair: no accepted session for %u (pending accepts %d, linked %d, "
           "backlog %u, accept pool %u)\n", dwClient, CSockListener::ms_nPendingAccepts,
           CClassLink<CSession>::ms_nCount, (DWORD)g_vecBacklog.size(),
           g_pEng->m_AcceptPool.m_nInUse);
    for (size_t i = 0; i < g_vecBacklog.size(); ++i)
        printf("  backlog %04X session %u\n", g_vecBacklog[i]->GetOpcode(),
               g_vecBacklog[i]->m_dwSessionID);
    return FALSE;
}

static CMsg* NewMsg(WORD wOpcode, DWORD dwValue, int nFill, BOOL bEncrypted)
{
    CMsg* p = g_pEng->AllocMsg(bEncrypted);
    p->SetOpcode(wOpcode);
    *p << dwValue;
    for (int i = 0; i < nFill; ++i)
        *p << (BYTE)(i * 3);
    return p;
}

// Sends from one session and checks the copy that reaches the application for the other.
static void Exchange(DWORD dwFrom, DWORD dwTo, WORD wOpcode, DWORD dwValue, int nFill,
                     BOOL bEncrypted)
{
    CMsg* pOut = NewMsg(wOpcode, dwValue, nFill, bEncrypted);
    CHECK(g_pEng->SendMsg(dwFrom, pOut) == 0);
    g_pEng->ReleaseMsg(pOut);

    CMsg* pIn = WaitMsg(wOpcode, dwTo);
    CHECK(pIn != NULL);
    if (!pIn)
        return;
    DWORD dw = 0;
    *pIn >> dw;
    CHECK(dw == dwValue);
    CHECK(pIn->GetUnreadSize() == nFill);
    BOOL bSame = TRUE;
    for (int i = 0; i < nFill; ++i)
    {
        BYTE b;
        *pIn >> b;
        if (b != (BYTE)(i * 3))
            bSame = FALSE;
    }
    CHECK(bSame);
    g_pEng->ReleaseMsg(pIn);
}

static void Handshake(DWORD dwServer, DWORD dwClient, BYTE bOptions)
{
    SECURITY_HANDSHAKE_PARAM param;
    param.bOptions = bOptions;
    param.dwCountSeed = 0x1234567;
    param.dwCrcSeed = 0x5A;
    for (int i = 0; i < 8; ++i)
        param.BlowFishKey[i] = (BYTE)(0x31 + 7 * i);
    CHECK(g_pEng->StartHandshake(dwServer, &param) == TRUE);
    // client sessions only answer; StartHandshake on them succeeds without sending
    CHECK(g_pEng->StartHandshake(dwClient, &param) == TRUE);

    DWORD dwParam = 0;
    CHECK(WaitEvent(SESSION_EVENT_HANDSHAKED, dwServer, &dwParam) && dwParam == 6);
    CHECK(WaitEvent(SESSION_EVENT_HANDSHAKED, dwClient, &dwParam) && dwParam == 6);
}

static std::string g_strDir;

static void WriteDiskFile(const std::string& strPath, const BYTE* pData, DWORD dwSize)
{
    HANDLE h = CreateFileA(strPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD dw = 0;
    WriteFile(h, pData, dwSize, &dw, NULL);
    CloseHandle(h);
}

static std::string ReadDiskFile(const std::string& strPath)
{
    std::string s;
    HANDLE h = CreateFileA(strPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return s;
    char buf[4096];
    DWORD dw = 0;
    while (ReadFile(h, buf, sizeof(buf), &dw, NULL) && dw)
        s.append(buf, dw);
    CloseHandle(h);
    return s;
}

// ---------------------------------------------------------------------------------------------
// Pieces that do not need connections

static void TestConstruction()
{
    CHECK(g_pNetEngine == g_pEng);
    CHECK(g_pNetEngineDatagram == &g_pEng->m_Datagram);
    CHECK(g_pEng->m_Datagram.pSocket == NULL && g_pEng->m_Datagram.dwTaskID == 0);
    CHECK(g_nNetIoModel == 0);
    CHECK(g_bSecurityCrcTableBuilt != FALSE);
    CHECK(g_pEng->m_pListener == NULL && g_pEng->m_dw4 == 0);

    // the defaults of the 148-byte block
    const BSNET_INIT_PARAM& c = g_pEng->m_Config;
    CHECK(c.bSecurityMode == 1 && c.bWinsockLow == 2 && c.bWinsockHigh == 2);
    CHECK(c.dwFlags == 0 && c.nAcceptCount == 0 && c.dwMaxConnectTime == 60);
    CHECK(c.dwFileCacheLimit == 0 && c.nSendFileType == -1 && c.nRecvFileType == -1);
    CHECK(c.pfnLog == NULL && c.pfnDumpMsgPool == NetEngine_DefaultDumpMsgPool);
    CHECK(c.Maintainer.dwWaitTimeout == 1000 && c.Maintainer.aJobs[0].dwPeriod == 60000);
    CHECK(c.aSocket[0].nSendQueueLimit == -1 && c.aSocket[0].dwSendMode == 1);
    CHECK(c.aSocket[1].dw4 == 1 && c.aSocket[1].dwSessionParam == 0);
    CHECK(g_pEng->m_MsgPool.m_dw4 == 0);

    // the socket code reads the configuration through these
    CHECK(NetEngine_GetSecurityMode(g_pNetEngine) == 1);
    CHECK(NetEngine_GetMaxConnectTime(g_pNetEngine) == 60);
    CHECK(NetEngine_GetSocketConfig(g_pNetEngine, 1) == &g_pEng->m_Config.aSocket[1]);
    CHECK(NetEngine_GetConnectPool(g_pNetEngine) == &g_pEng->m_ConnectPool);
    CHECK(NetEngine_GetDatagramPool(g_pNetEngine) == &g_pEng->m_DatagramPool);

    // COM-style part
    void* pv = (void*)1;
    CHECK(g_pEng->QueryInterface(IID_IBSNet, &pv) == S_OK && pv == (IBSNet*)g_pEng);
    GUID other = IID_IBSNet;
    other.Data4[7] ^= 1;
    CHECK(g_pEng->QueryInterface(other, &pv) == E_NOINTERFACE && pv == NULL);
    CHECK(g_pEng->AddRef() == 0);
    CHECK(g_pEng->vf09(1, 2, 3, 4) == 0);

    // every constructor takes the global pointers; deleting the instance leaves them alone
    CNetEngine* p2 = new CNetEngine;
    CHECK(g_pNetEngine == p2 && g_pNetEngineDatagram == &p2->m_Datagram);
    delete p2;
    CHECK(g_pNetEngine == p2);
    g_pNetEngine = g_pEng;
    g_pNetEngineDatagram = &g_pEng->m_Datagram;

    // no server flag yet: no listening
    CHECK(g_pEng->Listen("127.0.0.1", 0) == FALSE);
}

static void TestClassLink()
{
    typedef CClassLink<CSession> CLink;
    CHECK(CLink::ms_nCount == 0 && CLink::ms_pHead == NULL);
    CSession a, b, c;
    CHECK(a.m_Link.m_bLinked == FALSE);         // no automatic linking
    CHECK(a.m_dwID == 0 && a.m_pSocket == NULL && a.m_bTrusted == FALSE);
    a.m_Link.Link();
    b.m_Link.Link();
    c.m_Link.Link();
    b.m_Link.Link();                            // already linked: no change
    CHECK(CLink::ms_nCount == 3);
    CHECK(CLink::ms_pHead == &a.m_Link && CLink::ms_pTail == &c.m_Link);
    CHECK(CSession::FromLink(a.m_Link.m_pNext) == &b);

    // removing the current node while iterating keeps the walk going
    int nVisited = 0;
    for (CLink* p = CLink::IterFirst(); p; p = CLink::IterNext())
    {
        ++nVisited;
        if (p == &b.m_Link)
            b.m_Link.Unlink();
    }
    CHECK(nVisited == 3 && CLink::ms_nCount == 2);
    CHECK(a.m_Link.m_pNext == &c.m_Link && c.m_Link.m_pPrev == &a.m_Link);
    a.m_Link.Unlink();
    CHECK(CLink::ms_pHead == &c.m_Link && c.m_Link.m_pPrev == NULL);
    // the destructor unlinks c

    // a session without a socket is never alive
    CHECK(a.IsAlive((DWORD)-1) == FALSE);
    CHECK(a.SendMsg(NULL) == SOCK_ERR_CLOSED);
    CHECK(a.StartHandshake(NULL) == FALSE);
    CHECK(Session_FindTask(0x00000005) == NULL);    // no 0x11000000 bits
}

static void TestSessionTableAlone()
{
    CSessionTable table;
    CHECK(table.m_IDPool.m_dwGrowBy == 5000 && table.m_SessionPool.m_nItemsPerBlock == 500);
    CHECK(table.CreateNewSession(NULL, APP_TASK) == 0);
    CHECK(table.Initialize(3) == TRUE);
    CHECK(table.m_vecSessions.size() == 3 && table.m_IDPool.m_deqFree.size() == 3);
    CHECK(table.m_IDPool.m_deqFree.front() == 1 && table.m_IDPool.m_deqFree.back() == 3);
    CHECK(table.FindSession(2) == NULL);
    BOOL bThrown = FALSE;
    try
    {
        table.FindSession(3);
    }
    catch (std::out_of_range&)
    {
        bThrown = TRUE;
    }
    CHECK(bThrown);
    CHECK(table.Close(0, 1) == 1 && table.Close(1, 1) == 1);      // nothing to close
    DWORD t, u, f;
    table.GetCounters(&t, &u, &f);
    CHECK(t == 0 && u == 0 && f == 0);
    table.CloseAll();
    CHECK(table.m_vecSessions.empty());

    KEEPALIVE_ENTRY e;
    memset(&e, 0xCC, sizeof(e));
    sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_port = htons(5);
    e.Init((sockaddr*)&to, NULL, 7, 9);
    CHECK(e.To.sin_port == htons(5) && e.Bind.sin_addr.s_addr == 0 && e.Bind.sin_port == 0xCCCC);
    CHECK(e.dwTaskID == 7 && e.dwSessionID == 9 && e.nFailCount == 0);
}

static void TestProactors()
{
    CHECK(g_pEng->RegisterProactor(APP_TASK, NULL, &CAppJob::classCAppJob, NULL) == TRUE);
    g_pAppTask = g_pEng->GetProactor(APP_TASK);
    CHECK(g_pAppTask != NULL && g_pAppTask->m_pQueue != NULL && g_pAppTask->GetJob() != NULL);
    CHECK(g_pAppTask->GetJob()->GetRuntimeClass() == &CAppJob::classCAppJob);
    // the ID is taken: the new task is deleted again
    CHECK(g_pEng->RegisterProactor(APP_TASK, NULL, &CAppJob::classCAppJob, NULL) == FALSE);
    CHECK(g_pEng->GetProactor(APP_TASK) == g_pAppTask);

    // an existing task is registered as it is
    CTask* pTask = new CTask;
    pTask->m_pQueue = new CQueMT<CMsg>;     // ~CTask needs a queue (THREAD-006)
    CHECK(g_pEng->RegisterProactor(APP_TASK_EXTRA, pTask, NULL, NULL) == TRUE);
    CHECK(g_pEng->GetProactor(APP_TASK_EXTRA) == pTask && pTask->GetJob() == NULL);
    CHECK(g_pEng->StartProactorWorkers(APP_TASK_EXTRA, 1, 0, 0, 0, NULL) == FALSE);  // no job
    CHECK(g_pEng->UnregisterProactor(APP_TASK_EXTRA) == TRUE);     // deletes the task
    CHECK(g_pEng->GetProactor(APP_TASK_EXTRA) == NULL);
    CHECK(g_pEng->UnregisterProactor(APP_TASK_EXTRA) == FALSE);
    CHECK(g_pEng->StartProactorWorkers(0x12345678, 1, 0, 0, 0, NULL) == FALSE);
    CHECK(g_pEng->ProactorResume(0x12345678) == FALSE);
    CHECK(g_pEng->ProactorSuspend(0x12345678) == FALSE);
}

static BSNET_INIT_PARAM MakeParam()
{
    BSNET_INIT_PARAM p;
    p.dwFlags = BSNET_FLAG_SERVER;
    p.nAcceptCount = 10;
    p.pfnLog = TestLog;
    p.pfnDumpMsgPool = TestDump;
    p.nSendFileType = FILE_TYPE_LEGACY;
    p.nRecvFileType = FILE_TYPE_LEGACY;
    p.aSocket[0].dwSessionParam = APP_TASK;
    p.aSocket[1].dwSessionParam = APP_TASK;
    p.Maintainer.dwWaitTimeout = 20;
    p.Maintainer.dwFlags = MAINTAINER_CONFIG::FLAG_REAP_SESSIONS
                         | MAINTAINER_CONFIG::FLAG_KEEP_ALIVE;
    p.Maintainer.aJobs[0].dwPeriod = 200;
    p.Maintainer.aJobs[0].dwParam = 60000;
    p.Maintainer.aJobs[2].dwPeriod = 50;
    // ignored: Initialize starts Winsock with the engine's previous values (NET-001)
    p.bWinsockLow = 9;
    p.bWinsockHigh = 9;
    return p;
}

static void TestInitialize()
{
    CHECK(g_pEng->Initialize(MakeParam()) == TRUE);
    CHECK(g_pEng->m_Config.bWinsockLow == 9);           // copied, but not used
    CHECK(g_pEng->m_Config.pfnLog == TestLog);
    CHECK(g_pEng->m_MsgPool.m_dw4 == (DWORD)(DWORD_PTR)TestDump);
    CHECK(g_pEng->GetProactor(NETENGINE_TASK_LISTEN) != NULL);
    CHECK(g_pEng->GetProactor(NETENGINE_TASK_IO) != NULL);
    CTask* pMaintainer = g_pEng->GetProactor(NETENGINE_TASK_MAINTAINER);
    CHECK(pMaintainer != NULL);
    if (pMaintainer)
    {
        CMaintainer* pJob = (CMaintainer*)pMaintainer->GetJob();
        CHECK(pJob->m_config.dwWaitTimeout == 20 && pJob->m_Scheduler.m_vecSchedules.size() == 2);
    }
    CHECK(g_pEng->m_SessionTable.m_vecSessions.size() == 50);
    CHECK(g_pEng->m_SessionTable.m_IDPool.m_dwGrowBy == 50);
    CHECK(g_pEng->m_AcceptPool.m_nItemsPerBlock == 5000);
    CHECK(g_pEng->m_ConnectPool.m_nItemsPerBlock == 50);
    CHECK(g_pEng->m_MsgPool.m_nItemsPerBlock == 3000);

    CHECK(g_pEng->StartProactorWorkers(NETENGINE_TASK_LISTEN, 1, 0, 0, 0, NULL) == TRUE);
    CHECK(g_pEng->StartProactorWorkers(NETENGINE_TASK_IO, 2, 0, 0, 0, NULL) == TRUE);
    CHECK(g_pEng->StartProactorWorkers(NETENGINE_TASK_MAINTAINER, 1, 0, 0, 0, NULL) == TRUE);
    CHECK(g_pEng->ProactorSuspend(NETENGINE_TASK_IO) == TRUE);
    CHECK(g_pEng->ProactorResume(NETENGINE_TASK_IO) == TRUE);

    g_pEng->PrintVersion();
    CHECK(HasLog("netengine(ver 1.54)"));
}

// ---------------------------------------------------------------------------------------------
// Connections

static u_short g_wPort = 0;

static void TestListen()
{
    CHECK(g_pEng->Listen("no.such.host.invalid", 0) == FALSE);
    CHECK(g_pEng->Listen("127.0.0.1", 0) == TRUE);
    CHECK(g_pEng->m_pListener != NULL);
    sockaddr_in a;
    g_pEng->m_pListener->GetLocalAddr(&a);
    g_wPort = ntohs(a.sin_port);
    CHECK(g_wPort != 0);
    CHECK(CSockListener::ms_nPendingAccepts == 10);
}

static void TestSessionsAndMessages()
{
    DWORD dwClient = 0;
    DWORD dwServer = 0;
    CHECK(ConnectPair(g_wPort, &dwClient, &dwServer));
    if (!dwServer)
        return;
    CHECK(dwClient != dwServer && dwClient <= 50 && dwServer <= 50);

    CSession* pClient = g_pEng->m_SessionTable.FindSession(dwClient);
    CSession* pServer = g_pEng->m_SessionTable.FindSession(dwServer);
    CHECK(pClient->m_dwID == dwClient && pClient->m_dwTaskID == APP_TASK);
    CHECK(pClient->m_pSocket->m_pSession == pClient && pClient->m_Link.m_bLinked);
    CHECK(pClient->m_pSocket->m_pTask == g_pAppTask);
    CHECK(g_pEng->GetSessionSocketState(dwClient) == SOCK_ROLE_CONNECTED);
    CHECK(g_pEng->GetSessionSocketState(dwServer) == SOCK_ROLE_ACCEPTED);
    CHECK(g_pEng->GetSessionSocketState(49) == -1);
    CHECK(CClassLink<CSession>::ms_nCount == 2);

    std::string strIP;
    ULONG ulIP = 0;
    u_short wPort = 0;
    CHECK(g_pEng->GetSessionPeerAddress(dwClient, &strIP, &ulIP, &wPort) == TRUE);
    CHECK(strIP == "127.0.0.1" && ulIP == inet_addr("127.0.0.1") && wPort == g_wPort);
    CHECK(g_pEng->GetSessionPeerAddress(49, &strIP, &ulIP, &wPort) == FALSE);
    BYTE mac[6];
    CHECK(g_pEng->GetSessionLocalMacAddress(dwClient, NULL) == FALSE);
    CHECK(g_pEng->GetSessionLocalMacAddress(49, mac) == FALSE);
    // loopback belongs to no Ethernet adapter
    CHECK(g_pEng->GetSessionLocalMacAddress(dwClient, mac) == FALSE);

    // a plain handshake: no packet security at all
    Handshake(dwServer, dwClient, SECURITY_OPT_NONE);
    CHECK(pServer->m_pSocket->m_bHandshakeState == 0x06);
    CHECK(pServer->m_pSocket->m_Security.bOptions == SECURITY_OPT_NONE);
    CHECK(pClient->m_pSocket->m_Security.bOptions == SECURITY_OPT_NONE);

    Exchange(dwClient, dwServer, OP_TEST, 0x11111111, 100, FALSE);
    Exchange(dwServer, dwClient, OP_REPLY, 0x22222222, 3000, FALSE);
    Exchange(dwClient, dwServer, OP_TEST, 0x33333333, 4000, FALSE);

    // a free slot: 0x8009, the message stays with the caller
    CMsg* p = NewMsg(OP_TEST, 1, 1, FALSE);
    CHECK(g_pEng->SendMsg(49, p) == SOCK_ERR_NULL && p->m_lRefCount == 1);
    // IDs past the slot vector throw (std::vector subscript check)
    BOOL bThrown = FALSE;
    try
    {
        g_pEng->SendMsg(100000, p);
    }
    catch (std::out_of_range&)
    {
        bThrown = TRUE;
    }
    CHECK(bThrown);
    g_pEng->ReleaseMsg(p);

    // massive messages over the engine
    g_pBSNet = g_pEng;
    static BYTE data[10000];
    for (int i = 0; i < 10000; ++i)
        data[i] = (BYTE)(i * 13);
    CMassiveMsg* pOut = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pOut->SetOpcode(0x3013);
    pOut->Write(data, 1000);
    for (int i = 1000; i < 10000; i += 1000)
        pOut->Write(data + i, 1000);
    WORD wParts = (WORD)pOut->m_MsgList.size();
    pOut->SendTo(dwClient, FALSE);

    CMsg* pHeader = WaitMsg(MASSIVE_MSG_OPCODE, dwServer);
    CHECK(pHeader != NULL);
    if (pHeader)
    {
        BYTE bType = 0;
        WORD wCount = 0;
        WORD wOpcode = 0;
        *pHeader >> bType >> wCount >> wOpcode;
        CHECK(bType == 1 && wCount == wParts && wOpcode == 0x3013);
        g_pEng->ReleaseMsg(pHeader);
        CMassiveMsg* pIn = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
        pIn->SetRecvHeader(wOpcode, wCount);
        BOOL bComplete = FALSE;
        for (WORD i = 0; i < wCount && !bComplete; ++i)
        {
            CMsg* pPart = WaitMsg(MASSIVE_MSG_OPCODE, dwServer);
            CHECK(pPart != NULL);
            if (!pPart)
                break;
            bComplete = pIn->AddRecvPart(pPart);    // takes its own reference
            g_pEng->ReleaseMsg(pPart);
        }
        CHECK(bComplete);
        pIn->BeginRead();
        static BYTE back[10000];
        for (int i = 0; i < 10000; i += 1000)
            pIn->Read(back + i, 1000);
        CHECK(memcmp(back, data, sizeof(data)) == 0);
        pIn->Free();
    }

    // DumpMsgPool: one call per opcode in use plus the total
    g_vecDump.clear();
    g_pEng->DumpMsgPool(0, 0);
    CHECK(g_vecDump.empty());
    g_pEng->DumpMsgPool(1, 0);
    CHECK(!g_vecDump.empty() && g_vecDump.back().first == 0xFFFF);
    if (!g_vecDump.empty())
    {
        DWORD dwSum = 0;
        for (size_t i = 0; i + 1 < g_vecDump.size(); ++i)
            dwSum += g_vecDump[i].second;
        CHECK(dwSum == g_vecDump.back().second);
        CHECK(g_vecDump.back().second == g_pEng->m_MsgPool.m_setInUse.size());
    }

    // statistics
    NETENGINE_STATISTICS st;
    memset(&st, 0xEE, sizeof(st));
    g_pEng->GetStatistics(0x3F, &st);
    CHECK(st.dwMsgTotal == g_pEng->m_MsgPool.m_nCapacity && st.dwMsgTotal >= 3000);
    CHECK(st.dwMsgUsed == g_pEng->m_MsgPool.m_nInUse);
    CHECK(st.dwMsgFree == st.dwMsgTotal - st.dwMsgUsed);
    CHECK(st.dw0C[0] == 0xEEEEEEEE);
    // NET-002: only the accepted socket pool shows up (the connecting one holds the client)
    CHECK(st.dwSocketTotal == 5000 && st.dwSocketUsed == g_pEng->m_AcceptPool.m_nInUse);
    CHECK(g_pEng->m_ConnectPool.m_nInUse == 1 && g_pEng->m_ConnectPool.m_nCapacity == 50);
    CHECK(st.dwSessionTotal == 500 && st.dwSessionUsed == 2 && st.dwSessionFree == 498);
    CHECK(st.dwFileCacheLimit == 0 && st.dwOpenFiles == 0);
    CHECK(st.dwAcceptCount == 10 && st.dwPendingAccepts == (DWORD)CSockListener::ms_nPendingAccepts);
    CHECK(st.dwLinkedSessions == 2);

    // the client closes: both sides report it; the accepted socket is recycled for AcceptEx
    LONG nPending = CSockListener::ms_nPendingAccepts;
    CHECK(g_pEng->DisconnectSession(dwClient, 7) == 1);
    DWORD dwParam = 99;
    CHECK(WaitEvent(SESSION_EVENT_CLOSED, dwClient, &dwParam) && dwParam == 0);
    CHECK(g_pEng->m_SessionTable.FindSession(dwClient) == NULL);
    CHECK(WaitEvent(SESSION_EVENT_CLOSED, dwServer, &dwParam));
    // the client's abortive close fails the server's pending receive (proactor reason 3); the
    // accepted socket is then disconnected with that reason and closed with reason 6
    CHECK(dwParam == 3);
    CHECK(WaitCondition(SessionGone, (void*)(DWORD_PTR)dwServer));
    CHECK(CSockListener::ms_nPendingAccepts == nPending + 1);
    CHECK(g_pEng->m_ConnectPool.m_nInUse == 0);
    CHECK(g_pEng->DisconnectSession(0, 1) == 1);
    CHECK(g_pEng->DisconnectSession(dwClient, 1) == 1);     // already gone
}

static void TestKeyExchangeAndServerClose()
{
    g_pEng->SetSecurityMode(SECURITY_MODE_KEY_EXCHANGE);
    CHECK(g_pEng->m_Config.bSecurityMode == SECURITY_MODE_KEY_EXCHANGE);

    DWORD dwClient = 0;
    DWORD dwServer = 0;
    CHECK(ConnectPair(g_wPort, &dwClient, &dwServer));
    if (!dwServer)
        return;

    Handshake(dwServer, dwClient, SECURITY_OPT_BLOWFISH | SECURITY_OPT_COUNT_CRC);
    CSockStream* pServerSock = g_pEng->m_SessionTable.FindSession(dwServer)->m_pSocket;
    CSockStream* pClientSock = g_pEng->m_SessionTable.FindSession(dwClient)->m_pSocket;
    CHECK(pServerSock->m_bHandshakeState == 0x1E);
    CHECK(pClientSock->m_KeyRecipient.m_dwSecret == pServerSock->m_KeySender.m_dwSecret);

    Exchange(dwClient, dwServer, OP_TEST, 0x55555555, 2000, TRUE);
    Exchange(dwServer, dwClient, OP_REPLY, 0x66666666, 4000, TRUE);
    Exchange(dwClient, dwServer, OP_TEST, 0x77777777, 10, FALSE);

    // the server closes: the accepted socket gets a TransmitFile disconnect first and the
    // session ends once it completes (reason 6); its close event carries the reason given -
    // or 3 when the client's abortive close fails the pending receive first (NET-013)
    CHECK(g_pEng->DisconnectSession(dwServer, 5) == 1);
    DWORD dwParam = 0;
    CHECK(WaitEvent(SESSION_EVENT_CLOSED, dwServer, &dwParam));
    CHECK(dwParam == 5 || dwParam == 3);
    CHECK(WaitEvent(SESSION_EVENT_CLOSED, dwClient, &dwParam));
    CHECK(WaitCondition(SessionGone, (void*)(DWORD_PTR)dwClient));
    g_pEng->SetSecurityMode(SECURITY_MODE_BASIC);
}

static void TestFileTransfer()
{
    DWORD dwClient = 0;
    DWORD dwServer = 0;
    CHECK(ConnectPair(g_wPort, &dwClient, &dwServer));
    if (!dwServer)
        return;

    // basic security: Blowfish and count/CRC with fixed keys, no key exchange
    Handshake(dwServer, dwClient, SECURITY_OPT_BLOWFISH | SECURITY_OPT_COUNT_CRC);
    CHECK(g_pEng->m_SessionTable.FindSession(dwServer)->m_pSocket->m_bHandshakeState == 0x06);
    Exchange(dwClient, dwServer, OP_TEST, 0x44444444, 50, TRUE);
    Exchange(dwServer, dwClient, OP_REPLY, 0x55555555, 1000, TRUE);
    Exchange(dwServer, dwClient, OP_REPLY, 0x66666666, 10, FALSE);

    const DWORD dwSize = 25000;
    std::string strData;
    for (DWORD i = 0; i < dwSize; ++i)
        strData += (char)('a' + i % 23);
    std::string strIn = g_strDir + "send.bin";
    std::string strOutDir = g_strDir + "recv\\sub\\";
    std::string strOut = strOutDir + "copy.bin";
    WriteDiskFile(strIn, (const BYTE*)strData.data(), dwSize);

    CHECK(g_pEng->SendFile(49, strIn, 0, -1) == FALSE);
    CHECK(HasLog("Session\xC0\xBB \xC3\xA3\xC0\xBB \xBC\xF6 \xBE\xF8\xB4\xD9!"));
    CHECK(g_pEng->BeginSessionFileTransfer(49, strOut, dwSize, 0, -1) == FALSE);
    CHECK(g_pEng->BeginSessionFileTransfer(dwServer, strOut, 0, 0, -1) == FALSE);  // no size
    // an unknown file type fails the transfer
    CHECK(g_pEng->BeginSessionFileTransfer(dwServer, strOut, dwSize, 0, 7) == FALSE);
    DWORD dwParam = 99;
    CHECK(WaitEvent(SESSION_EVENT_FILE_FAILED, dwServer, &dwParam) && dwParam == 0);

    // the receiver creates the missing directories and writes every 0x1001 block
    CHECK(g_pEng->BeginSessionFileTransfer(dwServer, strOut, dwSize, 0, -1) == TRUE);
    CSession* pServer = g_pEng->m_SessionTable.FindSession(dwServer);
    CHECK(pServer->m_pFile != NULL && pServer->m_pFile->GetMode() == 1);
    // a session with a transfer file open cannot send one
    CHECK(g_pEng->SendFile(dwServer, strIn, 0, -1) == FALSE);
    CHECK(HasLog("CSession::SendFile() - \xC4\xCA m_pFile"));
    CHECK(g_pEng->SendFile(dwClient, strIn, 0, -1) == TRUE);

    DWORD dwServerFile = 0;
    DWORD dwClientFile = 0;
    CHECK(WaitEvent(SESSION_EVENT_FILE_DONE, dwServer, &dwServerFile));
    CHECK(WaitEvent(SESSION_EVENT_FILE_DONE, dwClient, &dwClientFile));
    int nProgress = 0;
    DWORD dwLast = 0;
    while (WaitEvent(SESSION_EVENT_FILE_PROGRESS, dwServer, &dwParam, NULL, 200))
    {
        CHECK(dwParam > dwLast && dwParam <= dwSize);
        dwLast = dwParam;
        ++nProgress;
    }
    CHECK(nProgress > 0 && dwLast == dwSize);
    CHECK(pServer->m_pFile == NULL && pServer->m_dwTransferred == 0);

    // both files stay registered for context 0 until closed through the engine
    CFileSystem* pFS = CFileSystem::GetInstance();
    IFile* pRecvFile = (IFile*)(DWORD_PTR)dwServerFile;
    IFile* pSentFile = (IFile*)(DWORD_PTR)dwClientFile;
    CHECK(pFS->GetFile(pRecvFile->GetFileHandle()) == pRecvFile);
    CHECK(pRecvFile->GetUserCount() == 1 && pSentFile->GetUserCount() == 1);
    NETENGINE_STATISTICS st;
    g_pEng->GetStatistics(BSNET_STAT_FILES, &st);
    CHECK(st.dwOpenFiles == 2 && st.dwFileUsers == (DWORD)pFS->m_nOpenCount);
    CHECK(g_pEng->CloseSessionTransferFile(0, pRecvFile->GetFileHandle()) == 0);
    CHECK(g_pEng->CloseSessionTransferFile(0, pSentFile->GetFileHandle()) == 0);
    CHECK(ReadDiskFile(strOut) == strData);

    // resuming at an offset only moves the counter (NET-008)
    CHECK(g_pEng->BeginSessionFileTransfer(dwServer, strOut, dwSize, 100, -1) == TRUE);
    CHECK(pServer->m_dwTransferred == 100 && pServer->m_pFile->GetPosition(dwServer) == 0);
    CHECK(g_pEng->CloseSessionTransferFile(dwServer, 0) == 0);
    CHECK(pServer->m_pFile == NULL);

    CHECK(g_pEng->DisconnectSession(dwClient, 1) == 1);
    CHECK(WaitCondition(SessionGone, (void*)(DWORD_PTR)dwServer));
    DeleteFileA(strIn.c_str());
    DeleteFileA(strOut.c_str());
    RemoveDirectoryA((g_strDir + "recv\\sub").c_str());
    RemoveDirectoryA((g_strDir + "recv").c_str());
}

static BOOL KeepAliveConnected(void* pArg)
{
    CSingleLockBS lock(&g_pEng->m_csKeepAlive, TRUE);
    KeepAliveList& l = g_pEng->m_listKeepAlive;
    return !l.empty() && l.front().dwSessionID != 0
        && l.front().dwSessionID != (DWORD)(DWORD_PTR)pArg;
}

static void TestKeepAlive()
{
    DrainQueue();
    // the maintainer connects the target on its next tick
    g_pEng->AddKeepAliveTarget("127.0.0.1", g_wPort, NULL, APP_TASK, 0);
    CHECK(g_pEng->m_listKeepAlive.size() == 1);
    CHECK(WaitCondition(KeepAliveConnected, NULL));
    DWORD dwFirst = g_pEng->m_listKeepAlive.front().dwSessionID;
    DWORD dwRole = 99;
    CHECK(WaitEvent(SESSION_EVENT_CONNECTED, dwFirst, &dwRole) && dwRole == 1);

    // closing the connected session clears the entry and the maintainer reconnects
    CHECK(g_pEng->DisconnectSession(dwFirst, 1) == 1);
    CHECK(WaitCondition(KeepAliveConnected, (void*)(DWORD_PTR)dwFirst));
    DWORD dwSecond = g_pEng->m_listKeepAlive.front().dwSessionID;
    CHECK(WaitEvent(SESSION_EVENT_CONNECTED, dwSecond, &dwRole) && dwRole == 1);
    CHECK(g_pEng->RemoveKeepAliveTarget(dwSecond) == TRUE);
    CHECK(g_pEng->RemoveKeepAliveTarget(dwSecond) == FALSE);
    CHECK(g_pEng->m_listKeepAlive.empty());

    // ConnectSockAddr with keep-alive 1 adds an entry for the new session
    sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(g_wPort);
    to.sin_addr.s_addr = inet_addr("127.0.0.1");
    DWORD dwThird = g_pEng->ConnectSockAddr((sockaddr*)&to, NULL, 0, 1);
    CHECK(dwThird != 0);
    CHECK(g_pEng->m_listKeepAlive.size() == 1
          && g_pEng->m_listKeepAlive.front().dwSessionID == dwThird
          && g_pEng->m_listKeepAlive.front().dwTaskID == APP_TASK);
    CHECK(g_pEng->RemoveKeepAliveTarget(dwThird) == TRUE);

    // a target nobody listens on: logged once it failed more than 15 times in a row
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in free;
    memset(&free, 0, sizeof(free));
    free.sin_family = AF_INET;
    free.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(s, (sockaddr*)&free, sizeof(free));
    int nLen = sizeof(free);
    getsockname(s, (sockaddr*)&free, &nLen);
    closesocket(s);
    {
        CSingleLockBS lock(&g_pEng->m_csKeepAlive, TRUE);
        g_pEng->AddKeepAliveTarget("127.0.0.1", ntohs(free.sin_port), NULL, APP_TASK, 0);
        g_pEng->m_listKeepAlive.front().nFailCount = 15;
    }
    char szLog[128];
    sprintf_s(szLog, "cannot establish keep alive session : 127.0.0.1 %d (bind 0.0.0.0)",
              ntohs(free.sin_port));
    DWORD dwStart = GetTickCount();
    while (!HasLog(szLog) && GetTickCount() - dwStart < 10000)
        Sleep(20);
    CHECK(HasLog(szLog));
    CHECK(g_pEng->RemoveKeepAliveTarget(0) == TRUE);    // the first entry without a session

    // TryKeepAliveConnect as the maintainer runs it, on an empty list
    NetEngine_KeepAliveTryConnect(g_pEng);

    CHECK(g_pEng->DisconnectSession(dwSecond, 1) == 1);
    CHECK(g_pEng->DisconnectSession(dwThird, 1) == 1);
    CHECK(WaitCondition(NoSessions, NULL));
}

static void TestCleanupAndReap()
{
    DrainQueue();
    DWORD c1, s1, c2, s2;
    CHECK(ConnectPair(g_wPort, &c1, &s1));
    CHECK(ConnectPair(g_wPort, &c2, &s2));
    if (!s1 || !s2)
        return;

    // CleanupDisconnectedSessions closes every untrusted session with an accepted socket
    g_pEng->ExcludeSessionFromCleanup(s1);
    CHECK(g_pEng->m_SessionTable.FindSession(s1)->m_bTrusted == TRUE);
    g_pEng->CleanupDisconnectedSessions();
    CHECK(g_pEng->m_SessionTable.FindSession(s2) == NULL);
    CHECK(g_pEng->m_SessionTable.FindSession(s1) != NULL);
    CHECK(g_pEng->m_SessionTable.FindSession(c1) != NULL);
    DWORD dwParam = 99;
    CHECK(WaitEvent(SESSION_EVENT_CLOSED, s2, &dwParam));
    CHECK(WaitEvent(SESSION_EVENT_CLOSED, c2, &dwParam));
    CHECK(WaitCondition(SessionGone, (void*)(DWORD_PTR)c2));

    // reaping with a 0 ms limit: the connected session closes at once, the trusted accepted
    // one never expires (it is the one session left linked when the reaper returns)
    Sleep(20);
    int nLinked = g_pEng->m_SessionTable.ReapExpiredSessions(0);
    CHECK(g_pEng->m_SessionTable.FindSession(c1) == NULL);
    CHECK(nLinked == 1);
    CHECK(WaitEvent(SESSION_EVENT_CLOSED, c1, &dwParam) && dwParam == 0);

    // the trusted session is reaped once its socket is gone: the peer closed, so the engine
    // closes it (reason 6 after the disconnect)
    CHECK(WaitCondition(SessionGone, (void*)(DWORD_PTR)s1));
    CHECK(WaitEvent(SESSION_EVENT_CLOSED, s1, &dwParam));
    CHECK(WaitCondition(NoSessions, NULL));
}

static void TestDatagram()
{
    CHECK(g_pEng->RegisterDatagramProactor(APP_TASK) == TRUE);
    CHECK(g_pEng->m_Datagram.dwTaskID == APP_TASK);
    CHECK(g_pEng->GetProactor(NETENGINE_TASK_DATAGRAM) != NULL);
    CHECK(g_pEng->OpenDatagramSocket(0) == 0);
    CSockDatagram* pSock = g_pEng->m_Datagram.pSocket;
    CHECK(pSock != NULL && pSock->m_pTask == g_pAppTask);
    if (!pSock)
        return;

    sockaddr_in a;
    pSock->GetLocalAddr(&a);
    a.sin_addr.s_addr = inet_addr("127.0.0.1");
    CMsg* p = NewMsg(0x7123, 0xABCDEF01, 20, FALSE);
    CHECK(g_pEng->SendDatagram((sockaddr*)&a, p) == 0);
    CMsg* pIn = WaitMsg(0x7123, 0);
    CHECK(pIn != NULL);
    if (pIn)
    {
        DWORD dw = 0;
        *pIn >> dw;
        CHECK(dw == 0xABCDEF01 && pIn->GetUnreadSize() == 20);
        g_pEng->ReleaseMsg(pIn);
    }
}

static void TestMisc()
{
    g_pEng->SetDebugAsserted(1);
    CHECK(g_bBSDebugAsserted == 1);
    g_pEng->SetDebugAsserted(0);
    CHECK(g_pEng->GetMsgPool() == &g_pEng->m_MsgPool);
    g_pEng->CloseAllFiles();
    CHECK(CFileSystem::GetInstance()->m_mapOpenFile.empty());
    CHECK(g_pEng->CloseSessionTransferFile(0, 12345) == 0);     // unknown handle

    // AllocMsg / ReleaseMsg: references, the encrypted flag, the pool's in-use set
    LONG nInUse = g_pEng->m_MsgPool.m_nInUse;
    CMsg* p = g_pEng->AllocMsg(TRUE);
    CHECK(p->m_lRefCount == 1 && (*p->m_pSize & MSG_ENCRYPTED) && p->m_lInUse == 1);
    CHECK(g_pEng->m_MsgPool.m_setInUse.count(p) == 1);
    InterlockedIncrement(&p->m_lRefCount);
    g_pEng->ReleaseMsg(p);
    CHECK(p->m_lRefCount == 1 && g_pEng->m_MsgPool.m_nInUse == (DWORD)nInUse + 1);
    g_pEng->ReleaseMsg(p);
    CHECK(p->m_lRefCount == 0 && p->m_lInUse == 0);
    g_pEng->ReleaseMsg(p);                      // already released: ignored
    CHECK(g_pEng->m_MsgPool.m_nInUse == (DWORD)nInUse);
}

int main()
{
    InitializeCriticalSection(&g_csLog);
    char szTemp[MAX_PATH];
    GetTempPathA(MAX_PATH, szTemp);
    char szDir[MAX_PATH];
    sprintf_s(szDir, "%sNetEngineTest_%lu\\", szTemp, GetCurrentProcessId());
    CreateDirectoryA(szDir, NULL);
    g_strDir = szDir;

    g_pEng = new CNetEngine;
    TestConstruction();
    TestClassLink();
    TestSessionTableAlone();
    TestProactors();
    TestInitialize();
    TestListen();
    TestDatagram();
    DrainQueue();

    // every message the engine handed out comes back
    DWORD dwBaseline = g_pEng->m_MsgPool.m_nInUse;

    TestSessionsAndMessages();
    TestKeyExchangeAndServerClose();
    TestFileTransfer();
    TestKeepAlive();
    TestCleanupAndReap();
    TestMisc();

    CHECK(WaitCondition(NoSessions, NULL));
    DrainQueue();
    if (g_pEng->m_MsgPool.m_nInUse != dwBaseline)
    {
        printf("in use %u, baseline %u\n", g_pEng->m_MsgPool.m_nInUse, dwBaseline);
        std::set<CMsg*>& s = g_pEng->m_MsgPool.m_setInUse;
        for (std::set<CMsg*>::iterator it = s.begin(); it != s.end(); ++it)
            printf("  op %04X ref %d session %u size %u\n", (*it)->GetOpcode(),
                   (*it)->m_lRefCount, (*it)->m_dwSessionID, (*it)->GetPayloadSize());
    }
    CHECK(g_pEng->m_MsgPool.m_nInUse == dwBaseline);
    CHECK(g_pEng->m_MsgPool.m_setInUse.size() == dwBaseline);
    CHECK(g_pEng->m_ConnectPool.m_nInUse == 0);

    // shutdown: the subsystems are released, messages are no longer returned
    CHECK(g_pEng->Shutdown() == 0);
    CHECK(g_pNetEngine == NULL && g_bNetEngineShutdown == TRUE);
    CHECK(CSystem::GetInstance()->m_pTaskManager == NULL);
    CHECK(CSystem::GetInstance()->m_pThreadManager == NULL);
    CHECK(g_pEng->Shutdown() == 0);
    CMsg* p = g_pEng->AllocMsg(FALSE);
    g_pEng->ReleaseMsg(p);
    CHECK(p->m_lRefCount == 1);

    // NetEngineLog needs an engine (NET-005); the instance is destroyed without one
    delete g_pEng;
    g_pEng = NULL;
    CHECK(RemoveDirectoryA(szDir));
    DeleteCriticalSection(&g_csLog);
    return TEST_RESULT("NetEngineTest");
}
