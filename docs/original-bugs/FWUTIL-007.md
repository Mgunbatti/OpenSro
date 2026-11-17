# FWUTIL-007: local smalldatetime ignores daylight saving time

- Original: `GetLocalTimeAsSmallDateTime` (0x47b660), GatewayServer
- Rewrite: `GetLocalTimeAsSmallDateTime` in `JMX_Library/BSLib/Util/DateTimeUtil.cpp`
- Severity: wrong

## What the original does

The current time is turned into local time by subtracting the CRT `_timezone` value, which is
the standard-time bias only. The daylight saving offset (`_dstbias`, `_daylight`) is not
applied.

## Effect

While daylight saving time is in effect the value is one hour (in most zones) behind the local
clock.

## Fix

Use `_localtime32_s` (or `GetLocalTime`) to get the local time and build the value from it.
