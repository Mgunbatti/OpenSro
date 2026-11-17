# BSNET-002: reading past the end of a packet crashes the process

- Original: `CMsg::ReadByte` 0x40a3a0, `ReadWord` 0x40a470, `ReadDword` 0x40a3f0,
  `ReadQword` 0x4731b0, `Read12Bytes` 0x473690, `ReadBlock20` 0x4735b0 and the pop-back
  readers 0x40a9c0, 0x40ab50, 0x40aa80, 0x473530, 0x4736e0, 0x473610 (GatewayServer)
- Rewrite: `CMsg::Read<T>` / `CMsg::PopBack<T>` in `JMX_Library/BSNet/Msg.h` (`BS_CRASH()`)
- Severity: crash

## What the original does

The fixed-size readers check that enough bytes are left and, if not, write 2000 to address 0.
The access violation is not caught anywhere in the message path. `ReadBytes` and the string
readers throw `(int)0` instead.

## Effect

A packet that is shorter than its handler expects takes the whole server process down.

## Fix

Report the error to the caller (exception or failed state on the message) and drop the
session that sent it.
