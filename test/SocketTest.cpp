#include <winsock2.h>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "BSLib/thread/ServiceObject.h"
#include "BSLib/thread/System.h"
#include "BSLib/thread/Task.h"
#include "BSNet/BaseSocket.h"
#include "BSNet/Msg.h"
#include "BSNet/NetEngineFwd.h"
#include "BSNet/NetEngineLog.h"
#include "BSNet/PeerProcess.h"
#include "BSNet/Proactor.h"
#include "BSNet/QueMT.h"
#include "BSNet/SockDatagram.h"
#include "BSNet/SockListener.h"
#include "BSNet/SockPool.h"
#include "BSNet/SockStream.h"
#include "BSNet/SockSystem.h"
#include "BSNet/UdpPing.h"
#include "BSNet/security/Security.h"

#include "TestCheck.h"

// ---------------------------------------------------------------------------------------------
// Test doubles

// Engine with CNetEngine's layout where the sockets read it. Messages are counted so leaks
// show up; session closes are recorded.
class CFakeEngine : public IBSNet
{
public:
    CFakeEngine()
    {
        memset(m_pad4, 0, sizeof(m_pad4));
        m_bSecurityMode = 0;
        memset(m_pad9, 0, sizeof(m_pad9));
        m_nAcceptCount = 0;
        m_dwMaxConnectTime = 0xFFFFFFFF;
        memset(m_pad18, 0, sizeof(m_pad18));
        memset(m_aConfig, 0, sizeof(m_aConfig));
        m_aConfig[0].nSendQueueLimit = -1;
        m_aConfig[1].nSendQueueLimit = -1;
        m_aConfig[0].dwSendMode = 1;
        m_aConfig[1].dwSendMode = 1;
        memset(m_pad94, 0, sizeof(m_pad94));
        m_pListener = NULL;
        InitializeCriticalSection(&m_csFake);
        m_hClosed = CreateEventA(NULL, FALSE, FALSE, NULL);
        m_nLiveMsgs = 0;
        m_nStartWorkers = 0;
        m_dwStartID = 0;
    }

    ~CFakeEngine()
    {
        CloseHandle(m_hClosed);
        DeleteCriticalSection(&m_csFake);
    }

    virtual int __stdcall QueryInterface(DWORD, void**) { return 0; }
    virtual int __stdcall AddRef() { return 0; }
    virtual int __stdcall Shutdown() { return 0; }
    virtual BOOL __stdcall Initialize(BSNET_INIT_PARAM) { return 0; }
    virtual DWORD __stdcall ConnectSockAddr(DWORD*, const sockaddr*, int, DWORD) { return 0; }
    virtual int __stdcall Connect(const char*, u_short, const char*, DWORD, DWORD) { return 0; }
    virtual BOOL __stdcall ListenSockAddr(const sockaddr*) { return 0; }
    virtual int __stdcall Listen(const char*, u_short) { return 0; }
    virtual int __stdcall RegisterProactor(DWORD, void*, DWORD, DWORD) { return 0; }
    virtual int __stdcall vf09(DWORD, DWORD, DWORD, DWORD) { return 0; }
    virtual void* __stdcall StartProactorWorkers(DWORD dwID, DWORD dwCount, DWORD, DWORD, DWORD,
                                                 DWORD*)
    {
        m_dwStartID = dwID;
        m_nStartWorkers = dwCount;
        return (void*)1;
    }
    virtual void* __stdcall ProactorResume(DWORD) { return 0; }
    virtual void* __stdcall ProactorSuspend(DWORD) { return 0; }
    virtual int __stdcall UnregisterProactor(DWORD) { return 0; }
    virtual int __stdcall GetStatistics(DWORD, DWORD) { return 0; }
    virtual int __stdcall GetSessionPeerAddress(DWORD, DWORD*, ULONG*, u_short*) { return 0; }
    virtual int __stdcall GetProactor(DWORD) { return 0; }
    virtual char __stdcall StartHandshake(DWORD, BYTE*) { return 0; }

    virtual CMsg* __stdcall AllocMsg(BOOL bEncrypted)
    {
        EnterCriticalSection(&m_csFake);
        CMsg* pMsg = new CMsg;
        pMsg->Reset();
        ++m_nLiveMsgs;
        LeaveCriticalSection(&m_csFake);
        if (bEncrypted == TRUE)
            *pMsg->m_pSize |= MSG_ENCRYPTED;
        InterlockedIncrement(&pMsg->m_lRefCount);
        return pMsg;
    }

    virtual LONG __stdcall ReleaseMsg(CMsg* pMsg)
    {
        EnterCriticalSection(&m_csFake);
        if (pMsg->m_lRefCount > 0)
        {
            InterlockedDecrement(&pMsg->m_lRefCount);
            if (pMsg->m_lRefCount == 0)
            {
                delete pMsg;
                --m_nLiveMsgs;
            }
        }
        LeaveCriticalSection(&m_csFake);
        return 0;
    }

    virtual int __stdcall SendMsg(DWORD, CMsg*) { return 0; }
    virtual int __stdcall BeginSessionFileTransfer(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                                   DWORD, DWORD, DWORD, DWORD) { return 0; }
    virtual int __stdcall SendFile(DWORD, void*, DWORD, DWORD) { return 0; }

    virtual int __stdcall DisconnectSession(DWORD* pdwSessionID, SOCKET s)
    {
        EnterCriticalSection(&m_csFake);
        m_vecClosedIDs.push_back((DWORD)(DWORD_PTR)pdwSessionID);
        m_vecClosedReasons.push_back((DWORD)s);
        LeaveCriticalSection(&m_csFake);
        SetEvent(m_hClosed);
        return 0;
    }

    virtual int __stdcall CloseSessionTransferFile(DWORD, DWORD) { return 0; }
    virtual int __stdcall ExcludeSessionFromCleanup(DWORD) { return 0; }
    virtual int __stdcall GetSessionSocketState(DWORD) { return 0; }
    virtual int __stdcall RemoveKeepAliveTarget(DWORD) { return 0; }
    virtual void* __stdcall AddKeepAliveTarget(DWORD, DWORD, DWORD, DWORD, DWORD) { return 0; }
    virtual void __stdcall CloseAllFiles() {}
    virtual int __stdcall DumpMsgPool(BYTE, DWORD) { return 0; }
    virtual char __stdcall vf31(BYTE) { return 0; }
    virtual DWORD __stdcall CleanupDisconnectedSessions() { return 0; }
    virtual void* __stdcall GetMsgPool() { return 0; }
    virtual int __stdcall GetSessionLocalMacAddress(DWORD, DWORD*) { return 0; }
    virtual int __stdcall SetSecurityMode(BYTE) { return 0; }
    virtual int __stdcall PrintVersion() { return 0; }
    virtual BOOL __stdcall RegisterDatagramProactor(DWORD) { return 0; }
    virtual int __stdcall OpenDatagramSocket(int) { return 0; }
    virtual int __stdcall SendDatagram(DWORD, DWORD) { return 0; }

    void ClearCloses()
    {
        EnterCriticalSection(&m_csFake);
        m_vecClosedIDs.clear();
        m_vecClosedReasons.clear();
        LeaveCriticalSection(&m_csFake);
        ResetEvent(m_hClosed);
    }

    // Waits for a close of dwSessionID (any ID for 0) and returns its reason, 0 on timeout.
    DWORD WaitClose(DWORD dwSessionID, DWORD dwTimeout = 5000)
    {
        DWORD dwStart = GetTickCount();
        for (;;)
        {
            EnterCriticalSection(&m_csFake);
            for (size_t i = 0; i < m_vecClosedIDs.size(); ++i)
            {
                if (!dwSessionID || m_vecClosedIDs[i] == dwSessionID)
                {
                    DWORD dwReason = m_vecClosedReasons[i];
                    LeaveCriticalSection(&m_csFake);
                    return dwReason;
                }
            }
            LeaveCriticalSection(&m_csFake);
            if (GetTickCount() - dwStart > dwTimeout)
                return 0;
            WaitForSingleObject(m_hClosed, 50);
        }
    }

    LONG LiveMsgs()
    {
        EnterCriticalSection(&m_csFake);
        LONG n = m_nLiveMsgs;
        LeaveCriticalSection(&m_csFake);
        return n;
    }

public:
    BYTE                    m_pad4[4];          // +4
    BYTE                    m_bSecurityMode;    // +8
    BYTE                    m_pad9[7];          // +9
    int                     m_nAcceptCount;     // +10
    DWORD                   m_dwMaxConnectTime; // +14
    BYTE                    m_pad18[0x34];      // +18
    NETENGINE_SOCKET_CONFIG m_aConfig[2];       // +4C
    BYTE                    m_pad94[0x80];      // +94
    CSockListener*          m_pListener;        // +114
    CSockStreamPool         m_ConnectPool;      // +118
    CSockStreamPool         m_AcceptPool;       // +194
    CSockDatagramPool       m_DatagramPool;     // +210

    CRITICAL_SECTION        m_csFake;
    HANDLE                  m_hClosed;
    LONG                    m_nLiveMsgs;
    std::vector<DWORD>      m_vecClosedIDs;
    std::vector<DWORD>      m_vecClosedReasons;
    DWORD                   m_dwStartID;
    DWORD                   m_nStartWorkers;
};

C_ASSERT(FIELD_OFFSET(CFakeEngine, m_bSecurityMode) == 0x8);
C_ASSERT(FIELD_OFFSET(CFakeEngine, m_nAcceptCount) == 0x10);
C_ASSERT(FIELD_OFFSET(CFakeEngine, m_aConfig) == 0x4C);
C_ASSERT(FIELD_OFFSET(CFakeEngine, m_pListener) == 0x114);
C_ASSERT(FIELD_OFFSET(CFakeEngine, m_ConnectPool) == 0x118);
C_ASSERT(FIELD_OFFSET(CFakeEngine, m_AcceptPool) == 0x194);
C_ASSERT(FIELD_OFFSET(CFakeEngine, m_DatagramPool) == 0x210);

