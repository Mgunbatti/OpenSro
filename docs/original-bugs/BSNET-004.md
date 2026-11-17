# BSNET-004: constructor reads an uninitialised pointer

- Original: `CMsg::InitFields` (the constructor) 0x47f7b0 (GatewayServer)
- Rewrite: `CMsg::CMsg` in `JMX_Library/BSNet/Msg.cpp` (not reproduced, see below)
- Severity: minor

## What the original does

It copies `m_pData` into `m_WsaBuf.buf` before `m_pData` is set. `AttachBuffer` overwrites
`m_WsaBuf.buf` a few instructions later, so the garbage value is never used.

## Effect

None observable. Reading an indeterminate value is undefined behaviour, so the rewrite sets the
fields in a safe order instead of copying it.

## Fix

Already done in the rewrite.
