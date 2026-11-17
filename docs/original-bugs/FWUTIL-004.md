# FWUTIL-004: net engine counters are registered with a sample limit of 0

- Original: `MonitorGraph_RegisterNetEngineCounter` (0x446d10), GatewayServer
- Rewrite: `MonitorGraph_RegisterNetEngineCounter` in `JMX_ServerFramework/ServerFramework/MonitorGraph.cpp`
- Severity: wrong

## What the original does

The counter object is zero-filled except for its ID and update callback, so its first field,
the number of samples the graph keeps, is 0. `MonitorGraph_AddCounterLocked` sets the same field
to 100 for counters of the caller. The monitor view (`CMoniterGraphData::AddSample`, 0x469c10)
appends each new sample and then drops the oldest one while the number of samples taken is
greater than this limit.

## Effect

With a limit of 0 every sample is dropped right after it is added. The graphs of MsgCount,
SessionCount, SockTCPCount, ClassLinkCount, AcceptCount, UsedFileCount, CachedFileCount,
MassiveMsgCount and OverlapCount never draw a line; only the current value in the legend
changes.

## Fix

Initialise the field to 100 like `MonitorGraph_AddCounterLocked` does.
