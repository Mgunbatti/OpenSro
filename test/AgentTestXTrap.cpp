// Stand-in for XTrap4Server.dll used by AgentTest: records the calls of AgentServer and answers
// with fixed data. Built twice: AgentTestXTrap.dll with every export and (FAKE_XTRAP_MISSING)
// AgentTestXTrapMissing.dll without XTrap_S_RecvGamePacket.

#include <windows.h>
#include <string.h>

struct FAKE_XTRAP_STATE
{
    DWORD   nStart;
    DWORD   adwStartArg[2];
    BYTE    abMapFirst[2];          // first byte of each map block
    void*   pMap;
    void*   pStartReserved;
    DWORD   nSetActiveCode;
    DWORD   dwActiveCode;
    DWORD   dwActiveCodeResult;     // returned by XTrap_S_SetActiveCode
    DWORD   nSessionInit;
    DWORD   dwSessionInitResult;    // returned by XTrap_S_SessionInit
    DWORD   nStep1;
    DWORD   nStep3;
    void*   pStep3Session;
    BYTE    abStep3[1024];
    DWORD   dwStep1Result;          // returned by XTrap_CS_Step1
};

static FAKE_XTRAP_STATE s_State;

extern "C" __declspec(dllexport) FAKE_XTRAP_STATE* __cdecl FakeXTrap_GetState()
{
    return &s_State;
}

extern "C" __declspec(dllexport) DWORD __cdecl XTrap_S_Start(DWORD dwTimeOut, DWORD dwMapCount,
                                                            void* pMapFile, void* pReserved)
{
    ++s_State.nStart;
    s_State.adwStartArg[0] = dwTimeOut;
    s_State.adwStartArg[1] = dwMapCount;
    s_State.pMap = pMapFile;
    s_State.pStartReserved = pReserved;
    s_State.abMapFirst[0] = ((BYTE*)pMapFile)[0];
    s_State.abMapFirst[1] = ((BYTE*)pMapFile)[0x32C8];
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl XTrap_S_SessionInit(DWORD, DWORD, void*,
                                                                  void* pSession)
{
    ++s_State.nSessionInit;
    memset(pSession, (BYTE)(0xA0 + s_State.nSessionInit), 320);
    return s_State.dwSessionInitResult;
}

extern "C" __declspec(dllexport) DWORD __cdecl XTrap_S_SetActiveCode(DWORD dwCode)
{
    ++s_State.nSetActiveCode;
    s_State.dwActiveCode = dwCode;
    return s_State.dwActiveCodeResult;
}

// The challenge: byte i is the session's first byte plus i.
extern "C" __declspec(dllexport) DWORD __cdecl XTrap_CS_Step1(void* pSession, void* pBuffer)
{
    ++s_State.nStep1;
    BYTE b = ((BYTE*)pSession)[0];
    for (int i = 0; i < 1024; ++i)
        ((BYTE*)pBuffer)[i] = (BYTE)(b + i);
    return s_State.dwStep1Result;
}

extern "C" __declspec(dllexport) DWORD __cdecl XTrap_CS_Step3(void* pSession, void* pBuffer)
{
    ++s_State.nStep3;
    s_State.pStep3Session = pSession;
    memcpy(s_State.abStep3, pBuffer, 1024);
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl XTrap_S_SessionReset() { return 0; }
extern "C" __declspec(dllexport) DWORD __cdecl XTrap_S_SetModuleOn() { return 0; }
extern "C" __declspec(dllexport) DWORD __cdecl XTrap_S_SetModuleOff() { return 0; }
extern "C" __declspec(dllexport) DWORD __cdecl XTrap_S_SetOption() { return 0; }
extern "C" __declspec(dllexport) DWORD __cdecl XTrap_S_SaveLog() { return 0; }
extern "C" __declspec(dllexport) DWORD __cdecl XTrap_CS_Loading() { return 0; }
extern "C" __declspec(dllexport) DWORD __cdecl XTrap_S_SendGamePacket() { return 0; }
#ifndef FAKE_XTRAP_MISSING
extern "C" __declspec(dllexport) DWORD __cdecl XTrap_S_RecvGamePacket() { return 0; }
#endif

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