static CFakeEngine* g_pEngine = NULL;

// CSession fields the sockets read.
struct FAKE_SESSION
{
    BYTE    pad[0x14];
    DWORD   dw14;       // +14
    DWORD   dwID;       // +18
    DWORD   dw1C;
    DWORD   dwSent;     // +20
    IFile*  pFile;      // +24
};

C_ASSERT(FIELD_OFFSET(FAKE_SESSION, dwID) == 0x18);
C_ASSERT(FIELD_OFFSET(FAKE_SESSION, pFile) == 0x24);

static CTask* g_pTask = NULL;                       // receives the messages
static CServiceObjectSock* g_pSessionProactor = NULL;
static volatile LONG g_nNextSessionID = 100;

struct SESSION_REC
{
    FAKE_SESSION*   pSession;
    CSockStream*    pSock;
    DWORD           dwParam;
    BOOL            bAccepted;
};

static std::vector<SESSION_REC> g_vecSessions;
static volatile LONG g_nHandshakeEvents = 0;
static volatile LONG g_nFileFinished = 0;
static BOOL g_bFileDone = FALSE;

static CRITICAL_SECTION g_csSessions;

DWORD NetEngine_CreateNewSession(IBSNet*, CSockStream* pSock, DWORD dwParam)
{
    FAKE_SESSION* pSession = new FAKE_SESSION;
    memset(pSession, 0, sizeof(*pSession));
    pSession->dwID = (DWORD)InterlockedIncrement(&g_nNextSessionID);
    pSock->m_pSession = (CSession*)pSession;
    pSock->SetTask(g_pTask);
    BOOL bAttached = pSock->AttachToProactor(g_pSessionProactor, (HANDLE)pSock->m_hSocket);

    SESSION_REC rec;
    rec.pSession = pSession;
    rec.pSock = pSock;
    rec.dwParam = dwParam;
    rec.bAccepted = pSock->m_nRole == SOCK_ROLE_ACCEPTED;
    EnterCriticalSection(&g_csSessions);
    g_vecSessions.push_back(rec);
    LeaveCriticalSection(&g_csSessions);
    return bAttached ? pSession->dwID : 0;
}

void Session_PostHandshakeEvent(CSession*)
{
    InterlockedIncrement(&g_nHandshakeEvents);
}

int Session_HandleFileTransferData(CMsg* pMsg, CSockStream*)
{
    g_pEngine->ReleaseMsg(pMsg);
    return 0;
}

BOOL FileTransfer_AccumulateSent(CSession* pSession, DWORD dwBytes)
{
    ((FAKE_SESSION*)pSession)->dwSent += dwBytes;
    return g_bFileDone;
}

void FileTransfer_Finish(CSession*, int nResult)
{
    if (nResult == 1)
        InterlockedIncrement(&g_nFileFinished);
}

void NetEngine_ReapExpiredSessions(IBSNet*, DWORD)
{
}

void NetEngine_KeepAliveTryConnect(IBSNet*)
{
}

// Log capture.
static CRITICAL_SECTION g_csLog;
static std::vector<std::string> g_vecLog;

static int CaptureLogText(DWORD /*dwFlags*/, const char* pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    const char* psz = va_arg(args, const char*);
    va_end(args);
    EnterCriticalSection(&g_csLog);
    g_vecLog.push_back(psz);
    LeaveCriticalSection(&g_csLog);
    return 0;
}

static BOOL HasLog(const char* psz)
{
    EnterCriticalSection(&g_csLog);
    BOOL bFound = FALSE;
    for (size_t i = 0; i < g_vecLog.size(); ++i)
    {
        if (g_vecLog[i] == psz)
            bFound = TRUE;
    }
    LeaveCriticalSection(&g_csLog);
    return bFound;
}

static void ClearLog()
{
    EnterCriticalSection(&g_csLog);
    g_vecLog.clear();
    LeaveCriticalSection(&g_csLog);
}

// Exposes the protected helpers.
class CTestStream : public CSockStream
{
public:
    using CSockStream::EncodeOutgoingPacket;
    using CSockStream::DecodeRecvPacket;
    using CSockStream::PostWSASend;
};

class CTestDatagram : public CSockDatagram
{
public:
    using CSockDatagram::ParseRecvHeader;
};

// Runs a proactor's Run on a plain thread.
struct RUNNER
{
    CServiceObject* pJob;
    volatile LONG   bStop;
    HANDLE          hThread;
};

static DWORD WINAPI RunnerProc(void* p)
{
    RUNNER* r = (RUNNER*)p;
    r->pJob->Run(&r->bStop, 0);
    return 0;
}

static void StartRunners(RUNNER* r, int n, CServiceObject* pJob)
{
    for (int i = 0; i < n; ++i)
    {
        r[i].pJob = pJob;
        r[i].bStop = 0;
        DWORD dwID;
        r[i].hThread = CreateThread(NULL, 0, RunnerProc, &r[i], 0, &dwID);
    }
}

static BOOL JoinRunners(RUNNER* r, int n)
{
    BOOL bOk = TRUE;
    for (int i = 0; i < n; ++i)
    {
        if (WaitForSingleObject(r[i].hThread, 5000) != WAIT_OBJECT_0)
            bOk = FALSE;
        CloseHandle(r[i].hThread);
    }
    return bOk;
}

static sockaddr_in Loopback(u_short wPortNet)
{
    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = wPortNet;
    return a;
}

// A framed packet: header + payload of bytes base, base+1, ...
static std::string MakePacket(WORD wOpcode, int nPayload, BYTE bBase, BOOL bEncryptedFlag = FALSE)
{
    std::string s;
    s.resize(MSG_HEADER_SIZE + nPayload);
    WORD wSize = (WORD)(nPayload | (bEncryptedFlag ? MSG_ENCRYPTED : 0));
    memcpy(&s[0], &wSize, 2);
    memcpy(&s[2], &wOpcode, 2);
    s[4] = 0;
    s[5] = 0;
    for (int i = 0; i < nPayload; ++i)
        s[MSG_HEADER_SIZE + i] = (char)(BYTE)(bBase + i);
    return s;
}

static BOOL CheckPayload(CMsg* pMsg, WORD wOpcode, int nPayload, BYTE bBase)
{
    if (pMsg->GetOpcode() != wOpcode || pMsg->GetPayloadSize() != nPayload)
        return FALSE;
    if (pMsg->m_wReadPos != MSG_HEADER_SIZE || pMsg->m_wWritePos != nPayload + MSG_HEADER_SIZE)
        return FALSE;
    for (int i = 0; i < nPayload; ++i)
    {
        if (pMsg->m_pData[MSG_HEADER_SIZE + i] != (BYTE)(bBase + i))
            return FALSE;
    }
    return TRUE;
}

static CMsg* PopTaskMsg(DWORD dwTimeout = 5000)
{
    DWORD dwStart = GetTickCount();
    for (;;)
    {
        CMsg* pMsg = NULL;
        g_pTask->m_pQueue->Pop(&pMsg, 100, FALSE);
        if (pMsg)
            return pMsg;
        if (GetTickCount() - dwStart > dwTimeout)
            return NULL;
    }
}

static CMsg* NewPayloadMsg(WORD wOpcode, int nPayload, BYTE bBase, BOOL bEncrypt)
{
    CMsg* pMsg = g_pEngine->AllocMsg(bEncrypt);
    pMsg->SetOpcode(wOpcode);
    for (int i = 0; i < nPayload; ++i)
    {
        BYTE b = (BYTE)(bBase + i);
        pMsg->WriteBytes(&b, 1);
    }
    return pMsg;
}

static BOOL WaitLong(volatile LONG* p, LONG n)
{
    for (int i = 0; i < 500; ++i)
    {
        if (*p == n)
            return TRUE;
        Sleep(10);
    }
    return FALSE;
}

static BOOL WaitTrue(volatile BOOL* p)
{
    for (int i = 0; i < 500; ++i)
    {
        if (*p)
            return TRUE;
        Sleep(10);
    }
    return FALSE;
}

static void SetupParam(SECURITY_HANDSHAKE_PARAM* p, BYTE bOptions)
{
    p->bOptions = bOptions;
    p->dwCountSeed = 0x1234567;
    p->dwCrcSeed = 0x5A;
    for (int i = 0; i < 8; ++i)
        p->BlowFishKey[i] = (BYTE)(0x31 + 7 * i);
}

// ---------------------------------------------------------------------------------------------

// Calls vtable slot nSlot of p as a __thiscall function with one int argument.
typedef SOCK_OVERLAPPED* (__fastcall* PFN_SLOT_INT)(void* pThis, void* pEdx, int n);
typedef SOCK_OVERLAPPED* (__fastcall* PFN_SLOT_VOID)(void* pThis, void* pEdx);

