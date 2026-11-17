#include <winsock2.h>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "BSLib/thread/ServiceObject.h"
#include "BSLib/thread/Task.h"
#include "BSLib/thread/ThreadManager.h"
#include "BSNet/BaseSocket.h"
#include "BSNet/Maintainer.h"
#include "BSNet/Msg.h"
#include "BSNet/NetEngine.h"
#include "BSNet/NetEngineLog.h"
#include "BSNet/PeerProcess.h"
#include "BSNet/Proactor.h"
#include "BSNet/QueMT.h"
#include "BSNet/SockDatagram.h"
#include "BSNet/SockListener.h"
#include "BSNet/SockStream.h"

#include "TestCheck.h"
#include "LoopbackBind.h"

// ---------------------------------------------------------------------------------------------
// Test doubles

// Engine: records released messages and session closes; the listener sits at +0x114 like in
// CNetEngine.
class CFakeNet : public IBSNet
{
public:
    CFakeNet()
    {
        memset(m_pad, 0, sizeof(m_pad));
        m_pListener = NULL;
        InitializeCriticalSection(&m_cs);
    }

    ~CFakeNet()
    {
        DeleteCriticalSection(&m_cs);
    }

    void Clear()
    {
        EnterCriticalSection(&m_cs);
        m_vecReleased.clear();
        m_vecClosedIDs.clear();
        m_vecClosedReasons.clear();
        LeaveCriticalSection(&m_cs);
    }

    int CountReleased(const void* p)
    {
        EnterCriticalSection(&m_cs);
        int n = 0;
        for (size_t i = 0; i < m_vecReleased.size(); ++i)
        {
            if (m_vecReleased[i] == p)
                ++n;
        }
        LeaveCriticalSection(&m_cs);
        return n;
    }

    // NetEngineLog reads the callback at +0x24 like in CNetEngine.
    void SetLog(PFN_NETENGINE_LOG pfn)
    {
        *(PFN_NETENGINE_LOG*)(m_pad + 0x20) = pfn;
    }

    int ClosedCount()
    {
        EnterCriticalSection(&m_cs);
        int n = (int)m_vecClosedIDs.size();
        LeaveCriticalSection(&m_cs);
        return n;
    }

    virtual LONG __stdcall ReleaseMsg(CMsg* p)
    {
        EnterCriticalSection(&m_cs);
        m_vecReleased.push_back(p);
        LeaveCriticalSection(&m_cs);
        return 0;
    }
    virtual int __stdcall DisconnectSession(DWORD dwID, DWORD dwReason)
    {
        EnterCriticalSection(&m_cs);
        m_vecClosedIDs.push_back(dwID);
        m_vecClosedReasons.push_back(dwReason);
        LeaveCriticalSection(&m_cs);
        return 77;
    }

    virtual HRESULT __stdcall QueryInterface(const GUID&, void**) { return 0; }
    virtual ULONG __stdcall AddRef() { return 0; }
    virtual int __stdcall Shutdown() { return 0; }
    virtual BOOL __stdcall Initialize(BSNET_INIT_PARAM) { return 0; }
    virtual DWORD __stdcall ConnectSockAddr(const sockaddr*, const sockaddr*, DWORD, int)
    {
        return 0;
    }
    virtual DWORD __stdcall Connect(const char*, u_short, const char*, DWORD, int) { return 0; }
    virtual BOOL __stdcall ListenSockAddr(const sockaddr*) { return 0; }
    virtual BOOL __stdcall Listen(const char*, u_short) { return 0; }
    virtual BOOL __stdcall RegisterProactor(DWORD, CTask*, CRuntimeClass*, void*) { return 0; }
    virtual int __stdcall vf09(DWORD, DWORD, DWORD, DWORD) { return 0; }
    virtual BOOL __stdcall StartProactorWorkers(DWORD, int, int, unsigned, DWORD,
                                                THREAD_AFFINITY*)
    {
        return 0;
    }
    virtual BOOL __stdcall ProactorResume(DWORD) { return 0; }
    virtual BOOL __stdcall ProactorSuspend(DWORD) { return 0; }
    virtual BOOL __stdcall UnregisterProactor(DWORD) { return 0; }
    virtual void __stdcall GetStatistics(DWORD, NETENGINE_STATISTICS*) {}
    virtual BOOL __stdcall GetSessionPeerAddress(DWORD, std::string*, ULONG*, u_short*)
    {
        return 0;
    }
    virtual CTask* __stdcall GetProactor(DWORD) { return 0; }
    virtual BOOL __stdcall StartHandshake(DWORD, SECURITY_HANDSHAKE_PARAM*) { return 0; }
    virtual CMsg* __stdcall AllocMsg(BOOL) { return 0; }
    virtual int __stdcall SendMsg(DWORD, CMsg*) { return 0; }
    virtual BOOL __stdcall BeginSessionFileTransfer(DWORD, std::string, DWORD, DWORD, int)
    {
        return 0;
    }
    virtual BOOL __stdcall SendFile(DWORD, const std::string&, DWORD, int) { return 0; }
    virtual int __stdcall CloseSessionTransferFile(DWORD, DWORD) { return 0; }
    virtual void __stdcall ExcludeSessionFromCleanup(DWORD) {}
    virtual int __stdcall GetSessionSocketState(DWORD) { return 0; }
    virtual BOOL __stdcall RemoveKeepAliveTarget(DWORD) { return 0; }
    virtual void __stdcall AddKeepAliveTarget(const char*, u_short, const char*, DWORD, DWORD) {}
    virtual void __stdcall CloseAllFiles() {}
    virtual void __stdcall DumpMsgPool(BYTE, DWORD) {}
    virtual void __stdcall SetDebugAsserted(BYTE) {}
    virtual void __stdcall CleanupDisconnectedSessions() {}
    virtual CMsgPool* __stdcall GetMsgPool() { return 0; }
    virtual BOOL __stdcall GetSessionLocalMacAddress(DWORD, BYTE*) { return 0; }
    virtual void __stdcall SetSecurityMode(BYTE) {}
    virtual void __stdcall PrintVersion() {}
    virtual BOOL __stdcall RegisterDatagramProactor(DWORD) { return 0; }
    virtual int __stdcall OpenDatagramSocket(WORD) { return 0; }
    virtual int __stdcall SendDatagram(const sockaddr*, CMsg*) { return 0; }

public:
    BYTE                m_pad[0x110];       // +4
    CSockListener*      m_pListener;        // +114
    CRITICAL_SECTION    m_cs;
    std::vector<void*>  m_vecReleased;
    std::vector<DWORD>  m_vecClosedIDs;
    std::vector<DWORD>  m_vecClosedReasons;
};

