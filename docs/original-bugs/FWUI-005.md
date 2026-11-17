# FWUI-005: cord status indexes the style table without a range check

- Original: `CServerArchitectureView::DrawLinks` 0x455fc0 (GatewayServer)
- Rewrite: `CServerArchitectureView::DrawLinks` in
  `JMX_ServerFramework/ServerFramework/ServerArchitectureView.cpp`
- Severity: wrong

## What the original does

The colour, pen style and width of a cord line come from a 5-entry table (0x4F88F0) indexed by
the cord status (`SERVER_CORD` +9), which is received from other servers. There is no check
of the index. The same is true for the node icon, chosen by `SERVER_BODY::nState` from the 9
loaded icons (`DrawNode`).

## Effect

A status above 4 (or a state above 8) reads the data behind the table: random colours and pen
styles, a huge pen width, or an invalid icon handle. Values far out of range can read
unmapped memory and crash the window thread.

## Fix

Clamp or validate the status and state before indexing.
