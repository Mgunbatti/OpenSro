# PROACT-005: stopping the overlapped peer process does not drain its send queue

- Original: `CPeerProcessOverlapped::Stop_DrainSendQueue` 0x49c380 (GatewayServer)
- Rewrite: `CPeerProcessOverlapped::SignalStop` in `JMX_Library/BSNet/PeerProcess.cpp`
- Severity: minor

## What the original does

`SignalStop` stops the send queue (`CQueMT9x`), sets the stop event and then pops messages with
`Pop(&p, 1, TRUE)` while it returns 1, handing each one back to the engine. A stopped `CQueMT9x`
with `bEvenIfStopped` set removes one message but returns FALSE.

## Effect

The loop never runs: the first queued message is removed and lost, the others stay in the queue
and are never returned to the message pool (the queue's destructor does not release them). Only
the Windows 9x path uses `CPeerProcessOverlapped`.

## Fix

Release the message returned by the first `Pop` and loop while the returned pointer is not NULL,
or make the stopped queue return TRUE while it still has messages.
