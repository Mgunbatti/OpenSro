#include "BSLib/BSDebug.h"
#include "ServerFramework/GdiUtil.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerApp.h"
#include "ServerFramework/ServerArchitectureView.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerFrameWindow.h"
#include "ServerFramework/ServerMoniterView.h"
#include "ServerFramework/ServerUiExtern.h"
#include "ServerFramework/Win32Dialog.h"
#include "ServerFramework/Win32PopupMenu.h"
#include "ServerFramework/Win32SizeRuler.h"

#include <commctrl.h>
#include <float.h>
#include <math.h>
#include <stdexcept>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "LogCapture.h"
#include "TestCheck.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace ServerFramework;

// ---------------------------------------------------------------------------------------------
// Parts of the server application the tested code calls

static std::vector<std::string> s_Log;
static int s_nCustomDumps;
static LPARAM s_lParam9000;

// Application object: counts the custom dumps and records what the window passes for 0x9000.
// Its (not initialised) engine provides the messages LogPrintf posts.
class CUiTestApp : public CServerApp
{
public:
    virtual void CustomDump()
    {
        ++s_nCustomDumps;
    }

    virtual LONG OnControlMsg(CMsg* pMsg)
    {
        s_lParam9000 = (LPARAM)pMsg;
        return 0;
    }
};

// Moves the lines LogPrintf posted so far to s_Log.
static void DrainLog()
{
    std::vector<CAPTURED_LOG> v = LogCapture_Drain();
    for (size_t i = 0; i < v.size(); ++i)
        s_Log.push_back(v[i].strText);
}

// ---------------------------------------------------------------------------------------------

static HINSTANCE s_hInstance;
static bool s_bIsolated;    // own window station and desktop: nothing visible, own clipboard

static void Isolate()
{
    HWINSTA hStation = CreateWindowStationA(NULL, 0, WINSTA_ALL_ACCESS, NULL);
    if (!hStation || !SetProcessWindowStation(hStation))
        return;
    HDESK hDesk = CreateDesktopA("ServerUiTest", NULL, NULL, 0, GENERIC_ALL, NULL);
    if (!hDesk || !SetThreadDesktop(hDesk))
        return;
    s_bIsolated = true;
}

static void PumpMessages(bool* pbQuit)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT && pbQuit)
            *pbQuit = true;
        else
            DispatchMessageA(&msg);
    }
}

static RECT MakeRect(int l, int t, int r, int b)
{
    RECT rc = { l, t, r, b };
    return rc;
}

static bool SameRect(const RECT& rc, int l, int t, int r, int b)
{
    return rc.left == l && rc.top == t && rc.right == r && rc.bottom == b;
}

static RECT ChildRect(HWND hChild)
{
    RECT rc;
    GetWindowRect(hChild, &rc);
    MapWindowPoints(NULL, GetParent(hChild), (POINT*)&rc, 2);
    return rc;
}

// The isolated desktop is never shown, so IsWindowVisible is always false there.
static bool HasVisibleStyle(HWND hWnd)
{
    return (GetWindowLongA(hWnd, GWL_STYLE) & WS_VISIBLE) != 0;
}

