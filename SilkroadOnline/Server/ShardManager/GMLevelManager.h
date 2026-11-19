#pragma once

#include <windows.h>

#include <map>

// GM levels of SR_ShardManager (static object 0x89bd14, g_pGMLevelManager 0x8a894c): the
// certification IP groups "SR_GM_Lvl1".."SR_GM_Lvl6" and the level each GM command needs.

typedef std::map<WORD, BYTE> GMCommandLevelMap;

class CGMLevelManager
{
public:
    // CGMLevelManager::StaticInit (0x61b880) / StaticDestroy (0x61b8f0)
    CGMLevelManager();
    ~CGMLevelManager();

    // 0x61b9a0 (CShardManager::vf11): the IP groups and the command table.
    void Init();
    // 0x61be30: the highest level whose IP group holds dwIP (6..1), 0 for none.
    BYTE FindGMLevelByIP(DWORD dwIP);
    // CGMLevelManager_HasGMLevelForIP (0x61be20)
    BOOL HasGMLevelForIP(DWORD dwIP)
    {
        return FindGMLevelByIP(dwIP) != 0;
    }

public:
    DWORD               m_adwIPGroup[6];    // +00 SR_GM_Lvl1..6
    GMCommandLevelMap   m_mapCommandLevel;  // +18 GM command ID -> level
};

C_ASSERT(FIELD_OFFSET(CGMLevelManager, m_mapCommandLevel) == 0x18);

extern CGMLevelManager* g_pGMLevelManager;
