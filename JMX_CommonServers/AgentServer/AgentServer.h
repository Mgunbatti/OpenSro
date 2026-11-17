#pragma once

#include <winsock2.h>
#include <windows.h>

#include "ServerFramework/MonitorGraph.h"
#include "ServerFramework/ServerApp.h"

// AgentServer: the server a game client plays through. The GatewayServer certifies the account
// and hands the login over (0x6203 token); the client connects here, logs in with the token
// (0x6103), is registered at the GlobalManager and the shard's game server, and from then on its
// packets are relayed to the game server and back. The server also runs the XTrap anti-cheat
// challenges and a traffic filter.

// The application object.
class CAgentServer : public ServerFramework::CServerApp
{
public:
    // The static constructor of g_AgentServer (0x4c1b30) only builds the base.
    CAgentServer() {}
    // 0x401460
    virtual ~CAgentServer() {}                                              // 0

    // vf03 (0x4014a0): engine flags 0x13, 100 pending accepts, security mode 2.
    virtual void SetDefaultEngineConfig();                                  // 3
    // CreateMainProcess (0x401500): structured exceptions become C++ exceptions, then the main
    // process (CAgentMainProcess).
    virtual bool RegisterMainProcessTask();                                 // 7
    // vf11 (0x401640): state 4, the monitor graphs, the traffic filter and XTrap. True.
    virtual bool OnLocalDataInitialized();                                  // 11
    // vf12 (0x401790): removes the users and the tokens. True.
    virtual bool OnShutdown();                                              // 12
    // vf13 (0x401550): the engine graphs (without the overlap jobs). True.
    virtual bool InitMonitorCounters();                                     // 13
};

#ifdef JMX_FRAMEWORK_2011
C_ASSERT(sizeof(CAgentServer) == 0x620);
#endif

// 0x52ABE0, g_pServerApp.
extern CAgentServer g_AgentServer;

// sub_4015A0: graph "UserCount" (users registered).
bool __cdecl AgentServer_ReadUserCount(MONITOR_COUNTER* pCounter);
// sub_4015D0 (sweep): graph "TokenCount" (tokens waiting).
bool __cdecl AgentServer_ReadTokenCount(MONITOR_COUNTER* pCounter);
// sub_4015E0: graphs "MsgSentCount" (dwID 0) and "MsgSentBytes" (dwID 1) of the relays to
// clients; reading resets the counter.
bool __cdecl AgentServer_ReadMsgSentCounter(MONITOR_COUNTER* pCounter);
