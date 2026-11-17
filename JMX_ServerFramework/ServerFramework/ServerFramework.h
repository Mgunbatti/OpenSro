#pragma once

#include <windows.h>

// Process-wide entry points and globals of the server framework that the data, window and
// process code use without needing the application class (ServerApp.h).

namespace ServerFramework
{
class CServerApp;
class CServerMoniterView;
}

// 0x4F9C9C: the application object (set by the module's WinMain).
extern ServerFramework::CServerApp* g_pServerApp;
// 0x4F9CF4: the server window (window mode).
extern HWND g_hServerFrameWnd;
// 0x4F9CFC: the completion port the log lines and control messages are posted to.
extern HANDLE g_hServerIOCP;
// 0x4F9CA4: division of this server's body, stored for 0x200C.
extern DWORD g_dwSelfDivisionID;
// 0x4F9D00: console output (console mode).
extern HANDLE g_hServerConsole;
// 0x4F9CA0: ServerFrameworkRes.dll (also declared in ServerUiExtern.h).
extern HMODULE g_hServerFrameworkRes;

// ServerApp_PostQuit (0x41d8b0): ends the message loop (completion port packet in IOCP mode,
// WM_CLOSE to the server window otherwise).
BOOL ServerApp_PostQuit();

// StrCmpI_Locale (0x41f430), LogPrintf (0x41d790): see ServerDataExtern.h.
// LogLevel_ToString (0x443e20), ReportLog_Save (0x45c8e0), ServerApp_CustomDump,
// ServerApp_OnFrameMessage9000: see ServerUiExtern.h.

// Global4F9CF8_GetField188 (0x4467d0): the monitor view of the server window.
ServerFramework::CServerMoniterView* ServerFrameWindow_GetMonitorView();
