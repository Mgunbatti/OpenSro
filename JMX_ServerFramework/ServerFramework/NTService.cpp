#include "NTService.h"

#include <string.h>

#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerApp.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"

#include <strsafe.h>

SERVICE_STATUS g_ServiceStatus;
HANDLE g_hServiceEventLog = NULL;

int __cdecl NTService_ReportEvent(DWORD dwFlags, const char* pszText)
{
    WORD wType;
    switch (dwFlags >> 24)
    {
    case 1:
        wType = EVENTLOG_WARNING_TYPE;
        break;
    case 2:
        wType = EVENTLOG_ERROR_TYPE;
        break;
    default:
        wType = EVENTLOG_INFORMATION_TYPE;
        break;
    }
    ReportEventA(g_hServiceEventLog, wType, 0, dwFlags, NULL, 1, 0, &pszText, NULL);
    return 0;
}

void WINAPI NTService_CtrlHandler(DWORD dwControl)
{
    if (dwControl == SERVICE_CONTROL_STOP || dwControl == SERVICE_CONTROL_SHUTDOWN)
    {
        if (g_hServerIOCP)
            PostQueuedCompletionStatus(g_hServerIOCP, 0, 0, NULL);
    }
}

BOOL NTService_SetStatus(SERVICE_STATUS_HANDLE hStatus, DWORD dwState)
{
    g_ServiceStatus.dwCurrentState = dwState;
    return SetServiceStatus(hStatus, &g_ServiceStatus);
}

// BUG(FWAPP-015): ServerApp_PostQuit posts WM_CLOSE here too (bIOCPMode is never set), so a quit
// request of the application does not end the service loop.
void WINAPI NTService_ServiceMain(DWORD, LPSTR*)
{
    if (!g_pServerApp)
        return;
    g_pServerApp->m_pfnLog = NTService_ReportEvent;
    if (!g_pServerApp->Initialize())
    {
        g_pServerApp->Shutdown();
        ServerApp_WriteStartupErrorFile(g_ServerGlobals.szServiceName, "InitializeModule fail");
        return;
    }
    SERVICE_STATUS_HANDLE hStatus =
        RegisterServiceCtrlHandlerA(g_ServerGlobals.szServiceName, NTService_CtrlHandler);
    if (!hStatus)
    {
        ServerApp_WriteStartupErrorFile(g_ServerGlobals.szServiceName, "hServiceStatus is NULL");
        return;
    }
    g_hServiceEventLog = RegisterEventSourceA(NULL, g_ServerGlobals.szServiceName);
    if (!g_hServiceEventLog)
    {
        ServerApp_WriteStartupErrorFile(g_ServerGlobals.szServiceName,
                                        "hLogEventSource is NULL");
        return;
    }

    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 0;
    if (!NTService_SetStatus(hStatus, SERVICE_START_PENDING))
        NTService_ReportEvent(0x2000000, "SetServiceState Error : START_PENDING");
    if (!g_pServerApp->InitNetEngine() || !g_pServerApp->RegisterServerTasks())
    {
        g_pServerApp->Shutdown();
        return;
    }
    g_hServerIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!NTService_SetStatus(hStatus, SERVICE_RUNNING))
    {
        NTService_ReportEvent(0x2000000, "SetServiceState Error : START_RUNNING");
        return;
    }
    if (!g_pServerApp->RequestServerCertification())
    {
        g_pServerApp->Shutdown();
        return;
    }

    DWORD dwBytes = 0;
    ULONG_PTR ulKey;
    LPOVERLAPPED pOverlapped = NULL;
    while (GetQueuedCompletionStatus(g_hServerIOCP, &dwBytes, &ulKey, &pOverlapped, INFINITE)
           && dwBytes)
    {
        g_pServerApp->OnControlMsg((CMsg*)pOverlapped);
        dwBytes = 0;
        pOverlapped = NULL;
    }
    g_pServerApp->Shutdown();
    CloseHandle(g_hServerIOCP);
    NTService_SetStatus(hStatus, SERVICE_STOPPED);
    DeregisterEventSource(g_hServiceEventLog);
}

static bool IsArg(const char* pszArg, const char* pszCommand)
{
    int nCommand = lstrlenA(pszCommand);
    int nArg = lstrlenA(pszArg);
    return CompareStringA(0x400, NORM_IGNORECASE | NORM_IGNOREKANATYPE, pszArg, nArg,
                          pszCommand, nCommand) == CSTR_EQUAL;
}

NTSERVICE_COMMAND NTService_ParseCommandLine(char* pszCmdLine, std::vector<char*>* pArgs)
{
    char* pszContext = NULL;
    for (char* pszToken = strtok_s(pszCmdLine, " ", &pszContext); pszToken;
         pszToken = strtok_s(NULL, " ", &pszContext))
    {
        pArgs->push_back(pszToken);
    }
    if (pArgs->empty())
        return NTSERVICE_CMD_DISPATCH;
    if (IsArg((*pArgs)[0], "-install") && pArgs->size() == 3)
        return NTSERVICE_CMD_INSTALL;
    if (IsArg((*pArgs)[0], "-uninstall"))
        return NTSERVICE_CMD_UNINSTALL;
    return NTSERVICE_CMD_INVALID;
}

