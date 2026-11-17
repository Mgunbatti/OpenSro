#include "SimpleThread.h"

#include <process.h>

#include "BSLib/BSDebug.h"

CSimpleThread::CSimpleThread()
{
    m_hThread = NULL;
    m_dwThreadID = 0;
    m_bSuspended = TRUE;
    // BUG(THREAD-001): the thunk is code in the object's (heap) memory; with DEP enforced the
    // new thread faults on its first instruction.
    InitThunk((DWORD_PTR)ThunkProc, this);
}

CSimpleThread::~CSimpleThread()
{
    Close();
}

void __stdcall CSimpleThread::Run(void*)
{
}

// Entered from the thunk with (this, argument) on the stack. The original jumps straight into
// Run, so the thread's return value is whatever Run left in eax; Run returns nothing here and
// the value is 0.
unsigned __stdcall CSimpleThread::ThunkProc(CSimpleThread* pThis, void* pArg)
{
    pThis->Run(pArg);
    return 0;
}

BOOL CSimpleThread::Create(void* pArg, unsigned nStackSize, int nPriority)
{
    if (m_hThread)
        return TRUE;

    m_hThread = (HANDLE)_beginthreadex(NULL, nStackSize,
                                       (unsigned (__stdcall*)(void*))GetCodeAddress(), pArg,
                                       CREATE_SUSPENDED, &m_dwThreadID);
    if (!m_hThread)
        return FALSE;

    SetThreadPriority(m_hThread, nPriority);
    return TRUE;
}

// Waits for the thread and closes the handle. After an assert dialog (or when the engine was
// told to shut down fast) the wait is limited to 100 ms.
BOOL CSimpleThread::Close()
{
    if (!m_hThread)
        return FALSE;

    WaitForSingleObject(m_hThread, g_bBSDebugAsserted ? 100 : INFINITE);
    CloseHandle(m_hThread);
    m_hThread = NULL;
    m_bSuspended = TRUE;
    return TRUE;
}

// Leaves the thread with a suspend count of exactly one.
BOOL CSimpleThread::Suspend()
{
    if (m_hThread && !m_bSuspended)
    {
        DWORD dwCount = SuspendThread(m_hThread);
        if (dwCount == (DWORD)-1)
        {
            m_bSuspended = FALSE;
            return FALSE;
        }
        for (DWORD i = dwCount; i > 0; --i)
            ResumeThread(m_hThread);
        m_bSuspended = TRUE;
    }
    return TRUE;
}

// Drops the suspend count to zero.
BOOL CSimpleThread::Resume()
{
    if (m_hThread && m_bSuspended == TRUE)
    {
        DWORD dwCount = ResumeThread(m_hThread);
        if (dwCount == (DWORD)-1)
            return FALSE;
        if (dwCount > 1)
        {
            for (DWORD i = dwCount - 1; i > 0; --i)
                ResumeThread(m_hThread);
        }
        m_bSuspended = FALSE;
    }
    return TRUE;
}
