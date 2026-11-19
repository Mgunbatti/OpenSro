#pragma once

#include <windows.h>

#include <map>

// Training camp (academy) manager base. Only the camp map and FindCamp are here (SM_social1);
// the rest of the class belongs to the training camp group.

class CTrainingCampCommon;

typedef std::map<DWORD, CTrainingCampCommon*> TrainingCampMap;

class CTrainingCampMgrCommon
{
public:
    virtual ~CTrainingCampMgrCommon() {}                                    // 0

    // CTrainingCampMgrCommon::FindCamp (0x40be70): NULL for ID 0 or an unknown camp.
    CTrainingCampCommon* FindCamp(DWORD dwCampID)
    {
        if (!dwCampID)
            return NULL;
        TrainingCampMap::iterator it = m_mapCamp.find(dwCampID);
        if (it == m_mapCamp.end())
            return NULL;
        return it->second;
    }

public:
    TrainingCampMap     m_mapCamp;      // +04
};

C_ASSERT(FIELD_OFFSET(CTrainingCampMgrCommon, m_mapCamp) == 0x04);
