#include "BSLib/NewSchedule/Schedule.h"

namespace NewSchedule
{

static const int SECONDS_PER_DAY = 86400;
static const int SECONDS_PER_WEEK = 604800;

// The rules build all dates with the component constructor of CDateTime and let it normalise
// overflowing days, months and minutes (JMX_FRAMEWORK_2011_08 build of TimeSpan.cpp).

static CDateTime NextDay(CDateTime& d)
{
    int nSec = d.GetSecond();
    int nMin = d.GetMinute();
    int nHour = d.GetHour();
    int nDay = d.GetDay() + 1;
    int nMonth = d.GetMonth();
    int nYear = d.GetYear();
    return CDateTime(nYear, nMonth, nDay, nHour, nMin, nSec);
}

static CDateTime PrevDay(CDateTime& d)
{
    int nSec = d.GetSecond();
    int nMin = d.GetMinute();
    int nHour = d.GetHour();
    int nDay = d.GetDay() - 1;
    int nMonth = d.GetMonth();
    int nYear = d.GetYear();
    return CDateTime(nYear, nMonth, nDay, nHour, nMin, nSec);
}

// ---------------------------------------------------------------------------------------------
// Helpers

BOOL IsBetween(CDateTime& t, CDateTime& begin, CDateTime& end)
{
    if (t.GetTime() < begin.GetTime())
        return FALSE;
    if (t.GetTime() > end.GetTime())
        return FALSE;
    return TRUE;
}

int CountMonths(CDateTime& start, CDateTime& t)
{
    int nMonth = start.GetMonth();
    int nYear = start.GetYear();
    CDateTime cur(nYear, nMonth, 1, 0, 0, 0);
    int nCount = 0;
    while (cur.GetTime() <= t.GetTime())
    {
        struct tm atm;
        cur.GetLocalTm(&atm);
        int nSec = cur.GetSecond();
        int nMin = cur.GetMinute();
        int nHour = cur.GetHour();
        int nDay = cur.GetDay();
        int nNextMonth = cur.GetMonth() + 1;
        int nCurYear = cur.GetYear();
        cur = CDateTime(nCurYear, nNextMonth, nDay, nHour, nMin, nSec);
        ++nCount;
    }
    return nCount;
}

int GetWeekOfMonth(CDateTime& t)
{
    int nSec = t.GetSecond();
    int nMin = t.GetMinute();
    int nHour = t.GetHour();
    int nMonth = t.GetMonth();
    int nYear = t.GetYear();
    CDateTime d(nYear, nMonth, 1, nHour, nMin, nSec);
    int nWeek = 1;
    while (d.GetTime() <= t.GetTime())
    {
        if (d.GetDayOfWeek() == 1 && d.GetDay() != 1)
            ++nWeek;
        d = NextDay(d);
    }
    return nWeek;
}

int CountWeeksBetween(CDateTime& start, CDateTime& t)
{
    CDateTime d(start.GetTime());
    int nWeeks = 1;
    while (d.GetTime() <= t.GetTime())
    {
        if (d.GetDayOfWeek() == 1 && d.GetTime() != start.GetTime())
            ++nWeeks;
        d = NextDay(d);
    }
    return nWeeks;
}

CDateTime FindNthWeekdayOfMonth(CDateTime& t, ISchedule* pSchedule, int nWeek)
{
    CDateTime d(t.GetYear(), t.GetMonth(), 1, 0, 0, 0);
    int nNextMonth = t.GetMonth() + 1;
    CDateTime next(t.GetYear(), nNextMonth, 1, 0, 0, 0);
    int nCur = 1;
    __time32_t tLimit = next.GetTime();
    while (d.GetTime() <= tLimit)
    {
        if (d.GetDayOfWeek() == 1 && d.GetDay() != 1)
            ++nCur;
        if (nCur == nWeek)
            break;
        d = NextDay(d);
    }
    while (d.GetTime() <= tLimit)
    {
        if (d.GetDayOfWeek() == pSchedule->m_Data.byDayOfWeek)
            return d;
        d = NextDay(d);
    }
    return d;
}

CDateTime FindNthWeekdayAfter(CDateTime& t, int nWeeks, int nDayOfWeek)
{
    CDateTime d(t.GetTime());
    int nSec = d.GetSecond();
    int nMin = d.GetMinute();
    int nHour = d.GetHour();
    int nDay = d.GetDay() + 7 * (nWeeks + 1);
    int nMonth = d.GetMonth();
    int nYear = d.GetYear();
    CDateTime limit(nYear, nMonth, nDay, nHour, nMin, nSec);
    int nCount = 0;
    __time32_t tLimit = limit.GetTime();
    while (d.GetTime() <= tLimit)
    {
        if (d.GetDayOfWeek() == 1 && d.GetTime() != t.GetTime())
            ++nCount;
        if (nCount == nWeeks)
            break;
        d = NextDay(d);
    }
    while (d.GetTime() <= tLimit)
    {
        if (d.GetDayOfWeek() == nDayOfWeek)
            return d;
        d = NextDay(d);
    }
    return d;
}

CDateTime FindNthWeekdayBefore(CDateTime& t, int nWeeks, int nDayOfWeek)
{
    CDateTime d(t.GetTime());
    int nSec = d.GetSecond();
    int nMin = d.GetMinute();
    int nHour = d.GetHour();
    int nDay = d.GetDay() - 7 * (nWeeks + 1);
    int nMonth = d.GetMonth();
    int nYear = d.GetYear();
    CDateTime limit(nYear, nMonth, nDay, nHour, nMin, nSec);
    int nCount = 0;
    __time32_t tLow = limit.GetTime();
    while (d.GetTime() >= tLow)
    {
        if (nCount == nWeeks && d.GetDayOfWeek() == 1)
            break;
        if (d.GetDayOfWeek() == 1 && d.GetTime() != t.GetTime())
            ++nCount;
        d = PrevDay(d);
    }
    nSec = d.GetSecond();
    nMin = d.GetMinute();
    nHour = d.GetHour();
    nDay = d.GetDay() + 7;
    nMonth = d.GetMonth();
    nYear = d.GetYear();
    limit = CDateTime(nYear, nMonth, nDay, nHour, nMin, nSec);
    __time32_t tHigh = limit.GetTime();
    while (d.GetTime() <= tHigh)
    {
        if (d.GetDayOfWeek() == nDayOfWeek)
            return d;
        d = NextDay(d);
    }
    return d;
}

CDateTime* ToStartOfWeek(CDateTime& src, CDateTime* pOut)
{
    int nDayOfWeek = src.GetDayOfWeek();
    if (nDayOfWeek <= 0)
        return pOut;
    // Every pass rebuilds *pOut from src; the last one is the Sunday.
    for (int i = 0; i < nDayOfWeek; ++i)
    {
        int nSec = src.GetSecond();
        int nMin = src.GetMinute();
        int nHour = src.GetHour();
        int nDay = src.GetDay() - i;
        int nMonth = src.GetMonth();
        int nYear = src.GetYear();
        *pOut = CDateTime(nYear, nMonth, nDay, nHour, nMin, nSec);
    }
    return pOut;
}

BOOL FillPeriodIfBetween(sSchedulePeriod* pPeriod, CDateTime& begin, CDateTime& end,
                         ISchedule* pSchedule, CDateTime t)
{
    BOOL bRet = IsBetween(t, begin, end);
    if (bRet == TRUE)
    {
        pPeriod->tBegin = begin.GetTime();
        pPeriod->tEnd = end.GetTime();
        pPeriod->strName.assign(pSchedule->m_Data.szName, strlen(pSchedule->m_Data.szName));
    }
    return bRet;
}

void SetPeriodRange(sSchedulePeriod* pPeriod, __time32_t tBegin, __time32_t tEnd)
{
    if (pPeriod)
    {
        pPeriod->tBegin = tBegin;
        pPeriod->tEnd = tEnd;
    }
}

void FillPeriodNthWeekdayOfMonth(ISchedule* pSchedule, sSchedulePeriod* pPeriod,
                                 int nMonthOffset, CDateTime t)
{
    if (!pPeriod)
        return;
    const SCHEDULE_DATA& data = pSchedule->m_Data;
    int nMonth = t.GetMonth() + nMonthOffset;
    CDateTime d(t.GetYear(), nMonth, 1, 0, 0, 0);
    d = FindNthWeekdayOfMonth(d, pSchedule, data.byWeek);
    d = CDateTime(d.GetYear(), d.GetMonth(), d.GetDay(), data.byHour, data.byMinute,
                  data.bySecond);
    __time32_t tBegin = d.GetTime();
    pPeriod->tBegin = tBegin;
    pPeriod->tEnd = tBegin + data.nDuration;
}

// ---------------------------------------------------------------------------------------------
// ISchedule

ISchedule::ISchedule()
{
    m_nType = 0;
    ZeroScheduleStruct(m_Period);
}

ISchedule* ISchedule::CreateByType(const SCHEDULE_DATA* pData)
{
    if (!pData)
        return NULL;
    switch (pData->nType)
    {
    case SCHEDULE_TYPE_EVERYDAYS:
        return new CSchedule_Everydays;
    case SCHEDULE_TYPE_SOMEDAYS:
        return new CSchedule_Somedays;
    case SCHEDULE_TYPE_EVERYWEEKS_DAYOFWEEK:
        return new CSchedule_Everyweeks_Dayofweek;
    case SCHEDULE_TYPE_SOMEWEEKS_DAYOFWEEK:
        return new CSchedule_Someweeks_Dayofweek;
    case SCHEDULE_TYPE_EVERYMONTHS_DAYOFWEEK:
        return new CSchedule_Everymonths_Dayofweek;
    case SCHEDULE_TYPE_EVERYMONTHS_DATE:
        return new CSchedule_Everymonths_Date;
    case SCHEDULE_TYPE_SOMEMONTHS_DAYOFWEEK:
        return new CSchedule_Somemonths_Dayofweek;
    case SCHEDULE_TYPE_SOMEMONTHS_DATE:
        return new CSchedule_Somemonths_Date;
    case SCHEDULE_TYPE_REPEAT_SOMETIME:
        return new CSchedule_Repeat_Sometime;
    case SCHEDULE_TYPE_ONCE_DATE:
        return new CSchedule_Once_Date;
    }
    return NULL;
}

CDateTime ISchedule::ClampToEndTime(const CDateTime& t)
{
    CDateTime end(m_Data.stEnd.wYear, m_Data.stEnd.wMonth, m_Data.stEnd.wDay,
                  m_Data.stEnd.wHour, m_Data.stEnd.wMinute, m_Data.stEnd.wSecond);
    if (end > t)
        return t;
    return end;
}

CDateTime ISchedule::GetStartTime()
{
    return CDateTime(m_Data.stBegin.wYear, m_Data.stBegin.wMonth, m_Data.stBegin.wDay,
                     m_Data.stBegin.wHour, m_Data.stBegin.wMinute, m_Data.stBegin.wSecond);
}

BOOL ISchedule::IsAfterStart(CDateTime& t)
{
    CDateTime start = GetStartTime();
    return t.GetTime() > start.GetTime();
}

BOOL ISchedule::IsAfterEnd(CDateTime& t)
{
    CDateTime end(m_Data.stEnd.wYear, m_Data.stEnd.wMonth, m_Data.stEnd.wDay,
                  m_Data.stEnd.wHour, m_Data.stEnd.wMinute, m_Data.stEnd.wSecond);
    return t.GetTime() > end.GetTime();
}

BOOL ISchedule::IsPeriodExpired()
{
    CDateTime begin(m_Period.tBegin);
    CDateTime end(m_Period.tEnd);
    if (IsAfterEnd(begin) == TRUE)
        return TRUE;
    if (IsAfterEnd(end) == TRUE)
        return TRUE;
    return FALSE;
}

void ISchedule::SetPeriodIdAndName(sSchedulePeriod* pPeriod)
{
    pPeriod->dwScheduleID = m_Data.dwScheduleID;
    SetPeriodName(pPeriod);
}

void ISchedule::SetPeriodName(sSchedulePeriod* pPeriod)
{
    pPeriod->strName.assign(m_Data.szName, strlen(m_Data.szName));
}

// ---------------------------------------------------------------------------------------------
// CSchedule_Everydays

CSchedule_Everydays::CSchedule_Everydays()
{
    m_nType = SCHEDULE_TYPE_EVERYDAYS;
}

BOOL CSchedule_Everydays::GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                           sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    if (pNext)
        SetPeriodIdAndName(pNext);
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    __time32_t tBegin = begin.GetTime();
    CDateTime end(tBegin + m_Data.nDuration);
    BOOL bRet = FillPeriodIfBetween(pCur, begin, end, this, t);
    if (t >= begin)
    {
        if (t < end)
        {
            pCur->tBegin = tBegin;
            pCur->tEnd = end.GetTime();
        }
        SetPeriodRange(pNext, tBegin + SECONDS_PER_DAY, end.GetTime() + SECONDS_PER_DAY);
    }
    else
    {
        SetPeriodRange(pNext, tBegin, end.GetTime());
    }
    return bRet;
}

