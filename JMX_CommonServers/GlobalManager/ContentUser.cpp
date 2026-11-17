#include "ContentUser.h"

#include "GlobalManagerDB.h"
#include "GlobalManagerJobs.h"
#include "BSLib/DB/DBQuery.h"
#include "BSLib/Util/TimeSpan.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerDataExtern.h"

using namespace ServerFramework;

CChunkAllocatorST<sContentUser> g_ContentUserPool;
CChunkAllocatorST<CSingleIPContentUserMap> g_SingleIPContentUserMapPool;
CContentUserManager g_ContentUserManager;

int g_nMaxUserPerIP = -1;

#pragma pack(push, 1)
// Row of SK_gamebang_ip.
struct FRANCHISE_IP_ROW
{
    DWORD   dwIP;           // +0
    short   nCapacity;      // +4
};
#pragma pack(pop)

C_ASSERT(sizeof(FRANCHISE_IP_ROW) == 6);

static CERT_SHARD* FindCertShard(WORD wShardID)
{
    std::map<WORD, CERT_SHARD*>::iterator it = g_mapCertShard.find(wShardID);
    if (it == g_mapCertShard.end())
        return NULL;
    return it->second;
}

// ---------------------------------------------------------------------------------------------
// Play time, pools, helpers

void CONTENT_USER_PLAYTIME::SetNow()
{
    dwTime = CDateTime::GetCurrent().GetTime();
    wPlayTime = 0;
}

int CONTENT_USER_PLAYTIME::GetPlayTime() const
{
    CTimeSpan now(CDateTime::GetCurrent().GetTime());
    CTimeSpan start((LONG)dwTime);
    return (now - start).GetTotalSeconds() + wPlayTime;
}

void __stdcall sContentUser::Release()
{
    if (bInUse)
    {
        bInUse = 0;
        g_ContentUserPool.Free(this);
    }
}

void __stdcall CSingleIPContentUserMap::Release()
{
    if (bInUse)
    {
        bInUse = 0;
        g_SingleIPContentUserMapPool.Free(this);
    }
}

sContentUser* ContentUserPool_Alloc()
{
    sContentUser* pUser = g_ContentUserPool.Alloc();
    pUser->bInUse = 1;
    return pUser;
}

CSingleIPContentUserMap* SingleIPContentUserMapPool_Alloc()
{
    CSingleIPContentUserMap* pMap = g_SingleIPContentUserMapPool.Alloc();
    pMap->bInUse = 1;
    return pMap;
}

bool ContentUser_IsNotToday(DWORD dwTime)
{
    CDateTime now = CDateTime::GetCurrent();
    CDateTime time((__time32_t)dwTime);
    if (now.GetDay() == time.GetDay() && now.GetMonth() == time.GetMonth()
        && now.GetYear() == time.GetYear())
    {
        return false;
    }
    return true;
}

bool ContentUser_WildcardMatch(const char* pszPattern, const char* pszText)
{
    const char* pszRetryPattern = NULL;
    const char* pszRetryText = NULL;
    while (*pszText && *pszPattern != '*')
    {
        if (*pszPattern != *pszText && *pszPattern != '?')
            return false;
        ++pszPattern;
        ++pszText;
    }
    while (*pszText)
    {
        if (*pszPattern == '*')
        {
            if (!*++pszPattern)
                return true;
            pszRetryPattern = pszPattern;
            pszRetryText = pszText + 1;
        }
        else if (*pszPattern == *pszText || *pszPattern == '?')
        {
            ++pszPattern;
            ++pszText;
        }
        else
        {
            pszPattern = pszRetryPattern;
            pszText = pszRetryText++;
        }
    }
    while (*pszPattern == '*')
        ++pszPattern;
    return *pszPattern == 0;
}

bool ContentUser_IsOfficialIP(DWORD dwIP)
{
    DWORD dwNet = dwIP & 0xFFFFFF;
    if (dwNet != 0xAC95DD && dwNet != 0x82093B)
        return false;
    LogPrintf(0x2000001, "[GlobalManager] Official IP Detected, Official IP Pass the Max User per "
                         "IP Limit Check (%d.%d.%d.%d)",
              (BYTE)dwIP, (BYTE)(dwIP >> 8), (BYTE)(dwIP >> 16), dwIP >> 24);
    return true;
}

