#pragma once

#include <windows.h>

#include <string>

#include "BSLib/NewSchedule/ScheduleExecutor.h"

class CMsg;
class GameWorldLayer;

// The schedule jobs of SR_ShardManager (IShard_ScheduleJob and the CShardSJ_* classes). This file
// holds the begin and end handlers of the game world, event and mentor jobs; the constructors,
// the instance slots, the base class helpers and the remaining jobs belong to SM_world3 and are
// only declared here ("Provided by SM_world3").

// ---------------------------------------------------------------------------------------------
// Provided by SM_world3 / SM_world1 / SM_main

// dword_8A7B5C (CShard_ScheduleExecutor::StaticInit): 1 logs the schedule lines at level 0x2000001,
// else at 0.
extern int g_nScheduleLogMode;
// The static CShard_ScheduleExecutor (0x8A7B48).
extern NewSchedule::CScheduleExecutor* g_pShardScheduleExecutor;
// dword_8588B4 (SM_main): the server ID WORD of this module, NULL before it is known.
extern WORD* g_pwShardServerID;

// Schedule_InsertLogAsync (0x473880, SM_world1): INSERT into _LogSchedule through an AQ job.
void Schedule_InsertLogAsync(std::string strModule, int nServerID, std::string strDefine,
                             DWORD dwScheduleID, std::string strState);
// UniqueSpawn_BroadcastL3Uniques_34BB (0x414450, SM_main).
void UniqueSpawn_BroadcastL3Uniques_34BB(BYTE byFlag);
// TrainingCamp_DBUpdateHonorRank (0x459a00) on the training camp manager (dword_8A8930, asserts
// when there is none).
void TrainingCamp_DBUpdateHonorRank();

// IShard_ScheduleJob (0x78 bytes; ctor 0x6644d0).
class IShard_ScheduleJob : public NewSchedule::IScheduleJob
{
public:
    // IShard_ScheduleJob::BeginNotify_34FE (0x666130): a new 0x34FE message in m_pNotifyMsg with
    // the schedule info of pInfo (this job's own when NULL).
    void BeginNotify(const NewSchedule::sScheduleInfo* pInfo);
    // ShardSJ_BroadcastToGameServers (0x6661c0): m_pNotifyMsg to every game server.
    void BroadcastNotify();

    // The two log lines of every begin and end: LogPrintf of pszText (NULL: none), then the
    // _LogSchedule row "Start" / "End".
    void LogSchedule(const char* pszText);

    CMsg*           m_pNotifyMsg;       // +70
    BYTE            m_b74;              // +74
};

C_ASSERT(FIELD_OFFSET(IShard_ScheduleJob, m_pNotifyMsg) == 0x70);
C_ASSERT(sizeof(IShard_ScheduleJob) == 0x78);

#define SHARD_SCHEDULE_JOB_SLOTS()                                                          \
    virtual void CreateInstance();                                                          \
    virtual void DeleteInstance();                                                          \
    virtual BOOL CanBegin();                                                                \
    virtual BOOL OnBegin();                                                                 \
    virtual BOOL OnTick(CDateTime& now);                                                    \
    virtual void OnEnd(CDateTime& now);

// Chin's tomb floors 5 and 6 (INS_QINSHI_FLOOR_5 / _6).
class CShardSJ_ChinsTomb : public IShard_ScheduleJob
{
public:
    SHARD_SCHEDULE_JOB_SLOTS()
    // CShardSJ_ChinsTomb::OpenGameWorlds (0x65da80).
    void OpenGameWorlds();

    GameWorldLayer* m_pFloor5;          // +78
    GameWorldLayer* m_pFloor6;          // +7C
};

C_ASSERT(FIELD_OFFSET(CShardSJ_ChinsTomb, m_pFloor5) == 0x78);

// The Roc (INS_ROC_TOP).
class CShardSJ_Roc : public IShard_ScheduleJob
{
public:
    SHARD_SCHEDULE_JOB_SLOTS()
    // CShardSJ_Roc::OpenGameWorld (0x65e560).
    void OpenGameWorld();

    GameWorldLayer* m_pLayer;           // +78
};

C_ASSERT(FIELD_OFFSET(CShardSJ_Roc, m_pLayer) == 0x78);

class CShardSJ_EUBusinessEventUpdate : public IShard_ScheduleJob
{
public:
    SHARD_SCHEDULE_JOB_SLOTS()
};

class CShardSJ_UniqueSpawn : public IShard_ScheduleJob
{
public:
    SHARD_SCHEDULE_JOB_SLOTS()
};

class CShardSJ_MentorHonor : public IShard_ScheduleJob
{
public:
    SHARD_SCHEDULE_JOB_SLOTS()
};
