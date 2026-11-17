#pragma once

#include "BSNet/UdpPing.h"
#include "ServerFramework/ServerProcessMain.h"

enum
{
    FARM_MSG_PATCH_RESET = 0x6306,  // to the GlobalManager when the cord is up
};

// Main process of FarmManager (task SERVER_TASK_MAIN): the framework packets and the UDP ping
// echo service. No packets of its own.
class CFarmManagerMainProcess : public ServerFramework::CServerProcessMain
{
    BS_DECLARE_DYNCREATE(CFarmManagerMainProcess)                               // 0

public:
    // 0x4010e0
    CFarmManagerMainProcess();
    // 0x401150 / 0x401170
    virtual ~CFarmManagerMainProcess();                                         // 1

    // vf02 (0x4013a0): opens the UDP ping service (suspended), then the base initialisation;
    // returns the base's result.
    virtual BOOL Initialize(void* pOwner, void* pParam);                       // 2
    // vf03 (0x401450): starts the UDP ping service and logs the result, then runs the base
    // message loop.
    virtual DWORD Run(volatile LONG* pbStop, int nIndex);                      // 3
    // 0x4014a0: stops the UDP ping service, then the base.
    virtual int SignalStop();                                                   // 4
    // vf06 (0x4014c0): the base handlers only.
    virtual void InitMsgHandlers();                                             // 6
    // OnGlobalManagerCord_Send6306 (0x4014d0): the cord to the GlobalManager is up: the patch
    // file data is dropped and 0x6306 asks the GlobalManager for it again. Slot 40 of the 2007
    // framework (SERVER_MAIN_PROCESS_631D=0).
    virtual void OnCordEstablished(SERVER_CORD* pCord, SERVER_BODY* pPeer);    // 40

public:
    CUdpPingServer  m_UdpPing;      // +400C8
};

C_ASSERT(FIELD_OFFSET(CFarmManagerMainProcess, m_UdpPing) == 0x400C8);
C_ASSERT(sizeof(CFarmManagerMainProcess) == 0x400DC);
