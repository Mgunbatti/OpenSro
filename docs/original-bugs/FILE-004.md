# FILE-004: lower-casing reads past a trailing DBCS lead byte

- Original: `String_ToLowerMbcsSafe`, GatewayServer 0x43e3a0
- Rewrite: `StringToLowerMbcs` in `JMX_Library/BSLib/file/IFile.cpp`
- Severity: minor

## What the original does

Every byte with the high bit set is treated as a lead byte and copied together with the next
byte, without checking that the next byte is not the terminating NUL. If the string ends with
such a byte, the NUL is appended to the result and the loop continues with the memory behind
the string.

## Effect

A malformed path (e.g. cut in the middle of a Korean character) produces a lower-case copy with
an embedded NUL followed by whatever bytes follow the source buffer, up to the next zero byte.
It can read beyond the allocation.

## Fix

Stop when the byte after a lead byte is NUL (or use `CharLowerBuffA` / `_mbslwr_s`).
