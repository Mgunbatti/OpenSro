#include "CrashDump.h"

#include <mbstring.h>
#include <stdio.h>

#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerConfig.h"
#ifdef JMX_FRAMEWORK_2011
#include "ServerFramework/LogQueue.h"
#endif

#include <strsafe.h>

BYTE g_bCrashDumpWritten = 0;

// MiniDumpWriteDump of dbghelp.h, which the build does not need otherwise.
struct CRASHDUMP_EXCEPTION_INFORMATION
{
    DWORD               ThreadId;
    EXCEPTION_POINTERS* ExceptionPointers;
    BOOL                ClientPointers;
};

typedef BOOL (WINAPI* PFN_MINIDUMPWRITEDUMP)(HANDLE hProcess, DWORD dwProcessId, HANDLE hFile,
                                             DWORD dwDumpType,
                                             CRASHDUMP_EXCEPTION_INFORMATION* pException,
                                             void* pUserStream, void* pCallback);

enum
{
    CRASHDUMP_TYPE_WITH_DATA_SEGS = 1,
};

static void CopyChecked(char* pszDst, const char* pszSrc)
{
    if (StringCchCopyA(pszDst, MAX_PATH, pszSrc))
        BSAssertFail();
}

static void CatChecked(char* pszDst, const char* pszSrc)
{
    if (StringCchCatA(pszDst, MAX_PATH, pszSrc))
        BSAssertFail();
}

#ifdef JMX_FRAMEWORK_2011
// AgentServer 0x44f2a0 / 0x44f8c0: the filter passes the thread and the exception to a worker
// (CrashDump_WriteDump), which runs on its own thread after a stack overflow. Failed steps are
// logged to ExceptionHandlerInfo.log in the current directory instead of asserting.

// The block the filter allocates for the worker.
struct CRASHDUMP_REQUEST
{
    DWORD               dwThreadID;         // +0
    EXCEPTION_POINTERS* pExceptionInfo;     // +4
};

static const char s_szExceptionLog[] = "ExceptionHandlerInfo.log";

static void CopyLogged(char* pszDst, const char* pszSrc, int nStep)
{
    if (StringCchCopyA(pszDst, MAX_PATH, pszSrc))
        LogFile_AppendTimestampedLine(s_szExceptionLog, "Cannot make string - szDumpPath, (%d)",
                                      nStep);
}

static void CatLogged(char* pszDst, const char* pszSrc, int nStep)
{
    if (StringCchCatA(pszDst, MAX_PATH, pszSrc))
        LogFile_AppendTimestampedLine(s_szExceptionLog, "Cannot make string - szDumpPath, (%d)",
                                      nStep);
}