BOOL CSchedule_Everydays::AdvanceToNextPeriod(CDateTime& /*t*/)
{
    CDateTime d(m_Period.tBegin);
    int nDay = d.GetDay() + 1;
    d = CDateTime(d.GetYear(), d.GetMonth(), nDay, m_Data.byHour, m_Data.byMinute,
                  m_Data.bySecond);
    m_Period.tBegin = d.GetTime();
    m_Period.tEnd = m_Period.tBegin + m_Data.nDuration;
    return TRUE;
}

BOOL CSchedule_Everydays::GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod)
{
    if (!pPeriod)
        return FALSE;
    t = ClampToEndTime(t);
    SetPeriodIdAndName(pPeriod);
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    __time32_t tBegin = begin.GetTime();
    CDateTime end(tBegin + m_Data.nDuration);
    if (t > begin && !(t < end))
    {
        pPeriod->tBegin = tBegin;
        pPeriod->tEnd = end.GetTime();
        return TRUE;
    }
    pPeriod->tBegin = tBegin - SECONDS_PER_DAY;
    pPeriod->tEnd = end.GetTime() - SECONDS_PER_DAY;
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// CSchedule_Somedays

CSchedule_Somedays::CSchedule_Somedays()
{
    m_nType = SCHEDULE_TYPE_SOMEDAYS;
}

BOOL CSchedule_Somedays::GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                          sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    if (pNext)
        SetPeriodIdAndName(pNext);
    CDateTime start = GetStartTime();
    int nDays = (t - start).GetDays() + 1;
    int nPhase = nDays % m_Data.nInterval;
    if (nPhase == 0)
        nPhase = m_Data.nInterval;
    BOOL bRet = CheckCurrentPeriod(start, t, nDays, pCur);
    int nDay = m_Data.byDay;
    if (nPhase == nDay)
    {
        GetPeriodOnMatchingDay(t, pCur, pNext);
        return bRet;
    }
    if (nPhase < nDay)
        FillPeriodAtDayOffset(pNext, t, SECONDS_PER_DAY * (nDay - nPhase));
    else
        FillPeriodAtDayOffset(pNext, t, SECONDS_PER_DAY * (m_Data.nInterval - nPhase + nDay));
    return bRet;
}

BOOL CSchedule_Somedays::GetPeriodOnMatchingDay(CDateTime& t, sSchedulePeriod* pCur,
                                                sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    __time32_t tBegin = begin.GetTime();
    CDateTime end(tBegin + m_Data.nDuration);
    int nStep = SECONDS_PER_DAY * m_Data.nInterval;
    if (t >= begin)
    {
        if (t < end)
        {
            pCur->tBegin = tBegin;
            pCur->tEnd = end.GetTime();
            SetPeriodRange(pNext, tBegin + nStep, end.GetTime() + nStep);
            return TRUE;
        }
        SetPeriodRange(pNext, tBegin + nStep, end.GetTime() + nStep);
        return FALSE;
    }
    SetPeriodRange(pNext, tBegin, end.GetTime());
    return FALSE;
}

BOOL CSchedule_Somedays::AdvanceToNextPeriod(CDateTime& /*t*/)
{
    m_Period.tBegin += SECONDS_PER_DAY * m_Data.nInterval;
    m_Period.tEnd = m_Period.tBegin + m_Data.nDuration;
    return TRUE;
}

