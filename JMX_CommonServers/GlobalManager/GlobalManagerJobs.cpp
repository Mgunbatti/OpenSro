#include "GlobalManagerJobs.h"

#include <string.h>
#include <strsafe.h>

#include "ContentUser.h"
#include "GlobalManagerDB.h"
#include "GlobalManagerMainFwd.h"
#include "BSLib/BSDebug.h"
#include "BSLib/Util/TimeSpan.h"
#include "BSNet/IBSNet.h"
#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerProcess.h"
#include "ServerFramework/Trigger.h"

using namespace ServerFramework;

ServerFramework::CServerProcessMain* g_pGlobalManagerProcess;

CChunkAllocatorST<COverlapJob_CheckCharName> g_CheckCharNameJobPool(
    NULL, 0, 500, "COverlapJob_CheckCharName");
CChunkAllocatorST<sCU_Cash_Changed> g_CUCashChangedPool;
CChunkAllocatorST<COverlapJob_RefreshCommercialUser_Cash> g_RefreshCommercialUserCashJobPool(
    NULL, 0, 500, "COverlapJob_RefreshCommercialUser_Cash");
CChunkAllocatorST<COverlapJob_RefreshShardStatus> g_RefreshShardStatusJobPool(
    NULL, 0, 500, "COverlapJob_RefreshShardStatus");
CChunkAllocatorST<COverlapJob_ManageShardCharacterData> g_ManageShardCharacterDataJobPool(
    NULL, 0, 500, "COverlapJob_ManageShardCharacterData");
CChunkAllocatorST<COverlapJob_RefreshShardUser> g_RefreshShardUserJobPool(
    NULL, 0, 500, "COverlapJob_RefreshShardUser");
CChunkAllocatorST<COverlapJob_UpdatePlayTimeOfUser> g_UpdatePlayTimeOfUserJobPool(
    NULL, 0, 500, "COverlapJob_UpdatePlayTimeOfUser");
CChunkAllocatorST<COverlapJob_ResetPlayTimeOfGlobalServer> g_ResetPlayTimeJobPool(
    NULL, 0, 500, "COverlapJob_ResetPlayTimeOfGlobalServer");
CChunkAllocatorST<COverlapJob_CustomLoginJob> g_CustomLoginJobPool(
    NULL, 0, 500, "COverlapJob_CustomLoginJob");

static CServerProcessBase* OwnerProcess(CTask* pOwner)
{
    return (CServerProcessBase*)pOwner->GetJob();
}

static void WriteByte(CMsg* pMsg, BYTE b)
{
    pMsg->WriteBytes(&b, 1);
}

void GM_FormatTimestamp(std::string* pstrOut, const TIMESTAMP_STRUCT* pTime)
{
    char szText[256];
    if (FAILED(StringCchPrintfA(szText, 256, "%d-%d-%d %d:%d:%d", pTime->year, pTime->month,
                                pTime->day, pTime->hour, pTime->minute, pTime->second)))
    {
        BSAssertFail();
        return;
    }
    pstrOut->assign(szText, strlen(szText));
}

void GM_GetLocalTimestamp(TIMESTAMP_STRUCT* pTime)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    pTime->year = st.wYear;
    pTime->month = st.wMonth;
    pTime->day = st.wDay;
    pTime->hour = st.wHour;
    pTime->minute = st.wMinute;
    pTime->second = st.wSecond;
    pTime->fraction = 0;
}

// ---------------------------------------------------------------------------------------------
// Allocation

COverlapJob_CheckCharName* OverlapJob_AllocCheckCharName()
{
    COverlapJob_CheckCharName* pJob = g_CheckCharNameJobPool.Alloc();
    pJob->AcquireId();
    return pJob;
}

COverlapJob_RefreshCommercialUser_Cash* OverlapJob_AllocRefreshCommercialUserCash()
{
    COverlapJob_RefreshCommercialUser_Cash* pJob = g_RefreshCommercialUserCashJobPool.Alloc();
    pJob->AcquireId();
    return pJob;
}

