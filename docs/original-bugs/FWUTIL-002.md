# FWUTIL-002: Server.cfg tokenizer copies tokens without a size limit

- Original: `Cfg_ReadNextToken` (0x453d40) called by `ServerConfig_Load` (0x41d910), GatewayServer
- Rewrite: `Cfg_ReadNextToken` in `JMX_ServerFramework/ServerFramework/ServerConfig.cpp`
- Severity: crash

## What the original does

`Cfg_ReadNextToken(text, token)` copies a quoted string or a run of non-separator characters
into `token` byte by byte until the closing quote or the next separator. It is not told the
size of the destination; `ServerConfig_Load` passes a stack buffer of about 1 KB.

## Effect

A quoted value or word in Server.cfg longer than the buffer overwrites the stack of
`ServerConfig_Load` (the /GS cookie then terminates the process at the latest when the function
returns).

## Fix

Pass the buffer size and stop copying (or fail) when the token does not fit.
