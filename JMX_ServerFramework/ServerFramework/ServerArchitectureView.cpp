#include "ServerArchitectureView.h"

#include <stdlib.h>
#include <string.h>
#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "ServerFramework/GdiUtil.h"
#include "ServerFramework/ServerUiExtern.h"

namespace ServerFramework
{

const ARCH_CORD_STYLE g_aArchCordStyle[5] =
{
    { 0x818181, PS_DOT,   1 },
    { 0xFFFFFF, PS_SOLID, 1 },
    { 0xFF0000, PS_SOLID, 1 },
    { 0x0000FF, PS_SOLID, 2 },
    { 0x008080, PS_SOLID, 1 },
};

namespace
{

char s_szNodeLabel[255];    // 0x53DE40

// Collision of a node moved by (dx, dy): the paths of the two leading corners are tested against
// the facing edge of the obstacle; on a hit the node is pushed back to that edge.
// ArchView_ClampRectOnMove (0x456aa0).
void ArchView_ClampRectOnMove(const RECT* pObstacle, RECT* pMoved, const RECT* pOrig, int dx,
                              int dy)
{
    if (dx > 0)
    {
        int x = pObstacle->left;
        if (Geom_SegmentsIntersect2D(pMoved->right, pMoved->top, pOrig->right, pOrig->top,
                                     x, pObstacle->top, x, pObstacle->bottom)
            || Geom_SegmentsIntersect2D(pMoved->right, pMoved->bottom, pOrig->right,
                                        pOrig->bottom, x, pObstacle->top, x, pObstacle->bottom))
        {
            if (pMoved->right > x)
                OffsetRect(pMoved, x - pMoved->right, 0);
        }
    }
    if (dx < 0)
    {
        int x = pObstacle->right;
        if (Geom_SegmentsIntersect2D(pMoved->left, pMoved->top, pOrig->left, pOrig->top,
                                     x, pObstacle->top, x, pObstacle->bottom)
            || Geom_SegmentsIntersect2D(pMoved->left, pMoved->bottom, pOrig->left,
                                        pOrig->bottom, x, pObstacle->top, x, pObstacle->bottom))
        {
            if (pMoved->left < pObstacle->right)
                OffsetRect(pMoved, pObstacle->right - pMoved->left, 0);
        }
    }
    if (dy > 0)
    {
        int y = pObstacle->top;
        if (Geom_SegmentsIntersect2D(pMoved->left, pMoved->bottom, pOrig->left, pOrig->bottom,
                                     pObstacle->left, y, pObstacle->right, y)
            || Geom_SegmentsIntersect2D(pMoved->right, pMoved->bottom, pOrig->right,
                                        pOrig->bottom, pObstacle->left, y, pObstacle->right, y))
        {
            if (pMoved->bottom > pObstacle->top)
                OffsetRect(pMoved, 0, pObstacle->top - pMoved->bottom);
        }
    }
    if (dy < 0)
    {
        int y = pObstacle->bottom;
        if (Geom_SegmentsIntersect2D(pMoved->left, pMoved->top, pOrig->left, pOrig->top,
                                     pObstacle->left, y, pObstacle->right, y)
            || Geom_SegmentsIntersect2D(pMoved->right, pMoved->top, pOrig->right, pOrig->top,
                                        pObstacle->left, y, pObstacle->right, y))
        {
            if (pMoved->top < pObstacle->bottom)
                OffsetRect(pMoved, 0, pObstacle->bottom - pMoved->top);
        }
    }
}

} // namespace

CServerArchitectureView::CServerArchitectureView()
{
    m_nNodeWidth = 32;
    m_nNodeHeight = 32;
    m_nGapX = 100;
    m_nGapY = 32;
    m_bShowCert = false;
    m_bShowCord = true;
    m_bShowName = true;
    m_bShowDetail = false;
    m_pRoot = NULL;
    m_nOriginX = 0;
    m_nOriginY = 0;
    m_bPanning = false;
    m_hCursor = LoadCursorA(NULL, IDC_ARROW);
    m_pSelected = NULL;
    for (int i = 0; i < ARCH_VIEW_ZOOM_LEVELS; ++i)
        m_afZoom[i] = 1.0f;
    m_nZoom = 15;

    // Not set by the original constructor.
    m_nLastX = 0;
    m_nLastY = 0;
    m_hMemDC = NULL;
    m_hMemBitmap = NULL;
    memset(m_ahStateIcon, 0, sizeof(m_ahStateIcon));
    m_hOverlayIcon = NULL;
}

CServerArchitectureView::~CServerArchitectureView()
{
}

bool CServerArchitectureView::Create(HINSTANCE hInstance, HWND hParent, RECT rc, HMENU hID)
{
    if (!hParent)
        BSAssertFail();
    if (!IsWindow(hParent))
        BSAssertFail();

    if (!RegisterWindowClass(hInstance, "ServerArchitectureView"))
        return false;
    if (!CreateWin32Window(WS_EX_CLIENTEDGE, "ServerArchitectureView", NULL,
                           WS_CHILD | WS_VISIBLE, hParent, hInstance, rc, hID))
        return false;

    InsertMsgHandler(WM_PAINT, &CServerArchitectureView::OnPaint);
    InsertMsgHandler(WM_LBUTTONDOWN, &CServerArchitectureView::OnLButtonDown);
    InsertMsgHandler(WM_LBUTTONUP, &CServerArchitectureView::OnLButtonUp);
    InsertMsgHandler(WM_MOUSEMOVE, &CServerArchitectureView::OnMouseMove);
    InsertMsgHandler(WM_ERASEBKGND, &CServerArchitectureView::OnEraseBkgnd);
    InsertMsgHandler(WM_SETCURSOR, &CServerArchitectureView::OnSetCursor);
    InsertMsgHandler(WM_RBUTTONUP, &CServerArchitectureView::OnRButtonUp);
    InsertMsgHandler(WM_SERVER_ARCH_LOADED, &CServerArchitectureView::OnArchLoaded);
    InsertMsgHandler(WM_SERVER_CORD_CHANGED, &CServerArchitectureView::OnCordChanged);
    InsertMsgHandler(WM_SERVER_BODY_CHANGED, &CServerArchitectureView::OnBodyChanged);
    Show(SW_SHOW);
    Update();

    HDC hDC = GetDC(m_hWnd);
    m_hMemDC = CreateCompatibleDC(hDC);
    m_hMemBitmap = CreateCompatibleBitmap(hDC, GetSystemMetrics(SM_CXSCREEN),
                                          GetSystemMetrics(SM_CYSCREEN));
    SelectObject(m_hMemDC, m_hMemBitmap);
    ReleaseDC(m_hWnd, hDC);

    HMODULE hRes = g_hServerFrameworkRes;
    m_ahStateIcon[0] = LoadIconA(hRes, MAKEINTRESOURCEA(IDI_ARCH_STATE0));
    m_ahStateIcon[1] = LoadIconA(g_hServerFrameworkRes, MAKEINTRESOURCEA(IDI_ARCH_STATE1));
    m_ahStateIcon[2] = LoadIconA(g_hServerFrameworkRes, MAKEINTRESOURCEA(IDI_ARCH_STATE2));
    m_ahStateIcon[3] = LoadIconA(g_hServerFrameworkRes, MAKEINTRESOURCEA(IDI_ARCH_STATE3));
    m_ahStateIcon[4] = LoadIconA(g_hServerFrameworkRes, MAKEINTRESOURCEA(IDI_ARCH_STATE4));
    m_ahStateIcon[5] = LoadIconA(g_hServerFrameworkRes, MAKEINTRESOURCEA(IDI_ARCH_STATE5));
    m_ahStateIcon[6] = LoadIconA(g_hServerFrameworkRes, MAKEINTRESOURCEA(IDI_ARCH_STATE6));
#ifdef JMX_FRAMEWORK_2007
    // FarmManager 0x441790: the icon of state 8 is the eighth.
    m_ahStateIcon[7] = LoadIconA(g_hServerFrameworkRes, MAKEINTRESOURCEA(IDI_ARCH_STATE8));
#else
    m_ahStateIcon[7] = LoadIconA(g_hServerFrameworkRes, MAKEINTRESOURCEA(IDI_ARCH_STATE7));
    m_ahStateIcon[8] = LoadIconA(g_hServerFrameworkRes, MAKEINTRESOURCEA(IDI_ARCH_STATE8));
#endif
    m_hOverlayIcon = LoadIconA(g_hServerFrameworkRes, MAKEINTRESOURCEA(IDI_ARCH_OVERLAY));
    g_hServerArchView = m_hWnd;
    return true;
}

bool CServerArchitectureView::Destroy()
{
    // BUG(FWUI-004): icons are freed with DeleteObject; the overlay handle is not cleared.
    for (int i = 0; i < ARCH_VIEW_ICON_COUNT; ++i)
    {
        DeleteObject(m_ahStateIcon[i]);
        m_ahStateIcon[i] = NULL;
    }
    DeleteObject(m_hOverlayIcon);
    if (m_hMemDC)
    {
        DeleteDC(m_hMemDC);
        DeleteObject(m_hMemBitmap);
    }
    m_mapNode.clear();
    m_mapCord.clear();
    return DestroyWindowBase();
}

LRESULT CServerArchitectureView::OnPaint(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    PAINTSTRUCT ps;
    HDC hDC = BeginPaint(m_hWnd, &ps);
    RECT rcClient;
    GetClientRect(&rcClient);
    COLORREF clrLine = GetSysColor(COLOR_3DFACE);
    COLORREF clrFill = GetSysColor(COLOR_3DFACE);
    Gdi_DrawRectFillOutline(m_hMemDC, &rcClient, clrFill, clrLine);

    ArchViewNodeMap::iterator it;
    for (it = m_mapNode.begin(); it != m_mapNode.end(); ++it)
    {
        if (!m_bShowCert)
            continue;
        ARCH_VIEW_ENTRY* pParent = FindItem(it->second.pBody->wParentID);
        if (!pParent)
            continue;
        const RECT& rc = it->second.rc;
        POINT ptFrom;
        POINT ptTo;
        WorldToScreen(&ptFrom, (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);
        WorldToScreen(&ptTo, (pParent->rc.left + pParent->rc.right) / 2,
                      (pParent->rc.top + pParent->rc.bottom) / 2);
        SetBkMode(m_hMemDC, TRANSPARENT);
        Gdi_DrawLine(m_hMemDC, 1, ptFrom.x, ptFrom.y, ptTo.x, ptTo.y, 0, PS_DASHDOTDOT);
    }

    if (m_bShowCord)
        DrawLinks(m_hMemDC);

    for (it = m_mapNode.begin(); it != m_mapNode.end(); ++it)
    {
        ARCH_VIEW_ENTRY* pEntry = &it->second;
        RECT rcScreen;
        RECT rcDst;
        WorldRectToScreen(&rcScreen, pEntry->rc.left, pEntry->rc.top, pEntry->rc.right,
                          pEntry->rc.bottom);
        if (IntersectRect(&rcDst, &rcClient, &rcScreen))
            DrawNode(m_hMemDC, pEntry);
    }

    if (m_pSelected)
    {
        RECT rc;
        WorldRectToScreen(&rc, m_pSelected->rc.left, m_pSelected->rc.top, m_pSelected->rc.right,
                          m_pSelected->rc.bottom);
        ArchView_DrawMarker6x6(m_hMemDC, rc.left - 3, rc.top - 3);
        ArchView_DrawMarker6x6(m_hMemDC, rc.right - 3, rc.top - 3);
        ArchView_DrawMarker6x6(m_hMemDC, rc.left - 3, rc.bottom - 3);
        ArchView_DrawMarker6x6(m_hMemDC, rc.right - 3, rc.bottom - 3);
    }

    BitBlt(hDC, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, m_hMemDC,
           0, 0, SRCCOPY);
    EndPaint(m_hWnd, &ps);
    return 0;
}

BOOL CServerArchitectureView::DrawNode(HDC hDC, ARCH_VIEW_ENTRY* pEntry)
{
    CGdiScopedArialFont font(hDC, 9);

    RECT rc;
    WorldRectToScreen(&rc, pEntry->rc.left, pEntry->rc.top, pEntry->rc.right, pEntry->rc.bottom);
    // BUG(FWUI-005): the body state indexes the 9 icons without a range check.
    DrawIcon(hDC, rc.left, rc.top, m_ahStateIcon[pEntry->pBody->nState]);
    if (pEntry->pParent)
        DrawIcon(hDC, rc.right - 2, rc.top - 15, m_hOverlayIcon);

    if (m_bShowName)
    {
        SERVER_BODY* pBody = pEntry->pBody;
        HRESULT hr;
        if (m_bShowDetail)
            hr = StringCchPrintfA(s_szNodeLabel, sizeof(s_szNodeLabel), "%2d-%s (%s)-(%s)",
                                  pBody->wID, pBody->pModule->szName,
                                  pBody->pMachine->szPublicIP, pBody->pMachine->szPrivateIP);
        else
            hr = StringCchPrintfA(s_szNodeLabel, sizeof(s_szNodeLabel), "%2d-%s", pBody->wID,
                                  pBody->pModule->szName);
        if (hr != S_OK)
            BSAssertFail();

        SIZE size;
        GetTextExtentPoint32A(hDC, s_szNodeLabel, (int)strlen(s_szNodeLabel), &size);
        RECT rcText;
        rcText.left = (rc.left + rc.right) / 2 - size.cx / 2;
        rcText.top = rc.top + 34;
        rcText.right = rcText.left + size.cx;
        rcText.bottom = rc.top + 50;
        RECT rcBox = rcText;
        InflateRect(&rcBox, 4, 1);
        if (g_pSelfBody == pEntry->pBody)
            Gdi_DrawRectFillOutline(hDC, &rcBox, 0xFFFF, 0);
        else
            Gdi_DrawRectFillOutline(hDC, &rcBox, 0xFFFFFF, 0);
        SetBkMode(hDC, TRANSPARENT);
        SetTextColor(hDC, 0);
        Gdi_DrawText(hDC, s_szNodeLabel, &rcText,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }
    return TRUE;
}

void CServerArchitectureView::DrawLinks(HDC hDC)
{
    SetBkMode(hDC, TRANSPARENT);
    for (ArchViewCordMap::iterator it = m_mapCord.begin(); it != m_mapCord.end(); ++it)
    {
        SERVER_CORD* pCord = it->second;
        ARCH_VIEW_ENTRY* pFrom = FindItem(pCord->wFromID);
        ARCH_VIEW_ENTRY* pTo = FindItem(pCord->wToID);
        if (!pFrom || !pTo)
            continue;

        POINT ptFrom;
        POINT ptTo;
        WorldToScreen(&ptFrom, (pFrom->rc.left + pFrom->rc.right) / 2,
                      (pFrom->rc.top + pFrom->rc.bottom) / 2);
        WorldToScreen(&ptTo, (pTo->rc.left + pTo->rc.right) / 2,
                      (pTo->rc.top + pTo->rc.bottom) / 2);
        // BUG(FWUI-005): the status indexes the 5-entry style table without a range check.
        const ARCH_CORD_STYLE* pStyle = &g_aArchCordStyle[pCord->dwStatus];
        if (m_bShowDetail)
        {
            SetTextColor(hDC, pStyle->clr);
            if (pCord->byPrivate)
                Gdi_TextOutf(hDC, (ptFrom.x + ptTo.x) / 2, (ptFrom.y + ptTo.y) / 2, 0, "%02dP",
                             pCord->dwID);
            else
                Gdi_TextOutf(hDC, (ptFrom.x + ptTo.x) / 2, (ptFrom.y + ptTo.y) / 2, 0, "%02d",
                             pCord->dwID);
        }
        Gdi_DrawLine(hDC, pStyle->nWidth, ptFrom.x, ptFrom.y, ptTo.x, ptTo.y, pStyle->clr,
                     pStyle->nPenStyle);
    }
}

ARCH_VIEW_ENTRY* CServerArchitectureView::FindItem(WORD wBodyID)
{
    ArchViewNodeMap::iterator it = m_mapNode.find(wBodyID);
    if (it == m_mapNode.end())
        return NULL;
    return &it->second;
}

bool CServerArchitectureView::RebuildTopology()
{
    ServerBodyList* pBodyList = g_pServerBodyList;
    for (ServerBodyList::iterator it = pBodyList->begin(); it != pBodyList->end(); ++it)
    {
        SERVER_BODY* pBody = *it;
        ARCH_VIEW_ENTRY entry;
        entry.pBody = pBody;
        entry.dw20 = 0;
        entry.pParent = (ARCH_VIEW_ENTRY*)(INT_PTR)(rand() % 2);
        SetRect(&entry.rc, 0, 0, m_nNodeWidth, m_nNodeHeight);
        // Existing entries are kept as they are.
        m_mapNode.insert(std::make_pair(pBody->wID, entry));
    }

    ServerCordList* pCordList = g_pServerCordList;
    for (ServerCordList::iterator it = pCordList->begin(); it != pCordList->end(); ++it)
        m_mapCord.insert(std::make_pair((*it)->dwID, *it));

    m_pRoot = NULL;
    for (ArchViewNodeMap::iterator it = m_mapNode.begin(); it != m_mapNode.end(); ++it)
    {
        ARCH_VIEW_ENTRY* pEntry = &it->second;
        if (pEntry->pBody->wParentID)
            pEntry->pParent = FindItem(pEntry->pBody->wParentID);
        // BUG(FWUI-006): children are appended again on every rebuild.
        for (ArchViewNodeMap::iterator itChild = m_mapNode.begin(); itChild != m_mapNode.end();
             ++itChild)
        {
            if (itChild->second.pBody->wParentID == pEntry->pBody->wID)
                pEntry->listChild.push_back(&itChild->second);
        }
    }

    for (ARCH_VIEW_ENTRY* p = FindItem(g_pSelfBody->wParentID); p;
         p = FindItem(p->pBody->wParentID))
    {
        m_pRoot = p;
    }
    if (!m_pRoot)
    {
        m_pRoot = FindItem(g_pSelfBody->wID);
        if (!m_pRoot)
            return false;
    }

    RECT rc;
    LayoutNodeTree(&rc, m_pRoot, 0, 0, 0);
    StackUnplacedNodes(&rc);
    Redraw(RDW_INVALIDATE | RDW_INTERNALPAINT);
    return true;
}

RECT* CServerArchitectureView::LayoutNodeTree(RECT* pOut, ARCH_VIEW_ENTRY* pEntry, int x, int y,
                                              int /*nUnused*/)
{
    pOut->left = 0;
    pOut->top = 0;
    pOut->right = 0;
    pOut->bottom = 0;
    if (!pEntry)
        return pOut;

    SetRect(&pEntry->rc, x, y, x + m_nNodeWidth, y + m_nNodeHeight);
    CopyRect(pOut, &pEntry->rc);
    int xChild = m_nNodeWidth + m_nGapX + x;
    for (std::list<ARCH_VIEW_ENTRY*>::iterator it = pEntry->listChild.begin();
         it != pEntry->listChild.end(); ++it)
    {
        RECT rcChild;
        LayoutNodeTree(&rcChild, *it, xChild, y, 0);
        y = m_nGapY + rcChild.bottom;
        if (rcChild.bottom - rcChild.top > m_nNodeHeight)
            y += m_nGapY;
        UnionRect(pOut, pOut, &rcChild);
    }
    return pOut;
}

RECT* CServerArchitectureView::StackUnplacedNodes(RECT* pOut)
{
    pOut->left = 0;
    pOut->top = 0;
    pOut->right = 0;
    pOut->bottom = 0;
    int x = FindItem(g_pSelfBody->wID)->rc.left;
    int y = -(m_nNodeHeight + m_nGapY);
    for (ArchViewNodeMap::iterator it = m_mapNode.begin(); it != m_mapNode.end(); ++it)
    {
        RECT* pRect = &it->second.rc;
        if (&it->second != m_pRoot && !pRect->left && !pRect->top)
        {
            SetRect(pRect, x, y, x + m_nNodeWidth, y + m_nNodeHeight);
            y -= m_nNodeHeight + m_nGapY;
        }
    }
    return pOut;
}

ARCH_VIEW_ENTRY* CServerArchitectureView::HitTestNode(POINT pt)
{
    for (ArchViewNodeMap::iterator it = m_mapNode.begin(); it != m_mapNode.end(); ++it)
    {
        ARCH_VIEW_ENTRY* pEntry = &it->second;
        RECT rc;
        WorldRectToScreen(&rc, pEntry->rc.left, pEntry->rc.top, pEntry->rc.right,
                          pEntry->rc.bottom);
        if (PtInRect(&rc, pt))
            return pEntry;
    }
    return NULL;
}

float CServerArchitectureView::GetZoomScale()
{
    if (m_nZoom < 0 || m_nZoom >= ARCH_VIEW_ZOOM_LEVELS)
        BSAssertFail();
    return m_afZoom[m_nZoom];
}

POINT* CServerArchitectureView::WorldToScreen(POINT* pOut, int x, int y)
{
    if (m_nZoom < 0 || m_nZoom >= ARCH_VIEW_ZOOM_LEVELS)
        BSAssertFail();
    pOut->x = (int)((double)(x - m_nOriginX) / m_afZoom[m_nZoom]);
    if (m_nZoom < 0 || m_nZoom >= ARCH_VIEW_ZOOM_LEVELS)
        BSAssertFail();
    pOut->y = (int)((double)(y - m_nOriginY) / m_afZoom[m_nZoom]);
    return pOut;
}

POINT* CServerArchitectureView::ScreenToWorld(POINT* pOut, int x, int y)
{
    if (m_nZoom < 0 || m_nZoom >= ARCH_VIEW_ZOOM_LEVELS)
        BSAssertFail();
    pOut->x = (int)(m_afZoom[m_nZoom] * (double)(x + m_nOriginX));
    if (m_nZoom < 0 || m_nZoom >= ARCH_VIEW_ZOOM_LEVELS)
        BSAssertFail();
    pOut->y = (int)((double)(y + m_nOriginY) * m_afZoom[m_nZoom]);
    return pOut;
}

RECT* CServerArchitectureView::WorldRectToScreen(RECT* pOut, int nLeft, int nTop, int nRight,
                                                 int nBottom)
{
    POINT pt;
    WorldToScreen(&pt, (nLeft + nRight) / 2, (nTop + nBottom) / 2);
    int x = pt.x - (nRight - nLeft) / 2;
    int y = pt.y - (nBottom - nTop) / 2;
    pOut->left = x;
    pOut->top = y;
    pOut->right = x + nRight - nLeft;
    pOut->bottom = nBottom - nTop + y;
    return pOut;
}

RECT* CServerArchitectureView::ScreenRectToWorldCentered(RECT* pOut, int nLeft, int nTop,
                                                         int nRight, int nBottom)
{
    POINT pt;
    ScreenToWorld(&pt, (nLeft + nRight) / 2, (nTop + nBottom) / 2);
    int x = pt.x - (nRight - nLeft) / 2;
    int y = pt.y - (nBottom - nTop) / 2;
    pOut->left = x;
    pOut->top = y;
    pOut->right = x + nRight - nLeft;
    pOut->bottom = nBottom - nTop + y;
    return pOut;
}

void CServerArchitectureView::MoveNodeRectWithCollision(RECT* pNode, POINT* pDelta)
{
    RECT rc = *pNode;
    OffsetRect(&rc, pDelta->x, pDelta->y);
    for (ArchViewNodeMap::iterator it = m_mapNode.begin(); it != m_mapNode.end(); ++it)
    {
        RECT* pOther = &it->second.rc;
        if (pOther == pNode)
            continue;
        RECT rcDst;
        if (IntersectRect(&rcDst, &rc, pOther))
            ArchView_ClampRectOnMove(pOther, &rc, pNode, pDelta->x, pDelta->y);
    }
    pDelta->x = rc.left - pNode->left;
    pDelta->y = rc.top - pNode->top;
    *pNode = rc;
}

LRESULT CServerArchitectureView::OnLButtonDown(WPARAM /*wParam*/, LPARAM lParam)
{
    POINT pt;
    pt.x = (short)LOWORD(lParam);
    pt.y = (short)HIWORD(lParam);
    m_nLastX = pt.x;
    m_nLastY = pt.y;
    ARCH_VIEW_ENTRY* pHit = HitTestNode(pt);
    m_pSelected = NULL;
    if (pHit)
    {
        m_hCursor = LoadCursorA(NULL, IDC_ARROW);
        m_pSelected = pHit;
    }
    else
    {
        SetCapture(m_hWnd);
        m_bPanning = true;
        m_hCursor = LoadCursorA(NULL, IDC_HAND);
    }
    Redraw(RDW_INVALIDATE | RDW_INTERNALPAINT);
    SetCursor(m_hCursor);
    return 0;
}

LRESULT CServerArchitectureView::OnLButtonUp(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    ReleaseCapture();
    m_pSelected = NULL;
    m_bPanning = false;
    m_hCursor = LoadCursorA(NULL, IDC_ARROW);
    SetCursor(m_hCursor);
    Redraw(RDW_INVALIDATE | RDW_INTERNALPAINT);
    return 0;
}

LRESULT CServerArchitectureView::OnMouseMove(WPARAM wParam, LPARAM lParam)
{
    if (!(wParam & MK_LBUTTON))
        return 0;

    int x = (short)LOWORD(lParam);
    int y = (short)HIWORD(lParam);
    POINT delta;
    delta.x = x - m_nLastX;
    delta.y = y - m_nLastY;
    if (m_bPanning)
    {
        if (m_nZoom < 0 || m_nZoom >= ARCH_VIEW_ZOOM_LEVELS)
            BSAssertFail();
        m_nOriginX -= (int)(m_afZoom[m_nZoom] * (double)delta.x);
        if (m_nZoom < 0 || m_nZoom >= ARCH_VIEW_ZOOM_LEVELS)
            BSAssertFail();
        m_nOriginY -= (int)(m_afZoom[m_nZoom] * (double)delta.y);
    }
    else if (m_pSelected)
    {
        delta.x = (int)(GetZoomScale() * (double)delta.x);
        delta.y = (int)(GetZoomScale() * (double)delta.y);
        MoveNodeRectWithCollision(&m_pSelected->rc, &delta);
    }
    m_nLastX = x;
    m_nLastY = y;
    if (delta.x || delta.y)
        Redraw(RDW_INVALIDATE | RDW_INTERNALPAINT);
    return 0;
}

LRESULT CServerArchitectureView::OnEraseBkgnd(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    return 1;
}

LRESULT CServerArchitectureView::OnSetCursor(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    SetCursor(m_hCursor);
    return 1;
}

LRESULT CServerArchitectureView::OnRButtonUp(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    HMENU hMenu = CreatePopupMenu();
    HMENU hView = CreatePopupMenu();
    AppendMenuA(hView, MF_STRING, IDM_ARCH_SHOW_NAME, "Show Server Module Name");
    AppendMenuA(hView, MF_STRING, IDM_ARCH_SHOW_CERT, "Show Certification Architecture");
    AppendMenuA(hView, MF_STRING, IDM_ARCH_SHOW_CORD, "Show Server Cord");
    AppendMenuA(hView, MF_STRING, IDM_ARCH_SHOW_DETAIL, "Show Detail");
    CheckMenuItem(hView, IDM_ARCH_SHOW_NAME, m_bShowName ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hView, IDM_ARCH_SHOW_CERT, m_bShowCert ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hView, IDM_ARCH_SHOW_CORD, m_bShowCord ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hView, IDM_ARCH_SHOW_DETAIL, m_bShowDetail ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hView, "View");
    AppendMenuA(hMenu, MF_STRING, IDM_ARCH_REARRANGE, "Rearrange Items");

    POINT ptScreen;
    GetCursorPos(&ptScreen);
    POINT pt = ptScreen;
    ScreenToClient(m_hWnd, &pt);
    if (HitTestNode(pt))
    {
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(hMenu, MF_STRING, IDM_ARCH_PROPERTY, "Property");
    }

    int nCmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, ptScreen.x, ptScreen.y, 0, m_hWnd, NULL);
    if (nCmd <= IDM_ARCH_REARRANGE)
    {
        bool bRedraw = true;
        if (nCmd == IDM_ARCH_REARRANGE)
        {
            RECT rc;
            LayoutNodeTree(&rc, m_pRoot, 0, 0, 0);
            StackUnplacedNodes(&rc);
            m_nOriginX = 0;
            m_nOriginY = 0;
        }
        else if (nCmd == IDM_ARCH_SHOW_NAME)
            m_bShowName = !m_bShowName;
        else if (nCmd == IDM_ARCH_SHOW_CERT)
            m_bShowCert = !m_bShowCert;
        else if (nCmd == IDM_ARCH_SHOW_CORD)
            m_bShowCord = !m_bShowCord;
        else if (nCmd == IDM_ARCH_SHOW_DETAIL)
            m_bShowDetail = !m_bShowDetail;
        else
            bRedraw = false;
        if (bRedraw)
            Redraw(RDW_INVALIDATE | RDW_INTERNALPAINT);
    }
    DestroyMenu(hView);
    DestroyMenu(hMenu);
    return 0;
}

LRESULT CServerArchitectureView::OnArchLoaded(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    RebuildTopology();
    return 0;
}

LRESULT CServerArchitectureView::OnCordChanged(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    Redraw(RDW_INVALIDATE | RDW_INTERNALPAINT);
    return 0;
}

LRESULT CServerArchitectureView::OnBodyChanged(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    Redraw(RDW_INVALIDATE | RDW_INTERNALPAINT);
    return 0;
}

} // namespace ServerFramework
