# PROACT-003: failed stop packets are never detected

- Original: `PostQueuedCompletionStatus_Repeat` 0x497540 / `PostQueuedCompletionStatus_wrap`
  0x497520, `IOCP_PostCompletionNTimes` 0x49ca20 / `PostQueuedCompletionStatus_wrap_2` 0x49ca00
  (GatewayServer)
- Rewrite: `CProactorIOCP::PostStopPackets`, `CPeerProcessIOCP::PostStopPackets`
- Severity: minor

## What the original does

The helper that posts one stop packet returns 1 when `PostQueuedCompletionStatus` succeeded and 0
otherwise (or the NULL port). The loop that posts one packet per worker stops early and reports
failure only when the helper returns -1.

## Effect

A failed post is ignored, the remaining packets are still posted and `SignalStop` reports success.
A worker that did not get its packet keeps waiting; the thread join in `CThreadControl::Stop` then
blocks.

## Fix

Test the helper's result for 0 instead of -1.
