#pragma once

#include <windows.h>

typedef int (*PFN_NETENGINE_LOG)(DWORD dwFlags, const char* pszFormat, ...);

// NetEngine_Log (0x481ce0): formats into 1024 bytes and passes the text as "%s" to the callback
// of the engine instance (BSNET_INIT_PARAM::pfnLog, CNetEngine +0x24). Nothing without a
// callback; g_pNetEngine must be set (NET-005). Faults while formatting or in the callback are
// caught and ignored (__try/__except).
void NetEngineLog(DWORD dwFlags, const char* pszFormat, ...);
