# SRVFW-001: crash-dump error file written through an invalid FILE*

- Original: `CrashDump_TopLevelExceptionFilter`, GatewayServer 0x452fd0
- Rewrite: not ported yet (ServerFramework)
- Severity: crash

## What the original does

The unhandled-exception filter writes `<dumpdir>\<ServiceName>_[date].dmp`. If that fails
(DBGHELP.DLL missing or too old, `CreateFileA` or `MiniDumpWriteDump` failing) it wants to write
the reason to a `.txt` file next to it, but the check is `if (fopen_s(&f, name, "w"))`, which
is true only when opening **failed**. Then `fprintf`/`fclose` run on the FILE* of the failed
open: NULL, or, if `fopen_s` left it alone, the `EXCEPTION_POINTERS*` the same variable held
earlier in the function.

## Effect

The .txt is never written when it could be, and when it cannot be opened the crash handler
itself crashes.

## Fix

`if (fopen_s(&f, name, "w") == 0)`, and do not reuse the variable.
