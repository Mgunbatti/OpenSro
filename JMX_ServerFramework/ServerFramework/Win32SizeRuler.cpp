#include "Win32SizeRuler.h"

#include <math.h>
#include <stdlib.h>

#include "ServerFramework/GdiUtil.h"

namespace ServerFramework
{

CWin32SizeRuler::CWin32SizeRuler()
{
    m_pDragPane = NULL;
}

CWin32SizeRuler::~CWin32SizeRuler()
{
}

bool CWin32SizeRuler::Create(DWORD dwExStyle, LPCSTR pszClassName, LPCSTR pszWindowName,
                             DWORD dwStyle, RECT rc, HWND hParent, HMENU hMenu,
                             HINSTANCE hInstance)
{
    bool bResult = CreateWin32Window(dwExStyle, pszClassName, pszWindowName, dwStyle, hParent,
                                     hInstance, rc, hMenu);
    if (bResult)
    {
        InsertMsgHandler(WM_SIZE, &CWin32SizeRuler::OnSize);
        InsertMsgHandler(WM_LBUTTONDOWN, &CWin32SizeRuler::OnLButtonDown);
        InsertMsgHandler(WM_MOUSEMOVE, &CWin32SizeRuler::OnMouseMove);
        InsertMsgHandler(WM_LBUTTONUP, &CWin32SizeRuler::OnLButtonUp);
        InsertMsgHandler(WM_PAINT, &CWin32SizeRuler::OnPaint);
        InsertMsgHandler(WM_SETCURSOR, &CWin32SizeRuler::OnSetCursor);
        m_hCursor = LoadCursorA(NULL, IDC_ARROW);
    }
    return bResult;
}

void CWin32SizeRuler::AddSplitPane(HWND hTop, HWND hBottom, float fRatio)
{
    SPLIT_PANE pane;
    pane.fRatio = fRatio;
    pane.hTop = hTop;
    pane.hBottom = hBottom;
    m_listPane.push_back(pane);
}

SPLIT_PANE* CWin32SizeRuler::HitTestSplitterAndCapture(int y)
{
    RECT rc;
    GetClientRect(&rc);
    for (std::list<SPLIT_PANE>::iterator it = m_listPane.begin(); it != m_listPane.end(); ++it)
    {
        int nSplitY = (int)((double)(rc.bottom - rc.top) * it->fRatio - 2.0);
        if (abs(nSplitY - y) <= 4)
        {
            SetCapture(m_hWnd);
            return &*it;
        }
    }
    ReleaseCapture();
    return NULL;
}

void CWin32SizeRuler::LayoutPanes(int nWidth, int nHeight)
{
    SPLIT_PANE* pPrev = NULL;
    for (std::list<SPLIT_PANE>::iterator it = m_listPane.begin(); it != m_listPane.end(); ++it)
    {
        SPLIT_PANE* pPane = &*it;
        if (!pPrev)
        {
            MoveWindow(pPane->hTop, 0, 0, nWidth, (int)((double)nHeight * pPane->fRatio - 4.0),
                       TRUE);
        }
        else
        {
            float fHeight = (float)nHeight;
            int nTop = (int)(pPrev->fRatio * fHeight);
            int nBottom = (int)(fHeight * pPane->fRatio);
            MoveWindow(pPrev->hBottom, 0, nTop, nWidth, nBottom - nTop - 4, TRUE);
        }
        pPrev = pPane;
    }
    if (pPrev)
    {
        int nTop = (int)((double)nHeight * pPrev->fRatio);
        MoveWindow(pPrev->hBottom, 0, nTop, nWidth, nHeight - nTop, TRUE);
    }
}

LRESULT CWin32SizeRuler::OnPaint(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    PAINTSTRUCT ps;
    HDC hDC = BeginPaint(m_hWnd, &ps);
    RECT rc;
    GetClientRect(&rc);
    for (std::list<SPLIT_PANE>::iterator it = m_listPane.begin(); it != m_listPane.end(); ++it)
    {
        SPLIT_PANE* pPane = &*it;
        int y = (int)((double)(rc.bottom - rc.top) * pPane->fRatio - 2.0);
        if (pPane == m_pDragPane)
            Gdi_DrawLine(hDC, 4, 0, y, rc.right - rc.left, y, 0xFF0000, PS_SOLID);
        else
            Gdi_DrawLine(hDC, 4, 0, y, rc.right - rc.left, y, GetSysColor(COLOR_3DFACE),
                         PS_SOLID);
    }
    EndPaint(m_hWnd, &ps);
    return 0;
}

LRESULT CWin32SizeRuler::OnSize(WPARAM /*wParam*/, LPARAM lParam)
{
    if (m_listPane.size())
        LayoutPanes(LOWORD(lParam), HIWORD(lParam));
    return 0;
}

LRESULT CWin32SizeRuler::OnLButtonDown(WPARAM /*wParam*/, LPARAM lParam)
{
    m_nLastX = (short)LOWORD(lParam);
    m_nLastY = (short)HIWORD(lParam);
    m_pDragPane = HitTestSplitterAndCapture((short)HIWORD(lParam));
    if (m_pDragPane)
    {
        SetCapture(m_hWnd);
        Redraw(RDW_INVALIDATE);
    }
    return 0;
}

LRESULT CWin32SizeRuler::OnLButtonUp(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    ReleaseCapture();
    if (m_pDragPane)
    {
        m_hCursor = LoadCursorA(NULL, IDC_ARROW);
        SetCursor(m_hCursor);
        Redraw(RDW_INVALIDATE);
    }
    m_pDragPane = NULL;
    return 0;
}

LRESULT CWin32SizeRuler::OnMouseMove(WPARAM wParam, LPARAM lParam)
{
    if (!(wParam & MK_LBUTTON) || !m_pDragPane)
    {
        // Hovering over a splitter also takes the mouse capture (see HitTestSplitterAndCapture).
        if (HitTestSplitterAndCapture((short)HIWORD(lParam)))
            m_hCursor = LoadCursorA(NULL, IDC_SIZENS);
        else
            m_hCursor = LoadCursorA(NULL, IDC_ARROW);
        SetCursor(m_hCursor);
        return 0;
    }

    RECT rc;
    GetClientRect(&rc);
    int nHeight = rc.bottom - rc.top;
    int x = (short)LOWORD(lParam);
    int y = (short)HIWORD(lParam);
    float fHeight = (float)nHeight;
    float fRatio = (float)((double)y / fHeight);
    float fMargin = (float)(35.0 / fHeight);

    // Keep 35 pixels to the window edges and to the neighbouring splitters.
    float fPos = fRatio;
    if (fMargin > fPos)
        fPos = fMargin;
    if ((double)fPos > 1.0 - fMargin)
        fPos = (float)(1.0 - fMargin);

    for (std::list<SPLIT_PANE>::iterator it = m_listPane.begin(); it != m_listPane.end(); ++it)
    {
        SPLIT_PANE* pPane = &*it;
        if (pPane == m_pDragPane)
            continue;
        if (pPane->hBottom == m_pDragPane->hTop && pPane->fRatio > (double)fPos - fMargin)
            fPos = (float)((double)pPane->fRatio + fMargin);
        if (pPane->hTop == m_pDragPane->hBottom && pPane->fRatio < (double)fMargin + fPos)
            fPos = (float)((double)pPane->fRatio - fMargin);
    }

    int nWidth = rc.right - rc.left;
    float fDiff = (float)fabs((float)(m_pDragPane->fRatio - fPos));
    bool bChanged = fDiff > 1.0 / (double)nWidth;
    m_pDragPane->fRatio = fPos;
    m_nLastX = x;
    m_nLastY = y;
    if (bChanged)
    {
        LayoutPanes(nWidth, nHeight);
        Redraw(RDW_INVALIDATE);
    }
    return 0;
}

LRESULT CWin32SizeRuler::OnSetCursor(WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcA(m_hWnd, WM_SETCURSOR, wParam, lParam);
}

} // namespace ServerFramework
