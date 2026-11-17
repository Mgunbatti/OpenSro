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

ServerFramework::CServerMoniterView* ServerFrameWindow_GetMonitorView()
{
    return g_pServerFrameWindow->m_pMoniterView;
}
