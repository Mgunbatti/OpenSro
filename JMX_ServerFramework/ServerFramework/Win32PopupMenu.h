#pragma once

#include <windows.h>
#include <list>

namespace ServerFramework
{

// Popup menu that owns its submenus.
class CWin32Control_PopupMenu
{
public:
    CWin32Control_PopupMenu();
    virtual ~CWin32Control_PopupMenu();

    // Appends a submenu with the formatted text (asserts beyond 259 characters).
    CWin32Control_PopupMenu* AppendSubMenuF(const char* pszFormat, ...);
    // Menu_AppendItemF (0x465b90, not referenced): AppendMenuA with the formatted text (asserts
    // beyond 259 characters).
    BOOL AppendItemF(UINT uFlags, UINT_PTR uID, const char* pszFormat, ...);
    // PopupMenu_TrackAtCursor (0x465c50)
    BOOL TrackAtCursor(HWND hOwner);

public:
    HMENU                                   m_hMenu;        // +4
    std::list<CWin32Control_PopupMenu*>     m_listSubMenu;  // +8
};

C_ASSERT(FIELD_OFFSET(CWin32Control_PopupMenu, m_listSubMenu) == 8);
C_ASSERT(sizeof(CWin32Control_PopupMenu) == 20);

} // namespace ServerFramework
