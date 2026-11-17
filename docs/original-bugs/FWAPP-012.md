# FWAPP-012: file transfer error log passes a std::string object for "%s"

- Original: `ServerProcess_SendFileTo` (0x45a260), GatewayServer
- Rewrite: `ServerProcess_SendFileTo` in `JMX_ServerFramework/ServerFramework/ServerApp.cpp`
- Severity: crash

## What the original does

When the engine refuses to send a requested file, the path is logged with
`LogPrintf(0x2000000, "error : SendFileTo : %s", strPath)`: the 28-byte `std::string` object is
copied onto the stack (`rep movsd`, 7 dwords) instead of passing `c_str()`.

## Effect

`%s` takes the first dword of the object, which is the allocator slot of the string and not a
pointer to the text: the log shows garbage or the formatter faults in the main process. The
path is triggered by a client (peer server) requesting a file the engine cannot open.

## Fix

Pass `strPath.c_str()`.
