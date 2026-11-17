# FWUI-002: the monitor view is deleted while its window still exists

- Original: `CServerMoniterView::DestroyGdiAndGraphs` 0x46a1d0 (vtable slot 2),
  `CServerFrameWindow::OnDestroyCleanup` 0x45c4f0 (GatewayServer)
- Rewrite: `CServerMoniterView::Destroy`, `CServerFrameWindow::Destroy` in
  `JMX_ServerFramework/ServerFramework/`
- Severity: crash

## What the original does

The server window's cleanup calls slot 2 (`Destroy`) of every view and deletes it, then destroys
its own window. The architecture view's `Destroy` ends with `CWindowBase::Destroy`
(DestroyWindow and removal from the handle -> object table). The monitor view's `Destroy` only
frees its DCs, bitmaps and graphs: the window stays alive (with its 100 timer) and stays in the
table.

## Effect

When the server window is then destroyed, Windows destroys the monitor view's window as a child
and sends it `WM_DESTROY`. `CWindowBase::StaticWndProc` finds the deleted object in the table
and calls its virtual `WndProc`: use after free. It usually goes unnoticed because the memory is
not reused yet at shutdown, but it can crash on exit. The stale table entry also stays.

## Fix

End `CServerMoniterView::Destroy` with `KillTimer` and `CWindowBase::Destroy`, like the
architecture view.
