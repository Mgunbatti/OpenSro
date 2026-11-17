# FILE-002: CFileMM opens files for reading with OPEN_ALWAYS

- Original: `CFileMM::Open`, GatewayServer 0x48a130
- Rewrite: `CFileMM::Open` in `JMX_Library/BSLib/file/FileMM.cpp`
- Severity: minor

## What the original does

`CreateFileA` is called with `OPEN_ALWAYS` in both modes. In read mode a missing file is
therefore created (zero bytes); `GetFileSize` then returns 0 and the function fails with -3.
The same and the -4 / -5 failures return without closing the file or mapping handle.

## Effect

Asking for a file that does not exist leaves an empty file on disk and reports "empty file"
(-3) instead of "cannot open" (-13). The handles stay open until the object is closed or
deleted; `CFileSystem::OpenFile` deletes newly created objects on failure, so they do not leak
there.

## Fix

Use `OPEN_EXISTING` in read mode and close the handles on every failure path.
