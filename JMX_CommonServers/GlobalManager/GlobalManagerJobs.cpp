#include "GlobalManagerJobs.h"

#include <string.h>

#include "GlobalManagerDB.h"
#include "BSLib/DB/DBQuery.h"
#include "BSLib/Util/TimeSpan.h"

using namespace ServerFramework;

ServerFramework::CServerProcessMain* g_pGlobalManagerProcess;

CChunkAllocatorST<COverlapJob_UpdatePlayTimeOfUser> g_UpdatePlayTimeOfUserJobPool;

// ---------------------------------------------------------------------------------------------
// COverlapJob_UpdatePlayTimeOfUser

COverlapJob_UpdatePlayTimeOfUser* OverlapJob_AllocUpdatePlayTimeOfUser()
{
    COverlapJob_UpdatePlayTimeOfUser* pJob = g_UpdatePlayTimeOfUserJobPool.Alloc();
    pJob->AcquireId();
    return pJob;
}

void COverlapJob_UpdatePlayTimeOfUser::ReturnToPool()
{
    if (m_dwID)
    {
        ReleaseId();
        g_UpdatePlayTimeOfUserJobPool.Free(this);
    }
}

void COverlapJob_UpdatePlayTimeOfUser::Setup(DWORD dwJID, WORD wPlayTime)
{
    Init((CTask*)g_pGlobalManagerProcess->GetOwner());
    m_dwJID = dwJID;
    m_wPlayTime = wPlayTime;
}

bool COverlapJob_UpdatePlayTimeOfUser::Prepare()
{
    return true;
}

bool COverlapJob_UpdatePlayTimeOfUser::Execute()
{
    // The original fills only the session and the connection index of the query.
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pAccountDBSession;
    query.m_nSession = 3;
    DWORD dwJID = m_dwJID;
    return query.ExecuteFormat("update TB_USER set AccPlayTime = %d, "
                               "LatestUpdateTime_ToPlayTime = %d where JID = %d",
                               m_wPlayTime, CDateTime::GetCurrent().GetTime(), dwJID) >= 0;
}

bool COverlapJob_UpdatePlayTimeOfUser::OnComplete()
{
    return true;
}

bool COverlapJob_UpdatePlayTimeOfUser::OnFail()
{
    return true;
}