BOOL CSchedule_Somedays::GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod)
{
    if (!pPeriod)
        return FALSE;
    t = ClampToEndTime(t);
    t = ClampToEndTime(t);
    SetPeriodIdAndName(pPeriod);
    CDateTime start = GetStartTime();
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    __time32_t tBegin = begin.GetTime();
    CDateTime end(tBegin + m_Data.nDuration);
    int nInterval = m_Data.nInterval;
    int nPhase = ((t - start).GetDays() + 1) % nInterval;
    if (nPhase == 0)
        nPhase = nInterval;
    int nDay = m_Data.byDay;
    if (nPhase == nDay)
    {
        int nStep = SECONDS_PER_DAY * nInterval;
        if (t > begin && !(t < end))
        {
            pPeriod->tBegin = tBegin;
            pPeriod->tEnd = end.GetTime();
        }
        else
        {
            pPeriod->tBegin = tBegin - nStep;
            pPeriod->tEnd = end.GetTime() - nStep;
        }
    }
    else if (nPhase < nDay)
    {
        *pPeriod = MakePeriodAt(SECONDS_PER_DAY * (nDay - nInterval - nPhase), t);
    }
    else
    {
        *pPeriod = MakePeriodAt(SECONDS_PER_DAY * (nDay - nPhase), t);
    }
    return TRUE;
}

BOOL CSchedule_Somedays::CheckCurrentPeriod(CDateTime& start, CDateTime& t, int nDays,
                                            sSchedulePeriod* pCur)
{
    int nInterval = m_Data.nInterval;
    CDateTime d(start.GetTime() + SECONDS_PER_DAY * (nInterval * (nDays / nInterval)));
    int nDay = d.GetDay() + m_Data.byDay - 1;
    d = CDateTime(d.GetYear(), d.GetMonth(), nDay, m_Data.byHour, m_Data.byMinute,
                  m_Data.bySecond);
    CDateTime begin(d);
    int nSec = begin.GetSecond() + m_Data.nDuration;
    CDateTime end(begin.GetYear(), begin.GetMonth(), begin.GetDay(), begin.GetHour(),
                  begin.GetMinute(), nSec);
    BOOL bRet = IsBetween(t, begin, end);
    if (bRet == TRUE)
    {
        pCur->tBegin = begin.GetTime();
        pCur->tEnd = end.GetTime();
        SetPeriodName(pCur);
    }
    return bRet;
}

void CSchedule_Somedays::FillPeriodAtDayOffset(sSchedulePeriod* pPeriod, CDateTime& t,
                                               int nOffset)
{
    if (!pPeriod)
        return;
    // The offset (in seconds) goes into the second field.
    CDateTime d(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                m_Data.bySecond + nOffset);
    __time32_t tBegin = d.GetTime();
    pPeriod->tBegin = tBegin;
    pPeriod->tEnd = tBegin + m_Data.nDuration;
}

sSchedulePeriod CSchedule_Somedays::MakePeriodAt(int nOffset, CDateTime t)
{
    sSchedulePeriod period;
    period.dwScheduleID = m_Data.dwScheduleID;
    CDateTime d(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                m_Data.bySecond + nOffset);
    __time32_t tBegin = d.GetTime();
    period.tBegin = tBegin;
    period.tEnd = tBegin + m_Data.nDuration;
    return period;
}

// ---------------------------------------------------------------------------------------------
// CSchedule_Everyweeks_Dayofweek

CSchedule_Everyweeks_Dayofweek::CSchedule_Everyweeks_Dayofweek()
{
    m_nType = SCHEDULE_TYPE_EVERYWEEKS_DAYOFWEEK;
}

BOOL CSchedule_Everyweeks_Dayofweek::GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                                      sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    if (pNext)
        SetPeriodIdAndName(pNext);
    BOOL bRet = CheckCurrentPeriod(t, pCur);
    int nDayOfWeek = m_Data.byDayOfWeek;
    if (t.GetDayOfWeek() == nDayOfWeek)
    {
        GetPeriodOnMatchingDay(t, pCur, pNext);
        return bRet;
    }
    int nOffset;
    if (t.GetDayOfWeek() < nDayOfWeek)
        nOffset = SECONDS_PER_DAY * (nDayOfWeek - t.GetDayOfWeek());
    else
        nOffset = SECONDS_PER_DAY * (nDayOfWeek - t.GetDayOfWeek() + 7);
    FillPeriodAtDayOffset(pNext, nOffset, t);
    return bRet;
}

BOOL CSchedule_Everyweeks_Dayofweek::GetPeriodOnMatchingDay(CDateTime& t, sSchedulePeriod* pCur,
                                                            sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    __time32_t tBegin = begin.GetTime();
    CDateTime end(tBegin + m_Data.nDuration);
    if (t >= begin)
    {
        if (t < end)
        {
            pCur->tBegin = tBegin;
            pCur->tEnd = end.GetTime();
            SetPeriodRange(pNext, tBegin + SECONDS_PER_WEEK, end.GetTime() + SECONDS_PER_WEEK);
            return TRUE;
        }
        SetPeriodRange(pNext, tBegin + SECONDS_PER_WEEK, end.GetTime() + SECONDS_PER_WEEK);
        return FALSE;
    }
    SetPeriodRange(pNext, tBegin, end.GetTime());
    return FALSE;
}

BOOL CSchedule_Everyweeks_Dayofweek::AdvanceToNextPeriod(CDateTime& /*t*/)
{
    m_Period.tBegin += SECONDS_PER_WEEK;
    m_Period.tEnd = m_Period.tBegin + m_Data.nDuration;
    return TRUE;
}

BOOL CSchedule_Everyweeks_Dayofweek::GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod)
{
    if (!pPeriod)
        return FALSE;
    t = ClampToEndTime(t);
    SetPeriodIdAndName(pPeriod);
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    __time32_t tBegin = begin.GetTime();
    CDateTime end(tBegin + m_Data.nDuration);
    int nDayOfWeek = m_Data.byDayOfWeek;
    if (t.GetDayOfWeek() == nDayOfWeek)
    {
        if (t > begin && !(t < end))
        {
            pPeriod->tBegin = tBegin;
            pPeriod->tEnd = end.GetTime();
        }
        else
        {
            pPeriod->tBegin = tBegin - SECONDS_PER_WEEK;
            pPeriod->tEnd = end.GetTime() - SECONDS_PER_WEEK;
        }
    }
    else if (t.GetDayOfWeek() < nDayOfWeek)
    {
        *pPeriod = MakePeriodAt(SECONDS_PER_DAY * (nDayOfWeek - t.GetDayOfWeek() - 7), t);
    }
    else
    {
        *pPeriod = MakePeriodAt(SECONDS_PER_DAY * (nDayOfWeek - t.GetDayOfWeek()), t);
    }
    return TRUE;
}

