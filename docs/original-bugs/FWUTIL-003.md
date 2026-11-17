# FWUTIL-003: Server.cfg text buffer is never freed

- Original: `ServerConfig_Load` (0x41d910), GatewayServer
- Rewrite: `ServerConfig_Load` in `JMX_ServerFramework/ServerFramework/ServerConfig.cpp`
- Severity: minor

## What the original does

The whole file is read into `new char[size + 1]`, tokenized, and the function returns on every
path without deleting the buffer.

## Effect

The size of Server.cfg is leaked once per load (once per process start in GatewayServer).

## Fix

`delete[]` the buffer before every return.
