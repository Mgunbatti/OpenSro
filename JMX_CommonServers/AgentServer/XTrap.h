#pragma once

#include <windows.h>

struct sUser;

// The XTrap anti-cheat server module (XTrap4Server.dll). AgentServer loads it at run time; the
// two map files map1.cs3 and map2.cs3 of the working directory are handed to XTrap_S_Start and
// to every session (XTrap_S_SessionInit). The main process sends each logged-in client a
// challenge (XTrap_CS_Step1, packet 0x2113) every three minutes and passes the client's answer to
// XTrap_CS_Step3.

enum
{
    XTRAP_TIMEOUT           = 600,          // first argument of XTrap_S_Start / SessionInit
    XTRAP_MAP_COUNT         = 2,
    XTRAP_MAP_SIZE          = 0x32C8,       // bytes read from each map file
    XTRAP_SESSION_SIZE      = 320,          // session buffer of a user (sUser +0x48)
    XTRAP_PACKET_SIZE       = 1024,         // challenge / answer data
    XTRAP_ACTIVE_CODE       = 0x0FFFFFC3,   // XTrap_S_SetActiveCode after the start
    XTRAP_STEP_INTERVAL     = 180000,       // ms between two challenges of a user
    XTRAP_BROADCAST_PERIOD  = 1000,         // ms between two passes over the users

    // XTrap_LoadDll results
    XTRAP_LOAD_FAILED       = 0x10000000,   // | GetLastError() of LoadLibraryA
    XTRAP_LOAD_MISSING_PROC = 0x20000000,
};

// The exports of XTrap4Server.dll (cdecl).
typedef DWORD (__cdecl* PFN_XTRAP_S_START)(DWORD dwTimeOut, DWORD dwMapCount, void* pMapFile,
                                           void* pReserved);
typedef DWORD (__cdecl* PFN_XTRAP_S_SESSIONINIT)(DWORD dwTimeOut, DWORD dwMapCount,
                                                 void* pMapFile, void* pSession);
typedef DWORD (__cdecl* PFN_XTRAP_S_SETACTIVECODE)(DWORD dwActiveCode);
typedef DWORD (__cdecl* PFN_XTRAP_CS_STEP)(void* pSession, void* pBuffer);

// 0x4E9AB4..0x4E9AE4: the procedures (only these five are called), 0x4E9AE8: the module.
extern PFN_XTRAP_S_START            g_pfnXTrap_S_Start;
extern PFN_XTRAP_S_SESSIONINIT      g_pfnXTrap_S_SessionInit;
extern PFN_XTRAP_S_SETACTIVECODE    g_pfnXTrap_S_SetActiveCode;
extern PFN_XTRAP_CS_STEP            g_pfnXTrap_CS_Step1;
extern PFN_XTRAP_CS_STEP            g_pfnXTrap_CS_Step3;
extern FARPROC                      g_pfnXTrap_S_SessionReset;
extern FARPROC                      g_pfnXTrap_S_SetModuleOn;
extern FARPROC                      g_pfnXTrap_S_SetModuleOff;
extern FARPROC                      g_pfnXTrap_S_SetOption;
extern FARPROC                      g_pfnXTrap_S_SaveLog;
extern FARPROC                      g_pfnXTrap_CS_Loading;
extern FARPROC                      g_pfnXTrap_S_SendGamePacket;
extern FARPROC                      g_pfnXTrap_S_RecvGamePacket;
extern HMODULE                      g_hXTrapModule;

// 0x4E34C0: the contents of map1.cs3 and map2.cs3, one block.
extern BYTE g_abXTrapMap[XTRAP_MAP_COUNT][XTRAP_MAP_SIZE];

// 0x4E9AA4: tick of the last challenge pass of the main process.
extern DWORD g_dwXTrapLastBroadcastTick;

// sub_40DE70: loads XTrap4Server.Dll and its procedures. 0 when every procedure the server needs
// was found, XTRAP_LOAD_FAILED | GetLastError() when the DLL cannot be loaded,
// XTRAP_LOAD_MISSING_PROC otherwise.
DWORD XTrap_LoadDll();

// sub_401110: reads the map files, loads the DLL, starts XTrap and sets the active code. 1 when
// a step before XTrap_S_SetActiveCode failed, otherwise the result of XTrap_S_SetActiveCode (0:
// XTrap runs).
DWORD XTrap_Initialize();

// sub_404820: true (and the user's tick restarted) when the user's last challenge is more than
// XTRAP_STEP_INTERVAL ago; false for NULL.
int XTrap_IsStepDue(sUser* pUser);

// sub_404590: sends a challenge (0x2113 {1, 1, step 1 data}) to every user whose step is due.
void XTrap_BroadcastStep1();
