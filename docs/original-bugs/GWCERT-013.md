# GWCERT-013: "#$" accounts of the Yahoo certification put the password into the SQL text

- Original: `Certify_YahooKoreaUDB` (0x415010), GatewayServer
- Rewrite: `Certify_YahooKoreaUDB` in `JMX_CommonServers/GatewayServer/CertifyDB.cpp`
- Severity: high (SQL injection)

## What the original does

The login request handler (0x6102) replaces quotes in the account name only (GW-005). The other
handlers send the password as MD5 hex, but for account names containing "#$" the Yahoo handler
formats `exec _CertifyUser_SU '%s','%s'` with the password as typed.

## Effect

A password with a quote ends the string literal; the rest of the password is executed as SQL on
the account database with the rights of the gateway's database login.

## Fix

Escape the password (or use a parameterised call).