static int ServiceMessage(const char* pszText)
{
    MessageBoxA(NULL, pszText, g_ServerGlobals.szServiceName, 0);
    return -1;
}

static int InstallService(const char* pszAccount, const char* pszPassword)
{
    SC_HANDLE hManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hManager)
        return ServiceMessage("cannot open service manager");

    char szPath[MAX_PATH];
    GetModuleFileNameA(g_ServerGlobals.hInstance, szPath, MAX_PATH);
    // Formatted but not used.
    char szArgs[MAX_PATH];
    if (StringCchPrintfA(szArgs, MAX_PATH, " %s", g_ServerGlobals.szServiceName))
    {
        BSAssertFail();
        return -1;
    }
    if (!StrCmpI_Locale(pszAccount, "NULL"))
        pszAccount = NULL;
    if (!StrCmpI_Locale(pszPassword, "NULL"))
        pszPassword = NULL;

    SC_HANDLE hService = CreateServiceA(hManager, g_ServerGlobals.szServiceName,
                                        g_ServerGlobals.szServiceName, SERVICE_ALL_ACCESS,
                                        SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS,
                                        SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, szPath, NULL,
                                        NULL, NULL, pszAccount, pszPassword);
    if (!hService)
    {
        char szText[MAX_PATH];
        if (StringCchPrintfA(szText, MAX_PATH, "cannot install service : 0x%x", GetLastError()))
        {
            BSAssertFail();
            return -1;
        }
        MessageBoxA(NULL, szText, g_ServerGlobals.szServiceName, 0);
        CloseServiceHandle(hManager);
        return -1;
    }
    CloseServiceHandle(hService);
    CloseServiceHandle(hManager);
    MessageBoxA(NULL, "service installed successfully", g_ServerGlobals.szServiceName, 0);
    return 1;
}

// BUG(FWAPP-016): a failed DeleteService leaks both handles, and the successful uninstall
// returns -1 like the failures.
static int UninstallService()
{
    SC_HANDLE hManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hManager)
        return ServiceMessage("cannot open service manager");
    SC_HANDLE hService = OpenServiceA(hManager, g_ServerGlobals.szServiceName,
                                      DELETE | SERVICE_STOP);
    if (!hService)
    {
        CloseServiceHandle(hManager);
        return ServiceMessage("cannot open service");
    }
    SERVICE_STATUS status;
    if (!ControlService(hService, SERVICE_CONTROL_STOP, &status))
    {
        DWORD dwError = GetLastError();
        if (dwError != ERROR_SERVICE_NOT_ACTIVE
            && (dwError != ERROR_SERVICE_CANNOT_ACCEPT_CTRL
                || status.dwCurrentState != SERVICE_STOP_PENDING))
        {
            CloseServiceHandle(hManager);
            CloseServiceHandle(hService);
            return ServiceMessage("cannot uninstall service : cannot stop service");
        }
    }
    if (!DeleteService(hService))
    {
        char szText[MAX_PATH];
        if (StringCchPrintfA(szText, MAX_PATH,
                             "cannot uninstall service : cannot delete service : 0x%x",
                             GetLastError()))
        {
            BSAssertFail();
            return -1;
        }
        return ServiceMessage(szText);
    }
    CloseServiceHandle(hService);
    CloseServiceHandle(hManager);
    return ServiceMessage("service uninstalled successfully");
}

int NTService_HandleCommandLine()
{
    std::vector<char*> vecArgs;
    switch (NTService_ParseCommandLine(g_ServerGlobals.szCommandLine, &vecArgs))
    {
    case NTSERVICE_CMD_DISPATCH:
        {
            SERVICE_TABLE_ENTRYA aTable[2];
            aTable[0].lpServiceName = g_ServerGlobals.szServiceName;
            aTable[0].lpServiceProc = NTService_ServiceMain;
            aTable[1].lpServiceName = NULL;
            aTable[1].lpServiceProc = NULL;
            if (!StartServiceCtrlDispatcherA(aTable))
            {
                ServerApp_WriteStartupErrorFile(g_ServerGlobals.szServiceName,
                                                "StartServiceCtrlDispatcher error");
            }
            return 1;
        }

    case NTSERVICE_CMD_INSTALL:
        return InstallService(vecArgs[1], vecArgs[2]);

    case NTSERVICE_CMD_UNINSTALL:
        return UninstallService();

    default:
        {
            char szText[MAX_PATH];
            if (StringCchPrintfA(szText, MAX_PATH, "invalid command : %s", vecArgs[0]))
            {
                BSAssertFail();
                return -1;
            }
            return ServiceMessage(szText);
        }
    }
}
