# SOCK-001: mswsock.dll is freed after taking function pointers from it

- Original: `Winsock_LoadAcceptExFuncs` 0x494050 (GatewayServer)
- Rewrite: `Winsock_LoadAcceptExFuncs` in `JMX_Library/BSNet/SockSystem.cpp`
- Severity: minor

## What the original does

`AcceptEx`, `GetAcceptExSockaddrs` and `TransmitFile` are looked up with
`SIO_GET_EXTENSION_FUNCTION_POINTER`. When that ioctl fails for `AcceptEx`, the function loads
`mswsock.dll` and takes the function from it with `GetProcAddress` (the other two fall back to the
same module). At the end it calls `FreeLibrary` on the module and keeps the pointers in globals.

## Effect

If the `LoadLibraryA` reference was the only one, the DLL is unloaded and the stored pointers
dangle; the first `AcceptEx`/`TransmitFile` call crashes. In practice Winsock keeps
`mswsock.dll` loaded, so the fallback path works by accident.

## Fix

Keep the module loaded for as long as the pointers are used (free it at Winsock cleanup).
