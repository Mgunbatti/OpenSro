# BSLIB-003: call stack cut at 520 characters

- Original: `Dbg_WalkStack`, GatewayServer 0x4751a0
- Rewrite: `WalkStack` in `JMX_Library/BSLib/BSDebug.cpp`
- Severity: minor

## What the original does

The output buffer is 4096 bytes (`szCallStack` in the assert dialog), but every frame is
appended with a 520-character limit (`StringCchCatA(buf, 520, frame)`). The 4096-byte budget is
reduced by the frame formatter's return value, which is always 0, so it never runs out.

## Effect

Only the first two or three frames reach the assert dialog; the rest are silently dropped.

## Fix

Append with the real buffer size and let the formatter return the length it wrote.
