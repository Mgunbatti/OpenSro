#pragma once

#include <windows.h>
#include <hash_map>
#include <list>
#include <map>
#include <set>
#include <string>

#include "BSLib/BSDebug.h"
#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/Trigger.h"

// Server architecture known to every server module: the records of the certification
// architecture (sent by the certification server in 0xA003) and the cords (connections) between
// the server bodies (server processes).
//
// The records are the packed wire layouts; the pointer fields are filled in after parsing (their
// wire bytes are whatever the sender had in memory).

#pragma pack(push, 1)

// Module (server program type), list 1 of 0xA003.
struct CERT_MODULE
{
    BYTE    byID;               // +0
    char    szName[64];         // +1  "GatewayServer", "AgentServer", ...
};
C_ASSERT(sizeof(CERT_MODULE) == 65);

// List 2 of 0xA003, sent on unchanged.
struct CERT_CONTENT
{
    BYTE    byID;               // +0
    char    szName[64];         // +1
};
C_ASSERT(sizeof(CERT_CONTENT) == 65);

// List 3 of 0xA003.
struct CERT_DIVISION
{
    BYTE    byID;               // +0
    BYTE    ab1[288];           // +1
    WORD    wGlobalManagerID;   // +289 body of the division's GlobalManager (set locally)
};
C_ASSERT(sizeof(CERT_DIVISION) == 291);

// List 4 of 0xA003.
struct CERT_FARM
{
    BYTE    byID;               // +0
    BYTE    byDivisionID;       // +1
    BYTE    ab2[288];           // +2
};
C_ASSERT(sizeof(CERT_FARM) == 290);

// List 5 of 0xA003: farm to content assignment.
struct CERT_FARM_CONTENT
{
    BYTE        byFarmID;       // +0
    BYTE        byContentID;    // +1
    CERT_FARM*  pFarm;          // +2 set locally
};
C_ASSERT(sizeof(CERT_FARM_CONTENT) == 6);

// List 6 of 0xA003.
struct CERT_SHARD
{
    WORD        wID;            // +0
    BYTE        byFarmID;       // +2
    BYTE        ab3[545];       // +3
    WORD        w548;           // +548 user limit (GatewayServer compares +557 with it)
    WORD        wManagerID;     // +550 body that manages the shard
    CERT_FARM*  pFarm;          // +552 set locally
    BYTE        b556;           // +556
    WORD        w557;           // +557 current users
};
C_ASSERT(sizeof(CERT_SHARD) == 559);

// List 7 of 0xA003.
struct CERT_MACHINE
{
    DWORD   dwID;               // +0
    BYTE    byDivisionID;       // +4
    BYTE    ab5[32];            // +5
    char    szPublicIP[16];     // +37
    char    szPrivateIP[16];    // +53
    WORD    wMachineManagerID;  // +69 body of the machine's MachineManager (set locally)
};
C_ASSERT(sizeof(CERT_MACHINE) == 71);

// List 8 of 0xA003: one server process.
struct SERVER_BODY
{
    WORD            wID;            // +0
    BYTE            byDivisionID;   // +2
    BYTE            byFarmID;       // +3
    WORD            wShardID;       // +4
    DWORD           dwMachineID;    // +6
    BYTE            byModuleID;     // +10
    BYTE            b11;            // +11
    WORD            wParentID;      // +12 body that certifies this one
    WORD            wPort;          // +14
    int             nState;         // +16
    CERT_MODULE*    pModule;        // +20 set locally
    CERT_MACHINE*   pMachine;       // +24
    CERT_DIVISION*  pDivision;      // +28
    CERT_FARM*      pFarm;          // +32
    CERT_SHARD*     pShard;         // +36
};
C_ASSERT(sizeof(SERVER_BODY) == 40);

// List 9 of 0xA003: connection between two bodies; wFromID connects to wToID.
struct SERVER_CORD
{
    DWORD   dwID;               // +0
    WORD    wFromID;            // +4
    WORD    wToID;              // +6
    BYTE    byPrivate;          // +8  1: connect through the private addresses
    DWORD   dwStatus;           // +9
    DWORD   dwSessionID;        // +13 0 while not connected
};
C_ASSERT(sizeof(SERVER_CORD) == 17);

#pragma pack(pop)

typedef std::list<CERT_MODULE*>         CertModuleList;
typedef std::list<CERT_CONTENT*>        CertContentList;
typedef std::list<CERT_DIVISION*>       CertDivisionList;
typedef std::list<CERT_FARM*>           CertFarmList;
typedef std::list<CERT_FARM_CONTENT*>   CertFarmContentList;
typedef std::list<CERT_SHARD*>          CertShardList;
typedef std::list<CERT_MACHINE*>        CertMachineList;
typedef std::list<SERVER_BODY*>         ServerBodyList;
typedef std::list<SERVER_CORD*>         ServerCordList;

// Message posted to the server architecture view (lParam: body / cord).
enum
{
    WM_SERVER_ARCH_LOADED       = 0x7E8,
    WM_SERVER_CORD_CHANGED      = 0x7E9,
    WM_SERVER_BODY_CHANGED      = 0x7EA,
};

enum
{
    SERVER_STATUS_OPCODE        = 0x2005,
};

// Receiver of body state changes: the trigger of CServerProcessMain. OnTrigger is called with
// (SERVER_BODY*, previous state).
typedef ServerFramework::CTrigger IServerBodyStateObserver;

