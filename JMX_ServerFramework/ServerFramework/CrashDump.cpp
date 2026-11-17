#include "CrashDump.h"

#include <mbstring.h>
#include <stdio.h>

#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerConfig.h"

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

void CrashDump_InstallFilter()
{
    SetUnhandledExceptionFilter(CrashDump_TopLevelExceptionFilter);
}