void ContentUser_UpdatePlayTime(sContentUser* pUser)
{
    if (!pUser->PlayTime.dwTime)
        return;
    COverlapJob_UpdatePlayTimeOfUser* pJob = OverlapJob_AllocUpdatePlayTimeOfUser();
    if (!pJob)
    {
        BSAssertFail();
        return;
    }
    int nPlayTime = pUser->PlayTime.GetPlayTime();
    pJob->Setup(pUser->dwJID, (WORD)nPlayTime);
    g_pGlobalManagerProcess->SendOverlapJob(pJob, NULL);
}

DWORD ContentUser_TakeUnregisteredCount()
{
    DWORD dw = g_ContentUserManager.m_dwUnregistered;
    g_ContentUserManager.m_dwUnregistered = 0;
    return dw;
}

DWORD ContentUser_TakeRegisteredCount()
{
    DWORD dw = g_ContentUserManager.m_dwRegistered;
    g_ContentUserManager.m_dwRegistered = 0;
    return dw;
}

// ---------------------------------------------------------------------------------------------
// CSingleIPContentUserMap

void CSingleIPContentUserMap::SetLimit(int nLimit)
{
    if ((DWORD)nLimit < m_mapUser.size())
        BSAssertFail();
    m_nLimit = nLimit;
}

sContentUser* CSingleIPContentUserMap::Find(DWORD dwJID)
{
    ContentUserMap::iterator it = m_mapUser.find(dwJID);
    if (it == m_mapUser.end())
        return NULL;
    return it->second;
}

void CSingleIPContentUserMap::Insert(sContentUser* pUser)
{
    m_mapUser.insert(ContentUserMap::value_type(pUser->dwJID, pUser));
}

void CSingleIPContentUserMap::Remove(DWORD dwJID)
{
    ContentUserMap::iterator it = m_mapUser.find(dwJID);
    if (it == m_mapUser.end())
    {
        BSAssertFail();
        return;
    }
    sContentUser* pUser = it->second;
    m_mapUser.erase(it);
    pUser->Release();
}

void CSingleIPContentUserMap::ReleaseAll()
{
    for (ContentUserMap::iterator it = m_mapUser.begin(); it != m_mapUser.end(); ++it)
        it->second->Release();
    m_mapUser.clear();
}

// ---------------------------------------------------------------------------------------------
// CContentUserManager

CContentUserManager::CContentUserManager()
{
    m_bChanged = FALSE;
    m_nDailyVisited = 0;
    m_nTotalVisited = 0;
    m_dwRegistered = 0;
    m_dwUnregistered = 0;
    m_nUserCount = 0;
}

CContentUserManager::~CContentUserManager()
{
}

CSingleIPContentUserMap* CContentUserManager::FindIP(DWORD dwIP)
{
    ContentUserIPMap::iterator it = m_mapIP.find(dwIP);
    if (it == m_mapIP.end())
        return NULL;
    return it->second;
}

CSingleIPContentUserMap* CContentUserManager::AddIP(DWORD dwIP, BOOL bFranchise)
{
    ContentUserIPMap::iterator it = m_mapIP.find(dwIP);
    if (it != m_mapIP.end())
        return it->second;
    CSingleIPContentUserMap* pMap = SingleIPContentUserMapPool_Alloc();
    if (bFranchise)
    {
        pMap->SetLimit(-1);
        pMap->m_bFranchise = TRUE;
    }
    else
    {
        pMap->SetLimit(g_nMaxUserPerIP);
        pMap->m_bFranchise = FALSE;
    }
    if (ContentUser_IsOfficialIP(dwIP))
        pMap->SetLimit(-1);
    m_mapIP.insert(ContentUserIPMap::value_type(dwIP, pMap));
    m_nOccupiedIPCount = (int)m_mapIP.size();
    return pMap;
}

BYTE CContentUserManager::CheckLogin(DWORD dwJID, DWORD dwIP)
{
    if (Find(dwJID))
        return 1;
    CSingleIPContentUserMap* pMap = FindIP(dwIP);
    if (pMap)
    {
        if (!pMap->CanAddUser())
            return 2;
        if (pMap->Find(dwJID))
            return 5;
    }
    return 0;
}