C_ASSERT(FIELD_OFFSET(CFakeNet, m_pListener) == 0x114);

static CFakeNet g_Net;

struct FAKE_SESSION
{
    BYTE    pad[0x18];
    DWORD   dwID;       // +18
};

// Socket with CSockStream's layout where the proactors read it.
class CTestSocket : public CBaseSocket
{
public:
    CTestSocket()
    {
        memset(m_pad50, 0, sizeof(m_pad50));
        memset(&m_ov60, 0, sizeof(m_ov60));
        memset(&m_ovA4, 0, sizeof(m_ovA4));
        memset(m_padE8, 0, sizeof(m_padE8));
        m_ov60.pOwner = this;
        m_ov60.dwNotPooled = 1;
        m_ovA4.pOwner = this;
        m_ovA4.dwNotPooled = 1;
        m_pSession = NULL;
        m_nCompletions = 0;
        m_dwLastBytes = 0;
        m_pLastOverlapped = NULL;
        m_nResult = 0;
        m_nLastType = -1;
        m_hDone = CreateEventA(NULL, FALSE, FALSE, NULL);
    }

    virtual ~CTestSocket()
    {
        CloseHandle(m_hDone);
    }

    void CreateEvents()
    {
        m_ov60.ov.hEvent = WSACreateEvent();
        m_ovA4.ov.hEvent = WSACreateEvent();
    }

    void CloseEvents()
    {
        WSACloseEvent(m_ov60.ov.hEvent);
        WSACloseEvent(m_ovA4.ov.hEvent);
    }

    virtual BOOL AttachToProactor(CServiceObjectSock* pProactor, HANDLE hHandle)
    {
        return pProactor->AssociateHandle(hHandle, this);
    }
    virtual int OnIOCompletion(DWORD dwBytes, LPOVERLAPPED pOverlapped)
    {
        m_dwLastBytes = dwBytes;
        m_pLastOverlapped = pOverlapped;
        InterlockedIncrement(&m_nCompletions);
        SetEvent(m_hDone);
        return m_nResult;
    }
    virtual void OnIdle() {}
    virtual SOCK_OVERLAPPED* GetOverlapped(int nType)
    {
        m_nLastType = nType;
        return &m_ov60;
    }

    BOOL WaitDone()
    {
        return WaitForSingleObject(m_hDone, 5000) == WAIT_OBJECT_0;
    }

public:
    DWORD           m_pad50[4];     // +50
    SOCK_OVERLAPPED m_ov60;         // +60
    SOCK_OVERLAPPED m_ovA4;         // +A4
    BYTE            m_padE8[0x54];  // +E8
    FAKE_SESSION*   m_pSession;     // +13C
    volatile LONG   m_nCompletions;
    DWORD           m_dwLastBytes;
    LPOVERLAPPED    m_pLastOverlapped;
    int             m_nResult;
    int             m_nLastType;
    HANDLE          m_hDone;
};

C_ASSERT(FIELD_OFFSET(CTestSocket, m_ov60) == 0x60);
C_ASSERT(FIELD_OFFSET(CTestSocket, m_ovA4) == 0xA4);
C_ASSERT(FIELD_OFFSET(CTestSocket, m_pSession) == 0x13C);

// Runs a job's Run on a plain thread.
struct RUNNER
{
    CServiceObject* pJob;
    volatile LONG   bStop;
    DWORD           dwResult;
    HANDLE          hThread;
};

static DWORD WINAPI RunnerProc(void* p)
{
    RUNNER* r = (RUNNER*)p;
    r->dwResult = r->pJob->Run(&r->bStop, 0);
    return 0;
}

static void StartRunner(RUNNER* r, CServiceObject* pJob)
{
    r->pJob = pJob;
    r->bStop = 0;
    r->dwResult = 0xDEAD;
    DWORD dwID;
    r->hThread = CreateThread(NULL, 0, RunnerProc, r, 0, &dwID);
}

static BOOL JoinRunner(RUNNER* r, DWORD dwTimeout = 5000)
{
    BOOL bOk = WaitForSingleObject(r->hThread, dwTimeout) == WAIT_OBJECT_0;
    if (bOk)
        CloseHandle(r->hThread);
    return bOk;
}

static BOOL WaitValue(volatile LONG* p, LONG n)
{
    for (int i = 0; i < 500; ++i)
    {
        if (*p == n)
            return TRUE;
        Sleep(10);
    }
    return FALSE;
}

static BOOL WaitClosed(int n)
{
    for (int i = 0; i < 500; ++i)
    {
        if (g_Net.ClosedCount() >= n)
            return TRUE;
        Sleep(10);
    }
    return FALSE;
}

static std::string g_strLog;

static int TestLog(DWORD, const char* pszFormat, ...)
{
    char sz[2048];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf_s(sz, sizeof(sz), _TRUNCATE, pszFormat, args);
    va_end(args);
    g_strLog = sz;
    return 0;
}

