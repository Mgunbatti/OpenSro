#include "BSLib/NewSchedule/ScheduleManager.h"

#include <string.h>
#include <time.h>

namespace NewSchedule
{

typedef std::map<DWORD, ISchedule*> ScheduleMap;
typedef std::list<sSchedulePeriod> PeriodList;
typedef std::list<sScheduleEvent> EventList;

// SR_ShardManager 0x6d7e40: delete through the base class (no virtual destructor).
static BOOL DeleteSchedule(ISchedule* pSchedule)
{
    if (!pSchedule)
        return FALSE;
    delete pSchedule;
    return TRUE;
}

// Keeps the first nCount entries of a sorted list.
static void TrimPeriodList(PeriodList& lstPeriod, int nCount)
{
    int nKept = 0;
    PeriodList::iterator it = lstPeriod.begin();
    while (it != lstPeriod.end())
    {
        if (nKept < nCount)
        {
            ++nKept;
            ++it;
        }
        else
        {
            it = lstPeriod.erase(it);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Events and periods

sScheduleInfo sScheduleEvent::ToScheduleInfo() const
{
    sScheduleInfo info;
    ZeroScheduleStruct(info);
    info.dwGroupID = pGroup->m_dwDefineID;
    info.dwScheduleID = dwScheduleID;
    info.bBegin = bBegin == 1;
    info.dwBegin = dwBegin;
    info.dwEnd = dwEnd;
    info.strDefine = pGroup->GetDefineName();
    info.strName = strName;
    return info;
}

bool ScheduleEventLessByTime(const sScheduleEvent& left, const sScheduleEvent& right)
{
    DWORD dwLeft = left.bBegin == 1 ? left.dwBegin : left.dwEnd;
    DWORD dwRight = right.bBegin == 1 ? right.dwBegin : right.dwEnd;
    return dwLeft < dwRight;
}

bool SchedulePeriodLess(const sSchedulePeriod& left, const sSchedulePeriod& right)
{
    return left.tBegin < right.tBegin;
}

bool SchedulePeriodGreater(const sSchedulePeriod& left, const sSchedulePeriod& right)
{
    return left.tBegin > right.tBegin;
}

// ---------------------------------------------------------------------------------------------
// CScheduleGroup

void CScheduleGroup::AddSchedule(const SCHEDULE_DATA* pData)
{
    ISchedule* pSchedule = ISchedule::CreateByType(pData);
    if (!pSchedule)
        return;
    memcpy(&pSchedule->m_Data, pData, sizeof(SCHEDULE_DATA));
    pSchedule->m_Period.dwScheduleID = pData->dwScheduleID;
    m_mapSchedule.insert(ScheduleMap::value_type(pData->dwScheduleID, pSchedule));
    CDateTime now(_time32(NULL));
    if (pSchedule->IsAfterEnd(now) == TRUE)
        return;
    m_mapActive.insert(ScheduleMap::value_type(pData->dwScheduleID, pSchedule));
}

void CScheduleGroup::RemoveSchedule(DWORD dwScheduleID)
{
    ScheduleMap::iterator it = m_mapActive.find(dwScheduleID);
    if (it != m_mapActive.end())
        m_mapActive.erase(it);
    it = m_mapSchedule.find(dwScheduleID);
    if (it != m_mapSchedule.end())
    {
        DeleteSchedule(it->second);
        m_mapSchedule.erase(it);
    }
}

ISchedule* CScheduleGroup::FindActiveSchedule(DWORD dwScheduleID)
{
    ScheduleMap::iterator it = m_mapActive.find(dwScheduleID);
    if (it == m_mapActive.end())
        return NULL;
    return it->second;
}

void CScheduleGroup::CollectActivePeriods(PeriodList& lstPeriod, CDateTime& now)
{
    sSchedulePeriod period;
    ZeroScheduleStruct(period);
    for (ScheduleMap::iterator it = m_mapSchedule.begin(); it != m_mapSchedule.end(); ++it)
    {
        ISchedule* pSchedule = it->second;
        if (!pSchedule)
            continue;
        period.dwScheduleID = pSchedule->m_Data.dwScheduleID;
        if (pSchedule->IsAfterEnd(now) == TRUE)
        {
            ScheduleMap::iterator itActive = m_mapActive.find(pSchedule->m_Data.dwScheduleID);
            if (itActive != m_mapActive.end())
                m_mapActive.erase(itActive);
        }
        else if (!pSchedule->IsAfterStart(now))
        {
            CDateTime start = pSchedule->GetStartTime();
            pSchedule->GetCurrentPeriod(start, &period, &pSchedule->m_Period);
        }
        else if (pSchedule->IsAfterStart(now) == TRUE && !pSchedule->IsAfterEnd(now))
        {
            if (pSchedule->GetCurrentPeriod(now, &period, &pSchedule->m_Period) == TRUE)
                lstPeriod.push_back(period);
        }
    }
}

void CScheduleGroup::GetPrevPeriods(PeriodList& lstPeriod, int nCount, CDateTime t)
{
    sSchedulePeriod period;
    for (ScheduleMap::iterator it = m_mapSchedule.begin(); it != m_mapSchedule.end(); ++it)
    {
        ISchedule* pSchedule = it->second;
        if (!pSchedule)
            continue;
        period.tBegin = t.GetTime();
        for (int i = 0; i < nCount; ++i)
        {
            CDateTime cur(period.tBegin);
            ZeroScheduleStruct(period);
            if (pSchedule->GetPrevPeriod(cur, &period) == TRUE)
            {
                cur = period.tBegin;
                if (pSchedule->IsAfterStart(cur) != TRUE)
                    break;
                lstPeriod.push_back(period);
            }
        }
    }
    lstPeriod.sort(SchedulePeriodGreater);
    TrimPeriodList(lstPeriod, nCount);
}

void CScheduleGroup::GetNextPeriods(PeriodList& lstPeriod, int nCount, CDateTime t)
{
    sSchedulePeriod period;
    for (ScheduleMap::iterator it = m_mapSchedule.begin(); it != m_mapSchedule.end(); ++it)
    {
        ISchedule* pSchedule = it->second;
        if (!pSchedule)
            continue;
        period.tBegin = t.GetTime();
        for (int i = 0; i < nCount; ++i)
        {
            CDateTime cur(period.tBegin);
            ZeroScheduleStruct(period);
            sSchedulePeriod running;
            ZeroScheduleStruct(running);
            if (pSchedule->IsAfterStart(cur) != TRUE)
                cur = pSchedule->GetStartTime();
            pSchedule->GetCurrentPeriod(cur, &running, &period);
            // Checks the rule's own m_Period, not the period just computed.
            if (pSchedule->IsPeriodExpired() == TRUE)
                break;
            CDateTime begin(period.tBegin);
            CDateTime end(period.tEnd);
            if (pSchedule->IsAfterStart(begin) == TRUE && !pSchedule->IsAfterEnd(end))
                lstPeriod.push_back(period);
        }
    }
    lstPeriod.sort(SchedulePeriodLess);
    TrimPeriodList(lstPeriod, nCount);
}

BOOL CScheduleGroup::GetNearestPeriod(CDateTime& now, sSchedulePeriod* pPeriod, BOOL bAdvance)
{
    PeriodList lstNext;
    ScheduleMap::iterator it = m_mapActive.begin();
    while (it != m_mapActive.end())
    {
        ISchedule* pSchedule = it->second;
        if (!pSchedule)
        {
            ++it;
            continue;
        }
        if (pSchedule->IsPeriodExpired() == TRUE)
        {
            m_mapActive.erase(it++);
            continue;
        }
        if (bAdvance == TRUE && now.GetTime() >= pSchedule->m_Period.tBegin)
        {
            pSchedule->AdvanceToNextPeriod(now);
            if (pSchedule->IsPeriodExpired() == TRUE)
            {
                m_mapActive.erase(it++);
                continue;
            }
        }
        lstNext.push_back(pSchedule->m_Period);
        ++it;
    }
    if (lstNext.size() == 0)
        return FALSE;
    lstNext.sort(SchedulePeriodLess);
    *pPeriod = lstNext.front();
    return TRUE;
}

BOOL CScheduleGroup::GetSchedulePeriod(DWORD dwScheduleID, CDateTime& now, sSchedulePeriod* pCur)
{
    ISchedule* pSchedule = FindActiveSchedule(dwScheduleID);
    if (!pSchedule)
        return FALSE;
    pSchedule->GetCurrentPeriod(now, pCur, &pSchedule->m_Period);
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// CScheduleManager

CScheduleManager::CScheduleManager()
{
    m_bRunning = FALSE;
}

CScheduleManager::~CScheduleManager()
{
    DeleteAllGroups();
}

void CScheduleManager::RebuildEventList()
{
    sSchedulePeriod nearest;
    CDateTime now(_time32(NULL));
    for (std::vector<CScheduleGroup*>::iterator it = m_vecGroup.begin(); it != m_vecGroup.end();
         ++it)
    {
        CScheduleGroup* pGroup = *it;
        if (!pGroup)
            continue;
        ZeroScheduleStruct(nearest);
        PeriodList lstActive;
        pGroup->CollectActivePeriods(lstActive, now);
        for (PeriodList::iterator itPeriod = lstActive.begin(); itPeriod != lstActive.end();
             ++itPeriod)
        {
            sScheduleEvent event;
            ZeroScheduleStruct(event);
            event.dwScheduleID = itPeriod->dwScheduleID;
            event.dwBegin = itPeriod->tBegin;
            event.dwEnd = itPeriod->tEnd;
            event.pGroup = pGroup;
            event.bBegin = 1;
            PeriodList lstPrev;
            // BUG(FW11-024): without a previous period, begin and end of the event come from
            // this uninitialised period.
            sSchedulePeriod prev;
            GetPrevPeriods(lstPrev, pGroup->GetDefineName(), 1, now);
            if (!lstPrev.empty())
                prev = lstPrev.front();
            event.dwBegin = prev.tBegin;
            event.dwEnd = prev.tEnd;
            OnServiceStartSchedule(event);
            ISchedule* pSchedule = pGroup->FindActiveSchedule(itPeriod->dwScheduleID);
            PushEvent(pGroup, itPeriod->tBegin, itPeriod->tEnd, 0, pSchedule);
        }
        if (pGroup->GetNearestPeriod(now, &nearest, FALSE) == TRUE)
        {
            pGroup->m_Period = nearest;
            ISchedule* pSchedule = pGroup->FindActiveSchedule(nearest.dwScheduleID);
            PushEvent(pGroup, nearest.tBegin, nearest.tEnd, 1, pSchedule);
            PushEvent(pGroup, nearest.tBegin, nearest.tEnd, 0, pSchedule);
        }
    }
    m_listEvent.sort(ScheduleEventLessByTime);
    m_bRunning = TRUE;
}

void CScheduleManager::ProcessDueEvents()
{
    CDateTime now(_time32(NULL));
    BOOL bQueued = FALSE;
    EventList::iterator it = m_listEvent.begin();
    while (it != m_listEvent.end())
    {
        sScheduleEvent event(*it);
        DWORD dwTime = event.bBegin == 1 ? event.dwBegin : event.dwEnd;
        if ((DWORD)now.GetTime() < dwTime)
            break;
        OnScheduleEvent(event);
        if (event.bBegin == 1)
        {
            CScheduleGroup* pGroup = event.pGroup;
            if (!pGroup)
            {
                // A begin event without a group stays queued.
                ++it;
                continue;
            }
            sSchedulePeriod period;
            ZeroScheduleStruct(period);
            if (pGroup->GetNearestPeriod(now, &period, TRUE) == TRUE)
            {
                pGroup->m_Period = period;
                ISchedule* pSchedule = pGroup->FindActiveSchedule(period.dwScheduleID);
                PushEvent(pGroup, period.tBegin, period.tEnd, 1, pSchedule);
                PushEvent(pGroup, period.tBegin, period.tEnd, 0, pSchedule);
                bQueued = TRUE;
            }
        }
        it = m_listEvent.erase(it);
    }
    if (bQueued == TRUE)
        m_listEvent.sort(ScheduleEventLessByTime);
}

void CScheduleManager::DeleteAllGroups()
{
    m_listEvent.clear();
    for (size_t i = 0; i < m_vecGroup.size(); ++i)
    {
        if (m_vecGroup[i])
        {
            delete m_vecGroup[i];
            m_vecGroup[i] = NULL;
        }
    }
    m_vecGroup.clear();
}

void CScheduleManager::AddGroup(const SCHEDULE_DEFINE_DATA* pDefine)
{
    CScheduleGroup* pGroup = new CScheduleGroup;
    if (!pGroup)
        return;
    pGroup->m_dwDefineID = pDefine->dwDefineID;
    pGroup->m_strName.assign(pDefine->szCodeName, strlen(pDefine->szCodeName));
    m_vecGroup.push_back(pGroup);
}

CScheduleGroup* CScheduleManager::FindGroup(DWORD dwDefineID)
{
    for (std::vector<CScheduleGroup*>::iterator it = m_vecGroup.begin(); it != m_vecGroup.end();
         ++it)
    {
        CScheduleGroup* pGroup = *it;
        if (!pGroup)
            return NULL;
        if (pGroup->m_dwDefineID == dwDefineID)
            return pGroup;
    }
    return NULL;
}

void CScheduleManager::AddSchedule(const SCHEDULE_DATA* pData)
{
    CScheduleGroup* pGroup = FindGroup(pData->dwDefineID);
    if (!pGroup)
        return;
    pGroup->AddSchedule(pData);
    CDateTime now(_time32(NULL));
    if (m_bRunning != TRUE)
        return;
    sSchedulePeriod period;
    ZeroScheduleStruct(period);
    pGroup->GetSchedulePeriod(pData->dwScheduleID, now, &period);
    sScheduleEvent event;
    while (FindBeginEventOfGroup(&event, pGroup->m_dwDefineID) == TRUE)
    {
        ISchedule* pQueued = pGroup->FindActiveSchedule(event.dwScheduleID);
        if (!pQueued)
            break;
        RemoveBeginEvents(pQueued->m_Data.dwScheduleID);
        RemoveEndEvent(pQueued->m_Data.dwScheduleID);
    }
    if (pGroup->GetNearestPeriod(now, &period, FALSE) == TRUE)
    {
        pGroup->m_Period = period;
        ISchedule* pSchedule = pGroup->FindActiveSchedule(period.dwScheduleID);
        PushEvent(pGroup, period.tBegin, period.tEnd, 1, pSchedule);
        PushEvent(pGroup, period.tBegin, period.tEnd, 0, pSchedule);
    }
    m_listEvent.sort(ScheduleEventLessByTime);
}

void CScheduleManager::RemoveSchedule(const SCHEDULE_DATA* pData)
{
    CScheduleGroup* pGroup = FindGroup(pData->dwDefineID);
    if (!pGroup)
        return;
    _time32(NULL);
    if (m_bRunning == TRUE)
    {
        RemoveBeginEvents(pData->dwScheduleID);
        RemoveEndEvent(pData->dwScheduleID);
    }
    pGroup->RemoveSchedule(pData->dwScheduleID);
}

void CScheduleManager::GetPrevPeriods(PeriodList& lstPeriod, std::string strDefine, int nCount,
                                      CDateTime t)
{
    for (std::vector<CScheduleGroup*>::iterator it = m_vecGroup.begin(); it != m_vecGroup.end();
         ++it)
    {
        CScheduleGroup* pGroup = *it;
        if (!pGroup)
            continue;
        if (pGroup->GetDefineName() == strDefine)
        {
            pGroup->GetPrevPeriods(lstPeriod, nCount, t);
            return;
        }
    }
}

void CScheduleManager::GetNextPeriods(PeriodList& lstPeriod, std::string strDefine, int nCount,
                                      CDateTime t)
{
    for (std::vector<CScheduleGroup*>::iterator it = m_vecGroup.begin(); it != m_vecGroup.end();
         ++it)
    {
        CScheduleGroup* pGroup = *it;
        if (!pGroup)
            continue;
        if (pGroup->GetDefineName() == strDefine)
        {
            pGroup->GetNextPeriods(lstPeriod, nCount, t);
            return;
        }
    }
}

void CScheduleManager::PushEvent(CScheduleGroup* pGroup, DWORD dwBegin, DWORD dwEnd, BYTE bBegin,
                                 ISchedule* pSchedule)
{
    if (!pSchedule)
        return;
    sScheduleEvent event;
    ZeroScheduleStruct(event);
    event.dwScheduleID = pSchedule->m_Data.dwScheduleID;
    event.dwEnd = dwEnd;
    event.dwBegin = dwBegin;
    event.pGroup = pGroup;
    event.bBegin = bBegin;
    event.strName.assign(pSchedule->m_Data.szName, strlen(pSchedule->m_Data.szName));
    m_listEvent.push_back(event);
}

void CScheduleManager::RemoveBeginEvents(DWORD dwScheduleID)
{
    EventList::iterator it = m_listEvent.begin();
    while (it != m_listEvent.end())
    {
        if (it->dwScheduleID == dwScheduleID && it->bBegin == 1)
            it = m_listEvent.erase(it);
        else
            ++it;
    }
}

void CScheduleManager::RemoveEndEvent(DWORD dwScheduleID)
{
    for (EventList::reverse_iterator it = m_listEvent.rbegin(); it != m_listEvent.rend(); ++it)
    {
        if (it->dwScheduleID == dwScheduleID && it->bBegin == 0)
        {
            m_listEvent.erase(--it.base());
            return;
        }
    }
}

BOOL CScheduleManager::FindBeginEventOfGroup(sScheduleEvent* pEvent, DWORD dwDefineID)
{
    for (EventList::iterator it = m_listEvent.begin(); it != m_listEvent.end(); ++it)
    {
        if (it->pGroup->m_dwDefineID == dwDefineID && it->bBegin == 1)
        {
            *pEvent = *it;
            return TRUE;
        }
    }
    return FALSE;
}

} // namespace NewSchedule
