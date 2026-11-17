#include "Session.h"

#include "BSLib/thread/System.h"
#include "BSLib/thread/Task.h"
#include "BSNet/Msg.h"
#include "BSNet/NetEngine.h"
#include "BSNet/SockPool.h"
#include "BSNet/SockStream.h"

// SessionSlot_GetElapsedTicks (0x46e6e0): milliseconds since the socket last received data.
static DWORD SockStream_GetIdleTicks(CSockStream* pSock)
{
    LONG lLast = 0;
    InterlockedExchange(&lLast, (LONG)pSock->m_dwLastRecvTick);
    return GetTickCount() - (DWORD)lLast;
}

CTask* Session_FindTask(DWORD dwTaskID)
{
    if (!(dwTaskID & NETENGINE_TASK_MASK))
        return NULL;
    CTaskManager* pTaskManager = CSystem::GetInstance()->m_pTaskManager;
    if (!pTaskManager)
        return NULL;
    return pTaskManager->FindTask(dwTaskID);
}

CSession::CSession()
{
    ResetIoFields();
}

CSession::~CSession()
{
    CloseInternal(0);
}

// Session_ResetIoFields (0x492300).
void CSession::ResetIoFields()
{
    m_pFile = NULL;
    m_dwID = 0;
    m_pSocket = NULL;
    m_dwTaskID = 0;
    m_dwTransferred = 0;
    m_bTrusted = FALSE;
}

// Session_CloseInternal (0x492260).
void CSession::CloseInternal(DWORD dwReason)
{
    if (g_bNetEngineShutdown)
        return;

    if (m_dwTaskID && Session_FindTask(m_dwTaskID))
    {
        PostEvent(SESSION_EVENT_CLOSED, m_dwID,
                  m_pSocket ? m_pSocket->m_dwCloseState : dwReason);
    }
    if (m_dwID && g_pNetEngine && m_pSocket && m_pSocket->m_nRole == SOCK_ROLE_CONNECTED)
        NetEngine_Cast(g_pNetEngine)->ClearKeepAliveSession(m_dwID);
    if (m_pSocket)
    {
        SockPool_CloseSocket(m_pSocket, g_pNetEngine);
        m_pSocket = NULL;
    }
    CloseTransferFile();
    ResetIoFields();
}

// Session_DisconnectSocketIfAny (0x4922f0): Session_ClearField150 on the socket.
void CSession::DisconnectSocketIfAny()
{
    if (m_pSocket)
        m_pSocket->m_pTask = NULL;
}

// Session_PostEvent (0x492320).
void CSession::PostEvent(WORD wEvent, DWORD dwID, DWORD dwParam)
{
    CTask* pTask = Session_FindTask(m_dwTaskID);
    if (!pTask || !pTask->GetJob())
        return;

    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(wEvent);
    pMsg->m_dwSessionID = dwID;
    pMsg->WriteBytes(&dwID, 4);
    pMsg->WriteBytes(&dwParam, 4);
    if (pMsg->m_nCapacity >= MSG_HEADER_SIZE)
        pMsg->m_wReadPos = MSG_HEADER_SIZE;
    // BUG(NET-006): the result is ignored; a stopped queue leaks the event message.
    pTask->m_pQueue->Push(pMsg);
}

// Session_BindAndPostConnect (0x4923b0).
BOOL CSession::Bind(CSockStream* pSock, DWORD dwID, DWORD dwTaskID)
{
    CSingleLockBS lock(&m_cs, TRUE);
    m_pSocket = pSock;
    m_dwID = dwID;
    pSock->m_pSession = this;
    if (!AttachTask(dwTaskID))
        return FALSE;

    CServiceObject* pProactor = Session_FindTask(NETENGINE_TASK_IO)->GetJob();
    m_Link.Link();
    PostEvent(SESSION_EVENT_CONNECTED, dwID, m_pSocket->m_nRole);
    // BUG(NET-007): when the socket cannot be attached the caller reports failure, but the
    // session stays linked and in its slot and the connect event was already posted.
    if (!m_pSocket->AttachToProactor(static_cast<CServiceObjectSock*>(pProactor),
                                     (HANDLE)m_pSocket->m_hSocket))
    {
        return FALSE;
    }
    return TRUE;
}

// Session_AttachProactor (0x492530).
BOOL CSession::AttachTask(DWORD dwTaskID)
{
    if (!(dwTaskID & NETENGINE_TASK_MASK))
        return FALSE;
    CTask* pTask = CSystem::GetInstance()->m_pTaskManager->FindTask(dwTaskID);
    if (!pTask)
        return FALSE;
    m_pSocket->SetTask(pTask);
    m_dwTaskID = dwTaskID;
    return TRUE;
}

// SessionSlot_CheckExpired (0x492570).
BOOL CSession::IsAlive(DWORD dwTimeout)
{
    CSingleLockBS lock(&m_cs, TRUE);
    CSockStream* pSock = m_pSocket;
    if (!pSock || pSock->m_hSocket == INVALID_SOCKET)
        return FALSE;
    if (pSock->m_dwCloseState == 6)
        return FALSE;
    if (dwTimeout != (DWORD)-1 && SockStream_GetIdleTicks(pSock) > dwTimeout)
        return FALSE;
    return TRUE;
}

// CSession::StartHandshake (0x492670).
BOOL CSession::StartHandshake(SECURITY_HANDSHAKE_PARAM* pParam)
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (!m_pSocket)
        return FALSE;
    return m_pSocket->SendHandshakeRequest(pParam);
}

// CSession::SendMsg (0x492d70).
int CSession::SendMsg(CMsg* pMsg)
{
    if (m_pSocket && m_pSocket->m_hSocket != INVALID_SOCKET)
        return m_pSocket->SendMsg(pMsg);
    return SOCK_ERR_CLOSED;
}

// Session_PostEvent_wrap (0x4924e0).
void Session_PostHandshakeEvent(CSession* pSession)
{
    pSession->PostEvent(SESSION_EVENT_HANDSHAKED, pSession->m_dwID, 6);
}
