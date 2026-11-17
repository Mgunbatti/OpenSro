#include "SockListener.h"

#include "BSLib/thread/ServiceObject.h"
#include "BSNet/NetEngine.h"
#include "BSNet/SockPool.h"
#include "BSNet/SockStream.h"
#include "BSNet/SockSystem.h"

int CSockListener::ms_nPendingAccepts = 0;

CSockListener::CSockListener()
{
    m_nBacklog = 0x7FFFFFFF;
    m_nKind = SOCK_KIND_LISTENER;
}

CSockListener::~CSockListener()
{
    Close();
}

int CSockListener::Close()
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (m_hSocket != INVALID_SOCKET)
    {
        closesocket(m_hSocket);
        m_hSocket = INVALID_SOCKET;
    }
    return 0;
}

BOOL CSockListener::CreateAndBind(const sockaddr* pAddr)
{
    if (!CreateSocket(SOCK_ROLE_ACCEPTED))
        return FALSE;

    int nOn = 1;
    setsockopt(m_hSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&nOn, 4);
    if (bind(m_hSocket, pAddr, sizeof(sockaddr_in)) == SOCKET_ERROR)
        return FALSE;
    m_nBacklog = 0x7FFFFFFF;
    ms_nPendingAccepts = 0;
    return TRUE;
}

BOOL CSockListener::ListenAndPostAccepts(CServiceObjectSock* pProactor)
{
    if (listen(m_hSocket, m_nBacklog) == SOCKET_ERROR)
    {
        WSAGetLastError();
        return FALSE;
    }
    if (!AttachToProactor(pProactor, (HANDLE)m_hSocket))
        return FALSE;

    CSingleLockBS lock(&m_cs, TRUE);
    if (g_nNetIoModel == 0)
    {
        int nCount = NetEngine_GetAcceptCount(g_pNetEngine);
        if (nCount < 10)
            nCount = 10;
        for (int i = 0; i < nCount; )
        {
            if (PostOneAccept(g_pNetEngine) != TRUE)
                return FALSE;
            ms_nPendingAccepts += 1;
            i += 1;
        }
    }
    return TRUE;
}

// CSockListener_CreateSessionForAccepted
int CSockListener::CreateSessionForAccepted(CSockStream* pSock)
{
    if (!pSock)
        return SOCK_ERR_ACCEPT;
    // BUG(NET-011): the session (and its connect event, and its first receive) exists before
    // AcceptPeer stores the peer address and checks the connect time.
    if (!NetEngine_CreateNewSession(g_pNetEngine, pSock,
                                    NetEngine_GetAcceptSessionParam(g_pNetEngine)))
    {
        return SOCK_ERR_ACCEPT;
    }
    return pSock->AcceptPeer(m_hSocket) ? SOCK_ERR_ACCEPT : 0;
}

int CSockListener::OnIOCompletion(DWORD, LPOVERLAPPED pOverlapped)
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (m_hSocket == INVALID_SOCKET)
        return 0;

    int nResult;
    if (g_nNetIoModel == 0)
    {
        CSockStream* pSock = (CSockStream*)((SOCK_OVERLAPPED*)pOverlapped)->pOwner;
        ReplenishAcceptsLocked();
        nResult = CreateSessionForAccepted(pSock);
        // DisconnectReuse never returns 0, so the engine is never told here.
        if (nResult == SOCK_ERR_ACCEPT && !pSock->DisconnectReuse(6))
        {
            DWORD dwState = pSock->m_dwCloseState;
            NetEngine_CloseSession(pSock->GetOwnerSessionID(), dwState);
        }
    }
    else
    {
        CSockStream* pSock = NetEngine_GetAcceptPool(g_pNetEngine)->Alloc();
        if (!pSock->PrepareAccept(m_hSocket))
        {
            Sock_ReleaseToAcceptPool(pSock, g_pNetEngine);
            return SOCK_ERR_ACCEPT;
        }
        nResult = CreateSessionForAccepted(pSock);
    }
    return nResult;
}

// CSockListener_ReplenishAcceptsLocked: one AcceptEx completed; post enough new ones to keep
// the configured number pending.
void CSockListener::ReplenishAcceptsLocked()
{
    CSingleLockBS lock(&m_csAccept, TRUE);
    int nTarget = NetEngine_GetAcceptCount(g_pNetEngine);
    if (nTarget < 10)
        nTarget = 10;
    int nPending = --ms_nPendingAccepts;
    int nPost = 0;
    if (nTarget > nPending)
    {
        if (g_nNetIoModel == 0)
            nPost = nTarget - nPending;
        for (; nPost > 0; --nPost)
        {
            if (PostOneAccept(g_pNetEngine) == TRUE)
                ms_nPendingAccepts += 1;
        }
    }
}

BOOL CSockListener::RepostAcceptForSocket(CSockStream* pSock)
{
    if (!pSock->PrepareAccept(m_hSocket))
        return FALSE;
    pSock->ClearCloseStateLocked();
    EnterCriticalSection(&m_csAccept.m_cs);
    ++ms_nPendingAccepts;
    m_csAccept.Unlock();
    return TRUE;
}

BOOL CSockListener::AttachToProactor(CServiceObjectSock* pProactor, HANDLE hHandle)
{
    if (!pProactor)
        return FALSE;
    return pProactor->AssociateHandle(hHandle, this);
}

BOOL CSockListener::PostOneAccept(IBSNet* pEngine)
{
    CSockStream* pSock = NetEngine_GetAcceptPool(pEngine)->Alloc();
    if (!pSock)
        return FALSE;
    if (!pSock->PrepareAccept(NetEngine_GetListener(pEngine)->m_hSocket))
    {
        Sock_ReleaseToAcceptPool(pSock, g_pNetEngine);
        return FALSE;
    }
    return TRUE;
}

int Listener_ForwardHandlerClosed(CSockListener* pListener, CBaseSocket* pSock, DWORD dwReason)
{
    CSingleLockBS lock(&pListener->m_cs, TRUE);
    if (pSock->m_nKind == SOCK_KIND_LISTENER)
        return 1;
    return NetEngine_CloseSession(static_cast<CSockStream*>(pSock)->GetOwnerSessionID(),
                                  dwReason);
}
