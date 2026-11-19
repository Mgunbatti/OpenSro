// SR_ShardManager climate (ShardManager\Climate.h): CClimate and CClimateManager. Compiled with
// SHARD_DEFS.

#include "ShardManager/Climate.h"

#include <stdlib.h>
#include <string.h>

#include "BSNet/Msg.h"

#include "TestCheck.h"

static RefClimateData MakeRow(int nID)
{
    RefClimateData r;
    r.ID = nID;
    r.InitialWeather = CLIMATE_CLEAR;
    r.InitialAmount = 100;
    r.ChangeWeather = 1;
    r.Division = 4;
    r.Duration = 60000;
    r.DurationVariance = 0;
    r.Snowfall = 30;
    r.SnowfallVariance = 0;
    r.ProbSnow = 0;
    r.Rainfall = 40;
    r.RainfallVariance = 0;
    r.ProbRain = 0;
    return r;
}

static void TestInit()
{
    RefClimateData r = MakeRow(7);
    r.Division = 0;
    r.Duration = 5000;
    g_dwClimateTick = 1000;
    CClimate c;
    CHECK(g_dwClimateTick == 0);            // the constructor resets the tick
    g_dwClimateTick = 1000;
    c.Init(&r);
    CHECK(r.Division == 1 && c.m_dwChangeCycle == 12600000);
    CHECK(c.m_dwNextChange == 1000 + 12600000);
    CHECK(r.Duration == 30000);             // clamped in the row
    CHECK(c.m_byWeather == CLIMATE_CLEAR && c.m_byTargetWeather == CLIMATE_CLEAR);
    CHECK(c.m_byAmount == 100 && c.m_byTargetAmount == 100);
    CHECK(c.m_dwStepInterval == 3000 && c.m_dwLastStep == 1000 && c.m_dwWeatherStart == 1000);
    CHECK(c.m_byStepFunc == CLIMATE_CLEAR);

    RefClimateData r2 = MakeRow(8);
    r2.Division = 100;
    r2.Duration = 5000000;
    r2.InitialAmount = 0;
    CClimate c2;
    c2.Init(&r2);
    CHECK(r2.Division == 60 && c2.m_dwChangeCycle == 210000 && r2.Duration == 1800000);
    CHECK(c2.m_byAmount >= 50 && c2.m_byAmount <= 200 && c2.m_byAmount == c2.m_byTargetAmount);
}

static void TestRollAndStep()
{
    RefClimateData r = MakeRow(9);
    r.ProbSnow = 100;
    CClimate c;
    g_dwClimateTick = 0;
    c.Init(&r);

    // a snow roll from clear 100: the clear sky fades out first in steps of (100 + 30) / 10
    g_dwClimateTick = 12600000 / 4 + 1;
    CHECK(c.Update());
    CHECK(c.m_byTargetWeather == CLIMATE_SNOW && c.m_byTargetAmount == 30);
    CHECK(c.m_dwWeatherDuration == 60000 && c.m_byStep == 13);
    CHECK(c.m_byWeather == CLIMATE_CLEAR && c.m_byAmount == 87);

    // within the step interval nothing changes
    CHECK(!c.Update());
    CHECK(c.m_byAmount == 87);

    int nSteps = 0;
    while (c.m_byWeather == CLIMATE_CLEAR && nSteps < 20)
    {
        g_dwClimateTick += 3000;
        CHECK(c.Update());
        ++nSteps;
    }
    CHECK(c.m_byWeather == CLIMATE_SNOW && c.m_byAmount == 0 && nSteps == 7);
    g_dwClimateTick += 3000;
    CHECK(c.Update() && c.m_byAmount == 13);
    g_dwClimateTick += 3000;
    CHECK(c.Update() && c.m_byAmount == 26);
    g_dwClimateTick += 3000;
    CHECK(c.Update() && c.m_byAmount == 30);
    g_dwClimateTick += 3000;
    CHECK(!c.Update());

    // after the duration it walks back to the initial weather
    g_dwClimateTick = c.m_dwWeatherStart + 60001;
    CHECK(c.Update());
    CHECK(c.m_byTargetWeather == CLIMATE_CLEAR && c.m_byTargetAmount == 100);
    CHECK(c.m_byStepFunc == CLIMATE_CLEAR && c.m_dwWeatherDuration == 0 && c.m_byStep == 13);
    CHECK(c.m_byWeather == CLIMATE_SNOW && c.m_byAmount == 17);

    // a row without ChangeWeather never changes
    r.ChangeWeather = 0;
    g_dwClimateTick += 100000000;
    CHECK(!c.Update());
}

