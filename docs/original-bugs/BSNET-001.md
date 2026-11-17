# BSNET-001: string writes clamp without the length prefix

- Original: `CMsg::WriteString` 0x40b6b0, `CMsg::WriteString_2` 0x443d70,
  `CMsg_WriteWStringClamped` 0x41b690 (GatewayServer)
- Rewrite: `CMsg::operator<<` for `const char*`, `std::string`, `const wchar_t*` in
  `JMX_Library/BSNet/Msg.cpp`
- Severity: wrong

## What the original does

Before writing a string it checks `len + writePos >= capacity` and, if so, shortens the string
to `capacity - writePos - 1`. That check ignores the 2-byte length word written first. Example
with the 4096-byte buffer: write position 4090, 10-byte string → clamped to 5, then 2 + 5 bytes
are written from 4090, which ends at 4097 and makes `WriteBytes` throw `(int)0`.

The wide-string version has the same mistake with `2 * len`.

## Effect

A string that should have been truncated near the end of the buffer throws instead; the
caller usually does not catch `int`, so the message is lost or the thread unwinds.

## Fix

Clamp against `capacity - writePos - 2` (and divide by 2 for wide strings), or reject the
string cleanly.
