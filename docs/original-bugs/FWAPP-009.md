# FWAPP-009: body state observer left pointing to the deleted trigger

- Original: `CServerProcessMain::ctor` (0x43f050) / `dtor` (0x43f170), GatewayServer
- Rewrite: `CServerProcessMain::~CServerProcessMain` in `JMX_ServerFramework/ServerFramework/ServerProcessMain.cpp`
- Severity: minor

## What the original does

The main process creates a `CClassTrigger<CServerProcessMain>` and stores it in the global
`g_pServerBodyStateObserver` (0x4F9CD0), which `ServerBody_SetStateAndBroadcast2005` calls on
every body state change. The destructor deletes the trigger but does not clear the global.

## Effect

A body state change after the main process was destroyed (engine shutdown deletes the
processes) calls into freed memory. In the shutdown order of `CServerApp::Shutdown` the state is
set before the engine goes down, so the window is small, but any later `SetServerState` crashes.

## Fix

Clear `g_pServerBodyStateObserver` in the destructor when it still points to the own trigger.
