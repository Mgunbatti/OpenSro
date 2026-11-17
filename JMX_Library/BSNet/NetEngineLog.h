#pragma once

#include <windows.h>

typedef int (*PFN_NETENGINE_LOG)(DWORD dwFlags, const char* pszFormat, ...);

// The original reads this callback from the engine instance (CNetEngine +36); it lives here
// until CNetEngine is ported.
extern PFN_NETENGINE_LOG g_pfnNetEngineLog;

// Formats into 1024 bytes and passes the text to the callback as "%s". No-op without callback.
void NetEngineLog(DWORD dwFlags, const char* pszFormat, ...);
