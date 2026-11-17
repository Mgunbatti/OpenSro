#include "KeepAlive.h"

#include "BSNet/NetEngine.h"
#include "BSNet/SockSystem.h"

// KeepAliveList_Add (0x481960).
void CNetEngine::AddKeepAliveEntry(const KEEPALIVE_ENTRY* pEntry)
{
    CSingleLockBS lock(&m_csKeepAlive, TRUE);
    m_listKeepAlive.push_back(*pEntry);
}

// KeepAlive_TryConnect (0x481a30).
void CNetEngine::TryKeepAliveConnect()
{
    // BUG(NET-012): the blocking connects run with the keep-alive lock held, which also
    // guards CleanupDisconnectedSessions and the close of every connected session.
    CSingleLockBS lock(&m_csKeepAlive, TRUE);
    for (KeepAliveList::iterator it = m_listKeepAlive.begin(); it != m_listKeepAlive.end(); ++it)
    {
        KEEPALIVE_ENTRY& e = *it;
        if (e.dwSessionID)
            continue;

        DWORD dwSessionID = ConnectSockAddr((const sockaddr*)&e.To,
                                            e.Bind.sin_addr.s_addr ? (const sockaddr*)&e.Bind
                                                                   : NULL,
                                            e.dwTaskID, 0);
        if (dwSessionID)
        {
            e.dwSessionID = dwSessionID;
            e.nFailCount = 0;
            continue;
        }

        if (++e.nFailCount < 0)
            e.nFailCount = 0;
        if (e.nFailCount > 15)
        {
            const BYTE* pTo = (const BYTE*)&e.To.sin_addr;
            const BYTE* pBind = (const BYTE*)&e.Bind.sin_addr;
            NetEngineLog(0x2000001,
                         "cannot establish keep alive session : %d.%d.%d.%d %d "
                         "(bind %d.%d.%d.%d)",
                         pTo[0], pTo[1], pTo[2], pTo[3], htons(e.To.sin_port),
                         pBind[0], pBind[1], pBind[2], pBind[3]);
        }
    }
}

// KeepAliveList_ClearSession (0x481bc0).
void CNetEngine::ClearKeepAliveSession(DWORD dwSessionID)
{
    CSingleLockBS lock(&m_csKeepAlive, TRUE);
    for (KeepAliveList::iterator it = m_listKeepAlive.begin(); it != m_listKeepAlive.end(); ++it)
    {
        if (it->dwSessionID == dwSessionID)
            it->dwSessionID = 0;
    }
}

BOOL CNetEngine::RemoveKeepAliveTarget(DWORD dwSessionID)
{
    CSingleLockBS lock(&m_csKeepAlive, TRUE);
    for (KeepAliveList::iterator it = m_listKeepAlive.begin(); it != m_listKeepAlive.end(); ++it)
    {
        if (it->dwSessionID == dwSessionID)
        {
            m_listKeepAlive.erase(it);
            return TRUE;
        }
    }
    return FALSE;
}

void CNetEngine::AddKeepAliveTarget(const char* pszHost, u_short wPort, const char* pszBindIP,
                                    DWORD dwTaskID, DWORD dwSessionID)
{
    ULONG ulBind = ResolveHostIPv4(pszBindIP);
    ULONG ulHost = ResolveHostIPv4(pszHost);
    if (ulHost == INADDR_NONE || ulBind == INADDR_NONE)
        return;

    sockaddr_in to;
    sockaddr_in bind;
    memset(&to, 0, sizeof(to));
    memset(&bind, 0, sizeof(bind));
    to.sin_family = AF_INET;
    to.sin_port = htons(wPort);
    to.sin_addr.s_addr = ulHost;
    // BUG(NET-003): like Connect, the bind address is resolved but never used.
    KEEPALIVE_ENTRY entry;
    entry.Init((const sockaddr*)&to, bind.sin_addr.s_addr ? (const sockaddr*)&bind : NULL,
               dwTaskID, dwSessionID);
    AddKeepAliveEntry(&entry);
}

// KeepAlive_TryConnect as the maintainer calls it.
void NetEngine_KeepAliveTryConnect(IBSNet* pEngine)
{
    NetEngine_Cast(pEngine)->TryKeepAliveConnect();
}
