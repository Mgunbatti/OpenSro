#pragma once

#include <windows.h>

class CMsg;

// Log lines of the server application. LogPrintf (ServerDataExtern.h) packs a line into a
// SERVER_CONTROL_LOG message and posts it to g_hServerIOCP; the loop of the run mode passes it to
// CServerApp::OnControlMsg, which hands it to the application's log receiver (m_pfnLog: the
// server window, the console or the event log) and copies fatal lines to the fatal log file.

// LogQueue_PostToIOCP (0x41d8e0): releases the message when the post fails.
void LogQueue_PostToIOCP(CMsg* pMsg);

// LogPrintf_wrap (0x43e5a0): receiver of CNetEngine::DumpMsgPool.
void __cdecl ServerApp_LogMsgPoolEntry(DWORD dwOpcode, DWORD dwCount);

// Console_WriteLogLine (0x4538d0): "[level] text" in the level's colour. 0.
int __cdecl Console_WriteLogLine(DWORD dwFlags, const char* pszText);

// FatalLog_AppendLine (0x43ea50): appends "date\ttime\ttext" to
// <module dir>\YYYY-MM-DD_FatalLog.txt.
void FatalLog_AppendLine(const char* pszText);

// WriteStartupErrorFile (0x41d1a0, not in the list): formats the text and appends it as a line
// to <module dir>\StartupError_<pszModule>.txt.
void ServerApp_WriteStartupErrorFile(const char* pszModule, const char* pszFormat, ...);

#ifdef JMX_FRAMEWORK_2011
// LogFile_AppendTimestampedLine (AgentServer 0x42b800): formats the text (0x1000 bytes) and
// appends "date\ttime\ttext\r\n" to pszPath (relative to the current directory). False on
// NULL arguments or when a formatting step fails.
bool LogFile_AppendTimestampedLine(const char* pszPath, const char* pszFormat, ...);
#endif
