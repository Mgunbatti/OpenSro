#include "LogQueue.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "BSLib/BSDebug.h"
#include "BSLib/thread/ThreadManager.h"
#include "BSNet/Msg.h"
#include "ServerFramework/GdiUtil.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerApp.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerFramework.h"
#include "ServerFramework/ServerFrameWindow.h"
#include "ServerFramework/ServerUiExtern.h"

#include <commctrl.h>
#include <strsafe.h>

using namespace ServerFramework;

// Level names by the high byte of the log flags (0x4F81C8); higher levels give "".
static const char* const s_apszLogLevel[4] = { "notify", "warnning", "fatal", "unknown" };
#ifdef JMX_FRAMEWORK_2007
// Console colours of the levels, the last for all higher ones (FarmManager 0x4CF7D8).
static const WORD s_awConsoleColor[4] = { 0x7, 0xE, 0xC, 0xF };
#else
// Console colours of the levels, the last for all higher ones (0x4F8134).
static const WORD s_awConsoleColor[5] = { 0x7, 0xE, 0xC, 0xF, 0x0 };
#endif

const char* LogLevel_ToString(DWORD dwFlags)
{
    BYTE byLevel = HIBYTE(HIWORD(dwFlags));
#ifdef JMX_FRAMEWORK_2007
    // FarmManager 0x409220: higher levels are "unknown".
    if (byLevel > 3)
        byLevel = 3;
    return s_apszLogLevel[byLevel];
#else
    if (byLevel <= 3)
        return s_apszLogLevel[byLevel];
    return "";
#endif
}

#ifdef JMX_FRAMEWORK_2007
// FarmManager 0x40a630: a 0x2800-byte buffer.
#define LOG_PRINTF_BUFFER_SIZE 0x2800
#else
#define LOG_PRINTF_BUFFER_SIZE 0x2000
#endif

int LogPrintf(DWORD dwFlags, const char* pszFormat, ...)
{
    char szText[LOG_PRINTF_BUFFER_SIZE];
    va_list args;
    va_start(args, pszFormat);
    int n = _vsnprintf_s(szText, sizeof(szText), sizeof(szText) - 1, pszFormat, args);
    va_end(args);
    if (n < 0 || n > (int)sizeof(szText) - 1)
    {
        szText[sizeof(szText) - 1] = 0;
        BSAssertFail();
        return FALSE;
    }
    if (n == sizeof(szText) - 1)
        szText[sizeof(szText) - 1] = 0;

    if (g_pBSNet && (g_hServerFrameWnd || g_hServerIOCP))
    {
        CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
        pMsg->SetOpcode(SERVER_CONTROL_LOG);
        pMsg->WriteBytes(&dwFlags, 4);
        *pMsg << szText;
        LogQueue_PostToIOCP(pMsg);
    }
    else
    {
        BS_THREAD_TRACE(LogLevel_ToString(dwFlags));
    }
    return TRUE;
}

void LogQueue_PostToIOCP(CMsg* pMsg)
{
    if (!PostQueuedCompletionStatus(g_hServerIOCP, 4, 1, &pMsg->m_Overlapped))
        g_pBSNet->ReleaseMsg(pMsg);
}

void __cdecl ServerApp_LogMsgPoolEntry(DWORD dwOpcode, DWORD dwCount)
{
    LogPrintf(0x2000001, "[MsgID: 0x%x >> Count: %d]", dwOpcode, dwCount);
}

int __cdecl Console_WriteLogLine(DWORD dwFlags, const char* pszText)
{
    BYTE byLevel = HIBYTE(HIWORD(dwFlags));
#ifdef JMX_FRAMEWORK_2007
    // FarmManager 0x43f360
    if (byLevel >= 3)
        byLevel = 3;
#else
    if (byLevel >= 4)
        byLevel = 4;
#endif
    SetConsoleTextAttribute(g_hServerConsole, s_awConsoleColor[byLevel]);
    DWORD dwWritten;
    WriteConsoleA(g_hServerConsole, "[", 1, &dwWritten, NULL);
    const char* pszLevel = LogLevel_ToString(dwFlags);
    WriteConsoleA(g_hServerConsole, pszLevel, (DWORD)strlen(pszLevel), &dwWritten, NULL);
    WriteConsoleA(g_hServerConsole, "] ", 2, &dwWritten, NULL);
    WriteConsoleA(g_hServerConsole, pszText, (DWORD)strlen(pszText), &dwWritten, NULL);
    WriteConsoleA(g_hServerConsole, "\n", 1, &dwWritten, NULL);
    return 0;
}

