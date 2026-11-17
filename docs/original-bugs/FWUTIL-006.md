# FWUTIL-006: CDateTime getters dereference NULL for times localtime rejects

- Original: the component getters of the BSLib date/time class (0x4673e0 ... 0x467440, year,
  month, day, hour, minute, second, day of week), GatewayServer
- Rewrite: `CDateTime::GetYear` ... `CDateTime::GetDayOfWeek` in `JMX_Library/BSLib/Util/TimeSpan.cpp`
- Severity: crash

## What the original does

Each getter calls the local-time conversion (0x467500), which returns NULL when
`_localtime32_s` fails, and reads the field from the result without checking it.
`GetAsSystemTime` (0x467740) does check.

## Effect

A time value `_localtime32_s` cannot convert (negative, i.e. before 1970 or the result of a
failed `mktime` = -1) crashes the caller with an access violation.

## Fix

Check the result and return a defined value (or assert) as `GetAsSystemTime` does.
