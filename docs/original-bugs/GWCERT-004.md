# GWCERT-004: the reserve images of the IBUV start-up are never made

- Original: `CIBUVProcess::Initialize` (0x4196b0), `IBUV_GenerateAndQueueImage` (0x419a30),
  GatewayServer
- Rewrite: `CIBUVProcess::Initialize` / `GenerateAndQueueImage` in
  `JMX_CommonServers/GatewayServer/IBUVProcess.cpp`
- Severity: minor

## What the original does

After loading ImageTrans.dll and the character set, `Initialize` calls
`IBUV_GenerateAndQueueImage` 20000 times (a constant, not the configured
`IBUVQueueReserveCount`) and logs "IBUV: Preserved image created : (dwImageCount)" (the variable
name, not a count). `IBUV_GenerateAndQueueImage` returns at once while the process is not enabled,
and `Initialize` enables it only after the loop.

## Effect

The queue is empty when the server starts accepting logins. The IBUV thread refills it in steps
of `ratio * (reserve - queued)` images every 100 ms, and requests that find it empty make an image
on demand, so captchas still work, but the start-up log claims a reserve that does not exist.

## Fix

Enable the process before the loop and use `m_dwQueueReserveCount` as the count.
