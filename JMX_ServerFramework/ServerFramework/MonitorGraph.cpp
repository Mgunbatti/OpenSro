#include "MonitorGraph.h"

#include <string.h>
#include <map>

#include "BSLib/BSDebug.h"
#include "BSLib/thread/SynchObject.h"
#include "BSNet/IBSNet.h"
#include "ServerFramework/MassiveMsg.h"

ServerFramework::CServerMoniterView* g_pMonitorGraphView = NULL;

namespace
{

typedef std::map<DWORD, MONITOR_COUNTER*> CounterMap;

CounterMap          s_mapCounter;       // 0x53B878
CCriticalSectionBS  s_csCounter;        // 0x53B884
HANDLE              s_hUpdateThread;    // 0x4F9CE0
HANDLE              s_hStopEvent;       // 0x4F9CE4

// Name of a counter ID without a name of its own (CP949).
const char s_szUnknownCounter[] = "\xC0\xCC\xB0\xD4\xB9\xB9\xB3\xC4?";

} // namespace

void MonitorGraph_StartUpdateThread()
{
    if (!g_pServerFrameWindow)
        return;
    g_pMonitorGraphView = ServerFrameWindow_GetMonitorView();
    s_hStopEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
    DWORD dwThreadID;
    s_hUpdateThread = CreateThread(NULL, 0, MonitorGraph_UpdateThreadProc, NULL, 0,
                                   &dwThreadID);
}

void MonitorGraph_StopUpdateThreadAndFreeCounters()
{
    // BUG(FWUTIL-005): the thread handle is never closed and the event handle is kept after
    // CloseHandle.
    if (s_hStopEvent)
    {
        SetEvent(s_hStopEvent);
        WaitForSingleObject(s_hUpdateThread, INFINITE);
        CloseHandle(s_hStopEvent);
    }
    for (CounterMap::iterator it = s_mapCounter.begin(); it != s_mapCounter.end(); ++it)
        delete it->second;
    s_mapCounter.clear();
}

DWORD WINAPI MonitorGraph_UpdateThreadProc(LPVOID)
{
    while (WaitForSingleObject(s_hStopEvent, 1000) == WAIT_TIMEOUT)
    {
        CSingleLockBS lock(&s_csCounter, TRUE);
        for (CounterMap::iterator it = s_mapCounter.begin(); it != s_mapCounter.end(); ++it)
        {
            MONITOR_COUNTER* pCounter = it->second;
            if (!pCounter->pfnUpdate)
                BSAssertFail();
            pCounter->pfnUpdate(pCounter);
        }
    }
    return 0;
}

void MonitorGraph_AddCounterLocked(DWORD dwID, const char* pszName, MONITOR_COUNTER* pCounter)
{
    CSingleLockBS lock(&s_csCounter, TRUE);
    if (!g_pServerFrameWindow)
        return;
    pCounter->dwMaxSamples = 100;
    if (s_mapCounter.find(dwID) == s_mapCounter.end())
    {
        s_mapCounter.insert(CounterMap::value_type(dwID, pCounter));
        MonitorView_AddGraph(g_pMonitorGraphView, dwID, pszName, pCounter);
    }
    else
    {
        BSAssertFail();
    }
}

void MonitorGraph_RemoveCounterLocked(DWORD dwID)
{
    CSingleLockBS lock(&s_csCounter, TRUE);
    if (!g_pServerFrameWindow)
        return;
    CounterMap::iterator it = s_mapCounter.find(dwID);
    if (it != s_mapCounter.end())
    {
        s_mapCounter.erase(it);
        MonitorView_RemoveGraph(g_pMonitorGraphView, dwID);
    }
}

