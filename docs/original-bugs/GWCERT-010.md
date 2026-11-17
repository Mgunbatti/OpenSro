# GWCERT-010: captcha image record is lost when drawing fails

- Original: `IBUV_GenerateAndQueueImage` (0x419a30), GatewayServer
- Rewrite: `CIBUVProcess::GenerateAndQueueImage` in `JMX_CommonServers/GatewayServer/IBUVProcess.cpp`
- Severity: minor

## What the original does

The function takes an `IBUVData` from its pool (marked as in use), fills the code and calls
`IBUV_CreateImage`. When ImageTrans.dll returns 0 or more than 0x800 bytes it returns 0 without
giving the record back.

## Effect

Every failed image costs one 0x844-byte record of the pool. With a failing ImageTrans.dll the
refill loop of the IBUV thread fails continuously and the pool grows by 500 records at a time.

## Fix

Call `pData->Release()` before returning on failure.
