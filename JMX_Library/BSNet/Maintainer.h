#pragma once

#include <windows.h>
#include <vector>

#include "BSLib/thread/ServiceObject.h"

// Callbacks of T run every dwPeriod milliseconds of accumulated elapsed time.
template <class T>
class CScheduledCallbacker
{
public:
    typedef void (T::*PFN_CALLBACK)();

    struct SCHEDULE
    {
        DWORD           dwElapsed;      // +0
        DWORD           dwPeriod;       // +4
        T*              pObject;        // +8
        PFN_CALLBACK    pfnCallback;    // +C
    };

    CScheduledCallbacker() {}
    virtual ~CScheduledCallbacker() {}                  // 0

    void Add(DWORD dwPeriod, T* pObject, PFN_CALLBACK pfnCallback)
    {
        SCHEDULE s;
        s.dwElapsed = 0;
        s.dwPeriod = dwPeriod;
        s.pObject = pObject;
        s.pfnCallback = pfnCallback;
        m_vecSchedules.push_back(s);
    }

    // Adds dwElapsed to every entry and runs the ones that reached their period (once per
    // call, the remainder is kept).
    void Tick(DWORD dwElapsed)
    {
        if (dwElapsed == 0)
            return;
        for (size_t i = 0; i < m_vecSchedules.size(); ++i)
        {
            SCHEDULE& s = m_vecSchedules[i];
            s.dwElapsed += dwElapsed;
            if (s.dwElapsed >= s.dwPeriod)
            {
                s.dwElapsed -= s.dwPeriod;
                (s.pObject->*s.pfnCallback)();
            }
        }
    }

public:
    std::vector<SCHEDULE>   m_vecSchedules;     // +4
};

// One periodic job: dwPeriod in milliseconds, dwParam passed to the job.
struct MAINTAINER_JOB
{
    DWORD dwPeriod;     // +0
    DWORD dwParam;      // +4
};

// Initialize parameter of CMaintainer (32 bytes, copied). The constructor's defaults come from
// TimeoutConfig_InitDefaults (0x43eda0); dwFlags is left uninitialised.
struct MAINTAINER_CONFIG
{
    enum
    {
        FLAG_REAP_SESSIONS  = 0x01,     // aJobs[0]: SessionMgr_ReapExpiredSessions(dwParam)
        FLAG_KEEP_ALIVE     = 0x04      // aJobs[2]: KeepAlive_TryConnect
    };

    MAINTAINER_CONFIG()
    {
        for (int i = 0; i < 3; ++i)
        {
            aJobs[i].dwPeriod = (DWORD)-1;
            aJobs[i].dwParam = 0;
        }
        aJobs[0].dwPeriod = 60000;
        dwWaitTimeout = 1000;
        aJobs[0].dwParam = 60000;
    }

    DWORD           dwWaitTimeout;  // +0   worker wait in milliseconds (the tick granularity)
    DWORD           dwFlags;        // +4
    MAINTAINER_JOB  aJobs[3];       // +8
};

C_ASSERT(sizeof(MAINTAINER_CONFIG) == 0x20);

class __single_inheritance CMaintainer;

// The engine's housekeeping worker: session reaping and keep-alive reconnects on timers.
class CMaintainer : public CServiceObject
{
    BS_DECLARE_DYNCREATE(CMaintainer)                   // 0

public:
    CMaintainer();
    virtual ~CMaintainer();                             // 1

    // pParam is a MAINTAINER_CONFIG; registers the timers selected by its dwFlags.
    virtual BOOL Initialize(void* pOwner, void* pParam);    // 2
    // Waits on the stop event with the configured timeout and ticks the timers by the time
    // spent waiting; returns when the event is set or the stop flag is raised.
    virtual DWORD Run(volatile LONG* pbStop, int nIndex);   // 3
    virtual int SignalStop();                           // 4

    void ReapSessions();
    void KeepAlive();

public:
    HANDLE                              m_hEvent;       // +4C  auto-reset
    MAINTAINER_CONFIG                   m_config;       // +50
    CScheduledCallbacker<CMaintainer>   m_Scheduler;    // +70
};

C_ASSERT(sizeof(CScheduledCallbacker<CMaintainer>::SCHEDULE) == 16);
C_ASSERT(sizeof(CScheduledCallbacker<CMaintainer>) == 0x14);
C_ASSERT(FIELD_OFFSET(CMaintainer, m_hEvent) == 0x4C);
C_ASSERT(FIELD_OFFSET(CMaintainer, m_config) == 0x50);
C_ASSERT(FIELD_OFFSET(CMaintainer, m_Scheduler) == 0x70);
C_ASSERT(sizeof(CMaintainer) == 0x84);