BOOL CSchedule_Everyweeks_Dayofweek::CheckCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur)
{
    CDateTime d;
    ToStartOfWeek(t, &d);
    while (d.GetDayOfWeek() != m_Data.byDayOfWeek)
        d = NextDay(d);
    CDateTime begin(d.GetYear(), d.GetMonth(), d.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    int nSec = begin.GetSecond() + m_Data.nDuration;
    CDateTime end(begin.GetYear(), begin.GetMonth(), begin.GetDay(), begin.GetHour(),
                  begin.GetMinute(), nSec);
    BOOL bRet = IsBetween(t, begin, end);
    if (bRet == TRUE)
    {
        pCur->tBegin = begin.GetTime();
        pCur->tEnd = end.GetTime();
        SetPeriodName(pCur);
    }
    return bRet;
}

void CSchedule_Everyweeks_Dayofweek::FillPeriodAtDayOffset(sSchedulePeriod* pPeriod, int nOffset,
                                                           CDateTime t)
{
    if (!pPeriod)
        return;
    CDateTime d(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                m_Data.bySecond + nOffset);
    __time32_t tBegin = d.GetTime();
    pPeriod->tBegin = tBegin;
    pPeriod->tEnd = tBegin + m_Data.nDuration;
}

sSchedulePeriod CSchedule_Everyweeks_Dayofweek::MakePeriodAt(int nOffset, CDateTime t)
{
    sSchedulePeriod period;
    period.dwScheduleID = m_Data.dwScheduleID;
    CDateTime d(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                m_Data.bySecond + nOffset);
    __time32_t tBegin = d.GetTime();
    period.tBegin = tBegin;
    period.tEnd = tBegin + m_Data.nDuration;
    return period;
}

// ---------------------------------------------------------------------------------------------
// CSchedule_Someweeks_Dayofweek

CSchedule_Someweeks_Dayofweek::CSchedule_Someweeks_Dayofweek()
{
    m_nType = SCHEDULE_TYPE_SOMEWEEKS_DAYOFWEEK;
}

BOOL CSchedule_Someweeks_Dayofweek::GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                                     sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    if (pNext)
        SetPeriodIdAndName(pNext);
    CDateTime start = GetStartTime();
    int nWeeks = CountWeeksBetween(start, t);
    int nPhase = nWeeks % m_Data.nInterval;
    if (nPhase == 0)
        nPhase = m_Data.nInterval;
    BOOL bRet = CheckCurrentPeriod(start, t, nWeeks, pCur);
    int nWeek = m_Data.byWeek;
    int nDayOfWeek = m_Data.byDayOfWeek;
    int nAhead;
    if (nPhase == nWeek)
    {
        if (t.GetDayOfWeek() == nDayOfWeek)
        {
            GetPeriodOnMatchingDay(t, pCur, pNext);
            return bRet;
        }
        if (t.GetDayOfWeek() < nDayOfWeek)
        {
            start = FindNthWeekdayAfter(t, 0, nDayOfWeek);
            FillPeriodAtDayOffset(pNext, start);
            return bRet;
        }
        nAhead = m_Data.nInterval;
    }
    else if (nPhase < nWeek)
    {
        nAhead = nWeek - nPhase;
    }
    else
    {
        nAhead = nWeek - nPhase + m_Data.nInterval;
    }
    start = FindNthWeekdayAfter(t, nAhead, nDayOfWeek);
    FillPeriodAtDayOffset(pNext, start);
    return bRet;
}

BOOL CSchedule_Someweeks_Dayofweek::GetPeriodOnMatchingDay(CDateTime& t, sSchedulePeriod* pCur,
                                                           sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    __time32_t tBegin = begin.GetTime();
    CDateTime end(tBegin + m_Data.nDuration);
    if (t >= begin)
    {
        if (t < end)
        {
            pCur->tBegin = tBegin;
            pCur->tEnd = end.GetTime();
            end = FindNthWeekdayAfter(t, m_Data.nInterval, m_Data.byDayOfWeek);
            FillPeriodAtDayOffset(pNext, end);
            return TRUE;
        }
        end = FindNthWeekdayAfter(t, m_Data.nInterval, m_Data.byDayOfWeek);
        FillPeriodAtDayOffset(pNext, end);
        return FALSE;
    }
    SetPeriodRange(pNext, tBegin, end.GetTime());
    return FALSE;
}

BOOL CSchedule_Someweeks_Dayofweek::AdvanceToNextPeriod(CDateTime& /*t*/)
{
    m_Period.tBegin += SECONDS_PER_WEEK * m_Data.nInterval;
    m_Period.tEnd = m_Period.tBegin + m_Data.nDuration;
    return TRUE;
}

BOOL CSchedule_Someweeks_Dayofweek::GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod)
{
    if (!pPeriod)
        return FALSE;
    t = ClampToEndTime(t);
    SetPeriodIdAndName(pPeriod);
    CDateTime start = GetStartTime();
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    CDateTime end(begin.GetTime() + m_Data.nDuration);
    int nInterval = m_Data.nInterval;
    int nPhase = CountWeeksBetween(start, t) % nInterval;
    if (nPhase == 0)
        nPhase = nInterval;
    int nWeek = m_Data.byWeek;
    int nDayOfWeek = m_Data.byDayOfWeek;
    int nBack;
    if (nPhase == nWeek)
    {
        if (t.GetDayOfWeek() == nDayOfWeek)
        {
            if (t > begin && !(t < end))
                nBack = 0;
            else
                nBack = nInterval;
        }
        else if (t.GetDayOfWeek() < nDayOfWeek)
        {
            nBack = nInterval;
        }
        else
        {
            nBack = 0;
        }
    }
    else if (nPhase < nWeek)
    {
        nBack = nInterval - nWeek + nPhase;
    }
    else
    {
        nBack = nPhase - nWeek;
    }
    begin = FindNthWeekdayBefore(t, nBack, nDayOfWeek);
    *pPeriod = MakePeriodAt(begin);
    return TRUE;
}

BOOL CSchedule_Someweeks_Dayofweek::CheckCurrentPeriod(CDateTime& start, CDateTime& t,
                                                       int nWeeks, sSchedulePeriod* pCur)
{
    int nInterval = m_Data.nInterval;
    CDateTime d = FindNthWeekdayAfter(start, nInterval * (nWeeks / nInterval) + m_Data.byWeek - 1,
                                      m_Data.byDayOfWeek);
    d = CDateTime(d.GetYear(), d.GetMonth(), d.GetDay(), m_Data.byHour, m_Data.byMinute,
                  m_Data.bySecond);
    int nSec = d.GetSecond() + m_Data.nDuration;
    CDateTime end(d.GetYear(), d.GetMonth(), d.GetDay(), d.GetHour(), d.GetMinute(), nSec);
    BOOL bRet = IsBetween(t, d, end);
    if (bRet == TRUE)
    {
        pCur->tBegin = d.GetTime();
        pCur->tEnd = end.GetTime();
        SetPeriodName(pCur);
    }
    return bRet;
}

void CSchedule_Someweeks_Dayofweek::FillPeriodAtDayOffset(sSchedulePeriod* pPeriod, CDateTime t)
{
    if (!pPeriod)
        return;
    t = CDateTime(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                  m_Data.bySecond);
    __time32_t tBegin = t.GetTime();
    pPeriod->tBegin = tBegin;
    pPeriod->tEnd = tBegin + m_Data.nDuration;
}

sSchedulePeriod CSchedule_Someweeks_Dayofweek::MakePeriodAt(CDateTime t)
{
    sSchedulePeriod period;
    period.dwScheduleID = m_Data.dwScheduleID;
    CDateTime d(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                m_Data.bySecond);
    __time32_t tBegin = d.GetTime();
    period.tBegin = tBegin;
    period.tEnd = tBegin + m_Data.nDuration;
    return period;
}

// ---------------------------------------------------------------------------------------------
// CSchedule_Everymonths_Dayofweek

CSchedule_Everymonths_Dayofweek::CSchedule_Everymonths_Dayofweek()
{
    m_nType = SCHEDULE_TYPE_EVERYMONTHS_DAYOFWEEK;
}

BOOL CSchedule_Everymonths_Dayofweek::GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                                       sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    if (pNext)
        SetPeriodIdAndName(pNext);
    int nWeekOfMonth = GetWeekOfMonth(t);
    BOOL bRet = CheckCurrentPeriod(t, pCur);
    int nWeek = m_Data.byWeek;
    if (nWeekOfMonth == nWeek)
    {
        if (t.GetDayOfWeek() == m_Data.byDayOfWeek)
        {
            GetPeriodOnMatchingDay(t, pCur, pNext);
            return bRet;
        }
        if (t.GetDayOfWeek() < m_Data.byDayOfWeek)
            FillPeriodNthWeekdayOfMonth(this, pNext, 0, t);
        else
            FillPeriodNthWeekdayOfMonth(this, pNext, 1, t);
        return bRet;
    }
    if (nWeekOfMonth < nWeek)
        FillPeriodNthWeekdayOfMonth(this, pNext, 0, t);
    else
        FillPeriodNthWeekdayOfMonth(this, pNext, 1, t);
    return bRet;
}

BOOL CSchedule_Everymonths_Dayofweek::GetPeriodOnMatchingDay(CDateTime& t, sSchedulePeriod* pCur,
                                                             sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    __time32_t tBegin = begin.GetTime();
    CDateTime end(tBegin + m_Data.nDuration);
    if (t >= begin)
    {
        if (t < end)
        {
            pCur->tBegin = tBegin;
            pCur->tEnd = end.GetTime();
            FillPeriodNthWeekdayOfMonth(this, pNext, 1, t);
            return TRUE;
        }
        FillPeriodNthWeekdayOfMonth(this, pNext, 1, t);
        return FALSE;
    }
    SetPeriodRange(pNext, tBegin, end.GetTime());
    return FALSE;
}

