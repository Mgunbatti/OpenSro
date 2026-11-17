# SOCK-004: a failed TransmitFile disconnect is never detected

- Original: `Socket_DisconnectReuse` 0x46e2e0 (GatewayServer)
- Rewrite: `CSockStream::DisconnectReuse` in `JMX_Library/BSNet/SockStream.cpp`
- Severity: wrong

## What the original does

The disconnect of an accepted socket is issued with
`TransmitFile(socket, NULL, 0, 0, &overlapped, NULL, TF_DISCONNECT | TF_REUSE_SOCKET)`. The
result is compared with -1 (`SOCKET_ERROR`); only then is `WSAGetLastError` checked and the
socket closed. `TransmitFile` returns a `BOOL` (0 on failure), so the error branch is dead.

## Effect

When `TransmitFile` fails immediately, no completion arrives and the socket is not closed: the
session never sees the disconnect completion (reason 6) and the socket stays open until it is
reused or the process exits. The function returns 1 in this path, so
`CSockListener::OnIOCompletion`, which tests for 0, never takes its error branch either.

## Fix

Test `TransmitFile(...) == FALSE`, then `WSAGetLastError() != WSA_IO_PENDING`.
