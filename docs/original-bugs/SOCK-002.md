# SOCK-002: socket buffer size helper halves the size after every success

- Original: `Socket_SetBufferSizeFallback` 0x494340 (GatewayServer; no callers)
- Rewrite: `Socket_SetBufferSizeFallback` in `JMX_Library/BSNet/SockSystem.cpp`
- Severity: minor

## What the original does

For a size above 1460 the helper loops: `setsockopt(SOL_SOCKET, option, size)`; when it
succeeds, the size is halved and the loop continues while the size is above 1460. Only a failing
`setsockopt` leaves the loop; then, unless the error is `WSAENOPROTOOPT` or `WSAEINVAL`, the
actual value is read back with `getsockopt` and returned.

## Effect

The intended "try the requested size, halve on failure" logic is inverted: a request that
succeeds is replaced by smaller and smaller sizes, ending with the smallest one above 1460
(8192 ends at 2048), and the function returns 0. The helper is not called in GatewayServer.

## Fix

Halve the size only after a failure and return the size that was accepted.
