#pragma once

#include <list>

#include "ServerFramework/Win32SizeRuler.h"

namespace ServerFramework
{

class CServerChildWindowBase;
class CServerArchitectureView;
class CServerMoniterView;

// Resource and control IDs of the server window. The menu (ID 0x65 in ServerFrameworkRes.dll)
// must provide the commands below and, at submenu 0 / 2, a popup the "MainView" submenu is
// appended to.
enum
{
    IDR_SERVER_FRAME_MENU   = 0x65,
    IDC_SERVER_LOG_LIST     = 0x100,
    IDC_SERVER_ARCH_VIEW    = 0x101,
    IDC_SERVER_MONITER_VIEW = 0x102,

    IDM_FRAME_EXIT          = 0x9C41,   // posts WM_CLOSE
    IDM_FRAME_ABOUT         = 0x9C43,   // empty message box
    IDM_FRAME_DUMP          = 0x9C44,   // diagnostic dump
    IDM_FRAME_ARCH_VIEW     = 0xBB9,    // "ArchitectureView" of the MainView submenu
    IDM_FRAME_CHILD_VIEW    = 0xBBA,    // menu item of an added view

    // log list popup (shown without TPM_RETURNCMD, see FWUI-001)
    IDM_LOG_SAVE_CLEAR      = 0x100,
    IDM_LOG_SAVE            = 0x101,
    IDM_LOG_CLEAR           = 0x102,
    IDM_LOG_COPY            = 0x103,

    WM_SERVER_FRAME_9000    = 0x9000,   // passed to the server application
    SERVER_FRAME_TIMER_ID   = 0,
    SERVER_LOG_MAX_ITEMS    = 500,
};

// Diagnostic dump flags (m_dwDumpFlags).
enum
{
    SERVER_DUMP_ENGINE_VERSION  = 0x1,
    SERVER_DUMP_MSG_USAGE       = 0x2,
    SERVER_DUMP_CUSTOM          = 0x4,
};

typedef std::list<CServerChildWindowBase*> ServerChildWindowList;

// Main window of a server module in window mode: monitor view, main view (architecture view
// or an added view) and the log list, separated by two splitters.
class CServerFrameWindow : public CWin32SizeRuler
{
public:
    CServerFrameWindow();
    virtual ~CServerFrameWindow();

    // 9: window (class and title: the service name), menu, log list, both views, splitters.
    virtual bool CreateFrame();
    // 10: frees the views and destroys the window. Returns true.
    virtual bool Destroy();
    virtual LRESULT OnAbout(WPARAM wParam, LPARAM lParam);             // 11  IDM_FRAME_ABOUT
    virtual LRESULT OnExit(WPARAM wParam, LPARAM lParam);              // 12  IDM_FRAME_EXIT
    virtual LRESULT OnDump(WPARAM wParam, LPARAM lParam);              // 13  IDM_FRAME_DUMP
    virtual LRESULT OnNotify(WPARAM wParam, LPARAM lParam);            // 14  WM_NOTIFY
    virtual LRESULT OnEraseBkgnd(WPARAM wParam, LPARAM lParam);        // 15
    virtual LRESULT OnSysCommand(WPARAM wParam, LPARAM lParam);        // 16
    virtual LRESULT OnArchViewCommand(WPARAM wParam, LPARAM lParam);   // 17  first view
    virtual LRESULT OnChildViewCommand(WPARAM wParam, LPARAM lParam);  // 18  last view
    virtual LRESULT OnMessage9000(WPARAM wParam, LPARAM lParam);       // 19

    // CServerFrameWindow_AddChildViewMenuItem (0x45c3a0): adds a view to the window (hidden)
    // and a menu item that switches to it.
    BOOL AddChildView(CServerChildWindowBase* pView);
    // CServerFrameWindow::SwitchView (0x45c460)
    void SwitchView(CServerChildWindowBase* pView);
    // ServerFrameWindow_ResetSplitPanes (0x45c2c0): rebuilds both splitters for the current
    // main view, keeping their positions.
    void ResetSplitPanes();
    // ListView_InsertColumnA (0x45bb70)
    LRESULT InsertLogColumn(int nColumn, const char* pszText, int nWidth);
    // LogView_AppendLine (0x45bbe0): appends a line (level, text, time) to the log list of
    // g_pServerFrameWindow, dropping the first line beyond 500.
    static LRESULT AppendLogLine(const char* pszText, DWORD dwFlags);
    // SendMessageA_wrap (0x45cd40)
    LRESULT ClearLog();
    // FrameWindow_CopySelectedLogToClipboard (0x45cd60)
    LRESULT CopySelectedLogToClipboard();

public:
    HWND                    m_hLogList;         // +140
    HFONT                   m_hFont;            // +144
    HANDLE                  m_hThread;          // +148 terminated by Destroy
    ServerChildWindowList   m_listChild;        // +152 owned views
    ServerChildWindowList   m_listMainView;     // +164 views selectable as main view
    HWND                    m_hMoniterView;     // +176
    HWND                    m_hMainView;        // +180
    HWND                    m_hLogListPane;     // +184
    CServerMoniterView*     m_pMoniterView;     // +188
    CServerArchitectureView* m_pArchView;       // +192
    DWORD                   m_dwDumpFlags;      // +196
};

C_ASSERT(FIELD_OFFSET(CServerFrameWindow, m_hLogList) == 140);
C_ASSERT(FIELD_OFFSET(CServerFrameWindow, m_listChild) == 152);
C_ASSERT(FIELD_OFFSET(CServerFrameWindow, m_listMainView) == 164);
C_ASSERT(FIELD_OFFSET(CServerFrameWindow, m_hMoniterView) == 176);
C_ASSERT(FIELD_OFFSET(CServerFrameWindow, m_pMoniterView) == 188);
C_ASSERT(FIELD_OFFSET(CServerFrameWindow, m_dwDumpFlags) == 196);
C_ASSERT(sizeof(CServerFrameWindow) == 0xC8);

// CServerFrameWindow_CreateListView (0x45df70)
HWND CServerFrameWindow_CreateListView(CServerFrameWindow* pFrame, HINSTANCE hInstance, RECT rc);

} // namespace ServerFramework

// 0x4F9CF8: the server window while ServerApp_RunWindowMode runs.
extern ServerFramework::CServerFrameWindow* g_pServerFrameWindow;

// LogView_AppendLine_wrap (0x453610): log callback of the server application in window mode.
int LogView_AppendLine_wrap(DWORD dwFlags, const char* pszText);