// Engine jobs the maintainer schedules (defined with CNetEngine in the real build).
static volatile LONG g_nReap = 0;
static volatile LONG g_nKeepAlive = 0;
static DWORD g_dwReapParam = 0;
static IBSNet* g_pJobEngine = NULL;

void NetEngine_ReapExpiredSessions(IBSNet* pEngine, DWORD dwParam)
{
    g_pJobEngine = pEngine;
    g_dwReapParam = dwParam;
    InterlockedIncrement(&g_nReap);
}

void NetEngine_KeepAliveTryConnect(IBSNet* pEngine)
{
    g_pJobEngine = pEngine;
    InterlockedIncrement(&g_nKeepAlive);
}

// Session functions the socket classes reference (not reached by these tests).
DWORD NetEngine_CreateNewSession(IBSNet*, CSockStream*, DWORD)
{
    return 0;
}

void Session_PostHandshakeEvent(CSession*)
{
}

int Session_HandleFileTransferData(CMsg*, CSockStream*)
{
    return 0;
}

BOOL FileTransfer_AccumulateSent(CSession*, DWORD)
{
    return TRUE;
}

void FileTransfer_Finish(CSession*, int)
{
}

static sockaddr_in LoopbackAddr(u_short wPort)
{
    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = wPort;
    return a;
}

static BOOL MakeTcpPair(SOCKET* pClient, SOCKET* pServer)
{
    SOCKET l = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    sockaddr_in a = LoopbackAddr(0);
    int nLen = sizeof(a);
    if (bind(l, (sockaddr*)&a, sizeof(a)) || listen(l, 1) || getsockname(l, (sockaddr*)&a, &nLen))
        return FALSE;
    *pClient = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (connect(*pClient, (sockaddr*)&a, sizeof(a)))
        return FALSE;
    *pServer = accept(l, NULL, NULL);
    closesocket(l);
    return *pServer != INVALID_SOCKET;
}

static SOCKET MakeUdp(sockaddr_in* pAddr)
{
    SOCKET s = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
    sockaddr_in a = LoopbackAddr(0);
    int nLen = sizeof(a);
    bind(s, (sockaddr*)&a, sizeof(a));
    getsockname(s, (sockaddr*)&a, &nLen);
    *pAddr = a;
    return s;
}

static void ResetConnection(SOCKET s)
{
    linger lg;
    lg.l_onoff = 1;
    lg.l_linger = 0;
    setsockopt(s, SOL_SOCKET, SO_LINGER, (const char*)&lg, sizeof(lg));
    closesocket(s);
}

static BOOL PostRecv(SOCKET s, BS_OVERLAPPED* pOv, char* pBuf, int nLen)
{
    WSABUF wb;
    wb.buf = pBuf;
    wb.len = nLen;
    DWORD dwBytes = 0;
    DWORD dwFlags = 0;
    WSAEVENT hEvent = pOv->ov.hEvent;
    memset(&pOv->ov, 0, sizeof(pOv->ov));
    pOv->ov.hEvent = hEvent;
    int n = WSARecv(s, &wb, 1, &dwBytes, &dwFlags, &pOv->ov, NULL);
    return n == 0 || WSAGetLastError() == WSA_IO_PENDING;
}

static CBase* __cdecl FaultingFactory()
{
    *(volatile int*)0 = 1;
    return NULL;
}

// ---------------------------------------------------------------------------------------------

static void TestRuntimeClass()
{
    CRuntimeClass* p = &CProactorIOCP::classCProactorIOCP;
    CHECK(strcmp(p->m_lpszClassName, "CProactorIOCP") == 0);
    CHECK(p->m_nObjectSize == 0x54);
    CHECK(p->m_pBaseClass == &CServiceObjectSock::classCServiceObjectSock);
    CHECK(CProactorEvent::classCProactorEvent.m_nObjectSize == 0x25C);
    CHECK(CPeerProcessIOCP::classCPeerProcessIOCP.m_nObjectSize == 0x54);
    CHECK(CPeerProcessOverlapped::classCPeerProcessOverlapped.m_nObjectSize == 0x254);
    CHECK(CMaintainer::classCMaintainer.m_nObjectSize == 0x84);
    CHECK(CMaintainer::classCMaintainer.m_pBaseClass == &CServiceObject::classCServiceObject);
    CHECK(CBase::classCBase.m_nObjectSize == 4);
    CHECK(CServiceObject::classCServiceObject.m_nObjectSize == 0x4C);
    CHECK(CServiceObject::classCServiceObject.m_pfnCreateObject == NULL);

    // The abstract records do not link to their bases.
    CHECK(p->IsDerivedFrom(p));
    CHECK(p->IsDerivedFrom(&CServiceObjectSock::classCServiceObjectSock));
    CHECK(!p->IsDerivedFrom(&CServiceObject::classCServiceObject));
    CHECK(!p->IsDerivedFrom(&CMaintainer::classCMaintainer));

    CBase* pObj = p->CreateObject();
    CHECK(pObj != NULL);
    CHECK(pObj->GetRuntimeClass() == p);
    CHECK(((CProactorIOCP*)pObj)->m_hPort != NULL);
    p->m_pfnDeleteObject(pObj);
    p->m_pfnDeleteObject(NULL);

    CHECK(CServiceObject::classCServiceObject.CreateObject() == NULL);
    CRuntimeClass bad = { "Bad", 4, FaultingFactory, NULL, NULL };
    CHECK(bad.CreateObject() == NULL);       // the access violation is swallowed

    CMaintainer m;
    CHECK(m.GetRuntimeClass() == &CMaintainer::classCMaintainer);
    CHECK(m.m_nActive == 0);
}

