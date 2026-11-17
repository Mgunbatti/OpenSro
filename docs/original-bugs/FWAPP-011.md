# FWAPP-011: patch file path formatted without its module directory

- Original: `PatchFile_BuildFullPath` (0x427530, not referenced), GatewayServer
- Rewrite: `PatchFile_BuildFullPath` in `JMX_ServerFramework/ServerFramework/ServerApp.cpp`
- Severity: minor

## What the original does

The path of a patch file record is built as module directory, optional sub directory, `\`, then
the `<module>_<x>` directory is formatted with `"%d_%d"` into a local buffer, followed by the
file's own path and name. The formatted directory is never appended; the `\` before it is.

## Effect

The function returns `<dir>\<sub>\\<path>\<name>` (double backslash, directory missing), which
does not match where `ServerProcess_SendFileTo` looks for the same file. The function is not
called in GatewayServer.

## Fix

Append the formatted directory after the separator.
