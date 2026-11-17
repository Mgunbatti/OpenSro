#pragma once

#include <winsock2.h>
#include <windows.h>
#include <list>
#include <map>

#include "BSLib/thread/ServiceObject.h"
#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/OverlapJob.h"

// Server processes: the jobs of the server's tasks. A process pops the messages of its task
// queue (session events, packets of the sessions bound to the task, internal messages) and
// dispatches them by opcode through a table of member functions.

namespace ServerFramework
{

class __single_inheritance CServerProcessBase;

// Message handler. Packets come as pMsg (pMassive NULL), reassembled 0x600D messages as
// pMassive (pMsg NULL). dwRelayID is the relay job ID of a message that arrived relayed (0x6008)
// or of the relay an answer (0xA008) belongs to; dwContext the context of that relay.
typedef void (CServerProcessBase::*PFN_MSG_HANDLER)(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                                    CMassiveMsg* pMassive);
C_ASSERT(sizeof(PFN_MSG_HANDLER) == 4);

// Periodic callback of the process timer (checked once a second); false removes the timer.
typedef bool (CServerProcessBase::*PFN_PROCESS_TIMER)(BYTE byTimerID);

struct SERVER_PROCESS_TIMER
{
    DWORD               dwLastTick;     // +0
    DWORD               dwPeriod;       // +4   ms
    PFN_PROCESS_TIMER   pfnTimer;       // +8
};

typedef std::map<BYTE, SERVER_PROCESS_TIMER> ServerProcessTimerMap;

enum
{
    SERVER_MSG_TIMER_TICK           = 0x2007,   // posted by the 1 s timer queue timer
    SERVER_MSG_RELAY_REQ            = 0x6008,
    SERVER_MSG_RELAY_ACK            = 0xA008,
    SERVER_MSG_OVERLAP_JOB_REQ      = 0x6009,
    SERVER_MSG_OVERLAP_JOB_ACK      = 0xA009,

    SERVER_RELAY_RESULT_OK          = 1,
    SERVER_RELAY_RESULT_FAILED      = 2,

    SERVER_PROCESS_TIMER_OVERLAP    = 1,        // 5 s, registered with overlap jobs
};

// Process with the handler table, the timer and the overlap jobs it waits for.
class CServerProcessBase : public CServiceObject
{
    BS_DECLARE_DYNCREATE(CServerProcessBase)                                    // 0

public:
    CServerProcessBase();
    virtual ~CServerProcessBase();                                              // 1

    // InitDispatcher (0x444250): pOwner is the task. Fills the handler table with
    // OnUnhandledMsg, calls InitMsgHandlers, starts the 1 s timer (m_bUseTimer) and prepares
    // the overlap jobs (m_bUseOverlapJob: job owner, 5 s timer, overlap task). TRUE.
    virtual BOOL Initialize(void* pOwner, void* pParam);                       // 2
    // MessageLoop (0x4442e0): dispatches the task's messages until the queue stops (or the
    // stop flag is set and m_bRunAfterStop is not 1). A handler that faults closes the
    // message's session.
    virtual DWORD Run(volatile LONG* pbStop, int nIndex);                      // 3
    // Shutdown (0x444430): timer entries and overlap jobs are released. Does not stop Run.
    virtual int SignalStop();                                                   // 4