BOOL CSchedule_Everymonths_Dayofweek::AdvanceToNextPeriod(CDateTime& /*t*/)
{
    CDateTime d(m_Period.tBegin);
    int nMonth = d.GetMonth() + 1;
    d = CDateTime(d.GetYear(), nMonth, 1, 0, 0, 0);
    d = FindNthWeekdayOfMonth(d, this, m_Data.byWeek);
    d = CDateTime(d.GetYear(), d.GetMonth(), d.GetDay(), m_Data.byHour, m_Data.byMinute,
                  m_Data.bySecond);
    m_Period.tBegin = d.GetTime();
    m_Period.tEnd = m_Period.tBegin + m_Data.nDuration;
    return TRUE;
}

BOOL CSchedule_Everymonths_Dayofweek::GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod)
{
    if (!pPeriod)
        return FALSE;
    t = ClampToEndTime(t);
    SetPeriodIdAndName(pPeriod);
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    CDateTime end(begin.GetTime() + m_Data.nDuration);
    int nOffset;
    int nWeekOfMonth = GetWeekOfMonth(t);
    if (nWeekOfMonth == m_Data.byWeek)
    {
        if (t.GetDayOfWeek() == m_Data.byDayOfWeek)
        {
            if (t > begin && !(t < end))
                nOffset = 0;
            else
                nOffset = -1;
        }
        else if (t.GetDayOfWeek() < m_Data.byDayOfWeek)
        {
            nOffset = -1;
        }
        else
        {
            nOffset = 0;
        }
    }
    else if (nWeekOfMonth < m_Data.byWeek)
    {
        nOffset = -1;
    }
    else
    {
        nOffset = 0;
    }
    *pPeriod = MakePeriodAt(nOffset, t);
    return TRUE;
}

BOOL CSchedule_Everymonths_Dayofweek::CheckCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur)
{
    CDateTime d = FindNthWeekdayOfMonth(t, this, m_Data.byWeek);
    d = CDateTime(d.GetYear(), d.GetMonth(), d.GetDay(), m_Data.byHour, m_Data.byMinute,
                  m_Data.bySecond);
    int nSec = d.GetSecond() + m_Data.nDuration;
    CDateTime end(d.GetYear(), d.GetMonth(), d.GetDay(), d.GetHour(), d.GetMinute(), nSec);
    BOOL bRet = IsBetween(t, d, end);
    if (bRet == TRUE)
    {
        pCur->tBegin = d.GetTime();
        pCur->tEnd = end.GetTime();
        SetPeriodName(pCur);
    }
    return bRet;
}

sSchedulePeriod CSchedule_Everymonths_Dayofweek::MakePeriodAt(int nMonthOffset, CDateTime t)
{
    sSchedulePeriod period;
    period.dwScheduleID = m_Data.dwScheduleID;
    int nMonth = t.GetMonth() + nMonthOffset;
    CDateTime d(t.GetYear(), nMonth, 1, 0, 0, 0);
    d = FindNthWeekdayOfMonth(d, this, m_Data.byWeek);
    d = CDateTime(d.GetYear(), d.GetMonth(), d.GetDay(), m_Data.byHour, m_Data.byMinute,
                  m_Data.bySecond);
    __time32_t tBegin = d.GetTime();
    period.tBegin = tBegin;
    period.tEnd = tBegin + m_Data.nDuration;
    return period;
}

// ---------------------------------------------------------------------------------------------
// CSchedule_Everymonths_Date

CSchedule_Everymonths_Date::CSchedule_Everymonths_Date()
{
    // BUG(FW11-022): the type of Everyweeks_Dayofweek instead of SCHEDULE_TYPE_EVERYMONTHS_DATE.
    m_nType = SCHEDULE_TYPE_EVERYWEEKS_DAYOFWEEK;
}

BOOL CSchedule_Everymonths_Date::GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                                  sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    if (pNext)
        SetPeriodIdAndName(pNext);
    BOOL bRet = CheckCurrentPeriod(t, pCur);
    if (t.GetDay() == m_Data.byDay)
    {
        GetPeriodOnMatchingDay(t, pCur, pNext);
        return bRet;
    }
    int nDay = m_Data.byDay;
    CDateTime d;
    if (t.GetDay() < nDay)
        d = CDateTime(t.GetYear(), t.GetMonth(), nDay, m_Data.byHour, m_Data.byMinute,
                      m_Data.bySecond);
    else
        d = CDateTime(t.GetYear(), t.GetMonth() + 1, nDay, m_Data.byHour, m_Data.byMinute,
                      m_Data.bySecond);
    __time32_t tBegin = d.GetTime();
    SetPeriodRange(pNext, tBegin, tBegin + m_Data.nDuration);
    return bRet;
}

BOOL CSchedule_Everymonths_Date::GetPeriodOnMatchingDay(CDateTime& t, sSchedulePeriod* pCur,
                                                        sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    __time32_t tBegin = begin.GetTime();
    CDateTime end(tBegin + m_Data.nDuration);
    if (t >= begin)
    {
        BOOL bRet = FALSE;
        if (t < end)
        {
            pCur->tBegin = tBegin;
            pCur->tEnd = end.GetTime();
            bRet = TRUE;
        }
        CDateTime next(tBegin);
        int nDay = next.GetDay();
        int nMonth = next.GetMonth() + 1;
        next = CDateTime(next.GetYear(), nMonth, nDay, m_Data.byHour, m_Data.byMinute,
                         m_Data.bySecond);
        __time32_t tNext = next.GetTime();
        SetPeriodRange(pNext, tNext, tNext + m_Data.nDuration);
        return bRet;
    }
    SetPeriodRange(pNext, tBegin, end.GetTime());
    return FALSE;
}

BOOL CSchedule_Everymonths_Date::AdvanceToNextPeriod(CDateTime& /*t*/)
{
    CDateTime d(m_Period.tBegin);
    int nSec = d.GetSecond();
    int nMin = d.GetMinute();
    int nHour = d.GetHour();
    int nDay = d.GetDay();
    int nMonth = d.GetMonth() + 1;
    d = CDateTime(d.GetYear(), nMonth, nDay, nHour, nMin, nSec);
    m_Period.tBegin = d.GetTime();
    m_Period.tEnd = m_Period.tBegin + m_Data.nDuration;
    return TRUE;
}

BOOL CSchedule_Everymonths_Date::GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod)
{
    if (!pPeriod)
        return FALSE;
    t = ClampToEndTime(t);
    SetPeriodIdAndName(pPeriod);
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    __time32_t tBegin = begin.GetTime();
    CDateTime end(tBegin + m_Data.nDuration);
    CDateTime d;
    if (t.GetDay() == m_Data.byDay)
    {
        d = tBegin;
        int nDay = d.GetDay();
        int nMonth = d.GetMonth();
        if (!(t > begin) || t < end)
            --nMonth;
        d = CDateTime(d.GetYear(), nMonth, nDay, m_Data.byHour, m_Data.byMinute, m_Data.bySecond);
    }
    else
    {
        int nDay = m_Data.byDay;
        int nMonth = t.GetMonth();
        if (t.GetDay() < nDay)
            --nMonth;
        d = CDateTime(t.GetYear(), nMonth, nDay, m_Data.byHour, m_Data.byMinute, m_Data.bySecond);
    }
    __time32_t tPrev = d.GetTime();
    pPeriod->tBegin = tPrev;
    pPeriod->tEnd = tPrev + m_Data.nDuration;
    return TRUE;
}

