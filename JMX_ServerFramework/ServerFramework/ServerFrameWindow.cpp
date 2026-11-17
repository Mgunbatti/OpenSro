#include "ServerFrameWindow.h"

#include <commctrl.h>
#include <string.h>
#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "BSNet/IBSNet.h"
#include "ServerFramework/GdiUtil.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerArchitectureView.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerMoniterView.h"
#include "ServerFramework/ServerUiExtern.h"

ServerFramework::CServerFrameWindow* g_pServerFrameWindow = NULL;

int LogView_AppendLine_wrap(DWORD dwFlags, const char* pszText)
{
    if (g_pServerFrameWindow)
        ServerFramework::CServerFrameWindow::AppendLogLine(pszText, dwFlags);
    return 0;
}

namespace ServerFramework
{

namespace
{

LVITEMA s_LogItem;          // 0x53DE18
char    s_szLogTime[255];   // 0x53DD08

} // namespace

CServerFrameWindow::CServerFrameWindow()
{
    m_dwDumpFlags = 0;

    // Not set by the original constructor.
    m_hLogList = NULL;
    m_hFont = NULL;
    m_hThread = NULL;
    m_hMoniterView = NULL;
    m_hMainView = NULL;
    m_hLogListPane = NULL;
    m_pMoniterView = NULL;
    m_pArchView = NULL;
}

CServerFrameWindow::~CServerFrameWindow()
{
}

HWND CServerFrameWindow_CreateListView(CServerFrameWindow* pFrame, HINSTANCE hInstance, RECT rc)
{
    if (!pFrame->m_hWnd)
        BSAssertFail();
    return CreateWindowExA(WS_EX_CLIENTEDGE, "SysListView32", NULL,
                           WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL, rc.left, rc.top,
                           rc.right - rc.left, rc.bottom - rc.top, pFrame->m_hWnd,
                           (HMENU)IDC_SERVER_LOG_LIST, hInstance, NULL);
}

LRESULT CServerFrameWindow::InsertLogColumn(int nColumn, const char* pszText, int nWidth)
{
    LVCOLUMNA col;
    memset(&col, 0, sizeof(col));
    col.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    col.cx = nWidth;
    col.pszText = (LPSTR)pszText;
    col.cchTextMax = 4;
    col.iSubItem = nColumn;
    return SendMessageA(m_hLogList, LVM_INSERTCOLUMNA, nColumn, (LPARAM)&col);
}

bool CServerFrameWindow::CreateFrame()
{
    HINSTANCE hInstance = g_ServerGlobals.hInstance;
    const char* pszName = g_ServerGlobals.szServiceName;
    if (!RegisterWindowClass(hInstance, pszName))
        return false;

    int x = GetSystemMetrics(SM_CXSCREEN) - 600;
    int nRight = GetSystemMetrics(SM_CXSCREEN);
    HMENU hMenu = LoadMenuA(g_hServerFrameworkRes, MAKEINTRESOURCEA(IDR_SERVER_FRAME_MENU));
    RECT rc;
    rc.left = x;
    rc.top = 0;
    rc.right = nRight;
    rc.bottom = 500;
    if (!Create(0, pszName, pszName, WS_OVERLAPPEDWINDOW, rc, NULL, hMenu, hInstance))
        return false;

    InsertCommandHandler(IDM_FRAME_ABOUT, &CServerFrameWindow::OnAbout);
    InsertCommandHandler(IDM_FRAME_EXIT, &CServerFrameWindow::OnExit);
    InsertCommandHandler(IDM_FRAME_DUMP, &CServerFrameWindow::OnDump);
    InsertMsgHandler(WM_NOTIFY, &CServerFrameWindow::OnNotify);
    InsertMsgHandler(WM_ERASEBKGND, &CServerFrameWindow::OnEraseBkgnd);
    InsertMsgHandler(WM_SERVER_FRAME_9000, &CServerFrameWindow::OnMessage9000);
    InsertMsgHandler(WM_SYSCOMMAND, &CServerFrameWindow::OnSysCommand);
    SetTimer(m_hWnd, SERVER_FRAME_TIMER_ID, 1000, NULL);

    RECT rcChild;
    rcChild.left = 0;
    rcChild.top = 0;
    rcChild.right = 300;
    rcChild.bottom = 300;
    m_hLogList = CServerFrameWindow_CreateListView(this, hInstance, rcChild);
    if (!m_hLogList)
        return false;
    SendMessageA(m_hLogList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                 LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP | LVS_EX_FLATSB);
    InsertLogColumn(0, "Type", 60);
    InsertLogColumn(1, "Log", 320);
    InsertLogColumn(2, "Time", 150);

    char szFace[255];
    memset(szFace, 0, sizeof(szFace));
    Win32_GetLocaleFontName(szFace, sizeof(szFace));
    // The font of the log list is never deleted.
    HFONT hListFont = CreateFontA(11, 0, 0, 0, 0, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, szFace);
    SendMessageA(m_hLogList, WM_SETFONT, (WPARAM)hListFont, 0);

    m_pArchView = new CServerArchitectureView;
    m_pMoniterView = new CServerMoniterView;
    if (!m_pArchView->Create(hInstance, m_hWnd, rcChild, (HMENU)IDC_SERVER_ARCH_VIEW)
        || !m_pMoniterView->Create(hInstance, m_hWnd, rcChild, (HMENU)IDC_SERVER_MONITER_VIEW))
        return false;

    m_listChild.push_back(m_pArchView);
    m_listChild.push_back(m_pMoniterView);
    m_hFont = CreateFontA(12, 0, 0, 0, 0, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "arial");
    SendMessageA(m_hWnd, WM_SETFONT, (WPARAM)m_hFont, 0);
    m_hMoniterView = m_pMoniterView->m_hWnd;
    m_hLogListPane = m_hLogList;
    m_hMainView = m_pArchView->m_hWnd;
    m_listMainView.push_back(m_pArchView);
    AddSplitPane(m_hMoniterView, m_hMainView, 0.2f);
    AddSplitPane(m_hMainView, m_hLogListPane, 0.7f);
    Show(SW_SHOW);
    Update();

    HMENU hViewMenu = CreatePopupMenu();
    HMENU hSubMenu = GetSubMenu(GetSubMenu(hMenu, 0), 2);
    AppendMenuA(hSubMenu, MF_POPUP, (UINT_PTR)hViewMenu, "MainView");
    AppendMenuA(hViewMenu, MF_STRING, IDM_FRAME_ARCH_VIEW, "ArchitectureView");
    CheckMenuItem(hViewMenu, IDM_FRAME_ARCH_VIEW, MF_CHECKED);
    InsertCommandHandler(IDM_FRAME_ARCH_VIEW, &CServerFrameWindow::OnArchViewCommand);
    return true;
}

void CServerFrameWindow::ResetSplitPanes()
{
    if (m_listPane.size() != 2)
        BSAssertFail();
    float fTopRatio = m_listPane.front().fRatio;
    float fBottomRatio = m_listPane.back().fRatio;
    m_listPane.clear();
    AddSplitPane(m_hMoniterView, m_hMainView, fTopRatio);
    AddSplitPane(m_hMainView, m_hLogListPane, fBottomRatio);
    RECT rc;
    GetClientRect(&rc);
    LayoutPanes(rc.right - rc.left, rc.bottom - rc.top);
}

BOOL CServerFrameWindow::AddChildView(CServerChildWindowBase* pView)
{
    m_listChild.push_back(pView);
    m_listMainView.push_back(pView);
    InsertCommandHandler(IDM_FRAME_CHILD_VIEW, &CServerFrameWindow::OnChildViewCommand);
    HMENU hSubMenu = GetSubMenu(GetSubMenu(GetSubMenu(GetMenu(m_hWnd), 0), 2), 2);
    AppendMenuA(hSubMenu, MF_STRING, IDM_FRAME_CHILD_VIEW, pView->GetTitle());
    return pView->Show(SW_HIDE);
}

void CServerFrameWindow::SwitchView(CServerChildWindowBase* pView)
{
    ShowWindow(m_hMainView, SW_HIDE);
    m_hMainView = pView->m_hWnd;
    ShowWindow(m_hMainView, SW_SHOW);
    HMENU hSubMenu = GetSubMenu(GetSubMenu(GetSubMenu(GetMenu(m_hWnd), 0), 2), 2);
    if (pView == m_pArchView)
    {
        CheckMenuItem(hSubMenu, IDM_FRAME_ARCH_VIEW, MF_CHECKED);
        CheckMenuItem(hSubMenu, IDM_FRAME_CHILD_VIEW, MF_UNCHECKED);
    }
    else
    {
        CheckMenuItem(hSubMenu, IDM_FRAME_ARCH_VIEW, MF_UNCHECKED);
        CheckMenuItem(hSubMenu, IDM_FRAME_CHILD_VIEW, MF_CHECKED);
    }
    ResetSplitPanes();
}

bool CServerFrameWindow::Destroy()
{
    m_listMainView.clear();
    TerminateThread(m_hThread, 0xFFFFFFFF);
    for (ServerChildWindowList::iterator it = m_listChild.begin(); it != m_listChild.end(); ++it)
    {
        CServerChildWindowBase* pView = *it;
        // BUG(FWUI-002): the monitor view keeps its window, which then reaches the deleted
        // object through the window table when the frame window is destroyed below.
        pView->Destroy();
        delete pView;
    }
    m_listChild.clear();
    DeleteObject(m_hFont);
    DestroyWindowBase();
    return true;
}

LRESULT CServerFrameWindow::OnAbout(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    MessageBoxA(NULL, "\xB8\xDE\xB7\xD5", "\xB8\xDE\xB7\xD5", MB_OK);
    return 0;
}

LRESULT CServerFrameWindow::OnExit(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    PostMessageA(m_hWnd, WM_CLOSE, 0, 0);
    return 0;
}

LRESULT CServerFrameWindow::OnDump(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    if (m_dwDumpFlags & SERVER_DUMP_ENGINE_VERSION)
    {
        LogPrintf(0, "============== Engine Version ================");
        g_pBSNet->PrintVersion();
    }
    if (m_dwDumpFlags & SERVER_DUMP_MSG_USAGE)
    {
        LogPrintf(0, "============== Msg Usage ================");
        g_pBSNet->DumpMsgPool(1, 0xFFFFFFFF);
    }
    if (m_dwDumpFlags & SERVER_DUMP_CUSTOM)
    {
        LogPrintf(0, "============== Custom Dump ================");
        ServerApp_CustomDump();
    }
    return 0;
}

LRESULT CServerFrameWindow::OnNotify(WPARAM wParam, LPARAM lParam)
{
    if (wParam != IDC_SERVER_LOG_LIST)
        return 0;

    NMHDR* pHdr = (NMHDR*)lParam;
    if (pHdr->code == NM_CUSTOMDRAW)
    {
        NMLVCUSTOMDRAW* pDraw = (NMLVCUSTOMDRAW*)lParam;
        DWORD dwStage = pDraw->nmcd.dwDrawStage;
        if (dwStage == CDDS_PREPAINT || dwStage == CDDS_ITEMPREPAINT)
            return CDRF_NOTIFYITEMDRAW;
        if (dwStage != (CDDS_ITEMPREPAINT | CDDS_SUBITEM))
            return 0;

        LVITEMA item;
        memset(&item, 0, sizeof(item));
        item.iItem = (int)pDraw->nmcd.dwItemSpec;
        item.mask = LVIF_PARAM;
        item.iSubItem = pDraw->iSubItem;
        SendMessageA(m_hLogList, LVM_GETITEMA, 0, (LPARAM)&item);
        // Colour by the log level in the high byte of the item's flags.
        switch ((int)item.lParam >> 24)
        {
        case 0:
            pDraw->clrText = 0;
            pDraw->clrTextBk = 0xFFFFFF;
            break;
        case 1:
            pDraw->clrText = 0xB400B4;
            pDraw->clrTextBk = 0xFFFFFF;
            break;
        case 2:
            pDraw->clrTextBk = 0xFFFFFF;
            pDraw->clrText = 0xFF;
            break;
        default:
            pDraw->clrTextBk = 0xFFFFFF;
            pDraw->clrText = 0x808080;
            break;
        }
        return 0;
    }

    if (pHdr->code == NM_RCLICK)
    {
        HMENU hMenu = CreatePopupMenu();
        AppendMenuA(hMenu, MF_STRING, IDM_LOG_SAVE_CLEAR, "Save && Clear");
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(hMenu, MF_STRING, IDM_LOG_SAVE, "Save");
        AppendMenuA(hMenu, MF_STRING, IDM_LOG_CLEAR, "Clear");
#ifndef JMX_FRAMEWORK_2007
        // FarmManager 0x446e20 has no copy item.
        AppendMenuA(hMenu, MF_STRING, IDM_LOG_COPY, "Copy SelectedItem To Clipboard");
#endif
        POINT pt;
        GetCursorPos(&pt);
        // BUG(FWUI-001): no TPM_RETURNCMD; the result is TRUE/FALSE, so no item has an effect.
        switch (TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hLogList, NULL))
        {
        case IDM_LOG_SAVE_CLEAR:
            ReportLog_Save(this);
            ClearLog();
            break;
        case IDM_LOG_SAVE:
            ReportLog_Save(this);
            break;
        case IDM_LOG_CLEAR:
            ClearLog();
            break;
#ifndef JMX_FRAMEWORK_2007
        case IDM_LOG_COPY:
            CopySelectedLogToClipboard();
            break;
#endif
        }
        DestroyMenu(hMenu);
    }
    return 0;
}

