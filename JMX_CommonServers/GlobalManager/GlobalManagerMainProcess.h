#pragma once

#include <winsock2.h>
#include <windows.h>

#include "ServiceManagerProcess.h"
#include "Billing/IBillingMgr.h"
#include "ServerFramework/ServerProcessMain.h"

class CMsg;

// Packets of the GlobalManager main process (requests come relayed from the AgentServers and the
// shard managers and are answered through the relay).
enum
{
    GM_MSG_USER_CHECK_REQ           = 0x6200,   // DWORD IP, DWORD JID, BYTE
    GM_MSG_USER_CHECK_ACK           = 0xA200,   // {JID, 1} or {JID, 2, error}
    GM_MSG_USER_LOGIN_REQ           = 0x6201,
    GM_MSG_USER_LOGIN_ACK           = 0xA201,   // {JID, 1, IP type, WORD play time} or
                                                // {JID, 2, error}
    GM_MSG_USER_LOGOUT_REQ          = 0x6202,   // massive: WORD n, n * DWORD JID
    GM_MSG_USER_LOGOUT_ACK          = 0xA202,
    GM_MSG_CHECK_CHAR_NAME_REQ      = 0x620B,
    GM_MSG_SHARD_USERS_REQ          = 0x620C,   // massive: WORD shard, WORD n, n users
    GM_MSG_SHARD_USERS_ACK          = 0xA20C,
    GM_MSG_CHAR_CREATE_REQ          = 0x620E,
    GM_MSG_CHAR_CREATE_ACK          = 0xA20E,
    GM_MSG_CHAR_DELETE_REQ          = 0x620F,
    GM_MSG_CHAR_DELETE_ACK          = 0xA20F,
    GM_MSG_CHAR_RENAME_REQ          = 0x6210,
    GM_MSG_CHAR_RENAME_ACK          = 0xA210,
    GM_MSG_VALIDATION_FAILED_REQ    = 0x6111,   // string account, BYTE check
    GM_MSG_VALIDATION_FAILED_ACK    = 0xA111,
    GM_MSG_VALIDATION_PASSED_REQ    = 0x6112,   // string account
    GM_MSG_VALIDATION_PASSED_ACK    = 0xA112,
    GM_MSG_MODULE_VERSION_REQ       = 0x6306,
    GM_MSG_BILLING_6900_REQ         = 0x6900,   // DWORD JID
    GM_MSG_BILLING_6900_ACK         = 0xA900,
    GM_MSG_BILLING_6904_REQ         = 0x6904,
    GM_MSG_BILLING_6904_ACK         = 0xA904,
    GM_MSG_PUNISHMENT_REQ           = 0x6318,   // DWORD JID
    GM_MSG_PUNISHMENT_ACK           = 0xA318,   // JID, 0x110 bytes
    GM_MSG_SLAVE_JOB                = 0x6319,
    GM_MSG_SILK_HISTORY_REQ         = 0x631A,
    GM_MSG_SILK_HISTORY_PARAM       = 0xA31C,
    GM_MSG_DIVISION_NOTIFY          = 0x631C,
    GM_MSG_PLAYTIME_RESTART         = 0x2320,   // DWORD JID
    GM_MSG_BROADCAST_TO_SHARDS      = 0x2327,   // WORD opcode, data
    GM_MSG_PLAYTIME_RESET           = 0x231F,   // WORD seconds since midnight

    // Timers of the main process.
    GM_TIMER_SHARD_USER_COUNT       = 0x1E,     // 1 s
    GM_TIMER_CASH                   = 0x1F,     // 5 s
    GM_TIMER_SHARD_STATUS           = 0x20,     // 10 s
    GM_TIMER_SHARD_USER             = 0x21,     // 300 s
    GM_TIMER_PUNISHMENT             = 0x22,     // 60 s
    GM_TIMER_PLAYTIME_RESET         = 0x23,     // at midnight
    GM_TIMER_VALIDATION_DAY         = 0x28,     // 1 s
};

// The GlobalManager main process (task SERVER_TASK_MAIN, 0x80268 bytes, vtable 0x52A9AC,
// runtime class 0x52A1B8). Besides the framework it keeps the content users of the division
// (AgentServer logins), passes them to the billing manager, answers the shard managers'
// character and punishment requests, feeds the slave with silk history requests, runs the
// periodic database jobs and holds the service manager (the SMC sessions).
class CGlobalManagerMainProcess : public ServerFramework::CServerProcessMain
{
    BS_DECLARE_DYNCREATE(CGlobalManagerMainProcess)                             // 0

public:
    // CGlobalManagerMainProcess::ctor (0x40be40)
    CGlobalManagerMainProcess();
    // sub_40BED0 (deleting dtor 0x40beb0)
    virtual ~CGlobalManagerMainProcess();                                       // 1

