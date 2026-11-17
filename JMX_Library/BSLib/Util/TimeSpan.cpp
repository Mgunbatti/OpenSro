#include "TimeSpan.h"

#include <string.h>

#include "BSLib/BSDebug.h"

// ---------------------------------------------------------------------------------------------
// CTimeSpan

// 0x4671b0
CTimeSpan::CTimeSpan(LONG lSpan)
{
    m_lSpan = lSpan;
}

// Time_DHMSToSeconds (0x4671c0)
CTimeSpan::CTimeSpan(LONG lDays, int nHours, int nMins, int nSecs)
{
    m_lSpan = nSecs + 60 * (nMins + 60 * (nHours + 24 * lDays));
}

// 0x4671f0
CTimeSpan::CTimeSpan(const CTimeSpan& span)
{
    m_lSpan = span.m_lSpan;
}

// 0x467200
CTimeSpan& CTimeSpan::operator=(const CTimeSpan& span)
{
    m_lSpan = span.m_lSpan;
    return *this;
}

// TimeSpan_GetDays (0x467210)
LONG CTimeSpan::GetDays() const
{
    return m_lSpan / (24 * 3600);
}

// TimeSpan_GetHours (0x467230): whole hours of the span.
LONG CTimeSpan::GetTotalHours() const
{
    return m_lSpan / 3600;
}

// 0x467250
LONG CTimeSpan::GetHours() const
{
    return GetTotalHours() - GetDays() * 24;
}

// TimeSpan_GetMinutes (0x467270): whole minutes of the span.
LONG CTimeSpan::GetTotalMinutes() const
{
    return m_lSpan / 60;
}

// 0x467290
LONG CTimeSpan::GetMinutes() const
{
    return GetTotalMinutes() - GetTotalHours() * 60;
}

// 0x4672b0
LONG CTimeSpan::GetTotalSeconds() const
{
    return m_lSpan;
}

// 0x4672c0
LONG CTimeSpan::GetSeconds() const
{
    return GetTotalSeconds() - GetTotalMinutes() * 60;
}

// 0x4672e0
CTimeSpan CTimeSpan::operator-(CTimeSpan span) const
{
    return CTimeSpan(m_lSpan - span.m_lSpan);
}

// 0x4672f0
CTimeSpan CTimeSpan::operator+(CTimeSpan span) const
{
    return CTimeSpan(m_lSpan + span.m_lSpan);
}

// 0x467300
CTimeSpan& CTimeSpan::operator+=(CTimeSpan span)
{
    m_lSpan += span.m_lSpan;
    return *this;
}

// 0x467310
CTimeSpan& CTimeSpan::operator-=(CTimeSpan span)
{
    m_lSpan -= span.m_lSpan;
    return *this;
}

// 0x467320
bool CTimeSpan::operator==(CTimeSpan span) const
{
    return m_lSpan == span.m_lSpan;
}

// 0x467330
bool CTimeSpan::operator!=(CTimeSpan span) const
{
    return m_lSpan != span.m_lSpan;
}

// 0x467340
bool CTimeSpan::operator<(CTimeSpan span) const
{
    return m_lSpan < span.m_lSpan;
}

// 0x467350
bool CTimeSpan::operator>(CTimeSpan span) const
{
    return m_lSpan > span.m_lSpan;
}

// 0x467360
bool CTimeSpan::operator<=(CTimeSpan span) const
{
    return m_lSpan <= span.m_lSpan;
}

// 0x467370
bool CTimeSpan::operator>=(CTimeSpan span) const
{
    return m_lSpan >= span.m_lSpan;
}

// ---------------------------------------------------------------------------------------------
// CDateTime

// 0x467390
CDateTime::CDateTime(__time32_t tTime)
{
    m_time = tTime;
}

// DateTime_SetFromComponents (0x467560)
CDateTime::CDateTime(int nYear, int nMonth, int nDay, int nHour, int nMin, int nSec, int nDST)
{
    struct tm atm;
    atm.tm_sec = nSec;
    atm.tm_min = nMin;
    atm.tm_hour = nHour;
    if (nDay < 1 || nDay > 31)
        BSAssertFail();
    atm.tm_mday = nDay;
    if (nMonth < 1 || nMonth > 12)
        BSAssertFail();
    atm.tm_mon = nMonth - 1;
    if (nYear < 1900)
        BSAssertFail();
    atm.tm_year = nYear - 1900;
    atm.tm_isdst = nDST;
    m_time = _mktime32(&atm);
    if (m_time == -1)
        BSAssertFail();
}

// DateTime_SetFromDosDateTime (0x4675f0)
CDateTime::CDateTime(WORD wDosDate, WORD wDosTime, int nDST)
{
    struct tm atm;
    atm.tm_sec = (wDosTime & 0x1F) * 2;
    atm.tm_min = (wDosTime >> 5) & 0x3F;
    atm.tm_hour = wDosTime >> 11;
    atm.tm_mday = wDosDate & 0x1F;
    atm.tm_mon = ((wDosDate >> 5) & 0x0F) - 1;
    atm.tm_year = (wDosDate >> 9) + 80;
    atm.tm_isdst = nDST;
    m_time = _mktime32(&atm);
    if (m_time == -1)
        BSAssertFail();
}