BOOL CSchedule_Everymonths_Date::CheckCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur)
{
    CDateTime begin(t.GetYear(), t.GetMonth(), m_Data.byDay, m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    int nSec = begin.GetSecond() + m_Data.nDuration;
    CDateTime end(begin.GetYear(), begin.GetMonth(), begin.GetDay(), begin.GetHour(),
                  begin.GetMinute(), nSec);
    BOOL bRet = IsBetween(t, begin, end);
    if (bRet == TRUE)
    {
        pCur->tBegin = begin.GetTime();
        pCur->tEnd = end.GetTime();
        SetPeriodName(pCur);
    }
    return bRet;
}

// ---------------------------------------------------------------------------------------------
// CSchedule_Somemonths_Dayofweek

CSchedule_Somemonths_Dayofweek::CSchedule_Somemonths_Dayofweek()
{
    m_nType = SCHEDULE_TYPE_SOMEMONTHS_DAYOFWEEK;
}

BOOL CSchedule_Somemonths_Dayofweek::GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                                      sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    if (pNext)
        SetPeriodIdAndName(pNext);
    CDateTime start = GetStartTime();
    int nMonths = CountMonths(start, t);
    int nPhase = nMonths % m_Data.nInterval;
    if (nPhase == 0)
        nPhase = m_Data.nInterval;
    BOOL bRet = CheckCurrentPeriod(nMonths, start, t, pCur);
    int nMonth = m_Data.byMonth;
    if (nPhase == nMonth)
    {
        int nWeekOfMonth = GetWeekOfMonth(t);
        if (nWeekOfMonth == m_Data.byWeek)
        {
            if (t.GetDayOfWeek() == m_Data.byDayOfWeek)
            {
                GetPeriodOnMatchingDay(t, pCur, pNext);
                return bRet;
            }
            if (t.GetDayOfWeek() < m_Data.byDayOfWeek)
                FillPeriodNthWeekdayOfMonth(this, pNext, 0, t);
            else
                FillPeriodNthWeekdayOfMonth(this, pNext, m_Data.nInterval, t);
            return bRet;
        }
        if (nWeekOfMonth < m_Data.byWeek)
            FillPeriodNthWeekdayOfMonth(this, pNext, 0, t);
        else
            FillPeriodNthWeekdayOfMonth(this, pNext, m_Data.nInterval, t);
        return bRet;
    }
    if (nPhase < nMonth)
        FillPeriodNthWeekdayOfMonth(this, pNext, nMonth - nPhase, t);
    else
        FillPeriodNthWeekdayOfMonth(this, pNext, nMonth - nPhase + m_Data.nInterval, t);
    return bRet;
}

BOOL CSchedule_Somemonths_Dayofweek::GetPeriodOnMatchingDay(CDateTime& t, sSchedulePeriod* pCur,
                                                            sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    __time32_t tBegin = begin.GetTime();
    CDateTime end(tBegin + m_Data.nDuration);
    if (t >= begin)
    {
        if (t < end)
        {
            pCur->tBegin = tBegin;
            pCur->tEnd = end.GetTime();
            FillPeriodNthWeekdayOfMonth(this, pNext, m_Data.nInterval, t);
            return TRUE;
        }
        FillPeriodNthWeekdayOfMonth(this, pNext, m_Data.nInterval, t);
        return FALSE;
    }
    SetPeriodRange(pNext, tBegin, end.GetTime());
    return FALSE;
}

BOOL CSchedule_Somemonths_Dayofweek::AdvanceToNextPeriod(CDateTime& /*t*/)
{
    CDateTime d(m_Period.tBegin);
    int nMonth = d.GetMonth() + m_Data.nInterval;
    d = CDateTime(d.GetYear(), nMonth, 1, 0, 0, 0);
    d = FindNthWeekdayOfMonth(d, this, m_Data.byWeek);
    d = CDateTime(d.GetYear(), d.GetMonth(), d.GetDay(), m_Data.byHour, m_Data.byMinute,
                  m_Data.bySecond);
    m_Period.tBegin = d.GetTime();
    m_Period.tEnd = m_Period.tBegin + m_Data.nDuration;
    return TRUE;
}

BOOL CSchedule_Somemonths_Dayofweek::GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod)
{
    if (!pPeriod)
        return FALSE;
    t = ClampToEndTime(t);
    SetPeriodIdAndName(pPeriod);
    CDateTime start = GetStartTime();
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    CDateTime end(begin.GetTime() + m_Data.nDuration);
    int nInterval = m_Data.nInterval;
    int nPhase = CountMonths(start, t) % nInterval;
    if (nPhase == 0)
        nPhase = nInterval;
    int nMonth = m_Data.byMonth;
    int nOffset;
    if (nPhase == nMonth)
    {
        int nWeekOfMonth = GetWeekOfMonth(t);
        if (nWeekOfMonth == m_Data.byWeek)
        {
            if (t.GetDayOfWeek() == m_Data.byDayOfWeek)
            {
                if (t > begin && !(t < end))
                    nOffset = 0;
                else
                    nOffset = -nInterval;
            }
            else if (t.GetDayOfWeek() < m_Data.byDayOfWeek)
            {
                nOffset = -nInterval;
            }
            else
            {
                nOffset = 0;
            }
        }
        else if (nWeekOfMonth < m_Data.byWeek)
        {
            nOffset = -nInterval;
        }
        else
        {
            nOffset = 0;
        }
    }
    else if (nPhase < nMonth)
    {
        nOffset = nMonth - nInterval - nPhase;
    }
    else
    {
        nOffset = nMonth - nPhase;
    }
    *pPeriod = MakePeriodAt(nOffset, t);
    return TRUE;
}

BOOL CSchedule_Somemonths_Dayofweek::CheckCurrentPeriod(int nMonths, CDateTime& start,
                                                        CDateTime& t, sSchedulePeriod* pCur)
{
    int nCycles = nMonths / m_Data.nInterval;
    int nSec = start.GetSecond();
    int nMin = start.GetMinute();
    int nHour = start.GetHour();
    int nDay = start.GetDay();
    int nMonth = start.GetMonth() + m_Data.byMonth + m_Data.nInterval * nCycles - 1;
    CDateTime d(start.GetYear(), nMonth, nDay, nHour, nMin, nSec);
    d = FindNthWeekdayOfMonth(d, this, m_Data.byWeek);
    d = CDateTime(d.GetYear(), d.GetMonth(), d.GetDay(), m_Data.byHour, m_Data.byMinute,
                  m_Data.bySecond);
    nSec = d.GetSecond() + m_Data.nDuration;
    CDateTime end(d.GetYear(), d.GetMonth(), d.GetDay(), d.GetHour(), d.GetMinute(), nSec);
    BOOL bRet = IsBetween(t, d, end);
    if (bRet == TRUE)
    {
        pCur->tBegin = d.GetTime();
        pCur->tEnd = end.GetTime();
        SetPeriodName(pCur);
    }
    return bRet;
}

sSchedulePeriod CSchedule_Somemonths_Dayofweek::MakePeriodAt(int nMonthOffset, CDateTime t)
{
    sSchedulePeriod period;
    period.dwScheduleID = m_Data.dwScheduleID;
    int nMonth = t.GetMonth() + nMonthOffset;
    CDateTime d(t.GetYear(), nMonth, 1, 0, 0, 0);
    d = FindNthWeekdayOfMonth(d, this, m_Data.byWeek);
    d = CDateTime(d.GetYear(), d.GetMonth(), d.GetDay(), m_Data.byHour, m_Data.byMinute,
                  m_Data.bySecond);
    __time32_t tBegin = d.GetTime();
    period.tBegin = tBegin;
    period.tEnd = tBegin + m_Data.nDuration;
    return period;
}

// ---------------------------------------------------------------------------------------------
// CSchedule_Somemonths_Date

CSchedule_Somemonths_Date::CSchedule_Somemonths_Date()
{
    m_nType = SCHEDULE_TYPE_SOMEMONTHS_DATE;
}

BOOL CSchedule_Somemonths_Date::GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                                 sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    if (pNext)
        SetPeriodIdAndName(pNext);
    CDateTime start = GetStartTime();
    int nMonths = CountMonths(start, t);
    int nPhase = nMonths % m_Data.nInterval;
    if (nPhase == 0)
        nPhase = m_Data.nInterval;
    BOOL bRet = CheckCurrentPeriod(nMonths, start, t, pCur);
    int nMonth = m_Data.byMonth;
    if (nPhase == nMonth)
    {
        if (t.GetDay() == m_Data.byDay)
        {
            GetPeriodOnMatchingDay(t, pCur, pNext);
            return bRet;
        }
        if (t.GetDay() < m_Data.byDay)
            FillPeriodAtMonthOffset(pNext, 0, t);
        else
            FillPeriodAtMonthOffset(pNext, m_Data.nInterval, t);
        return bRet;
    }
    if (nPhase < nMonth)
        FillPeriodAtMonthOffset(pNext, nMonth - nPhase, t);
    else
        FillPeriodAtMonthOffset(pNext, nMonth - nPhase + m_Data.nInterval, t);
    return bRet;
}

