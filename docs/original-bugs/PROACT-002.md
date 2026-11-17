# PROACT-002: event proactor leaves holes in its wait array

- Original: `CProactorEvent_ClearHandleSlot` 0x497a30 (GatewayServer), used by
  `CProactorEvent::CloseSlotAndNotify` 0x497d30
- Rewrite: `CProactorEvent::ClearSlot` in `JMX_Library/BSNet/Proactor.cpp`
- Severity: wrong

## What the original does

Each socket takes a pair of slots in the 64-entry event array; `WSAWaitForMultipleEvents` waits on
the first `m_nHandles` entries, where `m_nHandles` is the number of non-NULL entries anywhere in
the array. Closing a socket sets its pair to NULL and recounts. Placeholder events from the free
pool are put back into the pair only when the count did not change, i.e. only when the pair was
already empty; a pair that really held a socket stays NULL. Placeholders in a cleared pair are
dropped without being returned to the pool.

## Effect

As soon as a socket other than the last one is closed, the waited range contains NULL handles.
`WSAWaitForMultipleEvents` then fails with `WSA_WAIT_FAILED`, which ends the worker loop: the
proactor stops dispatching without being stopped, and sockets in the higher slots are no longer
served. Only the Windows 9x path uses `CProactorEvent`. Dropped placeholders are leaked.

## Fix

Refill the pair with pooled events when the count changed (and recount afterwards), or compact the
array so that the waited range never contains NULL entries.
