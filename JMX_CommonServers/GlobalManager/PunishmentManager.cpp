#include "PunishmentManager.h"

#include "ContentUser.h"
#include "GlobalManagerDB.h"
#include "BSLib/BSDebug.h"
#include "BSLib/DB/DBQuery.h"
#include "BSLib/thread/Task.h"
#include "BSNet/IBSNet.h"
#include "BSNet/Msg.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerProcess.h"
#include "ServerFramework/Trigger.h"

using namespace ServerFramework;

CChunkAllocatorST<Punishment_Info_Internal> g_PunishmentInfoPool;
CPunishmentManager* g_pPunishmentManager = NULL;
CPunishmentManager g_PunishmentManager;

// ---------------------------------------------------------------------------------------------
// Helpers

void __stdcall Punishment_Info_Internal::Release()
{
    if (bInUse)
    {
        bInUse = 0;
        g_PunishmentInfoPool.Free(this);
    }
}

Punishment_Info_Internal* PunishmentInfoPool_Alloc()
{
    Punishment_Info_Internal* p = g_PunishmentInfoPool.Alloc();
    p->bInUse = 1;
    return p;
}

FILETIME Punishment_TimestampToFileTime(const TIMESTAMP_STRUCT* pTime)
{
    // The output starts as the first two dwords of the SYSTEMTIME being built (year, month,
    // day of week 0, day); SystemTimeToFileTime leaves it when the date is invalid.
    FILETIME ft;
    ft.dwLowDateTime = pTime->year | ((DWORD)pTime->month << 16);
    ft.dwHighDateTime = (DWORD)pTime->day << 16;
    SYSTEMTIME st;
    st.wYear = pTime->year;
    st.wMonth = pTime->month;
    st.wDayOfWeek = 0;
    st.wDay = pTime->day;
    st.wHour = pTime->hour;
    st.wMinute = pTime->minute;
    st.wSecond = pTime->second;
    st.wMilliseconds = 0;
    SystemTimeToFileTime(&st, &ft);
    return ft;
}

SYSTEMTIME* Punishment_TimestampToSystemTime(SYSTEMTIME* pOut, const TIMESTAMP_STRUCT* pTime)
{
    memset(pOut, 0, sizeof(*pOut));
    pOut->wYear = pTime->year;
    pOut->wMonth = pTime->month;
    pOut->wDay = pTime->day;
    pOut->wHour = pTime->hour;
    pOut->wMinute = pTime->minute;
    pOut->wSecond = pTime->second;
    return pOut;
}

FILETIME Punishment_GetLocalFileTime()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    return ft;
}

static CERT_SHARD* FindCertShard(WORD wShardID)
{
    std::map<WORD, CERT_SHARD*>::iterator it = g_mapCertShard.find(wShardID);
    if (it == g_mapCertShard.end())
        return NULL;
    return it->second;
}

// ---------------------------------------------------------------------------------------------
// CPunishmentManager

CPunishmentManagerSingleton::CPunishmentManagerSingleton()
{
    if (g_pPunishmentManager)
        BSAssertFail();
    g_pPunishmentManager = (CPunishmentManager*)this;
}

CPunishmentManager::CPunishmentManager()
    : m_pTask(NULL)
{
}

CPunishmentManager::~CPunishmentManager()
{
    m_mapPunishment.clear();
    g_pPunishmentManager = NULL;
}

bool __stdcall PunishmentManager_Add(CPunishmentManager* pManager, DWORD dwJID, DWORD dwType,
                                     const TIMESTAMP_STRUCT* pBegin,
                                     const TIMESTAMP_STRUCT* pEnd)
{
    return pManager->Add(dwJID, dwType, pBegin, pEnd);
}

