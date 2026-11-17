#include "MachineManager.h"

// _WinMain@16 (0x4013e0)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int)
{
    g_pServerApp = &g_MachineManager;
    return ServerApp_Startup(hInstance, lpCmdLine);
}
