#include "AgentServer.h"

// _WinMain@16 (0x401480)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int)
{
    g_pServerApp = &g_AgentServer;
    return ServerApp_Startup(hInstance, lpCmdLine);
}
