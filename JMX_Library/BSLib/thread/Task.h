#pragma once

#include <windows.h>
#include <list>
#include <map>

#include "BSLib/thread/SynchObject.h"
#include "BSLib/thread/ThreadManager.h"
#include "BSNet/QueMT.h"

class CMsg;

// A message queue plus a group of worker threads running one job (the network engine's
// proactors). Created by CNetEngine::RegisterProactor and owned by CTaskManager.
class CTask
{
public:
    CTask();
    virtual ~CTask();                                   // 0

    // Starts nCount worker threads unless the task already has some. Returns the result of
    // CThreadManager::CreateThreads, or TRUE when threads were running already.
    BOOL StartThreads(int nCount, int nPriority, unsigned nStackSize, DWORD dwCreateFlag,
                      THREAD_AFFINITY* pAffinity);
    // Stops the worker threads and forgets their IDs. Returns FALSE when there were none.
    BOOL StopThreads();

    // Defined with the proactor code (CProactor_SuspendLocked 0x495d30, CProactor_ResumeLocked
    // 0x495e10); the task setup itself is Proactor_Create (0x495a10).
    BOOL SuspendThreads();
    BOOL ResumeThreads();

public:
    IQue<CMsg>*         m_pQueue;           // +4
    std::list<DWORD>    m_listThreadIDs;    // +8
    CThreadManager*     m_pThreadManager;   // +14
    THREAD_DESC         m_desc;             // +18  pOwner = this, pJob = the proactor
    CCriticalSectionBS  m_cs;               // +2C
    int                 m_nThreads;         // +68  not initialised by the constructor
};

C_ASSERT(FIELD_OFFSET(CTask, m_listThreadIDs) == 8);
C_ASSERT(FIELD_OFFSET(CTask, m_pThreadManager) == 0x14);
C_ASSERT(FIELD_OFFSET(CTask, m_desc) == 0x18);
C_ASSERT(FIELD_OFFSET(CTask, m_cs) == 0x2C);
C_ASSERT(sizeof(CTask) == 0x6C);

// Proactor ID -> task.
class CTaskManager
{
public:
    typedef std::map<DWORD, CTask*> TaskMap;

    CTaskManager();
    virtual ~CTaskManager();                            // 0

    // Calls Stop on every task's queue (engine shutdown).
    void StopAllQueues();

protected:
    void DeleteAllTasks();

public:
    TaskMap             m_mapTasks;         // +4
    CCriticalSectionBS  m_cs;               // +10
};

C_ASSERT(FIELD_OFFSET(CTaskManager, m_cs) == 0x10);
C_ASSERT(sizeof(CTaskManager) == 0x4C);
