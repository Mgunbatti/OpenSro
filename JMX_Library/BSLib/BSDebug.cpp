#include "BSDebug.h"

#include <dbghelp.h>
#include <eh.h>
#include <mbstring.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <strsafe.h>

#ifdef JMX_FRAMEWORK_2011
#include "BSLib/Util/StringUtil.h"
#endif

DWORD g_dwBSDebugFlags[2] = { 0x112, 0x12A };
BOOL  g_bBSDebugAsserted = FALSE;
char  g_szBSDebugError[4096];

static PFN_BSASSERT_CALLBACK g_pfnAssertCallback;
static PFN_BSDEBUG_OUTPUT g_pfnDebugOutput;
static char* g_pszCallStack;                // output of the stack walk filter
static bool  g_bDbgHelpLoaded;
static HMODULE g_hDbgHelp;

typedef BOOL  (WINAPI* PFN_MINIDUMPWRITEDUMP)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                              PMINIDUMP_EXCEPTION_INFORMATION,
                                              PMINIDUMP_USER_STREAM_INFORMATION,
                                              PMINIDUMP_CALLBACK_INFORMATION);
typedef BOOL  (WINAPI* PFN_STACKWALK)(DWORD, HANDLE, HANDLE, LPSTACKFRAME, PVOID,
                                      PREAD_PROCESS_MEMORY_ROUTINE,
                                      PFUNCTION_TABLE_ACCESS_ROUTINE,
                                      PGET_MODULE_BASE_ROUTINE, PTRANSLATE_ADDRESS_ROUTINE);
typedef BOOL  (WINAPI* PFN_SYMINITIALIZE)(HANDLE, PCSTR, BOOL);
typedef DWORD (WINAPI* PFN_SYMSETOPTIONS)(DWORD);
typedef DWORD (WINAPI* PFN_SYMGETOPTIONS)();
typedef DWORD (WINAPI* PFN_UNDECORATESYMBOLNAME)(PCSTR, PSTR, DWORD, DWORD);
typedef BOOL  (WINAPI* PFN_SYMGETLINEFROMADDR)(HANDLE, DWORD, PDWORD, PIMAGEHLP_LINE);
typedef BOOL  (WINAPI* PFN_SYMGETSYMFROMADDR)(HANDLE, DWORD, PDWORD, PIMAGEHLP_SYMBOL);
typedef BOOL  (WINAPI* PFN_SYMGETMODULEINFO)(HANDLE, DWORD, PIMAGEHLP_MODULE);

static PFN_MINIDUMPWRITEDUMP          g_pfnMiniDumpWriteDump;
static PFN_STACKWALK                  g_pfnStackWalk;
static PFUNCTION_TABLE_ACCESS_ROUTINE g_pfnSymFunctionTableAccess;
static PGET_MODULE_BASE_ROUTINE       g_pfnSymGetModuleBase;
static PFN_SYMINITIALIZE              g_pfnSymInitialize;
static PFN_SYMSETOPTIONS              g_pfnSymSetOptions;
static PFN_SYMGETOPTIONS              g_pfnSymGetOptions;
static PFN_UNDECORATESYMBOLNAME       g_pfnUnDecorateSymbolName;
static PFN_SYMGETLINEFROMADDR         g_pfnSymGetLineFromAddr;
static PFN_SYMGETSYMFROMADDR          g_pfnSymGetSymFromAddr;
static PFN_SYMGETMODULEINFO           g_pfnSymGetModuleInfo;

C_ASSERT(sizeof(IMAGEHLP_MODULE) == 572);
C_ASSERT(sizeof(STACKFRAME) == 0xA4);

void BSDebugSetFlags(DWORD dwWithDebugger, DWORD dwWithoutDebugger)
{
    g_dwBSDebugFlags[0] = dwWithDebugger;
    g_dwBSDebugFlags[1] = dwWithoutDebugger;
}

void BSDebugSetAssertCallback(PFN_BSASSERT_CALLBACK pfn)
{
    g_pfnAssertCallback = pfn;
}

