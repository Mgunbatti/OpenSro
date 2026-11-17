#include "BSLib/BSDebug.h"

#include <stdio.h>
#include <string.h>

static int g_nFailed = 0;

#define CHECK(expr)                                                         \
    do                                                                      \
    {                                                                       \
        if (!(expr))                                                        \
        {                                                                   \
            printf("FAILED line %d: %s\n", __LINE__, #expr);                \
            ++g_nFailed;                                                    \
        }                                                                   \
    } while (0)

static int CountDumps(bool bDelete)
{
    char szModule[MAX_PATH];
    GetModuleFileNameA(NULL, szModule, MAX_PATH);
    char* p = strrchr(szModule, '\\');
    p[1] = 0;
    char szPattern[MAX_PATH];
    sprintf_s(szPattern, "%sDebugTest_*.dmp", szModule);

    int n = 0;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(szPattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        ++n;
        if (bDelete)
        {
            char szFile[MAX_PATH];
            sprintf_s(szFile, "%s%s", szModule, fd.cFileName);
            DeleteFileA(szFile);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

int main()
{
    CountDumps(true);

    // default flags: no minidump, nothing thrown
    bool bThrown = false;
    try
    {
        BSAssertFail();
    }
    catch (...)
    {
        bThrown = true;
    }
    CHECK(!bThrown);
    CHECK(CountDumps(false) == 0);

    // minidump flag: the "oops" exception stays inside BSAssertFail
    BSDebugSetFlags(BSDBG_MINIDUMP, BSDBG_MINIDUMP);
    bThrown = false;
    try
    {
        BSAssertFail();
    }
    catch (...)
    {
        bThrown = true;
    }
    CHECK(!bThrown);
    CHECK(CountDumps(true) >= 1);

    // an expression longer than 1023 characters makes the dialog give up before showing anything
    BSDebugSetFlags(BSDBG_DIALOG_CONTINUE, BSDBG_DIALOG_CONTINUE);
    static char szLong[2000];
    memset(szLong, 'x', sizeof(szLong) - 1);
    CHECK(BSAssertDialog(1, "file", "%s", szLong) == false);
    CHECK(g_bBSDebugAsserted);

    // the dialog collects either the call stack or a minidump, never both
    BSDebugSetFlags(BSDBG_CALLSTACK | BSDBG_MINIDUMP | BSDBG_DIALOG_CONTINUE,
                    BSDBG_CALLSTACK | BSDBG_MINIDUMP | BSDBG_DIALOG_CONTINUE);
    CHECK(BSAssertDialog(1, "file", "%s", szLong) == false);
    CHECK(CountDumps(true) == 0);
    BSDebugSetFlags(BSDBG_MINIDUMP | BSDBG_DIALOG_CONTINUE, BSDBG_MINIDUMP | BSDBG_DIALOG_CONTINUE);
    CHECK(BSAssertDialog(1, "file", "%s", szLong) == false);
    CHECK(CountDumps(true) == 1);

    BSDebugSetFlags(0x112, 0x12A);
    printf(g_nFailed ? "DebugTest: %d failed\n" : "DebugTest: all passed\n", g_nFailed);
    return g_nFailed ? 1 : 0;
}
