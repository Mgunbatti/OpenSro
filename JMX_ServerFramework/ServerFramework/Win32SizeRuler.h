#pragma once

#include "ServerFramework/WindowBase.h"

namespace ServerFramework
{

// Horizontal splitter between two child windows. The ratio is the splitter position as a
// fraction of the client height.
struct SPLIT_PANE
{
    float   fRatio;     // +0
    HWND    hTop;       // +4
    HWND    hBottom;    // +8
};
C_ASSERT(sizeof(SPLIT_PANE) == 12);

// Window whose client area is split vertically into child windows by draggable splitters.
class CWin32SizeRuler : public CWindowBase
{
public:
    CWin32SizeRuler();
    virtual ~CWin32SizeRuler();

    // slot 2: creates the window and installs the splitter handlers.
    virtual bool Create(DWORD dwExStyle, LPCSTR pszClassName, LPCSTR pszWindowName,
                        DWORD dwStyle, RECT rc, HWND hParent, HMENU hMenu,
                        HINSTANCE hInstance);
    virtual LRESULT OnPaint(WPARAM wParam, LPARAM lParam);          // 3  WM_PAINT
    virtual LRESULT OnSize(WPARAM wParam, LPARAM lParam);           // 4  WM_SIZE
    virtual LRESULT OnLButtonDown(WPARAM wParam, LPARAM lParam);    // 5
    virtual LRESULT OnLButtonUp(WPARAM wParam, LPARAM lParam);      // 6
    virtual LRESULT OnMouseMove(WPARAM wParam, LPARAM lParam);      // 7
    virtual LRESULT OnSetCursor(WPARAM wParam, LPARAM lParam);      // 8

    // STL_list_PushBack_SplitPaneTriple (0x464c30)
    void AddSplitPane(HWND hTop, HWND hBottom, float fRatio);
    // CWin32SizeRuler::LayoutPanes (0x464d30): moves every pane window to its band.
    void LayoutPanes(int nWidth, int nHeight);
    // CWin32SizeRuler::HitTestSplitterAndCapture (0x464c80): the splitter within 4 pixels of y
    // gets the mouse capture and is returned; otherwise the capture is released.
    SPLIT_PANE* HitTestSplitterAndCapture(int y);

public:
    std::list<SPLIT_PANE>   m_listPane;     // +112
    SPLIT_PANE*             m_pDragPane;    // +124
    int                     m_nLastX;       // +128
    int                     m_nLastY;       // +132
    HCURSOR                 m_hCursor;      // +136
};

C_ASSERT(FIELD_OFFSET(CWin32SizeRuler, m_listPane) == 112);
C_ASSERT(FIELD_OFFSET(CWin32SizeRuler, m_pDragPane) == 124);
C_ASSERT(FIELD_OFFSET(CWin32SizeRuler, m_hCursor) == 136);
C_ASSERT(sizeof(CWin32SizeRuler) == 140);

} // namespace ServerFramework
