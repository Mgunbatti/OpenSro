# FWAPP-008: lost-session log indexes the error name table without bounds

- Original: `CServerProcessMain::OnSessionLost_impl` (0x43faa0), table 0x4F8720, GatewayServer
- Rewrite: `CServerProcessMain::OnSessionLost` / `g_apszNetEngineError` in `JMX_ServerFramework/ServerFramework/ServerProcessMain.cpp`
- Severity: minor

## What the original does

With `ReportConnectionLog` set, reason 0 (engine error) is logged as
`NETENGINE_ERROR-<name>` with the name taken from a 22-entry table at index
`HIWORD(reason) & 0x7FFF`.

## Effect

Any error code from 22 up reads past the table (the following data is a type descriptor, then
other globals) and passes whatever it finds as string pointer to the formatter: garbage in the
log or an access violation in the main process.

## Fix

Check the index against the table size and log the number otherwise.
