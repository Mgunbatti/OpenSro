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

// Receiver of BSDebugOutput (SetGlobal_4F9D10).
typedef void (*PFN_BSDEBUG_OUTPUT)(const char* pszText);
void BSDebugSetOutputCallback(PFN_BSDEBUG_OUTPUT pfn);

// DebugOutputf: formats into 1024 bytes, appends a newline and passes the text to the output
// callback. Nothing without a callback.
void BSDebugOutput(const char* pszFormat, ...);

// MsgBoxPrintf: message box "BSObj Plugin" with the formatted text (1024 bytes). Format strings
// of 1024 characters or more show nothing (the result is then lstrlenA of the format).
int BSMessageBoxPrintf(const char* pszFormat, ...);

// Release-build invariant check. Writes a minidump when BSDBG_MINIDUMP is set, otherwise does
// nothing. Never throws.
BOOL BSAssertFail();

// BSAssertFail followed by throw (int)0.
__declspec(noreturn) void BSAssertFailAndThrow();

// GatewayServer_RunWithSETranslator (GatewayServer 0x475af0): on the calling thread structured
// exceptions are thrown as unsigned int (the exception code), invalid CRT parameters assert and
// throw. Called by the modules' RegisterMainProcessTask.
void BSInstallSETranslator();

// Assert dialog. With BSDBG_CALLSTACK the call stack is collected and no minidump is written;
// otherwise BSDBG_MINIDUMP writes one. Returns FALSE when the caller should break into the
// debugger.
bool BSAssertDialog(int nLine, const char* pszFile, const char* pszFormat, ...);

bool BSDebugLoadDbgHelp();
LONG BSDebugWriteMiniDump(EXCEPTION_POINTERS* pExceptionInfo);

// The January 2011 framework (GlobalManager) is the July 2011 one plus a few blocks.
#if defined(JMX_FRAMEWORK_2011_01) && !defined(JMX_FRAMEWORK_2011)
#error JMX_FRAMEWORK_2011_01 needs JMX_FRAMEWORK_2011 (see GLOBAL_DEFS in the Makefile)
#endif

#ifdef JMX_FRAMEWORK_2011
// Wide character assert dialog (AgentServer 0x462960): the file name and the formatted text
// (0x400 characters) are converted to the ANSI code page; with BSDBG_CALLSTACK no minidump is
// written. Returns FALSE when the caller should break into the debugger.
bool BSAssertDialogW(int nLine, const wchar_t* pszFile, const wchar_t* pszFormat, ...);

// File_AppendString (AgentServer 0x4619f0): appends the text to the file (fopen_s "a").
void File_AppendString(const char* pszPath, const char* pszText);
#endif

#define BS_ASSERT(expr) ((expr) ? (void)0 : (void)BSAssertFail())

#define BS_ASSERT_DIALOG(expr)                                              \
    do                                                                      \
    {                                                                       \
        if (!(expr) && !BSAssertDialog(__LINE__, __FILE__, #expr))          \
            __debugbreak();                                                 \
    } while (0)

// Deliberate access violation the original code uses for hard bounds failures.
#define BS_CRASH() (*(volatile int*)0 = 2000)
