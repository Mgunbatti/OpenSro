#include "SockDatagram.h"

#include <string.h>

#include "BSLib/thread/ServiceObject.h"
#include "BSLib/thread/System.h"
#include "BSLib/thread/Task.h"
#include "BSNet/Msg.h"
#include "BSNet/NetEngine.h"
#include "BSNet/NetEngineLog.h"
#include "BSNet/SockStream.h"

namespace
{

inline void ReleaseMsgToEngine(CMsg* pMsg)
{
    pMsg->m_wReadPos = pMsg->m_wWritePos;
    g_pNetEngine->ReleaseMsg(pMsg);
}

} // namespace

CSockDatagram::CSockDatagram()
    : m_ovRecv(SOCK_IO_RECV)
{
    m_nPendingSends = 0;
    m_pRecvMsg = NULL;
    m_pSendQueue = NULL;
    ResetState();
    m_ovRecv.pOwner = this;
    m_ovRecv.AllocBuffer();
    for (int i = 0; i < 10; ++i)
        m_apfnIo[i] = NULL;
    m_pfnF4 = NULL;
    m_pfnSend = NULL;
    m_nKind = SOCK_KIND_DATAGRAM;
}

CSockDatagram::~CSockDatagram()
{
    Close();
}

BOOL CSockDatagram::Open(WORD wPort)
{
    m_bOpened = 1;
    if (m_hSocket == INVALID_SOCKET)
    {
        m_hSocket = WSASocketA(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (m_hSocket == INVALID_SOCKET)
        {
            WSAGetLastError();
            Close();
            return FALSE;
        }
    }

    memset(&m_FromAddr, 0, sizeof(m_FromAddr));
    m_FromAddr.sin_family = AF_INET;
    if (wPort > 0)
    {
        int nOn = 1;
        setsockopt(m_hSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&nOn, 4);
    }
    m_FromAddr.sin_port = htons(wPort);
    m_FromAddr.sin_addr.s_addr = 0;
    if (bind(m_hSocket, (const sockaddr*)&m_FromAddr, sizeof(m_FromAddr)) == SOCKET_ERROR)
    {
        WSAGetLastError();
        return FALSE;
    }
    if (!CreateEventIfOverlapped())
        return FALSE;
    InitCallbacks();
    return TRUE;
}

int CSockDatagram::Close()
{
    if (this == NULL)
        return SOCK_ERR_NULL;

    CSingleLockBS lock(&m_cs, TRUE);
    if (m_pSendQueue && g_pNetEngine)
    {
        CMsg* pMsg = NULL;
        m_pSendQueue->Pop(&pMsg, 0, TRUE);
        while (pMsg)
        {
            g_pNetEngine->ReleaseMsg(pMsg);
            pMsg = NULL;
            m_pSendQueue->Pop(&pMsg, 0, TRUE);
        }
    }
    if (m_hSocket != INVALID_SOCKET)
    {
        closesocket(m_hSocket);
        m_hSocket = INVALID_SOCKET;
    }
    if (!g_bNetEngineShutdown)
        ResetState();
    return 0;
}

void CSockDatagram::ResetState()
{
    CSingleLockBS lock(&m_cs, TRUE);
    m_nRecvMsgOffset = 0;
    m_ovRecv.Reset();
    m_pTask = NULL;
    m_nPendingSends = 0;
    m_bOpened = 0;
    if (m_pRecvMsg)
    {
        ReleaseMsgToEngine(m_pRecvMsg);
        m_pRecvMsg = NULL;
    }
}

// SockDatagram_CreateEventIfOverlapped
BOOL CSockDatagram::CreateEventIfOverlapped()
{
    if (g_nNetIoModel == 1 && !m_ovRecv.ov.hEvent)
    {
        m_ovRecv.ov.hEvent = WSACreateEvent();
        if (!m_ovRecv.ov.hEvent)
            return FALSE;
    }
    return TRUE;
}

// SockDatagram_InitCallbacks
void CSockDatagram::InitCallbacks()
{
    int nIoModel = g_nNetIoModel;
    m_apfnIo[2 * SOCK_IO_SEND] = &CSockDatagram::ReleaseSentMsg;
    m_apfnIo[2 * SOCK_IO_RECV] = &CSockDatagram::OnRecvComplete;
    if (nIoModel == 0)
        m_pfnSend = &CSockDatagram::PostWSASendTo;
    else if (nIoModel == 1)
        m_pfnSend = &CSockDatagram::SendTo;
}

// SockDatagram_ParseRecvHeader: datagrams are never encrypted.
int CSockDatagram::ParseRecvHeader(WORD* pwSize)
{
    WORD wSizeField = *m_pRecvMsg->m_pSize;
    *pwSize = wSizeField & MSG_SIZE_MASK;
    if (!(wSizeField & MSG_ENCRYPTED) && m_pRecvMsg->GetOpcode() == SOCK_OP_FILE_DATA)
        return 0;
    return (*m_pRecvMsg->m_pSize & MSG_ENCRYPTED) ? SOCK_ERR_NOT_SUPPORTED : 0;
}

int CSockDatagram::Send(const sockaddr* pTo, CMsg* pMsg)
{
    return (this->*m_pfnSend)(pTo, pMsg);
}

// CSockDatagram_SendTo: the event model's peer process sends from its queue.
int CSockDatagram::SendTo(const sockaddr* pTo, CMsg* pMsg)
{
    CSingleLockBS lock(&m_cs, TRUE);
    // SendOverlapped_ResetIfInvalidSocket
    if (m_hSocket == INVALID_SOCKET)
        return SOCK_ERR_FAILED;
    pMsg->m_dw24 = 0;

    memcpy(&pMsg->m_Addr, pTo, sizeof(pMsg->m_Addr));
    pMsg->m_WsaBuf.len = pMsg->m_wWritePos;
    if (m_pSendQueue->Push(pMsg))
    {
        InterlockedIncrement(&m_nPendingSends);
        return 0;
    }
    g_pNetEngine->ReleaseMsg(pMsg);
    return SOCK_ERR_FAILED;
}

// SockDatagram_PostWSASendTo
int CSockDatagram::PostWSASendTo(const sockaddr* pTo, CMsg* pMsg)
{
    CSingleLockBS lock(&m_cs, TRUE);
    // SendOverlapped_ResetIfInvalidSocket
    if (m_hSocket == INVALID_SOCKET)
        return SOCK_ERR_FAILED;
    pMsg->m_dw24 = 0;

    pMsg->m_WsaBuf.len = pMsg->m_wWritePos;
    DWORD dwSent;
    if (WSASendTo(m_hSocket, &pMsg->m_WsaBuf, pMsg->m_dw32, &dwSent, 0, pTo, 16,
                  &pMsg->m_Overlapped, NULL) == SOCKET_ERROR)
    {
        int nError = WSAGetLastError();
        if (nError != WSA_IO_PENDING)
        {
            g_pNetEngine->ReleaseMsg(pMsg);
            if (nError == WSAEWOULDBLOCK || nError == ERROR_NOT_ENOUGH_MEMORY
                || nError == WSAENOBUFS)
            {
                NetEngineLog(0, "WSASend() - NERR_TOO_MANY_OUTSTANDING: %d", nError);
                return SOCK_ERR_TOO_MANY;
            }
            NetEngineLog(0, "WSASend() - Unknow NERROR: %d", nError);
            lock.Unlock();
            return SOCK_ERR_FAILED;
        }
    }
    InterlockedIncrement(&m_nPendingSends);
    return 0;
}

// SockDatagram_ReleaseBufferCb
int CSockDatagram::ReleaseSentMsg(LPOVERLAPPED pOverlapped, DWORD)
{
    g_pNetEngine->ReleaseMsg((CMsg*)pOverlapped);
    return 0;
}

int CSockDatagram::OnRecvComplete(LPOVERLAPPED, DWORD dwBytes)
{
    if (!m_pTask)
        return SOCK_ERR_NO_TASK;
    if (dwBytes)
        m_ovRecv.Consume(dwBytes);

    SOCK_OVERLAPPED& o = m_ovRecv;
    int nResult;
    for (;;)
    {
        CMsg* pMsg = m_pRecvMsg;
        if (pMsg)
        {
            WORD wSizeField = *pMsg->m_pSize;
            int nNeed;
            if (wSizeField & MSG_ENCRYPTED)
            {
                nNeed = CBlowFish::AlignUp8((WORD)((wSizeField & MSG_SIZE_MASK) + 6) - 2) + 2
                        - m_nRecvMsgOffset;
            }
            else
            {
                nNeed = (WORD)((wSizeField & MSG_SIZE_MASK) + 6) - m_nRecvMsgOffset;
            }

            if (pMsg->m_wWritePos > pMsg->m_nCapacity)
                BSAssertFail();
            if (nNeed > (WORD)(pMsg->m_nCapacity - pMsg->m_wWritePos))
            {
                nResult = SOCK_ERR_BAD_SIZE;
                goto failed;
            }

            BYTE* pDst = m_pRecvMsg->PtrAt(m_nRecvMsgOffset);
            int nCopy = nNeed;
            if (nNeed)
            {
                if (o.nUsed == o.nRead)
                {
                    nCopy = 0;
                }
                else
                {
                    int nAvail = o.nUsed - o.nRead;
                    if (nNeed > nAvail)
                        nCopy = nAvail;
                    memcpy(pDst, o.pBuffer + o.nRead, nCopy);
                    o.nRead += nCopy;
                }
            }
            m_nRecvMsgOffset += nCopy;
            if (nCopy != nNeed)
                return PostRecvFrom();

            WORD wSize = 0;
            nResult = ParseRecvHeader(&wSize);
            if (nResult)
                goto failed;

            if (m_pRecvMsg->m_nCapacity < MSG_HEADER_SIZE)
            {
                nResult = SOCK_ERR_BAD_SIZE;
                goto failed;
            }
            m_pRecvMsg->m_wReadPos = MSG_HEADER_SIZE;
            if (wSize + MSG_HEADER_SIZE > m_pRecvMsg->m_nCapacity)
            {
                nResult = SOCK_ERR_BAD_SIZE;
                goto failed;
            }
            m_pRecvMsg->m_wWritePos = (WORD)(wSize + MSG_HEADER_SIZE);
            m_pRecvMsg->m_Addr = m_FromAddr;
            nResult = DispatchRecvMsg(m_pRecvMsg);
            if (nResult)
                goto failed;
            m_pRecvMsg = NULL;
            m_nRecvMsgOffset = 0;
        }

        if (o.nUsed - o.nRead < MSG_HEADER_SIZE)
            return PostRecvFrom();

        pMsg = g_pNetEngine->AllocMsg(FALSE);
        m_pRecvMsg = pMsg;
        BYTE* pDst = pMsg->PtrAt(0);
        int nCopy = MSG_HEADER_SIZE;
        if (o.nUsed == o.nRead)
        {
            nCopy = 0;
        }
        else
        {
            int nAvail = o.nUsed - o.nRead;
            if (nAvail < MSG_HEADER_SIZE)
                nCopy = nAvail;
            memcpy(pDst, o.pBuffer + o.nRead, nCopy);
            o.nRead += nCopy;
        }
        m_nRecvMsgOffset = nCopy;

        WORD wTotal = (WORD)((*m_pRecvMsg->m_pSize & MSG_SIZE_MASK) + MSG_HEADER_SIZE);
        if (wTotal > MSG_INLINE_BUF_SIZE)
        {
            if (wTotal > MSG_SIZE_MASK)
            {
                nResult = SOCK_ERR_BAD_SIZE;
                goto failed;
            }
            m_pRecvMsg->AllocLargeBuffer(wTotal);
        }
    }

failed:
    ReleaseMsgToEngine(m_pRecvMsg);
    m_pRecvMsg = NULL;
    m_nRecvMsgOffset = 0;
    return nResult;
}

// SockDatagram_DispatchRecvMsg
int CSockDatagram::DispatchRecvMsg(CMsg* pMsg)
{
    if (!m_pTask)
    {
        // BUG(SOCK-005): releases the message that the caller releases again on this error.
        ReleaseMsgToEngine(pMsg);
        return SOCK_ERR_NO_HANDLER;
    }
    m_pTask->m_pQueue->Push(pMsg);
    return 0;
}

int CSockDatagram::PostRecvFrom()
{
    SOCK_OVERLAPPED& o = m_ovRecv;
    if (o.nUsed == o.nRead)
    {
        o.nUsed = 0;
    }
    else
    {
        int nRest = o.nUsed - o.nRead;
        memmove(o.pBuffer, o.pBuffer + o.nRead, nRest);
        o.nUsed = nRest;
    }
    o.nRead = 0;
    o.SetupWSABuf();
    m_nFromLen = sizeof(m_FromAddr);

    DWORD dwFlags = 0;
    DWORD dwReceived = 0;
    if (WSARecvFrom(m_hSocket, &o.WsaBuf, o.dwBufferCount, &dwReceived, &dwFlags,
                    (sockaddr*)&m_FromAddr, &m_nFromLen, &o.ov, NULL) == SOCKET_ERROR)
    {
        int nError = WSAGetLastError();
        if (nError == WSA_IO_PENDING)
            return 0;
        if (nError == WSAEWOULDBLOCK || nError == ERROR_NOT_ENOUGH_MEMORY
            || nError == WSAENOBUFS)
        {
            return SOCK_ERR_TOO_MANY;
        }
        return SOCK_ERR_FAILED;
    }
    return 0;
}

int CSockDatagram::OnIOCompletion(DWORD dwBytes, LPOVERLAPPED pOverlapped)
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (m_hSocket == INVALID_SOCKET)
        return SOCK_ERR_CLOSED;

    BS_OVERLAPPED* pIo = (BS_OVERLAPPED*)pOverlapped;
    if (pIo->dwIoType == SOCK_IO_SEND)
    {
        LONG nPending = 0;
        InterlockedExchange(&nPending, m_nPendingSends);
        if (!nPending)
            return SOCK_SEND_NOT_PENDING;
        InterlockedDecrement(&m_nPendingSends);
    }
    return (this->*m_apfnIo[2 * pIo->dwIoType + pIo->dw18])(pOverlapped, dwBytes);
}

