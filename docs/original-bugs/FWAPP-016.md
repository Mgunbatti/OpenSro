# FWAPP-016: service uninstall leaks handles and always reports failure

- Original: `NTService_HandleCommandLine` (0x454190, not referenced), GatewayServer
- Rewrite: `UninstallService` / `NTService_HandleCommandLine` in `JMX_ServerFramework/ServerFramework/NTService.cpp`
- Severity: minor

## What the original does

For `-uninstall`, a failing `DeleteService` shows the error without closing the service and the
service manager handles. The successful path closes both handles and shows "service
uninstalled successfully", but continues in the common error exit and returns -1.

## Effect

A caller cannot tell a successful uninstall from a failed one; the handles of a failed delete
stay open until the process ends.

## Fix

Close both handles on every path and return 1 after a successful uninstall.
