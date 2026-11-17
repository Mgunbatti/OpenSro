#include "NetEngineLog.h"

#include <stdarg.h>
#include <strsafe.h>

PFN_NETENGINE_LOG g_pfnNetEngineLog = NULL;

void NetEngineLog(DWORD dwFlags, const char* pszFormat, ...)
{
    if (!g_pfnNetEngineLog)
        return;

    char szText[1024];
    va_list args;
    va_start(args, pszFormat);
    StringCchVPrintfA(szText, 0x400, pszFormat, args);
    va_end(args);
    g_pfnNetEngineLog(dwFlags, "%s", szText);
}