BOOL CSockDatagram::AttachToProactor(CServiceObjectSock* pProactor, HANDLE hHandle)
{
    if (!pProactor)
        return FALSE;
    m_pSendQueue = pProactor->GetSendQueue();
    if (!pProactor->AssociateHandle(hHandle, this))
        return FALSE;
    return PostRecvFrom() == 0;
}

SOCK_OVERLAPPED* CSockDatagram::GetOverlapped(int nType)
{
    if (nType == 1)
        return &m_ovRecv;
    return NULL;
}

SOCK_OVERLAPPED* CSockDatagram::GetOverlapped()
{
    return &m_ovRecv;
}

// DatagramSocket_SetOwnerLocked
void CSockDatagram::SetTask(CTask* pTask)
{
    CSingleLockBS lock(&m_cs, TRUE);
    m_pTask = pTask;
}

BOOL NetEngine_CreateDatagramSocket(NETENGINE_DATAGRAM* pDatagram, WORD wPort)
{
    CTaskManager* pTaskManager = CSystem::GetInstance()->m_pTaskManager;
    CTask* pTask = pTaskManager->FindTask(pDatagram->dwTaskID);
    if (!pTask)
        return FALSE;

    pDatagram->pSocket = NetEngine_GetDatagramPool(g_pNetEngine)->Alloc();
    // The result of Open is not checked; a failed open fails in AttachToProactor.
    pDatagram->pSocket->Open(wPort);
    pDatagram->pSocket->SetTask(pTask);

    CTask* pPeerTask = CSystem::GetInstance()->m_pTaskManager->FindTask(g_dwNetDatagramTaskID);
    CServiceObjectSock* pProactor = static_cast<CServiceObjectSock*>(pPeerTask->GetJob());
    CSockDatagram* pSock = pDatagram->pSocket;
    if (!pSock->AttachToProactor(pProactor, (HANDLE)pSock->m_hSocket))
        return FALSE;
    return g_pNetEngine->StartProactorWorkers(g_dwNetDatagramTaskID, 1, 0, 0, 0, NULL) != FALSE;
}

int NetEngine_SendDatagram(NETENGINE_DATAGRAM* pDatagram, const sockaddr* pTo, CMsg* pMsg)
{
    CSockDatagram* pSock = pDatagram->pSocket;
    LONG nRefs = pMsg->m_lRefCount;
    int nResult = pSock->Send(pTo, pMsg);
    if (!nResult)
        return 0;
    if (pMsg->m_lRefCount > nRefs)
        g_pNetEngine->ReleaseMsg(pMsg);
    return nResult;
}
