# GWCERT-001: certification handler table has invalid entries for divisions 0 and 27

- Original: `UserCertifier_GetHandler` (0x4144f0) and its table at 0x4F8224, GatewayServer
- Rewrite: `UserCertifier_GetHandler` / `s_apfnCertifyHandler` in `JMX_CommonServers/GatewayServer/CertifyDB.cpp`
- Severity: minor (configuration dependent crash)

## What the original does

The certifier processes pick the certification handler by the division ID of this server
(`g_pSelfBody->byDivisionID`). IDs above 36 are refused with "There is no certification
handler!!!", every other ID jumps through a 37-entry table. Entry 0 holds the value 0x400 (not a
function) and entry 27 holds 0.

## Effect

In a GatewayServer of division 0 or 27 every login faults with an access violation on the
certifier thread. The certifier process's structured exception handler writes a crash dump and
the job comes back as failed with the result of its previous use, so the client gets a random
0xA102 failure.

## Fix

Fill the two entries with a handler or with a function that returns `CERTIFY_RESULT_ERROR`, and
check the table entry before calling it.
