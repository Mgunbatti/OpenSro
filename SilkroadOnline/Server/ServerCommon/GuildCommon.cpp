#include "ServerCommon/GuildCommon.h"

// ---------------------------------------------------------------------------------------------
// CGuildCommon

GUILD_HOSTILE_ENTRY* CGuildCommon::FindHostileEntry(DWORD dwGuildID)
{
    GuildHostileMap::iterator it = m_mapHostile.find(dwGuildID);
    if (it == m_mapHostile.end())
        return NULL;
    return &it->second;
}

// ---------------------------------------------------------------------------------------------
// CGuildMgrCommon

CGuildCommon* CGuildMgrCommon::FindGuild(DWORD dwGuildID)
{
    if (!dwGuildID)
        return NULL;
    GuildMap::iterator it = m_mapGuild.find(dwGuildID);
    if (it == m_mapGuild.end())
        return NULL;
    return it->second;
}