static size_t CountWindowEntries(CWindowBase* p)
{
    size_t n = 0;
    for (std::map<HWND, CWindowBase*>::iterator it = g_mapWindow.begin(); it != g_mapWindow.end();
         ++it)
    {
        if (it->second == p)
            ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------------------------
// CWindowBase

class CTestWindow : public CWindowBase
{
public:
    CTestWindow() : m_nUser(0), m_nCommand(0), m_wParam(0) {}
    LRESULT OnUser(WPARAM wParam, LPARAM lParam)
    {
        ++m_nUser;
        return wParam + lParam;
    }
    LRESULT OnCommand(WPARAM wParam, LPARAM)
    {
        ++m_nCommand;
        m_wParam = wParam;
        return 77;
    }
    LRESULT OnGlobal(WPARAM wParam, LPARAM)
    {
        return 1000 + wParam;
    }

    int     m_nUser;
    int     m_nCommand;
    WPARAM  m_wParam;
};

static void TestWindowBase()
{
    CHECK(CWindowBase::RegisterWindowClass(s_hInstance, "UiTestWindow"));
    CHECK(!CWindowBase::RegisterWindowClass(s_hInstance, "UiTestWindow"));

    CTestWindow wnd;
    CHECK(wnd.m_hWnd == NULL && wnd.m_bDialog == 0 && wnd.m_mapMsgHandler.empty());
    CHECK(wnd.CreateWin32Window(0, "UiTestWindow", "Test Title", WS_OVERLAPPEDWINDOW, NULL,
                                s_hInstance, MakeRect(10, 20, 210, 170), NULL));
    CHECK(IsWindow(wnd.m_hWnd));
    CHECK(wnd.m_hInstance == s_hInstance);
    CHECK(g_mapWindow[wnd.m_hWnd] == &wnd);
    RECT rc;
    wnd.GetWindowRect(&rc);
    CHECK(rc.left == 10 && rc.top == 20 && rc.right == 210 && rc.bottom == 170);

    wnd.InsertMsgHandler(WM_USER + 1, &CTestWindow::OnUser);
    wnd.InsertCommandHandler(0x1234, &CTestWindow::OnCommand);
    CHECK(SendMessageA(wnd.m_hWnd, WM_USER + 1, 5, 6) == 11);
    CHECK(wnd.m_nUser == 1);
    CHECK(SendMessageA(wnd.m_hWnd, WM_COMMAND, 0x1234, 0) == 77);
    CHECK(wnd.m_nCommand == 1 && wnd.m_wParam == 0x1234);
    // the whole wParam is the key: notifications with a code are not dispatched
    CHECK(SendMessageA(wnd.m_hWnd, WM_COMMAND, MAKEWPARAM(0x1234, 1), 0) == 0);
    CHECK(wnd.m_nCommand == 1);
    // unhandled: DefWindowProcA, for dialogs 0
    CHECK(SendMessageA(wnd.m_hWnd, WM_GETTEXTLENGTH, 0, 0) == 10);
    wnd.m_bDialog = 1;
    CHECK(SendMessageA(wnd.m_hWnd, WM_GETTEXTLENGTH, 0, 0) == 0);
    wnd.m_bDialog = 0;

    // global handlers run before the window
    WindowHandlerMap_Insert(&wnd, WM_USER + 5,
                            static_cast<PFN_WINDOW_HANDLER>(&CTestWindow::OnGlobal));
    CHECK(WindowHandlerList_Dispatch(WM_USER + 5, 3, 0) == 1003);
    CHECK(WindowHandlerList_Dispatch(WM_USER + 6, 3, 0) == -1);
    CHECK(SendMessageA(wnd.m_hWnd, WM_USER + 5, 4, 0) == 1004);
    g_hmapWindowHandler.clear();

    // centred on the screen (no parent)
    CHECK(wnd.CenterOnParent());
    wnd.GetWindowRect(&rc);
    int cx = GetSystemMetrics(SM_CXSCREEN);
    int cy = GetSystemMetrics(SM_CYSCREEN);
    CHECK(rc.left == (cx - 200) / 2 && rc.top == (cy - 150) / 2);

    // destroy: unregistered, WM_DESTROY of a non-dialog posts WM_QUIT
    HWND hWnd = wnd.m_hWnd;
    CHECK(!wnd.DestroyWindowBase());
    CHECK(!IsWindow(hWnd));
    CHECK(g_mapWindow.find(hWnd) == g_mapWindow.end());
    bool bQuit = false;
    PumpMessages(&bQuit);
    CHECK(bQuit);

    // a dialog does not post WM_QUIT
    CTestWindow dlg;
    dlg.m_bDialog = 1;
    CHECK(dlg.CreateWin32Window(0, "UiTestWindow", "", WS_OVERLAPPEDWINDOW, NULL, s_hInstance,
                                MakeRect(0, 0, 50, 50), NULL));
    dlg.DestroyWindowBase();
    bQuit = false;
    PumpMessages(&bQuit);
    CHECK(!bQuit);
    CHECK(g_mapWindow.empty());

    CHECK(UnregisterClassA("UiTestWindow", s_hInstance));
}

// ---------------------------------------------------------------------------------------------
// CWin32SizeRuler

static void TestSizeRuler()
{
    CHECK(CWindowBase::RegisterWindowClass(s_hInstance, "UiTestRuler"));
    CWin32SizeRuler ruler;
    CHECK(ruler.Create(0, "UiTestRuler", "", WS_OVERLAPPEDWINDOW, MakeRect(0, 0, 400, 500), NULL,
                       NULL, s_hInstance));
    CHECK(ruler.m_mapMsgHandler.size() == 6);
    CHECK(ruler.m_hCursor == LoadCursorA(NULL, IDC_ARROW));

    HWND ahPane[3];
    for (int i = 0; i < 3; ++i)
        ahPane[i] = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                                    ruler.m_hWnd, NULL, s_hInstance, NULL);
    ruler.AddSplitPane(ahPane[0], ahPane[1], 0.25f);
    ruler.AddSplitPane(ahPane[1], ahPane[2], 0.75f);

    RECT rcClient;
    ruler.GetClientRect(&rcClient);
    int W = rcClient.right;
    int H = rcClient.bottom;
    // WM_SIZE lays the panes out
    SendMessageA(ruler.m_hWnd, WM_SIZE, 0, MAKELPARAM(W, H));
    RECT rc = ChildRect(ahPane[0]);
    CHECK(rc.left == 0 && rc.top == 0 && rc.right == W && rc.bottom == (int)(H * 0.25 - 4.0));
    rc = ChildRect(ahPane[1]);
    int nTop1 = (int)(0.25f * (float)H);
    CHECK(rc.top == nTop1 && rc.bottom == (int)((float)H * 0.75f) - 4);
    rc = ChildRect(ahPane[2]);
    CHECK(rc.top == (int)(H * 0.75) && rc.bottom == H);

    // hit test: within 4 pixels of ratio * height - 2
    int ySplit = (int)(H * 0.25 - 2.0);
    CHECK(ruler.HitTestSplitterAndCapture(ySplit + 4) == &ruler.m_listPane.front());
    CHECK(ruler.HitTestSplitterAndCapture(ySplit + 5) == NULL);
    CHECK(GetCapture() == NULL);

    // drag the first splitter
    SendMessageA(ruler.m_hWnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, ySplit));
    CHECK(ruler.m_pDragPane == &ruler.m_listPane.front());
    CHECK(ruler.m_nLastX == 10 && ruler.m_nLastY == ySplit);
    CHECK(GetCapture() == ruler.m_hWnd);

    float fH = (float)H;
    float fMargin = (float)(35.0 / fH);
    int y = H / 2;
    SendMessageA(ruler.m_hWnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(20, y));
    CHECK(ruler.m_listPane.front().fRatio == (float)((double)y / fH));
    CHECK(ruler.m_nLastX == 20 && ruler.m_nLastY == y);
    rc = ChildRect(ahPane[0]);
    CHECK(rc.bottom == (int)(H * (double)ruler.m_listPane.front().fRatio - 4.0));

    // stopped 35 pixels above the next splitter
    y = (int)(H * 0.74);
    SendMessageA(ruler.m_hWnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(20, y));
    CHECK(ruler.m_listPane.front().fRatio == (float)(0.75f - (double)fMargin));
    // and 35 pixels below the top
    SendMessageA(ruler.m_hWnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(20, 0));
    CHECK(ruler.m_listPane.front().fRatio == fMargin);

    SendMessageA(ruler.m_hWnd, WM_LBUTTONUP, 0, MAKELPARAM(20, 0));
    CHECK(ruler.m_pDragPane == NULL);
    CHECK(GetCapture() == NULL);

    // the second splitter is held 35 pixels below the first
    ySplit = (int)(H * 0.75 - 2.0);
    SendMessageA(ruler.m_hWnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, ySplit));
    CHECK(ruler.m_pDragPane == &ruler.m_listPane.back());
    SendMessageA(ruler.m_hWnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(10, 5));
    CHECK(ruler.m_listPane.back().fRatio == (float)((double)fMargin + fMargin));
    SendMessageA(ruler.m_hWnd, WM_LBUTTONUP, 0, 0);

    // hovering over a splitter sets the size cursor
    ySplit = (int)(H * (double)ruler.m_listPane.back().fRatio - 2.0);
    SendMessageA(ruler.m_hWnd, WM_MOUSEMOVE, 0, MAKELPARAM(10, ySplit));
    CHECK(ruler.m_hCursor == LoadCursorA(NULL, IDC_SIZENS));
    SendMessageA(ruler.m_hWnd, WM_MOUSEMOVE, 0, MAKELPARAM(10, ySplit + 20));
    CHECK(ruler.m_hCursor == LoadCursorA(NULL, IDC_ARROW));

    CHECK(SendMessageA(ruler.m_hWnd, WM_PAINT, 0, 0) == 0);
    ruler.DestroyWindowBase();
    PumpMessages(NULL);
    CHECK(UnregisterClassA("UiTestRuler", s_hInstance));
}

// ---------------------------------------------------------------------------------------------
// GDI helpers

