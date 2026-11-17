# FWUI-004: architecture view icons freed with DeleteObject

- Original: `CServerArchitectureView::Destroy` 0x455940 (GatewayServer)
- Rewrite: `CServerArchitectureView::Destroy` in
  `JMX_ServerFramework/ServerFramework/ServerArchitectureView.cpp`
- Severity: minor

## What the original does

The ten icons loaded with `LoadIconA` from ServerFrameworkRes.dll are released with
`DeleteObject`. The nine state icons are set to NULL afterwards, the overlay icon at +260 is not.

## Effect

`DeleteObject` does not accept icon handles and fails; icons from `LoadIconA` are shared and
need no release anyway. Nothing leaks, the calls are just useless. A second `Destroy` would pass
the stale overlay handle again.

## Fix

Drop the calls (or use `DestroyIcon` for icons loaded with `LoadImage`) and clear the overlay
handle.