static void TestLayout()
{
    CSockStream* pStream = new CSockStream;
    CHECK(pStream->m_nKind == SOCK_KIND_STREAM);
    CHECK(pStream->m_nRole == SOCK_ROLE_NONE);
    CHECK(pStream->m_hSocket == INVALID_SOCKET);
    CHECK(pStream->m_ovSend.dwIoType == SOCK_IO_SEND && pStream->m_ovSend.pOwner == pStream);
    CHECK(pStream->m_ovRecv.dwIoType == SOCK_IO_RECV && pStream->m_ovRecv.pOwner == pStream);
    CHECK(pStream->m_ovDisconnect.dwIoType == SOCK_IO_DISCONNECT);
    CHECK(pStream->m_ovDisconnect.pOwner == pStream);
    CHECK(pStream->m_ovSend.dwNotPooled == 1 && pStream->m_ovSend.dwBufferCount == 1);
    CHECK(pStream->m_ovSend.pBuffer != NULL && pStream->m_ovRecv.pBuffer != NULL);
    CHECK(pStream->m_ovDisconnect.pBuffer == NULL);
    CHECK(pStream->m_Security.Crc.dwValue == 0xFFFFFFFF && pStream->m_Security.bOptions == 0);
    CHECK(pStream->m_apfnIo[4] == NULL && pStream->m_pfnSend == NULL);
    CHECK(pStream->m_dwCloseState == 0 && pStream->m_pSendQueue == NULL);
    delete pStream;

    CSockDatagram* pDatagram = new CSockDatagram;
    CHECK(pDatagram->m_nKind == SOCK_KIND_DATAGRAM);
    CHECK(pDatagram->m_ovRecv.pOwner == pDatagram && pDatagram->m_ovRecv.pBuffer != NULL);
    // slot 5 is GetOverlapped(int), slot 6 GetOverlapped()
    void** vtbl = *(void***)pDatagram;
    CHECK(((PFN_SLOT_INT)vtbl[5])(pDatagram, NULL, 1) == &pDatagram->m_ovRecv);
    CHECK(((PFN_SLOT_INT)vtbl[5])(pDatagram, NULL, 2) == NULL);
    CHECK(((PFN_SLOT_VOID)vtbl[6])(pDatagram, NULL) == &pDatagram->m_ovRecv);
    delete pDatagram;

    CSockListener* pListener = new CSockListener;
    CHECK(pListener->m_nKind == SOCK_KIND_LISTENER && pListener->m_nBacklog == 0x7FFFFFFF);
    delete pListener;

    CUdpPingServer server;
    CHECK(server.m_dwType == 0 && server.m_hSocket == INVALID_SOCKET && server.m_bSuspended == 1);
    CUdpPingClient client;
    CHECK(client.m_dwType == 1 && client.m_uMsg == (UINT)-1 && client.m_hThread == NULL);
}

static void TestIoBlock()
{
    CSockIoBlock recv(SOCK_IO_RECV);
    CHECK(recv.pBuffer == NULL && recv.dwNotPooled == 1 && recv.dwBufferCount == 1);
    recv.AllocBuffer();
    recv.SetupWSABuf();
    CHECK(recv.WsaBuf.buf == (char*)recv.pBuffer && recv.WsaBuf.len == 0x2000);
    recv.Consume(100);
    CHECK(recv.nUsed == 100);
    recv.SetupWSABuf();
    CHECK(recv.WsaBuf.buf == (char*)recv.pBuffer + 100 && recv.WsaBuf.len == 0x2000 - 100);
    int nFree = 0;
    CHECK(recv.GetWritePtr(&nFree) == recv.pBuffer + 100 && nFree == 0x2000 - 100);
    recv.nRead = 40;
    CHECK(recv.GetUnread() == 60);
    int nUsed = 0;
    CHECK(recv.GetBuffer(&nUsed) == recv.pBuffer && nUsed == 100);
    recv.Reset();
    CHECK(recv.nUsed == 0 && recv.nRead == 0);

    CSockIoBlock send(SOCK_IO_SEND);
    send.AllocBuffer();
    for (int i = 0; i < 10; ++i)
        send.pBuffer[i] = (BYTE)i;
    send.nUsed = 10;
    send.SetupWSABuf();
    CHECK(send.WsaBuf.buf == (char*)send.pBuffer && send.WsaBuf.len == 10);
    ClearLog();
    send.Consume(4);    // partial send: the rest moves to the front and is logged
    CHECK(send.nUsed == 6 && send.pBuffer[0] == 4 && send.pBuffer[5] == 9);
    CHECK(HasLog("!!!! \xBF\xC0\xC8\xAA! \xC0\xCC\xB7\xB1 \xB0\xE6\xBF\xEC\xB0\xA1 \xC0\xD6\xB1"
                 "\xE4 \xC0\xD6\xB3\xD7?"));
    ClearLog();
    send.Consume(6);
    CHECK(send.nUsed == 0 && g_vecLog.empty());

    CSockIoBlock accept(SOCK_IO_ACCEPT);
    accept.AllocBuffer();
    accept.SetupWSABuf();
    CHECK(accept.WsaBuf.len == 0x1FFF && accept.WsaBuf.buf == (char*)accept.pBuffer);
}

static void TestSockSystem()
{
    CSockSystem sys;
    CHECK(sys.Startup(2, 2) == TRUE && sys.m_bStarted);
    CHECK(sys.Startup(9, 9) == TRUE);           // started: no new WSAStartup
    sys.Cleanup();
    CHECK(!sys.m_bStarted);
    CSockSystem bad;
    CHECK(bad.Startup(9, 9) == FALSE && !bad.m_bStarted);   // 9.9 is not granted
    CHECK(sys.Initialize(2, 2) == TRUE);
    CHECK(g_pfnAcceptEx != NULL && g_pfnGetAcceptExSockaddrs != NULL && g_pfnTransmitFile != NULL);

    CHECK(OS_IsNotWinNT() == 0);
    CHECK(ResolveHostIPv4("127.0.0.1") == htonl(INADDR_LOOPBACK));
    CHECK(ResolveHostIPv4("") == 0 && ResolveHostIPv4(NULL) == 0);
    CHECK(ResolveHostIPv4("localhost") == htonl(INADDR_LOOPBACK));
    CHECK(ResolveHostIPv4("no-such-host.invalid") == INADDR_NONE);

    in_addr addr;
    char szAddr[256];
    CHECK(Net_GetLocalHostAddr(&addr, szAddr) == TRUE && strlen(szAddr) > 0);
    CHECK(strcmp(inet_ntoa(addr), szAddr) == 0);

    // peer address of a connected pair
    SOCKET l = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a = Loopback(0);
    int nLen = sizeof(a);
    bind(l, (sockaddr*)&a, sizeof(a));
    listen(l, 1);
    getsockname(l, (sockaddr*)&a, &nLen);
    SOCKET c = socket(AF_INET, SOCK_STREAM, 0);
    connect(c, (sockaddr*)&a, sizeof(a));
    SOCKET s = accept(l, NULL, NULL);
    DWORD dwPeer = 0;
    CHECK(Socket_GetPeerAddr(c, &dwPeer, szAddr) == TRUE);
    CHECK(dwPeer == htonl(INADDR_LOOPBACK) && strcmp(szAddr, "127.0.0.1") == 0);
    CHECK(Socket_GetPeerAddr(l, &dwPeer, NULL) == FALSE);

    // SOCK-002: the size is halved after every success: 8192, 4096, 2048 all succeed, the
    // result is 0 and the buffer ends at 2048.
    CHECK(Socket_SetBufferSizeFallback(c, SO_SNDBUF, 8192) == 0);
    int nBuf = 0;
    nLen = 4;
    getsockopt(c, SOL_SOCKET, SO_SNDBUF, (char*)&nBuf, &nLen);
    CHECK(nBuf == 2048);
    CHECK(Socket_SetBufferSizeFallback(c, SO_SNDBUF, 1000) == 0);

    closesocket(s);
    closesocket(c);
    closesocket(l);
}

// Reassembly fed straight into ProcessRecvData (the socket handle is invalid, so the final
// WSARecv fails with 0x8002 after the buffer was compacted).
static void Feed(CSockStream* p, const std::string& s, size_t nFrom, size_t nLen, int* pnResult)
{
    memcpy(p->m_ovRecv.pBuffer + p->m_ovRecv.nUsed, s.data() + nFrom, nLen);
    *pnResult = p->ProcessRecvData(&p->m_ovRecv.ov, (DWORD)nLen);
}

static void TestReassembly()
{
    FAKE_SESSION session;
    memset(&session, 0, sizeof(session));
    session.dwID = 0x77;

    CSockStream* p = new CSockStream;
    int nResult = 0;
    Feed(p, MakePacket(0x1234, 3, 1), 0, 9, &nResult);
    CHECK(nResult == SOCK_ERR_NO_TASK);         // no message task yet

    p->m_ovRecv.Reset();
    p->m_pSession = (CSession*)&session;
    p->SetTask(g_pTask);
    p->m_pfnOnMsg = &CSockStream::OnMsgReceivedAfterHandshake;
    LONG nBase = g_pEngine->LiveMsgs();

    // one packet in three pieces, the first inside the header
    std::string a = MakePacket(0x2001, 20, 10);
    Feed(p, a, 0, 3, &nResult);
    CHECK(nResult == SOCK_ERR_FAILED && p->m_pRecvMsg == NULL && p->m_ovRecv.nUsed == 3);
    Feed(p, a, 3, 5, &nResult);
    CHECK(p->m_pRecvMsg != NULL && p->m_nRecvMsgOffset == 8 && p->m_ovRecv.nUsed == 0);
    CHECK(g_pTask->m_pQueue->GetSize() == 0);
    Feed(p, a, 8, a.size() - 8, &nResult);
    CHECK(p->m_pRecvMsg == NULL && p->m_nRecvMsgOffset == 0);
    CMsg* pMsg = PopTaskMsg(0);
    CHECK(pMsg && CheckPayload(pMsg, 0x2001, 20, 10) && pMsg->m_dwSessionID == 0x77);
    if (pMsg)
        g_pEngine->ReleaseMsg(pMsg);

    // two packets in one piece, plus the start of a third
    std::string b = MakePacket(0x2002, 0, 0) + MakePacket(0x2003, 100, 50) + MakePacket(0x2004,
                                                                                   7, 3);
    Feed(p, b, 0, b.size() - 4, &nResult);
    pMsg = PopTaskMsg(0);
    CHECK(pMsg && CheckPayload(pMsg, 0x2002, 0, 0));
    if (pMsg)
        g_pEngine->ReleaseMsg(pMsg);
    pMsg = PopTaskMsg(0);
    CHECK(pMsg && CheckPayload(pMsg, 0x2003, 100, 50));
    if (pMsg)
        g_pEngine->ReleaseMsg(pMsg);
    CHECK(g_pTask->m_pQueue->GetSize() == 0 && p->m_pRecvMsg != NULL);
    Feed(p, b, b.size() - 4, 4, &nResult);
    pMsg = PopTaskMsg(0);
    CHECK(pMsg && CheckPayload(pMsg, 0x2004, 7, 3));
    if (pMsg)
        g_pEngine->ReleaseMsg(pMsg);

    // larger than the inline buffer and than the receive buffer: fed in 1000-byte pieces
    std::string c = MakePacket(0x2005, 9000, 0x40);
    for (size_t i = 0; i < c.size(); i += 1000)
    {
        size_t n = c.size() - i;
        if (n > 1000)
            n = 1000;
        Feed(p, c, i, n, &nResult);
    }
    pMsg = PopTaskMsg(0);
    CHECK(pMsg && CheckPayload(pMsg, 0x2005, 9000, 0x40) && pMsg->m_nCapacity == 9006);
    if (pMsg)
        g_pEngine->ReleaseMsg(pMsg);
    CHECK(g_pEngine->LiveMsgs() == nBase);

    // size field over 0x7FFF - 6: dropped with 0x800D
    std::string d = MakePacket(0x2006, 0, 0);
    *(WORD*)&d[0] = 0x7FFF;
    Feed(p, d, 0, d.size(), &nResult);
    CHECK(nResult == SOCK_ERR_BAD_SIZE && p->m_pRecvMsg == NULL);
    CHECK(g_pEngine->LiveMsgs() == nBase);

    // encrypted flag on a session without Blowfish
    std::string e = MakePacket(0x2007, 2, 0, TRUE);
    e.append(8, '\0');
    Feed(p, e, 0, 6 + 2, &nResult);
    CHECK(nResult == SOCK_ERR_NOT_SUPPORTED && p->m_pRecvMsg == NULL);
    CHECK(g_pEngine->LiveMsgs() == nBase);

    p->m_pSession = NULL;
    delete p;
}