int CPunishmentManager::OnPunishmentRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (!pQuery->m_bRowFetched)
    {
        memset(pQuery->m_pBuffer, 0, sizeof(PUNISHMENT_ROW));
        return 1;
    }
    PUNISHMENT_ROW* pRow = (PUNISHMENT_ROW*)pQuery->m_pBuffer;
    if (!pRow)
        BSAssertFail();
    Punishment_Info_Internal* pInfo = PunishmentInfoPool_Alloc();
    if (!pInfo)
        BSAssertFail();
    pInfo->dwJID = pRow->dwJID;
    pInfo->byType = pRow->byType;
    pInfo->ftBegin = Punishment_TimestampToFileTime(&pRow->tsBegin);
    pInfo->ftEnd = Punishment_TimestampToFileTime(&pRow->tsEnd);
    pInfo->bActive = 0;
    m_mapPunishment.insert(PunishmentMap::value_type(pRow->dwJID, pInfo));
    return 1;
}

bool CPunishmentManager::Load(CTask* pTask)
{
    m_pTask = pTask;
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    CClassTrigger<CPunishmentManager> trigger(this);
    trigger.m_pfnTrigger2 = &CPunishmentManager::OnPunishmentRow;
    PUNISHMENT_ROW row;
    query.m_pSession = g_pAccountDBSession;
    query.m_pBuffer = (BYTE*)&row;
    query.m_nRowSize = sizeof(row);
    query.m_nBufferSize = sizeof(row);
    query.m_pTrigger = &trigger;
    if (query.ExecuteFormat("exec _QueryActivePunishmet") < 0)
    {
        LogPrintf(0x1000000, "Failed To Get Punishment Data");
        return false;
    }
    FILETIME ftNow = Punishment_GetLocalFileTime();
    for (PunishmentMap::iterator it = m_mapPunishment.begin(); it != m_mapPunishment.end(); ++it)
    {
        Punishment_Info_Internal* pInfo = it->second;
        if (CompareFileTime(&pInfo->ftBegin, &ftNow) <= 0)
            pInfo->bActive = 1;
    }
    LogPrintf(0, "Punishment Information [Count: %d] loaded.", m_mapPunishment.size());
    return true;
}

void CPunishmentManager::SendEnded(DWORD dwJID, BYTE byType)
{
    sContentUser* pUser = g_ContentUserManager.Find(dwJID);
    if (!pUser)
        return;
    CERT_SHARD* pShard = FindCertShard(pUser->wShardID);
    if (!pShard)
        BSAssertFail();
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(0x6317);
    BYTE byEnded = 1;
    pMsg->WriteBytes(&byEnded, 1);
    pMsg->WriteBytes(&dwJID, 4);
    pMsg->WriteBytes(&byType, 1);
    if (!m_pTask)
        BSAssertFail();
    RelayMsgToBody_6008((CServerProcessBase*)m_pTask->GetJob(), pMsg, pShard->wManagerID, 0, 0,
                        0);
}

void CPunishmentManager::SendStarted(DWORD dwJID, BYTE byType, const SYSTEMTIME* pEnd)
{
    sContentUser* pUser = g_ContentUserManager.Find(dwJID);
    if (!pUser)
        return;
    CERT_SHARD* pShard = FindCertShard(pUser->wShardID);
    if (!pShard)
        BSAssertFail();
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(0x6317);
    BYTE byEnded = 0;
    pMsg->WriteBytes(&byEnded, 1);
    pMsg->WriteBytes(&dwJID, 4);
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(pEnd, sizeof(SYSTEMTIME));
    if (!m_pTask)
        BSAssertFail();
    RelayMsgToBody_6008((CServerProcessBase*)m_pTask->GetJob(), pMsg, pShard->wManagerID, 0, 0,
                        0);
}

bool CPunishmentManager::RemoveByType(DWORD dwJID, DWORD dwType)
{
    std::pair<PunishmentMap::iterator, PunishmentMap::iterator> range =
        m_mapPunishment.equal_range(dwJID);
    for (PunishmentMap::iterator it = range.first; it != range.second; ++it)
    {
        Punishment_Info_Internal* pInfo = it->second;
        if (pInfo->byType == dwType)
        {
            m_mapPunishment.erase(it);
            pInfo->Release();
            return true;
        }
    }
    return false;
}

