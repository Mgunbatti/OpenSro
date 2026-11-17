#pragma once

#include <winsock2.h>
#include <windows.h>
#include <list>
#include <map>
#include <string>

#include "BSLib/thread/SynchObject.h"
#include "ServerFramework/MonitorGraph.h"
#include "ServerFramework/ServerApp.h"
#include "ServerFramework/ServerBody.h"

class CDBSessionMgr;
class CTask;

// GatewayServer: the server the game client logs in through. It checks the client version
// (0x6100), sends the shard list (0x6101) and the notices (0x6104), certifies the account
// (0x6102, the certification part) and hands the login to an AgentServer of the chosen shard.

// Task IDs of the GatewayServer processes (besides SERVER_TASK_MAIN / SERVER_TASK_OVERLAP).
enum
{
    GATEWAY_TASK_IBUV               = 0x01000009,   // captcha images (0x4F82D0)
    GATEWAY_TASK_CERTIFIER_FIRST    = 0x0100000A,   // the 30 certifier processes (0x4F82D4)
};

// Monitor graphs of the gateway (MONITOR_COUNTER::dwID holds the selector).
enum
{
    GATEWAY_COUNTER_SESSION_CREATED = 0x3020001,
    GATEWAY_COUNTER_SESSION_LOST    = 0x3020002,
    GATEWAY_COUNTER_CERTIFY_TIME    = 0x3020003,
};

// The application object.
class CGatewayServer : public ServerFramework::CServerApp
{
public:
    // DynInit_CGatewayServer_53AE78 (0x403b80): m_dw24 100 MB.
    CGatewayServer();
    // 0x403aa0
    virtual ~CGatewayServer();                                              // 0

    // SetDefaultAppParams (0x403c20): engine flags 0x13, 100 pending accepts, security mode 3.
    virtual void SetDefaultEngineConfig();                                  // 3
    // CreateProcesses (0x403c80): structured exceptions become C++ exceptions, then the main
    // process (CGatewayMainProcess), the certifier processes and the IBUV process.
    virtual bool RegisterMainProcessTask();                                 // 7
    // InitDBAndModuleVersions (0x403da0): client versions of Server.cfg, databases, module
    // versions, privileged addresses, shards, download servers, notices, monitor graphs, the
    // IBUV process; state 4.
    virtual bool OnLocalDataInitialized();                                  // 11
    // vf12 (0x404d20): frees the gateway data and closes the databases. False.
    virtual bool OnShutdown();                                              // 12
    // InitMonitorGraphCounters (0x404d40): engine and overlap graphs. True.
    virtual bool InitMonitorCounters();                                     // 13

    // DB row triggers of OnLocalDataInitialized, provided by the certification part:
    // _ModuleVersion / _ModuleVersionFile rows (0x404da0), _PrivilegedIP rows (0x404eb0) and
    // the second address set (0x404ef0, not used by the queries).
    int OnModuleVersionRow(DWORD dwQuery, DWORD dwUnused);
    int OnPrivilegedIPRow(DWORD dwQuery, DWORD dwUnused);
    int OnPrivilegedIPRow2(DWORD dwQuery, DWORD dwUnused);

public:
    // Server.cfg "LastFullVersion_<module>": the oldest client version that may still be
    // patched; older clients are rejected.
    std::map<std::string, DWORD>    m_mapLastFullVersion;   // +494
};

C_ASSERT(FIELD_OFFSET(CGatewayServer, m_mapLastFullVersion) == 0x494);
C_ASSERT(sizeof(CGatewayServer) == 0x4A0);

// 0x53AE78, g_pServerApp.
extern CGatewayServer g_GatewayServer;

// Database session pools: the gateway's own (0x4F9C8C) and the account database (0x4F9C90).
extern CDBSessionMgr* g_pGatewayDBSession;
extern CDBSessionMgr* g_pAccountDBSession;
// 0x4F9C38: local time of the last OnLocalDataInitialized.
extern SYSTEMTIME g_stGatewayLocalDataTime;
// 0x4F9C94 / 0x4F9C98: sessions created / lost since the graph last read them.
extern DWORD g_dwGatewaySessionCreated;
extern DWORD g_dwGatewaySessionLost;

// The certifier processes (0x53ADA8): task ID -> task, the round-robin cursor and the
// certification time statistics (sum and count, read and reset by the monitor graph).
typedef std::map<DWORD, CTask*> CertifierTaskMap;

struct CERTIFIER_PROCESS_POOL
{
    CertifierTaskMap::iterator  itNext;         // +0
    CertifierTaskMap            mapTask;        // +8
    DWORD                       dwElapsedSum;   // +14
    DWORD                       dwElapsedCount; // +18
    CCriticalSectionBS          csStat;         // +1C
};

C_ASSERT(FIELD_OFFSET(CERTIFIER_PROCESS_POOL, mapTask) == 0x8);
C_ASSERT(FIELD_OFFSET(CERTIFIER_PROCESS_POOL, dwElapsedSum) == 0x14);
C_ASSERT(FIELD_OFFSET(CERTIFIER_PROCESS_POOL, csStat) == 0x1C);
C_ASSERT(sizeof(CERTIFIER_PROCESS_POOL) == 0x58);

extern CERTIFIER_PROCESS_POOL g_CertifierProcessPool;

// GatewayStat_TakeAverage (0x40dfe0): average certification time since the last call (0 without
// samples); resets the statistics.
DWORD GatewayStat_TakeAverage();
// CGatewayServer_ReadAndResetStatCounter (0x403d20): update callback of the gateway graphs.
bool __cdecl GatewayServer_ReadStatCounter(MONITOR_COUNTER* pCounter);

// The DownloadServer bodies (0x53AD94) for patching clients, with a round-robin cursor.
struct DOWNLOAD_SERVER_LIST
{
    ServerBodyList              list;           // +0
    ServerBodyList::iterator    itNext;         // +C
};

C_ASSERT(sizeof(DOWNLOAD_SERVER_LIST) == 0x14);

extern DOWNLOAD_SERVER_LIST g_DownloadServerList;

// DownloadServerList_Build (0x402f20): every body of module "DownloadServer".
void DownloadServerList_Build();
// DownloadServerList_Clear (0x402fc0)
void DownloadServerList_Clear();
// SelectPatchServer_RoundRobin (0x403010): the next download server in state 5, NULL if none.
SERVER_BODY* SelectPatchServer_RoundRobin();

// Public addresses of the FarmManagers (0x53AE68), sent to clients in 0xA106.
#pragma pack(push, 1)
struct FARM_MANAGER_ADDR
{
    BYTE    byFarmID;   // +0
    BYTE    ab1[3];     // +1
    DWORD   dwIP;       // +4   network order
};
#pragma pack(pop)

C_ASSERT(sizeof(FARM_MANAGER_ADDR) == 8);

extern std::list<FARM_MANAGER_ADDR> g_listFarmManagerAddr;

// GatewayServer_CollectFarmManagerAddrs (0x404c60): FarmManager bodies with a public address.
// Returns the list size.
size_t GatewayServer_CollectFarmManagerAddrs();

// IPv4ToString (0x4036d0): "a.b.c.d" of an address in network order.
void IPv4ToString(std::string* pstrOut, DWORD dwIP);

// StringMap53B6BC_FindValueCStr (0x40b840): value of a key of the module's Server.cfg block,
// NULL without one.
const char* ServerConfig_FindExtraValue(const char* pszKey);

// GatewayServer_RunWithSETranslator (0x475af0): on the calling thread structured exceptions are
// thrown as unsigned int (the exception code), invalid CRT parameters assert and throw.
void GatewayServer_RunWithSETranslator();
