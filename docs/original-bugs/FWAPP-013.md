# FWAPP-013: service name copied with a 256-character limit into 64 bytes

- Original: `ServerApp_Startup` (0x41d280), GatewayServer
- Rewrite: `ServerApp_Startup` in `JMX_ServerFramework/ServerFramework/ServerApp.cpp`
- Severity: minor

## What the original does

The module name is copied into the service name field of the server globals (0x53B408, 64 bytes,
followed by the command line) with an inlined `StringCchCopyA(dst, 256, name)`.

## Effect

A module name of 64 characters or more would overwrite the command line copy (which is written
right after it, so the damage is hidden) and, beyond 324 characters, the module directory. With
the compiled-in names this does not happen.

## Fix

Use the size of the field as limit.
