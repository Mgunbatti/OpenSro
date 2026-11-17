# SOCK-008: UDP ping receive errors are only reported when formatting fails

- Original: `CUdpPingServer::OnRecv` 0x47dcb0, `CUdpPingClient::OnRecv` 0x47e3e0 (GatewayServer)
- Rewrite: `UdpPing_ReportRecvError` in `JMX_Library/BSNet/UdpPing.cpp`
- Severity: minor

## What the original does

When `recvfrom` fails with anything but `WSAECONNRESET`, the thread formats
`"recvfrom failed! [err: %d]\n"` with `StringCchPrintfA` and passes the text to
`OutputDebugStringA` only if the call returned non-zero. `StringCchPrintfA` returns `S_OK` (0)
on success.

## Effect

The error is never shown; the receive thread just ends.

## Fix

Output the text when `SUCCEEDED(hr)`.
