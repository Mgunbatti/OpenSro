#pragma once

#include <windows.h>
#include <string.h>
#include <time.h>

#include <string>

#include "BSLib/Util/TimeSpan.h"

// NewSchedule: schedule rules of the August 2011 library (SR_ShardManager 0x6d6d60..0x6dea10).
// A rule turns one row of the _Schedule table into periods (begin, end) in local time. Weekdays
// count from 1 = Sunday like CDateTime::GetDayOfWeek; weeks of a month start on Sunday, the
// first (partial) week is week 1.
namespace NewSchedule
{

// SCHEDULE_DATA::nType, the classes ISchedule::CreateByType makes.
enum
{
    SCHEDULE_TYPE_EVERYDAYS             = 1,
    SCHEDULE_TYPE_SOMEDAYS              = 2,
    SCHEDULE_TYPE_EVERYWEEKS_DAYOFWEEK  = 3,
    SCHEDULE_TYPE_SOMEWEEKS_DAYOFWEEK   = 4,
    SCHEDULE_TYPE_EVERYMONTHS_DAYOFWEEK = 5,
    SCHEDULE_TYPE_EVERYMONTHS_DATE      = 6,
    SCHEDULE_TYPE_SOMEMONTHS_DAYOFWEEK  = 7,
    SCHEDULE_TYPE_SOMEMONTHS_DATE       = 8,
    SCHEDULE_TYPE_REPEAT_SOMETIME       = 9,
    SCHEDULE_TYPE_ONCE_DATE             = 10
};

#pragma pack(push, 1)

// One row of _Schedule as the server reads it (0x913 bytes). The rule fields are read
// differently per type: nInterval is the cycle in days, weeks or months, byDay / byWeek /
// byMonth the position in that cycle (or the day of the month), byHour..bySecond the daily
// start and nDuration the length of a period in seconds.
struct SCHEDULE_DATA
{
    DWORD       dwScheduleID;       // +000
    DWORD       dwDefineID;         // +004 group (_RefScheduleDefine)
    SYSTEMTIME  stBegin;            // +008 the rule is valid from
    SYSTEMTIME  stEnd;              // +018 and up to
    int         nType;              // +028 SCHEDULE_TYPE_*
    int         nInterval;          // +02C
    BYTE        byDayOfWeek;        // +030 1 = Sunday
    BYTE        byDay;              // +031
    BYTE        byWeek;             // +032
    BYTE        byMonth;            // +033
    BYTE        byHour;             // +034
    BYTE        byMinute;           // +035
    BYTE        bySecond;           // +036
    int         nDuration;          // +037 seconds
    int         nRepeatInterval;    // +03B minutes (Repeat_Sometime)
    int         nRepeatLength;      // +03F minutes (Repeat_Sometime)
    char        szName[256];        // +043
    char        szParam[2000];      // +143
};

#pragma pack(pop)

C_ASSERT(FIELD_OFFSET(SCHEDULE_DATA, nType) == 0x28);
C_ASSERT(FIELD_OFFSET(SCHEDULE_DATA, nDuration) == 0x37);
C_ASSERT(FIELD_OFFSET(SCHEDULE_DATA, szName) == 0x43);
C_ASSERT(sizeof(SCHEDULE_DATA) == 0x913);

// A period of a schedule (ScheduleEntry in the analysis).
struct sSchedulePeriod
{
    DWORD       dwScheduleID;   // +00
    __time32_t  tBegin;         // +04
    __time32_t  tEnd;           // +08
    std::string strName;        // +0C
};

C_ASSERT(sizeof(sSchedulePeriod) == 0x28);

// BUG(FW11-023): the original clears periods, events and job infos after constructing them;
// the string loses its capacity and the next short name it gets is a lost allocation.
template <class T>
inline void ZeroScheduleStruct(T& value)
{
    memset(&value, 0, sizeof(value));
}

// ISchedule (vftable 0x79ab78): the rule base class. Not a virtual destructor, like the
// original; the rule classes add no members.
class ISchedule
{
public:
    ISchedule();                                                                // 0x6d7170

    // Factory by pData->nType (1..10); NULL for any other type or a NULL row. The row itself
    // is copied by the caller (CScheduleGroup::AddSchedule).
    static ISchedule* CreateByType(const SCHEDULE_DATA* pData);                 // 0x6d7c10

