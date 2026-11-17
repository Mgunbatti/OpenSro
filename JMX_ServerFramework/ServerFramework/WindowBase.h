#pragma once

#include <windows.h>
#include <hash_map>
#include <list>
#include <map>
#include <string>

// Window classes of the server monitor GUI. Every window object keeps two handler tables:
// window messages and WM_COMMAND IDs, each mapped to a member function of the object.

namespace ServerFramework
{

class __single_inheritance CWindowBase;

// Message handler: called with the object as this, returns the message result.
typedef LRESULT (CWindowBase::*PFN_WINDOW_HANDLER)(WPARAM wParam, LPARAM lParam);
C_ASSERT(sizeof(PFN_WINDOW_HANDLER) == 4);

typedef stdext::hash_map<UINT, PFN_WINDOW_HANDLER> WindowHandlerMap;

// Layout anchor of a child window (added by WindowLayout_AddAnchor). Flags 0x10 / 0x20 / 0x40 /
// 0x80 make the left / right / top / bottom distance proportional to the parent size.
struct WINDOW_ANCHOR
{
    HWND    hWnd;       // +0
    DWORD   dwFlags;    // +4
    float   fLeft;      // +8
    float   fRight;     // +C
    float   fTop;       // +10
    float   fBottom;    // +14
};
C_ASSERT(sizeof(WINDOW_ANCHOR) == 24);

class CWindowBase
{
public:
    CWindowBase();
    virtual ~CWindowBase();

    // slot 1: routes WM_COMMAND to the command table and everything else to the message table.
    // WM_DESTROY of a window that is not a dialog also posts WM_QUIT.
    virtual LRESULT WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    // CWin32Window::Create (0x45de80): CreateWindowExA with a CREATESTRUCT copy as parameter,
    // stores the handle and registers the object for the window procedure.
    bool CreateWin32Window(DWORD dwExStyle, LPCSTR pszClassName, LPCSTR pszWindowName,
                           DWORD dwStyle, HWND hParent, HINSTANCE hInstance, RECT rc,
                           HMENU hMenu);
    // CWindowBase::Destroy (0x45dfc0): DestroyWindow and unregisters the handle. Returns false.
    bool DestroyWindowBase();

    BOOL Show(int nCmdShow);
    BOOL Update();
    BOOL Redraw(UINT uFlags);
    RECT* GetWindowRect(RECT* pRect);
    RECT* GetClientRect(RECT* pRect);
    // Window rectangle of the parent, or the screen size when there is none.
    RECT* GetParentRectOrScreen(RECT* pRect);
    BOOL CenterOnParent();

    // WindowLayout_AddAnchor (0x45e3d0, not referenced): records the child's distances to the
    // client area (flags 1/2/4/8 keep left/right/top/bottom, 0x10..0x80 make them proportional).
    void AddAnchor(HWND hChild, BYTE byFlags);
    // Window_LayoutAnchoredChildren (0x45e540, not referenced): moves the anchored children.
    void LayoutAnchors();
    // Ctrl_GetWindowTextString (0x465840, not referenced): the window text (259 characters at
    // most).
    std::string* GetWindowTextString(std::string* pText);

    // Handler registration. Asserts when the ID already has a handler. Command IDs are 16 bit;
    // the dispatcher looks up the whole wParam.
    void InsertMsgHandler(UINT uMsg, PFN_WINDOW_HANDLER pfn);
    void InsertCommandHandler(WORD wID, PFN_WINDOW_HANDLER pfn);

    template <typename T>
    void InsertMsgHandler(UINT uMsg, LRESULT (T::*pfn)(WPARAM, LPARAM))
    {
        InsertMsgHandler(uMsg, static_cast<PFN_WINDOW_HANDLER>(pfn));
    }
    template <typename T>
    void InsertCommandHandler(WORD wID, LRESULT (T::*pfn)(WPARAM, LPARAM))
    {
        InsertCommandHandler(wID, static_cast<PFN_WINDOW_HANDLER>(pfn));
    }

