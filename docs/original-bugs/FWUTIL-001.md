# FWUTIL-001: Server.cfg certification addresses copied with a 256-character limit into 16 bytes

- Original: `ServerConfig_Load` (0x41d910), GatewayServer
- Rewrite: `ServerConfig_Load` / `ReadOption` in `JMX_ServerFramework/ServerFramework/ServerConfig.cpp`
- Severity: crash

## What the original does

The value of `certification` is copied to the global at 0x53B668 and the value of
`certification_ip_bind` to 0x53B678 with an inlined `StringCchCopyA(dst, 256, token)`. Both
globals are 16-byte fields of the server globals block: the bind address follows the
certification address directly, then the certification port, the main thread ID, the debug
option words, the other Server.cfg options and the map of extra keys.

## Effect

An address of 16 or more characters (a host name, a typo) overwrites the following fields
without any error: the bind address, the port, the debug flags. A value of more than about 80
characters reaches the `std::map` of extra keys and the process crashes when the map is used or
destroyed. Values up to 255 characters are accepted silently.

## Fix

Use `sizeof` of the destination (16) as the limit and report a value that does not fit.
