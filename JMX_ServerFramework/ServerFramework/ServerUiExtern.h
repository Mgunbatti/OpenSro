#pragma once

#include <windows.h>

// Parts of the server application used by the server window classes.

namespace ServerFramework
{
class CServerFrameWindow;
}

// 0x4F9CA0: ServerFrameworkRes.dll (menu 0x65, architecture view icons 0x66..0x71).
extern HMODULE g_hServerFrameworkRes;

// LogLevel_ToString (0x443e20): name of the level in the high byte of the log flags.
const char* LogLevel_ToString(DWORD dwFlags);

// ReportLog_Save (0x45c8e0): writes the log list of the server window to
// "<module dir>\ReportLog_[<service>][<date time>].txt".
void ReportLog_Save(ServerFramework::CServerFrameWindow* pFrame);

// The server application object (0x4F9C9C), virtual slot 15: custom diagnostic dump.
void ServerApp_CustomDump();
// The server application object, virtual slot 16, for message 0x9000 of the server window.
void ServerApp_OnFrameMessage9000(LPARAM lParam);
#ifdef JMX_FRAMEWORK_2011
// The server application object, virtual slots 16 and 17 of the 2011 framework (AgentServer).
void ServerApp_OnFrameCommand();
void ServerApp_OnSuspectedUserCommand(DWORD dwCommand);
#endif