// BUG(BSLIB-001): the environment paths are only appended when _dupenv_s fails, so in practice
// the path is "\.;<windir>;<windir>\System". It is built but never passed to SymInitialize.
static void BuildSymbolSearchPath(char* pszPath)
{
    char* pszEnv;
    size_t cch;
    char szWinDir[260];

    StringCchCatA(pszPath, 260, "\\.");

    if (_dupenv_s(&pszEnv, &cch, "_NT_SYMBOL_PATH"))
    {
        StringCchCatA(pszPath, 260, ";");
        StringCchCatA(pszPath, 260, pszEnv);
    }
    if (_dupenv_s(&pszEnv, &cch, "_NT_ALT_SYMBOL_PATH"))
    {
        StringCchCatA(pszPath, 260, ";");
        StringCchCatA(pszPath, 260, pszEnv);
    }
    if (_dupenv_s(&pszEnv, &cch, "SYSTEMROOT"))
    {
        StringCchCatA(pszPath, 260, ";");
        StringCchCatA(pszPath, 260, pszEnv);
        StringCchCatA(pszPath, 260, ";");
        StringCchCatA(pszPath, 260, pszEnv);
        StringCchCatA(pszPath, 260, "\\System32");
    }
    else
    {
        if (!GetWindowsDirectoryA(szWinDir, 259))
            return;
        StringCchCatA(pszPath, 260, ";");
        StringCchCatA(pszPath, 260, szWinDir);
        StringCchCatA(pszPath, 260, ";");
        StringCchCatA(pszPath, 260, szWinDir);
        StringCchCatA(pszPath, 260, "\\System");
    }
}

bool BSDebugLoadDbgHelp()
{
    char szSearchPath[260];
    memset(szSearchPath, 0, sizeof(szSearchPath));

    if (g_bDbgHelpLoaded)
        return true;

    // BUG(BSLIB-005): only success is cached; a failed load is retried on every call.
    g_hDbgHelp = LoadLibraryA("dbghelp.dll");
    if (!g_hDbgHelp)
    {
        StringCchCopyA(g_szBSDebugError, 4096, "cannot load dbghelp.dll");
        return false;
    }

    if ((g_pfnMiniDumpWriteDump = (PFN_MINIDUMPWRITEDUMP)GetProcAddress(g_hDbgHelp, "MiniDumpWriteDump")) &&
        (g_pfnStackWalk = (PFN_STACKWALK)GetProcAddress(g_hDbgHelp, "StackWalk")) &&
        (g_pfnSymFunctionTableAccess = (PFUNCTION_TABLE_ACCESS_ROUTINE)GetProcAddress(g_hDbgHelp, "SymFunctionTableAccess")) &&
        (g_pfnSymGetModuleBase = (PGET_MODULE_BASE_ROUTINE)GetProcAddress(g_hDbgHelp, "SymGetModuleBase")) &&
        (g_pfnSymInitialize = (PFN_SYMINITIALIZE)GetProcAddress(g_hDbgHelp, "SymInitialize")) &&
        (g_pfnSymSetOptions = (PFN_SYMSETOPTIONS)GetProcAddress(g_hDbgHelp, "SymSetOptions")) &&
        (g_pfnSymGetOptions = (PFN_SYMGETOPTIONS)GetProcAddress(g_hDbgHelp, "SymGetOptions")) &&
        (g_pfnUnDecorateSymbolName = (PFN_UNDECORATESYMBOLNAME)GetProcAddress(g_hDbgHelp, "UnDecorateSymbolName")) &&
        (g_pfnSymGetLineFromAddr = (PFN_SYMGETLINEFROMADDR)GetProcAddress(g_hDbgHelp, "SymGetLineFromAddr")) &&
        (g_pfnSymGetSymFromAddr = (PFN_SYMGETSYMFROMADDR)GetProcAddress(g_hDbgHelp, "SymGetSymFromAddr")) &&
        (g_pfnSymGetModuleInfo = (PFN_SYMGETMODULEINFO)GetProcAddress(g_hDbgHelp, "SymGetModuleInfo")))
    {
        g_pfnSymSetOptions((g_pfnSymGetOptions() & 0xFFFFFBE9) | 0x414);
        BuildSymbolSearchPath(szSearchPath);
        if (g_pfnSymInitialize(GetCurrentProcess(), NULL, TRUE))
        {
            g_bDbgHelpLoaded = true;
            return true;
        }
    }

    StringCchCopyA(g_szBSDebugError, 4096, "cannot load dbghelp.dll : wrong version or SymInitialize fail");
    FreeLibrary(g_hDbgHelp);
    return false;
}

