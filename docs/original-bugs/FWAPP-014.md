# FWAPP-014: second instance writes its startup error to the drive root

- Original: `ServerApp_Startup` (0x41d280), GatewayServer
- Rewrite: `ServerApp_Startup` in `JMX_ServerFramework/ServerFramework/ServerApp.cpp`
- Severity: minor

## What the original does

The single instance check (named semaphore) comes before the module directory is determined.
When another instance runs, the error is written with `WriteStartupErrorFile`, which builds the
path as `<module dir>\StartupError_<module>.txt` from the still empty module directory.

## Effect

The file `\StartupError_GatewayServer.txt` is created in the root of the current drive (or not at
all without write access there) instead of next to the exe.

## Fix

Determine the module directory before the instance check.
