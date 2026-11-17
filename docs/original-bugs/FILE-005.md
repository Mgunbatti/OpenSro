# FILE-005: cache purge subtracts files that were never counted

- Original: `CFileSystem::PurgeUnusedFileEntries` (0x48aec0) and `CFileSystem::RegisterOpenFile`
  (0x48ad60), GatewayServer
- Rewrite: same names in `JMX_Library/BSLib/file/FileSystem.cpp`
- Severity: minor

## What the original does

Only memory-mapped files opened for reading add their size to the cache usage
(`m_dwCacheUsed`). With a cache limit set, `CloseFile` keeps every read-mode file whose last
user left, whatever its type. When a new mapped file pushes the usage over the limit,
`PurgeUnusedFileEntries` deletes the first idle file it finds in the open-file map and returns
its size, which the caller subtracts from the usage. That file can be a `CFileLegacy` or
`CFileMem` whose size was never added. The result is clamped at 0.

## Effect

The usage is under-counted after such a purge, so later mapped files can exceed the limit.
Idle legacy and memory files also stay open (and keep their handles) until a mapped file
happens to purge them, because they are not in the name map and cannot be reused.

## Fix

Keep only mapped read-mode files cached (release the others on their last close) and let the
purge consider only those.
