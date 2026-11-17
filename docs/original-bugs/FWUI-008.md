# FWUI-008: the PERFMON log line is formatted into itself

- Original: `CServerMoniterView::SampleAndLogGraphs` 0x46a520 (GatewayServer)
- Rewrite: `CServerMoniterView::SampleAndLogGraphs` in
  `JMX_ServerFramework/ServerFramework/ServerMoniterView.cpp`
- Severity: minor

## What the original does

With "write perf log to text" set in Server.cfg, every graph is appended with
`StringCchPrintfA(buf, 1024, "%s, %s: %.2f", buf, name, value)`: the buffer is both destination
and argument.

## Effect

Overlapping source and destination is undefined for `vsnprintf`; it works with the static CRT
because the copy runs forward over the same bytes. The line always starts with ", "
("PERFMON >> , Msg: 5.00, ..."). Many graphs truncate the line silently at 1023 characters.

## Fix

Append with `StringCchCatA` (or format into a second buffer) and only add the separator between
entries.
