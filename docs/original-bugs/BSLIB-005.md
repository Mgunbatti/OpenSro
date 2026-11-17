# BSLIB-005: dbghelp.dll reloaded after every failure

- Original: `Dbg_LoadDbgHelp`, GatewayServer 0x474d10
- Rewrite: `BSDebugLoadDbgHelp` in `JMX_Library/BSLib/BSDebug.cpp`
- Severity: minor

## What the original does

Only success is cached. When a symbol is missing or `SymInitialize` fails, the library is freed
but the global module handle keeps the old value, and the next assert loads it again. The
search path from BSLIB-001 is built on every successful load and then ignored.

## Effect

Repeated `LoadLibrary`/`FreeLibrary` on every assert on systems with an old dbghelp.dll.

## Fix

Cache the failure too, and clear the handle after `FreeLibrary`.