static void InitSecurity(CSockStream* p, int nRole, BYTE bOptions)
{
    SECURITY_HANDSHAKE_PARAM param;
    SetupParam(&param, bOptions);
    p->m_nRole = nRole;
    Security_ApplyHandshakeFlags(&p->m_Security, &param);
}

static void TestEncodeDecode()
{
    FAKE_SESSION session;
    memset(&session, 0, sizeof(session));
    LONG nBase = g_pEngine->LiveMsgs();

    CTestStream* pClient = new CTestStream;
    CTestStream* pServer = new CTestStream;
    pClient->m_pSession = (CSession*)&session;
    pServer->m_pSession = (CSession*)&session;
    InitSecurity(pClient, SOCK_ROLE_CONNECTED, SECURITY_OPT_BLOWFISH | SECURITY_OPT_COUNT_CRC);
    InitSecurity(pServer, SOCK_ROLE_ACCEPTED, SECURITY_OPT_BLOWFISH | SECURITY_OPT_COUNT_CRC);

    for (int nRound = 0; nRound < 3; ++nRound)
    {
        // client -> server, encrypted: count + CRC, then Blowfish into a new message
        CMsg* pOrig = NewPayloadMsg(0x3001, 13 + nRound, 0x20, TRUE);
        CMsg* pMsg = pOrig;
        CHECK(pClient->EncodeOutgoingPacket(&pMsg) == 1 && pMsg != pOrig);
        CHECK(pOrig->GetCountByte() != 0 || pOrig->GetCrcByte() != 0);
        CHECK((*pMsg->m_pSize & MSG_ENCRYPTED) && pMsg->GetPayloadSize() == 13 + nRound);
        CHECK(pMsg->m_wWritePos == 2 + CBlowFish::AlignUp8(13 + nRound + 4));
        CHECK(pMsg->m_wReadPos == pMsg->m_wWritePos);
        CHECK(memcmp(pMsg->m_pData + 2, pOrig->m_pData + 2, 13 + nRound + 4) != 0);

        CMsg* pRecv = g_pEngine->AllocMsg(FALSE);
        memcpy(pRecv->m_pData, pMsg->m_pData, pMsg->m_wWritePos);
        pServer->m_pRecvMsg = pRecv;
        WORD wSize = 0;
        CHECK(pServer->DecodeRecvPacket(&wSize) == 0 && wSize == 13 + nRound);
        CHECK(pRecv->GetOpcode() == 0x3001);
        CHECK(memcmp(pRecv->m_pData + MSG_HEADER_SIZE, pOrig->m_pData + MSG_HEADER_SIZE,
                     wSize) == 0);
        pServer->m_pRecvMsg = NULL;
        g_pEngine->ReleaseMsg(pRecv);
        g_pEngine->ReleaseMsg(pMsg);
        g_pEngine->ReleaseMsg(pOrig);
    }

    // a wrong count byte, then a wrong CRC (the sequence stays in step)
    CMsg* pMsg = NewPayloadMsg(0x3002, 4, 1, FALSE);
    CHECK(pClient->EncodeOutgoingPacket(&pMsg) == 0);
    pServer->m_pRecvMsg = pMsg;
    pMsg->SetCountByte((BYTE)(pMsg->GetCountByte() + 1));
    WORD wSize = 0;
    CHECK(pServer->DecodeRecvPacket(&wSize) == SOCK_ERR_BAD_COUNT);
    g_pEngine->ReleaseMsg(pMsg);
    pMsg = NewPayloadMsg(0x3003, 4, 1, FALSE);
    pClient->EncodeOutgoingPacket(&pMsg);
    pMsg->SetCrcByte((BYTE)(pMsg->GetCrcByte() ^ 0x55));
    pServer->m_pRecvMsg = pMsg;
    CHECK(pServer->DecodeRecvPacket(&wSize) == SOCK_ERR_BAD_CRC);
    g_pEngine->ReleaseMsg(pMsg);
    pMsg = NewPayloadMsg(0x3004, 4, 1, FALSE);
    pClient->EncodeOutgoingPacket(&pMsg);
    pServer->m_pRecvMsg = pMsg;
    CHECK(pServer->DecodeRecvPacket(&wSize) == 0);
    g_pEngine->ReleaseMsg(pMsg);

    // server -> client: no count/CRC; the client rejects non-zero ones
    pMsg = NewPayloadMsg(0x3005, 4, 1, FALSE);
    CHECK(pServer->EncodeOutgoingPacket(&pMsg) == 0);
    CHECK(pMsg->GetCountByte() == 0 && pMsg->GetCrcByte() == 0);
    pClient->m_pRecvMsg = pMsg;
    CHECK(pClient->DecodeRecvPacket(&wSize) == 0);
    pMsg->SetCountByte(1);
    CHECK(pClient->DecodeRecvPacket(&wSize) == SOCK_ERR_BAD_COUNT);
    pMsg->SetCountByte(0);
    pMsg->SetCrcByte(1);
    CHECK(pClient->DecodeRecvPacket(&wSize) == SOCK_ERR_BAD_CRC);
    g_pEngine->ReleaseMsg(pMsg);

    // file data and "no security" sessions pass unchecked
    pMsg = NewPayloadMsg(SOCK_OP_FILE_DATA, 4, 1, FALSE);
    pMsg->SetCountByte(9);
    pServer->m_pRecvMsg = pMsg;
    CHECK(pServer->DecodeRecvPacket(&wSize) == 0 && wSize == 4);
    pMsg->SetOpcode(0x3006);
    session.dw14 = 1;
    CHECK(pServer->DecodeRecvPacket(&wSize) == 0);
    CMsg* pSame = pMsg;
    CHECK(pServer->EncodeOutgoingPacket(&pMsg) == 0 && pMsg == pSame && pMsg->GetCountByte() == 9);
    session.dw14 = 0;
    g_pEngine->ReleaseMsg(pMsg);

    // encryption requested on a session without Blowfish: logged, flag cleared
    CTestStream* pPlain = new CTestStream;
    pPlain->m_pSession = (CSession*)&session;
    InitSecurity(pPlain, SOCK_ROLE_ACCEPTED, SECURITY_OPT_COUNT_CRC);
    pMsg = NewPayloadMsg(0x3007, 4, 1, TRUE);
    ClearLog();
    CHECK(pPlain->EncodeOutgoingPacket(&pMsg) == 0 && !(*pMsg->m_pSize & MSG_ENCRYPTED));
    CHECK(HasLog("Encryption\xC0\xBB \xC1\xF6\xBF\xF8\xC7\xCF\xC1\xF6 \xBE\xCA\xB4\xC2 \xBC\xBC\xBC"
                 "\xC7\xC0\xCE\xB5\xA5 \xBF\xD6 \xC0\xDA\xB2\xD9\xB8\xB8 \xBE\xCF\xC8\xA3\xC8\xAD "
                 "\xC7\xCF\xB6\xF3\xB1\xB8 \xB1\xD7\xB7\xAF\xB4\xC2 \xB0\xC5\xB3\xC4 \xB4\xEB\xC3"
                 "\xBC! [msgid: 0x3007]"));
    g_pEngine->ReleaseMsg(pMsg);
    // and an encrypted packet arriving there
    pMsg = NewPayloadMsg(0x3008, 4, 1, TRUE);
    pPlain->m_pRecvMsg = pMsg;
    CHECK(pPlain->DecodeRecvPacket(&wSize) == SOCK_ERR_NOT_SUPPORTED);
    g_pEngine->ReleaseMsg(pMsg);
    pPlain->m_pRecvMsg = NULL;
    pPlain->m_pSession = NULL;
    delete pPlain;

    // too large to encrypt: logged, flag cleared, sent unencrypted
    pMsg = NewPayloadMsg(0x3009, 4090, 0, TRUE);
    ClearLog();
    CHECK(pClient->EncodeOutgoingPacket(&pMsg) == 0 && !(*pMsg->m_pSize & MSG_ENCRYPTED));
    CHECK(g_vecLog.size() == 1 && g_vecLog[0].find("encryption fail") != std::string::npos);
    CHECK(g_vecLog[0].find("[msgid: 0x3009]") != std::string::npos);
    g_pEngine->ReleaseMsg(pMsg);
    // an encrypted packet whose padded size exceeds 4096
    pMsg = NewPayloadMsg(0x300A, 4090, 0, TRUE);
    pServer->m_pRecvMsg = pMsg;
    CHECK(pServer->DecodeRecvPacket(&wSize) == SOCK_ERR_BAD_SIZE);
    g_pEngine->ReleaseMsg(pMsg);

    pClient->m_pRecvMsg = NULL;
    pServer->m_pRecvMsg = NULL;
    pClient->m_pSession = NULL;
    pServer->m_pSession = NULL;
    delete pClient;
    delete pServer;
    CHECK(g_pEngine->LiveMsgs() == nBase);
}

