#include "ShardManager/Climate.h"

#include <stdlib.h>

#include "BSLib/BSDebug.h"
#include "BSNet/Msg.h"

DWORD g_dwClimateTick = 0;

// ---------------------------------------------------------------------------------------------
// CClimate

CClimate::CClimate()
{
    g_dwClimateTick = 0;
    m_pRef = NULL;
    m_dwChangeCycle = 0;
    m_dwNextChange = 0;
    m_dwLastStep = 0;
    m_dwStepInterval = 0;
    m_byWeather = 0;
    m_byAmount = 0;
    m_byTargetWeather = 0;
    m_byTargetAmount = 0;
    m_dwWeatherStart = 0;
    m_dwWeatherDuration = 0;
    m_byStep = 0;
    m_byStepFunc = CLIMATE_CLEAR;
    m_apfnStep[CLIMATE_NONE] = &CClimate::StepNone;
    m_apfnStep[CLIMATE_CLEAR] = &CClimate::StepTo<CLIMATE_CLEAR>;
    m_apfnStep[CLIMATE_RAIN] = &CClimate::StepTo<CLIMATE_RAIN>;
    m_apfnStep[CLIMATE_SNOW] = &CClimate::StepTo<CLIMATE_SNOW>;
}

void CClimate::Init(RefClimateData* pRef)
{
    m_pRef = pRef;
    BS_ASSERT(pRef->Division <= 60);
    BYTE byDivision = pRef->Division;
    if (byDivision < 1)
        byDivision = 1;
    else if (byDivision >= 60)
        byDivision = 60;
    pRef->Division = byDivision;

    m_dwChangeCycle = 12600000 / byDivision;
    m_dwNextChange = g_dwClimateTick + m_dwChangeCycle;
    m_byTargetWeather = pRef->InitialWeather;
    m_byWeather = pRef->InitialWeather;

    BS_ASSERT((DWORD)pRef->Duration > 30000);
    DWORD dwDuration = (DWORD)pRef->Duration;
    if (dwDuration < 30000)
        dwDuration = 30000;
    else if (dwDuration >= 1800000)
        dwDuration = 1800000;
    pRef->Duration = (int)dwDuration;

    m_dwWeatherStart = g_dwClimateTick;
    m_dwLastStep = g_dwClimateTick;
    m_dwStepInterval = 3000;
    if (pRef->InitialAmount)
    {
        m_byAmount = pRef->InitialAmount;
        m_byTargetAmount = pRef->InitialAmount;
    }
    else
    {
        BYTE byAmount = (BYTE)(abs(rand()) % 151 + 50);
        m_byAmount = byAmount;
        m_byTargetAmount = byAmount;
    }
}

// The amount per step: a tenth of the way to the target (through zero when the weather changes),
// at least 1 and at most 51.
void CClimate::SetStepToTarget()
{
    int nDistance;
    if (m_byWeather == m_byTargetWeather)
        nDistance = abs((int)m_byAmount - (int)m_byTargetAmount);
    else
        nDistance = (int)m_byAmount + (int)m_byTargetAmount;
    if (nDistance < 10)
        nDistance = 10;
    else if (nDistance >= 510)
        nDistance = 510;
    m_byStep = (BYTE)(nDistance / 10);
}

bool CClimate::Update()
{
    if (!m_pRef->ChangeWeather)
        return false;

    DWORD dwNow = g_dwClimateTick;
    if (dwNow > m_dwNextChange)
    {
        RollWeather();
        dwNow = g_dwClimateTick;
    }

    if (m_byWeather == m_byTargetWeather && m_byAmount == m_byTargetAmount)
    {
        if (m_dwWeatherStart > dwNow)
        {
            m_dwWeatherStart = 0;
            m_dwWeatherDuration = 0;
        }
        if (!m_dwWeatherDuration || dwNow - m_dwWeatherStart <= m_dwWeatherDuration)
            return false;

        // the rolled weather is over: back to the row's initial weather
        m_byTargetWeather = m_pRef->InitialWeather;
        m_byStepFunc = m_pRef->InitialWeather;
        m_byTargetAmount = m_pRef->InitialAmount;
        m_dwWeatherDuration = 0;
        SetStepToTarget();
    }
    return (this->*m_apfnStep[m_byStepFunc])();
}

