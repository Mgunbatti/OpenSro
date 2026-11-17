#include "FarmManager.h"

// _WinMain@16 (0x401380)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int)
{
    g_pServerApp = &g_FarmManager;
    return ServerApp_Startup(hInstance, lpCmdLine);
}