// Writes <exe dir>\<exe name>_[YYYY-MM-DD hh-mm-ss]_N.dmp. Used as an exception filter.
LONG BSDebugWriteMiniDump(EXCEPTION_POINTERS* pExceptionInfo)
{
    char szModule[260];
    char szDrive[260];
    char szDir[260];
    char szName[260];
    char szSuffix[260];
    char szDumpFile[260];
    SYSTEMTIME st;

    if (!BSDebugLoadDbgHelp())
        return EXCEPTION_EXECUTE_HANDLER;

    GetModuleFileNameA(NULL, szModule, 260);
    _splitpath_s(szModule, szDrive, 260, szDir, 260, szName, 260, NULL, 0);
    GetLocalTime(&st);
#ifdef JMX_FRAMEWORK_2011
    // AgentServer 0x462240: a running number (1, 2, ...) instead of rand() % 10.
    static int s_nDumpCount = 0;
    if (StringCchPrintfA(szSuffix, 260, "_[%04d-%02d-%02d %02d-%02d-%02d]_%d", st.wYear, st.wMonth,
                         st.wDay, st.wHour, st.wMinute, st.wSecond, ++s_nDumpCount))
        return EXCEPTION_CONTINUE_SEARCH;
#else
    if (StringCchPrintfA(szSuffix, 260, "_[%04d-%02d-%02d %02d-%02d-%02d]_%d", st.wYear, st.wMonth,
                         st.wDay, st.wHour, st.wMinute, st.wSecond, rand() % 10))
        return EXCEPTION_CONTINUE_SEARCH;
#endif
    if (StringCchCatA(szName, 260, szSuffix))
        return EXCEPTION_CONTINUE_SEARCH;
    _makepath_s(szDumpFile, 260, szDrive, szDir, szName, ".dmp");

#ifdef JMX_FRAMEWORK_2011
    // AgentServer 0x462240: the handle is checked, and a failed MiniDumpWriteDump is reported in
    // very_fatal_log.txt of the current directory. The invalid handle is still closed.
    HANDLE hFile = CreateFileA(szDumpFile, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = pExceptionInfo;
        mei.ClientPointers = FALSE;
        if (!g_pfnMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                                    MiniDumpNormal, &mei, NULL, NULL))
        {
            LONG nError = (LONG)GetLastError();
            FILE* pFile = NULL;
            if (!fopen_s(&pFile, "very_fatal_log.txt", "a"))
            {
                SYSTEMTIME stLog;
                GetLocalTime(&stLog);
                if (nError > 0)
                    nError = (LONG)((nError & 0xFFFF) | 0x80070000);
                fprintf(pFile,
                        "[MiniDumpWriteDump] %04d-%02d-%02d %02d:%02d:%02d Cannot Write MiniDump "
                        "(LastError : %d)\r\n",
                        stLog.wYear, stLog.wMonth, stLog.wDay, stLog.wHour, stLog.wMinute,
                        stLog.wSecond, nError);
                fclose(pFile);
            }
        }
    }
    CloseHandle(hFile);
    return EXCEPTION_EXECUTE_HANDLER;
#else
    // BUG(BSLIB-004): the handle is not checked.
    HANDLE hFile = CreateFileA(szDumpFile, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = pExceptionInfo;
    mei.ClientPointers = FALSE;
    g_pfnMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &mei,
                           NULL, NULL);
    CloseHandle(hFile);
    return EXCEPTION_EXECUTE_HANDLER;
#endif
}

