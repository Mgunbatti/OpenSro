#include "WindowBase.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BSLib/BSDebug.h"

namespace ServerFramework
{

std::map<HWND, CWindowBase*> g_mapWindow;
stdext::hash_map<UINT, WINDOW_GLOBAL_HANDLER> g_hmapWindowHandler;

CWindowBase::CWindowBase()
{
    m_hWnd = NULL;
    m_pParent = NULL;
    m_hInstance = NULL;
    m_bDialog = 0;
    m_listAnchor.clear();
}

CWindowBase::~CWindowBase()
{
}

LRESULT CWindowBase::WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_DESTROY)
    {
        if (!m_bDialog)
            PostQuitMessage(0);
    }
    else if (uMsg == WM_COMMAND)
    {
        return DispatchCommandHandler(wParam, lParam);
    }
    return DispatchMessageHandler(uMsg, wParam, lParam);
}

bool CWindowBase::CreateWin32Window(DWORD dwExStyle, LPCSTR pszClassName, LPCSTR pszWindowName,
                                    DWORD dwStyle, HWND hParent, HINSTANCE hInstance, RECT rc,
                                    HMENU hMenu)
{
    CREATESTRUCTA cs;
    memset(&cs, 0, sizeof(cs));
    cs.style = dwStyle;
    cs.lpszName = pszWindowName;
    cs.lpszClass = pszClassName;
    cs.dwExStyle = dwExStyle;
    cs.hwndParent = hParent;
    cs.hMenu = hMenu;
    cs.x = rc.left;
    cs.cx = rc.right - rc.left;
    cs.cy = rc.bottom - rc.top;
    cs.hInstance = hInstance;
    cs.y = rc.top;

    m_hWnd = CreateWindowExA(dwExStyle, pszClassName, pszWindowName, dwStyle, rc.left, rc.top,
                             rc.right - rc.left, rc.bottom - rc.top, hParent, hMenu, hInstance,
                             &cs);
    if (!m_hWnd)
        return false;

    g_mapWindow.insert(std::make_pair(m_hWnd, this));
    m_hInstance = hInstance;
    return true;
}

bool CWindowBase::DestroyWindowBase()
{
    DestroyWindow(m_hWnd);
    std::map<HWND, CWindowBase*>::iterator it = g_mapWindow.find(m_hWnd);
    if (it != g_mapWindow.end())
        g_mapWindow.erase(it);
    return false;
}

BOOL CWindowBase::Show(int nCmdShow)
{
    if (!IsWindow(m_hWnd))
        BSAssertFail();
    return ShowWindow(m_hWnd, nCmdShow);
}

BOOL CWindowBase::Update()
{
    if (!IsWindow(m_hWnd))
        BSAssertFail();
    return UpdateWindow(m_hWnd);
}

BOOL CWindowBase::Redraw(UINT uFlags)
{
    return RedrawWindow(m_hWnd, NULL, NULL, uFlags);
}

RECT* CWindowBase::GetWindowRect(RECT* pRect)
{
    ::GetWindowRect(m_hWnd, pRect);
    return pRect;
}

RECT* CWindowBase::GetClientRect(RECT* pRect)
{
    ::GetClientRect(m_hWnd, pRect);
    return pRect;
}

RECT* CWindowBase::GetParentRectOrScreen(RECT* pRect)
{
    HWND hParent = GetParent(m_hWnd);
    if (hParent)
    {
        ::GetWindowRect(hParent, pRect);
    }
    else
    {
        pRect->left = 0;
        pRect->top = 0;
        pRect->right = GetSystemMetrics(SM_CXSCREEN);
        pRect->bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    return pRect;
}

BOOL CWindowBase::CenterOnParent()
{
    RECT rcWnd;
    RECT rcParent;
    GetWindowRect(&rcWnd);
    GetParentRectOrScreen(&rcParent);
    // The parent's position is not added: the result is only right for a top-level parent at
    // the screen origin.
    return SetWindowPos(m_hWnd, NULL,
                        (rcParent.right + rcWnd.left - rcWnd.right - rcParent.left) / 2,
                        (rcParent.bottom + rcWnd.top - rcWnd.bottom - rcParent.top) / 2,
                        0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void CWindowBase::InsertMsgHandler(UINT uMsg, PFN_WINDOW_HANDLER pfn)
{
    if (m_mapMsgHandler.find(uMsg) != m_mapMsgHandler.end())
        BSAssertFail();
    m_mapMsgHandler.insert(std::make_pair(uMsg, pfn));
}

void CWindowBase::InsertCommandHandler(WORD wID, PFN_WINDOW_HANDLER pfn)
{
    UINT uID = wID;
    if (m_mapCmdHandler.find(uID) != m_mapCmdHandler.end())
        BSAssertFail();
    m_mapCmdHandler.insert(std::make_pair(uID, pfn));
}

LRESULT CWindowBase::DispatchMessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    WindowHandlerMap::iterator it = m_mapMsgHandler.find(uMsg);
    if (it == m_mapMsgHandler.end())
    {
        if (m_bDialog)
            return 0;
        return DefWindowProcA(m_hWnd, uMsg, wParam, lParam);
    }
    return (this->*(it->second))(wParam, lParam);
}

LRESULT CWindowBase::DispatchCommandHandler(WPARAM wParam, LPARAM lParam)
{
    WindowHandlerMap::iterator it = m_mapCmdHandler.find((UINT)wParam);
    if (it == m_mapCmdHandler.end())
        return 0;
    return (this->*(it->second))(wParam, lParam);
}

bool CWindowBase::RegisterWindowClass(HINSTANCE hInstance, LPCSTR pszClassName)
{
    WNDCLASSA wc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.lpszMenuName = NULL;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)5;
    wc.lpszClassName = pszClassName;
    wc.lpfnWndProc = StaticWndProc;
    return RegisterClassA(&wc) != 0;
}

LRESULT CALLBACK CWindowBase::StaticWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LRESULT lResult = WindowHandlerList_Dispatch(uMsg, wParam, lParam);
    if (lResult != -1)
        return lResult;

    std::map<HWND, CWindowBase*>::iterator it = g_mapWindow.find(hWnd);
    if (it == g_mapWindow.end())
    {
        if (uMsg != WM_INITDIALOG)
            return DefWindowProcA(hWnd, uMsg, wParam, lParam);

        // Dialogs register themselves: the creation parameter is the object.
        CWindowBase* pDialog = (CWindowBase*)lParam;
        g_mapWindow.insert(std::make_pair(hWnd, pDialog));
        it = g_mapWindow.find(hWnd);
        pDialog->m_hWnd = hWnd;
    }
    return it->second->WndProc(uMsg, wParam, lParam);
}

