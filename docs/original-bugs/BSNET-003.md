# BSNET-003: string reads do nothing in pop-back mode

- Original: `CMsg::ReadString` 0x40b730, `CMsg_ReadWString` 0x40b430 (GatewayServer)
- Rewrite: `CMsg::operator>>(std::string&)` / `(std::wstring&)` in `JMX_Library/BSNet/Msg.cpp`
- Severity: wrong

## What the original does

When the message is in pop-back mode (`m_bReadForward == 0`) the string readers call
`AssertFail` and return without reading. With the release flags `AssertFail` does nothing, so
the output string keeps its previous content and the read cursor does not move.

## Effect

A handler that reads a string from such a message continues with stale data.

## Fix

Either support pop-back reads for strings or fail the message explicitly.
