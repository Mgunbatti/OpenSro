#pragma once

#include <string>

#include "BSNet/UdpPing.h"
#include "ServerFramework/ServerProcessMain.h"

struct CERT_MODULE;

// Client and server packets of GatewayServer.
enum
{
    GATEWAY_MSG_VERSION_REQ         = 0x6100,   // BYTE content, string module, DWORD version
    GATEWAY_MSG_VERSION_ACK         = 0xA100,   // massive
    GATEWAY_MSG_SHARD_LIST_REQ      = 0x6101,
    GATEWAY_MSG_SHARD_LIST_ACK      = 0xA101,
    GATEWAY_MSG_LOGIN_REQ           = 0x6102,   // the certification part
    GATEWAY_MSG_LOGIN_ACK           = 0xA102,
    GATEWAY_MSG_NOTICE_REQ          = 0x6104,   // [BYTE article type]
    GATEWAY_MSG_NOTICE_ACK          = 0xA104,   // massive
    GATEWAY_MSG_SHARD_USERS         = 0x6105,   // BYTE n, n * (WORD shard, WORD users)
    GATEWAY_MSG_FARM_MANAGER_REQ    = 0x6106,
    GATEWAY_MSG_FARM_MANAGER_ACK    = 0xA106,
    GATEWAY_MSG_FIND_USER_ACK       = 0xA200,   // GlobalManager, relay answer
    GATEWAY_MSG_LOGIN_GRANTED_ACK   = 0xA203,   // the certification part
    GATEWAY_MSG_AGENT_LOGIN_REQ     = 0x6203,   // relayed to the AgentServer
    GATEWAY_MSG_MODULE_VERSION_ACK  = 0xA306,   // massive, from the DivisionManager
    GATEWAY_MSG_RELOAD_ARTICLE_REQ  = 0x6307,   // relayed
    GATEWAY_MSG_RELOAD_ARTICLE_ACK  = 0xA307,
    GATEWAY_MSG_PATCH_DONE_REQ      = 0x6308,   // relayed
    GATEWAY_MSG_PATCH_DONE_ACK      = 0xA309,
    GATEWAY_MSG_SHARD_SERVICE_REQ   = 0x6312,   // relayed, WORD shard (0: all)
    GATEWAY_MSG_SHARD_SERVICE_ACK   = 0xA312,
    GATEWAY_MSG_IBUV_ACK            = 0xA321,   // the certification part
    GATEWAY_MSG_IBUV_ANSWER_REQ     = 0x6323,   // the certification part
    GATEWAY_MSG_IBUV_ANSWER_ACK     = 0xA323,
    GATEWAY_MSG_VALIDATION_FAIL_REQ = 0x6111,   // relayed to the GlobalManager
    GATEWAY_MSG_VALIDATION_OK_REQ   = 0x6112,
    GATEWAY_MSG_VALIDATION_FAIL_ACK = 0xA111,
    GATEWAY_MSG_VALIDATION_OK_ACK   = 0xA112,
};

// Main process of GatewayServer (task SERVER_TASK_MAIN). Besides the framework packets it runs
// the client login: version check, shard list, notices, certification and the hand-over to
// the AgentServer. It also owns the UDP ping echo service.
class CGatewayMainProcess : public ServerFramework::CServerProcessMain
{
    BS_DECLARE_DYNCREATE(CGatewayMainProcess)                                   // 0

public:
    // 0x403980
    CGatewayMainProcess();
    // 0x403a10
    virtual ~CGatewayMainProcess();                                             // 1

    // InitDispatcher (0x407370): base initialisation, then the UDP ping service. FALSE only
    // when the base fails.
    virtual BOOL Initialize(void* pOwner, void* pParam);                       // 2
    // Shutdown (0x407490): stops the UDP ping service, then the base.
    virtual int SignalStop();                                                   // 4
    // InitMsgHandlers (0x405030): the base handlers and the gateway opcodes.
    virtual void InitMsgHandlers();                                             // 6
    // OnRelayMsgError (0x406000): a relay of a login failed: 0xA102 {2, 6} to the client.
    virtual void OnRelayFailed(WORD wOpcode, DWORD dwContext);                  // 17
    // OnMsg_1001_SessionCreated (0x406500): counted for the monitor graph.
    virtual void OnSessionCreated(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                  ServerFramework::CMassiveMsg* pMassive);       // 30
    // OnMsg_1002_SessionLost (0x406530): counted; the session's pending captcha is dropped.
    virtual void OnSessionLost(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                               ServerFramework::CMassiveMsg* pMassive);          // 31
    // vf43 (0x406140): a shard body changed its state: the ready flags of the shards.
    virtual void OnServerBodyStateChanged(SERVER_BODY* pBody, int nOldState);  // 43

    // 0x6100 (0x4051a0)
    void OnMsg_6100(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0x6101 (0x405800)
    void OnMsg_6101(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0x6102 (0x405910, the certification part)
    void OnMsg_6102(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0xA200 (0x405cc0): context = CERTIFY_PARAM
    void OnMsg_A200(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0xA203 (0x405e50, the certification part)
    void OnMsg_A203(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0xA306 (0x406160)
    void OnMsg_A306(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0x6307 (0x406340)
    void OnMsg_6307(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0x6104 (0x406380)
    void OnMsg_6104(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0x6105 (0x406400)
    void OnMsg_6105(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0x6308 (0x406580)
    void OnMsg_6308(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0x6312 (0x406610)
    void OnMsg_6312(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0x6106 (0x405740)
    void OnMsg_6106(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0xA321 (0x406680, the certification part)
    void OnMsg_A321(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0x6323 (0x406910, the certification part)
    void OnMsg_6323(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0xA111 (0x4070a0): context = CERTIFY_PARAM
    void OnMsg_A111(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0xA112 (0x406e30): context = CERTIFY_PARAM
    void OnMsg_A112(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);

public:
    CUdpPingServer  m_UdpPing;      // +400C8
};

C_ASSERT(FIELD_OFFSET(CGatewayMainProcess, m_UdpPing) == 0x400C8);
C_ASSERT(sizeof(CGatewayMainProcess) == 0x400DC);

// ReturnConst_0 (0x405190): 0x6100 and 0x6102 skip the packet and close the session when this
// is set. Always 0.
int GatewayMainProcess_IsClientRequestBlocked();

// ClientVersion_IsBelowRequired (0x404f30): the client version is older than the module's
// "LastFullVersion_<module>"; false for modules without one.
bool ClientVersion_IsBelowRequired(CERT_MODULE* pModule, DWORD dwVersion);

// String_ReplaceQuotesWithSpace (0x4074b0): replaces ' and " (single-byte characters, walked
// with CharNextA) with spaces.
void String_ReplaceQuotesWithSpace(std::string* pstr);
