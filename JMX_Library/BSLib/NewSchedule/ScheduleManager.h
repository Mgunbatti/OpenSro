#pragma once

#include <windows.h>

#include <list>
#include <map>
#include <string>
#include <vector>

#include "BSLib/NewSchedule/Schedule.h"

// NewSchedule schedule groups and the begin / end event queue (August 2011 library,
// SR_ShardManager 0x6bf480..0x6c09a0, 0x6c77f0..0x6c8430). The application derives its manager
// (CShard_ScheduleManager, a static object) and implements the two event slots.
namespace NewSchedule
{

class CScheduleGroup;

// One row of _RefScheduleDefine as the server reads it (0x850 bytes). AddGroup uses the ID and
// the code name; the columns after the name are not known.
struct SCHEDULE_DEFINE_DATA
{
    DWORD   dwDefineID;         // +000
    char    szCodeName[0x84C];  // +004
};

C_ASSERT(sizeof(SCHEDULE_DEFINE_DATA) == 0x850);

// What a job gets for a schedule event.
struct sScheduleInfo
{
    std::string strDefine;      // +00 code name of the group
    DWORD       dwGroupID;      // +1C
    DWORD       dwScheduleID;   // +20
    DWORD       dwBegin;        // +24
    DWORD       dwEnd;          // +28
    BOOL        bBegin;         // +2C
    std::string strName;        // +30
};

C_ASSERT(FIELD_OFFSET(sScheduleInfo, dwScheduleID) == 0x20);
C_ASSERT(sizeof(sScheduleInfo) == 0x4C);

// An entry of the event queue: the begin (bBegin 1) or the end (0) of a period.
struct sScheduleEvent
{
    DWORD           dwBegin;        // +00
    DWORD           dwEnd;          // +04
    BYTE            bBegin;         // +08
    DWORD           dwScheduleID;   // +0C
    CScheduleGroup* pGroup;         // +10
    std::string     strName;        // +14

    // The event as job info: the group's code name and ID, bBegin 1 for a begin event.
    sScheduleInfo ToScheduleInfo() const;                                       // 0x6c0310
};

C_ASSERT(FIELD_OFFSET(sScheduleEvent, pGroup) == 0x10);
C_ASSERT(sizeof(sScheduleEvent) == 0x30);

// Schedule list order: begin time of begin events, end time of end events (unsigned).
bool ScheduleEventLessByTime(const sScheduleEvent& left, const sScheduleEvent& right); // 0x6c02e0
// Period list orders by begin time.
bool SchedulePeriodLess(const sSchedulePeriod& left, const sSchedulePeriod& right);   // 0x6c8460
bool SchedulePeriodGreater(const sSchedulePeriod& left, const sSchedulePeriod& right); // 0x6c8480

// The schedules (rules) of one _RefScheduleDefine entry (0x60 bytes, no vftable).
class CScheduleGroup
{
public:
    // The period is not cleared; the rule objects are not deleted with the group.
    CScheduleGroup() : m_dwDefineID(0) {}                                       // 0x6c77f0

    // A copy of the code name (an out-of-line copy at SR_ShardManager 0x663490).
    std::string GetDefineName() { return m_strName; }

