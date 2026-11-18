#include "ServerFramework.h"

#include "BSNet/Msg.h"
#include "ServerFramework/ServerApp.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerFrameWindow.h"
#include "ServerFramework/ServerUiExtern.h"

ServerFramework::CServerApp* g_pServerApp = NULL;
HMODULE g_hServerFrameworkRes = NULL;
DWORD g_dwSelfDivisionID = 0;
HWND g_hServerFrameWnd = NULL;
HANDLE g_hServerIOCP = NULL;
HANDLE g_hServerConsole = NULL;

int StrCmpI_Locale(const char* psz1, const char* psz2)
{
    int n2 = lstrlenA(psz2);
    int n1 = lstrlenA(psz1);
    return CompareStringA(0x400, NORM_IGNORECASE | NORM_IGNOREKANATYPE, psz1, n1, psz2, n2) - 2;
}

BOOL ServerApp_PostQuit()
{
    if (g_ServerGlobals.bIOCPMode)
        return PostQueuedCompletionStatus(g_hServerIOCP, 0, 0, NULL);
    return PostMessageA(g_hServerFrameWnd, WM_CLOSE, 0, 0);
}

void ServerApp_CustomDump()
{
    g_pServerApp->CustomDump();
}

void ServerApp_OnFrameMessage9000(LPARAM lParam)
{
    g_pServerApp->OnControlMsg((CMsg*)lParam);
}

#ifdef JMX_FRAMEWORK_2011
void ServerApp_OnFrameCommand()
{
    g_pServerApp->OnFrameCommand();
}

void ServerApp_OnSuspectedUserCommand(DWORD dwCommand)
{
    g_pServerApp->OnSuspectedUserCommand(0, dwCommand);
}
#endif

ServerFramework::CServerMoniterView* ServerFrameWindow_GetMonitorView()
{
    return g_pServerFrameWindow->m_pMoniterView;
}

BOOL SelfDivision_IsInGroup1()
{
    return g_dwSelfDivisionID == 6 || g_dwSelfDivisionID == 7 || g_dwSelfDivisionID == 12
        || g_dwSelfDivisionID == 8;
}

BOOL SelfDivision_IsInGroup2()
{
    return g_dwSelfDivisionID == 1 || g_dwSelfDivisionID == 4 || g_dwSelfDivisionID == 11
        || g_dwSelfDivisionID == 19 || g_dwSelfDivisionID == 20 || g_dwSelfDivisionID == 18
        || g_dwSelfDivisionID == 2 || g_dwSelfDivisionID == 31;
}

BOOL SelfDivision_IsInGroup3()
{
    return g_dwSelfDivisionID == 5 || g_dwSelfDivisionID == 9 || g_dwSelfDivisionID == 26
        || g_dwSelfDivisionID == 10 || g_dwSelfDivisionID == 3 || g_dwSelfDivisionID == 27
        || g_dwSelfDivisionID == 34;
}
