#include "Maintainer.h"

#include <string.h>

#include "BSNet/NetEngineFwd.h"

BS_IMPLEMENT_DYNCREATE(CMaintainer, &CServiceObject::classCServiceObject)

CMaintainer::CMaintainer()
{
    m_hEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
}

CMaintainer::~CMaintainer()
{
    if (m_hEvent)
    {
        CloseHandle(m_hEvent);
        m_hEvent = NULL;
    }
}

BOOL CMaintainer::Initialize(void* pOwner, void* pParam)
{
    m_pOwner = pOwner;
    m_pParam = pParam;
    const MAINTAINER_CONFIG* pConfig = (const MAINTAINER_CONFIG*)pParam;
    memcpy(&m_config, pConfig, sizeof(MAINTAINER_CONFIG));
    if (pConfig->dwFlags & MAINTAINER_CONFIG::FLAG_REAP_SESSIONS)
        m_Scheduler.Add(pConfig->aJobs[0].dwPeriod, this, &CMaintainer::ReapSessions);
    if (pConfig->dwFlags & MAINTAINER_CONFIG::FLAG_KEEP_ALIVE)
        m_Scheduler.Add(pConfig->aJobs[2].dwPeriod, this, &CMaintainer::KeepAlive);
    return TRUE;
}

DWORD CMaintainer::Run(volatile LONG* pbStop, int /*nIndex*/)
{
    if (*pbStop)
        return 0;

    InterlockedIncrement(&m_nActive);
    while (!*pbStop)
    {
        DWORD dwStart = GetTickCount();
        if (WaitForSingleObject(m_hEvent, m_config.dwWaitTimeout) != WAIT_TIMEOUT)
            break;
        m_Scheduler.Tick(GetTickCount() - dwStart);
    }
    InterlockedDecrement(&m_nActive);
    return 0;
}

int CMaintainer::SignalStop()
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (m_hEvent)
        SetEvent(m_hEvent);
    return 1;
}

void CMaintainer::ReapSessions()
{
    NetEngine_ReapExpiredSessions(g_pNetEngine, m_config.aJobs[0].dwParam);
}

void CMaintainer::KeepAlive()
{
    NetEngine_KeepAliveTryConnect(g_pNetEngine);
}