static void TestProactorIOCP()
{
    g_pNetEngine = &g_Net;
    g_Net.Clear();
    g_Net.m_pListener = NULL;

    CProactorIOCP* p = new CProactorIOCP;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    CHECK(p->m_hPort != NULL);
    CHECK(p->m_dwConcurrency == 2 * si.dwNumberOfProcessors);
    CHECK(p->m_nActive == 0);
    CHECK(p->Initialize((void*)1, (void*)2) == TRUE);
    CHECK(p->GetOwner() == (void*)1 && p->m_pParam == (void*)2);
    CHECK(p->GetSendQueue() == NULL);
    CHECK(p->SignalStop() == 0);             // nothing running

    // One worker keeps the completions in order.
    RUNNER r;
    StartRunner(&r, p);
    CHECK(WaitValue(&p->m_nActive, 1));

    FAKE_SESSION session;
    memset(&session, 0, sizeof(session));
    session.dwID = 0x1234;
    CTestSocket sock;
    sock.m_pSession = &session;
    sock.m_nKind = 1;
    CTestSocket sync;
    sync.m_nKind = 1;

    // plain completion of a socket block: dispatched, nothing released
    BS_OVERLAPPED ov;
    memset(&ov, 0, sizeof(ov));
    ov.dwNotPooled = 1;
    PostQueuedCompletionStatus(p->m_hPort, 123, (ULONG_PTR)&sock, &ov.ov);
    CHECK(sock.WaitDone());
    CHECK(sock.m_dwLastBytes == 123 && sock.m_pLastOverlapped == &ov.ov);

    // pooled CMsg, handler done with it: released once
    CMsg msg;
    CHECK(msg.m_dw28 == 0);
    sock.m_nResult = 1;
    PostQueuedCompletionStatus(p->m_hPort, 7, (ULONG_PTR)&sock, &msg.m_Overlapped);
    CHECK(sock.WaitDone());
    PostQueuedCompletionStatus(p->m_hPort, 0, (ULONG_PTR)&sync, &ov.ov);
    CHECK(sync.WaitDone());
    CHECK(g_Net.CountReleased(&msg) == 1);
    CHECK(g_Net.ClosedCount() == 0);

    // result 0 keeps the message
    sock.m_nResult = 0;
    PostQueuedCompletionStatus(p->m_hPort, 7, (ULONG_PTR)&sock, &msg.m_Overlapped);
    CHECK(sock.WaitDone());
    PostQueuedCompletionStatus(p->m_hPort, 0, (ULONG_PTR)&sync, &ov.ov);
    CHECK(sync.WaitDone());
    CHECK(g_Net.CountReleased(&msg) == 1);

    // bit 0x8000: closed with reason LOWORD << 16 (no listener: straight to the engine)
    sock.m_nResult = 0x8005;
    PostQueuedCompletionStatus(p->m_hPort, 1, (ULONG_PTR)&sock, &ov.ov);
    CHECK(sock.WaitDone());
    CHECK(WaitClosed(1));
    CHECK(g_Net.m_vecClosedIDs[0] == 0x1234);
    CHECK(g_Net.m_vecClosedReasons[0] == 0x80050000);

    // I/O type 3: closed with reason 6, not dispatched
    LONG nBefore = sock.m_nCompletions;
    BS_OVERLAPPED ov3;
    memset(&ov3, 0, sizeof(ov3));
    ov3.dwIoType = 3;
    ov3.dwNotPooled = 1;
    PostQueuedCompletionStatus(p->m_hPort, 1, (ULONG_PTR)&sock, &ov3.ov);
    CHECK(WaitClosed(2));
    CHECK(g_Net.m_vecClosedReasons[1] == 6);
    CHECK(sock.m_nCompletions == nBefore);

    // with a listener: state 2 is ignored, other states go through it to the engine
    CTestSocket listener;
    // only the listener's lock is used
    g_Net.m_pListener = (CSockListener*)(CBaseSocket*)&listener;
    sock.m_nKind = 2;
    CHECK(p->CloseHandler(&sock, 9) == 1);
    CHECK(g_Net.ClosedCount() == 2);
    sock.m_nKind = 1;
    CHECK(p->CloseHandler(&sock, 9) == 77);
    CHECK(g_Net.ClosedCount() == 3 && g_Net.m_vecClosedReasons[2] == 9);
    g_Net.m_pListener = NULL;
    sock.m_pSession = NULL;
    CHECK(p->CloseHandler(&sock, 4) == 77);
    CHECK(g_Net.m_vecClosedIDs[3] == 0);     // no session: ID 0

    // handle registration
    g_Net.SetLog(TestLog);
    g_strLog.clear();
    SOCKET c, s;
    CHECK(MakeTcpPair(&c, &s));
    CHECK(p->AssociateHandle((HANDLE)s, &sock) == TRUE);
    CHECK(p->AssociateHandle((HANDLE)s, &sock) == TRUE);    // again: ERROR_INVALID_PARAMETER
    CHECK(g_strLog.empty());
    CHECK(p->AssociateHandle((HANDLE)(DWORD_PTR)0x7FFFFFF0, &sock) == FALSE);
    CHECK(g_strLog.find("CreateIoCompletionPort(") != std::string::npos);
    CHECK(g_strLog.find("[LastError: 6]") != std::string::npos);

    // PROACT-001: a failed receive (connection reset) on a pooled block is released before the
    // dispatch and again after it
    g_Net.Clear();
    session.dwID = 0x99;
    sock.m_pSession = &session;
    sock.m_nKind = 1;
    sock.m_nResult = 1;
    BS_OVERLAPPED ovRecv;
    memset(&ovRecv, 0, sizeof(ovRecv));
    char buf[16];
    CHECK(PostRecv(s, &ovRecv, buf, sizeof(buf)));
    ResetConnection(c);
    CHECK(sock.WaitDone());
    PostQueuedCompletionStatus(p->m_hPort, 0, (ULONG_PTR)&sync, &ov.ov);
    CHECK(sync.WaitDone());
    CHECK(sock.m_pLastOverlapped == &ovRecv.ov);
    CHECK(g_Net.CountReleased(&ovRecv) == 2);
    closesocket(s);

    // failed completion of a socket in state 0: released, closed with reason 3, not dispatched
    CHECK(MakeTcpPair(&c, &s));
    CHECK(p->AssociateHandle((HANDLE)s, &sock) == TRUE);
    g_Net.Clear();
    sock.m_nKind = 0;
    nBefore = sock.m_nCompletions;
    CHECK(PostRecv(s, &ovRecv, buf, sizeof(buf)));
    ResetConnection(c);
    CHECK(WaitClosed(1));
    CHECK(g_Net.m_vecClosedIDs[0] == 0x99 && g_Net.m_vecClosedReasons[0] == 3);
    CHECK(g_Net.CountReleased(&ovRecv) == 1);
    CHECK(sock.m_nCompletions == nBefore);
    closesocket(s);

    // shutdown: one packet per running worker
    CHECK(p->SignalStop() == TRUE);
    CHECK(JoinRunner(&r));
    CHECK(r.dwResult == 0);
    CHECK(p->m_nActive == 0);

    // a raised stop flag returns before touching the counter
    r.bStop = 1;
    CHECK(p->Run(&r.bStop, 0) == 0);

    p->Close();
    CHECK(p->m_hPort == NULL);
    delete p;
    g_Net.SetLog(NULL);
}

