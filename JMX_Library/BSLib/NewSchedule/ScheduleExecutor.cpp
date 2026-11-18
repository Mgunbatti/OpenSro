#include "BSLib/NewSchedule/ScheduleExecutor.h"

#include <time.h>

namespace NewSchedule
{

typedef std::map<DWORD, IScheduleJob*> JobMap;

// SR_ShardManager 0x6c7530
static void PushJob(ScheduleJobList* pList, IScheduleJob* pJob)
{
    if (pJob)
        pList->push_back(pJob);
}

// SR_ShardManager 0x6c7570
static IScheduleJob* FindJobInList(ScheduleJobList* pList, const std::string& strDefine)
{
    for (ScheduleJobList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        IScheduleJob* pJob = *it;
        if (pJob && pJob->m_strDefine == strDefine)
            return pJob;
    }
    return NULL;
}

// ---------------------------------------------------------------------------------------------
// IScheduleJob

void IScheduleJob::SetDefineName(std::string strDefine)
{
    m_strDefine = strDefine;
}

BOOL IScheduleJob::SetScheduleInfo(const sScheduleInfo& info)
{
    m_Info = info;
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// CScheduleExecutor

CScheduleExecutor::~CScheduleExecutor()
{
    DeleteAllJobs();
}

BOOL CScheduleExecutor::CreateJobList()
{
    m_pJobList = new ScheduleJobList;
    return m_pJobList != NULL;
}

void CScheduleExecutor::DeleteAllJobs()
{
    for (JobMap::iterator it = m_mapJob.begin(); it != m_mapJob.end(); ++it)
    {
        IScheduleJob* pJob = it->second;
        if (pJob)
            delete pJob;
    }
    m_mapJob.clear();
    if (m_pJobList)
        delete m_pJobList;
}

void CScheduleExecutor::TickAllJobs()
{
    CDateTime now(_time32(NULL));
    for (JobMap::iterator it = m_mapJob.begin(); it != m_mapJob.end(); ++it)
    {
        IScheduleJob* pJob = it->second;
        if (pJob)
            pJob->OnTick(now);
    }
}

IScheduleJob* CScheduleExecutor::FindJob(DWORD dwScheduleID)
{
    JobMap::iterator it = m_mapJob.find(dwScheduleID);
    if (it == m_mapJob.end())
        return NULL;
    return it->second;
}

IScheduleJob* CScheduleExecutor::FindJobByName(const std::string& strDefine)
{
    for (JobMap::iterator it = m_mapJob.begin(); it != m_mapJob.end(); ++it)
    {
        IScheduleJob* pJob = it->second;
        if (pJob && pJob->m_strDefine == strDefine)
            return pJob;
    }
    return NULL;
}

BOOL CScheduleExecutor::AddJob(IScheduleJob* pJob, const sScheduleInfo& info)
{
    if (pJob->CanBegin() != TRUE)
        return FALSE;
    pJob->m_Info = info;
    if (pJob->OnBegin() != TRUE)
        return FALSE;
    m_mapJob.insert(JobMap::value_type(pJob->m_Info.dwScheduleID, pJob));
    return TRUE;
}

void CScheduleExecutor::EndJob(DWORD dwScheduleID, const sScheduleInfo& info)
{
    JobMap::iterator it = m_mapJob.find(dwScheduleID);
    CDateTime now(_time32(NULL));
    if (it == m_mapJob.end())
        return;
    IScheduleJob* pJob = it->second;
    if (!pJob)
        return;
    pJob->m_Info = info;
    pJob->OnEnd(now);
    m_mapJob.erase(it);
}

BOOL CScheduleExecutor::RegisterJob(IScheduleJob* pJob)
{
    if (!pJob)
        return FALSE;
    PushJob(m_pJobList, pJob);
    return TRUE;
}

IScheduleJob* CScheduleExecutor::FindRegisteredJob(const std::string& strDefine)
{
    if (!m_pJobList)
        return NULL;
    return FindJobInList(m_pJobList, strDefine);
}

} // namespace NewSchedule