static void TestGdiUtil()
{
    // crossing, parallel, apart, touching
    CHECK(Geom_SegmentsIntersect2D(0, 0, 10, 10, 0, 10, 10, 0));
    CHECK(!Geom_SegmentsIntersect2D(0, 0, 10, 0, 0, 5, 10, 5));
    CHECK(!Geom_SegmentsIntersect2D(0, 0, 10, 10, 20, 0, 30, -10));
    CHECK(Geom_SegmentsIntersect2D(164, 24, 164, 44, 142, 32, 174, 32));
    CHECK(!Geom_SegmentsIntersect2D(132, 24, 132, 44, 142, 32, 174, 32));
    CHECK(Geom_SegmentsIntersect2D(0, 0, 10, 0, 10, 0, 10, 10));

    HDC hScreen = GetDC(NULL);
    HDC hDC = CreateCompatibleDC(hScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, 100, 100);
    HGDIOBJ hOld = SelectObject(hDC, hBitmap);
    RECT rc = { 0, 0, 100, 100 };
    Gdi_DrawRectFillOutline(hDC, &rc, 0x00FF00, 0x0000FF);
    CHECK(GetPixel(hDC, 50, 50) == 0x00FF00);
    CHECK(GetPixel(hDC, 0, 50) == 0x0000FF);
    Gdi_DrawLine(hDC, 1, 10, 20, 90, 20, 0xFF0000, PS_SOLID);
    CHECK(GetPixel(hDC, 50, 20) == 0xFF0000);
    ArchView_DrawMarker6x6(hDC, 70, 70);
    CHECK(GetPixel(hDC, 73, 73) == 0xFF0000);
    Gdi_DrawCircleMarker10(hDC, 30, 60);
    CHECK(GetPixel(hDC, 35, 65) == 0x00FFFF);
    Gdi_DrawRectOutline(hDC, 1, 40, 40, 10, 10, 0x123456, PS_SOLID);
    CHECK(GetPixel(hDC, 45, 40) == 0x123456 && GetPixel(hDC, 50, 45) == 0x123456);
    {
        CGdiScopedArialFont font(hDC, 8);
        CHECK(Gdi_TextOutf(hDC, 0, 0, TA_RIGHT, "%d", 12) == TA_RIGHT);
        CHECK(GetTextAlign(hDC) == TA_LEFT);
        Gdi_TextOutColoredf(hDC, 0, 0, TA_LEFT, "^1a^2b%s", "c");
    }
    SelectObject(hDC, hOld);
    DeleteObject(hBitmap);
    DeleteDC(hDC);
    ReleaseDC(NULL, hScreen);

    if (s_bIsolated)
    {
        Clipboard_SetTextStripColorCodes("^1red^2 and ^x^9");
        CHECK(OpenClipboard(NULL));
        HANDLE hData = GetClipboardData(CF_TEXT);
        CHECK(hData && strcmp((const char*)GlobalLock(hData), "red and ^x") == 0);
        if (hData)
            GlobalUnlock(hData);
        CloseClipboard();
    }
}

// ---------------------------------------------------------------------------------------------
// List view helpers

static void TestListViewUtil()
{
    HWND hList = CreateWindowExA(0, "SysListView32", NULL, WS_POPUP | LVS_REPORT, 0, 0, 300, 200,
                                 NULL, NULL, s_hInstance, NULL);
    CHECK(hList != NULL);
    SendMessageA(hList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_CHECKBOXES);
    CHECK(ListView_InsertColumnA_2(hList, 0, "A", 50, LVCFMT_LEFT) == 0);
    CHECK(ListView_InsertColumnA_2(hList, 1, "B", 70, LVCFMT_LEFT) == 1);
    CHECK(SendMessageA(hList, LVM_GETCOLUMNWIDTH, 1, 0) == 70);
    CHECK(ListView_AppendItemText(hList, "first", 11) == 0);
    CHECK(ListView_AppendItemText(hList, "second", 22) == 1);
    CHECK(ListView_GetItemParam(hList, 1) == 22);
    CHECK(ListView_GetItemTextString(hList, 0, 0) == "first");
    CHECK(ListView_GetFirstItem(hList) == 0);
    CHECK(ListView_GetFirstSelectedItem(hList) == -1);
    ListView_SetItemCheckState(hList, 1, TRUE);
    CHECK((SendMessageA(hList, LVM_GETITEMSTATE, 1, LVIS_STATEIMAGEMASK) >> 12) == 2);
    ListView_SetItemCheckState(hList, 1, FALSE);
    CHECK((SendMessageA(hList, LVM_GETITEMSTATE, 1, LVIS_STATEIMAGEMASK) >> 12) == 1);

    if (s_bIsolated)
    {
        LVITEMA item;
        memset(&item, 0, sizeof(item));
        item.iSubItem = 1;
        item.pszText = "^3x";
        SendMessageA(hList, LVM_SETITEMTEXTA, 0, (LPARAM)&item);
        ListView_CopyAllToClipboard(hList, 2);
        CHECK(OpenClipboard(NULL));
        HANDLE hData = GetClipboardData(CF_TEXT);
        CHECK(hData && strcmp((const char*)GlobalLock(hData),
                              "A\tB\r\nfirst\tx\r\nsecond\t\r\n") == 0);
        if (hData)
            GlobalUnlock(hData);
        CloseClipboard();
    }
    DestroyWindow(hList);
}

// ---------------------------------------------------------------------------------------------
// Popup menu

static void TestPopupMenu()
{
    CWin32Control_PopupMenu* pRoot = new CWin32Control_PopupMenu;
    CHECK(IsMenu(pRoot->m_hMenu));
    CWin32Control_PopupMenu* pSub = pRoot->AppendSubMenuF("Sub %d", 5);
    CWin32Control_PopupMenu* pSub2 = pRoot->AppendSubMenuF("%s", "Other");
    CWin32Control_PopupMenu* pNested = pSub->AppendSubMenuF("Nested");
    CHECK(GetMenuItemCount(pRoot->m_hMenu) == 2);
    char szText[64];
    GetMenuStringA(pRoot->m_hMenu, 0, szText, sizeof(szText), MF_BYPOSITION);
    CHECK(strcmp(szText, "Sub 5") == 0);
    GetMenuStringA(pRoot->m_hMenu, 1, szText, sizeof(szText), MF_BYPOSITION);
    CHECK(strcmp(szText, "Other") == 0);
    CHECK(GetSubMenu(pRoot->m_hMenu, 0) == pSub->m_hMenu);
    CHECK(GetSubMenu(pSub->m_hMenu, 0) == pNested->m_hMenu);
    CHECK(pRoot->m_listSubMenu.size() == 2 && pRoot->m_listSubMenu.back() == pSub2);
    HMENU hNested = pNested->m_hMenu;
    HMENU hRoot = pRoot->m_hMenu;
    delete pRoot;
    CHECK(!IsMenu(hRoot) && !IsMenu(hNested));
}

// ---------------------------------------------------------------------------------------------
// Dialogs

static std::vector<BYTE> s_Template;

static void AlignTemplate()
{
    while (s_Template.size() % 4)
        s_Template.push_back(0);
}

static void AddWord(WORD w)
{
    s_Template.push_back((BYTE)w);
    s_Template.push_back((BYTE)(w >> 8));
}

