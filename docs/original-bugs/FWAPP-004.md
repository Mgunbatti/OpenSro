# FWAPP-004: overlap job pools and ID pool used from several threads without a lock

- Original: `IdPool_PopFront` (0x448f40), `COverlapJob::ReleaseId` (0x448250), `RelayMsgPool_Acquire` (0x449180), `COverlapJob_*::ReturnToPool` (0x43ef70, 0x4483b0), GatewayServer
- Rewrite: `g_OverlapJobIDPool`, `g_OverlapJobInitLocalDataPool`, `g_OverlapJobRelayMsgPool` in `JMX_ServerFramework/ServerFramework/OverlapJob.cpp`
- Severity: crash

## What the original does

The job ID pool (a `std::deque` with counters) and the class pools of the relay and
initialisation jobs are single-threaded chunk allocators without any lock. Relay jobs are taken
by `RelayMsgToBody_6008`, which the application calls from the main process and from jobs that
run on the overlap process (the user certifier of GatewayServer), and returned on the main
process when the answer arrives.

## Effect

Concurrent use corrupts the deques: the same ID or job object can be handed out twice, IDs get
lost, and a deque reallocation on one thread while the other reads crashes the server.

## Fix

Guard the pools and the ID pool with a critical section (as `CChunkAllocatorMT` does).
