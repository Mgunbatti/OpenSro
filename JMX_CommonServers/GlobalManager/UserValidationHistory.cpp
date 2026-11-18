#include "UserValidationHistory.h"

#include "ServiceManagerProcess.h"
#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerDataExtern.h"

CChunkAllocatorST<UVFailureHistory> g_UVFailureHistoryPool;
CUserValidationHistoryMgr* g_pUserValidationHistoryMgr = NULL;
CUserValidationHistoryMgr g_UserValidationHistoryMgr;

void __stdcall UVFailureHistory::Release()
{
    if (bInUse)
    {
        bInUse = 0;
        g_UVFailureHistoryPool.Free(this);
    }
}

UVFailureHistory* UVFailureHistoryPool_Alloc()
{
    UVFailureHistory* p = g_UVFailureHistoryPool.Alloc();
    p->bInUse = 1;
    return p;
}

CUserValidationHistoryMgrSingleton::CUserValidationHistoryMgrSingleton()
{
    if (g_pUserValidationHistoryMgr)
        BSAssertFail();
    g_pUserValidationHistoryMgr = (CUserValidationHistoryMgr*)this;
}

CUserValidationHistoryMgr::CUserValidationHistoryMgr()
{
    m_strPunisher = "AutomatedPunisher";
    for (int i = 0; i < UV_CHECK_COUNT; ++i)
        SetConfig(i, "PunishmentGuide", "PunishmentDesc", 3, 10);
    m_dtLastCheck = CDateTime::GetCurrent();
}

CUserValidationHistoryMgr::~CUserValidationHistoryMgr()
{
    ClearAll();
    g_pUserValidationHistoryMgr = NULL;
}

bool CUserValidationHistoryMgr::SetPunisher(CServiceManagerProcess* pSM, const char* pszName)
{
    if (!pSM)
        BSAssertFail();
    m_pSM = pSM;
    m_strPunisher = pszName;
    return true;
}

void CUserValidationHistoryMgr::SetConfig(int nCheck, const char* pszGuide,
                                          const char* pszDescription, DWORD dwTolerance,
                                          DWORD dwBlockTimeMin)
{
    UV_PUNISHMENT_CONFIG& config = m_aConfig[nCheck];
    config.dwTolerance = dwTolerance;
    config.dwBlockTimeMin = dwBlockTimeMin;
    config.strGuide = pszGuide;
    config.strDescription = pszDescription;
}

bool CUserValidationHistoryMgr::Punish(int nCheck, std::string* pstrUserID)
{
    if (!m_pSM)
        BSAssertFail();
    UV_PUNISHMENT_CONFIG& config = m_aConfig[nCheck];
    return m_pSM->RegisterAutomatedPunishment(pstrUserID, &m_strPunisher, &config.strGuide,
                                              &config.strDescription, config.dwBlockTimeMin);
}

bool CUserValidationHistoryMgr::RecordFailure(const std::string* pstrUserID, int nCheck,
                                              DWORD* pdwCount)
{
    UVFailureHistoryMap& map = m_aMap[nCheck];
    UVFailureHistoryMap::iterator it = map.find(*pstrUserID);
    if (it == map.end())
    {
        UVFailureHistory* pNew = UVFailureHistoryPool_Alloc();
        if (!pNew)
            BSAssertFail();
        pNew->strUserID = *pstrUserID;
        pNew->dwCount = 1;
        map.insert(UVFailureHistoryMap::value_type(*pstrUserID, pNew));
        *pdwCount = pNew->dwCount;
        return false;
    }
    UVFailureHistory* pHistory = it->second;
    if (!pHistory)
        BSAssertFail();
    ++pHistory->dwCount;
    if (pHistory->dwCount < m_aConfig[nCheck].dwTolerance)
    {
        *pdwCount = pHistory->dwCount;
        return false;
    }
    Punish(nCheck, &pHistory->strUserID);
    map.erase(it);
    pHistory->Release();
    // Read after the release (the pool keeps the object's memory).
    *pdwCount = pHistory->dwCount;
    return true;
}

void CUserValidationHistoryMgr::Clear(const std::string* pstrUserID)
{
    for (int i = 0; i < UV_CHECK_COUNT; ++i)
    {
        UVFailureHistoryMap& map = m_aMap[i];
        UVFailureHistoryMap::iterator it = map.find(*pstrUserID);
        if (it == map.end())
            continue;
        UVFailureHistory* pHistory = it->second;
        if (!pHistory)
        {
            LogPrintf(0x1000000, "Invalid UVFailureHistory Pointer!");
            continue;
        }
        map.erase(it);
        pHistory->Release();
    }
}

void CUserValidationHistoryMgr::CheckDay()
{
    CDateTime dtNow = CDateTime::GetCurrent();
    if (m_dtLastCheck.GetDayOfWeek() != dtNow.GetDayOfWeek())
    {
        ClearAll();
        LogPrintf(0, "UserValidationHistoryMgr processed Expire history!!");
    }
    m_dtLastCheck = dtNow;
}

void CUserValidationHistoryMgr::ClearAll()
{
    for (int i = 0; i < UV_CHECK_COUNT; ++i)
    {
        UVFailureHistoryMap& map = m_aMap[i];
        for (UVFailureHistoryMap::iterator it = map.begin(); it != map.end(); ++it)
            it->second->Release();
        map.clear();
    }
}
