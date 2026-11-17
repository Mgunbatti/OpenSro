#pragma once

#include <map>
#include <vector>

#include "ServerFramework/ServerProcessMain.h"

struct sUser;

// Packets of AgentServer.
enum
{
    // client
    AGENT_MSG_LOGIN_REQ             = 0x6103,   // DWORD token, string ID, string password,
                                                // BYTE content, 6 bytes MAC
    AGENT_MSG_LOGIN_ACK             = 0xA103,   // {1} or {2, error}
    AGENT_MSG_XTRAP                 = 0x2113,   // {BYTE 1, BYTE 1, 1024} / {BYTE 2, BYTE 2, 1024}
    AGENT_MSG_BILLING_NOTICE        = 0x6902,   // to the client: BYTE, BYTE, DWORD
    // GatewayServer (relayed)
    AGENT_MSG_TOKEN_REQ             = 0x6203,
    AGENT_MSG_TOKEN_ACK             = 0xA203,   // {1, DWORD token, WORD body} or {2}
    // GlobalManager
    AGENT_MSG_USER_LOGIN_REQ        = 0x6201,   // relayed, context = session
    AGENT_MSG_USER_LOGIN_ACK        = 0xA201,
    AGENT_MSG_USER_LOGOUT           = 0x6202,   // massive: WORD n, n * DWORD JID
    AGENT_MSG_USER_LOGOUT_ACK       = 0xA202,   // massive: WORD
    AGENT_MSG_KICK_USERS            = 0x2206,   // WORD n, n * DWORD session
    AGENT_MSG_MOVE_USER_REQ         = 0x6207,   // relayed: DWORD session, DWORD JID, WORD body
    AGENT_MSG_MOVE_USER_ACK         = 0xA207,   // {DWORD JID, 1} or {DWORD JID, 2, error}
    AGENT_MSG_BAN_USER_REQ          = 0x620D,   // relayed: DWORD JID
    AGENT_MSG_BAN_USER_ACK          = 0xA20D,
    AGENT_MSG_BILLING_NOTICE_REQ    = 0x6902,   // relayed: DWORD JID, BYTE, BYTE, DWORD
    AGENT_MSG_BILLING_NOTICE_ACK    = 0xA902,
    AGENT_MSG_BILLING_BAN_REQ       = 0x6905,   // relayed: DWORD JID
    AGENT_MSG_BILLING_BAN_ACK       = 0xA905,
    // game server
    AGENT_MSG_SHARD_LOGIN_REQ       = 0x6204,   // relayed to the user's home body
    AGENT_MSG_SHARD_LOGIN_ACK       = 0xA204,
    AGENT_MSG_SHARD_LOGOUT          = 0x6205,   // DWORD JID, relayed
    AGENT_MSG_SHARD_LOGOUT_ACK      = 0xA205,
    AGENT_MSG_MOVE_TARGET_REQ       = 0x6208,   // relayed to the new body, context = request
    AGENT_MSG_MOVE_TARGET_ACK       = 0xA208,
    AGENT_MSG_RELAY_TO_CLIENT       = 0x2209,   // DWORD session, WORD opcode, data
    AGENT_MSG_RELAY_TO_SESSIONS     = 0x220A,   // data, n * DWORD session, WORD n, WORD opcode
    AGENT_MSG_RELAY_MULTI_DATA      = 0x220E,   // data for the list of 0x220F
    AGENT_MSG_RELAY_MULTI_LIST      = 0x220F,   // WORD opcode, WORD n, n * DWORD session

    // Monitor graph IDs of the application (CAgentServer::OnLocalDataInitialized).
    AGENT_COUNTER_USER              = 0x3020001,
    AGENT_COUNTER_TOKEN             = 0x3020002,
    AGENT_COUNTER_MSG_SENT_COUNT    = 0x3020003,
    AGENT_COUNTER_MSG_SENT_BYTES    = 0x3020004,
};

// The pending multi-session relay of a server session (0x14 bytes): 0x220F collects the opcode
// and the target sessions, 0x220E sends its data to them.
struct AGENT_BROADCAST
{
    WORD                wRealMsgID;     // +0   0 while nothing is collected
    WORD                w2;             // +2
    std::vector<DWORD>  vecTarget;      // +4
};

C_ASSERT(sizeof(AGENT_BROADCAST) == 0x14);

typedef std::map<DWORD, AGENT_BROADCAST*> AgentBroadcastMap;

// A user move request (0x6207) while it waits for the new body's answer (0x14 bytes, the context
// of the 0x6208 relay).
struct AGENT_MOVE_REQUEST
{
    DWORD   dwSessionID;    // +0   session the request came from
    DWORD   dwUserSession;  // +4
    DWORD   dwJID;          // +8
    WORD    wBodyID;        // +C   new target body
    WORD    wE;             // +E
    DWORD   dwRelayID;      // +10  of the request
};

