# FWUTIL-005: monitor update thread handle never closed, stale stop event kept

- Original: `MonitorGraph_StartUpdateThread` (0x446820) and
  `MonitorGraph_StopUpdateThreadAndFreeCounters` (0x446870), GatewayServer
- Rewrite: same names in `JMX_ServerFramework/ServerFramework/MonitorGraph.cpp`
- Severity: minor

## What the original does

Start stores the handles of the stop event and of the update thread in two globals. Stop sets
the event, waits for the thread and closes the event handle, but leaves the global set and
never closes the thread handle.

## Effect

The thread handle leaks once per window-mode run. A second call of Stop signals and closes a
handle value that may meanwhile belong to another object.

## Fix

Close the thread handle after the wait and reset both globals to NULL.