BYTE CContentUserManager::Register(sContentUser* pUser)
{
    BYTE byResult = CheckLogin(pUser->dwJID, pUser->dwIP);
    if (byResult)
        return byResult;
    SYSTEMTIME st;
    GetLocalTime(&st);
    sContentUser* pNew;
    {
        CSingleLockBS lock(&m_cs, TRUE);
        CSingleIPContentUserMap* pMap = FindIP(pUser->dwIP);
        if (!pMap)
        {
            pMap = AddIP(pUser->dwIP, FALSE);
            byResult = CheckLogin(pUser->dwJID, pUser->dwIP);
            if (byResult)
                return byResult;
        }
        pNew = ContentUserPool_Alloc();
        pNew->dwIP = pUser->dwIP;
        pNew->dwJID = pUser->dwJID;
        pNew->strID.assign(pUser->strID, 0, std::string::npos);
        pNew->byContentID = pUser->byContentID;
        pNew->wShardID = pUser->wShardID;
        pNew->wAgentBodyID = pUser->wAgentBodyID;
        pNew->stLogin = st;
        pNew->strMAC.assign(pUser->strMAC, 0, std::string::npos);
        pNew->b5C = 0;
        pNew->b5D = 0;
        pNew->dw60 = 0;
        pNew->dw64 = 0;
        pNew->dwTick68 = 0;
        pNew->dwRelayID = 0;
        pNew->dwSessionID = 0;
        pUser->byIPType = pMap->m_bFranchise ? 3 : 4;
        pNew->byIPType = pUser->byIPType;
        pNew->PlayTime.dwTime = pUser->PlayTime.dwTime;
        pNew->PlayTime.wPlayTime = pUser->PlayTime.wPlayTime;
        pMap->Insert(pNew);
        m_mapUser.insert(ContentUserMap::value_type(pNew->dwJID, pNew));
        m_nUserCount = (int)m_mapUser.size();
    }
    DWORD dwJID = pNew->dwJID;
    ++m_dwRegistered;
    m_bChanged = TRUE;
    if (st.wDay != m_stLastLogin.wDay)
    {
        m_setDailyJID.clear();
        m_nDailyVisited = 0;
    }
    m_stLastLogin = st;
    if (m_setDailyJID.find(dwJID) == m_setDailyJID.end())
    {
        m_setDailyJID.insert(dwJID);
        m_nDailyVisited = (int)m_setDailyJID.size();
    }
    if (m_setTotalJID.find(dwJID) == m_setTotalJID.end())
    {
        m_setTotalJID.insert(dwJID);
        m_nTotalVisited = (int)m_setTotalJID.size();
    }
    return 0;
}

bool CContentUserManager::Unregister(DWORD dwJID, DWORD* pdwIP)
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (pdwIP)
        *pdwIP = 0;
    ContentUserMap::iterator it = m_mapUser.find(dwJID);
    if (it == m_mapUser.end())
        return false;
    sContentUser* pUser = it->second;
    CSingleIPContentUserMap* pMap = FindIP(pUser->dwIP);
    if (pdwIP)
        *pdwIP = pUser->dwIP;
    ContentUser_UpdatePlayTime(pUser);
    // The address map is not checked (it always exists for a registered user).
    pMap->Remove(dwJID);
    m_mapUser.erase(it);
    ++m_dwUnregistered;
    m_nUserCount = (int)m_mapUser.size();
    m_bChanged = TRUE;
    return true;
}

int CContentUserManager::UnregisterByAgent(WORD wAgentBodyID)
{
    int nCount = 0;
    CSingleLockBS lock(&m_cs, TRUE);
    ContentUserMap::iterator it = m_mapUser.begin();
    while (it != m_mapUser.end())
    {
        sContentUser* pUser = it->second;
        if (pUser->wAgentBodyID != wAgentBodyID)
        {
            ++it;
            continue;
        }
        ContentUser_UpdatePlayTime(pUser);
        ContentUserMap::iterator itNext = it;
        ++itNext;
        CSingleIPContentUserMap* pMap = FindIP(pUser->dwIP);
        if (pMap)
            pMap->Remove(pUser->dwJID);
        m_mapUser.erase(it);
        it = itNext;
        ++nCount;
    }
    m_nUserCount = (int)m_mapUser.size();
    m_bChanged = TRUE;
    return nCount;
}

sContentUser* CContentUserManager::Find(DWORD dwJID)
{
    CSingleLockBS lock(&m_cs, TRUE);
    ContentUserMap::iterator it = m_mapUser.find(dwJID);
    if (it == m_mapUser.end())
        return NULL;
    return it->second;
}

