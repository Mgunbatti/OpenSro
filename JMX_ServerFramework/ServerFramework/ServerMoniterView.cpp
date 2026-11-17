#include "ServerMoniterView.h"

#include <float.h>
#include <string.h>
#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "BSNet/NetEngineLog.h"
#include "ServerFramework/GdiUtil.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"

namespace ServerFramework
{

namespace
{

// 0x4F8AD8
const char* const s_apszCounterType[4] = { NULL, "OSModule", "NetEngine", "ServerBody" };

const COLORREF s_aGraphColor[12] =
{
    0xFFFFFF, 0x0000FF, 0x00FF00, 0xFF00FF, 0x00FFFF, 0xFFFF00,
    0x8080FF, 0x80FF80, 0xFF8080, 0x80FFFF, 0xFFFF80, 0xFF80FF,
};

} // namespace

const char* MoniterView_GetTypeNameByHiByte(DWORD dwID)
{
    DWORD dwType = dwID >> 24;
    if (dwType > 3)
        return "Unknown";
    return s_apszCounterType[dwType];
}

// ---------------------------------------------------------------------------------------------
// CMoniterGraphData

CMoniterGraphData::CMoniterGraphData()
{
    m_fMin = FLT_MAX;
    m_fMax = -FLT_MAX;
    m_fCurrent = 0.0f;
    m_bVisible = true;
    memset(m_szName, 0, sizeof(m_szName));
    m_pCounter = NULL;
    m_clrLine = 0;
}

CMoniterGraphData::~CMoniterGraphData()
{
    m_listSample.clear();
}

void CMoniterGraphData::Init(const char* pszName, COLORREF clr, MONITOR_COUNTER* pCounter)
{
    // BUG(FWUI-003): copied with a 256 character limit into the 64-byte name.
    if (StringCchCopyA(m_szName, 256, pszName) != S_OK)
        BSAssertFail();
    m_pCounter = pCounter;
    pCounter->dwSampleCount = 0;
    m_clrLine = clr;
}

void CMoniterGraphData::ClearSamples()
{
    m_listSample.clear();
}

void CMoniterGraphData::AddSample()
{
    m_fCurrent = m_pCounter->fValue;
    ++m_pCounter->dwSampleCount;
    m_listSample.push_back(m_fCurrent);
    if ((int)m_pCounter->dwSampleCount > (int)m_pCounter->dwMaxSamples)
        m_listSample.erase(m_listSample.begin());
    if (m_fMin > m_fCurrent)
        m_fMin = m_fCurrent;
    if (m_fMax < m_fCurrent)
        m_fMax = m_fCurrent;
}

void CMoniterGraphData::DrawSeries(HDC hDC, float fMin, float fMax, int nLeft, int nTop,
                                   int nRight, int nBottom)
{
    float fRange = fMax - fMin;
    int nHeight = nBottom - nTop - 16;
    int x = nRight - 4;
    std::list<float>::iterator itEnd = m_listSample.end();
    std::list<float>::iterator it = itEnd;
    while (it != m_listSample.begin() && x > nLeft + 31)
    {
        std::list<float>::iterator itSample = it;
        --itSample;
        int y = nHeight - (int)((double)nHeight * *itSample / fRange);
        if (y < 0)
            y = 0;
        if (it == itEnd)
        {
            SetPixel(hDC, x, y, m_clrLine);
            MoveToEx(hDC, x, y, NULL);
        }
        else
        {
            CGdiScopedPen pen(hDC, PS_SOLID, 1, m_clrLine);
            LineTo(hDC, x, y);
        }
        x -= 4;
        it = itSample;
    }
}

// ---------------------------------------------------------------------------------------------
// CServerMoniterView

CServerMoniterView::CServerMoniterView()
{
    m_fMinValue = 0.0f;
    m_clrAxis = 0x6AA67A;
    m_fMaxValue = 120.0f;
    m_clrGrid = 0x6AA67A;
    m_fInterval = 1.0f;
    m_hFrontDC = NULL;
    m_hGridDC = NULL;
    m_hFrontBitmap = NULL;
    m_hGridBitmap = NULL;
    m_clrBack = 0x17261C;
    m_clrText = 0xFFFFFF;
    m_nGridStep = 50;
    m_bShowLegend = true;
    if (StringCchCopyA(m_szValueLabel, 256, "value") != S_OK)
        BSAssertFail();
    if (StringCchCopyA(m_szTimeLabel, 256, "time") != S_OK)
        BSAssertFail();
    m_hCursor = LoadCursorA(NULL, IDC_ARROW);
    m_dwScaleTick = 0;
    m_nNextColor = 0;
}

CServerMoniterView::~CServerMoniterView()
{
}

bool CServerMoniterView::Create(HINSTANCE hInstance, HWND hParent, RECT rc, HMENU hID)
{
    if (!RegisterWindowClass(hInstance, "ServerMoniterView"))
        return false;
    if (!CreateWin32Window(WS_EX_STATICEDGE, "ServerMoniterView", NULL, WS_CHILD | WS_VISIBLE,
                           hParent, hInstance, rc, hID))
        return false;

    InsertMsgHandler(WM_PAINT, &CServerMoniterView::OnPaint);
    InsertMsgHandler(WM_SIZE, &CServerMoniterView::OnSize);
    InsertMsgHandler(WM_ERASEBKGND, &CServerMoniterView::OnEraseBkgnd);
    InsertMsgHandler(WM_SETCURSOR, &CServerMoniterView::OnSetCursor);
    InsertMsgHandler(WM_TIMER, &CServerMoniterView::OnTimer);
    InsertMsgHandler(WM_RBUTTONUP, &CServerMoniterView::OnRButtonUp);
    Show(SW_SHOW);
    Update();
    SetTimer(m_hWnd, MONITER_TIMER_ID, (UINT)(__int64)(m_fInterval * 1000.0), NULL);
    return true;
}

bool CServerMoniterView::Destroy()
{
    if (m_hFrontDC)
        DeleteDC(m_hFrontDC);
    if (m_hGridDC)
        DeleteDC(m_hGridDC);
    if (m_hFrontBitmap)
        DeleteObject(m_hFrontBitmap);
    if (m_hGridBitmap)
        DeleteObject(m_hGridBitmap);
    m_hFrontDC = NULL;
    m_hGridDC = NULL;
    m_hFrontBitmap = NULL;
    m_hGridBitmap = NULL;
    for (MoniterGraphMap::iterator it = m_mapGraph.begin(); it != m_mapGraph.end(); ++it)
    {
        CMoniterGraphData* pGraph = it->second;
        pGraph->ClearSamples();
        delete pGraph;
    }
    m_mapGraph.clear();
    // BUG(FWUI-002): the window is neither destroyed nor removed from the window table.
    return false;
}

CMoniterGraphData* CServerMoniterView::FindGraphData(DWORD dwID)
{
    MoniterGraphMap::iterator it = m_mapGraph.find(dwID);
    if (it == m_mapGraph.end())
        return NULL;
    return it->second;
}

bool CServerMoniterView::AddGraph(DWORD dwID, const char* pszName, MONITOR_COUNTER* pCounter)
{
    COLORREF aColor[12];
    memcpy(aColor, s_aGraphColor, sizeof(aColor));
    if (FindGraphData(dwID))
        BSAssertFail();
    if (FindGraphData(dwID))
        return false;

    CMoniterGraphData* pGraph = new CMoniterGraphData;
    pGraph->Init(pszName, aColor[m_nNextColor], pCounter);
    m_nNextColor = (m_nNextColor + 1) % 12;
    m_mapGraph.insert(std::make_pair(dwID, pGraph));
    return true;
}

bool CServerMoniterView::RemoveGraph(DWORD dwID)
{
    MoniterGraphMap::iterator it = m_mapGraph.find(dwID);
    CMoniterGraphData* pGraph = it->second;
    m_mapGraph.erase(it);
    pGraph->ClearSamples();
    delete pGraph;
    return true;
}

void CServerMoniterView::SampleAndLogGraphs()
{
    char szLog[1024];
    int nScale = 120;
    szLog[0] = 0;
    for (MoniterGraphMap::iterator it = m_mapGraph.begin(); it != m_mapGraph.end(); ++it)
    {
        CMoniterGraphData* pGraph = it->second;
        pGraph->AddSample();
        if ((double)nScale < pGraph->m_fMax)
            nScale = (int)(pGraph->m_fMax * 9.0 * 0.125);
#ifndef JMX_FRAMEWORK_2007
        // FarmManager 0x454870 has no PERFMON log.
        if (g_ServerGlobals.bWritePerfLogToText)
        {
            // BUG(FWUI-008): formats the buffer into itself; the line starts with ", ".
            StringCchPrintfA(szLog, sizeof(szLog), "%s, %s: %.2f", szLog, pGraph->m_szName,
                             (double)pGraph->m_fCurrent);
        }
#endif
    }
#ifndef JMX_FRAMEWORK_2007
    if (strlen(szLog))
        LogPrintf(0x3000000, "PERFMON >> %s", szLog);
#endif

    float fScale = (float)nScale;
    if (!(m_fMaxValue < fScale))
    {
        // Shrinking the scale waits until a second after m_dwScaleTick, which is never set.
        if (GetTickCount() - m_dwScaleTick <= 1000)
            return;
    }
    m_fMaxValue = fScale;
    m_nGridStep = (int)(fScale / 3.0);
    RenderGridBackground();
}

int CServerMoniterView::ValueToY(float fValue)
{
    RECT rc;
    GetClientRect(&rc);
    double dHeight = (double)(rc.bottom - 16);
    return (int)(dHeight - fValue * dHeight / ((double)m_fMaxValue - m_fMinValue));
}

BOOL CServerMoniterView::RenderGridBackground()
{
    if (!m_hWnd)
        BSAssertFail();
    RECT rc;
    GetClientRect(&rc);
    HDC hDC = GetDC(m_hWnd);
    if (!m_hFrontDC)
    {
        m_hFrontDC = CreateCompatibleDC(hDC);
        m_hGridDC = CreateCompatibleDC(hDC);
        m_clrBack = GetNearestColor(hDC, 0x17261C);
        m_clrAxis = GetNearestColor(hDC, 0x6AA67A);
        m_clrText = GetNearestColor(hDC, 0xFFFFFF);
        m_clrGrid = GetNearestColor(hDC, 0x6AA67A);
    }
    if (m_hFrontBitmap)
        DeleteObject(m_hFrontBitmap);
    if (m_hGridBitmap)
        DeleteObject(m_hGridBitmap);
    int nWidth = rc.right - rc.left;
    int nHeight = rc.bottom - rc.top;
    m_hFrontBitmap = CreateCompatibleBitmap(hDC, nWidth, nHeight);
    m_hGridBitmap = CreateCompatibleBitmap(hDC, nWidth, nHeight);
    GetDeviceCaps(m_hFrontDC, NUMCOLORS);
    SelectObject(m_hFrontDC, m_hFrontBitmap);
    SelectObject(m_hGridDC, m_hGridBitmap);

    Gdi_DrawRectFillOutline(m_hGridDC, &rc, m_clrBack, 0);
    int nAxisY = rc.bottom - 16;
    int nAxisX = rc.left + 35;
    Gdi_DrawLine(m_hGridDC, 1, nAxisX, rc.top + 4, nAxisX, nAxisY, m_clrAxis, PS_SOLID);
    int nRight = rc.right - 4;
    Gdi_DrawLine(m_hGridDC, 1, nAxisX, nAxisY, nRight, nAxisY, m_clrAxis, PS_SOLID);

    CGdiScopedArialFont font(m_hGridDC, 8);
    SetBkMode(m_hGridDC, TRANSPARENT);
    SetTextColor(m_hGridDC, m_clrText);
    Gdi_TextOutf(m_hGridDC, nAxisX, 0, TA_RIGHT, "%d", (int)m_fMaxValue);
    Gdi_TextOutf(m_hGridDC, nAxisX, nAxisY, TA_RIGHT | TA_BOTTOM, "%d", (int)m_fMinValue);

    int nValue = (int)((double)m_nGridStep + m_fMinValue);
    int y = ValueToY((float)nValue);
    int nTop = rc.top;
    while (y > nTop)
    {
        Gdi_DrawLine(m_hGridDC, 1, nAxisX, y, nRight, y, m_clrGrid, PS_DOT);
        Gdi_TextOutf(m_hGridDC, nAxisX, y - 4, TA_RIGHT, "%d", nValue);
        nValue += m_nGridStep;
        y = ValueToY((float)nValue);
        if (nTop < 0)
        {
#ifdef JMX_FRAMEWORK_2007
            // FarmManager 0x454970
            NetEngineLog(0x2000001,
                         "CLAMP() ==> min(%d) exceeded max(%d) value), File: %s, Line: %d",
                         0, nTop, ".\\ServerMoniterView.cpp", 421);
#else
            NetEngineLog(0x2000001,
                         "CLAMP() ==> min(%.3f) exceeded max(%.3f) value), File: %s, Line: %d",
                         0.0, (double)nTop,
                         "D:\\WORK2005\\JMX_ServerFramework\\ServerFramework\\"
                         "ServerMoniterView.cpp",
                         434);
#endif
            nTop = 0;
        }
    }
    ReleaseDC(m_hWnd, hDC);
    return TRUE;
}

void CServerMoniterView::DrawGraphs(HDC hDC, int nLeft, int nTop, int nRight, int nBottom)
{
    for (MoniterGraphMap::iterator it = m_mapGraph.begin(); it != m_mapGraph.end(); ++it)
    {
        CMoniterGraphData* pGraph = it->second;
        if (pGraph->m_bVisible)
            pGraph->DrawSeries(hDC, m_fMinValue, m_fMaxValue, nLeft, nTop, nRight, nBottom);
    }
}

BOOL CServerMoniterView::DrawLegend(HDC hDC, int nLeft, int /*nTop*/, int /*nRight*/,
                                    int /*nBottom*/)
{
    char szText[260];
    CGdiScopedArialFont font(hDC, 8);
    SetBkMode(hDC, TRANSPARENT);

    int x = nLeft + 10;
    RECT rcLegend;
    rcLegend.left = x + 35;
    rcLegend.top = 0;
    rcLegend.right = x + 45;
    rcLegend.bottom = 1;
    int y = 10;
    MoniterGraphMap::iterator it;
    for (it = m_mapGraph.begin(); it != m_mapGraph.end(); ++it)
    {
        CMoniterGraphData* pGraph = it->second;
        if (StringCchPrintfA(szText, sizeof(szText), "(%d)%s", (int)pGraph->m_pCounter->fValue,
                             pGraph->m_szName) != S_OK)
            BSAssertFail();
        SIZE sizeText;
        GetTextExtentPoint32A(hDC, szText, (int)strlen(szText), &sizeText);
        // BUG(FWUI-009): counter IDs with high byte 0 have no type name (NULL).
        const char* pszType = MoniterView_GetTypeNameByHiByte(it->first);
        SIZE sizeType;
        GetTextExtentPoint32A(hDC, pszType, (int)strlen(pszType), &sizeType);
        RECT rcLine;
        rcLine.left = x + 35;
        rcLine.right = sizeText.cx + sizeType.cx + x + 35 + 29;
        rcLine.top = y;
        rcLine.bottom = y + 16;
        y += 16;
        UnionRect(&rcLegend, &rcLegend, &rcLine);
    }
    InflateRect(&rcLegend, 4, 0);
    Gdi_DrawRectFillOutline(hDC, &rcLegend, m_clrBack, m_clrAxis);

    y = 2;
    for (it = m_mapGraph.begin(); it != m_mapGraph.end(); ++it)
    {
        CMoniterGraphData* pGraph = it->second;
        COLORREF clr = pGraph->m_clrLine;
        Gdi_DrawLine(hDC, 1, x + 35, y + 8, x + 45, y + 8, clr, PS_SOLID);
        SetTextColor(hDC, clr);
        if (StringCchPrintfA(szText, sizeof(szText), "(%d)%s", (int)pGraph->m_pCounter->fValue,
                             pGraph->m_szName) != S_OK)
            BSAssertFail();
        // The text is passed as the format string.
        Gdi_TextOutf(hDC, x + 47, y, 0, szText);
        SIZE sizeText;
        GetTextExtentPoint32A(hDC, szText, (int)strlen(szText), &sizeText);
        const char* pszType = MoniterView_GetTypeNameByHiByte(it->first);
        SetTextColor(hDC, m_clrText);
        Gdi_TextOutf(hDC, sizeText.cx + x + 52, y, 0, pszType);
        SIZE sizeType;
        GetTextExtentPoint32A(hDC, pszType, (int)strlen(pszType), &sizeType);
        if (pGraph->m_bVisible)
            Gdi_DrawCircleMarker10(hDC, sizeText.cx + sizeType.cx + x + 54, y + 3);
        y += 16;
    }
    return TRUE;
}

LRESULT CServerMoniterView::OnPaint(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    PAINTSTRUCT ps;
    HDC hDC = BeginPaint(m_hWnd, &ps);
    RECT rc;
    GetClientRect(&rc);
    if (m_hFrontDC && m_hGridDC)
    {
        int nHeight = rc.bottom - rc.top;
        BitBlt(m_hFrontDC, 0, 0, rc.right - rc.left, nHeight, m_hGridDC, 0, 0, SRCCOPY);
        if (m_bShowLegend)
            DrawLegend(m_hFrontDC, rc.left, rc.top, rc.right, rc.bottom);
        DrawGraphs(m_hFrontDC, rc.left, rc.top, rc.right, rc.bottom);
        BitBlt(hDC, 0, 0, rc.right - rc.left, nHeight, m_hFrontDC, 0, 0, SRCCOPY);
    }
    EndPaint(m_hWnd, &ps);
    return 0;
}

LRESULT CServerMoniterView::OnSize(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    RenderGridBackground();
    Redraw(RDW_INVALIDATE | RDW_INTERNALPAINT);
    return 0;
}

LRESULT CServerMoniterView::OnEraseBkgnd(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    return 1;
}

LRESULT CServerMoniterView::OnSetCursor(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    SetCursor(m_hCursor);
    return 1;
}

LRESULT CServerMoniterView::OnTimer(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    SampleAndLogGraphs();
    Redraw(RDW_INVALIDATE | RDW_INTERNALPAINT);
    return 0;
}

LRESULT CServerMoniterView::OnRButtonUp(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    HMENU hMenu = CreatePopupMenu();
    HMENU hView = CreatePopupMenu();
    AppendMenuA(hMenu, MF_STRING, IDM_MONITER_START, "Start Moniter");
    AppendMenuA(hMenu, MF_STRING, IDM_MONITER_STOP, "Stop Moniter");
    AppendMenuA(hMenu, MF_STRING, IDM_MONITER_OPTION, "Option");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hView, "View");
    AppendMenuA(hView, MF_STRING, IDM_MONITER_LEGEND, "Show Legend");
    CheckMenuItem(hView, IDM_MONITER_LEGEND, m_bShowLegend ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuA(hView, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hView, MF_STRING, IDM_MONITER_SHOW_ALL, "Show All Moniter");
    AppendMenuA(hView, MF_STRING, IDM_MONITER_HIDE_ALL, "Hide All Moniter");
    AppendMenuA(hView, MF_SEPARATOR, 0, NULL);

    MoniterGraphMap::iterator it;
    for (it = m_mapGraph.begin(); it != m_mapGraph.end(); ++it)
    {
        char szItem[255];
        CMoniterGraphData* pGraph = it->second;
        if (StringCchPrintfA(szItem, sizeof(szItem), "Show %s %s",
                             MoniterView_GetTypeNameByHiByte(it->first),
                             pGraph->m_szName) != S_OK)
            BSAssertFail();
        AppendMenuA(hView, MF_STRING, it->first, szItem);
        CheckMenuItem(hView, it->first, pGraph->m_bVisible ? MF_CHECKED : MF_UNCHECKED);
    }

    POINT pt;
    GetCursorPos(&pt);
    // BUG(FWUI-001): no TPM_RETURNCMD; the result is TRUE/FALSE, so no item has an effect.
    int nCmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, NULL);
    switch (nCmd)
    {
    case 0:
    case IDM_MONITER_START:
    case IDM_MONITER_STOP:
    case IDM_MONITER_OPTION:
        break;
    case IDM_MONITER_LEGEND:
        m_bShowLegend = !m_bShowLegend;
        break;
    case IDM_MONITER_SHOW_ALL:
        for (it = m_mapGraph.begin(); it != m_mapGraph.end(); ++it)
            it->second->SetVisible(true);
        break;
    case IDM_MONITER_HIDE_ALL:
        for (it = m_mapGraph.begin(); it != m_mapGraph.end(); ++it)
            it->second->SetVisible(false);
        break;
    default:
        it = m_mapGraph.find((DWORD)nCmd);
        if (it == m_mapGraph.end())
            BSAssertFail();
        else
            it->second->SetVisible(!it->second->m_bVisible);
        break;
    }
    Redraw(RDW_INVALIDATE | RDW_INTERNALPAINT);
    DestroyMenu(hView);
    DestroyMenu(hMenu);
    return 0;
}

} // namespace ServerFramework

// Calls of the monitor graph code into the view (vtable slots 5 and 6).
void MonitorView_AddGraph(ServerFramework::CServerMoniterView* pView, DWORD dwID,
                          const char* pszName, MONITOR_COUNTER* pCounter)
{
    pView->AddGraph(dwID, pszName, pCounter);
}

void MonitorView_RemoveGraph(ServerFramework::CServerMoniterView* pView, DWORD dwID)
{
    pView->RemoveGraph(dwID);
}