    // A rule of pData->nType with a copy of the row; active unless it already ended. A row
    // with an ID that is already there leaves the new rule object behind.
    void AddSchedule(const SCHEDULE_DATA* pData);                               // 0x6c7870
    void RemoveSchedule(DWORD dwScheduleID);                                    // 0x6c7910
    ISchedule* FindActiveSchedule(DWORD dwScheduleID);                          // 0x6c79b0
    // Drops ended rules from the active map; the period running at now of every started rule
    // goes to lstPeriod; a rule that has not started gets its first period into its m_Period.
    void CollectActivePeriods(std::list<sSchedulePeriod>& lstPeriod, CDateTime& now); // 0x6c7a10
    // Up to nCount periods of every rule before t (GetPrevPeriod backwards), latest first; the
    // list keeps the first nCount.
    void GetPrevPeriods(std::list<sSchedulePeriod>& lstPeriod, int nCount, CDateTime t); // 0x6c7c30
    // Up to nCount periods of every rule after t (GetCurrentPeriod forwards), earliest first.
    void GetNextPeriods(std::list<sSchedulePeriod>& lstPeriod, int nCount, CDateTime t); // 0x6c7e80
    // The earliest m_Period of the active rules (bAdvance: a period that began by now moves on
    // first); expired rules leave the active map.
    BOOL GetNearestPeriod(CDateTime& now, sSchedulePeriod* pPeriod, BOOL bAdvance); // 0x6c8210
    // GetCurrentPeriod of an active rule into pCur and its m_Period.
    BOOL GetSchedulePeriod(DWORD dwScheduleID, CDateTime& now, sSchedulePeriod* pCur); // 0x6c8430

public:
    sSchedulePeriod                 m_Period;       // +00 the group's next period
    std::map<DWORD, ISchedule*>     m_mapSchedule;  // +28 every rule by schedule ID
    std::map<DWORD, ISchedule*>     m_mapActive;    // +34 rules that have not ended
    DWORD                           m_dwDefineID;   // +40
    std::string                     m_strName;      // +44 code name
};

C_ASSERT(FIELD_OFFSET(CScheduleGroup, m_mapSchedule) == 0x28);
C_ASSERT(FIELD_OFFSET(CScheduleGroup, m_dwDefineID) == 0x40);
C_ASSERT(sizeof(CScheduleGroup) == 0x60);

// CScheduleManager (vftable 0x79a050).
class CScheduleManager
{
public:
    CScheduleManager();                                                         // 0x6bf480
    // Not virtual, like the original.
    ~CScheduleManager();                                                        // 0x6bf510

    // Fills the event queue from the groups: for every running period an end event (the
    // application hears of it through slot 2 first), the group's nearest period as a begin
    // and an end event; sorts and starts the queue.
    virtual void RebuildEventList();                                            // 0  0x6bf740
    // Hands every due event to slot 3 and drops it; after a begin event the group's next
    // period is queued.
    virtual void ProcessDueEvents();                                            // 1  0x6bfca0
    virtual void OnServiceStartSchedule(sScheduleEvent& event) = 0;            // 2
    virtual void OnScheduleEvent(sScheduleEvent& event) = 0;                   // 3

    void DeleteAllGroups();                                                     // 0x6bf5c0
    void AddGroup(const SCHEDULE_DEFINE_DATA* pDefine);                         // 0x6bffb0
    // The first group with the ID; the search stops at a NULL entry.
    CScheduleGroup* FindGroup(DWORD dwDefineID);                                // 0x6c0050
    // Adds the rule to its group; while the queue runs, the group's queued events are
    // replaced by its new nearest period.
    void AddSchedule(const SCHEDULE_DATA* pData);                               // 0x6c0090
    void RemoveSchedule(const SCHEDULE_DATA* pData);                            // 0x6c0290
    // CScheduleGroup::GetPrevPeriods / GetNextPeriods of the first group with the code name.
    void GetPrevPeriods(std::list<sSchedulePeriod>& lstPeriod, std::string strDefine,
                        int nCount, CDateTime t);                               // 0x6c0400
    void GetNextPeriods(std::list<sSchedulePeriod>& lstPeriod, std::string strDefine,
                        int nCount, CDateTime t);                               // 0x6c05e0
    // Appends an event of the schedule (nothing without one).
    void PushEvent(CScheduleGroup* pGroup, DWORD dwBegin, DWORD dwEnd, BYTE bBegin,
                   ISchedule* pSchedule);                                       // 0x6c07c0
    void RemoveBeginEvents(DWORD dwScheduleID);                                 // 0x6c08d0
    // The last queued end event of the schedule.
    void RemoveEndEvent(DWORD dwScheduleID);                                    // 0x6c0930
    // A copy of the first queued begin event of the group.
    BOOL FindBeginEventOfGroup(sScheduleEvent* pEvent, DWORD dwDefineID);       // 0x6c09a0

public:
    std::vector<CScheduleGroup*>    m_vecGroup;     // +04
    std::list<sScheduleEvent>       m_listEvent;    // +14
    BOOL                            m_bRunning;     // +20 RebuildEventList ran
};

C_ASSERT(FIELD_OFFSET(CScheduleManager, m_listEvent) == 0x14);
C_ASSERT(sizeof(CScheduleManager) == 0x24);

} // namespace NewSchedule