C_ASSERT(sizeof(AGENT_MOVE_REQUEST) == 0x14);

// 0x4E9AAC / 0x4E9AB0: messages and bytes relayed to clients since the monitor graph last read
// them.
extern DWORD g_dwAgentMsgSentCount;
extern DWORD g_dwAgentMsgSentBytes;

// Main process of AgentServer (task SERVER_TASK_MAIN). Besides the framework packets it keeps
// the login tokens of the gateway, logs the clients in (with the GlobalManager and the game
// server), relays their packets to the game server and the game server's packets to them, and
// runs the XTrap challenges and the traffic filter.
class CAgentMainProcess : public ServerFramework::CServerProcessMain
{
    BS_DECLARE_DYNCREATE(CAgentMainProcess)                                     // 0

public:
    // 0x4012e0
    CAgentMainProcess();
    // sub_4013A0
    virtual ~CAgentMainProcess();                                               // 1

    // vf02 (0x4017b0): the base initialisation, then the token timeout timer (3 s).
    virtual BOOL Initialize(void* pOwner, void* pParam);                       // 2
    // vf03 (0x404310): the base loop plus the XTrap challenge pass once a second; a handler
    // that throws closes the message's session.
    virtual DWORD Run(volatile LONG* pbStop, int nIndex);                      // 3
    // vf04 (0x4017f0): deletes the pending multi-session relays, then the base.
    virtual int SignalStop();                                                   // 4
    // vf06 (0x401970): the base handlers and the agent opcodes.
    virtual void InitMsgHandlers();                                             // 6
    // vf08 (0x401ac0): while in service, a client session may send only the login packets and
    // packets with bit 0x1000 and without 0x8800 in the opcode; anything else closes it.
    virtual void Dispatch(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                          ServerFramework::CMassiveMsg* pMassive);               // 8
    // vf09 (0x401d00): a packet of a user goes to the game server (traffic filter first).
    virtual void OnUnhandledMsg(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                ServerFramework::CMassiveMsg* pMassive);         // 9
    // vf17 (0x403060): a relay of 0x6201 / 0x6204 / 0x6208 failed.
    virtual void OnRelayFailed(WORD wOpcode, DWORD dwContext);                  // 17
    // vf42 (0x403e80): the cord to a body is gone: its users are dropped and state 4 is set.
    virtual void OnCordClosed(SERVER_CORD* pCord, SERVER_BODY* pPeer);         // 42
    // vf44 (0x402db0): a client session was lost: logout at the GlobalManager and the game
    // server.
    virtual void OnSessionClosed(DWORD dwSessionID);                           // 44

    // sub_401950: the timer callback; timer 2 checks the token timeouts. True.
    bool OnTimer(BYTE byTimerID);

    // The handlers (registered by InitMsgHandlers).
    void OnMsg_6203(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_6103(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_A201(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_A204(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_A202(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_A205(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_2206(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_6207(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_A208(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_2209(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_220A(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_220E(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_220F(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_620D(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_6902(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_6905(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    void OnMsg_2113(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);

    // sub_401C20: the opcodes a client may send before it is a user (0x2001, 0x2002, 0x2110,
    // 0x2113, 0x6103, 0x6110, 0x6314, 0x6316); for 0x600D the inner opcode counts.
    bool IsClientMsgAllowed(CMsg* pMsg, ServerFramework::CMassiveMsg* pMassive);

public:
    AgentBroadcastMap   m_mapBroadcast;     // +400C8 by server session
};

C_ASSERT(FIELD_OFFSET(CAgentMainProcess, m_mapBroadcast) == 0x400C8);
C_ASSERT(sizeof(CAgentMainProcess) == 0x400D4);

// 0x52B200: a second CAgentMainProcess object the module constructs at start-up and destroys at
// exit without using it (the engine creates the real main process from the class record).
extern CAgentMainProcess g_AgentMainProcessUnused;

// sub_401DF0: the "WARNING!! A SUSPECT DETECTED!!!" report (0x2000001) of a refused or faulting
// client message; with AGENT_XTRAP_DETECTION_OFF nothing is logged (see the .cpp).
void AgentMainProcess_ReportSuspect(const WORD* pwOpcode, const DWORD* pdwSessionID);

// sub_402680 (not referenced): logs "Msg[<0xA201 / 0xA204 name>] Failed - <error>".
void AgentMainProcess_LogRegisterFailure(WORD wOpcode, BYTE byError);
