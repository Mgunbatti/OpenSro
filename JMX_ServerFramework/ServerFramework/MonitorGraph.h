#pragma once

#include <windows.h>

// Performance counters shown as graphs in the monitor view of the server window. Each counter
// has an update callback; a worker thread calls all callbacks once a second and the view takes
// the current values as samples.

namespace ServerFramework
{
class CServerFrameWindow;
class CServerMoniterView;
}

struct MONITOR_COUNTER;

// Writes the counter's current value to fValue. The return value is not used.
typedef bool (__cdecl* PFN_MONITOR_COUNTER_UPDATE)(MONITOR_COUNTER* pCounter);

struct MONITOR_COUNTER
{
    DWORD                       dwMaxSamples;   // +0   samples the graph keeps
    DWORD                       dw4;            // +4
    float                       fValue;         // +8   current value
    DWORD                       dwSampleCount;  // +C   samples taken (counted by the view)
    DWORD                       dwID;           // +10
    DWORD                       dw14;           // +14
    PFN_MONITOR_COUNTER_UPDATE  pfnUpdate;      // +18
};

C_ASSERT(FIELD_OFFSET(MONITOR_COUNTER, fValue) == 8);
C_ASSERT(FIELD_OFFSET(MONITOR_COUNTER, dwID) == 0x10);
C_ASSERT(sizeof(MONITOR_COUNTER) == 0x1C);

// Counter IDs read by MonitorGraph_ReadNetEngineCounter.
enum
{
    MONITOR_COUNTER_MSG             = 0x2010001,    // messages handed out by the engine pool
    MONITOR_COUNTER_SESSION         = 0x2010002,
    MONITOR_COUNTER_SOCK_TCP        = 0x2010003,    // accepted sockets in use
    MONITOR_COUNTER_CLASS_LINK      = 0x2010004,    // linked sessions
    MONITOR_COUNTER_ACCEPT          = 0x2010005,    // pending accepts
    MONITOR_COUNTER_USED_FILE       = 0x2010006,
    MONITOR_COUNTER_CACHED_FILE     = 0x2010007,
    MONITOR_COUNTER_MASSIVE_MSG     = 0x3010001,    // CMassiveMsg objects in use
    MONITOR_COUNTER_OVERLAP         = 0x3010002,    // overlap job ID pool
};

// 0x4F9CDC: the monitor view of the server window, cached by MonitorGraph_StartUpdateThread.
extern ServerFramework::CServerMoniterView* g_pMonitorGraphView;

// MonitorGraph_StartUpdateThread (0x446820): without a server window does nothing. Otherwise
// caches its monitor view, creates the stop event and starts the update thread.
void MonitorGraph_StartUpdateThread();

// MonitorGraph_StopUpdateThreadAndFreeCounters (0x446870): stops the update thread, deletes
// every registered counter object and empties the counter map.
void MonitorGraph_StopUpdateThreadAndFreeCounters();

// MonitorGraph_UpdateThreadProc (0x446960)
DWORD WINAPI MonitorGraph_UpdateThreadProc(LPVOID pParam);

// MonitorGraph_AddCounterLocked (0x446ab0): registers a counter of the caller (keeping 100
// samples) and adds its graph to the view. Asserts if the ID is taken. Needs a server window.
void MonitorGraph_AddCounterLocked(DWORD dwID, const char* pszName, MONITOR_COUNTER* pCounter);

// MonitorGraph_RemoveCounterLocked (0x446bf0): unregisters the counter (not deleted) and removes
// its graph. Needs a server window.
void MonitorGraph_RemoveCounterLocked(DWORD dwID);

// MonitorGraph_RegisterNetEngineCounter (0x446d10): creates and registers one of the
// MONITOR_COUNTER_* counters, read by MonitorGraph_ReadNetEngineCounter. Needs a server window
// and its monitor view.
void MonitorGraph_RegisterNetEngineCounter(DWORD dwID);

// MonitorGraph_ReadNetEngineCounter (0x446ed0): false only without an engine.
bool __cdecl MonitorGraph_ReadNetEngineCounter(MONITOR_COUNTER* pCounter);

// ---------------------------------------------------------------------------------------------
// Provided by other parts of the server application.

// 0x4F9CF8: the server window, set by ServerApp_RunWindowMode while it runs.
extern ServerFramework::CServerFrameWindow* g_pServerFrameWindow;

// 0x4467d0: the monitor view of g_pServerFrameWindow (CServerFrameWindow +0xBC).
ServerFramework::CServerMoniterView* ServerFrameWindow_GetMonitorView();

// CServerMoniterView::AddGraph / RemoveGraph (vtable slots 5 and 6).
void MonitorView_AddGraph(ServerFramework::CServerMoniterView* pView, DWORD dwID,
                          const char* pszName, MONITOR_COUNTER* pCounter);
void MonitorView_RemoveGraph(ServerFramework::CServerMoniterView* pView, DWORD dwID);

// IdPool_GetUsageCounters (0x4481a0): the three counters of the overlap job ID pool (0x53B8E0,
// 0x53B8E4, 0x53B8E8); the graph shows the second.
void IdPool_GetUsageCounters(DWORD* pdwFirst, DWORD* pdwSecond, DWORD* pdwThird);