bool CPunishmentManager::Add(DWORD dwJID, DWORD dwType, const TIMESTAMP_STRUCT* pBegin,
                             const TIMESTAMP_STRUCT* pEnd)
{
    if (dwType == 1 || dwType == 2)
        return false;
    if (RemoveByType(dwJID, dwType))
        SendEnded(dwJID, (BYTE)dwType);
    Punishment_Info_Internal* pInfo = PunishmentInfoPool_Alloc();
    if (!pInfo)
        BSAssertFail();
    pInfo->dwJID = dwJID;
    pInfo->byType = (BYTE)dwType;
    pInfo->ftBegin = Punishment_TimestampToFileTime(pBegin);
    pInfo->ftEnd = Punishment_TimestampToFileTime(pEnd);
    FILETIME ftNow = Punishment_GetLocalFileTime();
    pInfo->bActive = CompareFileTime(&pInfo->ftBegin, &ftNow) <= 0;
    m_mapPunishment.insert(PunishmentMap::value_type(dwJID, pInfo));
    if (pInfo->bActive == 1)
    {
        SYSTEMTIME stEnd;
        Punishment_TimestampToSystemTime(&stEnd, pEnd);
        SendStarted(dwJID, (BYTE)dwType, &stEnd);
    }
    return true;
}

BYTE CPunishmentManager::GetActive(DWORD dwJID, BYTE* pOut)
{
    memset(pOut, 0, PUNISHMENT_ACTIVE_BUFFER);
    std::pair<PunishmentMap::iterator, PunishmentMap::iterator> range =
        m_mapPunishment.equal_range(dwJID);
    if (range.first == range.second)
        return 0;
    BYTE byCount = 0;
    for (PunishmentMap::iterator it = range.first; it != range.second; ++it)
    {
        Punishment_Info_Internal* pInfo = it->second;
        if (pInfo->bActive != 1)
            continue;
        SYSTEMTIME stEnd;
        FileTimeToSystemTime(&pInfo->ftEnd, &stEnd);
        int nType = pInfo->byType;
        if (nType > 4)
            BSAssertFail();
        // BUG(GM-014): types below 3 (loaded from the DB) write before the buffer.
        PUNISHMENT_ACTIVE_ENTRY* pEntry =
            (PUNISHMENT_ACTIVE_ENTRY*)(pOut + (nType - 3) * (int)sizeof(PUNISHMENT_ACTIVE_ENTRY));
        pEntry->stEnd = stEnd;
        pEntry->bActive = 1;
        ++byCount;
    }
    return byCount;
}

void CPunishmentManager::Tick()
{
    FILETIME ftNow = Punishment_GetLocalFileTime();
    for (PunishmentMap::iterator it = m_mapPunishment.begin(); it != m_mapPunishment.end(); ++it)
    {
        Punishment_Info_Internal* pInfo = it->second;
        if (pInfo->bActive == 1)
        {
            if (CompareFileTime(&pInfo->ftEnd, &ftNow) > 0)
                continue;
            SendEnded(pInfo->dwJID, pInfo->byType);
            // BUG(GM-015): the loop goes on from the erased node (its iterator is incremented
            // after the erase).
            PunishmentMap::iterator itErased = it;
            m_mapPunishment.erase(itErased);
            pInfo->Release();
        }
        else if (CompareFileTime(&pInfo->ftBegin, &ftNow) <= 0)
        {
            pInfo->bActive = 1;
            SYSTEMTIME stEnd;
            FileTimeToSystemTime(&pInfo->ftEnd, &stEnd);
            SendStarted(pInfo->dwJID, pInfo->byType, &stEnd);
        }
    }
}
