#pragma once

#include <winsock2.h>
#include <windows.h>

#include "ServerFramework/MonitorGraph.h"
#include "ServerFramework/ServerApp.h"

// GlobalManager: the division's central server. It keeps the content users the AgentServers log
// in (with the billing server), answers the shard managers' account requests, runs the periodic
// account database jobs and serves the service manager console (CGlobalManagerMainProcess); a
// slave task does the silk history and IP statistics work (CGlobalManagerSlave).

// Monitor graph IDs of the application (CGlobalManager::OnLocalDataInitialized).
enum
{
    GM_COUNTER_CONTENT_USER         = 0x3020001,
    GM_COUNTER_REGISTERED           = 0x3020002,
    GM_COUNTER_UNREGISTERED         = 0x3020003,
    GM_COUNTER_DAILY_VISITED        = 0x3020004,
    GM_COUNTER_TOTAL_VISITED        = 0x3020005,
    GM_COUNTER_OCCUPIED_IP          = 0x3020006,
};

// The application object (0x620 bytes, vtable 0x52AB14, instance 0x599F30).
class CGlobalManager : public ServerFramework::CServerApp
{
public:
    // CMachineManager_ConstructGlobal_513538 (0x40bcd0, the dynamic initializer)
    CGlobalManager() {}
    // 0x40bd80 (and the atexit destructor 0x5259c0)
    virtual ~CGlobalManager() {}                                            // 0

    // vf03 (0x40c010): the framework defaults, security mode 3.
    virtual void SetDefaultEngineConfig();                                  // 3
    // vf07 (0x40bf80): structured exceptions become C++ exceptions; the main process
    // (SERVER_TASK_MAIN, 1 thread) and the slave (GM_TASK_SLAVE, 1 thread, priority -1).
    virtual bool RegisterMainProcessTask();                                 // 7
    // vf11 (0x40c0b0): the databases, the security descriptions, the franchise addresses, the
    // punishments, the slave, the user validation history, state 4, the monitor graphs, 0x631C
    // to the parent and the last play time reset. False when a step fails.
    virtual bool OnLocalDataInitialized();                                  // 11
    // vf12 (0x40c790): the default DB session is destroyed and the content users removed.
    virtual bool OnShutdown();                                              // 12

    // sub_40C7B0: _SecurityDescription, _SecurityDescriptionGroup and
    // _SecurityDescriptionGroupAssign of the default DB into the certification data.
    bool LoadSecurityDescriptions();
    // sub_40CAF0: row trigger of LoadSecurityDescriptions (m_dwParam 0 / 1 / 2 is the table).
    int OnSecurityDescriptionRow(DWORD dwQuery, DWORD dwUnused);
};

#ifdef JMX_FRAMEWORK_2011
C_ASSERT(sizeof(CGlobalManager) == 0x620);
#endif

// 0x599F30, g_pServerApp.
extern CGlobalManager g_GlobalManager;

// sub_40C020: the six graphs of the content users (dwID 0..5); the registration counters are
// reset by reading them.
bool __cdecl GlobalManager_ReadContentUserCounter(MONITOR_COUNTER* pCounter);
// sub_40F240: the active punishments and their timer (0x22, 60 s).
void GlobalManager_LoadPunishments();
// sub_40F2A0: the punisher and the three checks of the user validation history from Server.cfg.
// True.
bool GlobalManager_InitValidationHistory();
// sub_40CAA0: the last play time reset of the account DB (ResetPlayTime job type 0, run here).
bool GlobalManager_LoadLastPlayTimeReset();