static void TestHandshakeErrors()
{
    FAKE_SESSION session;
    memset(&session, 0, sizeof(session));
    LONG nBase = g_pEngine->LiveMsgs();

    CSockStream* p = new CSockStream;
    p->m_pSession = (CSession*)&session;
    p->m_nRole = SOCK_ROLE_ACCEPTED;

    // nothing sent yet (the constructor leaves the state byte uninitialised, as the original)
    p->m_bHandshakeState = 0;
    CMsg* pMsg = NewPayloadMsg(SOCK_OP_HANDSHAKE, 12, 0, FALSE);
    ClearLog();
    CHECK(p->OnMsgReceivedBeforeHandshake(pMsg) == SOCK_ERR_HANDSHAKE);
    CHECK(HasLog("_OnMsgReceivedBeforeHandshake() - Initialize [MsgID: 0x5000]"));

    // 0x5000 without key exchange
    p->m_bHandshakeState = SECURITY_HS_BLOWFISH | SECURITY_HS_COUNT_CRC;
    ClearLog();
    CHECK(p->OnMsgReceivedBeforeHandshake(pMsg) == SOCK_ERR_HANDSHAKE);
    CHECK(HasLog("_OnMsgReceivedBeforeHandshake() - Handshake Failed:6 [MsgID: 0x5000]"));
    CHECK(p->m_bHandshakeState == 0);

    // wrong answer size
    p->m_bHandshakeState = 0x0E;
    g_pEngine->ReleaseMsg(pMsg);
    pMsg = NewPayloadMsg(SOCK_OP_HANDSHAKE, 11, 0, FALSE);
    ClearLog();
    CHECK(p->OnMsgReceivedBeforeHandshake(pMsg) == SOCK_ERR_HANDSHAKE);
    CHECK(HasLog("_OnMsgReceivedBeforeHandshake() - Handshake Recipient Size:11 [MsgID: "
                 "0x5000]"));

    // security mode not satisfied
    g_pEngine->m_bSecurityMode = SECURITY_MODE_KEY_EXCHANGE;
    p->m_bHandshakeState = 0x06;
    pMsg->SetOpcode(SOCK_OP_HANDSHAKE_ACCEPT);
    ClearLog();
    CHECK(p->OnMsgReceivedBeforeHandshake(pMsg) == SOCK_ERR_HANDSHAKE);
    CHECK(HasLog("SecurityModeCheck - Mode:2, Handshake:6"));

    // unexpected opcode, server and client side
    p->m_bHandshakeState = 0x06;
    pMsg->SetOpcode(0x7001);
    ClearLog();
    CHECK(p->OnMsgReceivedBeforeHandshake(pMsg) == SOCK_ERR_HANDSHAKE);
    const char* pszOdd = "_OnMsgReceivedBeforeHandshake() - Handshake \xC0\xCC\xC0\xFC\xBF\xA1 \xC0"
                         "\xCC\xBB\xF3\xC7\xD1 \xB8\xDE\xBD\xC3\xC1\xF6\xB0\xA1 \xB3\xAF\xBE\xC6"
                         "\xBF\xD4\xB4\xD9[MsgID: 0x7001]";
    CHECK(HasLog(pszOdd));
    p->m_nRole = SOCK_ROLE_CONNECTED;
    ClearLog();
    CHECK(p->OnMsgReceivedBeforeHandshake(pMsg) == SOCK_ERR_HANDSHAKE);
    CHECK(HasLog(pszOdd));

    // the message stays with the caller on errors
    CHECK(pMsg->m_lRefCount == 1);
    g_pEngine->ReleaseMsg(pMsg);

    // sends before the handshake completed
    pMsg = NewPayloadMsg(0x1234, 1, 0, FALSE);
    CHECK(p->SendMsg(pMsg) == SOCK_ERR_HANDSHAKE);
    CHECK(((CSockStream*)NULL)->SendMsg(pMsg) == SOCK_ERR_NULL);
    g_pEngine->ReleaseMsg(pMsg);

    // client sockets do not start a handshake
    SECURITY_HANDSHAKE_PARAM param;
    SetupParam(&param, SECURITY_OPT_BLOWFISH);
    CHECK(p->SendHandshakeRequest(&param) == TRUE && p->m_bHandshakeState == 0);

    p->m_pSession = NULL;
    delete p;
    g_pEngine->m_bSecurityMode = 0;
    CHECK(g_pEngine->LiveMsgs() == nBase);
}

static void TestSendLimit()
{
    LONG nBase = g_pEngine->LiveMsgs();
    SOCKET hDummy = socket(AF_INET, SOCK_STREAM, 0);

    CSockStream* p = new CSockStream;
    p->m_nRole = SOCK_ROLE_CONNECTED;
    p->m_hSocket = hDummy;
    p->SetupHandlersAndQueue();
    CHECK(p->m_pfnSend == &CSockStream::SendQueuedWithLimit);
    CHECK(p->m_apfnIo[4] == &CSockStream::FillSendBuffer);
    CHECK(p->m_apfnIo[5] == &CSockStream::OnFileSent);
    CHECK(p->m_apfnIo[2] == &CSockStream::ProcessRecvData);
    CHECK(p->m_pfnOnMsg == &CSockStream::OnMsgReceivedBeforeHandshake);
    CHECK(p->m_pSendQueue != NULL);

    // the send block is busy, so messages stay queued
    p->m_ovSend.nUsed = 5;
    g_pEngine->m_aConfig[1].nSendQueueLimit = 2;
    CMsg* apMsg[4];
    for (int i = 0; i < 3; ++i)
    {
        apMsg[i] = NewPayloadMsg(0x4000, 1, 0, FALSE);
        CHECK(p->SendQueuedWithLimit(apMsg[i]) == 0);
        CHECK(apMsg[i]->m_lRefCount == 2);
    }
    CHECK(p->m_pSendQueue->GetSize() == 3);
    apMsg[3] = NewPayloadMsg(0x4000, 1, 0, FALSE);
    ClearLog();
    CHECK(p->SendQueuedWithLimit(apMsg[3]) == SOCK_ERR_PEER_NOT_RESPONDING);
    CHECK(HasLog("NERR_PEER_DOESNT_RESPONSE - 1:2"));
    CHECK(p->m_pSendQueue->GetSize() == 3);
    // SOCK-003: the extra reference is never dropped
    CHECK(apMsg[3]->m_lRefCount == 2);

    // unlimited
    g_pEngine->m_aConfig[1].nSendQueueLimit = -1;
    CMsg* pMore = NewPayloadMsg(0x4000, 1, 0, FALSE);
    CHECK(p->SendQueuedWithLimit(pMore) == 0 && p->m_pSendQueue->GetSize() == 4);

    // closing state 6 and closed sockets reject sends
    p->m_dwCloseState = 6;
    CHECK(p->SendQueuedWithLimit(pMore) == SOCK_ERR_FAILED);
    p->m_dwCloseState = 0;

    // Close returns the queued messages to the engine
    for (int i = 0; i < 4; ++i)
        g_pEngine->ReleaseMsg(apMsg[i]);
    g_pEngine->ReleaseMsg(pMore);
    p->m_nRole = SOCK_ROLE_ACCEPTED;           // keep the handle: only connected ones close
    CHECK(p->Close() == 0);
    CHECK(p->m_pSendQueue->GetSize() == 0);
    CHECK(g_pEngine->LiveMsgs() == nBase + 1);  // the leaked one
    apMsg[3]->m_lRefCount = 1;
    g_pEngine->ReleaseMsg(apMsg[3]);
    CHECK(g_pEngine->LiveMsgs() == nBase);
    CHECK(p->m_hSocket == hDummy);

    // send mode 0, and the event model forcing mode 1
    g_pEngine->m_aConfig[0].dwSendMode = 0;
    CSockStream* q = new CSockStream;
    q->m_nRole = SOCK_ROLE_ACCEPTED;
    q->SetupHandlersAndQueue();
    CHECK(q->m_pfnSend == &CSockStream::SendQueued && q->m_apfnIo[4] == &CSockStream::OnMsgSent);
    CHECK(q->m_pSendQueue == NULL);
    g_nNetIoModel = 1;
    q->SetupHandlersAndQueue();
    CHECK(q->m_pfnSend == &CSockStream::SendQueuedWithLimit && q->m_pSendQueue != NULL);
    CHECK(dynamic_cast<CQueMT9x<CMsg>*>(q->m_pSendQueue) != NULL);
    CHECK(q->CreateEvents() && q->m_ovRecv.ov.hEvent && q->m_ovSend.ov.hEvent);
    g_nNetIoModel = 0;
    g_pEngine->m_aConfig[0].dwSendMode = 1;
    delete q;

    p->m_nRole = SOCK_ROLE_CONNECTED;
    p->m_bGraceful = TRUE;
    p->Close();
    CHECK(p->m_hSocket == INVALID_SOCKET && p->m_nRole == SOCK_ROLE_NONE);
    delete p;
    CHECK(((CSockStream*)NULL)->CSockStream::Close() == SOCK_ERR_NULL);
}

static void TestFileErrors()
{
    FAKE_SESSION session;
    memset(&session, 0, sizeof(session));
    CSockStream* p = new CSockStream;
    ClearLog();
    CHECK(p->SendFile(NULL, FALSE) == SOCK_ERR_INVALID_FILE);
    CHECK(HasLog("CSockStream::SendFile() - Valid File \xC0\xCC \xBE\xC6\xB4\xCF\xB4\xD9!"));
    CHECK(p->OnFileSent(NULL, 0) == SOCK_ERR_NULL);
    CHECK(HasLog("CSockStream::_OnFileSent() - Bind\xB5\xC8 \xBC\xBC\xBC\xC7\xC0\xCC NULL\xC0\xCC"
                 "\xB4\xD9!"));
    p->m_pSession = (CSession*)&session;
    CHECK(p->OnFileSent(NULL, 0) == SOCK_ERR_NO_FILE);
    CHECK(HasLog("CSockStream::_OnFileSent() - Session\xC0\xC7 WorkingFile\xC0\xCC NULL\xC0\xCC"
                 "\xB4\xD9"));
    p->m_pSession = NULL;
    delete p;
}

