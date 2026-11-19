#include "ShardManager/ShardScheduleJob.h"

#include "BSLib/BSDebug.h"
#include "BSNet/Msg.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerCommon/ReferenceData.h"
#include "ShardManager/GameWorld.h"

// The code each begin and end handler repeats inline.
void IShard_ScheduleJob::LogSchedule(const char* pszText)
{
    if (pszText)
        LogPrintf(g_nScheduleLogMode != 1 ? 0 : 0x2000001, pszText);
    const char* pszState = "Start";
    if (m_Info.bBegin != 1)
        pszState = "End";
    int nServerID = g_pwShardServerID ? *g_pwShardServerID : -1;
    Schedule_InsertLogAsync(std::string("SR_ShardManager"), nServerID, m_Info.strDefine,
                            m_Info.dwScheduleID, std::string(pszState));
}

static void WriteLayerID(CMsg* pMsg, const GameWorldLayer* pLayer)
{
    DWORD dwLayerID = pLayer->m_wLayerID;
    pMsg->WriteBytes(&dwLayerID, 2);
}

// The parameters of the scheduled instance layers: a layer that closes after dwDuration and
// stays dwEmptyRemainTime while empty.
static void SetScheduledLayerParam(sGameWorldCreateParam& param, DWORD dwDuration,
                                   DWORD dwEmptyRemainTime)
{
    param.m_dw0 = 0;
    param.m_b4 = 7;
    param.m_dw8 = 0;
    param.m_b12 = 1;
    param.m_b13 = 0;
    param.m_dwDuration = dwDuration;
    param.m_dwEmptyRemainTime = dwEmptyRemainTime;
    param.m_b24 = 0;
}

// ---------------------------------------------------------------------------------------------
// CShardSJ_ChinsTomb

BOOL CShardSJ_ChinsTomb::OnBegin()                      // 0x65d770
{
    if (g_pShardScheduleExecutor->FindJobByName(m_Info.strDefine))
        return FALSE;
    if (!g_pMainProcess)
        BSAssertFail();
    if (!CMainProcess::CountReadyGameServers())
        return FALSE;
    OpenGameWorlds();
    if (!m_pFloor5 || !m_pFloor6)
        return FALSE;

    BeginNotify(NULL);
    WriteLayerID(m_pNotifyMsg, m_pFloor5);
    WriteLayerID(m_pNotifyMsg, m_pFloor6);
    BroadcastNotify();
    LogSchedule("Scheduleing : Chin's tomb instance is now opened");
    return TRUE;
}

void CShardSJ_ChinsTomb::OnEnd(CDateTime& /*now*/)     // 0x65d900
{
    if (!g_pMainProcess)
        BSAssertFail();
    if (!CMainProcess::CountReadyGameServers() || !m_pFloor5 || !m_pFloor6)
        return;

    BeginNotify(NULL);
    WriteLayerID(m_pNotifyMsg, m_pFloor5);
    WriteLayerID(m_pNotifyMsg, m_pFloor6);
    BroadcastNotify();
    // ShardSJ_ChinsTomb_ResetState (0x65dda0)
    m_pFloor5 = NULL;
    m_pFloor6 = NULL;
    LogSchedule("Scheduleing : Chin's tomb instance is now closed");
}

void CShardSJ_ChinsTomb::OpenGameWorlds()
{
    RefGameWorld* pRef5 =
        CReferenceData::GetInstance()->FindGameWorldByCodeName(std::string("INS_QINSHI_FLOOR_5"));
    RefGameWorld* pRef6 =
        CReferenceData::GetInstance()->FindGameWorldByCodeName(std::string("INS_QINSHI_FLOOR_6"));
    if (!pRef5 || !pRef6)
    {
        LogPrintf(0x2000000,
                  "Chin's tomb data is not exists!! check the gust and service for the data");
        BSAssertFail();
        return;
    }

    sGameWorldCreateParam param5;
    WORD wWorldID5 = (WORD)pRef5->ID;
    BYTE byResult = 1;
    SetScheduledLayerParam(param5, 18000000, 21600000);
    CGameWorld* pWorld5 = CGameWorldMgr::GetInstance()->FindWorld(wWorldID5);
    if (!pWorld5)
    {
        LogPrintf(0x2000000, "Failed to find GameWorld!!");
        return;
    }
    m_pFloor5 = CGameWorldMgr::GetInstance()->OpenLayer(&byResult, wWorldID5, &param5);
    if (!m_pFloor5)
    {
        LogPrintf(0x2000000, "Open Chin's tomb GameWorld (Floor 5) Failed!! error[%d]", byResult);
        return;
    }
    WORD wOpened5 = (WORD)pWorld5->m_pRef->ID;
    CGameWorldMgr::GetInstance();
    CGameWorldMgr::BroadcastOpened(wOpened5, m_pFloor5);

    sGameWorldCreateParam param6;
    WORD wWorldID6 = (WORD)pRef6->ID;
    byResult = 1;
    SetScheduledLayerParam(param6, 18000000, 21600000);
    CGameWorld* pWorld6 = CGameWorldMgr::GetInstance()->FindWorld(wWorldID6);
    if (!pWorld6)
    {
        LogPrintf(0x2000000, "Failed to find GameWorld!!");
        return;
    }
    m_pFloor6 = CGameWorldMgr::GetInstance()->OpenLayer(&byResult, wWorldID6, &param6);
    if (!m_pFloor6)
    {
        LogPrintf(0x2000000, "Open Chin's tomb GameWorld (Floor 6) Failed!! error[%d]", byResult);
        return;
    }
    WORD wOpened6 = (WORD)pWorld6->m_pRef->ID;
    CGameWorldMgr::GetInstance();
    CGameWorldMgr::BroadcastOpened(wOpened6, m_pFloor6);
}

