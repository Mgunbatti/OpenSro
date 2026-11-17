# SOCK-003: a message rejected by the send queue limit is never released

- Original: `CSockStream::SendQueuedWithLimit` 0x46f240 (GatewayServer)
- Rewrite: `CSockStream::SendQueuedWithLimit` in `JMX_Library/BSNet/SockStream.cpp`
- Severity: wrong

## What the original does

`CSockStream::PrepareOutgoingMsg` either adds a reference to the caller's message or, for an
encrypted message, creates a new pooled message holding the encrypted packet. Then the queue
size is compared with the per-role limit; above the limit the function logs
`NERR_PEER_DOESNT_RESPONSE - %d:%d` and returns 0x8015 without queueing the message and without
dropping the reference (or releasing the encrypted copy).

## Effect

Every send to a peer that stopped reading leaks one pooled message. A slow client that keeps the
queue over the limit makes the server lose messages from its pool until the pool grows.

## Fix

On the limit error, release the reference taken by `PrepareOutgoingMsg` (or the encrypted copy)
before returning.
