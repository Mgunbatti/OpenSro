#include "ShardManager/GMLevelManager.h"

#include "BSLib/BSDebug.h"
#include "ServerFramework/CertData.h"

CGMLevelManager* g_pGMLevelManager = NULL;

static CGMLevelManager s_GMLevelManager;

namespace
{

struct GM_COMMAND_LEVEL
{
    WORD wCommand;
    BYTE byLevel;
};

// In the order of the original inserts (command 38 twice).
const GM_COMMAND_LEVEL s_aCommandLevel[] =
{
    { 1, 2 }, { 2, 1 }, { 3, 2 }, { 4, 2 }, { 5, 2 }, { 6, 2 }, { 7, 5 }, { 8, 1 },
    { 9, 2 }, { 10, 2 }, { 11, 3 }, { 12, 5 }, { 13, 2 }, { 14, 1 }, { 15, 1 }, { 16, 1 },
    { 17, 2 }, { 18, 2 }, { 20, 2 }, { 19, 1 }, { 32, 5 }, { 33, 5 }, { 21, 2 }, { 22, 2 },
    { 25, 2 }, { 26, 2 }, { 27, 5 }, { 29, 5 }, { 28, 5 }, { 30, 5 }, { 31, 5 }, { 48, 1 },
    { 50, 1 }, { 42, 2 }, { 43, 2 }, { 38, 1 }, { 55, 1 }, { 38, 1 },
};

}

CGMLevelManager::CGMLevelManager()
{
    if (g_pGMLevelManager)
        BSAssertFail();
    g_pGMLevelManager = this;
}

CGMLevelManager::~CGMLevelManager()
{
    g_pGMLevelManager = NULL;
}

void CGMLevelManager::Init()
{
    static const char* const s_apszGroup[6] =
    {
        "SR_GM_Lvl1", "SR_GM_Lvl2", "SR_GM_Lvl3", "SR_GM_Lvl4", "SR_GM_Lvl5", "SR_GM_Lvl6",
    };
    for (int i = 0; i < 6; ++i)
        m_adwIPGroup[i] = CertData_GetSettingValue(s_apszGroup[i]);
    for (int i = 0; i < _countof(s_aCommandLevel); ++i)
    {
        m_mapCommandLevel.insert(GMCommandLevelMap::value_type(s_aCommandLevel[i].wCommand,
                                                                s_aCommandLevel[i].byLevel));
    }
}

BYTE CGMLevelManager::FindGMLevelByIP(DWORD dwIP)
{
    for (int i = 5; i >= 0; --i)
    {
        if (PrivIPMap_ContainsIP((BYTE)m_adwIPGroup[i], dwIP))
            return (BYTE)(i + 1);
    }
    return 0;
}