static void AddItem(WORD wClassAtom, WORD wID, DWORD dwStyle, short y)
{
    AlignTemplate();
    DLGITEMTEMPLATE item = { dwStyle | WS_CHILD | WS_VISIBLE, 0, 5, y, 80, 12, wID };
    s_Template.insert(s_Template.end(), (BYTE*)&item, (BYTE*)&item + sizeof(item));
    AddWord(0xFFFF);
    AddWord(wClassAtom);
    AddWord(0);     // title
    AddWord(0);     // creation data
}

static const DLGTEMPLATE* BuildTemplate()
{
    s_Template.clear();
    DLGTEMPLATE dlg = { WS_POPUP | WS_CAPTION, 0, 5, 0, 0, 150, 100 };
    s_Template.insert(s_Template.end(), (BYTE*)&dlg, (BYTE*)&dlg + sizeof(dlg));
    AddWord(0);     // menu
    AddWord(0);     // class
    AddWord(0);     // title
    AddItem(0x81, 100, ES_AUTOHSCROLL, 0);                  // edit
    AddItem(0x80, 101, BS_PUSHBUTTON, 15);                  // button
    AddItem(0x80, 102, BS_AUTOCHECKBOX, 30);                // check box
    AddItem(0x85, 103, CBS_DROPDOWNLIST | WS_VSCROLL, 45);  // combo box
    AddItem(0x81, 104, ES_AUTOHSCROLL, 60);                 // edit
    return (const DLGTEMPLATE*)&s_Template[0];
}

static void TestDialog()
{
    CWindowBase owner;
    owner.m_hWnd = (HWND)0x1234;
    CWin32PropertyDialog dlg(77, 5, &owner);
    CHECK(dlg.m_bDialog == 1 && dlg.m_hInstance == (HINSTANCE)77 && dlg.m_dwParam == 5);
    CHECK(dlg.m_pParent == &owner && dlg.m_hParent == (HWND)0x1234 && dlg.m_nResult == 0);
    dlg.m_hParent = NULL;
    dlg.InsertMsgHandler(WM_INITDIALOG, &CWin32Dialog::OnInitDialog);
    dlg.QueueControlText(100, "abc", FALSE);
    dlg.QueueControlText(101, "btn", TRUE);

    HWND hDlg = CreateDialogIndirectParamA(s_hInstance, BuildTemplate(), NULL,
                                           (DLGPROC)CWindowBase::StaticWndProc, (LPARAM)&dlg);
    CHECK(hDlg != NULL && dlg.m_hWnd == hDlg);
    CHECK(g_mapWindow[hDlg] == &dlg);

    char szText[64];
    GetDlgItemTextA(hDlg, 100, szText, sizeof(szText));
    CHECK(strcmp(szText, "abc") == 0);
    CHECK(IsWindowEnabled(GetDlgItem(hDlg, 100)));
    GetDlgItemTextA(hDlg, 101, szText, sizeof(szText));
    CHECK(strcmp(szText, "btn") == 0);
    CHECK(!IsWindowEnabled(GetDlgItem(hDlg, 101)));

    CHECK(dlg.GetItem(103) == GetDlgItem(hDlg, 103));
    CHECK(dlg.SetItemText(104, "xyz") && dlg.GetItemText(104) == "xyz");
    CHECK(dlg.SetItemInt(104, (UINT)-42, true) && (int)dlg.GetItemInt(104, true) == -42);
    CHECK(dlg.SetItemFloat(104, 1.5f) && dlg.GetItemText(104) == "1.50");
    dlg.SetItemCheck(102, BST_CHECKED);
    CHECK(dlg.IsItemChecked(102));
    dlg.SetItemCheck(102, BST_UNCHECKED);
    CHECK(!dlg.IsItemChecked(102));
    CHECK(dlg.ComboAddString(103, "one") == 0 && dlg.ComboAddString(103, "two") == 1);
    CHECK(dlg.ComboGetCurSel(103) == CB_ERR);
    SendMessageA(dlg.GetItem(103), CB_SETCURSEL, 1, 0);
    CHECK(dlg.ComboGetCurSel(103) == 1 && ComboBox_GetCurSel(dlg.GetItem(103)) == 1);
    dlg.ComboResetContent(103);
    CHECK(SendMessageA(dlg.GetItem(103), CB_GETCOUNT, 0, 0) == 0);
    dlg.EnableItem(101, true);
    CHECK(IsWindowEnabled(GetDlgItem(hDlg, 101)));

    // OK collects the edit texts by control ID and ends the dialog with IDOK
    dlg.OnOK(0, 0);
    CHECK(dlg.m_mapFieldText.size() == 2);
    CHECK(dlg.GetFieldText(100) && strcmp(dlg.GetFieldText(100), "abc") == 0);
    CHECK(dlg.GetFieldText(104) && strcmp(dlg.GetFieldText(104), "1.50") == 0);
    CHECK(dlg.GetFieldText(101) == NULL);
    CHECK(dlg.m_nResult == IDOK);
    dlg.OnCancel(0, 0);
    CHECK(dlg.m_nResult == IDCANCEL);

    // the queued entries were freed but are still listed (FWUI-010)
    CHECK(dlg.m_listPending.size() == 2);
    dlg.m_listPending.clear();

    dlg.DestroyWindowBase();
    CHECK(!IsWindow(hDlg) && g_mapWindow.empty());
    bool bQuit = false;
    PumpMessages(&bQuit);
    CHECK(!bQuit);
}

// ---------------------------------------------------------------------------------------------
// Architecture view

static CERT_MODULE s_aModule[2];
static CERT_MACHINE s_Machine;
static SERVER_BODY s_aBody[5];
static SERVER_CORD s_aCord[2];
static ServerBodyList s_lstBody;
static ServerCordList s_lstCord;

static void SetUpArchitecture()
{
    memset(s_aModule, 0, sizeof(s_aModule));
    s_aModule[0].byID = 1;
    strcpy_s(s_aModule[0].szName, "GatewayServer");
    s_aModule[1].byID = 2;
    strcpy_s(s_aModule[1].szName, "GlobalManager");
    memset(&s_Machine, 0, sizeof(s_Machine));
    strcpy_s(s_Machine.szPublicIP, "1.2.3.4");
    strcpy_s(s_Machine.szPrivateIP, "10.0.0.1");

    // 1 (root) -> 2 (self), 3; 2 -> 4; 5 has no parent
    static const WORD s_awParent[5] = { 0, 1, 1, 2, 0 };
    memset(s_aBody, 0, sizeof(s_aBody));
    s_lstBody.clear();
    for (int i = 0; i < 5; ++i)
    {
        s_aBody[i].wID = (WORD)(i + 1);
        s_aBody[i].wParentID = s_awParent[i];
        s_aBody[i].nState = i;
        s_aBody[i].pModule = &s_aModule[i == 0];
        s_aBody[i].pMachine = &s_Machine;
        s_lstBody.push_back(&s_aBody[i]);
    }
    memset(s_aCord, 0, sizeof(s_aCord));
    s_aCord[0].dwID = 10;
    s_aCord[0].wFromID = 1;
    s_aCord[0].wToID = 2;
    s_aCord[0].dwStatus = 1;
    s_aCord[1].dwID = 11;
    s_aCord[1].wFromID = 2;
    s_aCord[1].wToID = 4;
    s_aCord[1].dwStatus = 3;
    s_aCord[1].byPrivate = 1;
    s_lstCord.clear();
    s_lstCord.push_back(&s_aCord[0]);
    s_lstCord.push_back(&s_aCord[1]);
    g_pServerBodyList = &s_lstBody;
    g_pServerCordList = &s_lstCord;
    g_pSelfBody = &s_aBody[1];
}

