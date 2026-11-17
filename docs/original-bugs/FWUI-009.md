# FWUI-009: counters of type 0 have no type name

- Original: `MoniterView_GetTypeNameByHiByte` 0x469a10, `CServerMoniterView::DrawLegend`
  0x46ab30 (GatewayServer)
- Rewrite: `MoniterView_GetTypeNameByHiByte`, `CServerMoniterView::DrawLegend` in
  `JMX_ServerFramework/ServerFramework/ServerMoniterView.cpp`
- Severity: crash

## What the original does

The counter kind is the high byte of the counter ID; the name table at 0x4F8AD8 has NULL for
kind 0 ("OSModule", "NetEngine", "ServerBody" for 1 to 3, "Unknown" above). The legend passes
the name to `strlen` and `GetTextExtentPoint32A`, the popup menu to `"Show %s %s"`.

## Effect

A graph whose ID is below 0x01000000 crashes the window thread at the next paint with the
legend shown (`strlen(NULL)`). GatewayServer only registers IDs 0x2010001..0x3010002.

## Fix

Give kind 0 a name (or map it to "Unknown").
