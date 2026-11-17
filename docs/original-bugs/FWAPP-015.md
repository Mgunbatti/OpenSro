# FWAPP-015: quit request never reaches the console and service loops

- Original: `ServerApp_PostQuit` (0x41d8b0), `ServerApp_RunConsoleMode` (0x4539c0), `NTService_ServiceMainLoop` (0x453f60), GatewayServer
- Rewrite: `ServerApp_PostQuit` (ServerFramework.cpp), `ServerApp_RunConsoleMode` (ServerApp.cpp), `NTService_ServiceMain` (NTService.cpp) in `JMX_ServerFramework/ServerFramework`
- Severity: minor

## What the original does

`ServerApp_PostQuit` posts an empty packet to the application's completion port only when the
IOCP mode flag (0x53B400) is set, and `WM_CLOSE` to the server window otherwise. The flag is
cleared by `ServerApp_Startup` and never set, also not by the console and service modes, which
run a completion port loop without a window.

## Effect

In console or service mode, a rejected certification (0xA003 result 2), a failed listener and
the 0x200B control message do not stop the server; the `WM_CLOSE` goes to a NULL window. Window
mode, the only mode GatewayServer uses, is not affected.

## Fix

Set the IOCP mode flag in the console and service modes.
