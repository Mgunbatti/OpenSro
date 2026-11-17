# THREAD-004: stopped threads are never removed

- Original: `WorkerThreads_ApplyFn49AC20` / `ThreadControl_StopWorker_wrap`, GatewayServer 0x49ad80
  / 0x49ac20
- Rewrite: `CThreadManager::StopThreads` in `JMX_Library/BSLib/thread/ThreadManager.cpp`
- Severity: minor

## What the original does

Stopping a task's threads joins them and closes their handles, but the `CThreadControl` objects stay
in the manager's key map. Only the manager's destructor deletes them. The same applies to the
threads already created when `CTask::StartThreads` fails half way.

## Effect

Each proactor start/stop cycle leaks 128 bytes plus a map node per thread until shutdown, and later
`SuspendThreads`/`ResumeThreads`/`StopThreads` calls keep iterating the dead entries.

## Fix

Erase and delete the matching entries after stopping them.