// DateTime_FromSystemTime (0x467670): years before 1900 give time 0.
CDateTime::CDateTime(const SYSTEMTIME& st, int nDST)
{
    if (st.wYear < 1900)
    {
        CDateTime dt(0);
        *this = dt;
    }
    else
    {
        CDateTime dt(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, nDST);
        *this = dt;
    }
}

// DateTime_FromFileTime (0x4676e0): the file time is converted to local time first; time 0 when
// a conversion fails.
CDateTime::CDateTime(const FILETIME& ft, int nDST)
{
    FILETIME ftLocal;
    if (!FileTimeToLocalFileTime(&ft, &ftLocal))
    {
        m_time = 0;
        return;
    }
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&ftLocal, &st))
    {
        m_time = 0;
        return;
    }
    CDateTime dt(st, nDST);
    *this = dt;
}

// 0x4673a0
CDateTime::CDateTime(const CDateTime& dt)
{
    m_time = dt.m_time;
}

// 0x4673b0
CDateTime& CDateTime::operator=(const CDateTime& dt)
{
    m_time = dt.m_time;
    return *this;
}

// 0x4673c0
CDateTime& CDateTime::operator=(__time32_t tTime)
{
    m_time = tTime;
    return *this;
}

// 0x467540
CDateTime CDateTime::GetCurrent()
{
    return CDateTime(_time32(NULL));
}

// 0x4673d0
__time32_t CDateTime::GetTime() const
{
    return m_time;
}

// 0x467500
struct tm* CDateTime::GetLocalTm()
{
    memset(&m_tm, 0, sizeof(m_tm));
    if (_localtime32_s(&m_tm, &m_time) != 0)
        return NULL;
    return &m_tm;
}

// 0x467740
bool CDateTime::GetAsSystemTime(SYSTEMTIME& st)
{
    struct tm* ptm = GetLocalTm();
    if (!ptm)
        return false;
    st.wYear = (WORD)(ptm->tm_year + 1900);
    st.wMonth = (WORD)(ptm->tm_mon + 1);
    st.wDayOfWeek = (WORD)ptm->tm_wday;
    st.wDay = (WORD)ptm->tm_mday;
    st.wHour = (WORD)ptm->tm_hour;
    st.wMinute = (WORD)ptm->tm_min;
    st.wSecond = (WORD)ptm->tm_sec;
    st.wMilliseconds = 0;
    return true;
}

// The getters below use the result of GetLocalTm without checking it.
// BUG(FWUTIL-006): a time localtime cannot convert (before 1970) dereferences NULL.

// 0x4673e0
int CDateTime::GetYear()
{
    return GetLocalTm()->tm_year + 1900;
}

// 0x4673f0
int CDateTime::GetMonth()
{
    return GetLocalTm()->tm_mon + 1;
}

// 0x467400
int CDateTime::GetDay()
{
    return GetLocalTm()->tm_mday;
}

// 0x467410
int CDateTime::GetHour()
{
    return GetLocalTm()->tm_hour;
}

// 0x467420
int CDateTime::GetMinute()
{
    return GetLocalTm()->tm_min;
}

// 0x467430
int CDateTime::GetSecond()
{
    return GetLocalTm()->tm_sec;
}

// 0x467440: 1 = Sunday.
int CDateTime::GetDayOfWeek()
{
    return GetLocalTm()->tm_wday + 1;
}

// 0x467450
CTimeSpan CDateTime::operator-(CDateTime dt) const
{
    return CTimeSpan(m_time - dt.m_time);
}

// 0x467460
CDateTime CDateTime::operator-(CTimeSpan span) const
{
    return CDateTime(m_time - span.m_lSpan);
}

// 0x467470
CDateTime CDateTime::operator+(CTimeSpan span) const
{
    return CDateTime(m_time + span.m_lSpan);
}

// 0x467480
CDateTime& CDateTime::operator+=(CTimeSpan span)
{
    m_time += span.m_lSpan;
    return *this;
}

// 0x467490
CDateTime& CDateTime::operator-=(CTimeSpan span)
{
    m_time -= span.m_lSpan;
    return *this;
}

// 0x4674a0
bool CDateTime::operator==(CDateTime dt) const
{
    return m_time == dt.m_time;
}

// 0x4674b0
bool CDateTime::operator!=(CDateTime dt) const
{
    return m_time != dt.m_time;
}

// 0x4674c0
bool CDateTime::operator<(CDateTime dt) const
{
    return m_time < dt.m_time;
}

// 0x4674d0
bool CDateTime::operator>(CDateTime dt) const
{
    return m_time > dt.m_time;
}

// 0x4674e0
bool CDateTime::operator<=(CDateTime dt) const
{
    return m_time <= dt.m_time;
}

// 0x4674f0
bool CDateTime::operator>=(CDateTime dt) const
{
    return m_time >= dt.m_time;
}
