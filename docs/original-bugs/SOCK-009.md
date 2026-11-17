# SOCK-009: removing a UDP ping target is inverted

- Original: `CUdpPingClient::RemoveTarget` 0x47e130 (GatewayServer)
- Rewrite: `CUdpPingClient::RemoveTarget` in `JMX_Library/BSNet/UdpPing.cpp`
- Severity: wrong

## What the original does

The target ID is looked up in the target map; the node is erased only when the lookup returned
`end()`.

## Effect

An existing target is never removed. Removing an unknown ID calls `erase(end())`, which throws
`std::out_of_range("invalid map/set<T> iterator")` and, uncaught, ends the process. The client
class is not used by GatewayServer.

## Fix

Erase when the lookup found the ID.
