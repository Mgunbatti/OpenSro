# FWAPP-005: lost session walks the overlap job map without its lock

- Original: `CServerProcessMain::OnSessionLost_impl` (0x43faa0), GatewayServer
- Rewrite: `CServerProcessMain::OnSessionLost` in `JMX_ServerFramework/ServerFramework/ServerProcessMain.cpp`
- Severity: crash

## What the original does

When a session bound to a cord is lost, the main process iterates the job map of its
`COverlapJobManager`, fails every relay job routed through that cord and erases it, all without
taking the manager's critical section, which every other access (insert, remove, release) uses.

## Effect

A job registered or removed at the same time by another thread (relays started from overlap
jobs, `SendOverlapJob` from other processes) corrupts the map or invalidates the iterator.

## Fix

Hold `m_OverlapJobManager.m_cs` while walking and erasing.
