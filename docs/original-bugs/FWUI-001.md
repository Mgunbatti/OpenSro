# FWUI-001: popup menus are shown without TPM_RETURNCMD

- Original: `CServerFrameWindow::OnLogListViewNotify` 0x45c6b0,
  `ArchitectureView_ShowContextMenu` 0x457170, `CServerMoniterView::OnRButtonContextMenu`
  0x46b100 (GatewayServer)
- Rewrite: `CServerFrameWindow::OnNotify`, `CServerArchitectureView::OnRButtonUp`,
  `CServerMoniterView::OnRButtonUp` in `JMX_ServerFramework/ServerFramework/`
- Severity: wrong

## What the original does

All three context menus call `TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, ...)` and then switch on
the return value as if it were the chosen command ID. Without `TPM_RETURNCMD` the function
returns nonzero on success (1) and zero on failure, and posts the chosen command as
`WM_COMMAND` to the owner window. None of the owners (the log list view, the architecture view,
the monitor view) has a handler for these IDs.

## Effect

- Log list: "Save && Clear", "Save", "Clear" and "Copy SelectedItem To Clipboard" never do
  anything.
- Architecture view: every menu that is shown (also when it is dismissed) toggles "Show Server
  Module Name" (return value 1 = that item's ID); the other items, "Rearrange Items" and
  "Property" do nothing.
- Monitor view: no item has an effect (1 is "Start Moniter", which is a no-op anyway).

## Fix

Pass `TPM_RIGHTBUTTON | TPM_RETURNCMD` (or handle the commands in the owner windows).
