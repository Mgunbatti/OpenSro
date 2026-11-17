#include "SessionTable.h"

#include <algorithm>

#include "BSNet/NetEngine.h"
#include "BSNet/SockStream.h"

typedef CClassLink<CSession> CSessionLink;

// CNetEngine_SessionTable_ctor (0x47fe70).
CSessionTable::CSessionTable()
{
}

// CNetEngine_SessionTable_dtor (0x47fef0).
CSessionTable::~CSessionTable()
{
    CloseAll();
}

// SessionTable_InitLocked (0x47fff0).
BOOL CSessionTable::Initialize(DWORD dwMaxSessions)
{
    CSingleLockBS lock(&m_cs, TRUE);
    m_IDPool.m_dwGrowBy = dwMaxSessions;
    m_IDPool.Grow();
    m_vecSessions.resize(dwMaxSessions);
    std::fill(m_vecSessions.begin(), m_vecSessions.end(), (CSession*)NULL);
    return TRUE;
}

// SessionTable_CloseAll (0x47ff90): without the table lock.
void CSessionTable::CloseAll()
{
    for (DWORD i = 0; i < m_vecSessions.size(); ++i)
    {
        CSession* pSession = m_vecSessions[i];
        if (pSession)
        {
            pSession->CloseInternal(4);
            pSession->m_Link.Unlink();
        }
    }
    m_vecSessions.erase(m_vecSessions.begin(), m_vecSessions.end());
}

// CNetEngine::FindSession (0x4800c0).
CSession* CSessionTable::FindSession(DWORD dwID)
{
    CSingleLockBS lock(&m_cs, TRUE);
    return m_vecSessions[dwID];
}

// SessionTable_CreateNewSession (0x4945e0).
DWORD CSessionTable::CreateNewSession(CSockStream* pSock, DWORD dwTaskID)
{
    if (!pSock)
        return 0;

    CSingleLockBS lock(&m_cs, TRUE);
    CSession* pSession = m_SessionPool.Alloc();
    if (!pSession)
    {
        // "New Session allocation failed!"
        NetEngineLog(0x1000000,
                     "CreateNewSession() - New Session \xC7\xD2\xB4\xE7 \xBD\xC7\xC6\xD0!");
        return 0;
    }

    DWORD dwID = m_IDPool.Alloc();
    if (!dwID)
    {
        // "Session ID allocation failed!"
        // BUG(NET-009): the session taken from the pool is not given back.
        NetEngineLog(0x1000000,
                     "CreateNewSession() - \xBC\xBC\xBC\xC7 \xBE\xC6\xC0\xCC\xB5\xF0 "
                     "\xC7\xD2\xB4\xE7 \xBD\xC7\xC6\xD0!");
        return 0;
    }

    DWORD dwSlots = (DWORD)m_vecSessions.size();
    if (dwID >= dwSlots)
        m_vecSessions.resize(dwSlots + m_IDPool.m_dwGrowBy);
    m_vecSessions[dwID] = pSession;

    if (!pSession->Bind(pSock, dwID, dwTaskID))
        return 0;
    return dwID;
}

// SessionTable_Close (0x494750).
int CSessionTable::Close(DWORD dwSessionID, DWORD dwReason)
{
    if (!dwSessionID)
        return 1;

    CSingleLockBS lock(&m_cs, TRUE);
    CSession* pSession = m_vecSessions[dwSessionID];
    if (!pSession)
        return 1;

    // An accepted socket is disconnected first; the session is closed when that completes.
    CSockStream* pSock = pSession->m_pSocket;
    if (pSock->m_nRole == SOCK_ROLE_ACCEPTED && dwReason != 6)
    {
        BYTE bResult = pSock->DisconnectReuse(dwReason);
        if (bResult == 1 || bResult == 2)
            return 1;
    }

    pSession->m_Link.Unlink();
    m_SessionPool.Free(pSession);
    m_vecSessions[dwSessionID] = NULL;
    m_IDPool.Free(dwSessionID);
    pSession->CloseInternal(dwReason);
    return 1;
}

// SessionMgr_ReapExpiredSessions (0x4948d0).
int CSessionTable::ReapExpiredSessions(DWORD dwTimeout)
{
    CSingleLockBS lock(&m_cs, TRUE);
    for (CSessionLink* pLink = CSessionLink::IterFirst(); pLink; pLink = CSessionLink::IterNext())
    {
        CSession* pSession = CSession::FromLink(pLink);
        BOOL bAlive = pSession->m_bTrusted == TRUE ? pSession->IsAlive((DWORD)-1)
                                                   : pSession->IsAlive(dwTimeout);
        if (bAlive)
            continue;

        // An accepted socket whose disconnect could be issued is closed when it completes.
        CSockStream* pSock = pSession->m_pSocket;
        if (pSock && pSock->m_nRole == SOCK_ROLE_ACCEPTED && pSock->DisconnectReuse(2) == 1)
            continue;

        DWORD dwID = pSession->m_dwID;
        pSession->m_Link.Unlink();
        m_SessionPool.Free(pSession);
        m_vecSessions[dwID] = NULL;
        if (dwID)
            m_IDPool.Free(dwID);
        pSession->CloseInternal(2);
    }
    return CSessionLink::ms_nCount;
}

// CNetEngine_ReapClosedSessions (0x494a70).
void CSessionTable::ReapClosedSessions()
{
    CSingleLockBS lock(&m_cs, TRUE);
    for (CSessionLink* pLink = CSessionLink::IterFirst(); pLink; pLink = CSessionLink::IterNext())
    {
        CSession* pSession = CSession::FromLink(pLink);
        CSockStream* pSock = pSession->m_pSocket;
        if (!pSock || pSock->m_nRole != SOCK_ROLE_ACCEPTED || pSession->m_bTrusted == TRUE)
            continue;

        DWORD dwID = pSession->m_dwID;
        pSession->CloseInternal(1);
        pSession->m_Link.Unlink();
        m_SessionPool.Free(pSession);
        m_vecSessions[dwID] = NULL;
        if (dwID)
            m_IDPool.Free(dwID);
    }
}

// SessionList_DisconnectAll (0x494510).
void CSessionTable::DisconnectAll()
{
    CSingleLockBS lock(&m_cs, TRUE);
    for (CSessionLink* pLink = CSessionLink::IterFirst(); pLink; pLink = CSessionLink::IterNext())
        CSession::FromLink(pLink)->DisconnectSocketIfAny();
}

void CSessionTable::GetCounters(DWORD* pdwTotal, DWORD* pdwUsed, DWORD* pdwFree)
{
    CSingleLockBS lock(&m_cs, TRUE);
    *pdwTotal = m_SessionPool.m_nCapacity;
    DWORD dwUsed = m_SessionPool.m_nInUse;
    *pdwUsed = dwUsed;
    *pdwFree = *pdwTotal - dwUsed;
}
