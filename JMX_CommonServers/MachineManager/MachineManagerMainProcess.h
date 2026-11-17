#pragma once

#include "ServerFramework/ServerProcessMain.h"

// Packets of MachineManager, relayed from the GlobalManager. The answers go back with
// SendRelayReply.
enum
{
    MACHINE_MSG_START_PROCESS_REQ   = 0x6301,   // BYTE module ID
    MACHINE_MSG_START_PROCESS_ACK   = 0xA301,   // BYTE 1, or BYTE 2 + BYTE error
    MACHINE_MSG_STOP_PROCESS_REQ    = 0x6302,   // BYTE module ID
    MACHINE_MSG_STOP_PROCESS_ACK    = 0xA302,   // BYTE 1, or BYTE 2 + BYTE error
    MACHINE_MSG_PERF_STATS_REQ      = 0x6304,
    MACHINE_MSG_PERF_STATS_ACK      = 0xA304,   // BYTE 1, 2 * (value, min, max, mean)

    // error byte after the result 2
    MACHINE_ERROR_UNKNOWN_MODULE    = 1,
    MACHINE_ERROR_PROCESS           = 2,        // start: CreateProcess failed; stop: not running
    MACHINE_ERROR_NOT_CERTIFIED     = 3,
};

// Main process of MachineManager (task SERVER_TASK_MAIN).
class CMachineManagerMainProcess : public ServerFramework::CServerProcessMain
{
    BS_DECLARE_DYNCREATE(CMachineManagerMainProcess)                            // 0

public:
    // 0x4010f0
    CMachineManagerMainProcess();
    // 0x401150
    virtual ~CMachineManagerMainProcess();                                      // 1

    // vf02 (0x401400): the base initialisation, its result as TRUE / FALSE.
    virtual BOOL Initialize(void* pOwner, void* pParam);                       // 2
    // InitMsgHandlers (0x401420): the base handlers, 0x6301, 0x6302 and 0x6304.
    virtual void InitMsgHandlers();                                             // 6

    // OnMsg_6301_StartServerProcess (0x401470)
    void OnMsg_6301(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // OnMsg_6302_StopServerProcess (0x4016e0)
    void OnMsg_6302(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // OnMsg_6304_GetPerfStats (0x401800)
    void OnMsg_6304(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
};

C_ASSERT(sizeof(CMachineManagerMainProcess) == 0x400C8);

// BuildModuleExePath (0x401940): <module dir>\<pszName>.exe into pszPath (MAX_PATH). False
// (after an assert) when it does not fit.
bool MachineManager_BuildModuleExePath(char* pszPath, const char* pszName);

// ServerProcess_CloseOrTerminate (0x401620): stops the running module pszName. Its window (class
// and title pszName) gets WM_CLOSE; the process is terminated when it has not ended after
// ShutdownProcessTimeout ms (15000 by default). False when no such process was found.
bool MachineManager_CloseOrTerminateProcess(const char* pszName);