static void TestArchitectureView(CServerArchitectureView* pView)
{
    CHECK(g_hServerArchView == pView->m_hWnd);
    CHECK(pView->m_nZoom == 15 && pView->m_afZoom[29] == 1.0f && pView->m_bShowCord);
    CHECK(pView->m_mapMsgHandler.size() == 10);

    SendMessageA(pView->m_hWnd, WM_SERVER_ARCH_LOADED, 0, 0);
    CHECK(pView->m_mapNode.size() == 5 && pView->m_mapCord.size() == 2);
    ARCH_VIEW_ENTRY* apEntry[6] = { NULL };
    for (WORD w = 1; w <= 5; ++w)
        apEntry[w] = pView->FindItem(w);
    CHECK(pView->FindItem(9) == NULL);
    CHECK(pView->m_pRoot == apEntry[1]);
    CHECK(apEntry[2]->pParent == apEntry[1] && apEntry[4]->pParent == apEntry[2]);
    CHECK(apEntry[1]->listChild.size() == 2 && apEntry[2]->listChild.size() == 1);
    CHECK(apEntry[3]->listChild.empty());
    // tree layout from the root, unplaced nodes stacked above this server's node
    CHECK(SameRect(apEntry[1]->rc, 0, 0, 32, 32));
    CHECK(SameRect(apEntry[2]->rc, 132, 0, 164, 32));
    CHECK(SameRect(apEntry[4]->rc, 264, 0, 296, 32));
    CHECK(SameRect(apEntry[3]->rc, 132, 64, 164, 96));
    CHECK(SameRect(apEntry[5]->rc, 132, -64, 164, -32));

    POINT pt;
    pView->WorldToScreen(&pt, 7, 9);
    CHECK(pt.x == 7 && pt.y == 9);
    POINT ptHit = { 140, 10 };
    CHECK(pView->HitTestNode(ptHit) == apEntry[2]);
    ptHit.y = 40;
    CHECK(pView->HitTestNode(ptHit) == NULL);

    // move node 2 to the right
    SendMessageA(pView->m_hWnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(140, 10));
    CHECK(pView->m_pSelected == apEntry[2] && !pView->m_bPanning);
    SendMessageA(pView->m_hWnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(150, 10));
    CHECK(SameRect(apEntry[2]->rc, 142, 0, 174, 32));
    SendMessageA(pView->m_hWnd, WM_LBUTTONUP, 0, MAKELPARAM(150, 10));
    CHECK(pView->m_pSelected == NULL);

    // move node 3 up into node 2: stopped at its bottom edge
    SendMessageA(pView->m_hWnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(140, 70));
    CHECK(pView->m_pSelected == apEntry[3]);
    SendMessageA(pView->m_hWnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(140, 50));
    CHECK(apEntry[3]->rc.top == 44);
    SendMessageA(pView->m_hWnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(140, 30));
    CHECK(SameRect(apEntry[3]->rc, 132, 32, 164, 64));
    POINT delta = { 0, -20 };
    pView->MoveNodeRectWithCollision(&apEntry[3]->rc, &delta);
    CHECK(delta.x == 0 && delta.y == 0 && apEntry[3]->rc.top == 32);
    SendMessageA(pView->m_hWnd, WM_LBUTTONUP, 0, 0);

    // pan on empty space
    SendMessageA(pView->m_hWnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(250, 250));
    CHECK(pView->m_bPanning && pView->m_pSelected == NULL);
    CHECK(GetCapture() == pView->m_hWnd);
    CHECK(pView->m_hCursor == LoadCursorA(NULL, IDC_HAND));
    SendMessageA(pView->m_hWnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(260, 255));
    CHECK(pView->m_nOriginX == -10 && pView->m_nOriginY == -5);
    pView->WorldToScreen(&pt, 0, 0);
    CHECK(pt.x == 10 && pt.y == 5);
    pView->ScreenToWorld(&pt, 10, 5);
    CHECK(pt.x == 0 && pt.y == 0);
    SendMessageA(pView->m_hWnd, WM_LBUTTONUP, 0, 0);
    CHECK(!pView->m_bPanning && GetCapture() == NULL);
    // no button: nothing moves
    SendMessageA(pView->m_hWnd, WM_MOUSEMOVE, 0, MAKELPARAM(0, 0));
    CHECK(pView->m_nOriginX == -10);
    pView->m_nOriginX = 0;
    pView->m_nOriginY = 0;

    CHECK(SendMessageA(pView->m_hWnd, WM_ERASEBKGND, 0, 0) == 1);
    CHECK(SendMessageA(pView->m_hWnd, WM_SETCURSOR, 0, 0) == 1);

    // paint into the back buffer: this server's label on yellow, cord 10 in white
    MoveWindow(pView->m_hWnd, 0, 0, 400, 300, FALSE);
    pView->m_bShowCert = true;
    SendMessageA(pView->m_hWnd, WM_PAINT, 0, 0);
    SIZE size;
    {
        CGdiScopedArialFont font(pView->m_hMemDC, 9);
        GetTextExtentPoint32A(pView->m_hMemDC, " 2-GatewayServer", 16, &size);
    }
    int xLabel = 158 - size.cx / 2;
    CHECK(GetPixel(pView->m_hMemDC, xLabel - 2, 34 + 8) == 0x00FFFF);
    CHECK(GetPixel(pView->m_hMemDC, xLabel - 4, 34 + 8) == 0);
    CHECK(GetPixel(pView->m_hMemDC, 80, (16 + 16) / 2) == 0xFFFFFF);
    pView->m_bShowDetail = true;
    SendMessageA(pView->m_hWnd, WM_PAINT, 0, 0);

    // a second rebuild keeps the entries and appends the children again (FWUI-006)
    SendMessageA(pView->m_hWnd, WM_SERVER_ARCH_LOADED, 0, 0);
    CHECK(apEntry[1]->listChild.size() == 4);
}

// ---------------------------------------------------------------------------------------------
// Monitor view