static void TestProactorEventDispatch()
{
    g_pNetEngine = &g_Net;
    g_Net.Clear();
    g_Net.m_pListener = NULL;

    CProactorEvent* p = new CProactorEvent;
    CHECK(p->m_nHandles == 2);
    CHECK(p->m_ahEvents[0] != NULL && p->m_ahEvents[1] != NULL && p->m_ahEvents[2] == NULL);
    CHECK(p->m_listFreeEvents.size() == 62);
    CHECK(WaitForSingleObject(p->m_ahEvents[1], 0) == WAIT_OBJECT_0);  // wake-up event set
    CHECK(WaitForSingleObject(p->m_ahEvents[0], 0) == WAIT_TIMEOUT);
    WSAResetEvent(p->m_ahEvents[1]);

    RUNNER r;
    StartRunner(&r, p);
    CHECK(WaitValue(&p->m_nActive, 1));

    FAKE_SESSION session;
    memset(&session, 0, sizeof(session));
    session.dwID = 0x4321;
    SOCKET c, s;
    CHECK(MakeTcpPair(&c, &s));
    CTestSocket sock;
    sock.CreateEvents();
    sock.m_hSocket = s;
    sock.m_pSession = &session;

    CHECK(sock.AttachToProactor(p, (HANDLE)s) == TRUE);
    CHECK(p->m_nHandles == 4);
    CHECK(p->m_apOverlapped[2] == &sock.m_ovA4 && p->m_apOverlapped[3] == &sock.m_ov60);
    CHECK(p->m_ahEvents[2] == sock.m_ovA4.ov.hEvent);
    CHECK(p->m_ahEvents[3] == sock.m_ov60.ov.hEvent);
    CHECK(p->GetSlotOverlapped(3) == &sock.m_ov60);
    CHECK(p->m_listFreeEvents.size() == 62);

    // data on the socket: the worker fetches the result and dispatches it
    char buf[16];
    CHECK(PostRecv(s, &sock.m_ov60, buf, sizeof(buf)));
    send(c, "hello", 5, 0);
    CHECK(sock.WaitDone());
    CHECK(sock.m_dwLastBytes == 5 && sock.m_pLastOverlapped == &sock.m_ov60.ov);
    CHECK(memcmp(buf, "hello", 5) == 0);
    CHECK(g_Net.ClosedCount() == 0);

    // bit 0x8000: slot pair cleared, session closed with reason 3
    sock.m_nResult = 0x8000;
    CHECK(PostRecv(s, &sock.m_ov60, buf, sizeof(buf)));
    send(c, "abc", 3, 0);
    CHECK(sock.WaitDone());
    CHECK(WaitClosed(1));
    CHECK(g_Net.m_vecClosedIDs[0] == 0x4321 && g_Net.m_vecClosedReasons[0] == 3);
    CHECK(p->m_nHandles == 2);
    CHECK(p->m_ahEvents[2] == NULL && p->m_apOverlapped[3] == NULL);

    // a graceful close (0 bytes) closes the handler as well
    sock.m_nResult = 0;
    CHECK(sock.AttachToProactor(p, (HANDLE)s) == TRUE);
    LONG nBefore = sock.m_nCompletions;
    CHECK(PostRecv(s, &sock.m_ov60, buf, sizeof(buf)));
    shutdown(c, SD_SEND);
    CHECK(WaitClosed(2));
    CHECK(sock.m_nCompletions == nBefore);
    CHECK(p->m_nHandles == 2);

    // clearing an empty slot refills it from the pool (and keeps the old count)
    CHECK(p->CloseHandler(NULL, 11) == 0);
    CHECK(p->m_ahEvents[10] != NULL && p->m_ahEvents[11] != NULL);
    CHECK(p->m_listFreeEvents.size() == 60);
    CHECK(p->m_nHandles == 2);
    // indexes outside 2..63 are ignored
    CHECK(p->CloseHandler(NULL, 1) == 0);
    CHECK(p->m_ahEvents[0] != NULL && p->m_ahEvents[1] != NULL);

    // stop event: the worker returns
    CHECK(p->SignalStop() == 1);
    CHECK(JoinRunner(&r));
    CHECK(r.dwResult == 0 && p->m_nActive == 0);

    // the pool's events are closed by the destructor, the socket's are not
    WSAEVENT hPool = p->m_ahEvents[0];
    CHECK(sock.AttachToProactor(p, (HANDLE)s) == TRUE);
    delete p;
    CHECK(WSASetEvent(sock.m_ov60.ov.hEvent) == TRUE);
    CHECK(WSASetEvent(hPool) == FALSE);
    sock.CloseEvents();
    closesocket(c);
}