COverlapJob_RefreshShardStatus* OverlapJob_AllocRefreshShardStatus()
{
    COverlapJob_RefreshShardStatus* pJob = g_RefreshShardStatusJobPool.Alloc();
    pJob->AcquireId();
    return pJob;
}

COverlapJob_ManageShardCharacterData* OverlapJob_AllocManageShardCharacterData()
{
    COverlapJob_ManageShardCharacterData* pJob = g_ManageShardCharacterDataJobPool.Alloc();
    pJob->AcquireId();
    return pJob;
}

COverlapJob_RefreshShardUser* OverlapJob_AllocRefreshShardUser()
{
    COverlapJob_RefreshShardUser* pJob = g_RefreshShardUserJobPool.Alloc();
    pJob->AcquireId();
    return pJob;
}

COverlapJob_UpdatePlayTimeOfUser* OverlapJob_AllocUpdatePlayTimeOfUser()
{
    COverlapJob_UpdatePlayTimeOfUser* pJob = g_UpdatePlayTimeOfUserJobPool.Alloc();
    pJob->AcquireId();
    return pJob;
}

COverlapJob_ResetPlayTimeOfGlobalServer* OverlapJob_AllocResetPlayTime()
{
    COverlapJob_ResetPlayTimeOfGlobalServer* pJob = g_ResetPlayTimeJobPool.Alloc();
    pJob->AcquireId();
    return pJob;
}

COverlapJob_CustomLoginJob* OverlapJob_AllocCustomLoginJob()
{
    COverlapJob_CustomLoginJob* pJob = g_CustomLoginJobPool.Alloc();
    pJob->AcquireId();
    return pJob;
}