// "\tmodule: file.cpp (line)\n\t\tsymbol\n\n".
// BUG(BSLIB-002): the undecorated name is computed but the decorated one is printed, and the
// return value is always 0.
static int FormatStackFrame(char* pszOut, HANDLE hProcess, DWORD dwAddr)
{
    BYTE symBuf[sizeof(IMAGEHLP_SYMBOL) + 520];
    IMAGEHLP_SYMBOL* pSym = (IMAGEHLP_SYMBOL*)symBuf;
    IMAGEHLP_MODULE mod;
    IMAGEHLP_LINE line;
    DWORD dwDisp;
    char szFile[260];
    char szExt[260];
    char szUndecorated[260];

    memset(symBuf, 0, sizeof(symBuf));
    memset(&mod, 0, sizeof(mod));
    pSym->SizeOfStruct = 544;
    pSym->MaxNameLength = 520;
    mod.SizeOfStruct = sizeof(IMAGEHLP_MODULE);

    if (g_pfnSymGetModuleInfo(hProcess, dwAddr, &mod))
    {
        unsigned char* p = _mbsrchr((unsigned char*)mod.ImageName, '\\');
        StringCchPrintfA(pszOut, 520, "\t%s: ", p ? (char*)p + 1 : mod.ImageName);
    }
    else
    {
        StringCchPrintfA(pszOut, 520, "\t<unknown module>: ");
    }

    int nLen = lstrlenA(pszOut);
    if (g_pfnSymGetSymFromAddr(hProcess, dwAddr, &dwDisp, pSym))
    {
        line.Key = 0;
        line.LineNumber = 0;
        line.FileName = NULL;
        line.Address = 0;
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE);
        if (g_pfnSymGetLineFromAddr(hProcess, dwAddr, &dwDisp, &line))
        {
            _splitpath_s(line.FileName, NULL, 0, NULL, 0, szFile, 260, szExt, 260);
            StringCchPrintfA(pszOut + nLen, 520 - nLen, "%s%s (%d)", szFile, szExt, line.LineNumber);
            nLen = lstrlenA(pszOut);
        }
        g_pfnUnDecorateSymbolName(pSym->Name, szUndecorated, 260, 0x42E2);
        StringCchPrintfA(pszOut + nLen, 520 - nLen, "\n\t\t%s", pSym->Name);
    }
    else
    {
        StringCchPrintfA(pszOut + nLen, 520 - nLen, "<unknown symbol>");
    }

    nLen = lstrlenA(pszOut);
    StringCchPrintfA(pszOut + nLen, 520 - nLen, "\n\n");
    return 0;
}

static void WalkStack(CONTEXT* pContext)
{
    char* pszOut = g_pszCallStack;
    HANDLE hProcess = GetCurrentProcess();
    HANDLE hThread = GetCurrentThread();
    char szFrame[520];

    if (!BSDebugLoadDbgHelp())
        return;

    std::vector<DWORD> frames;
    STACKFRAME sf;
    memset(&sf, 0, sizeof(sf));
    sf.AddrPC.Offset = pContext->Eip;
    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = pContext->Ebp;
    sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = pContext->Esp;
    sf.AddrStack.Mode = AddrModeFlat;

    for (unsigned i = 0; i < 64; ++i)
    {
        if (!g_pfnStackWalk(IMAGE_FILE_MACHINE_I386, hProcess, hThread, &sf, NULL, NULL,
                            g_pfnSymFunctionTableAccess, g_pfnSymGetModuleBase, NULL))
            break;
        if (i >= 3 && sf.AddrPC.Offset)
            frames.push_back(sf.AddrPC.Offset);
    }

    // BUG(BSLIB-003): appended with a 520-character limit into a 4096-byte buffer; nLeft never
    // shrinks because FormatStackFrame returns 0.
    unsigned nLeft = 4096;
    for (std::vector<DWORD>::iterator it = frames.begin(); it != frames.end(); ++it)
    {
        unsigned n = FormatStackFrame(szFrame, hProcess, *it);
        if (nLeft < n)
            break;
#ifdef JMX_FRAMEWORK_2011
        // AgentServer 0x4624e0: the limit is the buffer size (nLeft still never shrinks).
        StringCchCatA(pszOut, 4096, szFrame);
#else
        StringCchCatA(pszOut, 520, szFrame);
#endif
        nLeft -= n;
    }
}

static LONG StackWalkFilter(EXCEPTION_POINTERS* pExceptionInfo)
{
    WalkStack(pExceptionInfo->ContextRecord);
    return EXCEPTION_EXECUTE_HANDLER;
}

BOOL BSAssertFail()
{
    BOOL noDebugger = !IsDebuggerPresent();
    if (g_dwBSDebugFlags[noDebugger] & BSDBG_MINIDUMP)
    {
        __try
        {
            throw "oops";
        }
        __except (BSDebugWriteMiniDump(GetExceptionInformation()))
        {
        }
    }
    return noDebugger;
}

void BSAssertFailAndThrow()
{
    BSAssertFail();
    throw 0;
}

static void __cdecl BSSETranslator(unsigned int uCode, EXCEPTION_POINTERS*)
{
    throw uCode;
}