static void TestAmountWraps()
{
    // BUG(SM-101): 250 + 10 wraps to 4 instead of being held at 255
    RefClimateData r = MakeRow(10);
    r.ProbSnow = 100;
    r.Snowfall = 250;
    r.SnowfallVariance = 0;
    CClimate c;
    g_dwClimateTick = 0;
    c.Init(&r);
    r.Snowfall = 255;
    r.SnowfallVariance = 0;
    g_dwClimateTick = c.m_dwNextChange + 1;
    CHECK(c.RollWeather());
    CHECK(c.m_byTargetAmount == 255);
    r.Snowfall = 250;
    r.SnowfallVariance = 9;
    srand(1);
    bool bWrapped = false;
    for (int i = 0; i < 50 && !bWrapped; ++i)
    {
        c.m_dwWeatherDuration = 0;
        c.RollWeather();
        bWrapped = c.m_byTargetAmount < 250;
    }
    CHECK(bWrapped);

    // clear sky: 150..200
    r.ProbSnow = 0;
    c.m_dwWeatherDuration = 0;
    CHECK(c.RollWeather());
    CHECK(c.m_byTargetWeather == CLIMATE_CLEAR && c.m_byTargetAmount >= 150 &&
          c.m_byTargetAmount <= 200);
}

static void TestManager()
{
    RefClimateData r1 = MakeRow(1);
    RefClimateData r2 = MakeRow(2);
    RefClimateData r1b = MakeRow(1);
    r1b.Division = 0;
    r1.ChangeWeather = 0;                   // no rolls while the manager is tested
    r2.ChangeWeather = 0;
    CClimateManager mgr;
    mgr.AddClimate(&r1);
    mgr.AddClimate(&r2);
    mgr.AddClimate(&r1b);                   // asserts; the row is still clamped
    CHECK(mgr.m_mapClimate.size() == 2 && r1b.Division == 1);
    CHECK(mgr.m_mapClimate[1]->m_pRef == &r1);

    std::vector<CClimate*> vec;
    CHECK(mgr.UpdateAll(vec, false) == 0);
    CHECK(g_dwClimateTick != 0);
    CHECK(mgr.UpdateAll(vec, true) == 2);   // not cleared by UpdateAll (asserts)
    vec.clear();
    CHECK(mgr.UpdateAll(vec, true) == 2 && vec[0]->m_pRef == &r1 && vec[1]->m_pRef == &r2);

    CMsg msg;
    msg.Reset();
    vec[1]->m_byWeather = CLIMATE_RAIN;
    vec[1]->m_byAmount = 77;
    vec[1]->WriteToMsg(&msg);
    DWORD dwID = 0;
    BYTE byWeather = 0, byAmount = 0;
    msg >> dwID >> byWeather >> byAmount;
    CHECK(dwID == 2 && byWeather == CLIMATE_RAIN && byAmount == 77 && msg.GetUnreadSize() == 0);

    mgr.Clear();
    CHECK(mgr.m_mapClimate.empty());
}

int main()
{
    TestInit();
    TestRollAndStep();
    TestAmountWraps();
    TestManager();
    return TEST_RESULT("ShardClimateTest");
}
