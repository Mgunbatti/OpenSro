#pragma once

#include <winsock2.h>
#include <windows.h>
#include <map>
#include <string>
#include <vector>
#include <sql.h>

#include "ServerFramework/ServerProcess.h"

// CGlobalManagerSlave: the job of the slave task of GlobalManager (one thread). It takes the
// 0x6319 jobs the main process puts into its queue and, once enabled, polls the silk purchases
// every five minutes:
//
//   0x6319 {BYTE 0, BYTE detail ...}        detail 1: load the IP country table from the RIR
//                                           files; 16: IP statistics (answers 0xA319 into the
//                                           main task's queue)
//   0x6319 {BYTE 1, WORD body, 14 bytes}    "exec _GetSilkHistory", 0xA31A relayed to the body
//   0x6319 {BYTE 2, BYTE flag, string}      the last purchase number; flag 16 enables the poll
//
// The poll sends 0x231B {division, the sums of silk_reason 0 and 1, the last purchase number,
// the number of purchases} to the parent body. Its two queries are stored XOR 0x90 in the
// binary and decoded before use.

#pragma pack(push, 1)
// The 14 bytes of the 0x6319 silk history job (copied into the answer as they are).
struct SILK_HISTORY_REQ
{
    DWORD   dw0;            // +0
    DWORD   dw4;            // +4   first argument of _GetSilkHistory
    DWORD   dw8;            // +8
    BYTE    byC;            // +C   second argument
    BYTE    byD;            // +D   third argument
};

// A row of _GetSilkHistory (20 bytes).
struct SILK_HISTORY_ROW
{
    DWORD               dw0;    // +0
    TIMESTAMP_STRUCT    ts;     // +4
};
#pragma pack(pop)

C_ASSERT(sizeof(SILK_HISTORY_REQ) == 14);
C_ASSERT(sizeof(SILK_HISTORY_ROW) == 20);

// The time of a silk history entry, packed into 32 bits (GMSlave_PackTimestamp, 0x461730).
struct SILK_HISTORY_TIME
{
    DWORD   dwYear      : 6;    // year - 2000
    DWORD   dwMonth     : 4;
    DWORD   dwDay       : 5;
    DWORD   dwHour      : 5;
    DWORD   dwMinute    : 6;
    DWORD   dwSecond    : 6;
};

// An entry of the 0xA31A answer (8 bytes).
struct SILK_HISTORY_ENTRY
{
    DWORD               dw0;    // +0   the row's first column
    SILK_HISTORY_TIME   time;   // +4
};

C_ASSERT(sizeof(SILK_HISTORY_ENTRY) == 8);

// The row of the poll queries (16 bytes, int columns).
struct SILK_POLL_ROW
{
    DWORD   dwSilkReason0;  // +0   the first query's max(BuyNo) goes here
    DWORD   dwSilkReason1;  // +4
    DWORD   dwLastBuyNo;    // +8
    DWORD   dwCount;        // +C
};

C_ASSERT(sizeof(SILK_POLL_ROW) == 16);

// A value of the IP statistics (8 bytes, operator new).
struct IP_STATISTICS_ENTRY
{
    char    szCode[4];      // +0
    DWORD   dwCount;        // +4
};

C_ASSERT(sizeof(IP_STATISTICS_ENTRY) == 8);

typedef std::map<std::string, IP_STATISTICS_ENTRY*> IPStatisticsMap;

class CGlobalManagerSlave;

// CSingletonT<CGlobalManagerSlave> (0x45DBE0 / 0x45DC00, inlined in the constructor and the
// destructor): asserts that there is no slave yet and sets g_pGlobalManagerSlave; the
// destructor clears it.
class CGlobalManagerSlaveSingleton
{
public:
    CGlobalManagerSlaveSingleton();
    ~CGlobalManagerSlaveSingleton();
};

