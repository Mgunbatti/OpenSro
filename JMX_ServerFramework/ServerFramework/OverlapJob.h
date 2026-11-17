#pragma once

#include <windows.h>
#include <deque>
#include <map>

#include "BSLib/ChunkAllocator.h"
#include "BSLib/thread/SynchObject.h"
#include "BSLib/thread/Task.h"

// Overlap jobs: work a server process hands to another task and gets an answer for.
//
// Local jobs (COverlapJob_Local) run on the overlap process: the owner sends 0x6009 {job pointer}
// to the overlap task, which calls Prepare and Execute and answers 0xA009 {job ID} to the owner's
// queue; the owner then calls OnComplete or OnFail and returns the job to its pool.
//
// Relay jobs (COverlapJob_RelayMsg) remember a message relayed to another server body (0x6008)
// until its answer (0xA008) arrives or the session of the route is lost.
//
// Jobs are registered by ID in the COverlapJobManager of the owning process. IDs come from
// g_OverlapJobIDPool.

namespace ServerFramework
{

enum
{
    OVERLAP_JOB_STATE_PREPARE   = 1,    // Prepare is running (or failed)
    OVERLAP_JOB_STATE_EXECUTE   = 2,    // Execute is running (or failed)
    OVERLAP_JOB_STATE_DONE      = 3,
};

class COverlapJob
{
public:
    // COverlapJob::ctor (0x4481c0)
    COverlapJob()
    {
        m_dwID = 0;
        m_bRelay = 0;
    }

    virtual ~COverlapJob() {}                                   // 0

    // Init (0x448200): asserts an ID was acquired, sets the owner task and starts the clock
    // (no timeout).
    virtual void Init(CTask* pOwner);                           // 1
    // ReleaseId (0x448250): returns the ID to the pool.
    virtual void ReleaseId();                                   // 2
    // Releases the ID and puts the job back into its class pool. Jobs without an ID are not
    // returned.
    virtual void ReturnToPool() = 0;                            // 3
    // vf04 (0x40d290): set for relay jobs.
    virtual BYTE IsRelayJob()                                   // 4
    {
        return m_bRelay;
    }

    // PoolHandle_AcquireId (0x448230): asserts the job has no ID yet and takes one from
    // g_OverlapJobIDPool.
    void AcquireId();
    // AssertFail_wrap_5 (0x4482b0): stores and returns whether more than m_dwTimeout ms passed
    // since Init.
    bool CheckTimedOut(DWORD dwTick);

public:
    DWORD   m_dwID;             // +4
    CTask*  m_pOwner;           // +8   task that gets the answer
    BYTE    m_bTimedOut;        // +C
    BYTE    m_bCheckTimeout;    // +D   relay jobs: swept by SweepTimedOutJobs
    DWORD   m_dwStartTick;      // +10
    DWORD   m_dwTimeout;        // +14  -1 after Init
    BYTE    m_bRelay;           // +18
};

C_ASSERT(FIELD_OFFSET(COverlapJob, m_dwID) == 4);
C_ASSERT(FIELD_OFFSET(COverlapJob, m_bCheckTimeout) == 0xD);
C_ASSERT(FIELD_OFFSET(COverlapJob, m_dwTimeout) == 0x14);
C_ASSERT(sizeof(COverlapJob) == 0x1C);

// Job run by the overlap process (0x6009). Every step returns true on success.
class COverlapJob_Local : public COverlapJob
{
public:
    // COverlapJob_Local::ctor (0x4482d0); m_nState is not initialised.
    COverlapJob_Local()
    {
        m_bCheckTimeout = 0;
    }

    virtual bool Prepare() = 0;                                 // 5
    virtual bool Execute() = 0;                                 // 6
    // Owner side, after the answer: state OVERLAP_JOB_STATE_DONE and not timed out.
    virtual bool OnComplete() = 0;                              // 7
    // Owner side, after the answer, otherwise.
    virtual bool OnFail() = 0;                                  // 8

public:
    volatile LONG   m_nState;   // +1C  OVERLAP_JOB_STATE_*
};

C_ASSERT(FIELD_OFFSET(COverlapJob_Local, m_nState) == 0x1C);
C_ASSERT(sizeof(COverlapJob_Local) == 0x20);

// Initialisation of the local data after certification: Execute passes 0x200C to the server
// application (CServerApp::OnControlMsg) on the overlap thread.
class COverlapJob_InitializeLocalData : public COverlapJob_Local
{
public:
    virtual void ReturnToPool();                                // 3  (0x43ef70)
    virtual bool Prepare();                                     // 5  (0x43eef0)
    virtual bool Execute();                                     // 6  (0x43ef00)
    virtual bool OnComplete();                                  // 7  (0x43ef30)
    virtual bool OnFail();                                      // 8  (0x43ef40)
};

C_ASSERT(sizeof(COverlapJob_InitializeLocalData) == 0x20);

// A message relayed to another server body, waiting for its 0xA008 answer.
class COverlapJob_RelayMsg : public COverlapJob
{
public:
    virtual void ReturnToPool();                                // 3  (0x4483b0)
    // Setup (0x448400): Init plus the relay data; marks the job as relay job with timeout
    // checking.
    virtual void Setup(CTask* pOwner, WORD wTargetBody, WORD wOpcode, WORD wSourceBody,
                       DWORD dwSourceJobID, DWORD dwContext);   // 5

public:
    WORD    m_wTargetBody;      // +1C  body the message went to
    WORD    m_wSourceBody;      // +1E  body the relay came from, 0 when it started here
    DWORD   m_dwSourceJobID;    // +20  job ID of the relay at m_wSourceBody
    WORD    m_wOpcode;          // +24  opcode of the relayed message
    DWORD   m_dwContext;        // +28  passed back to the handler of the answer
};

C_ASSERT(FIELD_OFFSET(COverlapJob_RelayMsg, m_wTargetBody) == 0x1C);
C_ASSERT(FIELD_OFFSET(COverlapJob_RelayMsg, m_dwSourceJobID) == 0x20);
C_ASSERT(FIELD_OFFSET(COverlapJob_RelayMsg, m_wOpcode) == 0x24);
C_ASSERT(FIELD_OFFSET(COverlapJob_RelayMsg, m_dwContext) == 0x28);
C_ASSERT(sizeof(COverlapJob_RelayMsg) == 0x2C);

typedef std::map<DWORD, COverlapJob*> OverlapJobMap;

// The jobs a server process waits for, by ID.
class COverlapJobManager
{
public:
    COverlapJobManager() {}
    virtual ~COverlapJobManager() {}                            // 0