static void TestMoniterView(CServerMoniterView* pView)
{
    CHECK(strcmp(MoniterView_GetTypeNameByHiByte(0x01000000), "OSModule") == 0);
    CHECK(strcmp(MoniterView_GetTypeNameByHiByte(0x02010001), "NetEngine") == 0);
    CHECK(strcmp(MoniterView_GetTypeNameByHiByte(0x03000000), "ServerBody") == 0);
    CHECK(strcmp(MoniterView_GetTypeNameByHiByte(0x04000000), "Unknown") == 0);
    CHECK(MoniterView_GetTypeNameByHiByte(0x00FFFFFF) == NULL);

    CHECK(pView->m_fMaxValue == 120.0f && pView->m_nGridStep == 50 && pView->m_bShowLegend);
    CHECK(strcmp(pView->m_szValueLabel, "value") == 0 && strcmp(pView->m_szTimeLabel, "time") == 0);

    // the graphs keep pointers to the counters until the view is destroyed
    static MONITOR_COUNTER counter1;
    static MONITOR_COUNTER counter2;
    memset(&counter1, 0, sizeof(counter1));
    memset(&counter2, 0, sizeof(counter2));
    counter1.dwMaxSamples = 3;
    counter1.dwSampleCount = 9;
    counter2.dwMaxSamples = 100;
    CHECK(pView->AddGraph(0x2010001, "Msg", &counter1));
    CHECK(pView->AddGraph(0x3010002, "Job", &counter2));
    CHECK(!pView->AddGraph(0x2010001, "Again", &counter2));
    CHECK(pView->m_mapGraph.size() == 2 && counter1.dwSampleCount == 0);
    CMoniterGraphData* pGraph1 = pView->FindGraphData(0x2010001);
    CMoniterGraphData* pGraph2 = pView->FindGraphData(0x3010002);
    CHECK(pGraph1->m_clrLine == 0xFFFFFF && pGraph2->m_clrLine == 0x0000FF);
    CHECK(pView->m_nNextColor == 2);
    CHECK(pGraph1->m_fMin == FLT_MAX && pGraph1->m_fMax == -FLT_MAX && pGraph1->m_bVisible);

    // timer: one sample per graph, scale 9/8 of the largest maximum, at least 120
    g_ServerGlobals.bWritePerfLogToText = TRUE;
    DrainLog();
    s_Log.clear();
    counter1.fValue = 5.0f;
    counter2.fValue = 200.0f;
    SendMessageA(pView->m_hWnd, WM_TIMER, MONITER_TIMER_ID, 0);
    CHECK(pGraph1->m_listSample.size() == 1 && pGraph1->m_fCurrent == 5.0f);
    CHECK(counter1.dwSampleCount == 1);
    CHECK(pView->m_fMaxValue == 225.0f && pView->m_nGridStep == 75);
    DrainLog();
    CHECK(s_Log.size() == 1 && s_Log[0] == "PERFMON >> , Msg: 5.00, Job: 200.00");
    g_ServerGlobals.bWritePerfLogToText = FALSE;
    DrainLog();
    s_Log.clear();

    counter1.fValue = 2.0f;
    SendMessageA(pView->m_hWnd, WM_TIMER, MONITER_TIMER_ID, 0);
    counter1.fValue = 9.0f;
    SendMessageA(pView->m_hWnd, WM_TIMER, MONITER_TIMER_ID, 0);
    counter1.fValue = 7.0f;
    SendMessageA(pView->m_hWnd, WM_TIMER, MONITER_TIMER_ID, 0);
    DrainLog();
    CHECK(s_Log.empty());
    // three samples kept, oldest dropped
    CHECK(pGraph1->m_listSample.size() == 3 && pGraph1->m_listSample.front() == 2.0f);
    CHECK(pGraph1->m_fMin == 2.0f && pGraph1->m_fMax == 9.0f);
    CHECK(pGraph2->m_listSample.size() == 4);

    // grid in the back buffer, legend and graphs on top
    MoveWindow(pView->m_hWnd, 0, 0, 300, 200, TRUE);
    CHECK(pView->m_hFrontDC && pView->m_hGridDC);
    SendMessageA(pView->m_hWnd, WM_PAINT, 0, 0);
    CHECK(GetPixel(pView->m_hGridDC, 100, 100) == pView->m_clrBack);
    CHECK(GetPixel(pView->m_hGridDC, 35, 100) == pView->m_clrAxis);
    CHECK(GetPixel(pView->m_hFrontDC, 50, 5) == pView->m_clrBack);
    RECT rcClient;
    pView->GetClientRect(&rcClient);
    CHECK(pView->ValueToY(0.0f) == rcClient.bottom - 16);

    // remove: unknown IDs throw
    CHECK(pView->RemoveGraph(0x3010002));
    CHECK(pView->m_mapGraph.size() == 1);
    bool bThrown = false;
    try
    {
        pView->RemoveGraph(0x3010002);
    }
    catch (std::exception&)
    {
        bThrown = true;
    }
    CHECK(bThrown);

    CHECK(SendMessageA(pView->m_hWnd, WM_ERASEBKGND, 0, 0) == 1);
}

// ---------------------------------------------------------------------------------------------
// Server window

class CTestView : public CServerChildWindowBase
{
public:
    CTestView() : m_nDestroyed(0) { m_strTitle = "TestView"; }
    bool Create(HWND hParent)
    {
        CWindowBase::RegisterWindowClass(s_hInstance, "UiTestView");
        return CreateWin32Window(0, "UiTestView", NULL, WS_CHILD | WS_VISIBLE, hParent,
                                 s_hInstance, MakeRect(0, 0, 10, 10), NULL);
    }
    virtual bool Destroy()
    {
        ++m_nDestroyed;
        return DestroyWindowBase();
    }
    int m_nDestroyed;
};

static int LogItemCount(CServerFrameWindow* pFrame)
{
    return (int)SendMessageA(pFrame->m_hLogList, LVM_GETITEMCOUNT, 0, 0);
}