static void TestProactorEventHole()
{
    g_pNetEngine = &g_Net;
    g_Net.Clear();

    // PROACT-002: closing the first of two sockets leaves NULL handles in the waited range
    CProactorEvent* p = new CProactorEvent;
    CTestSocket a, b;
    a.CreateEvents();
    b.CreateEvents();
    CHECK(p->AssociateHandle(NULL, &a) == TRUE);
    CHECK(p->AssociateHandle(NULL, &b) == TRUE);
    CHECK(p->m_nHandles == 6 && p->m_apOverlapped[4] == &b.m_ovA4);

    RUNNER r;
    StartRunner(&r, p);
    CHECK(WaitValue(&p->m_nActive, 1));
    p->CloseHandler(&a, 3);
    CHECK(p->m_nHandles == 4);
    CHECK(p->m_ahEvents[2] == NULL && p->m_ahEvents[4] != NULL);
    CHECK(WSAWaitForMultipleEvents(p->m_nHandles, p->m_ahEvents, FALSE, 0, FALSE)
          == WSA_WAIT_FAILED);
    // the worker's next wait fails and it leaves without being stopped
    CHECK(JoinRunner(&r));
    CHECK(p->m_nActive == 0);

    // the slot search fills from 2 upwards; 31 sockets at most
    CProactorEvent* q = new CProactorEvent;
    CTestSocket many[32];
    for (int i = 0; i < 31; ++i)
        CHECK(q->AssociateHandle(NULL, &many[i]) == TRUE);
    CHECK(q->AssociateHandle(NULL, &many[31]) == FALSE);
    CHECK(q->m_apOverlapped[62] == &many[30].m_ovA4);
    CHECK(q->m_nHandles == 2);              // the test sockets have no events
    delete q;

    delete p;
    a.CloseEvents();
    b.CloseEvents();
}

static void TestPeerProcessIOCP()
{
    g_pNetEngine = &g_Net;
    g_Net.Clear();

    CPeerProcessIOCP* p = new CPeerProcessIOCP;
    CHECK(p->m_hPort != NULL && p->m_hPort != INVALID_HANDLE_VALUE);
    CHECK(p->m_nWorkers == 0);
    CHECK(p->Initialize((void*)3, (void*)4) == TRUE && p->GetOwner() == (void*)3);
    CHECK(p->CloseHandler(NULL, 0) == 1);
    CHECK(p->GetSendQueue() == NULL);
    CHECK(p->SignalStop() == 0);

    // the stop flag is ignored
    RUNNER r;
    StartRunner(&r, p);
    r.bStop = 1;
    CHECK(WaitValue(&p->m_nWorkers, 1));
    CHECK(p->m_nActive == 0);

    CTestSocket sock;
    CTestSocket sync;
    BS_OVERLAPPED ov;
    memset(&ov, 0, sizeof(ov));
    ov.dwNotPooled = 1;
    PostQueuedCompletionStatus(p->m_hPort, 11, (ULONG_PTR)&sock, &ov.ov);
    CHECK(sock.WaitDone());
    CHECK(sock.m_dwLastBytes == 11 && sock.m_pLastOverlapped == &ov.ov);

    CMsg msg;
    sock.m_nResult = 1;
    PostQueuedCompletionStatus(p->m_hPort, 2, (ULONG_PTR)&sock, &msg.m_Overlapped);
    CHECK(sock.WaitDone());
    PostQueuedCompletionStatus(p->m_hPort, 0, (ULONG_PTR)&sync, &ov.ov);
    CHECK(sync.WaitDone());
    CHECK(g_Net.CountReleased(&msg) == 1);
    // the close bit means nothing here
    sock.m_nResult = 0x8000;
    PostQueuedCompletionStatus(p->m_hPort, 2, (ULONG_PTR)&sock, &ov.ov);
    CHECK(sock.WaitDone());
    CHECK(g_Net.ClosedCount() == 0);

    SOCKET c, s;
    CHECK(MakeTcpPair(&c, &s));
    CHECK(p->AssociateHandle((HANDLE)s, &sock) == TRUE);
    CHECK(p->AssociateHandle((HANDLE)s, &sock) == TRUE);
    CHECK(p->AssociateHandle((HANDLE)(DWORD_PTR)0x7FFFFFF0, &sock) == FALSE);
    closesocket(c);
    closesocket(s);

    CHECK(p->SignalStop() == TRUE);
    CHECK(JoinRunner(&r));
    CHECK(p->m_nWorkers == 0);
    delete p;
}

