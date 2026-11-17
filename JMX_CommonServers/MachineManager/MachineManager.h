#pragma once

#include <winsock2.h>
#include <windows.h>

#include "ServerFramework/ServerApp.h"

// MachineManager: runs on every server machine and starts and stops the server modules there on
// request of the GlobalManager (0x6301 / 0x6302, relayed) and reports the processor and memory
// counters (0x6304). A module is the program <module dir>\<module name>.exe; it runs when its
// single-instance semaphore (named after the module) exists. Server.cfg
// "ShutdownProcessTimeout" sets how long a stopped module may take before it is terminated.

// The application object.
class CMachineManager : public ServerFramework::CServerApp
{
public:
    // CMachineManager_ConstructGlobal_513538 (0x4012d0)
    CMachineManager();
    // 0x401380
    virtual ~CMachineManager();                                             // 0

    // vf03 (0x4012c0): the framework defaults with security mode 3.
    virtual void SetDefaultEngineConfig();                                  // 3
    // CreateMainProcess (0x401230): structured exceptions become C++ exceptions, then the main
    // process (CMachineManagerMainProcess).
    virtual bool RegisterMainProcessTask();                                 // 7
    // InitPerfCounters (0x401280): state 5 (in service), then the PDH query with both
    // counters. True.
    virtual bool OnLocalDataInitialized();                                  // 11
    // vf12 (0x4012b0): closes the PDH query and frees the counters. True.
    virtual bool OnShutdown();                                              // 12
};

C_ASSERT(sizeof(CMachineManager) == 0x494);

// 0x513538, g_pServerApp.
extern CMachineManager g_MachineManager;

// Cfg_LookupShutdownProcessTimeout (0x402bf0): value of "ShutdownProcessTimeout" of the
// module's Server.cfg block, NULL without one.
const char* MachineManager_FindShutdownProcessTimeoutConfig();