    // min(t, end of validity)
    virtual CDateTime ClampToEndTime(const CDateTime& t);                       // 0  0x6d7320
    // The period running at t into *pCur (return value: one is running) and the next one into
    // *pNext; FALSE without pCur.
    virtual BOOL GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                  sSchedulePeriod* pNext) = 0;                  // 1
    // Moves m_Period to the following period.
    virtual BOOL AdvanceToNextPeriod(CDateTime& t) = 0;                         // 2
    // The last period before t (for most rules the last one that ended by t) into *pPeriod.
    virtual BOOL GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod) = 0;      // 3

    CDateTime GetStartTime();                                                   // 0x6d7200
    // t later than the start / the end of validity.
    BOOL IsAfterStart(CDateTime& t);                                            // 0x6d7230
    BOOL IsAfterEnd(CDateTime& t);                                              // 0x6d7280
    // Begin or end of m_Period after the end of validity.
    BOOL IsPeriodExpired();                                                     // 0x6d72d0

protected:
    void SetPeriodIdAndName(sSchedulePeriod* pPeriod);
    void SetPeriodName(sSchedulePeriod* pPeriod);

public:
    int             m_nType;    // +004 set by the rule constructors
    sSchedulePeriod m_Period;   // +008 the next period (kept by CScheduleGroup)
    SCHEDULE_DATA   m_Data;     // +030
};

C_ASSERT(FIELD_OFFSET(ISchedule, m_Period) == 0x08);
C_ASSERT(FIELD_OFFSET(ISchedule, m_Data) == 0x30);
C_ASSERT(sizeof(ISchedule) == 0x944);

// Every day at byHour:byMinute:bySecond.
class CSchedule_Everydays : public ISchedule
{
public:
    CSchedule_Everydays();                                                      // 0x6d6d60
    virtual BOOL GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                  sSchedulePeriod* pNext);                      // 0x6dc750
    virtual BOOL AdvanceToNextPeriod(CDateTime& t);                             // 0x6dc8b0
    virtual BOOL GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod);          // 0x6dc930
};

// Day byDay of every cycle of nInterval days counted from the start date.
class CSchedule_Somedays : public ISchedule
{
public:
    CSchedule_Somedays();                                                       // 0x6d6e10
    virtual BOOL GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                  sSchedulePeriod* pNext);                      // 0x6dcab0
    virtual BOOL AdvanceToNextPeriod(CDateTime& t);                             // 0x6dcce0
    virtual BOOL GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod);          // 0x6dcd00
    // The period of a day that matches the rule.
    virtual BOOL GetPeriodOnMatchingDay(CDateTime& t, sSchedulePeriod* pCur,
                                        sSchedulePeriod* pNext);                // 4  0x6dcbd0

private:
    BOOL CheckCurrentPeriod(CDateTime& start, CDateTime& t, int nDays,
                            sSchedulePeriod* pCur);                             // 0x6dcf80
    void FillPeriodAtDayOffset(sSchedulePeriod* pPeriod, CDateTime& t, int nOffset); // 0x6dd0c0
    sSchedulePeriod MakePeriodAt(int nOffset, CDateTime t);                     // 0x6dd130
};

// Every week on byDayOfWeek.
class CSchedule_Everyweeks_Dayofweek : public ISchedule
{
public:
    CSchedule_Everyweeks_Dayofweek();                                           // 0x6d6e70
    virtual BOOL GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                  sSchedulePeriod* pNext);                      // 0x6dd1f0
    virtual BOOL AdvanceToNextPeriod(CDateTime& t);                             // 0x6dd450
    virtual BOOL GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod);          // 0x6dd470
    virtual BOOL GetPeriodOnMatchingDay(CDateTime& t, sSchedulePeriod* pCur,
                                        sSchedulePeriod* pNext);                // 4  0x6dd340

private:
    BOOL CheckCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur);              // 0x6dd6a0
    void FillPeriodAtDayOffset(sSchedulePeriod* pPeriod, int nOffset, CDateTime t); // 0x6dd830
    sSchedulePeriod MakePeriodAt(int nOffset, CDateTime t);                     // 0x6dd8a0
};

