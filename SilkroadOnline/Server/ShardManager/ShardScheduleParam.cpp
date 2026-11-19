#include "ShardManager/ShardScheduleJob.h"

#include <stdlib.h>

BOOL ScheduleParam_BuildOpenTimeList(const std::string& strParam, sTempleOpenSchedule* pOpen,
                                     DWORD dwBegin)
{
    ScheduleParamList params;
    if (ScheduleParamParser_Parse(strParam, &params) != 1)
        return FALSE;

    BOOL bFound = FALSE;
    for (size_t i = 0; i < params.size(); ++i)
    {
        if (params.at(i).first == "WORLD_OPEN_TIME")
        {
            DWORD dwTime = dwBegin + 60 * atol(params.at(i).second.c_str());
            pOpen->lstTime.push_back(dwTime);
            pOpen->dwOpenTime = dwTime;
            bFound = TRUE;
        }
    }
    if (!bFound)
        return FALSE;

    for (size_t i = 0; i < params.size(); ++i)
    {
        if (params.at(i).first == "NOTIFY_BEFORE_OPEN")
            pOpen->lstTime.push_back(pOpen->dwOpenTime - 60 * atol(params.at(i).second.c_str()));
    }
    pOpen->lstTime.sort();
    return TRUE;
}

BOOL ScheduleParam_GetInt(const std::string& strParam, std::string strKey, int* pnValue)
{
    ScheduleParamList params;
    if (ScheduleParamParser_Parse(strParam, &params) == 1)
    {
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (params[i].first == strKey)
            {
                *pnValue = atol(params.at(i).second.c_str());
                return TRUE;
            }
        }
    }
    return FALSE;
}
