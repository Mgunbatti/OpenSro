# FWAPP-003: private address test compares bit subsets instead of prefixes

- Original: `Net_IsPrivateIPv4` (0x41d630), used by `Net_GetPublicIPv4Address` (0x41d670), GatewayServer
- Rewrite: `Net_IsPrivateIPv4` in `JMX_ServerFramework/ServerFramework/ServerApp.cpp`
- Severity: minor

## What the original does

An address (network byte order) counts as private when `ip == (ip & mask)` for one of the masks
0xFFFFFF0A, 0xFFFF1FAC and 0xFFFFA8C0, i.e. when the bits of its first one or two bytes are a
subset of 10, 172.31 or 192.168.

## Effect

Addresses such as 0.x, 2.x, 8.x (subsets of 10), 128.x, 160.x (subsets of 192) or 172.0.x to
172.15.x count as private, while 172.16.x to 172.31.x are only partly recognised. The startup
then takes such a public address for private and keeps looking, and on a machine with only such
addresses reports the last one as public IP.

## Fix

Compare prefixes: `(ip & 0xFF) == 10`, `(ip & 0xF0FF) == 0x10AC`, `(ip & 0xFFFF) == 0xA8C0`.
