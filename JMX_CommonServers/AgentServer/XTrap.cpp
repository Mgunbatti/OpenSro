#include "XTrap.h"

#include <stdio.h>
#include <string.h>
#include <list>

#include "User.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerDataExtern.h"

PFN_XTRAP_S_START           g_pfnXTrap_S_Start;
PFN_XTRAP_S_SESSIONINIT     g_pfnXTrap_S_SessionInit;
PFN_XTRAP_S_SETACTIVECODE   g_pfnXTrap_S_SetActiveCode;
PFN_XTRAP_CS_STEP           g_pfnXTrap_CS_Step1;
PFN_XTRAP_CS_STEP           g_pfnXTrap_CS_Step3;
FARPROC                     g_pfnXTrap_S_SessionReset;
FARPROC                     g_pfnXTrap_S_SetModuleOn;
FARPROC                     g_pfnXTrap_S_SetModuleOff;
FARPROC                     g_pfnXTrap_S_SetOption;
FARPROC                     g_pfnXTrap_S_SaveLog;
FARPROC                     g_pfnXTrap_CS_Loading;
FARPROC                     g_pfnXTrap_S_SendGamePacket;
FARPROC                     g_pfnXTrap_S_RecvGamePacket;
HMODULE                     g_hXTrapModule;

BYTE g_abXTrapMap[XTRAP_MAP_COUNT][XTRAP_MAP_SIZE];

DWORD g_dwXTrapLastBroadcastTick;

// BUG(AS-004): XTrap_S_SetActiveCode (and XTrap_S_SaveLog) are not among the checked procedures,
// although XTrap_Initialize calls XTrap_S_SetActiveCode.
DWORD XTrap_LoadDll()
{
    char szDll[17];
    memcpy(szDll, "XTrap4Server.Dll", sizeof(szDll));
    g_hXTrapModule = LoadLibraryA(szDll);
    if (!g_hXTrapModule)
        return GetLastError() | XTRAP_LOAD_FAILED;

    g_pfnXTrap_S_Start = (PFN_XTRAP_S_START)GetProcAddress(g_hXTrapModule, "XTrap_S_Start");
    g_pfnXTrap_S_SessionInit =
        (PFN_XTRAP_S_SESSIONINIT)GetProcAddress(g_hXTrapModule, "XTrap_S_SessionInit");
    g_pfnXTrap_S_SetActiveCode =
        (PFN_XTRAP_S_SETACTIVECODE)GetProcAddress(g_hXTrapModule, "XTrap_S_SetActiveCode");
    g_pfnXTrap_CS_Step1 = (PFN_XTRAP_CS_STEP)GetProcAddress(g_hXTrapModule, "XTrap_CS_Step1");
    g_pfnXTrap_CS_Step3 = (PFN_XTRAP_CS_STEP)GetProcAddress(g_hXTrapModule, "XTrap_CS_Step3");
    g_pfnXTrap_S_SessionReset = GetProcAddress(g_hXTrapModule, "XTrap_S_SessionReset");
    g_pfnXTrap_S_SetModuleOn = GetProcAddress(g_hXTrapModule, "XTrap_S_SetModuleOn");
    g_pfnXTrap_S_SetModuleOff = GetProcAddress(g_hXTrapModule, "XTrap_S_SetModuleOff");
    g_pfnXTrap_S_SetOption = GetProcAddress(g_hXTrapModule, "XTrap_S_SetOption");
    g_pfnXTrap_S_SaveLog = GetProcAddress(g_hXTrapModule, "XTrap_S_SaveLog");
    g_pfnXTrap_CS_Loading = GetProcAddress(g_hXTrapModule, "XTrap_CS_Loading");
    g_pfnXTrap_S_SendGamePacket = GetProcAddress(g_hXTrapModule, "XTrap_S_SendGamePacket");
    g_pfnXTrap_S_RecvGamePacket = GetProcAddress(g_hXTrapModule, "XTrap_S_RecvGamePacket");

    if (g_pfnXTrap_S_Start && g_pfnXTrap_S_SessionInit && g_pfnXTrap_CS_Step1
        && g_pfnXTrap_CS_Step3 && g_pfnXTrap_S_SessionReset && g_pfnXTrap_S_SetModuleOn
        && g_pfnXTrap_S_SetModuleOff && g_pfnXTrap_S_SetOption && g_pfnXTrap_CS_Loading
        && g_pfnXTrap_S_SendGamePacket && g_pfnXTrap_S_RecvGamePacket)
    {
        return 0;
    }
    return XTRAP_LOAD_MISSING_PROC;
}

