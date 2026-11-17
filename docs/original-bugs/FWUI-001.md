# FWUI-001: withdrawn (popup menus do use TPM_RETURNCMD)

- Original: `CServerFrameWindow::OnLogListViewNotify` 0x45c6b0,
  `ArchitectureView_ShowContextMenu` 0x457170, `PopupMenu_TrackAtCursor` 0x465c50,
  `CServerMoniterView::OnRButtonContextMenu` 0x46b100 (GatewayServer)
- Rewrite: `CServerFrameWindow::OnNotify`, `CServerArchitectureView::OnRButtonUp`,
  `CWin32Control_PopupMenu::TrackAtCursor`, `CServerMoniterView::OnRButtonUp` in
  `JMX_ServerFramework/ServerFramework/`
- Severity: none (not a bug)

## What the original does

This entry said that the popup menus are shown with `TPM_RIGHTBUTTON` and without
`TPM_RETURNCMD`. That was a misreading: every call pushes `0x100` as `uFlags`, which is
`TPM_RETURNCMD` alone (`TPM_RIGHTBUTTON` is `0x0002`). `TrackPopupMenu` therefore returns the
chosen command ID (0 when the menu is dismissed) and the switch on the result works as intended.

The same flag is used at every `TrackPopupMenu` call in GatewayServer (0x4572a1, 0x45c84d,
0x465c76, 0x46b2e1), FarmManager (0x443301, 0x446fae, 0x450136, 0x455591) and AgentServer
(0x43cef1, 0x457c76, 0x45a0b6, 0x45f721).

## Effect

None. The log list items (save and clear, save, clear, copy), the architecture view items
(view toggles, rearrange) and the monitor view items (legend, show/hide all, per-graph toggle)
all run their command.

## Fix

Nothing to fix. The rewrite passes `TPM_RETURNCMD` like the original. The ID is kept so it is
not reused.
