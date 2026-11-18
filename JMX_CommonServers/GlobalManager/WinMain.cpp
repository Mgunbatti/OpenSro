#include "GlobalManager.h"

// _WinMain@16 (0x40bf60)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int)
{
    g_pServerApp = &g_GlobalManager;
    return ServerApp_Startup(hInstance, lpCmdLine);
}
