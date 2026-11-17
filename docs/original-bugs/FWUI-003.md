# FWUI-003: graph name copied with a 256 character limit into 64 bytes

- Original: `CMoniterGraphData::Init` 0x469b70, `CServerMoniterView::ctor` 0x469e50
  (GatewayServer)
- Rewrite: `CMoniterGraphData::Init`, `CServerMoniterView::CServerMoniterView` in
  `JMX_ServerFramework/ServerFramework/ServerMoniterView.cpp`
- Severity: crash

## What the original does

The graph name is copied with the inlined `StringCchCopyA(dst, 256, name)` into the 64-byte
name field at +4 of `CMoniterGraphData` (the constructor clears only 0x40 bytes). The view
constructor copies "value" and "time" the same way into its 64-byte label fields.

## Effect

A counter name of 64 characters or more overwrites the sample list (+68), the current value,
minimum, maximum, the counter pointer and the colour of the graph object, and with 104 or more
characters the heap block after it. The sample list header is then garbage: the next sample or
the destruction of the graph crashes. The constant labels are short and harmless.

## Fix

Copy with `sizeof(m_szName)` (64) as the limit.
