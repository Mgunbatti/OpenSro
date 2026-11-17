# THREAD-002: Stop dereferences a NULL job

- Original: `ThreadControl_StopWorker`, GatewayServer 0x49cd00
- Rewrite: `CThreadControl::Stop` in `JMX_Library/BSLib/thread/ThreadManager.cpp`
- Severity: minor

## What the original does

Before resuming the thread, `Stop` checks `m_desc.pJob` before calling `GetRuntimeClass()` for a
(compiled-out) trace. After joining the thread it repeats the trace call without the check, then
clears `m_desc.pJob`.

## Effect

A thread created from a descriptor without a job (its thread body returns immediately) crashes the
caller of `Stop` with an access violation: `CThreadControl`'s destructor,
`CThreadManager::StopThreads` or the manager's destructor at shutdown. The engine always fills in
the job, so this is latent.

## Fix

Guard the second trace with the same `if (m_desc.pJob)` check.
