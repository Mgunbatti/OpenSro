#include "User.h"

#include <stdlib.h>

#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"

CChunkAllocatorST<sUser> g_UserPool;
CUserManager g_UserManager;

// ---------------------------------------------------------------------------------------------
// sUser

void __stdcall sUser::Release()
{
    if (bInUse)
    {
        bInUse = 0;
        g_UserPool.Free(this);
    }
}

sUser* UserPool_Alloc()
{
    sUser* pUser = g_UserPool.Alloc();
    pUser->bInUse = 1;
    return pUser;
}

// ---------------------------------------------------------------------------------------------
// CUserManager

CUserManager::CUserManager()
{
    m_bTrafficFilter = FALSE;
    m_dwTrafficMaxPackets = 100;
    m_dwTrafficPeriod = 60000;
}

CUserManager::~CUserManager()
{
    if (m_mapUser.size())
        BSAssertFail();
}

bool CUserManager::Register(sUser* pUser)
{
    if (Find(pUser->dwSessionID))
    {
        LogPrintf(0x1000000, "user register duplicated : %d, %d", pUser->dwSessionID,
                  pUser->dwJID);
        return false;
    }
    m_mapUser.insert(AgentUserMap::value_type(pUser->dwSessionID, pUser));
    return true;
}

bool CUserManager::Unregister(DWORD dwSessionID)
{
    AgentUserMap::iterator it = m_mapUser.find(dwSessionID);
    if (it == m_mapUser.end())
        return false;
    sUser* pUser = it->second;
    m_mapUser.erase(it);
    pUser->Release();
    return true;
}

sUser* CUserManager::Find(DWORD dwSessionID)
{
    AgentUserMap::iterator it = m_mapUser.find(dwSessionID);
    if (it == m_mapUser.end())
        return NULL;
    return it->second;
}

sUser* CUserManager::FindByJID(DWORD dwJID)
{
    for (AgentUserMap::iterator it = m_mapUser.begin(); it != m_mapUser.end(); ++it)
    {
        sUser* pUser = it->second;
        if (pUser->dwJID == dwJID)
            return pUser;
    }
    return NULL;
}

void CUserManager::RemoveAll()
{
    for (AgentUserMap::iterator it = m_mapUser.begin(); it != m_mapUser.end(); ++it)
        it->second->Release();
    m_mapUser.clear();
    g_UserPool.FreeAllBlocks();
}

void CUserManager::LoadTrafficFilterConfig()
{
    m_dwTrafficMaxPackets = 100;
    m_dwTrafficPeriod = 60000;
    const char* pszValue = ServerConfig_FindExtraValue("traffic_filter_activity");
    if (pszValue
        && CompareStringA(0x400, NORM_IGNORECASE | NORM_IGNOREKANATYPE, pszValue,
                          lstrlenA(pszValue), "service_on", lstrlenA("service_on")) == CSTR_EQUAL)
    {
        LogPrintf(0x2000000, "****************************");
        LogPrintf(0x2000000, "Traffic Filter Turned On !!!");
        m_bTrafficFilter = TRUE;
        pszValue = ServerConfig_FindExtraValue("traffic_filter_packet_count");
        if (pszValue)
            m_dwTrafficMaxPackets = atol(pszValue);
        pszValue = ServerConfig_FindExtraValue("traffic_filter_check_perid");
        if (pszValue)
            m_dwTrafficPeriod = atol(pszValue);
        LogPrintf(0x2000000, "Traffic Filter Configuration [%u Packets / %u ms]",
                  m_dwTrafficMaxPackets, m_dwTrafficPeriod);
        LogPrintf(0x2000000, "*****************************");
        return;
    }
    LogPrintf(0x1000000, "*****************************");
    LogPrintf(0x1000000, "Traffic Filter Turned Off !!!");
    LogPrintf(0x1000000, "*****************************");
    m_bTrafficFilter = FALSE;
}

// The count is compared for equality only: the check runs when the m_dwTrafficMaxPackets-th
// packet arrives, and the count starts again when that took at least the period.
bool CUserManager::CheckTraffic(sUser* pUser)
{
    if (!m_bTrafficFilter || !pUser)
        return true;
    if (++pUser->dwRecvCount != m_dwTrafficMaxPackets)
        return true;
    if (m_dwTrafficPeriod <= GetTickCount() - pUser->dwRecvTick)
    {
        pUser->dwRecvCount = 0;
        pUser->dwRecvTick = GetTickCount();
        return true;
    }
    const BYTE* pbyIP = (const BYTE*)&pUser->dwIP;
    LogPrintf(0x2000001, "[TrafficFilter] %s (RecvCnt : %d, Tic : %d, IP : %d.%d.%d.%d)",
              pUser->strID.c_str(), pUser->dwRecvCount, GetTickCount() - pUser->dwRecvTick,
              pbyIP[0], pbyIP[1], pbyIP[2], pbyIP[3]);
    return false;
}