// byDayOfWeek of week byWeek of every cycle of nInterval weeks counted from the start date.
class CSchedule_Someweeks_Dayofweek : public ISchedule
{
public:
    CSchedule_Someweeks_Dayofweek();                                            // 0x6d6ed0
    virtual BOOL GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                  sSchedulePeriod* pNext);                      // 0x6dd960
    virtual BOOL AdvanceToNextPeriod(CDateTime& t);                             // 0x6ddc30
    virtual BOOL GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod);          // 0x6ddc50
    virtual BOOL GetPeriodOnMatchingDay(CDateTime& t, sSchedulePeriod* pCur,
                                        sSchedulePeriod* pNext);                // 4  0x6ddae0

private:
    BOOL CheckCurrentPeriod(CDateTime& start, CDateTime& t, int nWeeks,
                            sSchedulePeriod* pCur);                             // 0x6de080
    void FillPeriodAtDayOffset(sSchedulePeriod* pPeriod, CDateTime t);         // 0x6de1b0
    sSchedulePeriod MakePeriodAt(CDateTime t);                                  // 0x6de220
};

// byDayOfWeek of week byWeek of every month.
class CSchedule_Everymonths_Dayofweek : public ISchedule
{
public:
    CSchedule_Everymonths_Dayofweek();                                          // 0x6d6f30
    virtual BOOL GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                  sSchedulePeriod* pNext);                      // 0x6de2e0
    virtual BOOL AdvanceToNextPeriod(CDateTime& t);                             // 0x6de540
    virtual BOOL GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod);          // 0x6de600
    virtual BOOL GetPeriodOnMatchingDay(CDateTime& t, sSchedulePeriod* pCur,
                                        sSchedulePeriod* pNext);                // 4  0x6de430

private:
    BOOL CheckCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur);              // 0x6de900
    sSchedulePeriod MakePeriodAt(int nMonthOffset, CDateTime t);                // 0x6dea10
};

// Day byDay of every month.
class CSchedule_Everymonths_Date : public ISchedule
{
public:
    CSchedule_Everymonths_Date();                                               // 0x6d6f90
    virtual BOOL GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                  sSchedulePeriod* pNext);                      // 0x6d9e40
    virtual BOOL AdvanceToNextPeriod(CDateTime& t);                             // 0x6da120
    virtual BOOL GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod);          // 0x6da1b0
    virtual BOOL GetPeriodOnMatchingDay(CDateTime& t, sSchedulePeriod* pCur,
                                        sSchedulePeriod* pNext);                // 4  0x6d9f70

private:
    BOOL CheckCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur);              // 0x6da3a0
};

// byDayOfWeek of week byWeek of month byMonth of every cycle of nInterval months.
class CSchedule_Somemonths_Dayofweek : public ISchedule
{
public:
    CSchedule_Somemonths_Dayofweek();                                           // 0x6d6ff0
    virtual BOOL GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                  sSchedulePeriod* pNext);                      // 0x6da4a0
    virtual BOOL AdvanceToNextPeriod(CDateTime& t);                             // 0x6da7b0
    virtual BOOL GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod);          // 0x6da870
    virtual BOOL GetPeriodOnMatchingDay(CDateTime& t, sSchedulePeriod* pCur,
                                        sSchedulePeriod* pNext);                // 4  0x6da6a0

private:
    BOOL CheckCurrentPeriod(int nMonths, CDateTime& start, CDateTime& t,
                            sSchedulePeriod* pCur);                             // 0x6dac70
    sSchedulePeriod MakePeriodAt(int nMonthOffset, CDateTime t);                // 0x6daea0
};

// Day byDay of month byMonth of every cycle of nInterval months.
class CSchedule_Somemonths_Date : public ISchedule
{
public:
    CSchedule_Somemonths_Date();                                                // 0x6d7050
    virtual BOOL GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                  sSchedulePeriod* pNext);                      // 0x6dafb0
    virtual BOOL AdvanceToNextPeriod(CDateTime& t);                             // 0x6db250
    virtual BOOL GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod);          // 0x6db2d0
    virtual BOOL GetPeriodOnMatchingDay(CDateTime& t, sSchedulePeriod* pCur,
                                        sSchedulePeriod* pNext);                // 4  0x6db140

