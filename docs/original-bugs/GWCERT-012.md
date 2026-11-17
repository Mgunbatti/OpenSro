# GWCERT-012: certification handlers read the result row when the query returned no row

- Original: `Certify_TBUser` (0x414530), `Certify_TBUser_Ext` (0x4158f0), `Certify_TBUser_3`
  (0x415ac0), `Certify_TBUser_4` (0x415c80), `DB_CertifyTBUser_WithBlockGuide` (0x415e40),
  `DB_CertifyUser_WithBlockGuide` (0x416010), `Certify_CertifyUserWithKey` (0x414710),
  `DB_CertifyUser` (0x4161d0), `Certify_YahooKoreaUDB` (0x415010), `Certify_JoymaxCGI` (0x415360),
  GatewayServer
- Rewrite: same names in `JMX_CommonServers/GatewayServer/CertifyDB.cpp`
- Severity: minor

## What the original does

The handlers read the procedure's single result row into an uninitialised stack buffer and only
check the query result for an error (`< 0`). Result 1 ("no row") is treated like a row.

## Effect

When a procedure returns no row (for example after an error inside the procedure that does not
fail the batch), the handler decides on whatever the stack held: usually "wrong password", but a
leftover zero can certify the account with a random JID and security groups.

## Fix

Treat result 1 as a failure, or clear the row before the query.
