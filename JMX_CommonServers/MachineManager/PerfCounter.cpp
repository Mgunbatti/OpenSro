#include "PerfCounter.h"

#include <new>

const char* const g_apszPerfCounterPath[2] =
{
    "\\Processor(_Total)\\% Processor Time",
    "\\Memory\\Committed Bytes",
};

PDH_HQUERY g_hPerfQuery;
std::map<int, PERF_COUNTER*> g_mapPerfCounter;

bool PerfCounter_OpenQuery()
{
    return PdhOpenQueryA(NULL, 1, &g_hPerfQuery) == ERROR_SUCCESS;
}

void PerfCounter_CloseAll()
{
    if (g_hPerfQuery)
    {
        // BUG(MM-001): the address of the handle is passed, so the query is never closed.
        PdhCloseQuery((PDH_HQUERY)&g_hPerfQuery);
        g_hPerfQuery = NULL;
    }
    for (std::map<int, PERF_COUNTER*>::iterator it = g_mapPerfCounter.begin();
         it != g_mapPerfCounter.end(); ++it)
    {
        operator delete(it->second);
    }
    g_mapPerfCounter.clear();
}

bool PerfCounter_Add(int nIndex)
{
    PERF_COUNTER* pCounter = (PERF_COUNTER*)operator new(sizeof(PERF_COUNTER));
    if (PdhAddCounterA(g_hPerfQuery, g_apszPerfCounterPath[nIndex], (DWORD_PTR)pCounter,
                       &pCounter->hCounter) != ERROR_SUCCESS)
    {
        operator delete(pCounter);
        return false;
    }
    pCounter->dw0 = 0;
    pCounter->lValue = 0;
    pCounter->nNext = 0;
    pCounter->nFirst = 0;
    pCounter->nCount = 0;
    pCounter->lMin = 0;
    pCounter->lMax = 0;
    pCounter->lMean = 0;
    g_mapPerfCounter.insert(std::pair<const int, PERF_COUNTER*>(nIndex, pCounter));
    return false;
}

void PerfCounter_CollectAll()
{
    if (PdhCollectQueryData(g_hPerfQuery) != ERROR_SUCCESS)
        return;
    for (std::map<int, PERF_COUNTER*>::iterator it = g_mapPerfCounter.begin();
         it != g_mapPerfCounter.end(); ++it)
    {
        PERF_COUNTER* p = it->second;
        PDH_FMT_COUNTERVALUE value;
        if (PdhGetFormattedCounterValue(p->hCounter, PDH_FMT_LONG, NULL, &value) != ERROR_SUCCESS
            || value.CStatus != ERROR_SUCCESS)
        {
            continue;
        }
        p->lValue = value.longValue;
        if (PdhGetRawCounterValue(p->hCounter, NULL, &p->aRaw[p->nNext]) != ERROR_SUCCESS)
            continue;
        int nCount = p->nCount + 1;
        if (nCount >= PERF_COUNTER_SAMPLES)
            nCount = PERF_COUNTER_SAMPLES;
        int nNext = (p->nNext + 1) % PERF_COUNTER_SAMPLES;
        p->nCount = nCount;
        p->nNext = nNext;
        if (nCount >= PERF_COUNTER_SAMPLES)
            p->nFirst = nNext;
        PDH_STATISTICS stats;
        if (PdhComputeCounterStatistics(p->hCounter, PDH_FMT_LONG, p->nFirst, nCount, p->aRaw,
                                        &stats) != ERROR_SUCCESS)
        {
            continue;
        }
        p->lMin = stats.min.CStatus == ERROR_SUCCESS ? stats.min.longValue : 0;
        p->lMax = stats.max.CStatus == ERROR_SUCCESS ? stats.max.longValue : 0;
        p->lMean = stats.mean.CStatus == ERROR_SUCCESS ? stats.mean.longValue : 0;
    }
}

void PerfCounter_GetValues(int nIndex, LONG* plValue, LONG* plMin, LONG* plMax, LONG* plMean)
{
    std::map<int, PERF_COUNTER*>::iterator it = g_mapPerfCounter.find(nIndex);
    if (it == g_mapPerfCounter.end())
        return;
    PERF_COUNTER* p = it->second;
    *plValue = p->lValue;
    *plMin = p->lMin;
    *plMax = p->lMax;
    *plMean = p->lMean;
}
