# FWUI-010: queued dialog texts are freed but stay in the list

- Original: `CWin32PropertyDialog::ApplyPendingControlTexts` 0x4662b0 (vtable slot 2),
  `CWin32PropertyDialog::dtor` 0x465eb0 (GatewayServer)
- Rewrite: `CWin32PropertyDialog::OnInitDialog`, `CWin32PropertyDialog::~CWin32PropertyDialog` in
  `JMX_ServerFramework/ServerFramework/Win32Dialog.cpp`
- Severity: crash

## What the original does

`WM_INITDIALOG` applies every queued `{control ID, text, enable}` entry and deletes it, but the
list still holds the pointers afterwards. The destructor frees only the list nodes, never the
entries.

## Effect

Showing the same dialog object a second time applies the freed entries again (reads freed
memory) and deletes them a second time: heap corruption. Entries queued for a dialog that is
never shown leak.

## Fix

Clear the list after applying it, and delete the remaining entries in the destructor.
