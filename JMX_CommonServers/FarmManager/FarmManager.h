#pragma once

#include <winsock2.h>
#include <windows.h>

#include "ServerFramework/ServerApp.h"

// FarmManager: the manager of one server farm. Besides the framework (certification, server
// architecture, cords, relays, file transfer) it runs the UDP ping echo service for the clients
// and, when its cord to the GlobalManager is up, drops the patch file data and asks the
// GlobalManager for it again (0x6306). It is built on the 2007 framework: compile it, its users
// and the libraries with JMX_FRAMEWORK_2007 (see README.md and the farm target).

// The application object.
class CFarmManager : public ServerFramework::CServerApp
{
public:
    // CFarmManager_ConstructGlobal_512558 (0x401270)
    CFarmManager();
    // 0x401320
    virtual ~CFarmManager();                                                // 0

    // vf03 (0x401260): the framework defaults with security mode 3.
    virtual void SetDefaultEngineConfig();                                  // 3
    // CreateMainProcess (0x401210): structured exceptions become C++ exceptions, then the main
    // process (CFarmManagerMainProcess).
    virtual bool RegisterMainProcessTask();                                 // 7
    // vf11 (0x401200): state 5 (in service). True.
    virtual bool OnLocalDataInitialized();                                  // 11
};

C_ASSERT(sizeof(CFarmManager) == 0x494);

// 0x512558, g_pServerApp.
extern CFarmManager g_FarmManager;
