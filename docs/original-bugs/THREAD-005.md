# THREAD-005: task destructor does not stop its threads

- Original: `CTask::dtor`, GatewayServer 0x495b40 (calls `CTask_StopWorkerThreadsLocked` 0x495c40)
- Rewrite: `CTask::~CTask` in `JMX_Library/BSLib/thread/Task.cpp`
- Severity: major

## What the original does

The destructor first sets the thread manager pointer (`+0x14`) to NULL, stops the queue and signals
the job, then calls `StopThreads`, which does nothing when the thread manager is NULL. It then
deletes the job (the proactor) and the queue.

## Effect

Worker threads are signalled but never joined. When a proactor is unregistered at run time
(`CNetEngine::UnregisterProactor`) its job and queue are freed while worker threads may still be
inside the job's loop (use after free). The `CThreadControl` objects keep a dangling job pointer;
when the thread manager is destroyed at shutdown, `Stop` calls `GetRuntimeClass`/`SignalStop` on
freed memory if the thread handle is still open. At normal shutdown the thread manager is deleted
before the task manager, which hides the problem.

## Fix

Call `StopThreads()` before clearing the thread manager pointer, and delete the job only after the
threads are joined.
