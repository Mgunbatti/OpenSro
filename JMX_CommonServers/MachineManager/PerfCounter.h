#pragma once

#include <windows.h>
#include <pdh.h>
#include <map>

// Performance counters of the machine, read through PDH (pdh.dll) for the 0x6304 statistics.
// One query (g_hPerfQuery) with the counters of g_apszPerfCounterPath, each with a ring of the
// last 20 raw samples for the minimum, maximum and mean.

enum
{
    PERF_COUNTER_PROCESSOR  = 0,    // "\Processor(_Total)\% Processor Time"
    PERF_COUNTER_MEMORY     = 1,    // "\Memory\Committed Bytes"

    PERF_COUNTER_SAMPLES    = 20,
};

struct PERF_COUNTER
{
    DWORD           dw0;                            // +0   0
    LONG            lValue;                         // +4   last formatted value
    PDH_HCOUNTER    hCounter;                       // +8
    int             nNext;                          // +C   raw slot of the next sample
    int             nFirst;                         // +10  oldest sample once the ring is full
    int             nCount;                         // +14  samples in the ring (at most 20)
    LONG            lMin;                           // +18
    LONG            lMax;                           // +1C
    LONG            lMean;                          // +20
    DWORD           dw24;                           // +24  not initialised
    PDH_RAW_COUNTER aRaw[PERF_COUNTER_SAMPLES];     // +28
};

C_ASSERT(FIELD_OFFSET(PERF_COUNTER, hCounter) == 8);
C_ASSERT(FIELD_OFFSET(PERF_COUNTER, lMin) == 0x18);
C_ASSERT(FIELD_OFFSET(PERF_COUNTER, aRaw) == 0x28);
C_ASSERT(sizeof(PERF_COUNTER) == 0x348);

// 0x4D08D4: the counter paths by index.
extern const char* const g_apszPerfCounterPath[2];
// 0x4D2484
extern PDH_HQUERY g_hPerfQuery;
// 0x513ABC: index -> counter.
extern std::map<int, PERF_COUNTER*> g_mapPerfCounter;

// PdhOpenQueryA_wrap (0x421c40): opens g_hPerfQuery (user data 1). True on success.
bool PerfCounter_OpenQuery();
// PerfCounter_CloseAll (0x421c60): closes the query and frees every counter.
void PerfCounter_CloseAll();
// PerfCounter_Add (0x421d40): adds the counter g_apszPerfCounterPath[nIndex] to the query and
// registers it under nIndex (not when PDH refuses it). Returns false in every case.
bool PerfCounter_Add(int nIndex);
// PdhCounters_CollectAndUpdateStats (0x421dc0): collects the query and updates every counter's
// value, sample ring and statistics.
void PerfCounter_CollectAll();
// PerfCounterMap_GetValues (0x421f20): value, minimum, maximum and mean of the counter; the
// outputs are left alone when the index is unknown.
void PerfCounter_GetValues(int nIndex, LONG* plValue, LONG* plMin, LONG* plMax, LONG* plMean);