LRESULT CServerFrameWindow::OnEraseBkgnd(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    return 1;
}

LRESULT CServerFrameWindow::OnSysCommand(WPARAM wParam, LPARAM lParam)
{
    if (wParam == SC_CLOSE && g_ServerGlobals.bPromptAtClose && g_pSelfBody
        && g_pSelfBody->nState == 5
        && MessageBoxA(m_hWnd, "server is operating\n\nare you sure?", "close server",
                       MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
    {
        return 0;
    }
    return DefWindowProcA(m_hWnd, WM_SYSCOMMAND, wParam, lParam);
}

LRESULT CServerFrameWindow::OnArchViewCommand(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    SwitchView(m_listMainView.front());
    return 0;
}

LRESULT CServerFrameWindow::OnChildViewCommand(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    SwitchView(m_listMainView.back());
    return 0;
}

LRESULT CServerFrameWindow::OnMessage9000(WPARAM /*wParam*/, LPARAM lParam)
{
    ServerApp_OnFrameMessage9000(lParam);
    return 0;
}

LRESULT CServerFrameWindow::AppendLogLine(const char* pszText, DWORD dwFlags)
{
    CServerFrameWindow* pFrame = g_pServerFrameWindow;
    HWND hList = pFrame->m_hLogList;
    if (SendMessageA(hList, LVM_GETITEMCOUNT, 0, 0) > SERVER_LOG_MAX_ITEMS)
        SendMessageA(hList, LVM_DELETEITEM, ListView_GetFirstItem(hList), 0);

    memset(&s_LogItem, 0, sizeof(s_LogItem));
    s_LogItem.mask = LVIF_TEXT | LVIF_PARAM;
    s_LogItem.pszText = (LPSTR)pszText;
    s_LogItem.cchTextMax = 4;
    s_LogItem.lParam = dwFlags;
    s_LogItem.iItem = (int)SendMessageA(hList, LVM_GETITEMCOUNT, 0, 0);
    int nItem = (int)SendMessageA(hList, LVM_INSERTITEMA, 0, (LPARAM)&s_LogItem);

    s_LogItem.mask = LVIF_TEXT;
    s_LogItem.iItem = nItem;
    s_LogItem.iSubItem = 0;
    s_LogItem.pszText = (LPSTR)LogLevel_ToString(dwFlags);
    s_LogItem.cchTextMax = 4;
    SendMessageA(hList, LVM_SETITEMA, 0, (LPARAM)&s_LogItem);

    s_LogItem.mask = LVIF_TEXT;
    s_LogItem.iItem = nItem;
    s_LogItem.iSubItem = 1;
    s_LogItem.pszText = (LPSTR)pszText;
    s_LogItem.cchTextMax = 4;
    SendMessageA(hList, LVM_SETITEMA, 0, (LPARAM)&s_LogItem);

    SYSTEMTIME st;
    GetLocalTime(&st);
    if (StringCchPrintfA(s_szLogTime, sizeof(s_szLogTime), "%04d-%02d-%02d %02d:%02d:%02d %03d",
                         st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                         st.wMilliseconds) != S_OK)
        BSAssertFail();
    s_LogItem.mask = LVIF_TEXT;
    s_LogItem.iItem = nItem;
    s_LogItem.iSubItem = 2;
    s_LogItem.pszText = s_szLogTime;
    s_LogItem.cchTextMax = 4;
    SendMessageA(hList, LVM_SETITEMA, 0, (LPARAM)&s_LogItem);
    return SendMessageA(hList, LVM_ENSUREVISIBLE, nItem, TRUE);
}

LRESULT CServerFrameWindow::ClearLog()
{
    return SendMessageA(m_hLogList, LVM_DELETEALLITEMS, 0, 0);
}

LRESULT CServerFrameWindow::CopySelectedLogToClipboard()
{
    LRESULT nItem = ListView_GetFirstSelectedItem(m_hLogList);
    if (nItem != -1)
    {
        std::string strText;
        strText.assign(ListView_GetItemTextString(m_hLogList, (int)nItem, 1));
        Clipboard_SetText(strText.c_str());
    }
    return nItem;
}

} // namespace ServerFramework
