#include "GatewayServer.h"

// _WinMain@16 (0x403b60)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int)
{
    g_pServerApp = &g_GatewayServer;
    return ServerApp_Startup(hInstance, lpCmdLine);
}
