#include "NetEngineLog.h"

#include "BSNet/NetEngine.h"

#include <stdarg.h>
#include <strsafe.h>

void NetEngineLog(DWORD dwFlags, const char* pszFormat, ...)
{
    // BUG(NET-005): no check for a missing engine; logging before the engine exists or after
    // Shutdown reads address 0x24.
    PFN_NETENGINE_LOG pfnLog = NetEngine_Cast(g_pNetEngine)->m_Config.pfnLog;
    if (!pfnLog)
        return;

    char szText[1024];
    va_list args;
    va_start(args, pszFormat);
    StringCchVPrintfA(szText, 0x400, pszFormat, args);
    va_end(args);
    pfnLog(dwFlags, "%s", szText);
}