static void TestFrameWindow()
{
    strcpy_s(g_ServerGlobals.szServiceName, "ServerUiTest");
    g_ServerGlobals.hInstance = s_hInstance;
    SetUpArchitecture();

    CServerFrameWindow* pFrame = new CServerFrameWindow;
    g_pServerFrameWindow = pFrame;
    CHECK(pFrame->m_dwDumpFlags == 0);
    CHECK(pFrame->CreateFrame());
    CHECK(IsWindow(pFrame->m_hWnd));
    char szText[64];
    GetWindowTextA(pFrame->m_hWnd, szText, sizeof(szText));
    CHECK(strcmp(szText, "ServerUiTest") == 0);
    GetClassNameA(pFrame->m_hWnd, szText, sizeof(szText));
    CHECK(strcmp(szText, "ServerUiTest") == 0);
    CHECK(GetDlgCtrlID(pFrame->m_hLogList) == IDC_SERVER_LOG_LIST);
    CHECK(GetDlgCtrlID(pFrame->m_pArchView->m_hWnd) == IDC_SERVER_ARCH_VIEW);
    CHECK(GetDlgCtrlID(pFrame->m_pMoniterView->m_hWnd) == IDC_SERVER_MONITER_VIEW);
    CHECK(SendMessageA(pFrame->m_hLogList, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0) == 0x130);
    CHECK(SendMessageA(pFrame->m_hLogList, LVM_GETCOLUMNWIDTH, 0, 0) == 60);
    CHECK(SendMessageA(pFrame->m_hLogList, LVM_GETCOLUMNWIDTH, 1, 0) == 320);
    CHECK(SendMessageA(pFrame->m_hLogList, LVM_GETCOLUMNWIDTH, 2, 0) == 150);
    CHECK(pFrame->m_listChild.size() == 2 && pFrame->m_listMainView.size() == 1);
    CHECK(pFrame->m_listPane.size() == 2);
    CHECK(pFrame->m_listPane.front().fRatio == 0.2f
          && pFrame->m_listPane.front().hTop == pFrame->m_hMoniterView
          && pFrame->m_listPane.front().hBottom == pFrame->m_pArchView->m_hWnd);
    CHECK(pFrame->m_listPane.back().fRatio == 0.7f
          && pFrame->m_listPane.back().hTop == pFrame->m_hMainView
          && pFrame->m_listPane.back().hBottom == pFrame->m_hLogList);
    CHECK(pFrame->m_mapMsgHandler.size() == 10 && pFrame->m_mapCmdHandler.size() == 4);

    // log lines: level, text, time; at most 501 lines
    LogView_AppendLine_wrap(0x01000000, "hello");
    CHECK(LogItemCount(pFrame) == 1);
    CHECK(ListView_GetItemTextString(pFrame->m_hLogList, 0, 0) == "warnning");
    CHECK(ListView_GetItemTextString(pFrame->m_hLogList, 0, 1) == "hello");
    std::string strTime = ListView_GetItemTextString(pFrame->m_hLogList, 0, 2);
    CHECK(strTime.size() == 23 && strTime[4] == '-' && strTime[13] == ':' && strTime[19] == ' ');
    CHECK(ListView_GetItemParam(pFrame->m_hLogList, 0) == 0x01000000);
    for (int i = 0; i < 502; ++i)
    {
        char szLine[32];
        sprintf_s(szLine, "line %d", i);
        LogView_AppendLine_wrap((i % 4) << 24, szLine);
    }
    CHECK(LogItemCount(pFrame) == 501);
    CHECK(ListView_GetItemTextString(pFrame->m_hLogList, 0, 1) == "line 1");
    CHECK(ListView_GetItemTextString(pFrame->m_hLogList, 500, 1) == "line 501");

    // WM_NOTIFY custom draw: colours by level
    NMLVCUSTOMDRAW draw;
    memset(&draw, 0, sizeof(draw));
    draw.nmcd.hdr.hwndFrom = pFrame->m_hLogList;
    draw.nmcd.hdr.idFrom = IDC_SERVER_LOG_LIST;
    draw.nmcd.hdr.code = NM_CUSTOMDRAW;
    draw.nmcd.dwDrawStage = CDDS_PREPAINT;
    CHECK(SendMessageA(pFrame->m_hWnd, WM_NOTIFY, IDC_SERVER_LOG_LIST, (LPARAM)&draw)
          == CDRF_NOTIFYITEMDRAW);
    draw.nmcd.dwDrawStage = CDDS_ITEMPREPAINT;
    CHECK(SendMessageA(pFrame->m_hWnd, WM_NOTIFY, IDC_SERVER_LOG_LIST, (LPARAM)&draw)
          == CDRF_NOTIFYITEMDRAW);
    draw.nmcd.dwDrawStage = CDDS_ITEMPREPAINT | CDDS_SUBITEM;
    static const COLORREF s_aclr[4] = { 0, 0xB400B4, 0xFF, 0x808080 };
    for (int nLevel = 0; nLevel < 4; ++nLevel)
    {
        // line i has level i % 4; item 0 is line 1
        draw.nmcd.dwItemSpec = (nLevel + 3) % 4;
        draw.clrText = 1;
        draw.clrTextBk = 1;
        CHECK(SendMessageA(pFrame->m_hWnd, WM_NOTIFY, IDC_SERVER_LOG_LIST, (LPARAM)&draw) == 0);
        CHECK(draw.clrText == s_aclr[nLevel] && draw.clrTextBk == 0xFFFFFF);
    }
    LogView_AppendLine_wrap(0x80000000, "negative level");
    draw.nmcd.dwItemSpec = 500;
    CHECK(SendMessageA(pFrame->m_hWnd, WM_NOTIFY, IDC_SERVER_LOG_LIST, (LPARAM)&draw) == 0);
    CHECK(draw.clrText == 0x808080);
    draw.clrText = 1;
    CHECK(SendMessageA(pFrame->m_hWnd, WM_NOTIFY, 0x101, (LPARAM)&draw) == 0);
    CHECK(draw.clrText == 1);
    draw.nmcd.dwDrawStage = CDDS_POSTPAINT;
    CHECK(SendMessageA(pFrame->m_hWnd, WM_NOTIFY, IDC_SERVER_LOG_LIST, (LPARAM)&draw) == 0);

    // copy selected line, clear
    CHECK(pFrame->CopySelectedLogToClipboard() == -1);
    if (s_bIsolated)
    {
        LVITEMA item;
        memset(&item, 0, sizeof(item));
        item.stateMask = LVIS_SELECTED;
        item.state = LVIS_SELECTED;
        SendMessageA(pFrame->m_hLogList, LVM_SETITEMSTATE, 2, (LPARAM)&item);
        CHECK(pFrame->CopySelectedLogToClipboard() == 2);
        CHECK(OpenClipboard(NULL));
        HANDLE hData = GetClipboardData(CF_TEXT);
        CHECK(hData && strcmp((const char*)GlobalLock(hData), "line 4") == 0);
        if (hData)
            GlobalUnlock(hData);
        CloseClipboard();
    }
    pFrame->ClearLog();
    CHECK(LogItemCount(pFrame) == 0);

    // commands and messages
    CHECK(SendMessageA(pFrame->m_hWnd, WM_SERVER_FRAME_9000, 0, 1234) == 0);
    CHECK(s_lParam9000 == 1234);
    CHECK(SendMessageA(pFrame->m_hWnd, WM_ERASEBKGND, 0, 0) == 1);
    DrainLog();
    s_Log.clear();
    pFrame->m_dwDumpFlags = SERVER_DUMP_CUSTOM;
    CHECK(SendMessageA(pFrame->m_hWnd, WM_COMMAND, IDM_FRAME_DUMP, 0) == 0);
    DrainLog();
    CHECK(s_nCustomDumps == 1 && s_Log.size() == 1
          && s_Log[0] == "============== Custom Dump ================");
    pFrame->m_dwDumpFlags = 0;
    SendMessageA(pFrame->m_hWnd, WM_COMMAND, IDM_FRAME_DUMP, 0);
    CHECK(s_nCustomDumps == 1);
    SendMessageA(pFrame->m_hWnd, WM_COMMAND, IDM_FRAME_EXIT, 0);
    MSG msg;
    CHECK(PeekMessageA(&msg, pFrame->m_hWnd, WM_CLOSE, WM_CLOSE, PM_REMOVE));

    TestArchitectureView(pFrame->m_pArchView);
    TestMoniterView(pFrame->m_pMoniterView);

    // added view: menu item and switching through WM_COMMAND (the test supplies the menu)
    HMENU hMenu = CreateMenu();
    HMENU hFile = CreatePopupMenu();
    HMENU hView = CreatePopupMenu();
    HMENU hMain = CreatePopupMenu();
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFile, "File");
    AppendMenuA(hFile, MF_STRING, 1, "a");
    AppendMenuA(hFile, MF_STRING, 2, "b");
    AppendMenuA(hFile, MF_POPUP, (UINT_PTR)hView, "View");
    AppendMenuA(hView, MF_STRING, 3, "c");
    AppendMenuA(hView, MF_STRING, 4, "d");
    AppendMenuA(hView, MF_POPUP, (UINT_PTR)hMain, "MainView");
    AppendMenuA(hMain, MF_STRING, IDM_FRAME_ARCH_VIEW, "ArchitectureView");
    SetMenu(pFrame->m_hWnd, hMenu);

    CTestView* pTestView = new CTestView;
    CHECK(pTestView->Create(pFrame->m_hWnd));
    pFrame->AddChildView(pTestView);
    CHECK(!HasVisibleStyle(pTestView->m_hWnd));
    CHECK(pFrame->m_listChild.size() == 3 && pFrame->m_listMainView.back() == pTestView);
    CHECK(GetMenuItemCount(hMain) == 2);
    GetMenuStringA(hMain, IDM_FRAME_CHILD_VIEW, szText, sizeof(szText), MF_BYCOMMAND);
    CHECK(strcmp(szText, "TestView") == 0);

    SendMessageA(pFrame->m_hWnd, WM_COMMAND, IDM_FRAME_CHILD_VIEW, 0);
    CHECK(pFrame->m_hMainView == pTestView->m_hWnd);
    CHECK(HasVisibleStyle(pTestView->m_hWnd) && !HasVisibleStyle(pFrame->m_pArchView->m_hWnd));
    CHECK(GetMenuState(hMain, IDM_FRAME_CHILD_VIEW, MF_BYCOMMAND) & MF_CHECKED);
    CHECK(!(GetMenuState(hMain, IDM_FRAME_ARCH_VIEW, MF_BYCOMMAND) & MF_CHECKED));
    CHECK(pFrame->m_listPane.front().hBottom == pTestView->m_hWnd
          && pFrame->m_listPane.back().hTop == pTestView->m_hWnd);
    CHECK(pFrame->m_listPane.front().fRatio == 0.2f && pFrame->m_listPane.back().fRatio == 0.7f);
    RECT rc = ChildRect(pTestView->m_hWnd);
    RECT rcClient;
    pFrame->GetClientRect(&rcClient);
    CHECK(rc.top == (int)(0.2f * (float)rcClient.bottom));

    SendMessageA(pFrame->m_hWnd, WM_COMMAND, IDM_FRAME_ARCH_VIEW, 0);
    CHECK(pFrame->m_hMainView == pFrame->m_pArchView->m_hWnd);
    CHECK(!HasVisibleStyle(pTestView->m_hWnd) && HasVisibleStyle(pFrame->m_pArchView->m_hWnd));
    CHECK(GetMenuState(hMain, IDM_FRAME_ARCH_VIEW, MF_BYCOMMAND) & MF_CHECKED);

    // WM_SYSCOMMAND SC_CLOSE: no prompt while this server is not running (state 5), the window
    // closes
    g_ServerGlobals.bPromptAtClose = TRUE;
    s_aBody[1].nState = 1;
    HWND hFrame = pFrame->m_hWnd;
    HWND hArch = pFrame->m_pArchView->m_hWnd;
    HWND hMoniter = pFrame->m_pMoniterView->m_hWnd;
    SendMessageA(hFrame, WM_SYSCOMMAND, SC_CLOSE, 0);
    CHECK(!IsWindow(hFrame) && !IsWindow(hArch) && !IsWindow(hMoniter));
    bool bQuit = false;
    PumpMessages(&bQuit);
    CHECK(bQuit);
    g_ServerGlobals.bPromptAtClose = FALSE;

    // cleanup: every view destroyed and deleted; the monitor view stays in the window table
    // (FWUI-002)
    CServerMoniterView* pMoniter = pFrame->m_pMoniterView;
    CServerArchitectureView* pArch = pFrame->m_pArchView;
    CHECK(pFrame->Destroy());
    CHECK(pFrame->m_listChild.empty() && pFrame->m_listMainView.empty());
    CHECK(CountWindowEntries(pFrame) == 0 && CountWindowEntries(pArch) == 0);
    CHECK(CountWindowEntries(pMoniter) == 1);
    g_mapWindow.erase(hMoniter);
    CHECK(g_mapWindow.empty());
    delete pFrame;
    g_pServerFrameWindow = NULL;
    PumpMessages(NULL);

    UnregisterClassA("ServerUiTest", s_hInstance);
    UnregisterClassA("ServerArchitectureView", s_hInstance);
    UnregisterClassA("ServerMoniterView", s_hInstance);
    UnregisterClassA("UiTestView", s_hInstance);
}

int main()
{
    BSDebugSetFlags(0, 0);
    CUiTestApp app;
    g_pServerApp = &app;
    g_pBSNet = &app.m_NetEngine;
    LogCapture_Start();
    Isolate();
    if (!s_bIsolated)
        printf("ServerUiTest: no own window station/desktop, clipboard checks skipped\n");
    s_hInstance = GetModuleHandleA(NULL);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);
    // The first ShowWindow of a process may use the show command of the caller's STARTUPINFO
    // (for example SW_HIDE); use it up here.
    HWND hFirst = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 1, 1, NULL, NULL, s_hInstance,
                                  NULL);
    ShowWindow(hFirst, SW_SHOWNORMAL);
    DestroyWindow(hFirst);

    TestWindowBase();
    TestSizeRuler();
    TestGdiUtil();
    TestListViewUtil();
    TestPopupMenu();
    TestDialog();
    if (s_bIsolated)
        TestFrameWindow();
    else
        printf("ServerUiTest: server window tests skipped (they would show windows)\n");
    LogCapture_Stop();
    g_pBSNet = NULL;
    g_pServerApp = NULL;
    return TEST_RESULT("ServerUiTest");
}
