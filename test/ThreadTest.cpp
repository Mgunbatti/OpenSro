#include "BSLib/thread/ThreadManager.h"
#include "BSLib/thread/Task.h"
#include "BSLib/thread/System.h"
#include "BSLib/thread/EventHandler.h"
#include "BSNet/QueMT.h"

#include "TestCheck.h"

// ---------------------------------------------------------------------------------------------
// Test doubles

class CTestJob : public CServiceObject
{
public:
    CTestJob(DWORD dwExitCode = 0, HANDLE hHold = NULL)
    {
        m_nRuns = 0;
        m_nStops = 0;
        m_dwExit = dwExitCode;
        m_hHold = hHold;
        m_bIgnoreStop = FALSE;
        for (int i = 0; i < 16; ++i)
            m_nIndexSeen[i] = 0;
    }

    virtual DWORD Run(volatile LONG* pbStop, int nIndex)
    {
        InterlockedIncrement(&m_nRuns);
        InterlockedIncrement(&m_nActive);
        if (nIndex >= 0 && nIndex < 16)
            InterlockedIncrement(&m_nIndexSeen[nIndex]);
        if (m_hHold)
            WaitForSingleObject(m_hHold, INFINITE);
        while (!*pbStop && !m_bIgnoreStop)
            Sleep(1);
        InterlockedDecrement(&m_nActive);
        return m_dwExit;
    }

    virtual int SignalStop()
    {
        InterlockedIncrement(&m_nStops);
        return 1;
    }

    volatile LONG m_nRuns;
    volatile LONG m_nStops;
    volatile LONG m_nIndexSeen[16];
    DWORD         m_dwExit;
    HANDLE        m_hHold;
    BOOL          m_bIgnoreStop;
};

static LONG g_nQueStops = 0;
static LONG g_nQueCloses = 0;
static LONG g_nQueDeletes = 0;

class CTestQue : public IQue<CMsg>
{
public:
    virtual ~CTestQue() { ++g_nQueDeletes; }
    virtual BOOL Close() { ++g_nQueCloses; return TRUE; }
    virtual LONG GetSize() { return 0; }
    virtual BOOL IsEmpty() { return TRUE; }
    virtual BOOL IsFull() { return FALSE; }
    virtual BOOL Push(CMsg*) { return FALSE; }
    virtual BOOL Pop(CMsg**, DWORD, BOOL) { return FALSE; }
    virtual BOOL Stop() { ++g_nQueStops; return TRUE; }
    virtual LONG Start() { return 0; }
    virtual void Reserved() {}
};

static int    g_nIdealCalls = 0;
static DWORD  g_dwIdeal[16];
static HANDLE g_hIdealThread[16];

static DWORD WINAPI FakeSetThreadIdealProcessor(HANDLE hThread, DWORD dwIdeal)
{
    if (g_nIdealCalls < 16)
    {
        g_hIdealThread[g_nIdealCalls] = hThread;
        g_dwIdeal[g_nIdealCalls] = dwIdeal;
    }
    ++g_nIdealCalls;
    return 0;
}

static BOOL WaitFor(volatile LONG* p, LONG nValue)
{
    for (int i = 0; i < 2000; ++i)
    {
        if (*p == nValue)
            return TRUE;
        Sleep(1);
    }
    return FALSE;
}

static int CountOwned(CThreadManager& mgr, CTask* pOwner)
{
    int n = 0;
    for (CThreadManager::ThreadMap::iterator it = mgr.m_mapThreads.begin();
         it != mgr.m_mapThreads.end(); ++it)
    {
        if (it->second->m_desc.pOwner == pOwner)
            ++n;
    }
    return n;
}

class CTestHandler : public CEventHandler
{
public:
    CTestHandler(int* pn) : m_pn(pn) {}
    virtual ~CTestHandler() { ++*m_pn; }
    virtual BOOL AttachToProactor(CServiceObjectSock*, HANDLE) { return TRUE; }
    virtual int OnIOCompletion(DWORD, LPOVERLAPPED) { return 0; }
    virtual void OnIdle() {}
    int* m_pn;
};

// ---------------------------------------------------------------------------------------------

static void TestThunk()
{
    CThreadControl tc;
    BYTE* p = &tc.m_mov;
    CHECK(p == (BYTE*)&tc + 4);
    CHECK(p[0] == 0xB8);
    CHECK(*(DWORD*)(p + 1) == (DWORD)(DWORD_PTR)&tc);
    CHECK(*(DWORD*)(p + 5) == 0x50240487);
    CHECK(p[9] == 0xE9);
    CHECK(tc.m_hThread == NULL);
    CHECK(tc.m_bSuspended == TRUE);
    CHECK(tc.m_dwThreadID == 0);
    CHECK(tc.m_desc.dw0C == (DWORD)-1);
    CHECK(tc.m_desc.pJob == NULL);
    CHECK(tc.Stop() == FALSE);              // no thread: nothing to do
    CHECK(tc.Close() == FALSE);
    CHECK(tc.Suspend() == TRUE);
    CHECK(tc.Resume() == TRUE);
}