sContentUser* CContentUserManager::FindByID(LPCSTR pszID)
{
    CSingleLockBS lock(&m_cs, TRUE);
    for (ContentUserMap::iterator it = m_mapUser.begin(); it != m_mapUser.end(); ++it)
    {
        sContentUser* pUser = it->second;
        const char* pszName = pUser->strID.c_str();
        if (CompareStringA(LOCALE_USER_DEFAULT, 0, pszName, lstrlenA(pszName), pszID,
                           lstrlenA(pszID)) == CSTR_EQUAL)
        {
            return pUser;
        }
    }
    return NULL;
}

bool CContentUserManager::WriteUserQuery(CMassiveMsg* pMsg, LPCSTR pszPattern)
{
    BYTE bAll = 0;
    if (CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE | NORM_IGNOREKANATYPE, pszPattern,
                       lstrlenA(pszPattern), "*", lstrlenA("*")) == CSTR_EQUAL)
    {
        bAll = 1;
    }
    CSingleLockBS lock(&m_cs, TRUE);
    BYTE byFlag;
    for (ContentUserMap::iterator it = m_mapUser.begin(); it != m_mapUser.end(); ++it)
    {
        sContentUser* pUser = it->second;
        if (bAll != 1 && (bAll || !ContentUser_WildcardMatch(pszPattern, pUser->strID.c_str())))
            continue;
        byFlag = 1;
        pMsg->Write(&byFlag, 1);
        pMsg->Write(&pUser->dwIP, 4);
        pMsg->Write(&pUser->dwJID, 4);
        pMsg->Write(&pUser->byIPType, 1);
        DWORD dwLen = (WORD)pUser->strID.size();
        pMsg->Write(&dwLen, 2);
        if ((WORD)dwLen >= 1)
            pMsg->Write(pUser->strID.c_str(), (WORD)dwLen);
        pMsg->Write(&pUser->byContentID, 1);
        pMsg->Write(&pUser->wShardID, 2);
        pMsg->Write(&pUser->stLogin, 16);
    }
    byFlag = 0;
    pMsg->Write(&byFlag, 1);
    return true;
}

void CContentUserManager::RecountShardUsers()
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (!m_bChanged)
        return;
    CertShardList* pShards = g_pCertShardList;
    for (CertShardList::iterator it = pShards->begin(); it != pShards->end(); ++it)
        (*it)->w557 = 0;
    for (ContentUserMap::iterator it = m_mapUser.begin(); it != m_mapUser.end(); ++it)
    {
        sContentUser* pUser = it->second;
        CERT_SHARD* pShard = FindCertShard(pUser->wShardID);
        if (pShard)
        {
            ++pShard->w557;
        }
        else
        {
            // "users in a strange shard : %s(%d)" (Korean, code page 949)
            LogPrintf(0x2000000, "\xc0\xcc\xbb\xf3\xc7\xd1 shard\xbf\xa1 \xb5\xe9\xbe\xee "
                                 "\xc0\xd6\xb4\xc2 \xb3\xd1\xb5\xe9 : %s(%d)",
                      pUser->strID.c_str(), pUser->wShardID);
        }
    }
}

void CContentUserManager::WriteShardUserCount(CMsg* pMsg)
{
    RecountShardUsers();
    CertShardList* pShards = g_pCertShardList;
    BYTE byCount = (BYTE)pShards->size();
    pMsg->WriteBytes(&byCount, 1);
    for (CertShardList::iterator it = pShards->begin(); it != pShards->end(); ++it)
    {
        CERT_SHARD* pShard = *it;
        pMsg->WriteBytes(&pShard->wID, 2);
        pMsg->WriteBytes(&pShard->w557, 2);
    }
}

void CContentUserManager::WriteBillingUserList(CMassiveMsg* pMsg)
{
    CSingleLockBS lock(&m_cs, TRUE);
    DWORD dwValue = (WORD)m_mapUser.size();
    pMsg->Write(&dwValue, 2);
    DWORD dwTick = GetTickCount();
    for (ContentUserMap::iterator it = m_mapUser.begin(); it != m_mapUser.end(); ++it)
    {
        sContentUser* pUser = it->second;
        dwValue = pUser->dwTick68 ? dwTick - pUser->dwTick68 : 0;
        DWORD dwContent = pUser->byContentID;
        DWORD dwShard = pUser->wShardID;
        pMsg->Write(&pUser->dwIP, 4);
        pMsg->Write(&pUser->dwJID, 4);
        pMsg->Write(&dwContent, 4);
        pMsg->Write(&dwShard, 4);
        pMsg->Write(&pUser->b5C, 1);
        pMsg->Write(&pUser->dw64, 4);
        pMsg->Write(&dwValue, 4);
        dwValue = (WORD)pUser->strMAC.size();
        pMsg->Write(&dwValue, 2);
        if ((WORD)dwValue)
            pMsg->Write(pUser->strMAC.c_str(), (WORD)dwValue);
    }
}

