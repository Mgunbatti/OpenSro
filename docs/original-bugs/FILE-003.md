# FILE-003: IFile::Read logs the block limit as the requested size

- Original: `IFile::Read`, GatewayServer 0x4892b0
- Rewrite: `IFile::Read` in `JMX_Library/BSLib/file/IFile.cpp`
- Severity: cosmetic

## What the original does

When the context's position is at the end of the file it logs
`IFile::Read() - RemainBytes == 0 (<requested byte count>: %d)` with the constant 4090.
`IFile::Read` has no size argument; 4090 is the largest block it reads.

## Effect

The log always says 4090 bytes were requested, which says nothing about the caller.

## Fix

Log the position and the file size instead.