static void TestKeys()
{
    CThreadManager mgr;
    CHECK(mgr.m_dwLastKey == 0);
    struct CKeyProbe : public CThreadManager
    {
        DWORD Next() { return NextThreadKey(); }
    };
    CKeyProbe probe;
    CHECK(probe.Next() == 1);
    CHECK(probe.Next() == 2);
    probe.m_dwLastKey = 0xFFFFFFFD;
    CHECK(probe.Next() == 0xFFFFFFFE);
    CHECK(probe.Next() == 1);               // -1 is skipped
}

static void TestManagerLifecycle()
{
    CTestJob job(1234);
    CTestJob other;
    CThreadManager mgr;
    CTask* pOwner = (CTask*)(DWORD_PTR)0x1000;
    CTask* pOther = (CTask*)(DWORD_PTR)0x2000;

    THREAD_DESC desc;
    desc.pOwner = pOwner;
    desc.pJob = &job;
    desc.nPriority = THREAD_PRIORITY_NORMAL;
    desc.dwStackSize = 0;

    DWORD dwIDs[3] = { 0, 0, 0 };
    CHECK(mgr.CreateThreads(NULL, 3, dwIDs, &desc, CREATE_SUSPENDED) == TRUE);
    CHECK(mgr.m_mapThreads.size() == 3);
    CHECK(dwIDs[0] && dwIDs[1] && dwIDs[2]);
    Sleep(50);
    CHECK(job.m_nRuns == 0);                // created suspended

    DWORD dwKey = 1;
    for (CThreadManager::ThreadMap::iterator it = mgr.m_mapThreads.begin();
         it != mgr.m_mapThreads.end(); ++it, ++dwKey)
    {
        CThreadControl* p = it->second;
        CHECK(it->first == dwKey);
        CHECK(p->m_dwKey == dwKey);
        CHECK(p->m_pManager == &mgr);
        CHECK(p->m_hThread != NULL);
        CHECK(p->m_bSuspended == TRUE);
        CHECK(p->m_dwThreadID == dwIDs[dwKey - 1]);
        CHECK(p->m_desc.pJob == &job);
        CHECK(p->m_desc.pOwner == pOwner);
    }

    // A second group that must not be touched by operations on pOwner.
    THREAD_DESC desc2 = desc;
    desc2.pOwner = pOther;
    desc2.pJob = &other;
    CHECK(mgr.CreateThreads(NULL, 1, NULL, &desc2, 0) == TRUE);    // started running
    CHECK(WaitFor(&other.m_nRuns, 1));

    CHECK(mgr.ResumeThreads(pOwner) == TRUE);
    CHECK(WaitFor(&job.m_nRuns, 3));
    CHECK(WaitFor(&job.m_nActive, 3));
    CHECK(job.m_nIndexSeen[0] == 1 && job.m_nIndexSeen[1] == 1 && job.m_nIndexSeen[2] == 1);

    CHECK(mgr.SuspendThreads(pOwner) == TRUE);
    for (CThreadManager::ThreadMap::iterator it = mgr.m_mapThreads.begin();
         it != mgr.m_mapThreads.end(); ++it)
    {
        CThreadControl* p = it->second;
        CHECK(p->m_bSuspended == (p->m_desc.pOwner == pOwner));
    }
    CHECK(mgr.ResumeThreads(pOwner) == TRUE);

    CHECK(mgr.StopThreads(pOwner) == TRUE);
    CHECK(job.m_nStops == 3);
    CHECK(job.m_nActive == 0);
    CHECK(other.m_nStops == 0);
    CHECK(other.m_nActive == 1);
    // THREAD-004: stopped threads stay registered.
    CHECK(mgr.m_mapThreads.size() == 4);
    for (CThreadManager::ThreadMap::iterator it = mgr.m_mapThreads.begin();
         it != mgr.m_mapThreads.end(); ++it)
    {
        CThreadControl* p = it->second;
        if (p->m_desc.pOwner == pOwner)
        {
            CHECK(p->m_hThread == NULL);
            CHECK(p->m_bStop == TRUE);
            CHECK(p->m_desc.pJob == NULL);
            CHECK(p->m_dwExitCode == 1234);
        }
    }
    // The other group is stopped by the manager's destructor.
}