    // Registers the framework handlers: 0x2007, 0xA008, 0x6008, 0xA009.
    virtual void InitMsgHandlers();                                             // 6
    virtual void RegisterHandler(WORD wOpcode, PFN_MSG_HANDLER pfnHandler);    // 7
    // Calls the handler of pMassive's (if set) or pMsg's opcode.
    virtual void Dispatch(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                          CMassiveMsg* pMassive);                               // 8
    // Logs "unhandled msg received <class> : <message>" and discards the data.
    virtual void OnUnhandledMsg(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                CMassiveMsg* pMassive);                         // 9
    // Timer queue callback: posts 0x2007 to the own queue.
    virtual BOOL PostTimerTick();                                               // 10
    // 0x2007: runs the due timers.
    virtual void OnTimerTick(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                             CMassiveMsg* pMassive);                            // 11
    // 0x6008 {DWORD relay ID, WORD target body, WORD opcode, data}: handled here when this
    // server is the target, relayed on otherwise; 0xA008 {2, ID, opcode} when that fails.
    virtual void OnMsg_6008(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 12
    // 0xA008 {BYTE result, DWORD relay ID, WORD opcode, data}: the answer of a relay. Passed to
    // the opcode's handler (or OnRelayFailed) when the relay started here, forwarded to the
    // body it came from otherwise.
    virtual void OnMsg_A008(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 13
    // 0xA009 {DWORD job ID}: answer of a local overlap job.
    virtual void OnMsg_A009(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 14
    // Answer to a relayed massive request: 0xA008 {1, relay ID, opcode, data}, or
    // 0xA008 {2, ...} when the data cannot be copied. Frees pReply.
    virtual void SendRelayReplyMassive(DWORD dwSessionID, DWORD dwRelayID,
                                       CMassiveMsg* pReply);                    // 15
    // Answer to a relayed request: 0xA008 {1, relay ID, opcode of pReply, rest of pReply}.
    // Releases pReply.
    virtual LONG SendRelayReply(DWORD dwSessionID, DWORD dwRelayID, CMsg* pReply);  // 16
    // A relay started here failed (no route, lost session or failure answer). Nothing here.
    virtual void OnRelayFailed(WORD wOpcode, DWORD dwContext);                  // 17
    // Sends pMsg to every session of the list and releases it. Returns the failures.
    virtual int BroadcastMsg(CMsg* pMsg, std::list<DWORD>* pSessions);         // 18
    // Registers the local job and sends 0x6009 {job} to pTask (the overlap task when NULL).
    virtual bool SendOverlapJob(COverlapJob_Local* pJob, CTask* pTask);        // 19

    // ServerProcessBase_RegisterTick5000Timer (0x444670)
    void RegisterOverlapTimer();
    // AgentServer 0x431720 (inlined as RegisterOverlapTimer in GatewayServer): adds a timer
    // entry (last tick now); an existing entry of the ID is kept.
    void RegisterTimer(BYTE byTimerID, DWORD dwPeriod, PFN_PROCESS_TIMER pfnTimer);
    // ServerProcessBase_TimerMap_SetPeriodByKey (0x444620, not referenced)
    void SetTimerPeriod(BYTE byTimerID, DWORD dwPeriod);
    // ServerProcessBase_TimerMap_EraseByKey (0x4446c0, not referenced)
    void EraseTimer(BYTE byTimerID);
    // ServerProcessBase_ClearTimerMapAndDeleteTimer (0x444710)
    void ClearTimers();
    // ReturnConst_1_4 (0x445210): the overlap timer does nothing and stays.
    bool OnOverlapTimer(BYTE byTimerID);

    // 0x4445e0: timer queue callback.
    static VOID CALLBACK TimerCallback(PVOID pParam, BOOLEAN bTimerOrWaitFired);

    template <typename T>
    void RegisterHandler(WORD wOpcode,
                         void (T::*pfn)(CMsg*, DWORD, DWORD, CMassiveMsg*))
    {
        RegisterHandler(wOpcode, static_cast<PFN_MSG_HANDLER>(pfn));
    }

public:
    // BUG(FWAPP-001): the table has room for 0xFFFF opcodes only; Initialize fills 0x10000
    // entries and opcode 0xFFFF shares its slot with the timer handle.
    union
    {
        PFN_MSG_HANDLER m_aHandler[0x10000];                // +4C
        struct
        {
            BYTE        m_abHandlerSpace[0x3FFFC];
            HANDLE      m_hTimer;                           // +40048
        };
    };
    BOOL                    m_bUseTimer;                    // +4004C
    ServerProcessTimerMap   m_mapTimer;                     // +40050
    BOOL                    m_bUseOverlapJob;               // +4005C
    COverlapJobManager      m_OverlapJobManager;            // +40060
    CTask*                  m_pOverlapTask;                 // +400B0
    BOOL                    m_bRunAfterStop;                // +400B4
};

C_ASSERT(FIELD_OFFSET(CServerProcessBase, m_aHandler) == 0x4C);
C_ASSERT(FIELD_OFFSET(CServerProcessBase, m_hTimer) == 0x40048);
C_ASSERT(FIELD_OFFSET(CServerProcessBase, m_bUseTimer) == 0x4004C);
C_ASSERT(FIELD_OFFSET(CServerProcessBase, m_mapTimer) == 0x40050);
C_ASSERT(FIELD_OFFSET(CServerProcessBase, m_bUseOverlapJob) == 0x4005C);
C_ASSERT(FIELD_OFFSET(CServerProcessBase, m_OverlapJobManager) == 0x40060);
C_ASSERT(FIELD_OFFSET(CServerProcessBase, m_pOverlapTask) == 0x400B0);
C_ASSERT(sizeof(CServerProcessBase) == 0x400B8);

// Runs the local overlap jobs (task SERVER_TASK_OVERLAP); keeps running after the stop flag
// until its queue stops.
class CServerProcessOverlap : public CServerProcessBase
{
    BS_DECLARE_DYNCREATE(CServerProcessOverlap)                                 // 0

public:
    CServerProcessOverlap() {}

    // Base handlers and 0x6009; sets m_bRunAfterStop.
    virtual void InitMsgHandlers();                                             // 6
    // 0x6009 {job pointer}: Prepare and Execute (state 1, 2, 3) under a structured exception
    // handler that writes a crash dump, then 0xA009 {job ID} to the job's owner.
    virtual void OnMsg_6009(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                            CMassiveMsg* pMassive);                             // 20
};

C_ASSERT(sizeof(CServerProcessOverlap) == 0x400B8);

// RelayMsgToBody_6008 (0x444f60): sends 0x6008 {relay ID, target, opcode, unread data of pMsg}
// on the route to wTargetBody and registers a relay job for the answer. pMsg is released
// (after skipping its data) in every case. False without route or when the send failed.
bool RelayMsgToBody_6008(CServerProcessBase* pProcess, CMsg* pMsg, WORD wTargetBody,
                         WORD wSourceBody, DWORD dwSourceJobID, DWORD dwContext);
// RelayMassiveMsgToBody_6008 (0x4450e0): the same for a massive message (whole data); frees
// pMassive.
bool RelayMassiveMsgToBody_6008(CMassiveMsg* pMassive, CServerProcessBase* pProcess,
                                WORD wTargetBody, WORD wSourceBody, DWORD dwSourceJobID);

// CheckMsgFullyConsumed (0x443fa0): logs a message the last holder did not read completely.
void CheckMsgFullyConsumed(CMsg* pMsg);

#ifdef JMX_FRAMEWORK_2011
// Msg_FormatDebugString (AgentServer 0x42b520): the 2011 description of a message in the
// framework logs, "IP:<peer> Req|Ack|NoDir:0x<opcode> (0x<inner opcode of 0x2209>) (MsgType:<high
// nibble>), <size> - <unread>" (255 characters). NULL when the text does not fit.
char* Msg_FormatDebugString(char* pszOut, CMsg* pMsg);
#endif

} // namespace ServerFramework
