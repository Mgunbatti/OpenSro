# GWCERT-008: Yahoo age check takes its last field from the wrong string

- Original: `Certify_YahooAgeCheck` (0x414da0), GatewayServer
- Rewrite: `Certify_YahooAgeCheck` in `JMX_CommonServers/GatewayServer/CertifyDB.cpp`
- Severity: minor

## What the original does

The answer of the age check is split at the delimiter 0x05: the first field becomes the record's
name, the third its age, the fourth the value at +0x20. The last value (+0x24) is meant to be the
last character of the fourth field, but the code reads the remaining text after the fourth field
at the index `field.size() - 1` (and only when the field is not longer than that rest).

## Effect

The +0x24 value is an arbitrary character of the rest of the answer, or the check fails (result 3,
the login is refused as a server error) when the rest is shorter than the fourth field. The value
is not used by the login hand-over, so only the failure case matters.

## Fix

Read `field[field.size() - 1]`.