private:
    BOOL CheckCurrentPeriod(int nMonths, CDateTime& start, CDateTime& t,
                            sSchedulePeriod* pCur);                             // 0x6db640
    void FillPeriodAtMonthOffset(sSchedulePeriod* pPeriod, int nMonthOffset,
                                 CDateTime t);                                  // 0x6db750
    sSchedulePeriod MakePeriodAt(int nMonthOffset, CDateTime t);                // 0x6db7d0
};

// Every nRepeatInterval minutes for nRepeatLength minutes, inside a daily window that starts at
// byHour:byMinute:bySecond and lasts nDuration seconds.
class CSchedule_Repeat_Sometime : public ISchedule
{
public:
    CSchedule_Repeat_Sometime();                                                // 0x6d70b0
    virtual BOOL GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                  sSchedulePeriod* pNext);                      // 0x6db890
    virtual BOOL AdvanceToNextPeriod(CDateTime& t);                             // 0x6dbb20
    virtual BOOL GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod);          // 0x6dbc30

private:
    void CheckCurrentPeriod(sSchedulePeriod* pCur, sSchedulePeriod* pNext, int nOffset,
                            CDateTime t);                                       // 0x6dbf30
    // A period starting after the daily window of t moves to the next day.
    void FillPeriod(sSchedulePeriod* pPeriod, CDateTime t);                     // 0x6dc0f0
    sSchedulePeriod MakePeriodAt(int nOffset, CDateTime t);                     // 0x6dc260
    CDateTime AlignToSlot(CDateTime t);                                         // 0x6dc380
};

// Once, from stBegin (with the daily start byHour..bySecond for GetPrevPeriod) to stEnd.
class CSchedule_Once_Date : public ISchedule
{
public:
    CSchedule_Once_Date();                                                      // 0x6d7110
    virtual BOOL GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                  sSchedulePeriod* pNext);                      // 0x6dc490
    virtual BOOL AdvanceToNextPeriod(CDateTime& t);                             // 0x6dc610
    virtual BOOL GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod);          // 0x6dc660
};

// Date helpers of the rules (functions of the rule source in the original).

// begin <= t <= end
BOOL IsBetween(CDateTime& t, CDateTime& begin, CDateTime& end);                 // 0x6d7380
// Months from the first day of the start month to t (1 for t in the start month).
int CountMonths(CDateTime& start, CDateTime& t);                                // 0x6d73c0
// Week of the month of t (1..6).
int GetWeekOfMonth(CDateTime& t);                                               // 0x6d74a0
// Weeks from start to t, counting a new week at every Sunday after start (1 in the first).
int CountWeeksBetween(CDateTime& start, CDateTime& t);                          // 0x6d75b0
// The schedule's weekday in week nWeek of the month of t (the next one after the week when the
// week has no such day), at midnight; day 1 of the next month at most.
CDateTime FindNthWeekdayOfMonth(CDateTime& t, ISchedule* pSchedule, int nWeek); // 0x6d7690
// nDayOfWeek in the nWeeks-th week after t (the week of t for 0), the time of day of t.
CDateTime FindNthWeekdayAfter(CDateTime& t, int nWeeks, int nDayOfWeek);        // 0x6d77f0
// nDayOfWeek in the nWeeks-th week before t (the week of t for 0), the time of day of t.
CDateTime FindNthWeekdayBefore(CDateTime& t, int nWeeks, int nDayOfWeek);       // 0x6d7980
// The Sunday of the week of src into *pOut (unchanged for a weekday of 0 or less).
CDateTime* ToStartOfWeek(CDateTime& src, CDateTime* pOut);                      // 0x6d7b70
// Writes begin, end and name into *pPeriod when begin <= t <= end.
BOOL FillPeriodIfBetween(sSchedulePeriod* pPeriod, CDateTime& begin, CDateTime& end,
                         ISchedule* pSchedule, CDateTime t);                    // 0x6dca50
// Begin and end of *pPeriod when it is not NULL.
void SetPeriodRange(sSchedulePeriod* pPeriod, __time32_t tBegin, __time32_t tEnd); // 0x6db7c0
// The period on the schedule's weekday of week byWeek nMonthOffset months after the month of t.
void FillPeriodNthWeekdayOfMonth(ISchedule* pSchedule, sSchedulePeriod* pPeriod,
                                 int nMonthOffset, CDateTime t);                // 0x6dadf0

} // namespace NewSchedule
