# GWCERT-009: Joymax login fail notice repeats the login request when parsing fails

- Original: `Certify_JoymaxCGI` (0x415360), GatewayServer
- Rewrite: `Certify_JoymaxCGI` in `JMX_CommonServers/GatewayServer/CertifyDB.cpp`
- Severity: minor

## What the original does

After a successful web login (checkUser.cgi) whose account is refused by
`_CertifyTB_User_Portal`, the server tells the portal with LoginFail.cgi?jid=<jid>, the jid being
the second '|' field of the first answer. When that field cannot be found ("Fail to parsing at
result of Login CGI") the URL buffer is not rebuilt, but the request is sent anyway.

## Effect

The portal receives the checkUser.cgi request (with account name, password hash and client
address) a second time instead of a fail notice.

## Fix

Skip the notice when the jid cannot be parsed.