    // Unhandled: DefWindowProcA, or 0 for dialogs.
    LRESULT DispatchMessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam);
    // Unhandled: 0.
    LRESULT DispatchCommandHandler(WPARAM wParam, LPARAM lParam);

    // RegisterClassA with CS_HREDRAW | CS_VREDRAW, background brush 5 and StaticWndProc.
    static bool RegisterWindowClass(HINSTANCE hInstance, LPCSTR pszClassName);
    static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    HWND GetHWnd() const { return m_hWnd; }

public:
    CWindowBase*                m_pParent;          // +4   set by CWin32Dialog
    std::list<WINDOW_ANCHOR>    m_listAnchor;       // +8
    HWND                        m_hWnd;             // +20
    HINSTANCE                   m_hInstance;        // +24  dialogs: template ID
    BYTE                        m_bDialog;          // +28
    WindowHandlerMap            m_mapMsgHandler;    // +32
    WindowHandlerMap            m_mapCmdHandler;    // +72
};

C_ASSERT(FIELD_OFFSET(CWindowBase, m_listAnchor) == 8);
C_ASSERT(FIELD_OFFSET(CWindowBase, m_hWnd) == 20);
C_ASSERT(FIELD_OFFSET(CWindowBase, m_mapMsgHandler) == 32);
C_ASSERT(FIELD_OFFSET(CWindowBase, m_mapCmdHandler) == 72);
C_ASSERT(sizeof(CWindowBase) == 112);

// Handle -> object table of StaticWndProc (0x53B9D8).
extern std::map<HWND, CWindowBase*> g_mapWindow;

// Message handlers that run before any window (0x53B9E4), see WindowHandlerList_Dispatch.
struct WINDOW_GLOBAL_HANDLER
{
    CWindowBase*        pTarget;
    PFN_WINDOW_HANDLER  pfn;
};
extern stdext::hash_map<UINT, WINDOW_GLOBAL_HANDLER> g_hmapWindowHandler;

// WindowHandlerList_Dispatch (0x45dbf0): calls the global handler of the message and returns its
// result, or -1 when there is none. The message loop and StaticWndProc call it first.
LRESULT WindowHandlerList_Dispatch(UINT uMsg, WPARAM wParam, LPARAM lParam);
// WindowHandlerMap_Insert (0x45e300): asserts when the message already has a handler.
void WindowHandlerMap_Insert(CWindowBase* pTarget, UINT uMsg, PFN_WINDOW_HANDLER pfn);

// Font for the user's language (SimSun, PMingliu, MS Gothic or Gulim), sent with WM_SETFONT.
LRESULT Win32Dialog_SetLocaleFont(HWND hWnd, int nHeight);
// Face name used by the server window and dialogs for the user's language.
void Win32_GetLocaleFontName(char* pszFace, size_t cchFace);

// Child controls created through CWin32Window::Create with WS_CHILD | WS_VISIBLE added.
bool Win32Ctrl_CreateStatic(CWindowBase* pCtrl, HINSTANCE hInstance, HWND hParent, HMENU hID,
                            DWORD dwExStyle, DWORD dwStyle, int x, int y, int cx, int cy);
bool Win32Ctrl_CreateEdit(CWindowBase* pCtrl, HINSTANCE hInstance, HWND hParent, HMENU hID,
                          DWORD dwExStyle, DWORD dwStyle, int x, int y, int cx, int cy);
bool Win32Ctrl_CreateButton(CWindowBase* pCtrl, HINSTANCE hInstance, HWND hParent, HMENU hID,
                            DWORD dwExStyle, DWORD dwStyle, int x, int y, int cx, int cy);
BOOL Win32Ctrl_SetTextInt(CWindowBase* pCtrl, int nValue);

} // namespace ServerFramework