// BUG(SRVFW-001): the error text is written only when the .txt could NOT be opened, through the
// FILE* of the failed open (the variable that held the request before).
// BUG(FWAPP-002): the .txt name lacks the '\' after the module directory.
DWORD WINAPI CrashDump_WriteDump(LPVOID pParam)
{
    CRASHDUMP_REQUEST* pRequest = (CRASHDUMP_REQUEST*)pParam;
    FILE* pFile = (FILE*)pParam;
    LogFile_AppendTimestampedLine(s_szExceptionLog, "TopLevelFilter has been called!");
    char szDbgHelp[MAX_PATH];
    HMODULE hDbgHelp = NULL;
    bool bTryDefault = true;
    if (GetModuleFileNameA(NULL, szDbgHelp, MAX_PATH))
    {
        unsigned char* pSlash = _mbsrchr((unsigned char*)szDbgHelp, '\\');
        if (pSlash)
        {
            if (StringCchCopyA((char*)pSlash + 1, 256, "DBGHELP.DLL"))
            {
                LogFile_AppendTimestampedLine(s_szExceptionLog,
                                              "Cannot make string - DbgHelpPath");
            }
            hDbgHelp = LoadLibraryA(szDbgHelp);
            bTryDefault = hDbgHelp == NULL;
        }
    }
    if (bTryDefault)
        hDbgHelp = LoadLibraryA("DBGHELP.DLL");

    const char* pszError = NULL;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char szTime[0x20];
    if (StringCchPrintfA(szTime, sizeof(szTime), "_[%04d-%02d-%02d %02d-%02d-%02d]", st.wYear,
                         st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond))
    {
        LogFile_AppendTimestampedLine(s_szExceptionLog, "Cannot make string - szDateTime");
    }

    char szFile[MAX_PATH];
    char szMessage[MAX_PATH];
    if (hDbgHelp)
    {
        PFN_MINIDUMPWRITEDUMP pfnWriteDump =
            (PFN_MINIDUMPWRITEDUMP)GetProcAddress(hDbgHelp, "MiniDumpWriteDump");
        if (pfnWriteDump)
        {
            CopyLogged(szFile, g_ServerGlobals.szModuleDir, 0);
            CatLogged(szFile, "\\", 1);
            CatLogged(szFile, g_ServerGlobals.szServiceName, 2);
            CatLogged(szFile, szTime, 3);
            CatLogged(szFile, ".dmp", 4);
            HANDLE hFile = CreateFileA(szFile, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE)
            {
                if (StringCchPrintfA(szMessage, MAX_PATH,
                                     "Failed to create dump file '%s' (error %d)", szFile,
                                     GetLastError()))
                {
                    LogFile_AppendTimestampedLine(s_szExceptionLog,
                                                  "Cannot make string - szDumpPath, (6)");
                }
                pszError = szMessage;
            }
            else
            {
                // BUG(FW11-020): a request the filter could not allocate (NULL) is read here.
                CRASHDUMP_EXCEPTION_INFORMATION info;
                info.ThreadId = pRequest->dwThreadID;
                info.ExceptionPointers = pRequest->pExceptionInfo;
                info.ClientPointers = FALSE;
                DWORD dwProcessId = GetCurrentProcessId();
                if (!pfnWriteDump(GetCurrentProcess(), dwProcessId, hFile,
                                  CRASHDUMP_TYPE_WITH_DATA_SEGS, &info, NULL, NULL))
                {
                    if (StringCchPrintfA(szMessage, MAX_PATH,
                                         "Failed to save dump file to '%s' (error %d)", szFile,
                                         GetLastError()))
                    {
                        LogFile_AppendTimestampedLine(s_szExceptionLog,
                                                      "Cannot make string - szDumpPath, (5)");
                    }
                    pszError = szMessage;
                }
                CloseHandle(hFile);
                if (!pszError)
                {
                    g_bCrashDumpWritten = 1;
                    return 1;
                }
            }
        }
        else
        {
            pszError = "DBGHELP.DLL too old (couldn't find procedure MiniDumpWriteDump)";
        }
    }
    else
    {
        pszError = "DBGHELP.DLL not found";
    }

    CopyLogged(szFile, g_ServerGlobals.szModuleDir, 7);
    CatLogged(szFile, g_ServerGlobals.szServiceName, 8);
    CatLogged(szFile, szTime, 9);
    CatLogged(szFile, ".txt", 10);
    if (fopen_s(&pFile, szFile, "w"))
    {
        fprintf(pFile, "%s\n", pszError);
        fclose(pFile);
    }
    g_bCrashDumpWritten = 1;
    return 1;
}

