# BSLIB-001: symbol search path built from the wrong condition

- Original: `Dbg_BuildSymbolSearchPath`, GatewayServer 0x474920 (same code in every module)
- Rewrite: `BuildSymbolSearchPath` in `JMX_Library/BSLib/BSDebug.cpp`
- Severity: minor

## What the original does

It appends `_NT_SYMBOL_PATH`, `_NT_ALT_SYMBOL_PATH` and `SYSTEMROOT` to the path only when
`_dupenv_s` returns non-zero, which means the call failed. On success (the normal case) the
variables are skipped and the `SYSTEMROOT` branch falls back to `GetWindowsDirectoryA`. The
strings returned by `_dupenv_s` are never freed.

The finished path is then not used at all: `SymInitialize` is called with a NULL search path.

## Effect

Symbols are looked up only in dbghelp's default locations. On the rare `_dupenv_s` failure the
code would append a NULL pointer. Small leak per call.

## Fix

Test `_dupenv_s(...) == 0 && pszEnv`, free each string, and pass the path to `SymInitialize`.