LRESULT WindowHandlerList_Dispatch(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    stdext::hash_map<UINT, WINDOW_GLOBAL_HANDLER>::iterator it = g_hmapWindowHandler.find(uMsg);
    if (it == g_hmapWindowHandler.end())
        return -1;
    return (it->second.pTarget->*(it->second.pfn))(wParam, lParam);
}

void WindowHandlerMap_Insert(CWindowBase* pTarget, UINT uMsg, PFN_WINDOW_HANDLER pfn)
{
    if (g_hmapWindowHandler.find(uMsg) != g_hmapWindowHandler.end())
        BSAssertFail();
    WINDOW_GLOBAL_HANDLER handler;
    handler.pTarget = pTarget;
    handler.pfn = pfn;
    g_hmapWindowHandler.insert(std::make_pair(uMsg, handler));
}

void Win32_GetLocaleFontName(char* pszFace, size_t cchFace)
{
    LANGID wLang = GetUserDefaultLangID();
    if (wLang > 0x412)
    {
        if (wLang == 0x804)
            sprintf_s(pszFace, cchFace, "SimSun");
        else
            sprintf_s(pszFace, cchFace, "Gulim");
    }
    else if (wLang == 0x412)
    {
        sprintf_s(pszFace, cchFace, "Gulim");
    }
    else if (wLang == 0x404)
    {
        sprintf_s(pszFace, cchFace, "PMingliu");
    }
    else if (wLang == 0x411)
    {
        sprintf_s(pszFace, cchFace, "MS Gothic");
    }
    else
    {
        sprintf_s(pszFace, cchFace, "Gulim");
    }
}

LRESULT Win32Dialog_SetLocaleFont(HWND hWnd, int nHeight)
{
    char szFace[255];
    memset(szFace, 0, sizeof(szFace));
    Win32_GetLocaleFontName(szFace, sizeof(szFace));
    HFONT hFont = CreateFontA(nHeight, 0, 0, 0, 0, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, szFace);
    return SendMessageA(hWnd, WM_SETFONT, (WPARAM)hFont, 0);
}

static bool Win32Ctrl_Create(CWindowBase* pCtrl, LPCSTR pszClass, HINSTANCE hInstance,
                             HWND hParent, HMENU hID, DWORD dwExStyle, DWORD dwStyle, int x,
                             int y, int cx, int cy)
{
    RECT rc;
    rc.left = x;
    rc.top = y;
    rc.right = x + cx;
    rc.bottom = y + cy;
    return pCtrl->CreateWin32Window(dwExStyle, pszClass, "", dwStyle | WS_CHILD | WS_VISIBLE,
                                    hParent, hInstance, rc, hID);
}

bool Win32Ctrl_CreateStatic(CWindowBase* pCtrl, HINSTANCE hInstance, HWND hParent, HMENU hID,
                            DWORD dwExStyle, DWORD dwStyle, int x, int y, int cx, int cy)
{
    return Win32Ctrl_Create(pCtrl, "STATIC", hInstance, hParent, hID, dwExStyle, dwStyle, x, y,
                            cx, cy);
}

bool Win32Ctrl_CreateEdit(CWindowBase* pCtrl, HINSTANCE hInstance, HWND hParent, HMENU hID,
                          DWORD dwExStyle, DWORD dwStyle, int x, int y, int cx, int cy)
{
    return Win32Ctrl_Create(pCtrl, "EDIT", hInstance, hParent, hID, dwExStyle, dwStyle, x, y,
                            cx, cy);
}

bool Win32Ctrl_CreateButton(CWindowBase* pCtrl, HINSTANCE hInstance, HWND hParent, HMENU hID,
                            DWORD dwExStyle, DWORD dwStyle, int x, int y, int cx, int cy)
{
    return Win32Ctrl_Create(pCtrl, "BUTTON", hInstance, hParent, hID, dwExStyle, dwStyle, x, y,
                            cx, cy);
}

BOOL Win32Ctrl_SetTextInt(CWindowBase* pCtrl, int nValue)
{
    char szText[260];
    _itoa_s(nValue, szText, sizeof(szText), 10);
    return SetWindowTextA(pCtrl->m_hWnd, szText);
}

} // namespace ServerFramework