// 0x400E8 bytes; vtable 0x52DAAC, runtime class 0x52A1A4.
class CGlobalManagerSlave : public ServerFramework::CServerProcessBase,
                            public CGlobalManagerSlaveSingleton
{
    BS_DECLARE_DYNCREATE(CGlobalManagerSlave)                                   // 0

public:
    // CGlobalManagerSlave::ctor (0x45C4B0)
    CGlobalManagerSlave();
    // CGlobalManagerSlave::dtor (0x45C5F0): clears the IP statistics and frees the IP country
    // table (g_pIPCountryTable keeps its value).
    virtual ~CGlobalManagerSlave();                                             // 1

    // vf03 (0x45C6F0): dispatches the messages waiting in the queue (Sleep(1) when there are
    // none) and polls the silk history while enabled, until the stop flag is set or the queue
    // is stopped.
    virtual DWORD Run(volatile LONG* pbStop, int nIndex);                      // 3
    // vf06 (0x45C6D0): 0x6319 only (not the framework handlers).
    virtual void InitMsgHandlers();                                             // 6
    // vf09 (0x45C6A0): logs the opcode (the data stays unread).
    virtual void OnUnhandledMsg(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                ServerFramework::CMassiveMsg* pMassive);        // 9

    // sub_45C820
    void OnMsg_6319(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // sub_45D050: operation 0.
    void OnDetailJob(CMsg* pMsg);
    // sub_45D0C0: detail 1. Reloads Server.cfg and the IP country table, then 0xA319 {2,
    // request, 3} to the main task.
    void LoadRIRFiles(CMsg* pMsg);
    // DB_IPStatistics (0x45D3B0): detail 16 {string, string, DWORD request}.
    void IPStatistics(CMsg* pMsg);
    // sub_45D850: row trigger of IPStatistics.
    int OnIPStatisticsRow(DWORD dwQuery, DWORD dwUnused);
    // sub_45DA90: frees the IP statistics and empties the map.
    void ClearIPStatistics();
    // DB_GetSilkHistory (0x45CB40): operation 1.
    void GetSilkHistory(CMsg* pMsg);
    // sub_45CFA0: row trigger of GetSilkHistory.
    int OnSilkHistoryRow(DWORD dwQuery, DWORD dwUnused);
    // sub_45CEA0: operation 2.
    void SetSilkHistoryParam(CMsg* pMsg);
    // sub_45C8C0: the silk poll (every 300000 ms on the silk DB).
    void PollSilkHistory();
    // GMSlave_SilkHistoryDue (0x45C0B0)
    BOOL IsSilkHistoryDue();

    // GMSlave_XorDecode90 (0x4616A0): XOR 0x90 in place up to the first 0 (the length is taken
    // again after every byte).
    static void DecodeText(char* pszText);
    // The two poll queries as stored in the binary (0x52D690, 0x52D740), XOR 0x90.
    static const char s_szEncodedFirstQuery[];
    static const char s_szEncodedPollQuery[];

public:
    // +400B8 holds the singleton base (0x400B9 in the original)
    BOOL                m_bRIRLoaded;               // +400BC set by LoadRIRFiles, never read
    IPStatisticsMap     m_mapIPStatistics;          // +400C0
    BOOL                m_bSilkHistoryEnabled;      // +400CC
    unsigned __int64    m_qwLastSilkPoll;           // +400D0 ms of the performance counter
    __int64             m_qwLastBuyNo;              // +400D8
    ServerFramework::CServerProcessBase* m_pMainProcess;   // +400E0 set by CGlobalManager vf11
};

C_ASSERT(FIELD_OFFSET(CGlobalManagerSlave, m_bRIRLoaded) == 0x400BC);
C_ASSERT(FIELD_OFFSET(CGlobalManagerSlave, m_mapIPStatistics) == 0x400C0);
C_ASSERT(FIELD_OFFSET(CGlobalManagerSlave, m_bSilkHistoryEnabled) == 0x400CC);
C_ASSERT(FIELD_OFFSET(CGlobalManagerSlave, m_qwLastSilkPoll) == 0x400D0);
C_ASSERT(FIELD_OFFSET(CGlobalManagerSlave, m_qwLastBuyNo) == 0x400D8);
C_ASSERT(FIELD_OFFSET(CGlobalManagerSlave, m_pMainProcess) == 0x400E0);
C_ASSERT(sizeof(CGlobalManagerSlave) == 0x400E8);

// 0x59B964
extern CGlobalManagerSlave* g_pGlobalManagerSlave;

// GMSlave_GetPerfFrequency (0x4617A0): QueryPerformanceFrequency, once.
LARGE_INTEGER* GMSlave_GetPerfFrequency();
// GMSlave_GetPerfCounterMs (0x461830): the performance counter in ms.
__int64 GMSlave_GetPerfCounterMs(const LARGE_INTEGER* pliFrequency);
// GMSlave_PackTimestamp (0x461730)
void GMSlave_PackTimestamp(SILK_HISTORY_TIME* pOut, const TIMESTAMP_STRUCT* pTime);
