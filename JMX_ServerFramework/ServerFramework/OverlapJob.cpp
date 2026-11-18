#include "OverlapJob.h"

#include <list>

#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerApp.h"

namespace ServerFramework
{

// BUG(FWAPP-004): the ID pool and both job pools are not locked, but jobs are allocated and
// returned on the main process thread and on the overlap process thread.
COverlapJobIDPool g_OverlapJobIDPool;
#ifdef JMX_FRAMEWORK_2011
// AgentServer 0x42de40 / 0x44aed0: the pools are named.
CChunkAllocatorST<COverlapJob_InitializeLocalData> g_OverlapJobInitLocalDataPool(
    NULL, 0, 500, "COverlapJob_InitializeLocalData");
CChunkAllocatorST<COverlapJob_RelayMsg> g_OverlapJobRelayMsgPool(NULL, 0, 500,
                                                                 "COverlapJob_RelayMsg");
#else
CChunkAllocatorST<COverlapJob_InitializeLocalData> g_OverlapJobInitLocalDataPool;
CChunkAllocatorST<COverlapJob_RelayMsg> g_OverlapJobRelayMsgPool;
#endif

// ---------------------------------------------------------------------------------------------
// COverlapJobIDPool

COverlapJobIDPool::COverlapJobIDPool()
{
    m_dwNextID = 1;
    m_dwMaxID = (DWORD)-1;
    m_dwGrowBy = 500;
    PushNextIds(500);
    m_dwInUse = 0;
}

COverlapJobIDPool::COverlapJobIDPool(DWORD dwMaxID, DWORD dwFirstID)
{
    m_dwMaxID = dwMaxID;
    m_dwNextID = dwFirstID;
    m_dwGrowBy = 500;
    PushNextIds(500);
    m_dwInUse = 0;
}

void COverlapJobIDPool::PushNextIds(int nCount)
{
    if (m_dwNextID >= m_dwMaxID - nCount)
        BSAssertFail();
    for (; nCount > 0; --nCount)
    {
        m_deqFree.push_back(m_dwNextID);
        ++m_dwNextID;
    }
}

DWORD COverlapJobIDPool::Pop()
{
    if (m_deqFree.size() == 0)
        PushNextIds(m_dwGrowBy);
    DWORD dwID = m_deqFree.front();
    if (m_deqFree.size())
        m_deqFree.pop_front();
    m_dwStatNextID = m_dwNextID;
    ++m_dwInUse;
    m_dwStatFree = m_dwNextID - m_dwInUse;
    return dwID;
}

void COverlapJobIDPool::Push(DWORD dwID)
{
    if (!m_dwInUse)
        BSAssertFail();
    m_dwStatNextID = m_dwNextID;
    --m_dwInUse;
    m_dwStatFree = m_dwNextID - m_dwInUse;
    m_deqFree.push_back(dwID);
}

// ---------------------------------------------------------------------------------------------
// COverlapJob

void COverlapJob::Init(CTask* pOwner)
{
    if (!m_dwID)
        BSAssertFail();
    m_pOwner = pOwner;
    m_bTimedOut = 0;
    m_dwStartTick = GetTickCount();
    m_dwTimeout = (DWORD)-1;
}

void COverlapJob::ReleaseId()
{
    if (m_dwID)
    {
        g_OverlapJobIDPool.Push(m_dwID);
        m_dwID = 0;
    }
}

void COverlapJob::AcquireId()
{
    if (m_dwID)
        BSAssertFail();
    m_dwID = g_OverlapJobIDPool.Pop();
}

bool COverlapJob::CheckTimedOut(DWORD dwTick)
{
    if (m_dwStartTick > dwTick)
        BSAssertFail();
    m_bTimedOut = dwTick - m_dwStartTick > m_dwTimeout;
    return m_bTimedOut != 0;
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_InitializeLocalData

void COverlapJob_InitializeLocalData::ReturnToPool()
{
    if (m_dwID)
    {
        ReleaseId();
        g_OverlapJobInitLocalDataPool.Free(this);
    }
}

bool COverlapJob_InitializeLocalData::Prepare()
{
    return true;
}

bool COverlapJob_InitializeLocalData::Execute()
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(SERVER_CONTROL_INIT_LOCAL_DATA);
    g_pServerApp->OnControlMsg(pMsg);
    return true;
}

bool COverlapJob_InitializeLocalData::OnComplete()
{
    return true;
}

bool COverlapJob_InitializeLocalData::OnFail()
{
    return true;
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_RelayMsg

void COverlapJob_RelayMsg::ReturnToPool()
{
    if (m_dwID)
    {
        ReleaseId();
        g_OverlapJobRelayMsgPool.Free(this);
    }
}

void COverlapJob_RelayMsg::Setup(CTask* pOwner, WORD wTargetBody, WORD wOpcode,
                                 WORD wSourceBody, DWORD dwSourceJobID, DWORD dwContext)
{
    Init(pOwner);
    m_wTargetBody = wTargetBody;
    m_wOpcode = wOpcode;
    m_wSourceBody = wSourceBody;
    m_dwSourceJobID = dwSourceJobID;
    m_bCheckTimeout = 1;
    m_dwContext = dwContext;
    m_bRelay = 1;
}

COverlapJob_InitializeLocalData* OverlapJob_AllocInitializeLocalData()
{
    COverlapJob_InitializeLocalData* pJob = g_OverlapJobInitLocalDataPool.Alloc();
    pJob->AcquireId();
    return pJob;
}

COverlapJob_RelayMsg* OverlapJob_AllocRelayMsg()
{
    COverlapJob_RelayMsg* pJob = g_OverlapJobRelayMsgPool.Alloc();
    pJob->AcquireId();
    return pJob;
}

// ---------------------------------------------------------------------------------------------
// COverlapJobManager

void COverlapJobManager::Insert(COverlapJob* pJob)
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (m_mapJob.find(pJob->m_dwID) != m_mapJob.end())
        BSAssertFail();
    m_mapJob.insert(OverlapJobMap::value_type(pJob->m_dwID, pJob));
}

void COverlapJobManager::RemoveAndRelease(COverlapJob* pJob)
{
    CSingleLockBS lock(&m_cs, TRUE);
    OverlapJobMap::iterator it = m_mapJob.find(pJob->m_dwID);
    if (it == m_mapJob.end())
        BSAssertFail();
    m_mapJob.erase(it);
    pJob->ReturnToPool();
}

COverlapJob* COverlapJobManager::Remove(DWORD dwID)
{
    CSingleLockBS lock(&m_cs, TRUE);
    OverlapJobMap::iterator it = m_mapJob.find(dwID);
    if (it == m_mapJob.end())
        return NULL;
    COverlapJob* pJob = it->second;
    m_mapJob.erase(it);
    return pJob;
}

void COverlapJobManager::ReleaseAll()
{
    CSingleLockBS lock(&m_cs, TRUE);
    for (OverlapJobMap::iterator it = m_mapJob.begin(); it != m_mapJob.end(); ++it)
        it->second->ReturnToPool();
    m_mapJob.clear();
}

void COverlapJobManager::SweepTimedOutJobs()
{
    std::list<COverlapJob*> lstTimedOut;
    {
        CSingleLockBS lock(&m_cs, TRUE);
        for (OverlapJobMap::iterator it = m_mapJob.begin(); it != m_mapJob.end(); ++it)
        {
            COverlapJob* pJob = it->second;
            if (pJob->CheckTimedOut(GetTickCount()) && pJob->m_bCheckTimeout)
            {
                lstTimedOut.push_back(pJob);
                BSAssertFail();
            }
        }
    }

    for (std::list<COverlapJob*>::iterator it = lstTimedOut.begin(); it != lstTimedOut.end();
         ++it)
    {
        COverlapJob_RelayMsg* pJob = static_cast<COverlapJob_RelayMsg*>(*it);
        if (!pJob->m_bTimedOut)
            BSAssertFail();
        CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
        pMsg->SetOpcode(0xA008);
        DWORD dwID = pJob->m_dwID;
        WORD wOpcode = pJob->m_wOpcode;
        DWORD dwContext = pJob->m_dwContext;
        BYTE bResult = 2;
        pMsg->WriteBytes(&bResult, 1);
        pMsg->WriteBytes(&dwID, 4);
        pMsg->WriteBytes(&wOpcode, 2);
        pMsg->WriteBytes(&dwContext, 4);
        pJob->m_pOwner->m_pQueue->Push(pMsg);
    }
}

} // namespace ServerFramework

void IdPool_GetUsageCounters(DWORD* pdwFirst, DWORD* pdwSecond, DWORD* pdwThird)
{
    using ServerFramework::g_OverlapJobIDPool;
    *pdwFirst = g_OverlapJobIDPool.m_dwStatNextID;
    *pdwSecond = g_OverlapJobIDPool.m_dwInUse;
    *pdwThird = g_OverlapJobIDPool.m_dwStatFree;
}