static void TestExitCode()
{
    CTestJob job(77);
    CThreadManager mgr;
    job.m_bIgnoreStop = TRUE;           // returns at once
    THREAD_DESC desc;
    desc.pJob = &job;
    CHECK(mgr.CreateThreads(NULL, 1, NULL, &desc, 0) == TRUE);
    CThreadControl* p = mgr.m_mapThreads.begin()->second;
    CHECK(WaitForSingleObject(p->m_hThread, 5000) == WAIT_OBJECT_0);
    DWORD dwCode = 0;
    CHECK(GetExitCodeThread(p->m_hThread, &dwCode));
    CHECK(dwCode == 77);                // _endthreadex(job result)
    CHECK(p->m_dwExitCode == 77);
}

static void TestAffinity()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int nMax = (int)si.dwNumberOfProcessors - 1;

    CTestJob job;
    CThreadManager mgr;
    mgr.m_pfnSetThreadIdealProcessor = FakeSetThreadIdealProcessor;
    THREAD_DESC desc;
    desc.pJob = &job;

    THREAD_AFFINITY aff = { FALSE, -5, 1000 };
    g_nIdealCalls = 0;
    CHECK(mgr.CreateThreads(&aff, 3, NULL, &desc, CREATE_SUSPENDED) == TRUE);
    CHECK(aff.nMinCPU == 0);
    CHECK(aff.nMaxCPU == nMax);
    CHECK(g_nIdealCalls == 3);
    CHECK(g_dwIdeal[0] == 0 && g_dwIdeal[1] == 0 && g_dwIdeal[2] == 0);
    CHECK(g_hIdealThread[0] == mgr.m_mapThreads.begin()->second->m_hThread);

    THREAD_AFFINITY aff2 = { TRUE, 0, 1000 };
    g_nIdealCalls = 0;
    CHECK(mgr.CreateThreads(&aff2, 8, NULL, &desc, CREATE_SUSPENDED) == TRUE);
    CHECK(g_nIdealCalls == 8);
    for (int i = 0; i < 8; ++i)
        CHECK((int)g_dwIdeal[i] >= 0 && (int)g_dwIdeal[i] <= nMax);

    // No affinity: no ideal-processor calls.
    g_nIdealCalls = 0;
    CHECK(mgr.CreateThreads(NULL, 1, NULL, &desc, CREATE_SUSPENDED) == TRUE);
    CHECK(g_nIdealCalls == 0);
    CHECK(mgr.m_mapThreads.size() == 12);
    // Destructor resumes, stops and joins all of them.
}

static void TestTask()
{
    CThreadManager mgr;
    CTestJob* pJob = new CTestJob;

    CTask* pTask = new CTask;
    CHECK(pTask->m_pThreadManager == NULL);
    CHECK(pTask->m_desc.dw0C == (DWORD)-1);
    CHECK(pTask->StopThreads() == FALSE);

    pTask->m_pQueue = new CTestQue;
    pTask->m_pThreadManager = &mgr;
    pTask->m_desc.pOwner = pTask;
    pTask->m_desc.pJob = pJob;

    CHECK(pTask->StartThreads(2, THREAD_PRIORITY_NORMAL, 0, 0, NULL) == TRUE);
    CHECK(pTask->m_nThreads == 2);
    CHECK(pTask->m_listThreadIDs.size() == 2);
    CHECK(WaitFor(&pJob->m_nRuns, 2));
    // Already running: nothing new.
    CHECK(pTask->StartThreads(5, THREAD_PRIORITY_NORMAL, 0, 0, NULL) == TRUE);
    CHECK(pTask->m_listThreadIDs.size() == 2);
    CHECK(mgr.m_mapThreads.size() == 2);

    std::list<DWORD>::iterator itID = pTask->m_listThreadIDs.begin();
    CThreadManager::ThreadMap::iterator itT = mgr.m_mapThreads.begin();
    CHECK(*itID == itT->second->m_dwThreadID);

    CHECK(pTask->StopThreads() == TRUE);
    CHECK(pTask->m_listThreadIDs.empty());
    CHECK(pJob->m_nStops == 2);
    CHECK(pJob->m_nActive == 0);
    CHECK(pTask->StopThreads() == FALSE);

    // Restart, then destroy the task: THREAD-005, the destructor does not stop the threads.
    // The job is detached first so the running threads keep a valid object.
    CHECK(pTask->StartThreads(1, THREAD_PRIORITY_NORMAL, 0, 0, NULL) == TRUE);
    CHECK(WaitFor(&pJob->m_nRuns, 3));
    pTask->m_desc.pJob = NULL;
    g_nQueStops = g_nQueCloses = g_nQueDeletes = 0;
    delete pTask;
    CHECK(g_nQueStops == 1);
    CHECK(g_nQueCloses == 1);
    CHECK(g_nQueDeletes == 1);
    CHECK(CountOwned(mgr, pTask) == 3);
    int nRunning = 0;
    for (CThreadManager::ThreadMap::iterator it = mgr.m_mapThreads.begin();
         it != mgr.m_mapThreads.end(); ++it)
    {
        if (it->second->m_hThread)
            ++nRunning;
    }
    CHECK(nRunning == 1);
    CHECK(pJob->m_nActive == 1);
    CHECK(mgr.StopThreads(pTask) == TRUE);      // pointer only compared
    CHECK(pJob->m_nActive == 0);
    delete pJob;

    // A job deleted by the task destructor (no threads started).
    CTask* pTask2 = new CTask;
    pTask2->m_pQueue = new CTestQue;
    pTask2->m_desc.pJob = new CTestJob;
    g_nQueStops = g_nQueCloses = g_nQueDeletes = 0;
    delete pTask2;
    CHECK(g_nQueStops == 1 && g_nQueCloses == 1 && g_nQueDeletes == 1);
}

