#include "PeerProcess.h"

#include <string.h>

#include "BSNet/Msg.h"
#include "BSNet/NetEngineFwd.h"
#include "BSNet/NetEngineLog.h"
#include "BSNet/QueMT.h"
#include "BSNet/SockDatagram.h"

// Discarded trace output of the originals (empty function); the argument is still evaluated.
#define BS_PEER_TRACE(x) ((void)(x))

// ---------------------------------------------------------------------------------------------
// CPeerProcessIOCP

BS_IMPLEMENT_DYNCREATE(CPeerProcessIOCP, &CServiceObjectSock::classCServiceObjectSock)

CPeerProcessIOCP::CPeerProcessIOCP()
{
    m_nWorkers = 0;
    m_hPort = INVALID_HANDLE_VALUE;
    m_hPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 2);
}

CPeerProcessIOCP::~CPeerProcessIOCP()
{
    Close();
}

BOOL CPeerProcessIOCP::Initialize(void* pOwner, void* pParam)
{
    m_pOwner = pOwner;
    m_pParam = pParam;
    return TRUE;
}

void CPeerProcessIOCP::Close()
{
    SignalStop();
    if (m_hPort)
    {
        CloseHandle(m_hPort);
        m_hPort = NULL;
    }
}

DWORD CPeerProcessIOCP::Run(volatile LONG* /*pbStop*/, int /*nIndex*/)
{
    InterlockedIncrement(&m_nWorkers);
    DWORD dwBytes = 0;
    ULONG_PTR ulKey = 0;
    LPOVERLAPPED pOverlapped = NULL;
    BOOL bOk = GetQueuedCompletionStatus(m_hPort, &dwBytes, &ulKey, &pOverlapped, INFINITE);
    while (ulKey)
    {
        CEventHandler* pHandler = (CEventHandler*)ulKey;
        // BUG(PROACT-001): a failed pooled CMsg is released here and still dispatched; a
        // non-zero result below releases it a second time.
        if (!bOk && pOverlapped && ((BS_OVERLAPPED*)pOverlapped)->dwNotPooled == 0)
            NetEngine_ReleaseMsg(pOverlapped);
        if (pHandler->OnIOCompletion(dwBytes, pOverlapped) && pOverlapped
            && ((BS_OVERLAPPED*)pOverlapped)->dwNotPooled == 0)
        {
            NetEngine_ReleaseMsg(pOverlapped);
        }
        bOk = GetQueuedCompletionStatus(m_hPort, &dwBytes, &ulKey, &pOverlapped, INFINITE);
    }
    InterlockedDecrement(&m_nWorkers);
    return 0;
}

BOOL CPeerProcessIOCP::PostStopPacket()
{
    if (m_hPort == NULL)
        return FALSE;
    return PostQueuedCompletionStatus(m_hPort, 0x44, 0, NULL) != 0;
}

BOOL CPeerProcessIOCP::PostStopPackets(int nCount)
{
    for (int i = 0; i < nCount; ++i)
    {
        // BUG(PROACT-003): PostStopPacket returns 0 or 1, so a failed post is never detected.
        if (PostStopPacket() == -1)
            return FALSE;
    }
    return TRUE;
}

int CPeerProcessIOCP::SignalStop()
{
    CSingleLockBS lock(&m_cs, TRUE);
    int nWorkers = m_nWorkers;
    if (nWorkers)
        return PostStopPackets(nWorkers);
    return 0;
}

BOOL CPeerProcessIOCP::AssociateHandle(HANDLE hHandle, CEventHandler* pHandler)
{
    return CreateIoCompletionPort(hHandle, m_hPort, (ULONG_PTR)pHandler, m_nWorkers)
           || GetLastError() == ERROR_INVALID_PARAMETER;
}

int CPeerProcessIOCP::CloseHandler(CEventHandler* /*pHandler*/, DWORD /*dwParam*/)
{
    return 1;
}

// ---------------------------------------------------------------------------------------------
// CPeerProcessOverlapped

BS_IMPLEMENT_DYNCREATE(CPeerProcessOverlapped, &CServiceObjectSock::classCServiceObjectSock)

CPeerProcessOverlapped::CPeerProcessOverlapped()
{
    m_pSendQueue = NULL;
    memset(m_ahEvents, 0, sizeof(m_ahEvents));
    memset(m_apOverlapped, 0, sizeof(m_apOverlapped));
    m_ahEvents[0] = WSACreateEvent();
    m_ahEvents[1] = WSACreateEvent();
    RecountHandles();
}

CPeerProcessOverlapped::~CPeerProcessOverlapped()
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (m_ahEvents[0])
        WSACloseEvent(m_ahEvents[0]);
    if (m_ahEvents[1])
        WSACloseEvent(m_ahEvents[1]);
    if (m_pSendQueue)
    {
        delete m_pSendQueue;
        m_pSendQueue = NULL;
    }
}

BOOL CPeerProcessOverlapped::Initialize(void* pOwner, void* pParam)
{
    m_pParam = pParam;
    m_pOwner = pOwner;
    if (m_pSendQueue)
    {
        delete m_pSendQueue;
        m_pSendQueue = NULL;
    }
    m_pSendQueue = new CQueMT9x<CMsg>;
    m_pSendQueue->Start();
    return TRUE;
}

