# THREAD-006: task queue used without NULL check

- Original: `CTask::dtor` 0x495b40 and `CTaskManager_ForEachProactor_CallVf07` 0x496d20,
  GatewayServer
- Rewrite: `CTask::~CTask` and `CTaskManager::StopAllQueues` in `JMX_Library/BSLib/thread/Task.cpp`
- Severity: minor

## What the original does

Both call `Stop()` on the task's queue (`+4`) without checking it. The queue is created by
`Proactor_Create`; when that fails before creating it (`GetVersionEx` failure), `RegisterProactor`
deletes the half-built task. The destructor checks the queue for NULL later on, so the author
expected it can be missing.

## Effect

The failed registration crashes the process instead of returning an error. Practically unreachable
on supported systems.

## Fix

Check `m_pQueue` before calling `Stop()`.
