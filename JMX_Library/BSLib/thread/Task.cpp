#include "Task.h"

#include <string.h>

// ---------------------------------------------------------------------------------------------
// CTask

CTask::CTask()
{
    m_pThreadManager = NULL;
    m_desc.pJob = NULL;
    m_desc.pOwner = NULL;
}

CTask::~CTask()
{
    // BUG(THREAD-005): the thread manager is cleared before StopThreads, so StopThreads does
    // nothing and the job below is deleted while its worker threads may still run.
    m_pThreadManager = NULL;
    // BUG(THREAD-006): no NULL check; a task whose queue was never created crashes here.
    m_pQueue->Stop();
    if (m_desc.pJob)
        m_desc.pJob->SignalStop();
    StopThreads();
    if (m_desc.pJob)
    {
        delete m_desc.pJob;
        m_desc.pJob = NULL;
    }
    if (m_pQueue)
    {
        m_pQueue->Close();
        if (m_pQueue)
            delete m_pQueue;
        m_pQueue = NULL;
    }
    m_listThreadIDs.clear();
    m_desc.Clear();
}

BOOL CTask::StartThreads(int nCount, int nPriority, unsigned nStackSize, DWORD dwCreateFlag,
                         THREAD_AFFINITY* pAffinity)
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (m_listThreadIDs.size() != 0)
        return TRUE;

    m_desc.nPriority = nPriority;
    m_desc.dwStackSize = nStackSize;
    DWORD* pdwThreadIDs = new DWORD[nCount];
    memset(pdwThreadIDs, 0, nCount * sizeof(DWORD));
    m_nThreads = nCount;

    BOOL bResult = m_pThreadManager->CreateThreads(pAffinity, nCount, pdwThreadIDs, &m_desc,
                                                   dwCreateFlag);
    if (bResult == TRUE)
    {
        for (int i = 0; i < nCount; ++i)
            m_listThreadIDs.push_back(pdwThreadIDs[i]);
    }
    else
    {
        m_pThreadManager->StopThreads(this);
    }
    delete[] pdwThreadIDs;
    return bResult;
}

BOOL CTask::StopThreads()
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (m_pThreadManager && m_listThreadIDs.size() > 0)
    {
        BOOL bResult = m_pThreadManager->StopThreads(this);
        m_listThreadIDs.clear();
        return bResult;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------------------------
// CTaskManager

CTaskManager::CTaskManager()
{
}

CTaskManager::~CTaskManager()
{
    DeleteAllTasks();
}

void CTaskManager::DeleteAllTasks()
{
    for (TaskMap::iterator it = m_mapTasks.begin(); it != m_mapTasks.end(); )
    {
        CTask* pTask = it->second;
        m_mapTasks.erase(it++);
        if (pTask)
            delete pTask;
    }
    m_mapTasks.clear();
}

void CTaskManager::StopAllQueues()
{
    for (TaskMap::iterator it = m_mapTasks.begin(); it != m_mapTasks.end(); ++it)
        it->second->m_pQueue->Stop();     // BUG(THREAD-006): no NULL checks
}

// ---------------------------------------------------------------------------------------------
// CTask: suspend / resume (CNetEngine::ProactorSuspend / ProactorResume)

BOOL CTask::SuspendThreads()
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (m_pThreadManager && m_listThreadIDs.size() > 0)
        return m_pThreadManager->SuspendThreads(this);
    return FALSE;
}

BOOL CTask::ResumeThreads()
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (m_pThreadManager && m_listThreadIDs.size() > 0)
        return m_pThreadManager->ResumeThreads(this);
    return FALSE;
}

// ---------------------------------------------------------------------------------------------
// CTaskManager: proactor map

CTask* CTaskManager::FindTask(DWORD dwID)
{
    CSingleLockBS lock(&m_cs, TRUE);
    TaskMap::iterator it = m_mapTasks.find(dwID);
    if (it == m_mapTasks.end())
        return NULL;
    return it->second;
}

BOOL CTaskManager::AddTask(DWORD dwID, CTask* pTask)
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (m_mapTasks.find(dwID) != m_mapTasks.end())
        return FALSE;
    m_mapTasks[dwID] = pTask;
    return TRUE;
}

BOOL CTaskManager::RemoveAndDeleteTask(DWORD dwID)
{
    CSingleLockBS lock(&m_cs, TRUE);
    TaskMap::iterator it = m_mapTasks.find(dwID);
    if (it == m_mapTasks.end())
        return FALSE;
    // The task (and its job) is deleted while the map lock is held.
    if (it->second)
        delete it->second;
    m_mapTasks.erase(it);
    return TRUE;
}
