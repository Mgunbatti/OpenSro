#pragma once

#include <winsock2.h>
#include <windows.h>

#include "BSLib/ChunkAllocator.h"
#include "ServerFramework/OverlapJob.h"
#include "ServerFramework/ServerProcessMain.h"

// Overlap jobs of the GlobalManager main process (run on the overlap process, mostly database
// work on the account DB).

// 0x558C98: the main process, set by its Initialize. Declared as the framework base until the
// main process class is ported.
extern ServerFramework::CServerProcessMain* g_pGlobalManagerProcess;

// COverlapJob_UpdatePlayTimeOfUser (0x28 bytes): writes the play time of a user that logged out
// to TB_USER.
class COverlapJob_UpdatePlayTimeOfUser : public ServerFramework::COverlapJob_Local
{
public:
    // COverlapJob_UpdatePlayTimeOfUser::ctor (0x41b120)
    COverlapJob_UpdatePlayTimeOfUser() {}

    // vf03 (0x413e80): releases the ID and goes back to its pool (jobs with an ID only).
    virtual void ReturnToPool();                                // 3
    // vf05 (0x413f00): true.
    virtual bool Prepare();                                     // 5
    // vf06 (0x413f10): "update TB_USER set AccPlayTime = ..., LatestUpdateTime_ToPlayTime = <now>
    // where JID = ..." on the account DB (connection 3); true unless the query fails.
    virtual bool Execute();                                     // 6
    // vf07 / vf08 (0x413f90 / 0x413fa0): true.
    virtual bool OnComplete();                                  // 7
    virtual bool OnFail();                                      // 8

    // sub_413ED0: Init with the main process' task, then the data.
    void Setup(DWORD dwJID, WORD wPlayTime);

public:
    DWORD   m_dwJID;        // +20
    WORD    m_wPlayTime;    // +24
};

C_ASSERT(FIELD_OFFSET(COverlapJob_UpdatePlayTimeOfUser, m_dwJID) == 0x20);
C_ASSERT(sizeof(COverlapJob_UpdatePlayTimeOfUser) == 0x28);

// 0x59A7F0: the job pool.
extern CChunkAllocatorST<COverlapJob_UpdatePlayTimeOfUser> g_UpdatePlayTimeOfUserJobPool;

// sub_413E60: a job from the pool with a job ID.
COverlapJob_UpdatePlayTimeOfUser* OverlapJob_AllocUpdatePlayTimeOfUser();