BOOL CPeerProcessOverlapped::RecountHandles()
{
    m_nHandles = 0;
    for (int i = 0; i < MAX_EVENTS; ++i)
    {
        if (m_ahEvents[i])
            ++m_nHandles;
    }
    return SetEvent(m_ahEvents[1]);
}

BOOL CPeerProcessOverlapped::AssociateHandle(HANDLE /*hHandle*/, CEventHandler* pHandler)
{
    CSingleLockBS lock(&m_cs, TRUE);
    SOCK_OVERLAPPED* pOverlapped = static_cast<CSockDatagram*>(pHandler)->GetOverlapped(1);
    if (pOverlapped)
    {
        m_ahEvents[2] = pOverlapped->ov.hEvent;
        m_apOverlapped[2] = pOverlapped;
    }
    RecountHandles();
    return TRUE;
}

int CPeerProcessOverlapped::CloseHandler(CEventHandler* /*pHandler*/, DWORD /*dwParam*/)
{
    return 1;
}

IQue<CMsg>* CPeerProcessOverlapped::GetSendQueue()
{
    return m_pSendQueue;
}

SOCK_OVERLAPPED* CPeerProcessOverlapped::GetSlotOverlapped(int nIndex)
{
    CSingleLockBS lock(&m_cs, TRUE);
    return m_apOverlapped[nIndex];
}

int CPeerProcessOverlapped::SignalStop()
{
    m_pSendQueue->Stop();
    WSASetEvent(m_ahEvents[0]);
    CMsg* pMsg = NULL;
    // BUG(PROACT-005): a stopped CQueMT9x hands out one message and returns FALSE, so this loop
    // never runs: that message is lost and the rest stay queued.
    while (m_pSendQueue->Pop(&pMsg, 1, TRUE) == TRUE)
        g_pNetEngine->ReleaseMsg(pMsg);
    return 1;
}

void CALLBACK PeerProcess_SendCompletion(DWORD /*dwError*/, DWORD cbTransferred,
                                         LPWSAOVERLAPPED lpOverlapped, DWORD /*dwFlags*/)
{
    ((CEventHandler*)lpOverlapped->hEvent)->OnIOCompletion(cbTransferred, lpOverlapped);
}

DWORD CPeerProcessOverlapped::Run(volatile LONG* /*pbStop*/, int /*nIndex*/)
{
    DWORD cbTransfer;
    DWORD dwFlags;
    SleepEx(1, TRUE);
    DWORD nCount = m_nHandles;
    if (nCount == 0)
        return 0;

    for (;;)
    {
        DWORD dwIndex = WSAWaitForMultipleEvents(nCount, m_ahEvents, FALSE, 1, FALSE);
        if (dwIndex == 0)
            return 0;
        if (dwIndex == WSA_WAIT_FAILED)
        {
            BS_PEER_TRACE(WSAGetLastError());
            return 0;
        }

        BOOL bSend = TRUE;
        if (dwIndex == 1)
        {
            WSAResetEvent(m_ahEvents[1]);
            bSend = FALSE;
        }
        else if (dwIndex != WSA_WAIT_TIMEOUT)
        {
            WSAResetEvent(m_ahEvents[dwIndex]);
            SOCK_OVERLAPPED* pOverlapped = GetSlotOverlapped(dwIndex);
            if (pOverlapped == NULL)
            {
                bSend = FALSE;
            }
            else
            {
                CBaseSocket* pSock = pOverlapped->pOwner;
                cbTransfer = 0;
                if (!WSAGetOverlappedResult(pSock->m_hSocket, &pOverlapped->ov, &cbTransfer,
                                            FALSE, &dwFlags))
                {
                    if (WSAGetLastError() != WSA_IO_INCOMPLETE)
                        CloseHandler(pSock, dwIndex);
                }
                else
                {
                    int nResult = pSock->OnIOCompletion(cbTransfer, &pOverlapped->ov);
                    if (nResult && (nResult & 0x8000))
                        CloseHandler(pSock, dwIndex);
                }
            }
        }

        if (bSend)
        {
            CBaseSocket* pDatagram = NetEngine_GetDatagram()->pSocket;
            CMsg* pMsg;
            while (m_pSendQueue->Pop(&pMsg, 1, FALSE) == TRUE)
            {
                if (pMsg == NULL)
                    break;
                pMsg->m_Overlapped.hEvent = (WSAEVENT)pDatagram;
                DWORD dwSent;
                if (WSASendTo(pDatagram->m_hSocket, &pMsg->m_WsaBuf, pMsg->m_dw32, &dwSent, 0,
                              (const sockaddr*)&pMsg->m_Addr, sizeof(sockaddr_in),
                              &pMsg->m_Overlapped, PeerProcess_SendCompletion) == SOCKET_ERROR)
                {
                    int nError = WSAGetLastError();
                    if (nError != WSA_IO_PENDING)
                    {
                        g_pNetEngine->ReleaseMsg(pMsg);
                        if (nError == WSAEWOULDBLOCK || nError == ERROR_NOT_ENOUGH_MEMORY
                            || nError == WSAENOBUFS)
                        {
                            NetEngineLog(0, "WSASend() - NERR_TOO_MANY_OUTSTANDING: %d", nError);
                        }
                        else
                        {
                            NetEngineLog(0, "WSASend() - Unknow NERROR: %d", nError);
                        }
                    }
                }
            }
        }

        SleepEx(1, TRUE);
        nCount = m_nHandles;
        if (nCount == 0)
            return 0;
    }
}
