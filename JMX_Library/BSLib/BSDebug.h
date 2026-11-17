#pragma once

#include <windows.h>

// Behaviour flags selected by IsDebuggerPresent(): index 0 with a debugger attached, 1 without.
// Release defaults of the original binaries are 0x112 and 0x12A.
enum
{
    BSDBG_THROW_ON_ASSERT_DIALOG = 0x008,   // Dbg_AssertDialog throws "oops" instead of reporting
    BSDBG_THROW_ON_ASSERT        = 0x200,   // BSAssertFail throws "oops"
};

extern DWORD g_dwBSDebugFlags[2];

// Release-build invariant check used all over the libraries. Does nothing unless
// BSDBG_THROW_ON_ASSERT is set for the current debugger state; then throws const char* "oops".
BOOL BSAssertFail();

// AssertFail followed by throw (int)0.
__declspec(noreturn) void BSAssertFailAndThrow();

#define BS_ASSERT(expr) ((expr) ? (void)0 : (void)BSAssertFail())

// Deliberate access violation the original code uses for hard bounds failures.
#define BS_CRASH() (*(volatile int*)0 = 2000)