BOOL CSchedule_Somemonths_Date::GetPeriodOnMatchingDay(CDateTime& t, sSchedulePeriod* pCur,
                                                       sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    __time32_t tBegin = begin.GetTime();
    CDateTime end(tBegin + m_Data.nDuration);
    if (t >= begin)
    {
        if (t < end)
        {
            pCur->tBegin = tBegin;
            pCur->tEnd = end.GetTime();
            FillPeriodAtMonthOffset(pNext, m_Data.nInterval, t);
            return TRUE;
        }
        FillPeriodAtMonthOffset(pNext, m_Data.nInterval, t);
        return FALSE;
    }
    SetPeriodRange(pNext, tBegin, end.GetTime());
    return FALSE;
}

BOOL CSchedule_Somemonths_Date::AdvanceToNextPeriod(CDateTime& /*t*/)
{
    CDateTime d(m_Period.tBegin);
    int nMonth = d.GetMonth() + m_Data.nInterval;
    d = CDateTime(d.GetYear(), nMonth, m_Data.byDay, m_Data.byHour, m_Data.byMinute,
                  m_Data.bySecond);
    m_Period.tBegin = d.GetTime();
    m_Period.tEnd = m_Period.tBegin + m_Data.nDuration;
    return TRUE;
}

BOOL CSchedule_Somemonths_Date::GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod)
{
    if (!pPeriod)
        return FALSE;
    t = ClampToEndTime(t);
    SetPeriodIdAndName(pPeriod);
    CDateTime start = GetStartTime();
    CDateTime begin(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    CDateTime end(begin.GetTime() + m_Data.nDuration);
    int nInterval = m_Data.nInterval;
    int nPhase = CountMonths(start, t) % nInterval;
    if (nPhase == 0)
        nPhase = nInterval;
    int nMonth = m_Data.byMonth;
    int nOffset;
    if (nPhase == nMonth)
    {
        if (t.GetDay() == m_Data.byDay)
        {
            if (t > begin && !(t < end))
                nOffset = 0;
            else
                nOffset = -nInterval;
        }
        else if (t.GetDay() < m_Data.byDay)
        {
            nOffset = -nInterval;
        }
        else
        {
            nOffset = 0;
        }
    }
    else if (nPhase < nMonth)
    {
        nOffset = nMonth - nInterval - nPhase;
    }
    else
    {
        nOffset = nMonth - nPhase;
    }
    *pPeriod = MakePeriodAt(nOffset, t);
    return TRUE;
}

BOOL CSchedule_Somemonths_Date::CheckCurrentPeriod(int nMonths, CDateTime& start, CDateTime& t,
                                                   sSchedulePeriod* pCur)
{
    int nCycles = nMonths / m_Data.nInterval;
    int nMonth = start.GetMonth() + m_Data.byMonth + m_Data.nInterval * nCycles - 1;
    CDateTime begin(start.GetYear(), nMonth, m_Data.byDay, m_Data.byHour, m_Data.byMinute,
                    m_Data.bySecond);
    int nSec = begin.GetSecond() + m_Data.nDuration;
    CDateTime end(begin.GetYear(), begin.GetMonth(), begin.GetDay(), begin.GetHour(),
                  begin.GetMinute(), nSec);
    BOOL bRet = IsBetween(t, begin, end);
    if (bRet == TRUE)
    {
        pCur->tBegin = begin.GetTime();
        pCur->tEnd = end.GetTime();
        SetPeriodName(pCur);
    }
    return bRet;
}

void CSchedule_Somemonths_Date::FillPeriodAtMonthOffset(sSchedulePeriod* pPeriod,
                                                        int nMonthOffset, CDateTime t)
{
    if (!pPeriod)
        return;
    int nMonth = t.GetMonth() + nMonthOffset;
    CDateTime d(t.GetYear(), nMonth, m_Data.byDay, m_Data.byHour, m_Data.byMinute,
                m_Data.bySecond);
    __time32_t tBegin = d.GetTime();
    pPeriod->tBegin = tBegin;
    pPeriod->tEnd = tBegin + m_Data.nDuration;
}

sSchedulePeriod CSchedule_Somemonths_Date::MakePeriodAt(int nMonthOffset, CDateTime t)
{
    sSchedulePeriod period;
    period.dwScheduleID = m_Data.dwScheduleID;
    int nMonth = t.GetMonth() + nMonthOffset;
    CDateTime d(t.GetYear(), nMonth, m_Data.byDay, m_Data.byHour, m_Data.byMinute,
                m_Data.bySecond);
    __time32_t tBegin = d.GetTime();
    period.tBegin = tBegin;
    period.tEnd = tBegin + m_Data.nDuration;
    return period;
}

// ---------------------------------------------------------------------------------------------
// CSchedule_Repeat_Sometime

CSchedule_Repeat_Sometime::CSchedule_Repeat_Sometime()
{
    m_nType = SCHEDULE_TYPE_REPEAT_SOMETIME;
}

BOOL CSchedule_Repeat_Sometime::GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                                 sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    if (pNext)
    {
        SetPeriodIdAndName(pNext);
        SetPeriodIdAndName(pCur);
    }
    CDateTime dayStart(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                       m_Data.bySecond);
    __time32_t tBegin;
    if (t >= dayStart)
    {
        int nMinutes = (t - dayStart).GetTotalMinutes();
        int nInterval = m_Data.nRepeatInterval;
        int nIntoSlot = nMinutes % nInterval;
        if (nIntoSlot == 0)
            nIntoSlot = nInterval;
        if (nIntoSlot == nInterval)
        {
            CheckCurrentPeriod(pCur, pNext, 0, t);
            return TRUE;
        }
        if (nIntoSlot < m_Data.nRepeatLength)
        {
            CheckCurrentPeriod(pCur, pNext, nIntoSlot, t);
            return TRUE;
        }
        int nMinute = m_Data.byMinute + (nInterval - nIntoSlot + nMinutes);
        CDateTime slot(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, nMinute,
                       m_Data.bySecond);
        tBegin = slot.GetTime();
        int nEndMinute = slot.GetMinute() + m_Data.nRepeatLength;
        CDateTime end(slot.GetYear(), slot.GetMonth(), slot.GetDay(), slot.GetHour(), nEndMinute,
                      0);
        SetPeriodRange(pNext, tBegin, end.GetTime());
        FillPeriod(pNext, t);
        return FALSE;
    }
    tBegin = dayStart.GetTime();
    int nEndMinute = dayStart.GetMinute() + m_Data.nRepeatLength;
    CDateTime end(dayStart.GetYear(), dayStart.GetMonth(), dayStart.GetDay(), dayStart.GetHour(),
                  nEndMinute, 0);
    SetPeriodRange(pNext, tBegin, end.GetTime());
    FillPeriod(pNext, t);
    return FALSE;
}

BOOL CSchedule_Repeat_Sometime::AdvanceToNextPeriod(CDateTime& t)
{
    CDateTime d(m_Period.tBegin);
    int nSec = d.GetSecond();
    int nMin = d.GetMinute() + m_Data.nRepeatInterval;
    int nHour = d.GetHour();
    d = CDateTime(d.GetYear(), d.GetMonth(), d.GetDay(), nHour, nMin, nSec);
    m_Period.tBegin = d.GetTime();
    nSec = d.GetSecond();
    nMin = d.GetMinute() + m_Data.nRepeatLength;
    nHour = d.GetHour();
    d = CDateTime(d.GetYear(), d.GetMonth(), d.GetDay(), nHour, nMin, nSec);
    m_Period.tEnd = d.GetTime();
    FillPeriod(&m_Period, t);
    return TRUE;
}

