# BSLIB-004: minidump file handle not checked

- Original: `Dbg_WriteMiniDump`, GatewayServer 0x474fa0
- Rewrite: `BSDebugWriteMiniDump` in `JMX_Library/BSLib/BSDebug.cpp`
- Severity: minor

## What the original does

`CreateFileA` is not checked; on failure `MiniDumpWriteDump` and `CloseHandle` get
`INVALID_HANDLE_VALUE`. The file name has one-second resolution plus `rand() % 10`, so two dumps
in the same second have a 1 in 10 chance of overwriting each other (`CREATE_ALWAYS`).
`rand()` is never seeded by this code.

## Effect

No dump and no error when the exe directory is read-only; occasional overwritten dumps.

## Fix

Check the handle, log the failure, and use a counter or milliseconds in the name.
