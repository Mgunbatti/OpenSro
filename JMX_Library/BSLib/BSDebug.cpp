#include "BSDebug.h"

DWORD g_dwBSDebugFlags[2] = { 0x112, 0x12A };

BOOL BSAssertFail()
{
    BOOL noDebugger = !IsDebuggerPresent();
    if (g_dwBSDebugFlags[noDebugger] & BSDBG_THROW_ON_ASSERT)
    {
        const char* reason = "oops";
        throw reason;
    }
    return noDebugger;
}

void BSAssertFailAndThrow()
{
    BSAssertFail();
    throw 0;
}