// ---------------------------------------------------------------------------------------------
// Real sockets through the completion port proactor

struct STREAM_RIG
{
    CProactorIOCP*  pProactor;
    RUNNER          aRunner[2];
    CSockListener*  pListener;
    sockaddr_in     addr;
};

static void StartRig(STREAM_RIG* r)
{
    r->pProactor = new CProactorIOCP;
    g_pSessionProactor = r->pProactor;
    StartRunners(r->aRunner, 2, r->pProactor);

    r->pListener = new CSockListener;
    sockaddr_in a = Loopback(0);
    CHECK(r->pListener->CreateAndBind((sockaddr*)&a) == TRUE);
    CHECK(CSockListener::ms_nPendingAccepts == 0);
    int nLen = sizeof(r->addr);
    getsockname(r->pListener->m_hSocket, (sockaddr*)&r->addr, &nLen);
    g_pEngine->m_pListener = r->pListener;
    DWORD nInUse = g_pEngine->m_AcceptPool.m_nInUse;
    CHECK(r->pListener->ListenAndPostAccepts(r->pProactor) == TRUE);
    CHECK(CSockListener::ms_nPendingAccepts == 10);
    CHECK(g_pEngine->m_AcceptPool.m_nInUse == nInUse + 10);
}

static void StopRig(STREAM_RIG* r)
{
    r->pProactor->SignalStop();
    CHECK(JoinRunners(r->aRunner, 2));
    g_pEngine->m_pListener = NULL;
    delete r->pListener;
    delete r->pProactor;
    g_pSessionProactor = NULL;
}

static size_t CountAccepted()
{
    EnterCriticalSection(&g_csSessions);
    size_t n = 0;
    for (size_t i = 0; i < g_vecSessions.size(); ++i)
    {
        if (g_vecSessions[i].bAccepted)
            ++n;
    }
    LeaveCriticalSection(&g_csSessions);
    return n;
}

// The accepted session number nIndex (0-based), once the AcceptEx completion created it.
static SESSION_REC WaitAccepted(size_t nIndex)
{
    for (int i = 0; i < 500; ++i)
    {
        EnterCriticalSection(&g_csSessions);
        size_t n = 0;
        for (size_t j = 0; j < g_vecSessions.size(); ++j)
        {
            if (!g_vecSessions[j].bAccepted)
                continue;
            if (n++ == nIndex)
            {
                SESSION_REC rec = g_vecSessions[j];
                LeaveCriticalSection(&g_csSessions);
                return rec;
            }
        }
        LeaveCriticalSection(&g_csSessions);
        Sleep(10);
    }
    SESSION_REC none;
    memset(&none, 0, sizeof(none));
    return none;
}

static CSockStream* ConnectClient(STREAM_RIG* r)
{
    CSockStream* pClient = g_pEngine->m_ConnectPool.Alloc();
    CHECK(pClient->Connect(NULL, (sockaddr*)&r->addr) == TRUE);
    CHECK(pClient->m_nRole == SOCK_ROLE_CONNECTED);
    CHECK(memcmp(&pClient->m_PeerAddr, &r->addr, sizeof(r->addr)) == 0);
    CHECK(pClient->m_pfnSend == &CSockStream::SendQueuedWithLimit);
    CHECK(NetEngine_CreateNewSession(g_pEngine, pClient, 0x99) != 0);
    return pClient;
}

static BOOL WaitHandshaked(CSockStream* p)
{
    for (int i = 0; i < 500; ++i)
    {
        if (p->m_bHandshaked)
            return TRUE;
        Sleep(10);
    }
    return FALSE;
}

// Sends n messages each way and checks they arrive in order.
static void Exchange(CSockStream* pFrom, CSockStream* pTo, BOOL bEncrypt, int nCount, int nSize)
{
    for (int i = 0; i < nCount; ++i)
    {
        CMsg* pMsg = NewPayloadMsg((WORD)(0x6000 + i), nSize + i, (BYTE)i, bEncrypt);
        CHECK(pFrom->SendMsg(pMsg) == 0);
        g_pEngine->ReleaseMsg(pMsg);
    }
    for (int i = 0; i < nCount; ++i)
    {
        CMsg* pMsg = PopTaskMsg();
        CHECK(pMsg != NULL);
        if (!pMsg)
            return;
        CHECK(CheckPayload(pMsg, (WORD)(0x6000 + i), nSize + i, (BYTE)i));
        CHECK(pMsg->m_dwSessionID == pTo->GetOwnerSessionID());
        g_pEngine->ReleaseMsg(pMsg);
    }
}

static void TestHandshake(BYTE bSecurityMode)
{
    STREAM_RIG rig;
    g_pEngine->m_bSecurityMode = bSecurityMode;
    StartRig(&rig);

    size_t nAccepted = CountAccepted();
    CSockStream* pClient = ConnectClient(&rig);
    SESSION_REC rec = WaitAccepted(nAccepted);
    CHECK(rec.pSock != NULL);
    if (!rec.pSock)
    {
        StopRig(&rig);
        return;
    }
    CHECK(rec.dwParam == 0x68);                 // the accept session parameter (config +0x1C)
    CHECK(WaitLong((volatile LONG*)&CSockListener::ms_nPendingAccepts, 10));
    CSockStream* pServer = rec.pSock;
    CHECK(pServer->m_nRole == SOCK_ROLE_ACCEPTED);
    sockaddr_in local;
    pClient->GetLocalAddr(&local);
    CHECK(memcmp(&pServer->m_PeerAddr, &local, sizeof(local)) == 0);

    LONG nEvents = g_nHandshakeEvents;
    SECURITY_HANDSHAKE_PARAM param;
    SetupParam(&param, SECURITY_OPT_BLOWFISH | SECURITY_OPT_COUNT_CRC);
    CHECK(pServer->SendHandshakeRequest(&param) == TRUE);
    BOOL bKeyExchange = (bSecurityMode & SECURITY_MODE_KEY_EXCHANGE) != 0;
    CHECK((param.bOptions & SECURITY_OPT_KEY_EXCHANGE) == (bKeyExchange ? 8 : 0));
    CHECK(WaitHandshaked(pClient));
    CHECK(WaitHandshaked(pServer));
    CHECK(WaitLong(&g_nHandshakeEvents, nEvents + 2));
    CHECK(pServer->m_bHandshakeState == (bKeyExchange ? 0x1E : 0x06));
    CHECK(pClient->m_pfnOnMsg == &CSockStream::OnMsgReceivedAfterHandshake);
    CHECK(pServer->m_pfnOnMsg == &CSockStream::OnMsgReceivedAfterHandshake);
    if (bKeyExchange)
    {
        // both derived the same session key, different from the one sent in the clear
        CHECK(pClient->m_KeyRecipient.m_dwSecret == pServer->m_KeySender.m_dwSecret);
        CHECK(pClient->m_KeyRecipient.m_dwSecret != 0);
    }

    // encrypted and plain traffic, both ways
    Exchange(pClient, pServer, TRUE, 20, 30);
    Exchange(pServer, pClient, TRUE, 20, 30);
    Exchange(pClient, pServer, FALSE, 5, 2000);
    Exchange(pServer, pClient, FALSE, 5, 3000);

    // the server drops a client packet with a bad CRC and closes the connection
    g_pEngine->ClearCloses();
    DWORD dwServerID = pServer->GetOwnerSessionID();
    pClient->m_Security.Crc.dwSeed ^= 1;
    CMsg* pMsg = NewPayloadMsg(0x6100, 10, 0, FALSE);
    CHECK(pClient->SendMsg(pMsg) == 0);
    g_pEngine->ReleaseMsg(pMsg);
    CHECK(g_pEngine->WaitClose(dwServerID) == ((DWORD)SOCK_ERR_BAD_CRC << 16));

    // disconnect of the accepted socket with TransmitFile: the client sees the end of the
    // connection (0 bytes: 0x8003); once the engine closes the client, the disconnect
    // completes and the proactor reports reason 6
    g_pEngine->ClearCloses();
    DWORD dwClientID = pClient->GetOwnerSessionID();
    CHECK(pServer->DisconnectReuse(5) == 1);
    CHECK(pServer->m_dwCloseState == 5);
    CHECK(g_pEngine->WaitClose(dwClientID) == ((DWORD)SOCK_ERR_NO_DATA << 16));
    CHECK(HasOverlappedIoCompleted(&pServer->m_ovDisconnect.ov) == FALSE);

    // the engine recycles both: the connected one closes, the accepted one gets a new AcceptEx
    LONG nPending = CSockListener::ms_nPendingAccepts;
    SockPool_CloseSocket(pClient, g_pEngine);
    CHECK(pClient->m_hSocket == INVALID_SOCKET && pClient->m_pSession == NULL);
    CHECK(g_pEngine->m_ConnectPool.m_nInUse == 0);
    CHECK(g_pEngine->WaitClose(dwServerID) == 6);
    SockPool_CloseSocket(pServer, g_pEngine);
    CHECK(CSockListener::ms_nPendingAccepts == nPending + 1);
    CHECK(pServer->m_hSocket != INVALID_SOCKET && pServer->m_dwCloseState == 0);
    CHECK(pServer->m_pSession == NULL && pServer->m_bHandshaked == FALSE);

    // the recycled socket accepts again (it is first in line only by chance, so connect
    // until it is used)
    BOOL bReused = FALSE;
    std::vector<CSockStream*> vecClients;
    for (int i = 0; i < 12 && !bReused; ++i)
    {
        size_t n = CountAccepted();
        CSockStream* p = ConnectClient(&rig);
        vecClients.push_back(p);
        if (WaitAccepted(n).pSock == pServer)
            bReused = TRUE;
    }
    CHECK(bReused);
    for (size_t i = 0; i < vecClients.size(); ++i)
        Sock_ReleaseToConnectPool(vecClients[i], g_pEngine);

    StopRig(&rig);
    g_pEngine->m_bSecurityMode = 0;
}

