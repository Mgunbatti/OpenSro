# SRVFW-003: peeking the massive opcode can consume a byte

- Original: `CMsg_PeekHeaderWords`, GatewayServer 0x41fcc0
- Rewrite: `GetMassiveOpcode` in `JMX_ServerFramework/ServerFramework/MassiveMsg.cpp`
- Severity: minor

## What the original does

It saves the read cursor, reads the type byte of a 0x600D packet and restores the cursor at the
end. Two paths return early without restoring it: a header packet (type 1) when a massive
message is passed in, and a part (type != 1) when none is passed in. In both cases the cursor
stays one byte further.

## Effect

The caller sees the packet without its first byte if it reads it afterwards.

## Fix

Restore the cursor on every return path.
