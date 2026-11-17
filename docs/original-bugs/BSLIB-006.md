# BSLIB-006: long assert messages break into the debugger silently

- Original: `Dbg_AssertDialog`, GatewayServer 0x475620
- Rewrite: `BSAssertDialog` in `JMX_Library/BSLib/BSDebug.cpp`
- Severity: wrong

## What the original does

The formatted expression goes into a 1024-byte buffer with `_vsnprintf_s(buf, 0x400, 0x3FF)`.
On truncation the function returns -1, which the unsigned check `>= 0x400` treats as an error,
so the dialog returns false before showing anything or calling the callback. Callers then run
`__debugbreak()`. Every later formatting failure (`StringCch*` errors) also returns false.

In a release build without a debugger this ends the process with a breakpoint exception
instead of the configured dialog.

## Effect

An assert with a long message crashes instead of reporting.

## Fix

Use `_TRUNCATE` and keep going with the shortened text; treat formatting failures as
"continue".