    // vf02 (0x40e8a0): the base initialisation; g_pGlobalManagerProcess, the billing manager
    // (created and started).
    virtual BOOL Initialize(void* pOwner, void* pParam);                       // 2
    // vf04 (0x40e910): billing stopped and destroyed, then the base.
    virtual int SignalStop();                                                   // 4
    // vf06 (0x40cc50): the base handlers, the main process opcodes, the service manager and
    // the timers.
    virtual void InitMsgHandlers();                                             // 6
    // vf09 (0x40d920): the service manager's opcodes, then the base.
    virtual void OnUnhandledMsg(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                ServerFramework::CMassiveMsg* pMassive);         // 9
    // vf17 (0x40d960): relay failures of the service manager's requests.
    virtual void OnRelayFailed(WORD wOpcode, DWORD dwContext);                  // 17
    // vf41 (0x40d9d0): state 5 when every cord of this server is up (else 4); a FarmManager
    // cord starts the daily play time reset when the last one was not today.
    virtual void OnCordEstablished(SERVER_CORD* pCord, SERVER_BODY* pPeer);    // 41
    // vf42 (0x40db20): the state again, then the service manager.
    virtual void OnCordClosed(SERVER_CORD* pCord, SERVER_BODY* pPeer);         // 42
    // vf43 (0x40df30): an AgentServer leaving state 5 loses its users.
    virtual void OnServerBodyStateChanged(SERVER_BODY* pBody, int nOldState);  // 43
    // vf44 (0x40cec0)
    virtual void OnSessionClosed(DWORD dwSessionID);                           // 44
    // vf47 (0x40dc40)
    virtual void OnFileDone(DWORD dwSessionID, DWORD dwParam);                 // 47
    // vf48 (0x40dc60)
    virtual void OnFileFailed(DWORD dwSessionID);                              // 48

    // The handlers (slots 49..72; 72 is not registered).
    virtual void OnMsg_2320(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 49
    virtual void OnMsg_6201(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 50
    virtual void OnMsg_6200(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 51
    virtual void OnMsg_6202(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 52
    virtual void OnMsg_620B(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 53
    virtual void OnMsg_6306(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 54
    virtual void OnMsg_620C(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 55
    virtual void OnMsg_AA00(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 56 empty
    virtual void OnMsg_620E(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 57
    virtual void OnMsg_620F(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 58
    virtual void OnMsg_6210(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 59
    virtual void OnMsg_6111(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 60
    virtual void OnMsg_6112(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 61
    virtual void OnMsg_6900(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 62
    virtual void OnMsg_6901(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 63 empty
    virtual void OnMsg_6904(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 64
    virtual void OnMsg_A903(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 65 skip
    virtual void OnMsg_A902(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 66 skip
    virtual void OnMsg_A905(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 67
    virtual void OnMsg_6318(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 68
    virtual void OnMsg_A317(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 69 skip
    virtual void OnMsg_631A(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 70
    virtual void OnMsg_A31C(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 71
    virtual void OnMsg_SkipByte(CMsg*, DWORD, DWORD, ServerFramework::CMassiveMsg*);   // 72

    // sub_40D7B0 (0x2327, not virtual): the rest of the message with the opcode of its first
    // WORD to the manager of every shard.
    void OnMsg_2327(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // ManageShardCharacterData requests (shared code of vf57..vf59).
    void ManageShardCharacter(CMsg* pMsg, DWORD dwRelayID, BYTE byType, WORD wAckOpcode);

    // Timers.
    bool OnShardUserCountTimer(BYTE byTimerID);     // 0x40df80 (0x1E)
    bool OnCashTimer(BYTE byTimerID);               // 0x40e330 (0x1F)
    bool OnShardStatusTimer(BYTE byTimerID);        // 0x40e390 (0x20)
    bool OnShardUserTimer(BYTE byTimerID);          // 0x40dfa0 (0x21)
    bool OnPunishmentTimer(BYTE byTimerID);         // 0x40dfe0 (0x22)
    // sub_40E160 (0x23): reschedules itself, writes the reset time (ResetPlayTime job type 1),
    // restarts every user's play time and sends 0x231F to every shard manager.
    bool OnPlayTimeResetTimer(BYTE byTimerID);
    bool OnValidationDayTimer(BYTE byTimerID);      // 0x40e010 (0x28)

    // sub_40E0D0: timer 0x23 for the coming midnight (registered, or its period changed).
    void SchedulePlayTimeReset(BOOL bUpdate);
    // sub_40E040: false when the last play time reset was today (asserts that there was one).
    bool IsPlayTimeResetDue();
    // CDummyBillingMgr::ctor (0x40e950): the billing manager of ./billing.ini, initialised.
    bool CreateBillingMgr();
    // sub_40EB10
    void DestroyBillingMgr();

public:
    IBillingMgr*            m_pBillingMgr;              // +400C8
    CServiceManagerProcess  m_SM;                       // +400CC
    DWORD                   m_dwCashTick;               // +8025C
    DWORD                   m_dwShardStatusTick;        // +80260
    DWORD                   m_dwLastPlayTimeReset;      // +80264 __time32_t
};

C_ASSERT(FIELD_OFFSET(CGlobalManagerMainProcess, m_pBillingMgr) == 0x400C8);
C_ASSERT(FIELD_OFFSET(CGlobalManagerMainProcess, m_SM) == 0x400CC);
C_ASSERT(FIELD_OFFSET(CGlobalManagerMainProcess, m_dwCashTick) == 0x8025C);
C_ASSERT(FIELD_OFFSET(CGlobalManagerMainProcess, m_dwLastPlayTimeReset) == 0x80264);
C_ASSERT(sizeof(CGlobalManagerMainProcess) == 0x80268);

// 0x01000014 (0x555E58): the slave task.
enum
{
    GM_TASK_SLAVE = 0x01000014,
};

// sub_40EB60: the session and relay of the user's last 0x6201 / 0x6900.
void ContentUser_SetRequest(DWORD dwJID, DWORD dwSessionID, DWORD dwRelayID);
// sub_40F560 (__stdcall): pushes the message into the slave task's queue (lost when the slave
// has no task).
void __stdcall GlobalManager_PostToSlave(CMsg* pMsg);
