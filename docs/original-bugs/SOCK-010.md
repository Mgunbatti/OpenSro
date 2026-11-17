# SOCK-010: handshake state byte is never initialised

- Original: CSockStream constructor and `ResetState` (GatewayServer, see the C_sockets ledger)
- Rewrite: `CSockStream::m_bHandshakeState` in `JMX_Library/BSNet/SockStream.h`
- Severity: wrong

## What the original does

The byte at +0x200 holds the server-side handshake state (`SECURITY_HS_*` bits). Neither the
constructor nor `ResetState`, which runs when a pooled socket is reused, sets it. Its value is
whatever the pool block or the previous connection left there.

## Effect

A fresh socket from a new pool block usually starts at 0 because the heap block happens to be
zeroed; a reused socket starts with the previous connection's state, so handshake checks and
log lines can behave differently for the same packet.

## Fix

Clear the byte in the constructor and in `ResetState`.
