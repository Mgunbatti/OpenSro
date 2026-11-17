# SOCK-007: UDP ping host names that do not resolve crash the process

- Original: `CUdpPingServer::Open` 0x47db30, `SockAddr_FromHostPort` 0x47d910 (GatewayServer)
- Rewrite: `UdpPing_ResolveHost` in `JMX_Library/BSNet/UdpPing.cpp`
- Severity: crash

## What the original does

The host string is converted with `inet_addr`; when that fails, the result of
`gethostbyname(name)` is dereferenced (`h_addr_list[0]`) without checking for NULL.

## Effect

A configured ping host name that does not resolve (typo, DNS down) crashes the server at start
(`CUdpPingServer::Open`) or when a client target is added.

## Fix

Check the `gethostbyname` result (and `h_length == 4`), as `ResolveHostIPv4` does.