void BSInstallSETranslator()
{
    // C4535: the translator only reaches code built with /EHa; installed as the original does.
#pragma warning(push)
#pragma warning(disable : 4535)
    _set_se_translator(BSSETranslator);
#pragma warning(pop)
    // The handler takes no arguments; the CRT's (cdecl) arguments are ignored.
    _set_invalid_parameter_handler((_invalid_parameter_handler)BSAssertFailAndThrow);
}

bool BSAssertDialog(int nLine, const char* pszFile, const char* pszFormat, ...)
{
    char szCallStack[4096];
    char szText[4096];
    char szExpr[1028];

    g_bBSDebugAsserted = TRUE;
    DWORD dwFlags = g_dwBSDebugFlags[!IsDebuggerPresent()];
    szCallStack[0] = 0;

    // A call stack and a minidump exclude each other (GatewayServer 0x475620, FarmManager
    // 0x4587e0, AgentServer 0x462b90).
    if (dwFlags & BSDBG_CALLSTACK)
    {
        __try
        {
            g_pszCallStack = szCallStack;
            throw "oops";
        }
        __except (StackWalkFilter(GetExceptionInformation()))
        {
        }
    }
    else if (dwFlags & BSDBG_MINIDUMP)
    {
        __try
        {
            g_pszCallStack = szCallStack;
            throw "oops";
        }
        __except (BSDebugWriteMiniDump(GetExceptionInformation()))
        {
        }
    }

    va_list args;
    va_start(args, pszFormat);
    unsigned n = (unsigned)_vsnprintf_s(szExpr, 0x400, 0x3FF, pszFormat, args);
    va_end(args);
    // BUG(BSLIB-006): truncation returns -1 and the caller breaks into the debugger.
    if (n >= 0x400)
        return false;
    if (n == 0x3FF)
        szExpr[0x3FF] = 0;

    if (dwFlags & BSDBG_CALLBACK)
        g_pfnAssertCallback(nLine, pszFile, szExpr);

    DWORD dwContinue = dwFlags & BSDBG_DIALOG_CONTINUE;
    if (!dwContinue && !(dwFlags & BSDBG_DIALOG_EXIT))
        ExitProcess(0);

    if (StringCchPrintfA(szText, 4096,
                         "Debug Assertion Failed.\n\nExpression : %s\nLine : %d\nFile : %s\n\n",
                         szExpr, nLine, pszFile))
        return false;

    if (dwFlags & BSDBG_CALLSTACK)
    {
        if (StringCchCatA(szText, 4096, "CallStack:\n"))
            return false;
        if (StringCchCatA(szText, 4096, szCallStack))
            return false;
        if (StringCchCatA(szText, 4096, "\n\n"))
            return false;
    }

    if (!dwContinue)
    {
        if (StringCchCatA(szText, 4096, "(press \"ok\" to exit program)"))
            return false;
        MessageBoxA(NULL, szText, "BSLib Debug Module", MB_ICONHAND);
        ExitProcess(0);
    }

    if (StringCchCatA(szText, 4096, "(press \"ok\" to advance, press \"cancel\" to exit)"))
        return false;
    if (MessageBoxA(NULL, szText, "BSLib Debug Module", MB_OKCANCEL | MB_ICONHAND) != IDOK)
        ExitProcess(0);
    return (dwFlags & BSDBG_BREAK) == 0;
}

