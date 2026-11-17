#pragma once

#include <list>
#include <map>

#include "ServerFramework/MonitorGraph.h"
#include "ServerFramework/ServerChildWindowBase.h"

namespace ServerFramework
{

// One graph of the monitor view: the samples taken from a performance counter.
class CMoniterGraphData
{
public:
    CMoniterGraphData();
    virtual ~CMoniterGraphData();

    // CMoniterGraphData::Init (0x469b70): name (asserts beyond 255 characters), counter (its
    // sample count is reset) and line colour.
    void Init(const char* pszName, COLORREF clr, MONITOR_COUNTER* pCounter);
    // CMoniterGraphData_ClearSamples (0x469bd0)
    void ClearSamples();
    // CMoniterGraphData::AddSample (0x469c10): appends the counter's current value, drops the
    // oldest sample beyond the counter's sample limit and tracks the minimum and maximum.
    void AddSample();
    // CMoniterGraphData::DrawSeries (0x469cc0): newest sample at the right edge, 4 pixels per
    // sample to the left.
    void DrawSeries(HDC hDC, float fMin, float fMax, int nLeft, int nTop, int nRight,
                    int nBottom);
    void SetVisible(bool bVisible) { m_bVisible = bVisible; }  // Obj::SetField_0x60

public:
    char                m_szName[64];   // +4
    std::list<float>    m_listSample;   // +68
    float               m_fCurrent;     // +80
    float               m_fMin;         // +84
    float               m_fMax;         // +88
    MONITOR_COUNTER*    m_pCounter;     // +92
    bool                m_bVisible;     // +96
    COLORREF            m_clrLine;      // +100
};

C_ASSERT(FIELD_OFFSET(CMoniterGraphData, m_listSample) == 68);
C_ASSERT(FIELD_OFFSET(CMoniterGraphData, m_pCounter) == 92);
C_ASSERT(sizeof(CMoniterGraphData) == 0x68);

typedef std::map<DWORD, CMoniterGraphData*> MoniterGraphMap;

// Popup menu command IDs (the menu is shown without TPM_RETURNCMD, see FWUI-001).
enum
{
    IDM_MONITER_START       = 1,
    IDM_MONITER_STOP        = 2,
    IDM_MONITER_OPTION      = 3,
    IDM_MONITER_LEGEND      = 4,
    IDM_MONITER_SHOW_ALL    = 5,
    IDM_MONITER_HIDE_ALL    = 6,
};

enum
{
    MONITER_TIMER_ID        = 100,
};

// Graphs of the performance counters, sampled by a timer, drawn over a grid with value labels
// and an optional legend.
class CServerMoniterView : public CServerChildWindowBase
{
public:
    CServerMoniterView();
    virtual ~CServerMoniterView();

    // 2: frees the back buffers and every graph. The window is not destroyed (FWUI-002).
    virtual bool Destroy();
    virtual bool Create(HINSTANCE hInstance, HWND hParent, RECT rc, HMENU hID); // 4
    // 5: asserts and fails when the ID has a graph.
    virtual bool AddGraph(DWORD dwID, const char* pszName, MONITOR_COUNTER* pCounter);
    // 6: throws std::out_of_range when the ID has no graph.
    virtual bool RemoveGraph(DWORD dwID);

    LRESULT OnPaint(WPARAM wParam, LPARAM lParam);
    LRESULT OnSize(WPARAM wParam, LPARAM lParam);
    LRESULT OnEraseBkgnd(WPARAM wParam, LPARAM lParam);
    LRESULT OnSetCursor(WPARAM wParam, LPARAM lParam);
    LRESULT OnTimer(WPARAM wParam, LPARAM lParam);
    LRESULT OnRButtonUp(WPARAM wParam, LPARAM lParam);

    CMoniterGraphData* FindGraphData(DWORD dwID);
    // Timer step: one sample per graph, value scale update, optional perfmon log line.
    void SampleAndLogGraphs();
    // Back buffers, grid and value labels.
    BOOL RenderGridBackground();
    int ValueToY(float fValue);
    void DrawGraphs(HDC hDC, int nLeft, int nTop, int nRight, int nBottom);
    BOOL DrawLegend(HDC hDC, int nLeft, int nTop, int nRight, int nBottom);

public:
    HDC                 m_hFrontDC;     // +140 graphs and legend on top of the grid
    HBITMAP             m_hFrontBitmap; // +144
    HDC                 m_hGridDC;      // +148
    HBITMAP             m_hGridBitmap;  // +152
    COLORREF            m_clrBack;      // +156
    COLORREF            m_clrAxis;      // +160
    COLORREF            m_clrGrid;      // +164
    COLORREF            m_clrText;      // +168
    float               m_fMinValue;    // +172
    float               m_fMaxValue;    // +176
    int                 m_nGridStep;    // +180
    float               m_fInterval;    // +184 seconds per sample
    char                m_szValueLabel[64];     // +188 "value"
    char                m_szTimeLabel[64];      // +252 "time"
    bool                m_bShowLegend;  // +316
    HCURSOR             m_hCursor;      // +320
    MoniterGraphMap     m_mapGraph;     // +324
    DWORD               m_dwScaleTick;  // +336
    int                 m_nNextColor;   // +340
};

C_ASSERT(FIELD_OFFSET(CServerMoniterView, m_hFrontDC) == 140);
C_ASSERT(FIELD_OFFSET(CServerMoniterView, m_clrBack) == 156);
C_ASSERT(FIELD_OFFSET(CServerMoniterView, m_fMinValue) == 172);
C_ASSERT(FIELD_OFFSET(CServerMoniterView, m_szValueLabel) == 188);
C_ASSERT(FIELD_OFFSET(CServerMoniterView, m_bShowLegend) == 316);
C_ASSERT(FIELD_OFFSET(CServerMoniterView, m_mapGraph) == 324);
C_ASSERT(FIELD_OFFSET(CServerMoniterView, m_nNextColor) == 340);
C_ASSERT(sizeof(CServerMoniterView) == 0x158);

// Kind of a counter by the high byte of its ID: NULL (0), "OSModule", "NetEngine",
// "ServerBody", "Unknown" above 3.
const char* MoniterView_GetTypeNameByHiByte(DWORD dwID);

} // namespace ServerFramework
