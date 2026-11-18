#pragma once

#include <winsock2.h>
#include <windows.h>
#include <vector>

class CIOCP;
class CThreadExMgr;

// sThreadWork: the receiver of the completions a CThreadEx worker takes from the port.
struct sThreadWork
{
    virtual void DoWork(ULONG_PTR dwKey, DWORD dwBytes, LPOVERLAPPED pOverlapped) = 0; // 0
    virtual ~sThreadWork() {}                                                        // 1
};

// CThreadEx (Billing\ThreadEx.cpp, 0xC bytes): one worker thread, created suspended.
class CThreadEx
{
public:
    // ThreadExEntry_ctor_4540F0
    explicit CThreadEx(CThreadExMgr* pMgr)
    {
        m_pMgr = pMgr;
        m_hThread = NULL;
        m_bSuspended = 1;
    }

    // sub_454170: _beginthreadex suspended with the argument, then the priority. False when
    // the thread cannot be created.
    BOOL Create(void* pArg, int nPriority, unsigned nStackSize);
    // sub_4541D0: resumes a suspended thread (the extra suspend counts too). False when
    // ResumeThread fails.
    BOOL Resume();
    // sub_454230: suspends a running thread once. False when SuspendThread fails.
    BOOL Suspend();
    // sub_454110: terminates the thread when it still runs and closes the handle.
    void Close();

public:
    CThreadExMgr*   m_pMgr;         // +0
    HANDLE          m_hThread;      // +4
    volatile LONG   m_bSuspended;   // +8
};

C_ASSERT(sizeof(CThreadEx) == 0xC);

// The billing manager's event notifier ("ThreadExMgr", 0x20 bytes): the workers that take the
// completions of the port and pass them to the work object.
class CThreadExMgr
{
public:
    // sub_454280
    CThreadExMgr(sThreadWork* pWork, CIOCP* pIOCP)
    {
        m_pWork = pWork;
        m_pIOCP = pIOCP;
        m_nPriority = 0;
        m_nStackSize = 0;
    }
    // sub_4542E0: stops the workers and frees the list.
    ~CThreadExMgr();

    // sub_4543A0: one more worker; false when it cannot be created.
    BOOL CreateThread();
    // sub_454450 (ResumeTask): resumes every suspended worker; false (and stops) when one
    // cannot be resumed.
    BOOL ResumeAll();
    // sub_4544C0: suspends every running worker; false when one cannot be suspended.
    BOOL SuspendAll();
    // sub_454530: resumes the workers, posts one empty completion per worker, waits up to
    // 5 s for all of them and deletes them.
    void StopAll();
    // sub_454350: the loop of a worker; ends on an empty completion.
    DWORD WorkLoop();

public:
    std::vector<CThreadEx*> m_vecThread;    // +0
    sThreadWork*            m_pWork;        // +10
    CIOCP*                  m_pIOCP;        // +14
    int                     m_nPriority;    // +18
    unsigned                m_nStackSize;   // +1C
};

C_ASSERT(sizeof(CThreadExMgr) == 0x20);

// CThreadEx_ThreadProc_4540E0: the thread procedure (the argument is the CThreadExMgr).
unsigned __stdcall ThreadEx_ThreadProc(void* pArg);
