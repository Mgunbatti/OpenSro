#include "System.h"

#include "BSLib/thread/Task.h"
#include "BSLib/thread/ThreadManager.h"
#include "BSNet/SockSystem.h"

CSystem g_System;

CSystem::CSystem()
{
    m_pSockSystem = new CSockSystem;
    m_pTaskManager = new CTaskManager;
    m_pThreadManager = new CThreadManager;
}

CSystem::~CSystem()
{
    ReleaseSubsystems();
}

void CSystem::ReleaseSubsystems()
{
    if (m_pSockSystem)
        m_pSockSystem->Cleanup();
    if (m_pTaskManager)
        m_pTaskManager->StopAllQueues();
    if (m_pThreadManager)
    {
        delete m_pThreadManager;
        m_pThreadManager = NULL;
    }
    if (m_pTaskManager)
    {
        delete m_pTaskManager;
        m_pTaskManager = NULL;
    }
    if (m_pSockSystem)
    {
        delete m_pSockSystem;
        m_pSockSystem = NULL;
    }
}
