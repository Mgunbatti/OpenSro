#pragma once

#include "BSLib/DB/Database.h"

// The database connections of GlobalManager (opened by CGlobalManager::OnLocalDataInitialized).

// 0x558C40: the division's DB (connection string of the certification data).
extern CDBSessionMgr* g_pDefaultDBSession;
// 0x558C3C: Server.cfg "AccountDBConfig", else the default DB.
extern CDBSessionMgr* g_pAccountDBSession;
// 0x558C38: Server.cfg "SilkDBConfig", else the account DB.
extern CDBSessionMgr* g_pSilkDBSession;
