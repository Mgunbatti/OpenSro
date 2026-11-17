# GWCERT-005: Yahoo certification logs passwords in clear text

- Original: `Certify_YahooKorea` (0x414a90), `Certify_YahooKoreaUDB` (0x415010), GatewayServer
- Rewrite: same names in `JMX_CommonServers/GatewayServer/CertifyDB.cpp`
- Severity: medium (credential disclosure)

## What the original does

The certification of division 2 and 20 (Yahoo Korea) writes the password into the server log:

- "UrlEscapeError : %s, %s" with account name and password when escaping fails;
- "[Yahoo] Certification elapsed time is %d ms !! (%s, %s)" with the escaped account name and the
  escaped password after every successful request to the Yahoo login check;
- "there is no additional account info! [%s/%s]" with account name and password.

## Effect

Every login of these divisions leaves the account's password in the log window and log files.

## Fix

Leave the password out of the log lines.