// ---------------------------------------------------------------------------------------------
// CShardSJ_Roc

BOOL CShardSJ_Roc::OnBegin()                            // 0x65e290
{
    if (g_pShardScheduleExecutor->FindJobByName(m_Info.strDefine))
        return FALSE;
    if (!g_pMainProcess)
        BSAssertFail();
    if (!CMainProcess::CountReadyGameServers())
        return FALSE;
    OpenGameWorld();
    if (!m_pLayer)
        return FALSE;

    m_Info.bBegin = 1;
    BeginNotify(NULL);
    WriteLayerID(m_pNotifyMsg, m_pLayer);
    BroadcastNotify();
    LogSchedule("Scheduleing : Roc instance is now opened");
    return TRUE;
}

void CShardSJ_Roc::OnEnd(CDateTime& /*now*/)           // 0x65e400
{
    if (!g_pMainProcess)
        BSAssertFail();
    if (!CMainProcess::CountReadyGameServers() || !m_pLayer)
        return;

    m_Info.bBegin = 0;
    BeginNotify(NULL);
    WriteLayerID(m_pNotifyMsg, m_pLayer);
    BroadcastNotify();
    LogSchedule("Scheduleing : Roc instance is now closed");
}

void CShardSJ_Roc::OpenGameWorld()
{
    RefGameWorld* pRef =
        CReferenceData::GetInstance()->FindGameWorldByCodeName(std::string("INS_ROC_TOP"));
    if (!pRef)
    {
        LogPrintf(0x2000000, "RocLoad data is not exists!! check the gust and service for the data");
        BSAssertFail();
        return;
    }

    sGameWorldCreateParam param;
    WORD wWorldID = (WORD)pRef->ID;
    BYTE byResult = 1;
    SetScheduledLayerParam(param, (DWORD)pRef->WorldDurationTime,
                           (DWORD)pRef->WorldEmptyRemainTime);
    CGameWorld* pWorld = CGameWorldMgr::GetInstance()->FindWorld(wWorldID);
    if (!pWorld)
    {
        LogPrintf(0x2000000, "Failed to find GameWorld!!");
        return;
    }
    m_pLayer = CGameWorldMgr::GetInstance()->OpenLayer(&byResult, wWorldID, &param);
    if (!m_pLayer)
    {
        LogPrintf(0x2000000, "Open Roc GameWorld Failed!! error[%d]", byResult);
        return;
    }
    WORD wOpened = (WORD)pWorld->m_pRef->ID;
    CGameWorldMgr::GetInstance();
    CGameWorldMgr::BroadcastOpened(wOpened, m_pLayer);
}

// ---------------------------------------------------------------------------------------------
// CShardSJ_EUBusinessEventUpdate, CShardSJ_UniqueSpawn, CShardSJ_MentorHonor

BOOL CShardSJ_EUBusinessEventUpdate::OnBegin()          // 0x65ddb0
{
    BeginNotify(NULL);
    BroadcastNotify();
    LogSchedule("Scheduleing : CHANGE_EXP_RATIO_EVENT_START");
    return TRUE;
}

void CShardSJ_EUBusinessEventUpdate::OnEnd(CDateTime& /*now*/)     // 0x65def0
{
    BeginNotify(NULL);
    BroadcastNotify();
    LogSchedule("Scheduleing : CHANGE_EXP_RATIO_EVENT_STOP");
}

BOOL CShardSJ_UniqueSpawn::OnBegin()                    // 0x65e030
{
    if (!g_pMainProcess)
        BSAssertFail();
    UniqueSpawn_BroadcastL3Uniques_34BB(0);
    LogSchedule("Scheduleing : Unique spawn has been started");
    return TRUE;
}

void CShardSJ_UniqueSpawn::OnEnd(CDateTime& /*now*/)   // 0x65e180
{
    LogSchedule(NULL);
}

BOOL CShardSJ_MentorHonor::OnBegin()                    // 0x65e720
{
    TrainingCamp_DBUpdateHonorRank();
    LogSchedule("Scheduleing : Mentor Honor Record Update is Start & End ..... !");
    return TRUE;
}

void CShardSJ_MentorHonor::OnEnd(CDateTime& /*now*/)   // 0x65e870
{
    LogSchedule(NULL);
}
