#pragma once

#include <windows.h>
#include <vector>

// The server application as NT service (not used by GatewayServer, whose WinMain runs the window
// mode). Command line: nothing (started by the service manager), "-install <account> <password>"
// ("NULL" for none) or "-uninstall". Install and uninstall report through message boxes.

enum NTSERVICE_COMMAND
{
    NTSERVICE_CMD_DISPATCH,     // no argument: StartServiceCtrlDispatcher
    NTSERVICE_CMD_INSTALL,      // "-install" with exactly two more arguments
    NTSERVICE_CMD_UNINSTALL,    // "-uninstall"
    NTSERVICE_CMD_INVALID,      // anything else
};

// 0x4F9C5C, 0x4F9D04
extern SERVICE_STATUS g_ServiceStatus;
extern HANDLE g_hServiceEventLog;

// The argument handling of NTService_HandleCommandLine: splits pszCmdLine in place at spaces
// (strtok_s) and classifies the arguments (case-insensitive, locale 0x400).
NTSERVICE_COMMAND NTService_ParseCommandLine(char* pszCmdLine, std::vector<char*>* pArgs);

// NTService_HandleCommandLine (0x454190): runs the command of g_ServerGlobals.szCommandLine.
// 1 after dispatching or installing, -1 otherwise (also after a successful uninstall).
int NTService_HandleCommandLine();

// NTService_ServiceMainLoop (0x453f60): ServiceMain. Log lines go to the event log; the
// application runs like in console mode until the service is stopped.
void WINAPI NTService_ServiceMain(DWORD dwArgc, LPSTR* ppszArgv);
// NTService_CtrlHandler (0x453f10): stop and shutdown end the loop.
void WINAPI NTService_CtrlHandler(DWORD dwControl);
// NTService_ReportEvent (0x453ec0): log receiver; event type by level (0 information,
// 1 warning, 2 error, others information), event ID = the log flags.
int __cdecl NTService_ReportEvent(DWORD dwFlags, const char* pszText);
// SetServiceStatus_wrap (0x453f40)
BOOL NTService_SetStatus(SERVICE_STATUS_HANDLE hStatus, DWORD dwState);