DWORD XTrap_Initialize()
{
    FILE* pFile = NULL;
    if (fopen_s(&pFile, "map1.cs3", "rb"))
    {
        LogPrintf(0x2000000, "[For Xtrap] Failed to open XTrap map1 file!!");
        return 1;
    }
    fread(g_abXTrapMap[0], XTRAP_MAP_SIZE, 1, pFile);
    fclose(pFile);
    if (fopen_s(&pFile, "map2.cs3", "rb"))
    {
        LogPrintf(0x2000000, "[For Xtrap] Failed to open XTrap map2 file!!");
        return 1;
    }
    fread(g_abXTrapMap[1], XTRAP_MAP_SIZE, 1, pFile);
    fclose(pFile);
    if (XTrap_LoadDll())
    {
        LogPrintf(0x2000000, "[For Xtrap] Failed to XTrap Load DLL!!");
        return 1;
    }
    if (g_pfnXTrap_S_Start(XTRAP_TIMEOUT, XTRAP_MAP_COUNT, g_abXTrapMap, NULL))
        return 1;
    return g_pfnXTrap_S_SetActiveCode(XTRAP_ACTIVE_CODE);
}

int XTrap_IsStepDue(sUser* pUser)
{
    if (!pUser)
        return 0;
    if (GetTickCount() - pUser->dwXTrapTick > XTRAP_STEP_INTERVAL)
    {
        pUser->dwXTrapTick = GetTickCount();
        return 1;
    }
    return 0;
}

// The user's AgentServer.exe is patched here (AGENT_XTRAP_DETECTION_OFF builds that variant):
// the check of the XTrap_CS_Step1 result is an unconditional jump, so a detected user is never
// collected, and the call made for each collected user is replaced by NOPs.
// RECONSTRUCTED: the jump at 0x4046e5 is "eb 7e" after "test ebx, ebx" (ebx = the Step1 result);
// the original is taken as "74 7e" (jz: skip on 0, the XTrap success value).
// RECONSTRUCTED: the NOPed instruction at 0x40479b is one 5-byte instruction after
// "mov esi, [node + 8]" (the collected user) without argument pushes, i.e. a direct call with the
// user in a register. No such helper is left in the binary; the user is taken to be
// disconnected as the module does with every misbehaving client (DisconnectSession(session, 1),
// e.g. the traffic filter, 0x620D, 0x6905).
void XTrap_BroadcastStep1()
{
    BYTE abChallenge[1 + XTRAP_PACKET_SIZE];
    abChallenge[0] = 0;
    memset(abChallenge + 1, 0, XTRAP_PACKET_SIZE);
    std::list<sUser*> listDetected;
    for (AgentUserMap::iterator it = g_UserManager.m_mapUser.begin();
         it != g_UserManager.m_mapUser.end(); ++it)
    {
        sUser* pUser = it->second;
        if (!pUser)
        {
            LogPrintf(0, "[For Xtrap] Broadcasting Target User Is NULL");
            continue;
        }
        if (XTrap_IsStepDue(pUser) != 1)
            continue;
        abChallenge[0] = 1;
        DWORD dwResult = g_pfnXTrap_CS_Step1(pUser->abXTrapSession, abChallenge + 1);
        CMsg* pMsg = g_pBSNet->AllocMsg(TRUE);
        pMsg->SetOpcode(0x2113);
        BYTE byType = 1;
        pMsg->WriteBytes(&byType, 1);
        pMsg->WriteBytes(abChallenge, sizeof(abChallenge));
        g_pBSNet->SendMsg(pUser->dwSessionID, pMsg);
        g_pBSNet->ReleaseMsg(pMsg);
#ifdef AGENT_XTRAP_DETECTION_OFF
        (void)dwResult;
#else
        if (!dwResult)
            continue;
        LogPrintf(0x2000000, "[For Xtrap] Hack Detect!! %s", pUser->strID.c_str());
        LogPrintf(0x2000001, "[For Xtrap] Hack Detect!! %s", pUser->strID.c_str());
        listDetected.push_back(pUser);
#endif
    }
#ifndef AGENT_XTRAP_DETECTION_OFF
    for (std::list<sUser*>::iterator it = listDetected.begin(); it != listDetected.end(); ++it)
    {
        sUser* pUser = *it;
        g_pBSNet->DisconnectSession(pUser->dwSessionID, 1);
    }
#endif
    listDetected.clear();
}