static void TestTaskManager()
{
    CTaskManager* pMgr = new CTaskManager;
    for (DWORD i = 1; i <= 3; ++i)
    {
        CTask* p = new CTask;
        p->m_pQueue = new CTestQue;
        pMgr->m_mapTasks[i] = p;
    }
    g_nQueStops = g_nQueCloses = g_nQueDeletes = 0;
    pMgr->StopAllQueues();
    CHECK(g_nQueStops == 3);
    CHECK(g_nQueDeletes == 0);
    delete pMgr;
    CHECK(g_nQueStops == 6);        // once more from each task destructor
    CHECK(g_nQueDeletes == 3);
}

static void TestSystem()
{
    CHECK(CSystem::GetInstance() == &g_System);
    CHECK(g_System.m_pSockSystem != NULL);
    CHECK(g_System.m_pTaskManager != NULL);
    CHECK(g_System.m_pThreadManager != NULL);
    CHECK(g_System.m_pThreadManager->m_dwLastKey == 0);

    CSystem* pSys = new CSystem;
    CHECK(CSystem::GetInstance() == pSys);
    pSys->ReleaseSubsystems();
    CHECK(pSys->m_pSockSystem == NULL);
    CHECK(pSys->m_pTaskManager == NULL);
    CHECK(pSys->m_pThreadManager == NULL);
    delete pSys;
    CHECK(CSystem::GetInstance() == NULL);
}

static int StopWithoutJob(CThreadControl* p)
{
    __try
    {
        p->Stop();
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                               : EXCEPTION_CONTINUE_SEARCH)
    {
        return 1;
    }
    return 0;
}

static void TestStopWithoutJob()
{
    // THREAD-002: a thread without a job ends at once, but Stop dereferences the job.
    CThreadManager* pMgr = new CThreadManager;
    THREAD_DESC desc;
    CHECK(pMgr->CreateThreads(NULL, 1, NULL, &desc, 0) == TRUE);
    CThreadControl* p = pMgr->m_mapThreads.begin()->second;
    CHECK(WaitForSingleObject(p->m_hThread, 5000) == WAIT_OBJECT_0);
    CHECK(p->m_dwExitCode == (DWORD)-1);
    CHECK(StopWithoutJob(p) == 1);
    CHECK(p->m_hThread == NULL);            // the crash comes after the join
    delete pMgr;                            // the lock is recursive, Stop returns FALSE
}

static void TestShortJoin()
{
    // After an assert the join gives up after 100 ms.
    HANDLE hHold = CreateEvent(NULL, TRUE, FALSE, NULL);
    CTestJob job(0, hHold);
    CThreadManager mgr;
    THREAD_DESC desc;
    desc.pJob = &job;
    CHECK(mgr.CreateThreads(NULL, 1, NULL, &desc, 0) == TRUE);
    CHECK(WaitFor(&job.m_nRuns, 1));
    CThreadControl* p = mgr.m_mapThreads.begin()->second;

    BOOL bOld = g_bBSDebugAsserted;
    g_bBSDebugAsserted = TRUE;
    DWORD dwStart = GetTickCount();
    CHECK(p->Stop() == TRUE);
    DWORD dwElapsed = GetTickCount() - dwStart;
    g_bBSDebugAsserted = bOld;
    CHECK(dwElapsed >= 80 && dwElapsed < 2000);
    CHECK(p->m_hThread == NULL);
    CHECK(job.m_nActive == 1);              // still running
    SetEvent(hHold);
    CHECK(WaitFor(&job.m_nActive, 0));
    CloseHandle(hHold);
}

static void TestEventHandler()
{
    int n = 0;
    CEventHandler* p = new CTestHandler(&n);
    CHECK(p->AttachToProactor(NULL, NULL) == TRUE);
    delete p;
    CHECK(n == 1);
}

int main()
{
    TestThunk();
    TestKeys();
    TestManagerLifecycle();
    TestExitCode();
    TestAffinity();
    TestTask();
    TestTaskManager();
    TestSystem();
    TestStopWithoutJob();
    TestShortJoin();
    TestEventHandler();
    return TEST_RESULT("ThreadTest");
}
