# FWAPP-002: crash dump error file written next to the module directory

- Original: `CrashDump_TopLevelExceptionFilter` (0x452fd0), GatewayServer
- Rewrite: `CrashDump_TopLevelExceptionFilter` in `JMX_ServerFramework/ServerFramework/CrashDump.cpp`
- Severity: minor

## What the original does

The dump file name is built as `<module dir>` + `\` + `<service>` + `_[date time]` + `.dmp`. The
name of the text file that should explain a failed dump is built from the same parts, but the
`\` after the module directory is missing.

## Effect

The text file goes to the parent directory with the module directory's name as prefix, for
example `D:\Server\GatewayServerGatewayServer_[...].txt` instead of
`D:\Server\GatewayServer\GatewayServer_[...].txt` (see also SRVFW-001: it is written only in the
case where it cannot be opened).

## Fix

Append `\` after the module directory as for the dump file.