static void TestPeerProcessOverlapped()
{
    g_pNetEngine = &g_Net;
    g_Net.Clear();
    g_Net.SetLog(TestLog);

    // stopping without a worker
    {
        CPeerProcessOverlapped pp;
        CHECK(pp.m_pSendQueue == NULL && pp.m_nHandles == 2);
        CHECK(pp.m_ahEvents[0] != NULL && pp.m_ahEvents[1] != NULL);
        CHECK(pp.Initialize((void*)5, (void*)6) == TRUE);
        IQue<CMsg>* pFirst = pp.GetSendQueue();
        CHECK(pFirst != NULL);
        CHECK(pp.Initialize((void*)5, (void*)6) == TRUE);
        CHECK(pp.GetSendQueue() != NULL);
        CHECK(pp.GetOwner() == (void*)5 && pp.m_pParam == (void*)6);
        CMsg m1, m2;
        pp.GetSendQueue()->Push(&m1);
        pp.GetSendQueue()->Push(&m2);
        // PROACT-005: the first Pop after Stop takes one message and returns FALSE, so the
        // drain loop never releases anything
        CHECK(pp.SignalStop() == 1);
        CHECK(g_Net.CountReleased(&m1) == 0 && g_Net.CountReleased(&m2) == 0);
        CHECK(pp.GetSendQueue()->GetSize() == 1);
        CHECK(WaitForSingleObject(pp.m_ahEvents[0], 0) == WAIT_OBJECT_0);
        CHECK(pp.CloseHandler(NULL, 0) == 1);
    }

    CPeerProcessOverlapped* p = new CPeerProcessOverlapped;
    CHECK(p->Initialize(NULL, NULL) == TRUE);

    sockaddr_in aSender, aPeer;
    SOCKET hSender = MakeUdp(&aSender);
    SOCKET hPeer = MakeUdp(&aPeer);
    CTestSocket sock;
    sock.CreateEvents();
    sock.m_hSocket = hSender;
    CHECK(p->AssociateHandle(NULL, &sock) == TRUE);
    CHECK(sock.m_nLastType == 1);
    CHECK(p->m_nHandles == 3);
    CHECK(p->m_ahEvents[2] == sock.m_ov60.ov.hEvent && p->m_apOverlapped[2] == &sock.m_ov60);

    NETENGINE_DATAGRAM dg;
    // the peer process only uses the socket handle
    dg.pSocket = (CSockDatagram*)(CBaseSocket*)&sock;
    dg.dwTaskID = 0;
    g_pNetEngineDatagram = &dg;
    CHECK(NetEngine_GetDatagram() == &dg);

    RUNNER r;
    StartRunner(&r, p);

    // queued datagram: sent from the engine's socket, completion routine reaches the socket
    CMsg msg;
    char szPing[] = "ping";
    msg.m_WsaBuf.buf = szPing;
    msg.m_WsaBuf.len = 4;
    msg.m_Addr = aPeer;
    p->GetSendQueue()->Push(&msg);
    char buf[16];
    sockaddr_in aFrom;
    int nFrom = sizeof(aFrom);
    CHECK(recvfrom(hPeer, buf, sizeof(buf), 0, (sockaddr*)&aFrom, &nFrom) == 4);
    CHECK(memcmp(buf, "ping", 4) == 0 && aFrom.sin_port == aSender.sin_port);
    CHECK(sock.WaitDone());
    CHECK(sock.m_dwLastBytes == 4 && sock.m_pLastOverlapped == &msg.m_Overlapped);
    CHECK(msg.m_Overlapped.hEvent == (HANDLE)&sock);
    CHECK(g_Net.CountReleased(&msg) == 0);

    // receive on the socket's own block: event 2 dispatches it
    WSABUF wb;
    wb.buf = buf;
    wb.len = sizeof(buf);
    DWORD dwBytes = 0;
    DWORD dwFlags = 0;
    int nRet = WSARecvFrom(hSender, &wb, 1, &dwBytes, &dwFlags, NULL, NULL, &sock.m_ov60.ov,
                           NULL);
    CHECK(nRet == 0 || WSAGetLastError() == WSA_IO_PENDING);
    sendto(hPeer, "pong!", 5, 0, (sockaddr*)&aSender, sizeof(aSender));
    CHECK(sock.WaitDone());
    CHECK(sock.m_dwLastBytes == 5 && sock.m_pLastOverlapped == &sock.m_ov60.ov);

    // a send that fails at once goes back to the engine and is logged
    CMsg bad;
    bad.m_WsaBuf.buf = szPing;
    bad.m_WsaBuf.len = 4;
    memset(&bad.m_Addr, 0, sizeof(bad.m_Addr));
    g_strLog.clear();
    p->GetSendQueue()->Push(&bad);
    for (int i = 0; i < 500 && g_Net.CountReleased(&bad) == 0; ++i)
        Sleep(10);
    CHECK(g_Net.CountReleased(&bad) == 1);
    CHECK(g_strLog.find("WSASend() - Unknow NERROR: ") == 0);

    CHECK(p->SignalStop() == 1);
    CHECK(JoinRunner(&r));
    CHECK(r.dwResult == 0);

    delete p;
    g_pNetEngineDatagram = NULL;
    closesocket(hPeer);
    sock.CloseEvents();
    g_Net.SetLog(NULL);
}

static void TestMaintainer()
{
    g_pNetEngine = &g_Net;

    MAINTAINER_CONFIG def;
    CHECK(def.dwWaitTimeout == 1000);
    CHECK(def.aJobs[0].dwPeriod == 60000 && def.aJobs[0].dwParam == 60000);
    CHECK(def.aJobs[1].dwPeriod == (DWORD)-1 && def.aJobs[1].dwParam == 0);
    CHECK(def.aJobs[2].dwPeriod == (DWORD)-1 && def.aJobs[2].dwParam == 0);

    MAINTAINER_CONFIG cfg;
    cfg.dwWaitTimeout = 5;
    cfg.dwFlags = 0x02;
    {
        CMaintainer m;
        CHECK(m.m_hEvent != NULL);
        CHECK(m.m_config.dwWaitTimeout == 1000);
        CHECK(m.Initialize((void*)7, &cfg) == TRUE);
        CHECK(m.m_Scheduler.m_vecSchedules.size() == 0);
        CHECK(m.m_pParam == &cfg && m.GetOwner() == (void*)7);
        CHECK(m.m_config.dwWaitTimeout == 5);
    }

    cfg.dwFlags = 0x05;
    cfg.aJobs[0].dwPeriod = 20;
    cfg.aJobs[0].dwParam = 0xABC;
    cfg.aJobs[2].dwPeriod = 40;
    CMaintainer* pm = new CMaintainer;
    CHECK(pm->Initialize(NULL, &cfg) == TRUE);
    CHECK(pm->m_Scheduler.m_vecSchedules.size() == 2);
    CHECK(pm->m_Scheduler.m_vecSchedules[0].dwPeriod == 20);
    CHECK(pm->m_Scheduler.m_vecSchedules[1].dwPeriod == 40);
    CHECK(pm->m_Scheduler.m_vecSchedules[0].pObject == pm);

    // ticks by hand
    g_nReap = 0;
    g_nKeepAlive = 0;
    pm->m_Scheduler.Tick(15);
    CHECK(g_nReap == 0 && g_nKeepAlive == 0);
    pm->m_Scheduler.Tick(0);
    pm->m_Scheduler.Tick(5);
    CHECK(g_nReap == 1 && g_nKeepAlive == 0);
    CHECK(g_dwReapParam == 0xABC && g_pJobEngine == &g_Net);
    CHECK(pm->m_Scheduler.m_vecSchedules[0].dwElapsed == 0);
    pm->m_Scheduler.Tick(45);              // once per tick, the rest is kept
    CHECK(g_nReap == 2 && g_nKeepAlive == 1);
    CHECK(pm->m_Scheduler.m_vecSchedules[0].dwElapsed == 25);
    CHECK(pm->m_Scheduler.m_vecSchedules[1].dwElapsed == 25);

    // the worker
    g_nReap = 0;
    g_nKeepAlive = 0;
    RUNNER r;
    StartRunner(&r, pm);
    Sleep(300);
    CHECK(pm->m_nActive == 1);
    CHECK(g_nReap >= 5 && g_nKeepAlive >= 2);
    CHECK(g_nReap > g_nKeepAlive);
    CHECK(pm->SignalStop() == 1);
    CHECK(JoinRunner(&r, 1000));
    CHECK(r.dwResult == 0 && pm->m_nActive == 0);

    // stop flag raised before the start
    r.bStop = 1;
    CHECK(pm->Run(&r.bStop, 0) == 0);
    delete pm;
}