bool CClimate::RollWeather()
{
    DWORD dwNow = g_dwClimateTick;
    m_dwNextChange = dwNow + m_dwChangeCycle;
    if (dwNow - m_dwWeatherStart < m_dwWeatherDuration)
        return false;
    m_dwWeatherStart = dwNow;

    BYTE byProbSnow = m_pRef->ProbSnow;
    BYTE byProbSnowOrRain = (BYTE)(m_pRef->ProbSnow + m_pRef->ProbRain);
    BS_ASSERT(byProbSnowOrRain <= 100);
    m_byTargetWeather = m_byWeather;
    m_byTargetAmount = m_byAmount;
    m_dwWeatherDuration = 0;

    BYTE byAmount;
    int nRoll = abs(rand()) % 100 + 1;
    if (nRoll <= byProbSnow)
    {
        m_byTargetWeather = CLIMATE_SNOW;
        m_byStepFunc = CLIMATE_SNOW;
        // BUG(SM-101): the sum wraps in a BYTE, so the clamp to 255 never applies.
        byAmount = (BYTE)(m_pRef->Snowfall + abs(rand()) % (m_pRef->SnowfallVariance + 1));
        m_byTargetAmount = byAmount;
        if (byAmount >= 0xFF)
            byAmount = 0xFF;
    }
    else if (nRoll <= byProbSnowOrRain)
    {
        m_byTargetWeather = CLIMATE_RAIN;
        m_byStepFunc = CLIMATE_RAIN;
        byAmount = (BYTE)(m_pRef->Rainfall + abs(rand()) % (m_pRef->RainfallVariance + 1));
        m_byTargetAmount = byAmount;
        if (byAmount >= 0xFF)
            byAmount = 0xFF;
    }
    else
    {
        m_byTargetWeather = CLIMATE_CLEAR;
        m_byStepFunc = CLIMATE_CLEAR;
        byAmount = (BYTE)(abs(rand()) % 51 - 106);         // 150..200
    }
    m_byTargetAmount = byAmount;
    m_dwWeatherDuration = (DWORD)m_pRef->Duration
                        + (DWORD)abs(rand()) % ((DWORD)m_pRef->DurationVariance + 1);
    SetStepToTarget();
    return true;
}

template <BYTE byWeather> bool CClimate::StepTo()
{
    if (m_byWeather == m_byTargetWeather && m_byAmount == m_byTargetAmount)
        return false;
    DWORD dwNow = g_dwClimateTick;
    if (dwNow - m_dwLastStep < m_dwStepInterval)
        return false;
    m_dwLastStep = dwNow;

    BYTE byAmount = m_byAmount;
    if (m_byWeather != byWeather)
    {
        // the other weather fades out first
        m_byAmount = byAmount < m_byStep ? 0 : (BYTE)(byAmount - m_byStep);
        if (!m_byAmount)
            m_byWeather = byWeather;
        return true;
    }

    BYTE byTarget = m_byTargetAmount;
    if (byTarget >= byAmount)
    {
        if ((int)byAmount + (int)m_byStep < (int)byTarget)
        {
            m_byAmount = (BYTE)(byAmount + m_byStep);
            return true;
        }
    }
    else if ((int)byAmount - (int)m_byStep > (int)byTarget)
    {
        m_byAmount = (BYTE)(byAmount - m_byStep);
        return true;
    }
    m_byAmount = byTarget;
    return true;
}

void CClimate::WriteToMsg(CMsg* pMsg) const
{
    DWORD dwID = (DWORD)m_pRef->ID;
    pMsg->WriteBytes(&dwID, 4);
    pMsg->WriteBytes(&m_byWeather, 2);                  // weather and amount
}

// ---------------------------------------------------------------------------------------------
// CClimateManager

CClimateManager::CClimateManager()
{
    g_dwClimateTick = 0;
}

CClimateManager::~CClimateManager()
{
    Clear();
}

void CClimateManager::AddClimate(RefClimateData* pRef)
{
    CClimate* pClimate = new CClimate;
    pClimate->Init(pRef);
    DWORD dwID = (DWORD)pRef->ID;
    if (m_mapClimate.find(dwID) != m_mapClimate.end())
    {
        // BUG(SM-100): pClimate is not deleted.
        BSAssertFail();
        return;
    }
    m_mapClimate.insert(std::make_pair(dwID, pClimate));
}

void CClimateManager::Clear()
{
    for (std::map<DWORD, CClimate*>::iterator it = m_mapClimate.begin();
         it != m_mapClimate.end(); ++it)
    {
        if (it->second)
            delete it->second;
    }
    m_mapClimate.clear();
}

int CClimateManager::UpdateAll(std::vector<CClimate*>& vecChanged, bool bForce)
{
    if (vecChanged.size())
        BSAssertFail();
    g_dwClimateTick = GetTickCount();
    for (std::map<DWORD, CClimate*>::iterator it = m_mapClimate.begin();
         it != m_mapClimate.end(); ++it)
    {
        CClimate* pClimate = it->second;
        if (pClimate->Update() == true || bForce == true)
            vecChanged.push_back(pClimate);
    }
    return (int)vecChanged.size();
}
