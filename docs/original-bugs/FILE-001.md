# FILE-001: CFileMem::Open replaces the buffer on every open

- Original: `CFileMem::Open`, GatewayServer 0x489750
- Rewrite: `CFileMem::Open` in `JMX_Library/BSLib/file/FileMem.cpp`
- Severity: minor

## What the original does

After the base checks in `IFile::Open` pass, the function sets the size to the `dwSize`
argument and allocates a new buffer with `new BYTE[dwSize]` whenever the context is non-zero.
It does not look at whether the file is already open, so a second open of a shared memory file
(read mode, another context) overwrites `m_pBuffer` and `m_dwSize`.

## Effect

The previous buffer leaks and its contents are lost. A read-mode open passes `dwSize == 0`, so
the file is left with a zero-byte buffer and every later read returns nothing. Context 0 never
gets a buffer.

## Fix

Allocate only when the file is not open yet (`!IsOpen()`), as `CFileLegacy` and `CFileMM` do
with their handles.