// Plain framed traffic from a raw client: coalesced and split TCP segments, send mode 0.
static void TestRawStream()
{
    // accepted sockets pick their send mode when the AcceptEx is posted
    g_pEngine->m_aConfig[0].dwSendMode = 0;
    STREAM_RIG rig;
    StartRig(&rig);

    size_t nAccepted = CountAccepted();
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    BOOL bNoDelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&bNoDelay, sizeof(bNoDelay));
    CHECK(connect(s, (sockaddr*)&rig.addr, sizeof(rig.addr)) == 0);
    CSockStream* pServer = WaitAccepted(nAccepted).pSock;
    CHECK(pServer != NULL);
    if (!pServer)
    {
        closesocket(s);
        StopRig(&rig);
        return;
    }
    CHECK(pServer->m_pfnSend == &CSockStream::SendQueued);
    // no security: deliver as after the handshake
    pServer->m_pfnOnMsg = &CSockStream::OnMsgReceivedAfterHandshake;

    // three packets in one send
    std::string all = MakePacket(0x7001, 10, 1) + MakePacket(0x7002, 0, 0)
                      + MakePacket(0x7003, 300, 7);
    CHECK(send(s, all.data(), (int)all.size(), 0) == (int)all.size());
    CMsg* pMsg = PopTaskMsg();
    CHECK(pMsg && CheckPayload(pMsg, 0x7001, 10, 1));
    if (pMsg)
        g_pEngine->ReleaseMsg(pMsg);
    pMsg = PopTaskMsg();
    CHECK(pMsg && CheckPayload(pMsg, 0x7002, 0, 0));
    if (pMsg)
        g_pEngine->ReleaseMsg(pMsg);
    pMsg = PopTaskMsg();
    CHECK(pMsg && CheckPayload(pMsg, 0x7003, 300, 7));
    if (pMsg)
        g_pEngine->ReleaseMsg(pMsg);

    // one packet in pieces (2 bytes of header, the rest of the header, the payload in parts)
    std::string one = MakePacket(0x7004, 5000, 3);
    size_t aCut[] = { 2, 6, 1000, 4000, one.size() };
    size_t nFrom = 0;
    for (int i = 0; i < 5; ++i)
    {
        send(s, one.data() + nFrom, (int)(aCut[i] - nFrom), 0);
        nFrom = aCut[i];
        Sleep(30);
        if (i < 4)
            CHECK(g_pTask->m_pQueue->GetSize() == 0);
    }
    pMsg = PopTaskMsg();
    CHECK(pMsg && CheckPayload(pMsg, 0x7004, 5000, 3));
    if (pMsg)
        g_pEngine->ReleaseMsg(pMsg);

    // server -> raw client, mode 0: one WSASend per message
    LONG nBase = g_pEngine->LiveMsgs();
    pServer->m_bHandshaked = TRUE;
    pServer->m_Security.bOptions = SECURITY_OPT_NONE;
    for (int i = 0; i < 3; ++i)
    {
        pMsg = NewPayloadMsg((WORD)(0x7100 + i), 50, (BYTE)i, FALSE);
        CHECK(pServer->SendMsg(pMsg) == 0);
        g_pEngine->ReleaseMsg(pMsg);
    }
    std::string got;
    while (got.size() < 3 * 56)
    {
        char buf[512];
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        got.append(buf, n);
    }
    CHECK(got == MakePacket(0x7100, 50, 0) + MakePacket(0x7101, 50, 1)
                 + MakePacket(0x7102, 50, 2));
    for (int i = 0; i < 100 && g_pEngine->LiveMsgs() != nBase; ++i)
        Sleep(10);
    CHECK(g_pEngine->LiveMsgs() == nBase);       // OnMsgSent released them

    // a packet with the encrypted flag on a session without Blowfish closes the connection
    g_pEngine->ClearCloses();
    DWORD dwID = pServer->GetOwnerSessionID();
    std::string bad = MakePacket(0x7005, 2, 0, TRUE) + std::string(8, '\0');
    send(s, bad.data(), (int)bad.size(), 0);
    CHECK(g_pEngine->WaitClose(dwID) == ((DWORD)SOCK_ERR_NOT_SUPPORTED << 16));
    closesocket(s);

    g_pEngine->m_aConfig[0].dwSendMode = 1;
    StopRig(&rig);
}

static void TestDisconnectWithoutSession()
{
    CSockStream* p = new CSockStream;
    p->CreateSocket(SOCK_ROLE_ACCEPTED);
    CHECK(p->m_hSocket != INVALID_SOCKET);
    // no session and reason 2: closed instead of TransmitFile
    CHECK(p->DisconnectReuse(2) == 3);
    CHECK(p->m_hSocket == INVALID_SOCKET && p->m_dwCloseState == 0);
    delete p;
}

// ---------------------------------------------------------------------------------------------
// Datagrams

static void TestDatagram()
{
    LONG nBase = g_pEngine->LiveMsgs();
    CPeerProcessIOCP* pPeer = new CPeerProcessIOCP;
    RUNNER runner[2];
    StartRunners(runner, 2, pPeer);

    // the engine path: message task and the datagram peer process from the task manager.
    // Naming g_System pulls System.obj (the singleton) out of BSLib.lib; nothing else in this
    // test references it, and CSystem::GetInstance() alone would return NULL.
    CHECK(CSystem::GetInstance() == &g_System);
    CTaskManager* pTasks = CSystem::GetInstance()->m_pTaskManager;
    CTask* pPeerTask = new CTask;
    pPeerTask->m_pQueue = new CQueMT9x<CMsg>;
    pPeerTask->m_desc.pJob = pPeer;
    CHECK(pTasks->AddTask(0x10000077, g_pTask));
    CHECK(pTasks->AddTask(g_dwNetDatagramTaskID, pPeerTask));

    NETENGINE_DATAGRAM dg;
    dg.pSocket = NULL;
    dg.dwTaskID = 0x10000077;
    g_pEngine->m_nStartWorkers = 0;
    CHECK(NetEngine_CreateDatagramSocket(&dg, 0) == TRUE);
    CHECK(dg.pSocket != NULL && dg.pSocket->m_pTask == g_pTask && dg.pSocket->m_bOpened == 1);
    CHECK(dg.pSocket->m_pfnSend == &CSockDatagram::PostWSASendTo);
    CHECK(dg.pSocket->m_pSendQueue == NULL);       // CPeerProcessIOCP has none
    CHECK(g_pEngine->m_dwStartID == 0x10000005 && g_pEngine->m_nStartWorkers == 1);
    sockaddr_in a;
    dg.pSocket->GetLocalAddr(&a);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(a.sin_port != 0);

    // unknown message task
    NETENGINE_DATAGRAM bad;
    bad.pSocket = NULL;
    bad.dwTaskID = 0x10000078;
    CHECK(NetEngine_CreateDatagramSocket(&bad, 0) == FALSE && bad.pSocket == NULL);

    // a second socket sends to the first
    CSockDatagram* pSender = g_pEngine->m_DatagramPool.Alloc();
    CHECK(pSender->Open(0) == TRUE);
    pSender->SetTask(g_pTask);
    CHECK(pSender->AttachToProactor(pPeer, (HANDLE)pSender->m_hSocket) == TRUE);
    sockaddr_in from;
    pSender->GetLocalAddr(&from);
    from.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    NETENGINE_DATAGRAM dgSender;
    dgSender.pSocket = pSender;
    dgSender.dwTaskID = 0;
    for (int i = 0; i < 5; ++i)
    {
        CMsg* pMsg = NewPayloadMsg((WORD)(0x8000 + i), 40 + i, (BYTE)i, FALSE);
        CHECK(NetEngine_SendDatagram(&dgSender, (sockaddr*)&a, pMsg) == 0);
    }
    for (int i = 0; i < 5; ++i)
    {
        CMsg* pMsg = PopTaskMsg();
        CHECK(pMsg && CheckPayload(pMsg, (WORD)(0x8000 + i), 40 + i, (BYTE)i));
        if (pMsg)
        {
            CHECK(memcmp(&pMsg->m_Addr, &from, sizeof(from)) == 0);
            g_pEngine->ReleaseMsg(pMsg);
        }
    }

    // a raw datagram carrying two packets
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    std::string two = MakePacket(0x8100, 3, 9) + MakePacket(0x8101, 0, 0);
    CHECK(sendto(s, two.data(), (int)two.size(), 0, (sockaddr*)&a, sizeof(a)) == (int)two.size());
    CMsg* pMsg = PopTaskMsg();
    CHECK(pMsg && CheckPayload(pMsg, 0x8100, 3, 9));
    if (pMsg)
        g_pEngine->ReleaseMsg(pMsg);
    pMsg = PopTaskMsg();
    CHECK(pMsg && CheckPayload(pMsg, 0x8101, 0, 0));
    if (pMsg)
        g_pEngine->ReleaseMsg(pMsg);
    closesocket(s);

    // datagrams are never decrypted
    CTestDatagram* pTest = new CTestDatagram;
    pTest->m_pRecvMsg = NewPayloadMsg(0x8200, 2, 0, TRUE);
    WORD wSize = 0;
    CHECK(pTest->ParseRecvHeader(&wSize) == SOCK_ERR_NOT_SUPPORTED && wSize == 2);
    *pTest->m_pRecvMsg->m_pSize &= MSG_SIZE_MASK;
    CHECK(pTest->ParseRecvHeader(&wSize) == 0);
    g_pEngine->ReleaseMsg(pTest->m_pRecvMsg);
    pTest->m_pRecvMsg = NULL;
    // a send on a closed socket
    pMsg = NewPayloadMsg(0x8201, 1, 0, FALSE);
    CHECK(pTest->PostWSASendTo((sockaddr*)&a, pMsg) == SOCK_ERR_FAILED);
    CHECK(pTest->OnIOCompletion(1, &pMsg->m_Overlapped) == SOCK_ERR_CLOSED);
    g_pEngine->ReleaseMsg(pMsg);
    delete pTest;

    // the event model queues on the peer process's queue
    CSockDatagram* pQueued = new CSockDatagram;
    g_nNetIoModel = 1;
    CHECK(pQueued->Open(0) == TRUE && pQueued->m_ovRecv.ov.hEvent != NULL);
    g_nNetIoModel = 0;
    CHECK(pQueued->m_pfnSend == &CSockDatagram::SendTo);
    CQueMT9x<CMsg> que;
    pQueued->m_pSendQueue = &que;
    pMsg = NewPayloadMsg(0x8202, 3, 0, FALSE);
    CHECK(pQueued->Send((sockaddr*)&a, pMsg) == 0);
    CHECK(que.GetSize() == 1 && pQueued->m_nPendingSends == 1);
    CHECK(memcmp(&pMsg->m_Addr, &a, sizeof(a)) == 0 && pMsg->m_WsaBuf.len == 9);
    pQueued->Close();                           // returns the queued message
    CHECK(que.GetSize() == 0 && pQueued->m_hSocket == INVALID_SOCKET);
    pQueued->m_pSendQueue = NULL;
    delete pQueued;

    // shutdown
    Sock_ReleaseToDatagramPool(pSender, g_pEngine);
    Sock_ReleaseToDatagramPoolAndClear(&dg.pSocket);
    CHECK(dg.pSocket == NULL && g_pEngine->m_DatagramPool.m_nInUse == 0);
    pPeer->SignalStop();
    CHECK(JoinRunners(runner, 2));
    {
        CSingleLockBS lock(&pTasks->m_cs, TRUE);
        pTasks->m_mapTasks.erase(0x10000077);
        pTasks->m_mapTasks.erase(g_dwNetDatagramTaskID);
    }
    pPeerTask->m_desc.pJob = NULL;
    delete pPeerTask;
    delete pPeer;
    for (int i = 0; i < 100 && g_pEngine->LiveMsgs() != nBase; ++i)
        Sleep(10);
    CHECK(g_pEngine->LiveMsgs() == nBase);
}