BOOL CSchedule_Repeat_Sometime::GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod)
{
    if (!pPeriod)
        return FALSE;
    t = ClampToEndTime(t);
    SetPeriodIdAndName(pPeriod);
    SetPeriodIdAndName(pPeriod);
    CDateTime dayStart(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                       m_Data.bySecond);
    if (t > dayStart)
    {
        int nMinutes = (t - dayStart).GetTotalMinutes();
        int nInterval = m_Data.nRepeatInterval;
        int nIntoSlot = nMinutes % nInterval;
        if (nIntoSlot == 0)
            nIntoSlot = nInterval;
        if (nIntoSlot == nInterval)
            *pPeriod = MakePeriodAt(-nInterval, t);
        else if (nIntoSlot < m_Data.nRepeatLength)
            *pPeriod = MakePeriodAt(-(nInterval + nIntoSlot), t);
        else
            *pPeriod = MakePeriodAt(-nIntoSlot, t);
        CDateTime begin(pPeriod->tBegin);
        if (begin < dayStart)
            *pPeriod = MakePeriodAt(0, AlignToSlot(t));
        return TRUE;
    }
    *pPeriod = MakePeriodAt(0, AlignToSlot(t));
    return TRUE;
}

void CSchedule_Repeat_Sometime::CheckCurrentPeriod(sSchedulePeriod* pCur, sSchedulePeriod* pNext,
                                                   int nOffset, CDateTime t)
{
    if (!pNext || !pCur)
        return;
    // BUG(FW11-025): the running slot is taken to start nOffset minutes after t instead of
    // before it.
    int nMin = t.GetMinute() + nOffset;
    CDateTime slot(t.GetYear(), t.GetMonth(), t.GetDay(), t.GetHour(), nMin, 0);
    pCur->tBegin = slot.GetTime();
    nMin = slot.GetMinute() + m_Data.nRepeatLength - nOffset;
    CDateTime end(slot.GetYear(), slot.GetMonth(), slot.GetDay(), slot.GetHour(), nMin, 0);
    pCur->tEnd = end.GetTime();
    int nSec = slot.GetSecond();
    nMin = slot.GetMinute() + m_Data.nRepeatInterval;
    int nHour = slot.GetHour();
    slot = CDateTime(slot.GetYear(), slot.GetMonth(), slot.GetDay(), nHour, nMin, nSec);
    pNext->tBegin = slot.GetTime();
    nSec = slot.GetSecond();
    nMin = slot.GetMinute() + m_Data.nRepeatLength;
    nHour = slot.GetHour();
    end = CDateTime(slot.GetYear(), slot.GetMonth(), slot.GetDay(), nHour, nMin, nSec);
    pNext->tEnd = end.GetTime();
    FillPeriod(pNext, t);
}

void CSchedule_Repeat_Sometime::FillPeriod(sSchedulePeriod* pPeriod, CDateTime t)
{
    if (!pPeriod)
        return;
    CDateTime begin(pPeriod->tBegin);
    CDateTime windowEnd(t.GetYear(), t.GetMonth(), t.GetDay(), m_Data.byHour, m_Data.byMinute,
                        m_Data.bySecond);
    windowEnd = windowEnd.GetTime() + m_Data.nDuration;
    if (begin.GetTime() >= windowEnd.GetTime())
    {
        int nDay = t.GetDay() + 1;
        CDateTime nextDay(t.GetYear(), t.GetMonth(), nDay, 0, 0, 0);
        sSchedulePeriod cur;
        GetCurrentPeriod(nextDay, &cur, pPeriod);
    }
}

sSchedulePeriod CSchedule_Repeat_Sometime::MakePeriodAt(int nOffset, CDateTime t)
{
    sSchedulePeriod period;
    period.dwScheduleID = m_Data.dwScheduleID;
    int nMin = t.GetMinute() + nOffset;
    CDateTime d(t.GetYear(), t.GetMonth(), t.GetDay(), t.GetHour(), nMin, 0);
    period.tBegin = d.GetTime();
    nMin = t.GetMinute() + m_Data.nRepeatInterval + nOffset;
    d = CDateTime(t.GetYear(), t.GetMonth(), t.GetDay(), t.GetHour(), nMin, 0);
    period.tEnd = d.GetTime();
    return period;
}

CDateTime CSchedule_Repeat_Sometime::AlignToSlot(CDateTime t)
{
    int nDay = t.GetDay() - 1;
    CDateTime d(t.GetYear(), t.GetMonth(), nDay, m_Data.byHour, m_Data.byMinute, m_Data.bySecond);
    CDateTime limit(d.GetTime() + m_Data.nDuration);
    while (d < limit)
    {
        int nSec = d.GetSecond();
        int nMin = d.GetMinute() + m_Data.nRepeatInterval;
        int nHour = d.GetHour();
        d = CDateTime(d.GetYear(), d.GetMonth(), d.GetDay(), nHour, nMin, nSec);
    }
    int nSec = d.GetSecond();
    int nMin = d.GetMinute() - m_Data.nRepeatInterval;
    int nHour = d.GetHour();
    d = CDateTime(d.GetYear(), d.GetMonth(), d.GetDay(), nHour, nMin, nSec);
    return d;
}

// ---------------------------------------------------------------------------------------------
// CSchedule_Once_Date

CSchedule_Once_Date::CSchedule_Once_Date()
{
    m_nType = SCHEDULE_TYPE_ONCE_DATE;
}

BOOL CSchedule_Once_Date::GetCurrentPeriod(CDateTime& t, sSchedulePeriod* pCur,
                                           sSchedulePeriod* pNext)
{
    if (!pCur)
        return FALSE;
    if (pNext)
        SetPeriodIdAndName(pNext);
    CDateTime begin = GetStartTime();
    // BUG(FW11-026): the end gets the second of the start.
    CDateTime end(m_Data.stEnd.wYear, m_Data.stEnd.wMonth, m_Data.stEnd.wDay,
                  m_Data.stEnd.wHour, m_Data.stEnd.wMinute, m_Data.stBegin.wSecond);
    BOOL bRet = FillPeriodIfBetween(pCur, begin, end, this, t);
    if (t > begin)
    {
        if (t < end)
        {
            pCur->tBegin = begin.GetTime();
            pCur->tEnd = end.GetTime();
        }
        // pNext is used without a check here.
        int nSec = end.GetSecond();
        int nMin = end.GetMinute() + 30;
        int nHour = end.GetHour();
        CDateTime after(end.GetYear(), end.GetMonth(), end.GetDay(), nHour, nMin, nSec);
        __time32_t tAfter = after.GetTime();
        pNext->tBegin = tAfter;
        pNext->tEnd = tAfter;
        return bRet;
    }
    pNext->tBegin = begin.GetTime();
    pNext->tEnd = end.GetTime();
    return bRet;
}

BOOL CSchedule_Once_Date::AdvanceToNextPeriod(CDateTime& /*t*/)
{
    // BUG(FW11-026): the end minute goes into the second field.
    CDateTime after(m_Data.stEnd.wYear, m_Data.stEnd.wMonth, m_Data.stEnd.wDay,
                    m_Data.stEnd.wHour, m_Data.stEnd.wMinute + 30, m_Data.stEnd.wMinute);
    __time32_t tAfter = after.GetTime();
    m_Period.tBegin = tAfter;
    m_Period.tEnd = tAfter;
    return TRUE;
}

BOOL CSchedule_Once_Date::GetPrevPeriod(CDateTime t, sSchedulePeriod* pPeriod)
{
    if (!pPeriod)
        return FALSE;
    t = ClampToEndTime(t);
    SetPeriodIdAndName(pPeriod);
    CDateTime begin(m_Data.stBegin.wYear, m_Data.stBegin.wMonth, m_Data.stBegin.wDay,
                    m_Data.byHour, m_Data.byMinute, m_Data.bySecond);
    CDateTime end(begin.GetTime() + m_Data.nDuration);
    if (t < end)
        return FALSE;
    begin = CDateTime(m_Data.stBegin.wYear, m_Data.stBegin.wMonth, m_Data.stBegin.wDay,
                      m_Data.byHour, m_Data.byMinute, m_Data.bySecond);
    __time32_t tBegin = begin.GetTime();
    pPeriod->tBegin = tBegin;
    pPeriod->tEnd = tBegin + m_Data.nDuration;
    return TRUE;
}

} // namespace NewSchedule
