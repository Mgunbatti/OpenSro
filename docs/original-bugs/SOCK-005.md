# SOCK-005: a datagram without a message task is released twice

- Original: `SockDatagram_DispatchRecvMsg` 0x4744d0, `SockDatagram_OnRecvComplete` 0x4741f0
  (GatewayServer)
- Rewrite: `CSockDatagram::DispatchRecvMsg` in `JMX_Library/BSNet/SockDatagram.cpp`
- Severity: minor

## What the original does

When the datagram socket has no message task (`+0x98`), `DispatchRecvMsg` returns the received
message to the engine and returns 0x8013. `OnRecvComplete` treats the non-zero result as an
error and releases the same message (`+0x94`) a second time.

## Effect

A double release of a pooled message. `OnRecvComplete` already returns 0x8007 when the task is
missing, and the task only changes under the socket lock the completion holds, so the path is
not reachable in the current code.

## Fix

Let only one of the two functions release the message (clear `+0x94` in `DispatchRecvMsg`, or
do not release there).