// ---------------------------------------------------------------------------------------------
// UDP ping

static void TestUdpPing()
{
    // packet format
    SOCKET r = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a = Loopback(0);
    int nLen = sizeof(a);
    bind(r, (sockaddr*)&a, sizeof(a));
    getsockname(r, (sockaddr*)&a, &nLen);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(UdpPing_SendReq(s, (sockaddr*)&a, 0x11223344, 0x55667788) == 16);
    char buf[64];
    CHECK(recv(r, buf, sizeof(buf), 0) == 16);
    CHECK(memcmp(buf, "REQ", 4) == 0 && *(DWORD*)(buf + 8) == 0x11223344);
    CHECK(*(DWORD*)(buf + 12) == 0x55667788);
    CHECK(UdpPing_SendAck(s, (sockaddr*)&a, 1, 2) == 16);
    CHECK(recv(r, buf, sizeof(buf), 0) == 16 && memcmp(buf, "ACK", 4) == 0);
    closesocket(s);
    closesocket(r);

    CUdpPingServer server;
    HANDLE hThread = server.Open("127.0.0.1");
    CHECK(hThread != NULL && server.m_hThread == hThread);
    CHECK(server.m_bSuspended == 1);
    CHECK(server.Suspend() == TRUE);            // already suspended: nothing to do
    CHECK(server.Resume() == TRUE && server.m_bSuspended == 0);
    CHECK(server.Suspend() == TRUE && server.m_bSuspended == 1);
    CHECK(server.Resume() == TRUE && server.m_bSuspended == 0);

    HWND hWnd = CreateWindowExA(0, "STATIC", "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    CHECK(hWnd != NULL);
    CUdpPingClient client;
    client.m_hWnd = hWnd;
    client.m_uMsg = WM_USER + 7;
    client.m_hSocket = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(client.AddTarget(7, "127.0.0.1", UDPPING_SERVER_PORT) == 7);
    CHECK(client.AddTarget(7, "127.0.0.1", 1) == UDPPING_ID_ANY);
    // a closed port: ICMP unreachable comes back as WSAECONNRESET
    SOCKET hClosed = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in c = Loopback(0);
    nLen = sizeof(c);
    bind(hClosed, (sockaddr*)&c, sizeof(c));
    getsockname(hClosed, (sockaddr*)&c, &nLen);
    closesocket(hClosed);
    CHECK(client.AddTarget(9, "", ntohs(c.sin_port)) == 9);
    CHECK(client.m_mapTargets[9].Addr.sin_addr.s_addr == 0);
    client.m_mapTargets[9].Addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    client.PingTarget(7);
    CHECK(client.m_hThread != NULL);
    BOOL bAck = FALSE;
    BOOL bUnreachable = FALSE;
    DWORD dwStart = GetTickCount();
    while (GetTickCount() - dwStart < 5000 && !bAck)
    {
        MSG msg;
        while (PeekMessageA(&msg, hWnd, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_USER + 7 && msg.wParam == 7)
                bAck = TRUE;
        }
        Sleep(10);
    }
    CHECK(bAck);
    client.PingTarget(9);
    client.PingTarget(1234);                    // unknown: nothing
    dwStart = GetTickCount();
    while (GetTickCount() - dwStart < 5000 && !bUnreachable)
    {
        MSG msg;
        while (PeekMessageA(&msg, hWnd, 0, 0, PM_REMOVE))
        {
            // the sender address of a failed recvfrom is whatever the last datagram left
            if (msg.message == WM_USER + 7
                && (msg.wParam & 0xFFFF0000) == UDPPING_UNREACHABLE)
            {
                bUnreachable = TRUE;
            }
        }
        Sleep(10);
    }
    CHECK(bUnreachable);

    // all targets
    bAck = FALSE;
    client.PingTarget(UDPPING_ID_ANY);
    dwStart = GetTickCount();
    while (GetTickCount() - dwStart < 5000 && !bAck)
    {
        MSG msg;
        while (PeekMessageA(&msg, hWnd, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_USER + 7 && msg.wParam == 7)
                bAck = TRUE;
        }
        Sleep(10);
    }
    CHECK(bAck);

    // SOCK-009: removing an existing target keeps it, removing a missing one throws
    client.RemoveTarget(7);
    CHECK(client.m_mapTargets.size() == 2);
    BOOL bThrown = FALSE;
    try
    {
        client.RemoveTarget(55);
    }
    catch (std::out_of_range&)
    {
        bThrown = TRUE;
    }
    CHECK(bThrown);

    client.Close();
    CHECK(client.m_hSocket == INVALID_SOCKET && client.m_hThread == NULL);
    CHECK(client.m_mapTargets.empty());
    server.Stop();
    CHECK(server.m_hThread == NULL && server.m_hSocket == INVALID_SOCKET);
    DestroyWindow(hWnd);

    // a server that was never resumed stops cleanly too
    CUdpPingServer idle;
    idle.Open("");
    idle.Stop();
    CHECK(idle.m_hThread == NULL && idle.m_bSuspended == 0);
}

// The BSLib debug output helpers that sit between the socket code and the assert code.
static std::string g_strDebugOutput;

static void DebugOutputReceiver(const char* psz)
{
    g_strDebugOutput = psz;
}

static void TestDebugOutput()
{
    g_strDebugOutput = "none";
    BSDebugOutput("x %d", 1);
    CHECK(g_strDebugOutput == "none");      // no callback: nothing
    BSDebugSetOutputCallback(DebugOutputReceiver);
    BSDebugOutput("value %d/%s", 42, "abc");
    CHECK(g_strDebugOutput == "value 42/abc\n");
    std::string strLong(1100, 'a');
    BSDebugOutput("%s", strLong.c_str());
    CHECK(g_strDebugOutput.size() == 1023 && g_strDebugOutput[0] == 'a');
    BSDebugSetOutputCallback(NULL);

    // format strings of 1024 characters or more show no message box
    std::string strFormat(1024, 'b');
    CHECK(BSMessageBoxPrintf(strFormat.c_str()) == 1024);
}

int main()
{
    InitializeCriticalSection(&g_csLog);
    InitializeCriticalSection(&g_csSessions);
    g_pfnNetEngineLog = CaptureLogText;
    Security_BuildCrcTable();

    CSockSystem sys;
    sys.Initialize(2, 2);

    g_pEngine = new CFakeEngine;
    g_pEngine->m_aConfig[0].dwSessionParam = 0x68;
    g_pEngine->m_aConfig[1].dwSessionParam = 0x8C;
    g_pNetEngine = g_pEngine;
    g_nNetIoModel = 0;
    g_pTask = new CTask;
    g_pTask->m_pQueue = new CQueMT9x<CMsg>;

    TestDebugOutput();
    TestLayout();
    TestIoBlock();
    TestSockSystem();
    TestReassembly();
    TestEncodeDecode();
    TestHandshakeErrors();
    TestSendLimit();
    TestFileErrors();
    TestDisconnectWithoutSession();
    TestRawStream();
    TestHandshake(SECURITY_MODE_BASIC);
    TestHandshake(SECURITY_MODE_KEY_EXCHANGE);
    TestDatagram();
    TestUdpPing();

    // clean shutdown: no message left behind, every pooled socket closes with the pools
    LONG nLive = g_pEngine->LiveMsgs();
    CHECK(nLive == 0);
    g_bNetEngineShutdown = TRUE;
    g_pNetEngine = NULL;
    delete g_pEngine;
    delete g_pTask;
    for (size_t i = 0; i < g_vecSessions.size(); ++i)
        delete g_vecSessions[i].pSession;

    return TEST_RESULT("SocketTest");
}
