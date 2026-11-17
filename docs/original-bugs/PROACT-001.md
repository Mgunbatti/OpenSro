# PROACT-001: failed completion of a pooled message is released twice

- Original: `CProactorIOCP::CompletionWorkerLoop` 0x4972f0, `CPeerProcessIOCP::CompletionWorkerLoop`
  0x49c7f0 (GatewayServer)
- Rewrite: `CProactorIOCP::Run` in `JMX_Library/BSNet/Proactor.cpp`, `CPeerProcessIOCP::Run` in
  `JMX_Library/BSNet/PeerProcess.cpp`
- Severity: crash

## What the original does

When `GetQueuedCompletionStatus` returns FALSE with a completion key, the loop hands the overlapped
block back to the engine (`ReleaseMsg`, slot 19) if its `+0x1C` field is 0 (a pooled `CMsg`). It
then still calls the handler's `OnIOCompletion` with that block (`CProactorIOCP` only skips this
when the socket state `+0x4C` is 0), and if the handler returns non-zero it calls `ReleaseMsg` on
the same block a second time.

## Effect

The handler works on a message that is already back in the pool and may have been handed out to
another thread. The second release decrements the reference count again or puts the message into
the free list twice, so two later `AllocMsg` calls can get the same buffer. Failed completions
happen on every connection reset or aborted operation.

## Fix

Release the block only once: either skip the dispatch for failed completions of pooled messages,
or drop the release before the dispatch and keep the one after it.