void ReportLog_Save(CServerFrameWindow* pFrame)
{
    std::string strLevel;
    std::string strText;
    std::string strTime;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char szFile[MAX_PATH];
    if (StringCchPrintfA(szFile, MAX_PATH, "%s\\ReportLog_[%s][%04d-%02d-%02d %02d-%02d-%02d].txt",
                         g_ServerGlobals.szModuleDir, g_ServerGlobals.szServiceName, st.wYear,
                         st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond))
    {
        BSAssertFail();
    }
    FILE* pFile;
    if (fopen_s(&pFile, szFile, "w"))
    {
        BSAssertFail();
        return;
    }
    HWND hList = pFrame->m_hLogList;
    for (int nItem = (int)ListView_GetFirstItem(hList); nItem != -1;
         nItem = (int)SendMessageA(hList, LVM_GETNEXTITEM, nItem, 0))
    {
        strLevel = ListView_GetItemTextString(hList, nItem, 0);
        strText = ListView_GetItemTextString(hList, nItem, 1);
        strTime = ListView_GetItemTextString(hList, nItem, 2);
        fprintf(pFile, "%s\t%s\t%s\n", strLevel.c_str(), strText.c_str(), strTime.c_str());
    }
    fclose(pFile);
    LogPrintf(0, "successfully log saved : %s", szFile);
}

void ServerApp_WriteStartupErrorFile(const char* pszModule, const char* pszFormat, ...)
{
    char szText[MAX_PATH];
    va_list args;
    va_start(args, pszFormat);
    int n = _vsnprintf_s(szText, MAX_PATH, MAX_PATH - 1, pszFormat, args);
    va_end(args);
    if (n < 0 || n > MAX_PATH - 1)
    {
        szText[MAX_PATH - 1] = 0;
        BSAssertFail();
    }
    else if (n == MAX_PATH - 1)
    {
        szText[MAX_PATH - 1] = 0;
    }

    char szFile[MAX_PATH];
    if (StringCchPrintfA(szFile, MAX_PATH, "%s\\StartupError_%s.txt",
                         g_ServerGlobals.szModuleDir, pszModule))
    {
        BSAssertFail();
    }
    FILE* pFile;
    if (!fopen_s(&pFile, szFile, "a"))
    {
        fprintf(pFile, "%s\n", szText);
        fclose(pFile);
    }
}

void FatalLog_AppendLine(const char* pszText)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char szFile[MAX_PATH];
    if (StringCchPrintfA(szFile, MAX_PATH, "%s\\%04d-%02d-%02d_FatalLog.txt",
                         g_ServerGlobals.szModuleDir, st.wYear, st.wMonth, st.wDay))
    {
        BSAssertFail();
    }
    FILE* pFile;
    if (!fopen_s(&pFile, szFile, "a"))
    {
#ifdef JMX_FRAMEWORK_2011
        // AgentServer 0x430570: the module name in brackets before the text.
        fprintf(pFile, "%04d-%02d-%02d\t%02d:%02d:%02d\t[%s]\t%s\n", st.wYear, st.wMonth,
                st.wDay, st.wHour, st.wMinute, st.wSecond, g_ServerGlobals.szServiceName,
                pszText);
#else
        fprintf(pFile, "%04d-%02d-%02d\t%02d:%02d:%02d\t%s\n", st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, pszText);
#endif
        fclose(pFile);
    }
}

#ifdef JMX_FRAMEWORK_2011
// AgentServer 0x42b800
bool LogFile_AppendTimestampedLine(const char* pszPath, const char* pszFormat, ...)
{
    if (!pszPath || !pszFormat)
        return false;
    char szText[0x1000];
    va_list args;
    va_start(args, pszFormat);
    int n = _vsnprintf_s(szText, sizeof(szText), sizeof(szText) - 1, pszFormat, args);
    va_end(args);
    if (n < 0 || n > (int)sizeof(szText) - 1)
        return false;
    if (n == sizeof(szText) - 1)
        szText[sizeof(szText) - 1] = 0;

    char szFile[MAX_PATH];
    if (StringCchPrintfA(szFile, MAX_PATH, "%s", pszPath))
        return false;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char szLine[0x2000];
    if (StringCchPrintfA(szLine, sizeof(szLine), "%04d-%02d-%02d\t%02d:%02d:%02d\t%s\r\n",
                         st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, szText))
    {
        return false;
    }
    File_AppendString(szFile, szLine);
    return true;
}
#endif
