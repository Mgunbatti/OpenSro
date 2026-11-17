#include "DownloadServer.h"

// _WinMain@16 (0x401330)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int)
{
    g_pServerApp = &g_DownloadServer;
    return ServerApp_Startup(hInstance, lpCmdLine);
}
