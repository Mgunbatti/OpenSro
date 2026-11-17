# FWUI-006: rebuilding the architecture view keeps old entries and duplicates children

- Original: `CServerArchitectureView::RebuildTopology` 0x4561f0 (GatewayServer)
- Rewrite: `CServerArchitectureView::RebuildTopology` in
  `JMX_ServerFramework/ServerFramework/ServerArchitectureView.cpp`
- Severity: wrong

## What the original does

Each `WM_SERVER_ARCH_LOADED` (0x7E8) inserts one entry per body and one per cord with
`insert`, which keeps an existing entry, so the maps are never cleared. Then for every entry it
appends every entry whose parent ID matches to the entry's child list, again without clearing
the list.

## Effect

After a second architecture message (a new 0xA003 from the certification server) every child
appears twice in its parent's list (three times after the third one, ...), so the tree layout
places each subtree several times, the last placement winning, and the gaps grow. Bodies or
cords that were removed stay in the view with pointers to freed records; drawing them reads
freed memory.

## Fix

Clear both maps (or at least the child lists and parent pointers) before rebuilding.
