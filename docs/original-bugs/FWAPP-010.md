# FWAPP-010: server window pointer left dangling when the window cannot be created

- Original: `ServerApp_RunWindowMode` (0x453630), GatewayServer
- Rewrite: `ServerApp_RunWindowMode` in `JMX_ServerFramework/ServerFramework/ServerApp.cpp`
- Severity: minor

## What the original does

The new `CServerFrameWindow` is stored in `g_pServerFrameWindow` before `CreateFrame`. When
`CreateFrame` fails the object is deleted and the application shut down, but the global keeps
the old pointer.

## Effect

`LogPrintf` callers and the monitor graph code test `g_pServerFrameWindow` and use the freed
window object during the shutdown that follows (crash or garbage); only the path where the
window cannot be created is affected.

## Fix

Set `g_pServerFrameWindow` to NULL after deleting the window.
