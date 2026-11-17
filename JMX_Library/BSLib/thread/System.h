#pragma once

#include <windows.h>

#include "BSLib/SingletonT.h"

class CSockSystem;
class CTaskManager;
class CThreadManager;

// Owner of the network engine's process-wide subsystems. One static instance (g_System) is
// built before main; CNetEngine reaches it through CSystem::GetInstance().
class CSystem : public CSingletonT<CSystem>
{
public:
    CSystem();
    virtual ~CSystem();                         // 0

    // Cleans up Winsock, stops the task queues, then deletes the thread manager, the task
    // manager and the socket system in that order.
    void ReleaseSubsystems();

public:
    CSockSystem*    m_pSockSystem;      // +4
    CTaskManager*   m_pTaskManager;     // +8
    CThreadManager* m_pThreadManager;   // +C
};

C_ASSERT(FIELD_OFFSET(CSystem, m_pSockSystem) == 4);
C_ASSERT(sizeof(CSystem) == 0x10);

extern CSystem g_System;
