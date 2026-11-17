# FWUI-007: colour codes below '0' read before the colour table

- Original: `Gdi_TextOutColoredf` 0x464340 (GatewayServer)
- Rewrite: `Gdi_TextOutColoredf` in `JMX_ServerFramework/ServerFramework/GdiUtil.cpp`
- Severity: minor

## What the original does

A "^" followed by any character switches the colour to `(c - '0') % 10`, a signed modulo, and
uses it as index into the 10-entry table at 0x4F8140. Characters below '0' give a negative
index. A "^" at the end of the text also skips the terminating zero.

## Effect

"^" followed by a space, '!', '+' and so on takes text colour, background colour and background
mode from the bytes before the table. A trailing "^" makes the loop read past the end of the
formatted text. The function has no callers in GatewayServer.

## Fix

Only treat "^<digit>" as a colour code, like `Clipboard_SetTextStripColorCodes` does.