// Globals of the architecture (0x4F9CA8..0x4F9CCC). The lists own their records.
extern SERVER_BODY*             g_pSelfBody;            // 0x4F9CA8 first body of the list
extern CertMachineList*         g_pCertMachineList;     // 0x4F9CAC
extern CertContentList*         g_pCertContentList;     // 0x4F9CB0
extern CertDivisionList*        g_pCertDivisionList;    // 0x4F9CB4
extern CertFarmList*            g_pCertFarmList;        // 0x4F9CB8
extern CertFarmContentList*     g_pCertFarmContentList; // 0x4F9CBC
extern CertShardList*           g_pCertShardList;       // 0x4F9CC0
extern ServerBodyList*          g_pServerBodyList;      // 0x4F9CC4
extern ServerCordList*          g_pServerCordList;      // 0x4F9CC8
extern CertModuleList*          g_pCertModuleList;      // 0x4F9CCC
extern IServerBodyStateObserver* g_pServerBodyStateObserver;   // 0x4F9CD0
extern HWND                     g_hServerArchView;      // 0x4F9D08 server architecture view

extern std::map<WORD, CERT_SHARD*>          g_mapCertShard;         // 0x53B744
extern std::map<BYTE, CERT_MODULE*>         g_mapCertModuleByID;    // 0x53B750
extern std::map<WORD, SERVER_BODY*>         g_mapServerBody;        // 0x53B75C
extern std::map<DWORD, SERVER_CORD*>        g_mapServerCord;        // 0x53B768 by cord ID
extern std::map<DWORD, CERT_MACHINE*>       g_mapCertMachine;       // 0x53B774
extern std::map<std::string, CERT_MODULE*>  g_mapCertModuleByName;  // 0x53B780 lower case
// 0x53B790: never filled in GatewayServer; only checked and freed.
typedef std::list<std::list<void*>*> ServerRouteListList;
extern stdext::hash_map<WORD, ServerRouteListList*> g_hmapServerRoute2;
// 0x53B7B8: body ID -> cord to send through (direct cord or first hop).
extern stdext::hash_map<WORD, SERVER_CORD*> g_hmapServerRoute;
extern std::map<DWORD, SERVER_CORD*>        g_mapSessionCord;       // 0x53B7DC session -> cord
extern std::set<WORD>                       g_setNotifyBody;        // 0x53B7E8
extern std::set<DWORD>                      g_setNotifySession;     // 0x53B7F4

// Appends every cord that ends at the body (either side) to pList.
void ServerCord_CollectForBody(WORD wBodyID, ServerCordList* pList);
// Number of cords of the body and how many of them are connected.
void ServerCord_CountForBody(WORD wBodyID, DWORD* pnCords, DWORD* pnConnected);
// Depth-first search from pFrom to the body wTargetID; returns the ID of the cord leaving pFrom.
DWORD ServerCord_FindRouteRecursive(SERVER_BODY* pFrom, WORD wTargetID, DWORD dwSkipCordID,
                                    std::map<WORD, int>* pDepth, BOOL* pbFound);
bool ServerBody_BuildRouteTable();

SERVER_BODY* ServerBodyInfo_Find(WORD wBodyID);
// ServerList4F9CC4_FindByIdAndName (0x424460, not referenced): the body listening on wPort of the
// machine with the public address pszPublicIP (compared ignoring case), NULL if none.
SERVER_BODY* ServerBody_FindByPortAndPublicIP(const char* pszPublicIP, WORD wPort);
// The other end of a cord that touches pBody.
SERVER_BODY* ServerCord_FindPeerBody(SERVER_CORD* pCord, SERVER_BODY* pBody);
SERVER_BODY* ServerBody_FindSelf(SERVER_CORD* pCord);
SERVER_CORD* ServerCordMap_FindById(DWORD dwCordID);
// The cord bound to the session, NULL if none (AgentServer 0x4100d0; inlined in GatewayServer).
SERVER_CORD* ServerCordMap_FindBySession(DWORD dwSessionID);
// Route lookup: the cord to send through to reach the body.
SERVER_CORD* ServerBody_Find(WORD wBodyID);

// Sets a body's state; broadcasts 0x2005 when it changed or the body is this server.
bool ServerBody_SetStateAndBroadcast2005(int nState, WORD wBodyID, DWORD dwExcludeSession);
// Sets a cord's status; broadcasts 0x2005 when it changed.
bool ServerBody_UpdateStatusAndNotify(DWORD dwCordID, DWORD dwStatus, DWORD dwExcludeSession);
// Sends pMassive to the notify bodies and sessions except dwExcludeSession, then frees it.
void ServerBody_BroadcastToNotifySetsAndFree(ServerFramework::CMassiveMsg* pMassive,
                                             DWORD dwExcludeSession);
// Sends pMsg through the body's route and releases it; false (message kept) without a route.
bool ServerBody_SendMsg(WORD wBodyID, CMsg* pMsg);

// A session to a peer server is up: binds it to the matching cord. Returns the cord or NULL.
SERVER_CORD* ServerCord_OnEstablished(DWORD dwSessionID, const char* pszModuleName);
SERVER_CORD* ServerCord_OnClosed(DWORD dwSessionID);
// Starts keep-alive connects for the cords this server opens.
bool ServerCord_ConnectOutgoingCords();
