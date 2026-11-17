# GWCERT-006: Yahoo login check reads up to 1024 bytes into a 256-byte buffer

- Original: `Certify_YahooKorea` (0x414a90), GatewayServer
- Rewrite: `Certify_YahooKorea` in `JMX_CommonServers/GatewayServer/CertifyDB.cpp` (the stack
  frame is kept as one buffer with the original offsets)
- Severity: minor

## What the original does

The frame holds the answer buffer (256 bytes), the escaped password (256 bytes before the URL),
the URL (1024 bytes) and the escaped account name (1024 bytes), in this order.
`Http_GetUrlContent` is called with the answer buffer and a limit of 1024 bytes. The escaped
password is also written with a limit of 1024 characters into its 256 bytes.

## Effect

An answer of more than 255 bytes overwrites the escaped password and the beginning of the URL,
which the following log line prints; a long escaped password overwrites the URL before it is
formatted from it. Everything stays inside the function's frame, so nothing crashes, but the log
line and, for long passwords, the request are wrong.

## Fix

Pass the real buffer sizes.
