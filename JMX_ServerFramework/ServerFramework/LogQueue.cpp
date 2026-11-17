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
// Console colours of the levels, the last for all higher ones (0x4F8134).
static const WORD s_awConsoleColor[5] = { 0x7, 0xE, 0xC, 0xF, 0x0 };

const char* LogLevel_ToString(DWORD dwFlags)
{
    BYTE byLevel = HIBYTE(HIWORD(dwFlags));
    if (byLevel <= 3)
        return s_apszLogLevel[byLevel];
    return "";
}

int LogPrintf(DWORD dwFlags, const char* pszFormat, ...)
{
    char szText[0x2000];
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
    if (byLevel >= 4)
        byLevel = 4;
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
        fprintf(pFile, "%04d-%02d-%02d\t%02d:%02d:%02d\t%s\n", st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, pszText);
        fclose(pFile);
    }
}