static BOOL CreateWithoutFactory(CTask* pTask, CThreadManager* pMgr)
{
    CRuntimeClass rtc = { "NoFactory", 4, NULL, NULL, NULL };
    __try
    {
        Proactor_Create(&rtc, pTask, pMgr, NULL);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return TRUE;
    }
    return FALSE;
}

static void TestTaskAndMap()
{
    g_pNetEngine = &g_Net;
    CThreadManager mgr;
    CTaskManager tm;

    CTask* pTask = new CTask;
    CHECK(Proactor_Create(&CProactorIOCP::classCProactorIOCP, pTask, &mgr, (void*)0x55) == TRUE);
    CHECK(pTask->m_pThreadManager == &mgr);
    CHECK(pTask->m_pQueue != NULL);
    CHECK(dynamic_cast<CQueMT<CMsg>*>(pTask->m_pQueue) != NULL);       // NT
    CHECK(pTask->m_desc.pOwner == pTask);
    CServiceObject* pJob = pTask->m_desc.pJob;
    CHECK(pJob != NULL && pJob->GetRuntimeClass() == &CProactorIOCP::classCProactorIOCP);
    CHECK(pJob->m_pOwner == pTask && pJob->m_pParam == (void*)0x55);

    CTask* pOther = new CTask;
    CHECK(Proactor_Create(&CProactorEvent::classCProactorEvent, pOther, &mgr, NULL) == TRUE);
    CHECK(pOther->m_desc.pJob->GetRuntimeClass() == &CProactorEvent::classCProactorEvent);

    // map
    CHECK(tm.FindTask(7) == NULL);
    CHECK(tm.AddTask(7, pTask) == TRUE);
    CHECK(tm.AddTask(7, pOther) == FALSE);
    CHECK(tm.FindTask(7) == pTask);
    CHECK(tm.AddTask(0x11000001, pOther) == TRUE);
    CHECK(tm.FindTask(0x11000001) == pOther);
    CHECK(tm.m_mapTasks.size() == 2);
    CHECK(tm.RemoveAndDeleteTask(8) == FALSE);

    // threads
    CHECK(pTask->SuspendThreads() == FALSE);
    CHECK(pTask->ResumeThreads() == FALSE);
    CHECK(pTask->StartThreads(2, THREAD_PRIORITY_NORMAL, 0, 0, NULL) == TRUE);
    CProactorIOCP* pIocp = (CProactorIOCP*)pJob;
    CHECK(WaitValue(&pIocp->m_nActive, 2));
    CHECK(pTask->SuspendThreads() == TRUE);
    CTestSocket sock;
    BS_OVERLAPPED ov;
    memset(&ov, 0, sizeof(ov));
    ov.dwNotPooled = 1;
    PostQueuedCompletionStatus(pIocp->m_hPort, 1, (ULONG_PTR)&sock, &ov.ov);
    CHECK(WaitForSingleObject(sock.m_hDone, 200) == WAIT_TIMEOUT);    // suspended
    CHECK(pTask->ResumeThreads() == TRUE);
    CHECK(sock.WaitDone());

    CHECK(pTask->StopThreads() == TRUE);
    CHECK(pIocp->m_nActive == 0);
    CHECK(tm.RemoveAndDeleteTask(7) == TRUE);
    CHECK(tm.FindTask(7) == NULL);
    CHECK(tm.RemoveAndDeleteTask(7) == FALSE);
    CHECK(tm.m_mapTasks.size() == 1);

    // PROACT-004: a class without factory crashes Proactor_Create
    CTask* pBroken = new CTask;
    CHECK(CreateWithoutFactory(pBroken, &mgr));
    CHECK(pBroken->m_desc.pJob == NULL && pBroken->m_pQueue != NULL);
    delete pBroken;
    // tm deletes pOther
}

int main()
{
    CHECK(LoopbackBind_Install() == 1);
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);

    TestRuntimeClass();
    TestProactorIOCP();
    TestProactorEventDispatch();
    TestProactorEventHole();
    TestPeerProcessIOCP();
    TestPeerProcessOverlapped();
    TestMaintainer();
    TestTaskAndMap();

    WSACleanup();
    return TEST_RESULT("ProactorTest");
}