sCU_Cash_Changed* CUCashChangedPool_Alloc()
{
    sCU_Cash_Changed* p = g_CUCashChangedPool.Alloc();
    p->bInUse = 1;
    return p;
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_CheckCharName

void COverlapJob_CheckCharName::ReturnToPool()
{
    if (m_dwID)
    {
        ReleaseId();
        g_CheckCharNameJobPool.Free(this);
    }
}

void COverlapJob_CheckCharName::Setup(CTask* pTask, CMsg* pMsg, DWORD dwSessionID,
                                      DWORD dwRelayID)
{
    Init(pTask);
    *pMsg >> m_by20;
    *pMsg >> m_by21;
    *pMsg >> m_by22;
    *pMsg >> m_strName;
    m_dwSessionID = dwSessionID;
    m_dwRelayID = dwRelayID;
}

bool COverlapJob_CheckCharName::Prepare()
{
    return g_pBSNet->GetSessionSocketState(m_dwSessionID) != -1;
}

bool COverlapJob_CheckCharName::Execute()
{
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pDefaultDBSession;
    query.m_pBuffer = &m_byResult;
    query.m_nRowSize = 1;
    query.m_nBufferSize = 1;
    query.m_nSession = 0;
    if (query.ExecuteFormat("exec _CheckCharName %d,'%s',%d,%d", m_by20, m_strName.c_str(),
                            m_by21, m_by22) < 0)
    {
        m_byResult = 2;
    }
    return true;
}

bool COverlapJob_CheckCharName::OnComplete()
{
    if (g_pBSNet->GetSessionSocketState(m_dwSessionID) == -1)
        return false;
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(0xA20B);
    if (m_byResult)
    {
        WriteByte(pMsg, 2);
        pMsg->WriteBytes(&m_byResult, 1);
    }
    else
    {
        WriteByte(pMsg, 1);
    }
    OwnerProcess(m_pOwner)->SendRelayReply(m_dwSessionID, m_dwRelayID, pMsg);
    return true;
}

bool COverlapJob_CheckCharName::OnFail()
{
    BSAssertFail();
    return true;
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_RefreshCommercialUser_Cash

void __stdcall sCU_Cash_Changed::Release()
{
    if (bInUse)
    {
        bInUse = 0;
        g_CUCashChangedPool.Free(this);
    }
}

void COverlapJob_RefreshCommercialUser_Cash::Init(CTask* pOwner)
{
    COverlapJob::Init(pOwner);
    if (m_listRow.size())
        BSAssertFail();
}

void COverlapJob_RefreshCommercialUser_Cash::ReturnToPool()
{
    if (m_dwID)
    {
        ReleaseId();
        g_RefreshCommercialUserCashJobPool.Free(this);
    }
}

bool COverlapJob_RefreshCommercialUser_Cash::Prepare()
{
    return true;
}

int COverlapJob_RefreshCommercialUser_Cash::OnCashRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (pQuery->m_dwParam == 1)
        return 1;
    if (!pQuery->m_bRowFetched)
    {
        sCU_Cash_Changed* pRow = CUCashChangedPool_Alloc();
        pQuery->m_pBuffer = (BYTE*)&pRow->Row;
        pQuery->m_pUserData = pRow;
        pQuery->m_nRowSize = sizeof(pRow->Row);
        pQuery->m_nBufferSize = sizeof(pRow->Row);
        return 1;
    }
    m_listRow.push_back((sCU_Cash_Changed*)pQuery->m_pUserData);
    return 1;
}

bool COverlapJob_RefreshCommercialUser_Cash::Execute()
{
    CClassTrigger<COverlapJob_RefreshCommercialUser_Cash> trigger(this);
    trigger.m_pfnTrigger2 = &COverlapJob_RefreshCommercialUser_Cash::OnCashRow;
    // The query is not cleared: the trigger sets the buffer of every row.
    CDBQuery query;
    query.m_pSession = g_pSilkDBSession;
    query.m_pTrigger = &trigger;
    query.m_nSession = 0;
    query.m_dwParam = 0;
    if (query.ExecuteFormat("select * from SK_SilkChange_BY_Web") < 0)
        return false;
    if (m_listRow.size())
    {
        BYTE abBuffer[0x104];
        query.m_pBuffer = abBuffer;
        query.m_nRowSize = 0x104;
        query.m_nBufferSize = 0x104;
        query.m_dwParam = 1;
        if (query.ExecuteFormat("delete SK_SilkChange_BY_Web where ID between %d and %d",
                                m_listRow.front()->Row.dwID,
                                m_listRow.back()->Row.dwID) < 0)
        {
            return false;
        }
    }
    return true;
}

bool COverlapJob_RefreshCommercialUser_Cash::OnComplete()
{
    for (CUCashChangedList::iterator it = m_listRow.begin(); it != m_listRow.end(); ++it)
    {
        sCU_Cash_Changed* pRow = *it;
        sContentUser* pUser = g_ContentUserManager.Find(pRow->Row.dwJID);
        if (!pUser)
            continue;
        std::map<WORD, CERT_SHARD*>::iterator itShard = g_mapCertShard.find(pUser->wShardID);
        CERT_SHARD* pShard = itShard == g_mapCertShard.end() ? NULL : itShard->second;
        if (!pShard)
        {
            LogPrintf(0x2000000, "cannot find shard : %d", pUser->wShardID);
            continue;
        }
        CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
        pMsg->SetOpcode(0x6A00);
        pMsg->WriteBytes(&pRow->Row.dwJID, 4);
        pMsg->WriteBytes(&pRow->Row.dw8, 4);
        pMsg->WriteBytes(&pRow->Row.dwC, 4);
        pMsg->WriteBytes(&pRow->Row.by10, 1);
        pMsg->WriteBytes(&pRow->Row.by11, 1);
        // BUG(GM-009): RelayMsgToBody_6008 releases the message in every case; it is released
        // a second time when the relay fails.
        if (!RelayMsgToBody_6008(OwnerProcess(m_pOwner), pMsg, pShard->wManagerID, 0, 0, 0))
            g_pBSNet->ReleaseMsg(pMsg);
    }
    ReleaseRows();
    return true;
}

bool COverlapJob_RefreshCommercialUser_Cash::OnFail()
{
    ReleaseRows();
    return true;
}

void COverlapJob_RefreshCommercialUser_Cash::ReleaseRows()
{
    for (CUCashChangedList::iterator it = m_listRow.begin(); it != m_listRow.end(); ++it)
        (*it)->Release();
    m_listRow.clear();
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_RefreshShardStatus

void COverlapJob_RefreshShardStatus::Init(CTask* pOwner)
{
    COverlapJob::Init(pOwner);
    m_listShard.clear();
    ServerBodyList* pBodies = g_pServerBodyList;
    CertShardList* pShards = g_pCertShardList;
    for (CertShardList::iterator it = pShards->begin(); it != pShards->end(); ++it)
    {
        CERT_SHARD* pShard = *it;
        int nBodies = 0;
        int nUp = 0;
        for (ServerBodyList::iterator itBody = pBodies->begin(); itBody != pBodies->end();
             ++itBody)
        {
            SERVER_BODY* pBody = *itBody;
            if (pBody->pShard != pShard)
                continue;
            if (pBody->nState == 5 || pBody->nState == 4)
                ++nUp;
            ++nBodies;
        }
        SHARD_STATUS_ITEM item;
        item.pShard = pShard;
        if (!nUp)
            item.byStatus = 0;
        else if (nUp == nBodies)
            item.byStatus = 2;
        else if (nUp < nBodies)
            item.byStatus = 1;
        // BUG(GM-010): a shard with the user limit 0 divides by zero.
        item.byRatio = (BYTE)(100 * pShard->w557 / pShard->w548);
        m_listShard.push_back(item);
    }
}

void COverlapJob_RefreshShardStatus::ReturnToPool()
{
    if (m_dwID)
    {
        ReleaseId();
        g_RefreshShardStatusJobPool.Free(this);
    }
}

bool COverlapJob_RefreshShardStatus::Prepare()
{
    return true;
}

bool COverlapJob_RefreshShardStatus::Execute()
{
    // The query is not cleared (updates only).
    CDBQuery query;
    query.m_pSession = g_pAccountDBSession;
    query.m_nSession = 0;
    query.m_dwParam = 0;
    for (std::list<SHARD_STATUS_ITEM>::iterator it = m_listShard.begin();
         it != m_listShard.end(); ++it)
    {
        if (query.ExecuteFormat("update _Shard set nStatus = %d, nCurrentUserRatio = %d "
                                "where nID = %d", it->byStatus, it->byRatio,
                                it->pShard->wID) < 0)
        {
            return false;
        }
    }
    m_listShard.clear();
    return true;
}

bool COverlapJob_RefreshShardStatus::OnComplete()
{
    return true;
}

bool COverlapJob_RefreshShardStatus::OnFail()
{
    return true;
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_ManageShardCharacterData

void COverlapJob_ManageShardCharacterData::ReturnToPool()
{
    if (m_dwID)
    {
        ReleaseId();
        g_ManageShardCharacterDataJobPool.Free(this);
    }
}

bool COverlapJob_ManageShardCharacterData::Setup(CTask* pTask, CMsg* pMsg, DWORD dwRelayID,
                                                 BYTE byType, WORD wShardID)
{
    Init(pTask);
    m_strName.clear();
    m_strName2.clear();
    m_dwRelayID = dwRelayID;
    m_dwSessionID = pMsg->m_dwSessionID;
    m_byType = byType;
    m_wShardID = wShardID;
    *pMsg >> m_dw2C;
    *pMsg >> m_strName;
    if (!m_strName.size())
        return false;
    switch (m_byType)
    {
    case 0:
    case 1:
        return true;
    case 2:
        *pMsg >> m_strName2;
        return m_strName2.size() != 0;
    }
    return false;
}

bool COverlapJob_ManageShardCharacterData::Prepare()
{
    return true;
}

bool COverlapJob_ManageShardCharacterData::Execute()
{
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pAccountDBSession;
    query.m_nSession = 0;
    return query.ExecuteFormat("exec _ManageShardCharName %d, %d, %d, '%s', '%s'", m_byType,
                               m_dw2C, m_wShardID, m_strName.c_str(),
                               m_strName2.c_str()) >= 0;
}

void COverlapJob_ManageShardCharacterData::Answer(BYTE byResult)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    switch (m_byType)
    {
    case 0:
        pMsg->SetOpcode(0xA20E);
        break;
    case 1:
        pMsg->SetOpcode(0xA20F);
        break;
    case 2:
        pMsg->SetOpcode(0xA210);
        break;
    default:
        g_pBSNet->ReleaseMsg(pMsg);
        return;
    }
    WriteByte(pMsg, byResult);
    OwnerProcess(m_pOwner)->SendRelayReply(m_dwSessionID, m_dwRelayID, pMsg);
}

bool COverlapJob_ManageShardCharacterData::OnComplete()
{
    Answer(1);
    return true;
}

bool COverlapJob_ManageShardCharacterData::OnFail()
{
    Answer(2);
    return true;
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_RefreshShardUser

void COverlapJob_RefreshShardUser::Init(CTask* pOwner)
{
    COverlapJob::Init(pOwner);
    m_listShard.clear();
    CertShardList* pShards = g_pCertShardList;
    for (CertShardList::iterator it = pShards->begin(); it != pShards->end(); ++it)
    {
        SHARD_USER_ITEM item;
        item.wShardID = (*it)->wID;
        item.wUserCount = (*it)->w557;
        m_listShard.push_back(item);
    }
}

void COverlapJob_RefreshShardUser::ReturnToPool()
{
    if (m_dwID)
    {
        ReleaseId();
        g_RefreshShardUserJobPool.Free(this);
    }
}

bool COverlapJob_RefreshShardUser::Prepare()
{
    return true;
}

bool COverlapJob_RefreshShardUser::Execute()
{
    // The query is not cleared.
    CDBQuery query;
    query.m_pSession = g_pDefaultDBSession;
    query.m_nSession = 0;
    query.m_dwParam = 0;
    SYSTEMTIME st;
    GetLocalTime(&st);
    TIMESTAMP_STRUCT ts;
    ts.year = st.wYear;
    ts.month = st.wMonth;
    ts.day = st.wDay;
    ts.hour = st.wHour;
    ts.minute = st.wMinute;
    ts.second = st.wSecond;
    ts.fraction = 0;
    std::string strTime;
    GM_FormatTimestamp(&strTime, &ts);
    for (std::list<SHARD_USER_ITEM>::iterator it = m_listShard.begin(); it != m_listShard.end();
         ++it)
    {
        if (query.ExecuteFormat("exec _AddCurrentUserCount %d, %d, '%s'", it->wShardID,
                                it->wUserCount, strTime.c_str()) < 0)
        {
            return false;
        }
    }
    m_listShard.clear();
    return true;
}

bool COverlapJob_RefreshShardUser::OnComplete()
{
    return true;
}

bool COverlapJob_RefreshShardUser::OnFail()
{
    return true;
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_UpdatePlayTimeOfUser

void COverlapJob_UpdatePlayTimeOfUser::ReturnToPool()
{
    if (m_dwID)
    {
        ReleaseId();
        g_UpdatePlayTimeOfUserJobPool.Free(this);
    }
}

void COverlapJob_UpdatePlayTimeOfUser::Setup(DWORD dwJID, WORD wPlayTime)
{
    Init((CTask*)g_pGlobalManagerProcess->GetOwner());
    m_dwJID = dwJID;
    m_wPlayTime = wPlayTime;
}

bool COverlapJob_UpdatePlayTimeOfUser::Prepare()
{
    return true;
}

bool COverlapJob_UpdatePlayTimeOfUser::Execute()
{
    // The original fills only the session and the connection index of the query.
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pAccountDBSession;
    query.m_nSession = 3;
    DWORD dwJID = m_dwJID;
    return query.ExecuteFormat("update TB_USER set AccPlayTime = %d, "
                               "LatestUpdateTime_ToPlayTime = %d where JID = %d",
                               m_wPlayTime, CDateTime::GetCurrent().GetTime(), dwJID) >= 0;
}

bool COverlapJob_UpdatePlayTimeOfUser::OnComplete()
{
    return true;
}

bool COverlapJob_UpdatePlayTimeOfUser::OnFail()
{
    return true;
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_ResetPlayTimeOfGlobalServer

void COverlapJob_ResetPlayTimeOfGlobalServer::ReturnToPool()
{
    if (m_dwID)
    {
        ReleaseId();
        g_ResetPlayTimeJobPool.Free(this);
    }
}

bool COverlapJob_ResetPlayTimeOfGlobalServer::Setup(int nType, DWORD dwTime)
{
    Init((CTask*)g_pGlobalManagerProcess->GetOwner());
    m_strQuery.clear();
    memset(&m_Query, 0, sizeof(m_Query));
    m_Query.m_pSession = g_pAccountDBSession;
    m_nType = nType;
    m_Query.m_nSession = 3;
    m_dwTime = 0;
    switch (nType)
    {
    case 0:
        m_Query.m_nRowSize = 4;
        m_Query.m_nBufferSize = 4;
        m_Query.m_pBuffer = (BYTE*)&m_dwTime;
        m_strQuery.assign("select * from _WriteOutResetPlayTime", 0x24);
        return true;
    case 1:
    {
        char szQuery[256];
        // The result is not checked (the text always fits).
        StringCchPrintfA(szQuery, 0xFF, "update _WriteOutResetPlayTime set LatestResetTime = %d",
                         dwTime);
        m_strQuery.assign(szQuery, strlen(szQuery));
        return true;
    }
    }
    BSAssertFail();
    return false;
}

bool COverlapJob_ResetPlayTimeOfGlobalServer::Prepare()
{
    return true;
}

bool COverlapJob_ResetPlayTimeOfGlobalServer::Execute()
{
    // The prepared text is the format (it has no '%').
    return m_Query.ExecuteFormat(m_strQuery.c_str()) >= 0;
}

bool COverlapJob_ResetPlayTimeOfGlobalServer::OnComplete()
{
    if (!m_nType)
        GlobalManagerMain_SetLastPlayTimeReset(m_dwTime);
    return true;
}

bool COverlapJob_ResetPlayTimeOfGlobalServer::OnFail()
{
    return true;
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_CustomLoginJob

void COverlapJob_CustomLoginJob::ReturnToPool()
{
    if (m_dwID)
    {
        ReleaseId();
        g_CustomLoginJobPool.Free(this);
    }
}

bool COverlapJob_CustomLoginJob::Prepare()
{
    return true;
}

bool COverlapJob_CustomLoginJob::Execute()
{
    if (!m_map.size() || m_dw20 == 0xFFFFFFFF)
        return false;
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pAccountDBSession;
    for (CustomLoginMap::iterator it = m_map.begin(); it != m_map.end(); ++it)
    {
        char szQuery[0x2800];
        if (FAILED(StringCchPrintfA(szQuery, 0x2800, "exec _AddLoginHistory %d,%d,%d",
                                    it->first, it->second, m_dw20)))
        {
            BSAssertFail();
            return false;
        }
        // The formatted text is the format (numbers only); the result is not checked.
        query.ExecuteFormat(szQuery);
    }
    m_map.clear();
    return true;
}

bool COverlapJob_CustomLoginJob::OnComplete()
{
    return true;
}

bool COverlapJob_CustomLoginJob::OnFail()
{
    return true;
}