// BUG(FW11-020): the request block is never freed.
LONG WINAPI CrashDump_TopLevelExceptionFilter(EXCEPTION_POINTERS* pExceptionInfo)
{
    if (!pExceptionInfo)
        return EXCEPTION_EXECUTE_HANDLER;
    CRASHDUMP_REQUEST* pRequest = (CRASHDUMP_REQUEST*)operator new(sizeof(CRASHDUMP_REQUEST));
    if (pRequest)
    {
        pRequest->dwThreadID = GetCurrentThreadId();
        pRequest->pExceptionInfo = pExceptionInfo;
    }
    if (pExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW)
    {
        // the faulting thread has no stack left
        HANDLE hThread = CreateThread(NULL, 0x19000, CrashDump_WriteDump, pRequest, 0, NULL);
        if (WaitForSingleObject(hThread, INFINITE) == WAIT_OBJECT_0)
            CloseHandle(hThread);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    return CrashDump_WriteDump(pRequest);
}
#else
// BUG(SRVFW-001): the error text is written only when the .txt could NOT be opened, through the
// FILE* of the failed open (the variable that held pExceptionInfo before).
// BUG(FWAPP-002): the .txt name lacks the '\' after the module directory.
LONG WINAPI CrashDump_TopLevelExceptionFilter(EXCEPTION_POINTERS* pExceptionInfo)
{
    FILE* pFile = (FILE*)pExceptionInfo;
    char szDbgHelp[MAX_PATH];
    HMODULE hDbgHelp = NULL;
    bool bTryDefault = true;
    if (GetModuleFileNameA(NULL, szDbgHelp, MAX_PATH))
    {
        unsigned char* pSlash = _mbsrchr((unsigned char*)szDbgHelp, '\\');
        if (pSlash)
        {
            if (StringCchCopyA((char*)pSlash + 1, 256, "DBGHELP.DLL"))
                BSAssertFail();
            hDbgHelp = LoadLibraryA(szDbgHelp);
            bTryDefault = hDbgHelp == NULL;
        }
    }
    if (bTryDefault)
        hDbgHelp = LoadLibraryA("DBGHELP.DLL");

    const char* pszError = NULL;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char szTime[MAX_PATH];
    if (StringCchPrintfA(szTime, MAX_PATH, "_[%04d-%02d-%02d %02d-%02d-%02d]", st.wYear,
                         st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond))
    {
        BSAssertFail();
    }

    char szFile[MAX_PATH];
    char szMessage[MAX_PATH];
    if (hDbgHelp)
    {
        PFN_MINIDUMPWRITEDUMP pfnWriteDump =
            (PFN_MINIDUMPWRITEDUMP)GetProcAddress(hDbgHelp, "MiniDumpWriteDump");
        if (pfnWriteDump)
        {
            CopyChecked(szFile, g_ServerGlobals.szModuleDir);
            CatChecked(szFile, "\\");
            CatChecked(szFile, g_ServerGlobals.szServiceName);
            CatChecked(szFile, szTime);
            CatChecked(szFile, ".dmp");
            HANDLE hFile = CreateFileA(szFile, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE)
            {
                if (StringCchPrintfA(szMessage, MAX_PATH,
                                     "Failed to create dump file '%s' (error %d)", szFile,
                                     GetLastError()))
                {
                    BSAssertFail();
                }
                pszError = szMessage;
            }
            else
            {
                CRASHDUMP_EXCEPTION_INFORMATION info;
                info.ThreadId = GetCurrentThreadId();
                info.ExceptionPointers = pExceptionInfo;
                info.ClientPointers = FALSE;
                DWORD dwProcessId = GetCurrentProcessId();
                if (!pfnWriteDump(GetCurrentProcess(), dwProcessId, hFile,
                                  CRASHDUMP_TYPE_WITH_DATA_SEGS, &info, NULL, NULL))
                {
                    if (StringCchPrintfA(szMessage, MAX_PATH,
                                         "Failed to save dump file to '%s' (error %d)", szFile,
                                         GetLastError()))
                    {
                        BSAssertFail();
                    }
                    pszError = szMessage;
                }
                CloseHandle(hFile);
                if (!pszError)
                {
                    g_bCrashDumpWritten = 1;
                    return EXCEPTION_EXECUTE_HANDLER;
                }
            }
        }
        else
        {
            pszError = "DBGHELP.DLL too old (couldn't find procedure MiniDumpWriteDump)";
        }
    }
    else
    {
        pszError = "DBGHELP.DLL not found";
    }

    CopyChecked(szFile, g_ServerGlobals.szModuleDir);
    CatChecked(szFile, g_ServerGlobals.szServiceName);
    CatChecked(szFile, szTime);
    CatChecked(szFile, ".txt");
    if (fopen_s(&pFile, szFile, "w"))
    {
        fprintf(pFile, "%s\n", pszError);
        fclose(pFile);
    }
    g_bCrashDumpWritten = 1;
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void CrashDump_InstallFilter()
{
    SetUnhandledExceptionFilter(CrashDump_TopLevelExceptionFilter);
}
