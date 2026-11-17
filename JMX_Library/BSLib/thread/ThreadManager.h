#pragma once

#include <windows.h>
#include <map>

#include "BSLib/thread/SynchObject.h"
#include "BSLib/thread/SimpleThread.h"
#include "BSLib/thread/ServiceObject.h"

class CTask;
class CThreadManager;

// Discarded trace output of the originals; the argument is still evaluated.
#define BS_THREAD_TRACE(x) ((void)(x))

// What a worker thread runs. CTask keeps one and every thread of the task gets a copy.
struct THREAD_DESC
{
    THREAD_DESC()
    {
        Clear();
    }

    void Clear()
    {
        pOwner = NULL;
        pJob = NULL;
        nPriority = 0;
        dwStackSize = 0;
        dw0C = (DWORD)-1;
    }

    CTask*          pOwner;         // +0  threads are grouped by owner
    CServiceObject* pJob;           // +4  Run is the thread body
    int             nPriority;      // +8
    DWORD           dw0C;           // +C
    unsigned        dwStackSize;    // +10
};

C_ASSERT(sizeof(THREAD_DESC) == 20);

// Ideal-processor selection for CThreadManager::CreateThreads.
struct THREAD_AFFINITY
{
    BOOL bRandom;       // +0  random CPU in [nMinCPU, nMaxCPU] per thread, else nMinCPU
    int  nMinCPU;       // +4  clamped to the processor count
    int  nMaxCPU;       // +8
};

class CThreadControl : public CSimpleThread
{
public:
    CThreadControl();
    virtual ~CThreadControl();                      // 0
    virtual void __stdcall Run(void* pArg);         // 1

    BOOL Create(const THREAD_DESC* pDesc, CThreadManager* pManager, DWORD dwKey);
    BOOL Stop();

public:
    THREAD_DESC         m_desc;         // +20
    DWORD               m_dwKey;        // +34
    volatile LONG       m_bStop;        // +38  passed to the job's Run
    DWORD               m_dwExitCode;   // +3C
    CThreadManager*     m_pManager;     // +40
    CCriticalSectionBS  m_cs;           // +44
};

C_ASSERT(FIELD_OFFSET(CThreadControl, m_desc) == 0x20);
C_ASSERT(FIELD_OFFSET(CThreadControl, m_dwKey) == 0x34);
C_ASSERT(FIELD_OFFSET(CThreadControl, m_pManager) == 0x40);
C_ASSERT(FIELD_OFFSET(CThreadControl, m_cs) == 0x44);
C_ASSERT(sizeof(CThreadControl) == 0x80);

typedef DWORD (WINAPI* PFN_SETTHREADIDEALPROCESSOR)(HANDLE hThread, DWORD dwIdealProcessor);

BOOL OSIsWindowsNT();

class CThreadManager
{
public:
    typedef std::map<DWORD, CThreadControl*> ThreadMap;
    typedef BOOL (CThreadManager::*PFN_THREADOP)(CThreadControl* pThread);

    CThreadManager();
    virtual ~CThreadManager();                      // 0

    // Creates nCount threads running pDesc->pJob, stores their thread IDs in pdwThreadIDs
    // (may be NULL) and leaves them suspended when dwCreateFlag is CREATE_SUSPENDED.
    BOOL CreateThreads(THREAD_AFFINITY* pAffinity, int nCount, DWORD* pdwThreadIDs,
                       const THREAD_DESC* pDesc, DWORD dwCreateFlag);

    // Apply to every thread whose descriptor owner is pOwner.
    BOOL SuspendThreads(CTask* pOwner);
    BOOL ResumeThreads(CTask* pOwner);
    BOOL StopThreads(CTask* pOwner);

protected:
    DWORD NextThreadKey();
    CThreadControl* SpawnThread(DWORD* pdwThreadID, const THREAD_DESC* pDesc,
                                DWORD dwCreateFlag);
    void DestroyAllThreads();
    BOOL ForEachThread(CTask* pOwner, PFN_THREADOP pfn);

    BOOL SuspendOne(CThreadControl* pThread);
    BOOL ResumeOne(CThreadControl* pThread);
    BOOL StopOne(CThreadControl* pThread);

public:
    DWORD                       m_dwLastKey;                // +4
    ThreadMap                   m_mapThreads;               // +8
    CCriticalSectionBS          m_cs;                       // +14
    PFN_SETTHREADIDEALPROCESSOR m_pfnSetThreadIdealProcessor;   // +50
};

C_ASSERT(FIELD_OFFSET(CThreadManager, m_mapThreads) == 8);
C_ASSERT(FIELD_OFFSET(CThreadManager, m_cs) == 0x14);
C_ASSERT(FIELD_OFFSET(CThreadManager, m_pfnSetThreadIdealProcessor) == 0x50);
C_ASSERT(sizeof(CThreadManager) == 0x54);
