#include "ThreadManager.h"

#include <stdlib.h>
#include <time.h>
#include <process.h>

#include "BSNet/NetEngineLog.h"

#ifdef JMX_FRAMEWORK_2007
// FarmManager 0x47dbd0: the CLAMP() log with integers and the relative file name.
static void ClampLog(int nMin, int nMax, int nLine)
{
    NetEngineLog(0x2000001, "CLAMP() ==> min(%d) exceeded max(%d) value), File: %s, Line: %d",
                 nMin, nMax, ".\\thread\\threadmanager.cpp", nLine);
}
#else
// The CLAMP() log names the original source file.
static const char s_szThreadManagerFile[] =
    "D:\\WORK2005\\JMX_Library\\BSLib\\thread\\threadmanager.cpp";

static void ClampLog(double dMin, double dMax, int nLine)
{
    NetEngineLog(0x2000001, "CLAMP() ==> min(%.3f) exceeded max(%.3f) value), File: %s, Line: %d",
                 dMin, dMax, s_szThreadManagerFile, nLine);
}
#endif

// ---------------------------------------------------------------------------------------------
// CThreadControl

CThreadControl::CThreadControl()
{
    m_pManager = NULL;
    m_bStop = FALSE;
    m_bSuspended = TRUE;
    m_hThread = NULL;
    m_dwThreadID = 0;
    m_dwExitCode = 0;
    m_dwKey = 0;
}

CThreadControl::~CThreadControl()
{
    Stop();
}

BOOL CThreadControl::Create(const THREAD_DESC* pDesc, CThreadManager* pManager, DWORD dwKey)
{
    m_pManager = pManager;
    m_desc = *pDesc;
    m_dwKey = dwKey;
    return CSimpleThread::Create(this, pDesc->dwStackSize, pDesc->nPriority) != FALSE;
}

void __stdcall CThreadControl::Run(void* pArg)
{
    if (!pArg)
        return;

    CServiceObject* pJob = m_desc.pJob;
    m_dwExitCode = (DWORD)-1;
    if (pJob)
    {
        srand(1);
        srand((unsigned)time(NULL));
        m_dwExitCode = pJob->Run(&m_bStop, m_dwKey - 1);
        _endthreadex(m_dwExitCode);
    }
}

// Resumes the thread, raises the stop flag, signals the job and waits for the thread.
BOOL CThreadControl::Stop()
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (!m_hThread)
        return FALSE;

    if (m_desc.pJob)
        BS_THREAD_TRACE(m_desc.pJob->GetRuntimeClass()->m_lpszClassName);
    Resume();
    m_bStop = TRUE;
    if (m_desc.pJob)
        m_desc.pJob->SignalStop();
    Close();
    // BUG(THREAD-002): m_desc.pJob is dereferenced without the NULL check used above.
    BS_THREAD_TRACE(m_desc.pJob->GetRuntimeClass()->m_lpszClassName);
    m_desc.pJob = NULL;
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// CThreadManager

BOOL OSIsWindowsNT()
{
    OSVERSIONINFOEXA vi;
    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXA);
    if (!GetVersionExA((LPOSVERSIONINFOA)&vi))
    {
        vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
        if (!GetVersionExA((LPOSVERSIONINFOA)&vi))
            return FALSE;
    }
    return vi.dwPlatformId == VER_PLATFORM_WIN32_NT;
}

CThreadManager::CThreadManager()
{
    m_dwLastKey = 0;
    m_pfnSetThreadIdealProcessor = NULL;
    if (OSIsWindowsNT() == TRUE)
    {
        HMODULE hKernel = LoadLibraryA("kernel32.dll");
        if (hKernel)
        {
            m_pfnSetThreadIdealProcessor =
                (PFN_SETTHREADIDEALPROCESSOR)GetProcAddress(hKernel, "SetThreadIdealProcessor");
            FreeLibrary(hKernel);
        }
    }
}

CThreadManager::~CThreadManager()
{
    DestroyAllThreads();
}

// Keys are never 0 or -1.
DWORD CThreadManager::NextThreadKey()
{
    if (++m_dwLastKey == (DWORD)-1)
        m_dwLastKey = 1;
    return m_dwLastKey;
}

// Stops and deletes every thread, last key first.
void CThreadManager::DestroyAllThreads()
{
    if (m_mapThreads.empty())
        return;

    for (ThreadMap::reverse_iterator it = m_mapThreads.rbegin(); it != m_mapThreads.rend(); ++it)
    {
        CThreadControl* pThread = it->second;
        pThread->Stop();
        if (pThread)
            delete pThread;
    }
    m_mapThreads.clear();
}

