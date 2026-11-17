#pragma once

#include <windows.h>

// BSLib debug module: assert handling, stack traces and minidumps through dbghelp.dll.
//
// Behaviour is driven by a flag word chosen with IsDebuggerPresent(): index 0 with a debugger
// attached, 1 without. Release defaults of the original binaries are 0x112 and 0x12A.
enum
{
    BSDBG_DIALOG_CONTINUE   = 0x002,    // assert dialog offers OK (continue) / Cancel (exit)
    BSDBG_DIALOG_EXIT       = 0x004,    // assert dialog offers OK (exit)
    BSDBG_CALLSTACK         = 0x008,    // add a call stack to the assert dialog
    BSDBG_BREAK             = 0x010,    // assert dialog tells the caller to break into the debugger
    BSDBG_MINIDUMP          = 0x200,    // asserts write a minidump next to the exe
    BSDBG_CALLBACK          = 0x400,    // asserts are passed to the registered callback
};

typedef void (*PFN_BSASSERT_CALLBACK)(int nLine, const char* pszFile, const char* pszExpr);

extern DWORD g_dwBSDebugFlags[2];
extern BOOL  g_bBSDebugAsserted;            // set once any assert dialog ran
extern char  g_szBSDebugError[4096];        // why dbghelp.dll could not be used

void BSDebugSetFlags(DWORD dwWithDebugger, DWORD dwWithoutDebugger);
void BSDebugSetAssertCallback(PFN_BSASSERT_CALLBACK pfn);

// Release-build invariant check. Writes a minidump when BSDBG_MINIDUMP is set, otherwise does
// nothing. Never throws.
BOOL BSAssertFail();

// BSAssertFail followed by throw (int)0.
__declspec(noreturn) void BSAssertFailAndThrow();

// Assert dialog. Returns FALSE when the caller should break into the debugger.
bool BSAssertDialog(int nLine, const char* pszFile, const char* pszFormat, ...);

bool BSDebugLoadDbgHelp();
LONG BSDebugWriteMiniDump(EXCEPTION_POINTERS* pExceptionInfo);

#define BS_ASSERT(expr) ((expr) ? (void)0 : (void)BSAssertFail())

#define BS_ASSERT_DIALOG(expr)                                              \
    do                                                                      \
    {                                                                       \
        if (!(expr) && !BSAssertDialog(__LINE__, __FILE__, #expr))          \
            __debugbreak();                                                 \
    } while (0)

// Deliberate access violation the original code uses for hard bounds failures.
#define BS_CRASH() (*(volatile int*)0 = 2000)
