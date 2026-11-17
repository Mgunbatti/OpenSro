#pragma once

#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerProcess.h"
#include "ServerFramework/Trigger.h"

// 1: CServerProcessMain has the 0x631D handler (virtual slot 40) and registers it, as in the 2008
// framework of GatewayServer, DownloadServer and MachineManager. 0: the 2007 framework of
// FarmManager (JMX_FRAMEWORK_2007) without that slot; the hooks from OnCordEstablished on are one
// slot lower. Every file that sees CServerProcessMain must be compiled with the same value.
#ifndef SERVER_MAIN_PROCESS_631D
#ifdef JMX_FRAMEWORK_2007
#define SERVER_MAIN_PROCESS_631D 0
#else
#define SERVER_MAIN_PROCESS_631D 1
#endif
#endif

namespace ServerFramework
{

class CServerProcessMain;

typedef std::map<DWORD, CMassiveMsg*> MassiveMsgSessionMap;

// Main process of a server module (task SERVER_TASK_MAIN): session events, the server
// architecture (cords, certification, status) and the framework packets. Modules derive their
// main process from it and override the empty hooks (slots 41..48) and the handlers.
class CServerProcessMain : public CServerProcessBase
{
    BS_DECLARE_DYNCREATE(CServerProcessMain)                                    // 0

public:
    // Uses the timer and overlap jobs; creates the body state trigger and makes it
    // g_pServerBodyStateObserver.
    CServerProcessMain();
    // Deletes the trigger (g_pServerBodyStateObserver keeps pointing to it, FWAPP-009).
    virtual ~CServerProcessMain();                                              // 1

    // Base handlers plus the framework opcodes 0x1001..0x1006, 0x2001, 0x2002, 0x6003, 0xA003,
    // 0x6004, 0xA004, 0x2005, 0x6005, 0x6006, 0xA006, 0x6300, 0x6303, 0x600D, 0x2311, 0x631D (with
    // SERVER_MAIN_PROCESS_631D).
    virtual void InitMsgHandlers();                                             // 6

    // 0x2001 {string module, BYTE not certified}: a peer server introduces itself.
    virtual void OnMsg_2001(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 20
    // 0x2002: ignored.
    virtual void OnMsg_2002(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 21
    // 0x6003 {module, address}: certification request; answers 0xA003 (massive).
    virtual void OnMsg_6003(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 22
    // 0xA003 (massive): the certification answer with the server architecture.
    virtual void OnMsg_A003(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 23
    // 0x6004 {DWORD file ID, DWORD offset}: file request; 0xA004 {0} when it cannot be served.
    virtual void OnMsg_6004(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 24
    // 0xA004 {BYTE 1 ok}: a requested file is complete; requests the next one.
    virtual void OnMsg_A004(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 25
    // 0x2005 (massive): body / cord states of other servers.
    virtual void OnMsg_2005(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 26
    // 0x6005 (massive): state query; answers 0x2005.
    virtual void OnMsg_6005(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 27
    virtual void OnMsg_6006(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 28 empty
    virtual void OnMsg_A006(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 29 empty
    // 0x1001 {session, role}: starts the handshake of accepted sessions (closes them while
    // this server is not certified).
    virtual void OnSessionCreated(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                  CMassiveMsg* pMassive);                       // 30
    // 0x1002 {session, reason}: fails the relays routed through a lost cord, drops partial
    // massive messages and closes the cord.
    virtual void OnSessionLost(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                               CMassiveMsg* pMassive);                          // 31
    // 0x1003 {session, 6}: connected sessions introduce this server with 0x2001.
    virtual void OnSessionHandshaked(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                     CMassiveMsg* pMassive);                    // 32
    virtual void OnMsg_1004(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 33
    virtual void OnMsg_1005(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 34
    virtual void OnMsg_1006(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 35
    // 0x6300 (relayed): answers 0xA300 {1}.
    virtual void OnMsg_6300(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 36
    // 0x6303 {BYTE mode} (relayed): service mode change, answers 0xA303 {result, check, mode}.
    virtual void OnMsg_6303(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 37
    // 0x600D: parts of massive messages, dispatched when complete.
    virtual void OnMsg_600D(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 38
    // 0x2311 {WORD shard, BYTE type, ...}: shard name (type 0) or user limit changed;
    // forwarded to the bodies this server certifies.
    virtual void OnMsg_2311(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 39
#if SERVER_MAIN_PROCESS_631D
    // 0x631D: state 7.
    virtual void OnMsg_631D(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 40
#endif

    // Hooks, empty here (slots 40..47 without SERVER_MAIN_PROCESS_631D).
    virtual void OnCordEstablished(SERVER_CORD* pCord, SERVER_BODY* pPeer);    // 41
    virtual void OnCordClosed(SERVER_CORD* pCord, SERVER_BODY* pPeer);         // 42
    virtual void OnServerBodyStateChanged(SERVER_BODY* pBody, int nOldState);  // 43
    // A session without cord was lost.
    virtual void OnSessionClosed(DWORD dwSessionID);                           // 44
    // 0 when the service mode may change (1: to state 5 needs state 4 and every cord of this
    // server connected; 2: to state 4 needs state 5), 3 otherwise.
    virtual BYTE CheckModeChange(BYTE byMode);                                  // 45
    virtual void OnFileProgress(DWORD dwSessionID, DWORD dwBytes);             // 46
    virtual void OnFileDone(DWORD dwSessionID, DWORD dwParam);                 // 47
    virtual void OnFileFailed(DWORD dwSessionID);                              // 48

    // ClassTrigger_ForwardVf43 (0x4408d0): the trigger's target.
    int ForwardBodyStateChanged(DWORD dwBody, DWORD dwOldState);

public:
    MassiveMsgSessionMap                    m_mapMassive;   // +400B8 messages being received
    CClassTrigger<CServerProcessMain>*      m_pTrigger;     // +400C4
};

C_ASSERT(FIELD_OFFSET(CServerProcessMain, m_mapMassive) == 0x400B8);
C_ASSERT(sizeof(CServerProcessMain) == 0x400C8);
C_ASSERT(sizeof(CClassTrigger<CServerProcessMain>) == 0x10);

// Names of the engine error codes of a lost session (0x4F8720).
extern const char* const g_apszNetEngineError[22];

} // namespace ServerFramework
