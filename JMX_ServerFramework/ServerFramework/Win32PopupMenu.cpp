#include "Win32PopupMenu.h"

#include <stdarg.h>
#include <stdio.h>

#include "BSLib/BSDebug.h"

namespace ServerFramework
{

CWin32Control_PopupMenu::CWin32Control_PopupMenu()
{
    m_hMenu = CreatePopupMenu();
}

CWin32Control_PopupMenu::~CWin32Control_PopupMenu()
{
    for (std::list<CWin32Control_PopupMenu*>::iterator it = m_listSubMenu.begin();
         it != m_listSubMenu.end(); ++it)
    {
        if (*it)
            delete *it;
    }
    m_listSubMenu.clear();
    DestroyMenu(m_hMenu);
}

CWin32Control_PopupMenu* CWin32Control_PopupMenu::AppendSubMenuF(const char* pszFormat, ...)
{
    char szText[260];
    va_list args;
    va_start(args, pszFormat);
    int nLen = _vsnprintf_s(szText, sizeof(szText), sizeof(szText) - 1, pszFormat, args);
    va_end(args);
    if (nLen < 0 || nLen > (int)sizeof(szText) - 1)
    {
        szText[sizeof(szText) - 1] = 0;
        BSAssertFail();
    }
    else if (nLen == sizeof(szText) - 1)
    {
        szText[sizeof(szText) - 1] = 0;
    }

    CWin32Control_PopupMenu* pSubMenu = new CWin32Control_PopupMenu;
    AppendMenuA(m_hMenu, MF_POPUP, (UINT_PTR)pSubMenu->m_hMenu, szText);
    m_listSubMenu.push_back(pSubMenu);
    return pSubMenu;
}

BOOL CWin32Control_PopupMenu::TrackAtCursor(HWND hOwner)
{
    POINT pt;
    GetCursorPos(&pt);
    return TrackPopupMenu(m_hMenu, TPM_RETURNCMD, pt.x, pt.y, 0, hOwner, NULL);
}

BOOL CWin32Control_PopupMenu::AppendItemF(UINT uFlags, UINT_PTR uID, const char* pszFormat, ...)
{
    char szText[260];
    va_list args;
    va_start(args, pszFormat);
    int nLen = _vsnprintf_s(szText, sizeof(szText), sizeof(szText) - 1, pszFormat, args);
    va_end(args);
    if (nLen < 0 || nLen > (int)sizeof(szText) - 1)
    {
        szText[sizeof(szText) - 1] = 0;
        BSAssertFail();
    }
    else if (nLen == sizeof(szText) - 1)
    {
        szText[sizeof(szText) - 1] = 0;
    }
    return AppendMenuA(m_hMenu, uFlags, uID, szText);
}

} // namespace ServerFramework