void CContentUserManager::ResetPlayTimes()
{
    for (ContentUserMap::iterator it = m_mapUser.begin(); it != m_mapUser.end(); ++it)
        it->second->PlayTime.SetNow();
}

void CContentUserManager::RemoveAll()
{
    CSingleLockBS lock(&m_cs, TRUE);
    for (ContentUserIPMap::iterator it = m_mapIP.begin(); it != m_mapIP.end(); ++it)
    {
        CSingleIPContentUserMap* pMap = it->second;
        pMap->ReleaseAll();
        pMap->Release();
    }
    m_mapIP.clear();
    m_mapUser.clear();
    m_setDailyJID.clear();
    m_setTotalJID.clear();
}

int CContentUserManager::OnFranchiseIPRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    FRANCHISE_IP_ROW* pRow = (FRANCHISE_IP_ROW*)pQuery->m_pBuffer;
    if (!pQuery->m_bRowFetched)
    {
        pRow->dwIP = 0;
        pRow->nCapacity = 0;
        return 1;
    }
    CSingleIPContentUserMap* pMap = AddIP(pRow->dwIP, TRUE);
    pMap->SetLimit(pRow->nCapacity);
    pMap->m_bFranchise = TRUE;
    ++m_nFranchiseIPCount;
    return 1;
}

int ContentUser_LoadFranchiseIPs()
{
    CContentUserManager* pManager = &g_ContentUserManager;
    CSingleLockBS lock(&pManager->m_cs, TRUE);
    FRANCHISE_IP_ROW row;
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pBuffer = (BYTE*)&row;
    query.m_pSession = g_pAccountDBSession;
    query.m_nRowSize = sizeof(row);
    query.m_nBufferSize = sizeof(row);
    CClassTrigger<CContentUserManager>* pTrigger =
        new CClassTrigger<CContentUserManager>(pManager);
    if (pTrigger)
        pTrigger->m_pfnTrigger2 = &CContentUserManager::OnFranchiseIPRow;
    query.m_pTrigger = pTrigger;
    query.m_nSession = 1;
    pManager->m_nFranchiseIPCount = 0;
    for (ContentUserIPMap::iterator it = pManager->m_mapIP.begin();
         it != pManager->m_mapIP.end(); ++it)
    {
        it->second->m_bFranchise = FALSE;
    }
    if (query.ExecuteFormat("select ip, capacity from SK_gamebang_ip") < 0)
    {
        delete query.m_pTrigger;
        return -1;
    }
    delete query.m_pTrigger;
    LogPrintf(0, "franchied ip refreshed : %d", pManager->m_nFranchiseIPCount);
    return pManager->m_nFranchiseIPCount;
}

void ContentUser_BroadcastShardUserCount()
{
    g_ContentUserManager.RecountShardUsers();
    CertShardList* pShards = g_pCertShardList;
    ServerBodyList* pBodies = g_pServerBodyList;
    CERT_MODULE* pGateway = CertModule_FindByName("GatewayServer");
    for (ServerBodyList::iterator it = pBodies->begin(); it != pBodies->end(); ++it)
    {
        SERVER_BODY* pBody = *it;
        if (pBody->pModule != pGateway)
            continue;
        if (pBody->nState != 5 && pBody->nState != 4)
            continue;
        CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
        pMsg->SetOpcode(0x6105);    // shard user counts
        BYTE byCount = (BYTE)pShards->size();
        pMsg->WriteBytes(&byCount, 1);
        for (CertShardList::iterator itShard = pShards->begin(); itShard != pShards->end();
             ++itShard)
        {
            CERT_SHARD* pShard = *itShard;
            pMsg->WriteBytes(&pShard->wID, 2);
            pMsg->WriteBytes(&pShard->w557, 2);
        }
        if (!ServerBody_SendMsg(pBody->wID, pMsg))
            g_pBSNet->ReleaseMsg(pMsg);
    }
}
