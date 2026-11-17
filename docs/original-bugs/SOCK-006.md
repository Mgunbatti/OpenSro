# SOCK-006: UDP ping packets carry four uninitialised bytes

- Original: `UdpPing_SendReq` 0x47d7e0, `CUdpPingServer::SendAck` 0x47d840 (GatewayServer)
- Rewrite: `UdpPing_SendReq` / `UdpPing_SendAck` in `JMX_Library/BSNet/UdpPing.cpp`
- Severity: minor

## What the original does

The 16-byte packet is built on the stack: bytes 0..3 are `"REQ"`/`"ACK"` and a terminator,
bytes 8..15 the two timestamp DWORDs. Bytes 4..7 are never written and are sent with whatever
the stack held.

## Effect

Four bytes of stack memory of the server process go to any address that asks for a ping reply.
The receiver does not use them.

## Fix

Zero the packet before filling it.