#ifdef JMX_FRAMEWORK_2011
// AgentServer 0x462d10: the message part of the assert dialogs, from the callback on (the same
// steps as the end of BSAssertDialog). The call stack is the buffer g_pszCallStack points to.
static bool BSAssertDialog_ShowMessage(const char* pszFile, const char* pszExpr, int nLine,
                                       DWORD dwFlags)
{
    char szText[4096];

    if (dwFlags & BSDBG_CALLBACK)
        g_pfnAssertCallback(nLine, pszFile, pszExpr);

    DWORD dwContinue = dwFlags & BSDBG_DIALOG_CONTINUE;
    if (!dwContinue && !(dwFlags & BSDBG_DIALOG_EXIT))
        ExitProcess(0);

    if (StringCchPrintfA(szText, 4096,
                         "Debug Assertion Failed.\n\nExpression : %s\nLine : %d\nFile : %s\n\n",
                         pszExpr, nLine, pszFile))
        return false;

    if (dwFlags & BSDBG_CALLSTACK)
    {
        if (StringCchCatA(szText, 4096, "CallStack:\n"))
            return false;
        if (StringCchCatA(szText, 4096, g_pszCallStack))
            return false;
        if (StringCchCatA(szText, 4096, "\n\n"))
            return false;
    }

    if (!dwContinue)
    {
        if (StringCchCatA(szText, 4096, "(press \"ok\" to exit program)"))
            return false;
        MessageBoxA(NULL, szText, "BSLib Debug Module", MB_ICONHAND);
        ExitProcess(0);
    }

    if (StringCchCatA(szText, 4096, "(press \"ok\" to advance, press \"cancel\" to exit)"))
        return false;
    if (MessageBoxA(NULL, szText, "BSLib Debug Module", MB_OKCANCEL | MB_ICONHAND) != IDOK)
        ExitProcess(0);
    return (dwFlags & BSDBG_BREAK) == 0;
}

// AgentServer 0x462960 (not referenced in AgentServer).
bool BSAssertDialogW(int nLine, const wchar_t* pszFile, const wchar_t* pszFormat, ...)
{
    char szCallStack[4096];
    char szExpr[4096];
    wchar_t szExprW[0x400];
    char szFile[260];

    g_bBSDebugAsserted = TRUE;
    DWORD dwFlags = g_dwBSDebugFlags[!IsDebuggerPresent()];
    szCallStack[0] = 0;

    // As in BSAssertDialog, a call stack and a minidump exclude each other.
    if (dwFlags & BSDBG_CALLSTACK)
    {
        __try
        {
            g_pszCallStack = szCallStack;
            throw "oops";
        }
        __except (StackWalkFilter(GetExceptionInformation()))
        {
        }
    }
    else if (dwFlags & BSDBG_MINIDUMP)
    {
        __try
        {
            g_pszCallStack = szCallStack;
            throw "oops";
        }
        __except (BSDebugWriteMiniDump(GetExceptionInformation()))
        {
        }
    }

    va_list args;
    va_start(args, pszFormat);
    HRESULT hr = StringCchVPrintfW(szExprW, 0x400, pszFormat, args);
    va_end(args);
    if (hr != S_OK)
        return false;

    memset(szFile, 0, sizeof(szFile));
    memset(szExpr, 0, sizeof(szExpr));
    size_t cchFile = 0;
    size_t cchExpr = 0;
    if (!pszFile)
        return false;
    if (StringCchLengthW(pszFile, 4096, &cchFile) != S_OK)
        return false;
    if (StringCchLengthW(szExprW, 4096, &cchExpr) != S_OK)
        return false;
    if (!WideCharToMultiByte_wrap(pszFile, (int)cchFile, szFile, 260))
        return false;
    if (!WideCharToMultiByte_wrap(szExprW, (int)cchExpr, szExpr, 0x1000))
        return false;
    return BSAssertDialog_ShowMessage(szFile, szExpr, nLine, dwFlags);
}

// AgentServer 0x4619f0
void File_AppendString(const char* pszPath, const char* pszText)
{
    FILE* pFile = NULL;
    if (fopen_s(&pFile, pszPath, "a") == 0 && pFile)
    {
        fprintf_s(pFile, "%s", pszText);
        fclose(pFile);
    }
}
#endif

void BSDebugSetOutputCallback(PFN_BSDEBUG_OUTPUT pfn)
{
    g_pfnDebugOutput = pfn;
}

void BSDebugOutput(const char* pszFormat, ...)
{
    if (!g_pfnDebugOutput)
        return;

    char szText[1024];
    va_list args;
    va_start(args, pszFormat);
    StringCchVPrintfA(szText, 1024, pszFormat, args);
    va_end(args);
    StringCchCatA(szText, 1024, "\n");
    g_pfnDebugOutput(szText);
}

int BSMessageBoxPrintf(const char* pszFormat, ...)
{
    int nResult = lstrlenA(pszFormat);
    if (nResult >= 1024)
        return nResult;

    char szText[1024];
    memset(szText, 0, sizeof(szText));
    va_list args;
    va_start(args, pszFormat);
    StringCchVPrintfA(szText, 1024, pszFormat, args);
    va_end(args);
    return MessageBoxA(NULL, szText, "BSObj Plugin", 0);
}