void MonitorGraph_RegisterNetEngineCounter(DWORD dwID)
{
    CSingleLockBS lock(&s_csCounter, TRUE);
    if (!g_pServerFrameWindow || !g_pMonitorGraphView)
        return;

    MONITOR_COUNTER* pCounter = new MONITOR_COUNTER;
    // BUG(FWUTIL-004): dwMaxSamples 0, the graph drops every sample right away.
    pCounter->dwMaxSamples = 0;
    pCounter->dw4 = 0;
    pCounter->fValue = 0;
    pCounter->dwSampleCount = 0;
    pCounter->dw14 = 0;
    pCounter->pfnUpdate = MonitorGraph_ReadNetEngineCounter;
    const char* pszName = s_szUnknownCounter;
    pCounter->dwID = dwID;
    switch (dwID)
    {
    case MONITOR_COUNTER_MSG:           pszName = "MsgCount";           break;
    case MONITOR_COUNTER_SESSION:       pszName = "SessionCount";       break;
    case MONITOR_COUNTER_SOCK_TCP:      pszName = "SockTCPCount";       break;
    case MONITOR_COUNTER_CLASS_LINK:    pszName = "ClassLinkCount";     break;
    case MONITOR_COUNTER_ACCEPT:        pszName = "AcceptCount";        break;
    case MONITOR_COUNTER_USED_FILE:     pszName = "UsedFileCount";      break;
    case MONITOR_COUNTER_CACHED_FILE:   pszName = "CachedFileCount";    break;
    case MONITOR_COUNTER_MASSIVE_MSG:   pszName = "MassiveMsgCount";    break;
    case MONITOR_COUNTER_OVERLAP:       pszName = "OverlapCount";       break;
    default:
        BSAssertFail();
        break;
    }
    // an ID registered twice keeps the first counter; the second one is still shown
    s_mapCounter.insert(CounterMap::value_type(dwID, pCounter));
    MonitorView_AddGraph(g_pMonitorGraphView, dwID, pszName, pCounter);
}

bool __cdecl MonitorGraph_ReadNetEngineCounter(MONITOR_COUNTER* pCounter)
{
    IBSNet* pNet = g_pBSNet;
    if (!pNet)
        return false;

    // the original passes a 0x78-byte buffer
    union
    {
        NETENGINE_STATISTICS    s;
        BYTE                    ab[0x78];
    } stats;
    memset(&stats, 0, sizeof(stats));

    DWORD dwValue;
    switch (pCounter->dwID)
    {
    case MONITOR_COUNTER_MSG:
        pNet->GetStatistics(BSNET_STAT_MSG_POOL, &stats.s);
        dwValue = stats.s.dwMsgUsed;
        break;
    case MONITOR_COUNTER_SESSION:
        pNet->GetStatistics(BSNET_STAT_SESSIONS, &stats.s);
        dwValue = stats.s.dwSessionUsed;
        break;
    case MONITOR_COUNTER_SOCK_TCP:
        pNet->GetStatistics(BSNET_STAT_SOCKET_POOL, &stats.s);
        dwValue = stats.s.dwSocketUsed;
        break;
    case MONITOR_COUNTER_CLASS_LINK:
        pNet->GetStatistics(BSNET_STAT_SESSION_LIST, &stats.s);
        dwValue = stats.s.dwLinkedSessions;
        break;
    case MONITOR_COUNTER_ACCEPT:
        pNet->GetStatistics(BSNET_STAT_ACCEPT, &stats.s);
        dwValue = stats.s.dwPendingAccepts;
        break;
    case MONITOR_COUNTER_USED_FILE:
        pNet->GetStatistics(BSNET_STAT_FILES, &stats.s);
        dwValue = stats.s.dwFileUsers;
        break;
    case MONITOR_COUNTER_CACHED_FILE:
        pNet->GetStatistics(BSNET_STAT_FILES, &stats.s);
        dwValue = stats.s.dwOpenFiles;
        break;
    case MONITOR_COUNTER_MASSIVE_MSG:
        {
            DWORD dwTotal;
            DWORD dwFree;
            ServerFramework::g_MassiveMsgPool.GetStats(dwTotal, dwValue, dwFree);
        }
        break;
    case MONITOR_COUNTER_OVERLAP:
        {
            DWORD dwFirst;
            DWORD dwThird;
            IdPool_GetUsageCounters(&dwFirst, &dwValue, &dwThird);
        }
        break;
    default:
        return true;
    }
    pCounter->fValue = (float)dwValue;
    return true;
}
