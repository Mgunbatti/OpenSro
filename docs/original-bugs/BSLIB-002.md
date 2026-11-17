# BSLIB-002: undecorated symbol name thrown away

- Original: `Dbg_FormatStackFrame`, GatewayServer 0x4753b0
- Rewrite: `FormatStackFrame` in `JMX_Library/BSLib/BSDebug.cpp`
- Severity: minor

## What the original does

It calls `UnDecorateSymbolName` into a local buffer and then prints `pSym->Name`, the decorated
name, in both branches of a displacement check that does the same thing either way. The
function always returns 0, although its caller treats the result as the length used (see
BSLIB-003).

## Effect

Call stacks show mangled C++ names such as `?Read@CMsg@@...`.

## Fix

Print the undecorated buffer, drop the duplicate branch, return the length written.
