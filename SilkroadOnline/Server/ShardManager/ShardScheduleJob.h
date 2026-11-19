#pragma once

#include <windows.h>

#include <list>
#include <string>
#include <utility>
#include <vector>

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

// The KEY=VALUE pairs of a _Schedule parameter string; ScheduleParamParser_Parse (0x666260)
// returns 1 when the string could be parsed.
typedef std::vector<std::pair<std::string, std::string> > ScheduleParamList;
BOOL ScheduleParamParser_Parse(const std::string& strParam, ScheduleParamList* pParams);

// The open times of a temple gate job (the list +0 and the open time +C of the job member the
// temple gate jobs pass).
struct sTempleOpenSchedule
{
    std::list<DWORD>    lstTime;            // +0   open time and the notices before it
    DWORD               dwOpenTime;         // +C
};

// ---------------------------------------------------------------------------------------------
// The schedule parameter helpers of the temple gate jobs (SM_world4, ShardScheduleParam.cpp)

// ScheduleParam_BuildOpenTimeList (0x666680): dwBegin + WORLD_OPEN_TIME minutes for each
// WORLD_OPEN_TIME entry (the last one is the open time), then the open time - NOTIFY_BEFORE_OPEN
// minutes for each NOTIFY_BEFORE_OPEN entry, sorted. FALSE without a WORLD_OPEN_TIME entry.
BOOL ScheduleParam_BuildOpenTimeList(const std::string& strParam, sTempleOpenSchedule* pOpen,
                                     DWORD dwBegin);
// ScheduleParam_GetInt (0x6669c0): atol of the first value of strKey.
BOOL ScheduleParam_GetInt(const std::string& strParam, std::string strKey, int* pnValue);

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

// The flag world participation phase (SCHEDULE_DEF_FLAG_WORLD_PARTICIPATION; slots 5 and 7 in
// FlagWorld.cpp).
class CShard_FlagWorldParticipation : public IShard_ScheduleJob
{
public:
    SHARD_SCHEDULE_JOB_SLOTS()
};

C_ASSERT(sizeof(CShard_FlagWorldParticipation) == 0x78);

// The flag world game (SCHEDULE_DEF_FLAG_WORLD; the class name is spelled so in the original).
// Begins 5 minutes before the game: 1 minute before it the game servers hear of it, at the game
// start the participant levels are requested (slots 5 to 7 in FlagWorld.cpp).
class CSahrd_FlagWorld : public IShard_ScheduleJob
{
public:
    SHARD_SCHEDULE_JOB_SLOTS()

    BYTE            m_byStage;          // +78  0 begun, 1 one minute notice sent, 2 game started
    DWORD           m_dwBegin;          // +7C
    DWORD           m_dwOneMinuteLeft;  // +80  begin + 240 s
    DWORD           m_dwGameStart;      // +84  begin + 300 s
};

C_ASSERT(FIELD_OFFSET(CSahrd_FlagWorld, m_byStage) == 0x78);
C_ASSERT(sizeof(CSahrd_FlagWorld) == 0x88);
