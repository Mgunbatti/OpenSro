#pragma once

#include <windows.h>

#include <map>
#include <vector>

#include "ServerCommon/RefRecords.h"

class CMsg;

// The weather of the regions (_RefClimate). Every CClimate walks from its current weather and
// amount (rain / snow fall, or the clear sky value) toward a target that is rolled again once per
// change cycle; CClimateManager updates them all and hands the changed ones to the broadcast of
// opcode 0x3809.

// g_ClimateTick (0x858864): GetTickCount of the last CClimateManager::UpdateAll. Reset to 0 by
// the constructors of CClimate and CClimateManager.
extern DWORD g_dwClimateTick;

enum
{
    CLIMATE_NONE  = 0,
    CLIMATE_CLEAR = 1,
    CLIMATE_RAIN  = 2,
    CLIMATE_SNOW  = 3
};

class CClimate
{
public:
    typedef bool (CClimate::*PFN_STEP)();

    CClimate();                                         // 0x656a50
    virtual ~CClimate() {}

    // CClimate::Init (0x656ad0): binds the row and clamps its Division (changes per day, 1..60)
    // and Duration (30000..1800000 ms) in the row itself.
    void Init(RefClimateData* pRef);
    // CClimate::Update (0x656ba0): rolls a new target when the change cycle is over, falls back
    // to the row's initial weather when the rolled weather has lasted its duration, then steps.
    // true when the weather or amount changed.
    bool Update();
    // CClimate::RollWeather (0x656c80).
    bool RollWeather();
    // CClimate::StepToClear / StepToRain / StepToSnow (0x656df0 / 0x656f30 / 0x656e90).
    template <BYTE byWeather> bool StepTo();
    // ReturnConst_0_2: slot 0 of the step table.
    bool StepNone() { return false; }
    // CClimate::WriteToMsg (0x656fe0): the row ID, then the weather and amount bytes.
    void WriteToMsg(CMsg* pMsg) const;

    RefClimateData*     m_pRef;             // +4
    DWORD               m_dwChangeCycle;    // +8   12600000 / Division
    DWORD               m_dwNextChange;     // +C
    DWORD               m_dwLastStep;       // +10
    DWORD               m_dwStepInterval;   // +14  3000
    BYTE                m_byWeather;        // +18
    BYTE                m_byAmount;         // +19
    BYTE                m_byTargetWeather;  // +1A
    BYTE                m_byTargetAmount;   // +1B
    DWORD               m_dwWeatherStart;   // +1C
    DWORD               m_dwWeatherDuration;// +20  0: the weather stays
    BYTE                m_byStep;           // +24  amount per step
    BYTE                m_byStepFunc;       // +25  index into m_apfnStep
    PFN_STEP            m_apfnStep[4];      // +28

private:
    void SetStepToTarget();
};

C_ASSERT(sizeof(CClimate::PFN_STEP) == 4);
C_ASSERT(FIELD_OFFSET(CClimate, m_byWeather) == 0x18);
C_ASSERT(FIELD_OFFSET(CClimate, m_apfnStep) == 0x28);
C_ASSERT(sizeof(CClimate) == 0x38);

class CClimateManager
{
public:
    CClimateManager();                                  // 0x657020
    virtual ~CClimateManager();                         // 0x6570f0

    // CClimateManager::AddClimate (0x657160): a CClimate for the row, keyed by the row ID.
    // BUG(SM-100): a second row with the same ID asserts and leaks the new CClimate.
    void AddClimate(RefClimateData* pRef);
    // CClimateManager::Clear (0x657220): deletes every CClimate and empties the map.
    void Clear();
    // CClimateManager::UpdateAll (0x6572d0): sets g_ClimateTick, updates every CClimate and
    // appends the changed ones (all of them with bForce) to vecChanged; returns its size.
    int UpdateAll(std::vector<CClimate*>& vecChanged, bool bForce);

    std::map<DWORD, CClimate*>  m_mapClimate;           // +4
};
