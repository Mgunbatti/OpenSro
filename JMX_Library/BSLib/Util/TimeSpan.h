#pragma once

#include <windows.h>
#include <time.h>

// Time span and point in time, shaped like MFC's CTimeSpan / CTime but with 32-bit time values
// (the originals use _mktime32 / _localtime32_s / _time32).

class CTimeSpan
{
public:
    CTimeSpan() {}
    CTimeSpan(LONG lSpan);
    CTimeSpan(LONG lDays, int nHours, int nMins, int nSecs);
    CTimeSpan(const CTimeSpan& span);
    CTimeSpan& operator=(const CTimeSpan& span);

    LONG GetDays() const;
    LONG GetTotalHours() const;
    LONG GetHours() const;
    LONG GetTotalMinutes() const;
    LONG GetMinutes() const;
    LONG GetTotalSeconds() const;
    LONG GetSeconds() const;

    CTimeSpan operator-(CTimeSpan span) const;
    CTimeSpan operator+(CTimeSpan span) const;
    CTimeSpan& operator+=(CTimeSpan span);
    CTimeSpan& operator-=(CTimeSpan span);
    bool operator==(CTimeSpan span) const;
    bool operator!=(CTimeSpan span) const;
    bool operator<(CTimeSpan span) const;
    bool operator>(CTimeSpan span) const;
    bool operator<=(CTimeSpan span) const;
    bool operator>=(CTimeSpan span) const;

public:
    LONG m_lSpan;   // +0 seconds
};

C_ASSERT(sizeof(CTimeSpan) == 4);

// Point in time. Keeps a struct tm next to the time value that the component getters fill with
// the local time. Copying copies only the time value.
class CDateTime
{
public:
    CDateTime() {}
    CDateTime(__time32_t tTime);
    // Asserts nDay 1..31, nMonth 1..12 and nYear >= 1900, and that mktime succeeds.
    CDateTime(int nYear, int nMonth, int nDay, int nHour, int nMin, int nSec, int nDST = -1);
    CDateTime(WORD wDosDate, WORD wDosTime, int nDST = -1);
    CDateTime(const SYSTEMTIME& st, int nDST = -1);
    CDateTime(const FILETIME& ft, int nDST = -1);
    CDateTime(const CDateTime& dt);
    CDateTime& operator=(const CDateTime& dt);
    CDateTime& operator=(__time32_t tTime);

    // windows.h defines GetCurrentTime as a macro, hence the name.
    static CDateTime GetCurrent();

    __time32_t GetTime() const;
    // Fills m_tm with the local time; NULL when the time cannot be converted.
    struct tm* GetLocalTm();
    bool GetAsSystemTime(SYSTEMTIME& st);

    int GetYear();
    int GetMonth();
    int GetDay();
    int GetHour();
    int GetMinute();
    int GetSecond();
    int GetDayOfWeek();

    CTimeSpan operator-(CDateTime dt) const;
    CDateTime operator-(CTimeSpan span) const;
    CDateTime operator+(CTimeSpan span) const;
    CDateTime& operator+=(CTimeSpan span);
    CDateTime& operator-=(CTimeSpan span);
    bool operator==(CDateTime dt) const;
    bool operator!=(CDateTime dt) const;
    bool operator<(CDateTime dt) const;
    bool operator>(CDateTime dt) const;
    bool operator<=(CDateTime dt) const;
    bool operator>=(CDateTime dt) const;

public:
    struct tm   m_tm;       // +0
    __time32_t  m_time;     // +24
};

C_ASSERT(FIELD_OFFSET(CDateTime, m_time) == 0x24);
C_ASSERT(sizeof(CDateTime) == 0x28);
