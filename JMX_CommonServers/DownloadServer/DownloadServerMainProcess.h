#pragma once

#include "ServerFramework/ServerProcessMain.h"

// Server packets of DownloadServer (the clients use the framework's 0x6004 / 0xA004).
enum
{
    DOWNLOAD_MSG_MODULE_VERSION_ACK = 0xA306,   // massive, from the DivisionManager
    DOWNLOAD_MSG_PATCH_RELAY_REQ    = 0x6305,   // massive: WORD n, n * MODULE_VERSION_FILE
    DOWNLOAD_MSG_PATCH_RELAY_ACK    = 0xA305,   // BYTE 1
    DOWNLOAD_MSG_PATCH_RESET        = 0x6306,   // to the GlobalManager when the cord is up
    DOWNLOAD_MSG_PATCH_DONE_REQ     = 0x6308,   // relayed
    DOWNLOAD_MSG_PATCH_DONE_ACK     = 0xA309,   // BYTE 1
};

// Main process of DownloadServer (task SERVER_TASK_MAIN): the framework packets, the session
// limit and the patch relay from the GlobalManager.
class CDownloadServerMainProcess : public ServerFramework::CServerProcessMain
{
    BS_DECLARE_DYNCREATE(CDownloadServerMainProcess)                            // 0

public:
    // 0x4010c0
    CDownloadServerMainProcess();
    // 0x401120
    virtual ~CDownloadServerMainProcess();                                      // 1

    // InitMsgHandlers (0x401580): the base handlers, 0xA306, 0x6305 and 0x6308.
    virtual void InitMsgHandlers();                                             // 6
    // OnSessionCreated_EnforceUserLimit (0x4014b0): with a USER_LIMIT, an accepted session
    // that makes the engine's session count exceed it is closed instead of being set up.
    virtual void OnSessionCreated(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                  ServerFramework::CMassiveMsg* pMassive);       // 30
    // OnGlobalManagerCord_Send6306 (0x4015d0): the cord to the GlobalManager is up: the patch
    // file data is dropped and 0x6306 asks the GlobalManager for it again.
    virtual void OnCordEstablished(SERVER_CORD* pCord, SERVER_BODY* pPeer);    // 41
    // nullsub_12 (0x401a60): empty.
    virtual void OnFileFailed(DWORD dwSessionID);                              // 48

    // 0xA306 (0x401640): the module version data of the DivisionManager, as in GatewayServer.
    virtual void OnMsg_A306(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            ServerFramework::CMassiveMsg* pMassive);             // 49
    // OnMsg_6305_impl_StartPatchRelay (0x401820)
    virtual void OnMsg_6305(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            ServerFramework::CMassiveMsg* pMassive);             // 50
    // OnMsg_6308_impl (0x401a70)
    virtual void OnMsg_6308(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            ServerFramework::CMassiveMsg* pMassive);             // 51

public:
    WORD    m_wPatchFileCount;      // +400C8 files of the last 0x6305
    WORD    m_w400CA;               // +400CA
};

C_ASSERT(FIELD_OFFSET(CDownloadServerMainProcess, m_wPatchFileCount) == 0x400C8);
C_ASSERT(sizeof(CDownloadServerMainProcess) == 0x400CC);