    // SetField_0x40 (0x4485a0)
    void SetOwner(CTask* pOwner)
    {
        m_pOwner = pOwner;
    }

    // ServerFramework_Registry_InsertUnique_Locked (0x448700): asserts the ID is new.
    void Insert(COverlapJob* pJob);
    // RemoveAndReleaseObj (0x448800): asserts the job is registered, unregisters it and
    // returns it to its pool.
    void RemoveAndRelease(COverlapJob* pJob);
    // EraseMapEntryLocked (0x448900): unregisters and returns the job, NULL if unknown.
    COverlapJob* Remove(DWORD dwID);
    // ReleaseAllRegistryObjsLocked (0x4485b0): returns every job to its pool and empties the
    // map.
    void ReleaseAll();
    // SweepTimedOutRequests_SendA008 (0x448a30, not referenced): answers every timed out relay
    // job with 0xA008 {BYTE 2, DWORD ID, WORD opcode, DWORD context} to its owner's queue. The
    // jobs stay registered.
    void SweepTimedOutJobs();

public:
    CCriticalSectionBS  m_cs;           // +4
    CTask*              m_pOwner;       // +40  not initialised by the constructor
    OverlapJobMap       m_mapJob;       // +44
};

C_ASSERT(FIELD_OFFSET(COverlapJobManager, m_pOwner) == 0x40);
C_ASSERT(FIELD_OFFSET(COverlapJobManager, m_mapJob) == 0x44);
C_ASSERT(sizeof(COverlapJobManager) == 0x50);

// Overlap job IDs (0x53B8C0): a FIFO of free IDs refilled in steps of m_dwGrowBy. The three
// counters behind it are what IdPool_GetUsageCounters reports.
class COverlapJobIDPool
{
public:
    // IdPool53B8C0_StaticInit (0x448ea0). m_dwStatNextID and m_dwStatFree keep the zero of the
    // static storage until the first Pop.
    COverlapJobIDPool();

    // IdPool_PushNextIds (0x4496a0): queues the next nCount IDs.
    void PushNextIds(int nCount);
    // IdPool_PopFront (0x448f40)
    DWORD Pop();
    // The return half of COverlapJob::ReleaseId.
    void Push(DWORD dwID);

public:
    DWORD               m_dwMaxID;      // +0   -1
    DWORD               m_dwNextID;     // +4   first ID never handed out
    std::deque<DWORD>   m_deqFree;      // +8
    DWORD               m_dwGrowBy;     // +1C  500
    DWORD               m_dwStatNextID; // +20  m_dwNextID at the last Pop / Push
    DWORD               m_dwInUse;      // +24
    DWORD               m_dwStatFree;   // +28  m_dwNextID - m_dwInUse
};

C_ASSERT(FIELD_OFFSET(COverlapJobIDPool, m_dwGrowBy) == 0x1C);
C_ASSERT(FIELD_OFFSET(COverlapJobIDPool, m_dwInUse) == 0x24);
C_ASSERT(sizeof(COverlapJobIDPool) == 0x2C);

// 0x53B8C0
extern COverlapJobIDPool g_OverlapJobIDPool;
// 0x53B838 and 0x53B8F0: the class pools (single threaded allocators, 500 jobs per block).
extern CChunkAllocatorST<COverlapJob_InitializeLocalData> g_OverlapJobInitLocalDataPool;
extern CChunkAllocatorST<COverlapJob_RelayMsg> g_OverlapJobRelayMsgPool;

// OverlapJobInitLocalData_Pool_Acquire + PoolHandle_AcquireId
// (ServerProcessMain_AcquirePoolId_A003, 0x43ef50)
COverlapJob_InitializeLocalData* OverlapJob_AllocInitializeLocalData();
// RelayMsgPool_Acquire + PoolHandle_AcquireId (ServerFramework_LookupAndDispatch_448390)
COverlapJob_RelayMsg* OverlapJob_AllocRelayMsg();

} // namespace ServerFramework

// IdPool_GetUsageCounters (0x4481a0): the three counters of g_OverlapJobIDPool (next ID, IDs in
// use, IDs free).
void IdPool_GetUsageCounters(DWORD* pdwFirst, DWORD* pdwSecond, DWORD* pdwThird);
