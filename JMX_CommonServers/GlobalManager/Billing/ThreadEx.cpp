#include "ThreadEx.h"

#include <process.h>

#include "IOCP.h"
#include "BSLib/BSDebug.h"

static const char s_szThreadExFile[] =
    "D:\\WORK2005\\Source\\JMX_CommonServers\\GlobalManager\\Billing\\ThreadEx.cpp";

unsigned __stdcall ThreadEx_ThreadProc(void* pArg)
{
    return ((CThreadExMgr*)pArg)->WorkLoop();
}

// ---------------------------------------------------------------------------------------------
// CThreadEx

BOOL CThreadEx::Create(void* pArg, int nPriority, unsigned nStackSize)
{
    if (m_hThread)
        BSAssertFail();
    unsigned nThreadID;
    m_hThread = (HANDLE)_beginthreadex(NULL, nStackSize, ThreadEx_ThreadProc, pArg,
                                       CREATE_SUSPENDED, &nThreadID);
    if (!m_hThread)
        return FALSE;
    SetThreadPriority(m_hThread, nPriority);
    return TRUE;
}

BOOL CThreadEx::Resume()
{
    if (m_hThread && m_bSuspended == 1)
    {
        DWORD dwCount = ResumeThread(m_hThread);
        if (dwCount == (DWORD)-1)
            return FALSE;
        if (dwCount > 1)
        {
            for (DWORD i = dwCount - 1; i; --i)
                ResumeThread(m_hThread);
        }
        InterlockedExchange(&m_bSuspended, 0);
    }
    return TRUE;
}

BOOL CThreadEx::Suspend()
{
    if (m_hThread && !m_bSuspended)
    {
        DWORD dwCount = SuspendThread(m_hThread);
        if (dwCount == (DWORD)-1)
            return FALSE;
        for (DWORD i = dwCount; i; --i)
            ResumeThread(m_hThread);
        InterlockedExchange(&m_bSuspended, 1);
    }
    return TRUE;
}

void CThreadEx::Close()
{
    if (!m_hThread)
        return;
    DWORD dwExitCode;
    if (!GetExitCodeThread(m_hThread, &dwExitCode)
        && !BSAssertDialog(24, s_szThreadExFile, "GetExitCodeThread(m_hThread, &dwExitCode)"))
    {
        DebugBreak();
    }
    if (dwExitCode == STILL_ACTIVE)
        TerminateThread(m_hThread, (DWORD)-1);
    CloseHandle(m_hThread);
}

// ---------------------------------------------------------------------------------------------
// CThreadExMgr

CThreadExMgr::~CThreadExMgr()
{
    StopAll();
}

BOOL CThreadExMgr::CreateThread()
{
    CThreadEx* pThread = new CThreadEx(this);
    if (!pThread)
        return FALSE;
    if (!pThread->Create(this, m_nPriority, m_nStackSize))
        return FALSE;
    m_vecThread.push_back(pThread);
    return TRUE;
}

BOOL CThreadExMgr::ResumeAll()
{
    for (size_t i = 0; i < m_vecThread.size(); ++i)
    {
        if (m_vecThread.at(i)->m_bSuspended && !m_vecThread.at(i)->Resume())
            return FALSE;
    }
    return TRUE;
}

BOOL CThreadExMgr::SuspendAll()
{
    for (size_t i = 0; i < m_vecThread.size(); ++i)
    {
        if (!m_vecThread.at(i)->m_bSuspended && !m_vecThread.at(i)->Suspend())
            return FALSE;
    }
    return TRUE;
}

void CThreadExMgr::StopAll()
{
    if (m_vecThread.empty())
        return;
    HANDLE* phThread = new HANDLE[m_vecThread.size()];
    for (size_t i = 0; i < m_vecThread.size(); ++i)
        phThread[i] = m_vecThread.at(i)->m_hThread;
    ResumeAll();
    for (size_t i = 0; i < m_vecThread.size(); ++i)
    {
        if (!m_pIOCP->PostStatus(0)
            && !BSAssertDialog(185, s_szThreadExFile, "m_pIOCP->PostStatus((ULONG_PTR)0)"))
        {
            DebugBreak();
        }
    }
    WaitForMultipleObjects((DWORD)m_vecThread.size(), phThread, TRUE, 5000);
    for (size_t i = 0; i < m_vecThread.size(); ++i)
    {
        CThreadEx* pThread = m_vecThread.at(i);
        if (pThread)
        {
            pThread->Close();
            delete pThread;
        }
    }
    m_vecThread.clear();
    delete[] phThread;
}

DWORD CThreadExMgr::WorkLoop()
{
    for (;;)
    {
        ULONG_PTR dwKey;
        DWORD dwBytes;
        LPOVERLAPPED pOverlapped;
        if (!m_pIOCP->GetStatus(&dwKey, &dwBytes, &pOverlapped))
            continue;
        if (!dwKey && !dwBytes && !pOverlapped)
            return 0;
        m_pWork->DoWork(dwKey, dwBytes, pOverlapped);
    }
}