// Called with m_cs held.
CThreadControl* CThreadManager::SpawnThread(DWORD* pdwThreadID, const THREAD_DESC* pDesc,
                                            DWORD dwCreateFlag)
{
    DWORD dwKey = NextThreadKey();
    CThreadControl* pThread = new CThreadControl;
    if (!pThread->Create(pDesc, this, dwKey))
    {
        if (pThread)
            delete pThread;
        return NULL;
    }

    if (pdwThreadID)
        *pdwThreadID = pThread->m_dwThreadID;
    m_mapThreads.insert(ThreadMap::value_type(dwKey, pThread));
    if (dwCreateFlag == CREATE_SUSPENDED)
        pThread->Suspend();
    else
        pThread->Resume();
    return pThread;
}

BOOL CThreadManager::CreateThreads(THREAD_AFFINITY* pAffinity, int nCount, DWORD* pdwThreadIDs,
                                   const THREAD_DESC* pDesc, DWORD dwCreateFlag)
{
    CSingleLockBS lock(&m_cs, TRUE);

    int nCPU = -1;
    if (pAffinity)
    {
        if (OSIsWindowsNT())
        {
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            int nMax = (int)si.dwNumberOfProcessors - 1;

            if (nMax < 0)
            {
#ifdef JMX_FRAMEWORK_2007
                ClampLog(0, nMax, 92);
#else
                ClampLog(0.0, (double)(int)si.dwNumberOfProcessors - 1.0, 92);
#endif
                pAffinity->nMinCPU = 0;
            }
            int n = pAffinity->nMinCPU;
            if (n < 0)
                n = 0;
            else if (n >= nMax)
                n = nMax;
            pAffinity->nMinCPU = n;

            if (nMax < 0)
            {
#ifdef JMX_FRAMEWORK_2007
                ClampLog(0, nMax, 93);
#else
                ClampLog(0.0, (double)(int)si.dwNumberOfProcessors - 1.0, 93);
#endif
                pAffinity->nMaxCPU = 0;
            }
            n = pAffinity->nMaxCPU;
            if (n < 0)
                n = 0;
            else if (n >= nMax)
                n = nMax;
            nCPU = pAffinity->nMinCPU;
            pAffinity->nMaxCPU = n;
        }
        else
        {
            pAffinity = NULL;
        }
    }

    DWORD* pdwID = pdwThreadIDs;
    for (int i = 0; i < nCount; ++i, ++pdwID)
    {
        CThreadControl* pThread = SpawnThread(pdwThreadIDs ? pdwID : NULL, pDesc, dwCreateFlag);
        if (!pThread)
            return FALSE;

        if (pAffinity)
        {
            int nRange = pAffinity->nMaxCPU - pAffinity->nMinCPU + 1;
            int nIdeal;
            if (pAffinity->bRandom)
                // BUG(THREAD-003): nRange is 0 or negative when nMaxCPU < nMinCPU.
                nIdeal = rand() % nRange + pAffinity->nMinCPU;
            else if (nCPU < 0)
                nIdeal = 0;
            else
                nIdeal = nCPU >= 32 ? 31 : nCPU;

            if (nIdeal >= 0 && m_pfnSetThreadIdealProcessor)
            {
                m_pfnSetThreadIdealProcessor(pThread->m_hThread, nIdeal);
                nCPU = nIdeal;
            }
        }
    }
    return TRUE;
}

BOOL CThreadManager::ForEachThread(CTask* pOwner, PFN_THREADOP pfn)
{
    CSingleLockBS lock(&m_cs, TRUE);
    for (ThreadMap::iterator it = m_mapThreads.begin(); it != m_mapThreads.end(); ++it)
    {
        CThreadControl* pThread = it->second;
        if (pThread->m_desc.pOwner == pOwner && !(this->*pfn)(pThread))
            BS_THREAD_TRACE(pThread);
    }
    return TRUE;
}

BOOL CThreadManager::SuspendOne(CThreadControl* pThread)
{
    return pThread->Suspend() != FALSE;
}

BOOL CThreadManager::ResumeOne(CThreadControl* pThread)
{
    return pThread->Resume() != FALSE;
}

BOOL CThreadManager::StopOne(CThreadControl* pThread)
{
    return pThread->Stop() != FALSE;
}

BOOL CThreadManager::SuspendThreads(CTask* pOwner)
{
    return ForEachThread(pOwner, &CThreadManager::SuspendOne);
}

BOOL CThreadManager::ResumeThreads(CTask* pOwner)
{
    return ForEachThread(pOwner, &CThreadManager::ResumeOne);
}

// BUG(THREAD-004): stopped threads stay in m_mapThreads until the manager is destroyed.
BOOL CThreadManager::StopThreads(CTask* pOwner)
{
    return ForEachThread(pOwner, &CThreadManager::StopOne);
}
