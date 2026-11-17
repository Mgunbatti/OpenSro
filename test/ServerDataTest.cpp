#include "ServerFramework/CertArch.h"
#include "ServerFramework/CertData.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerFramework/ServerBody.h"
#include "BSNet/MsgPool.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "TestCheck.h"

using namespace ServerFramework;

// Application layer functions the server data code uses.
static std::string g_strLastLog;
static int g_nLogLines = 0;

void LogPrintf(DWORD, const char* pszFormat, ...)
{
    char sz[1024];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf_s(sz, sizeof(sz), _TRUNCATE, pszFormat, args);
    va_end(args);
    g_strLastLog = sz;
    ++g_nLogLines;
}

int StrCmpI_Locale(const char* psz1, const char* psz2)
{
    int n2 = lstrlenA(psz2);
    int n1 = lstrlenA(psz1);
    return CompareStringA(0x400, NORM_IGNORECASE | NORM_IGNOREKANATYPE, psz1, n1, psz2, n2) - 2;
}

struct SENT
{
    DWORD dwSessionID;
    CMsg* pMsg;
};

struct KEEPALIVE
{
    std::string strHost;
    u_short wPort;
    std::string strBind;
};

// Engine double: pooled messages, recorded sends, configurable peer address.
class CFakeNet : public IBSNet
{
public:
    CMsgPool m_Pool;
    std::vector<SENT> m_Sent;
    std::vector<DWORD> m_Disconnected;
    std::vector<DWORD> m_Excluded;
    std::vector<KEEPALIVE> m_KeepAlive;
    std::string m_strPeerIP;
    u_short m_wPeerPort;
    int m_nSocketState;

    virtual CMsg* __stdcall AllocMsg(BOOL bEncrypted)
    {
        CMsg* p = m_Pool.Alloc();
        if (bEncrypted == 1)
            p->SetEncryptFlag();
        InterlockedIncrement(&p->m_lRefCount);
        return p;
    }
    virtual LONG __stdcall ReleaseMsg(CMsg* p)
    {
        if (p->m_lRefCount > 0 && InterlockedDecrement(&p->m_lRefCount) == 0)
            m_Pool.Free(p);
        return 0;
    }
    virtual int __stdcall SendMsg(DWORD dwSessionID, CMsg* p)
    {
        InterlockedIncrement(&p->m_lRefCount);
        SENT s = { dwSessionID, p };
        m_Sent.push_back(s);
        return 1;
    }
    virtual BOOL __stdcall GetSessionPeerAddress(DWORD, std::string* pstrIP, ULONG*,
                                                 u_short* pwPort)
    {
        *pstrIP = m_strPeerIP;
        *pwPort = m_wPeerPort;
        return TRUE;
    }
    virtual int __stdcall DisconnectSession(DWORD dwSessionID, DWORD)
    {
        m_Disconnected.push_back(dwSessionID);
        return 0;
    }
    virtual void __stdcall ExcludeSessionFromCleanup(DWORD dwSessionID)
    {
        m_Excluded.push_back(dwSessionID);
    }
    virtual int __stdcall GetSessionSocketState(DWORD) { return m_nSocketState; }
    virtual void __stdcall AddKeepAliveTarget(const char* pszHost, u_short wPort,
                                              const char* pszBind, DWORD, DWORD)
    {
        KEEPALIVE k;
        k.strHost = pszHost;
        k.wPort = wPort;
        k.strBind = pszBind ? pszBind : "(null)";
        m_KeepAlive.push_back(k);
    }

    void ReleaseSent()
    {
        for (size_t i = 0; i < m_Sent.size(); ++i)
            ReleaseMsg(m_Sent[i].pMsg);
        m_Sent.clear();
    }

    virtual HRESULT __stdcall QueryInterface(const GUID&, void**) { return 0; }
    virtual ULONG __stdcall AddRef() { return 0; }
    virtual int __stdcall Shutdown() { return 0; }
    virtual BOOL __stdcall Initialize(BSNET_INIT_PARAM) { return 0; }
    virtual DWORD __stdcall ConnectSockAddr(const sockaddr*, const sockaddr*, DWORD, int)
    {
        return 0;
    }
    virtual DWORD __stdcall Connect(const char*, u_short, const char*, DWORD, int) { return 0; }
    virtual BOOL __stdcall ListenSockAddr(const sockaddr*) { return 0; }
    virtual BOOL __stdcall Listen(const char*, u_short) { return 0; }
    virtual BOOL __stdcall RegisterProactor(DWORD, CTask*, CRuntimeClass*, void*) { return 0; }
    virtual int __stdcall vf09(DWORD, DWORD, DWORD, DWORD) { return 0; }
    virtual BOOL __stdcall StartProactorWorkers(DWORD, int, int, unsigned, DWORD,
                                                THREAD_AFFINITY*)
    {
        return 0;
    }
    virtual BOOL __stdcall ProactorResume(DWORD) { return 0; }
    virtual BOOL __stdcall ProactorSuspend(DWORD) { return 0; }
    virtual BOOL __stdcall UnregisterProactor(DWORD) { return 0; }
    virtual void __stdcall GetStatistics(DWORD, NETENGINE_STATISTICS*) {}
    virtual CTask* __stdcall GetProactor(DWORD) { return 0; }
    virtual BOOL __stdcall StartHandshake(DWORD, SECURITY_HANDSHAKE_PARAM*) { return 0; }
    virtual BOOL __stdcall BeginSessionFileTransfer(DWORD, std::string, DWORD, DWORD, int)
    {
        return 0;
    }
    virtual BOOL __stdcall SendFile(DWORD, const std::string&, DWORD, int) { return 0; }
    virtual int __stdcall CloseSessionTransferFile(DWORD, DWORD) { return 0; }
    virtual BOOL __stdcall RemoveKeepAliveTarget(DWORD) { return 0; }
    virtual void __stdcall CloseAllFiles() {}
    virtual void __stdcall DumpMsgPool(BYTE, DWORD) {}
    virtual void __stdcall SetDebugAsserted(BYTE) {}
    virtual void __stdcall CleanupDisconnectedSessions() {}
    virtual CMsgPool* __stdcall GetMsgPool() { return &m_Pool; }
    virtual BOOL __stdcall GetSessionLocalMacAddress(DWORD, BYTE*) { return 0; }
    virtual void __stdcall SetSecurityMode(BYTE) {}
    virtual void __stdcall PrintVersion() {}
    virtual BOOL __stdcall RegisterDatagramProactor(DWORD) { return 0; }
    virtual int __stdcall OpenDatagramSocket(WORD) { return 0; }
    virtual int __stdcall SendDatagram(const sockaddr*, CMsg*) { return 0; }
};

static CFakeNet g_Net;

class CObserver : public IServerBodyStateObserver
{
public:
    CObserver() : m_nCalls(0), m_pBody(NULL), m_nOld(0) {}
    virtual void vf00() {}
    virtual void OnServerBodyStateChanged(SERVER_BODY* pBody, int nOldState)
    {
        ++m_nCalls;
        m_pBody = pBody;
        m_nOld = nOldState;
    }
    int m_nCalls;
    SERVER_BODY* m_pBody;
    int m_nOld;
};

// Sends a written massive message through the fake engine and feeds the parts back in, like a
// receiving server would get them.
static CMassiveMsg* Loopback(CMassiveMsg* pOut)
{
    g_Net.ReleaseSent();
    pOut->SendTo(1, FALSE);
    BYTE bType;
    WORD wCount, wOpcode;
    CMsg* pHeader = g_Net.m_Sent[0].pMsg;
    *pHeader >> bType >> wCount >> wOpcode;
    CMassiveMsg* pIn = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pIn->SetRecvHeader(wOpcode, wCount);
    for (size_t i = 1; i < g_Net.m_Sent.size(); ++i)
    {
        CMsg* p = g_Net.m_Sent[i].pMsg;
        p->m_wReadPos = MSG_HEADER_SIZE;
        pIn->AddRecvPart(p);
    }
    pIn->BeginRead();
    g_Net.ReleaseSent();
    return pIn;
}

// Concatenated payload bytes (part byte stripped) of the parts sent after a header packet.
static std::vector<BYTE> SentPayload(size_t nFirst, size_t nParts)
{
    std::vector<BYTE> v;
    for (size_t i = nFirst; i < nFirst + nParts; ++i)
    {
        CMsg* p = g_Net.m_Sent[i].pMsg;
        BYTE* pData = p->m_pData + MSG_HEADER_SIZE + 1;
        v.insert(v.end(), pData, pData + p->GetPayloadSize() - 1);
    }
    return v;
}

template <class T>
static void WriteList(CMassiveMsg* p, const std::vector<T>& v)
{
    BYTE b = 0;
    p->Write(&b, 1);
    for (size_t i = 0; i < v.size(); ++i)
    {
        b = 1;
        p->Write(&b, 1);
        p->Write(&v[i], sizeof(T));
    }
    b = 2;
    p->Write(&b, 1);
}

template <class T>
static T Zero()
{
    T t;
    memset(&t, 0, sizeof(t));
    return t;
}

static CERT_MODULE Module(BYTE byID, const char* pszName)
{
    CERT_MODULE m = Zero<CERT_MODULE>();
    m.byID = byID;
    strcpy_s(m.szName, pszName);
    return m;
}

static SERVER_BODY Body(WORD wID, BYTE byModule, BYTE byFarm, WORD wShard, DWORD dwMachine,
                        WORD wParent, WORD wPort, int nState)
{
    SERVER_BODY b = Zero<SERVER_BODY>();
    b.wID = wID;
    b.byDivisionID = 1;
    b.byFarmID = byFarm;
    b.wShardID = wShard;
    b.dwMachineID = dwMachine;
    b.byModuleID = byModule;
    b.wParentID = wParent;
    b.wPort = wPort;
    b.nState = nState;
    return b;
}

static SERVER_CORD Cord(DWORD dwID, WORD wFrom, WORD wTo)
{
    SERVER_CORD c = Zero<SERVER_CORD>();
    c.dwID = dwID;
    c.wFromID = wFrom;
    c.wToID = wTo;
    return c;
}

static CERT_MACHINE Machine(DWORD dwID, const char* pszPublic, const char* pszPrivate)
{
    CERT_MACHINE m = Zero<CERT_MACHINE>();
    m.dwID = dwID;
    m.byDivisionID = 1;
    strcpy_s(m.szPublicIP, pszPublic);
    strcpy_s(m.szPrivateIP, pszPrivate);
    return m;
}

// The architecture as the certification server (body 1) sees it.
static CMassiveMsg* BuildA003()
{
    std::vector<CERT_MODULE> modules;
    modules.push_back(Module(1, "Certification"));
    modules.push_back(Module(2, "GlobalManager"));
    modules.push_back(Module(3, "MachineManager"));
    modules.push_back(Module(4, "DownloadServer"));
    modules.push_back(Module(5, "FarmManager"));
    modules.push_back(Module(6, "GatewayServer"));
    modules.push_back(Module(7, "AgentServer"));
    modules.push_back(Module(8, "SR_ShardManager"));

    std::vector<CERT_CONTENT> contents(1, Zero<CERT_CONTENT>());
    contents[0].byID = 3;
    strcpy_s(contents[0].szName, "SR");

    std::vector<CERT_DIVISION> divisions(2, Zero<CERT_DIVISION>());
    divisions[0].byID = 1;
    divisions[1].byID = 2;

    std::vector<CERT_FARM> farms(2, Zero<CERT_FARM>());
    farms[0].byID = 1;
    farms[0].byDivisionID = 1;
    farms[1].byID = 9;
    farms[1].byDivisionID = 2;

    std::vector<CERT_FARM_CONTENT> farmContents(2, Zero<CERT_FARM_CONTENT>());
    farmContents[0].byFarmID = 1;
    farmContents[0].byContentID = 3;
    farmContents[1].byFarmID = 9;
    farmContents[1].byContentID = 3;

    std::vector<CERT_SHARD> shards(1, Zero<CERT_SHARD>());
    shards[0].wID = 64;
    shards[0].byFarmID = 1;
    shards[0].w548 = 1000;
    shards[0].wManagerID = 8;

    std::vector<CERT_MACHINE> machines;
    machines.push_back(Machine(100, "10.0.0.100", "192.168.0.100"));
    machines.push_back(Machine(101, "10.0.0.101", "192.168.0.101"));
    machines.push_back(Machine(102, "10.0.0.102", "192.168.0.102"));
    machines[2].byDivisionID = 2;

    std::vector<SERVER_BODY> bodies;
    SERVER_BODY self = Body(1, 1, 0, 0, 100, 0, 32000, 4);
    self.byDivisionID = 1;
    bodies.push_back(self);
    bodies.push_back(Body(2, 2, 0, 0, 100, 1, 15880, 4));
    bodies.push_back(Body(3, 3, 0, 0, 100, 1, 15881, 4));
    bodies.push_back(Body(4, 5, 1, 0, 100, 1, 15882, 5));
    bodies.push_back(Body(5, 6, 1, 0, 101, 1, 15779, 4));
    bodies.push_back(Body(6, 4, 1, 0, 101, 1, 15881, 4));
    bodies.push_back(Body(7, 7, 1, 64, 101, 1, 15884, 4));
    bodies.push_back(Body(8, 8, 1, 64, 101, 1, 15885, 4));

    std::vector<SERVER_CORD> cords;
    cords.push_back(Cord(1, 2, 1));
    cords.push_back(Cord(2, 3, 2));
    cords.push_back(Cord(3, 4, 2));
    cords.push_back(Cord(4, 5, 4));
    cords.push_back(Cord(5, 6, 2));
    cords.push_back(Cord(6, 7, 5));
    cords.push_back(Cord(7, 8, 4));
    cords[0].dwSessionID = 0x55555555;      // must be reset by the parser

    CMassiveMsg* p = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    p->SetOpcode(0xA003);
    WriteList(p, modules);
    WriteList(p, contents);
    WriteList(p, divisions);
    WriteList(p, farms);
    WriteList(p, farmContents);
    WriteList(p, shards);
    WriteList(p, machines);
    WriteList(p, bodies);
    WriteList(p, cords);

    // certification data
    BYTE b = 1;
    p->Write(&b, 1);
    std::vector<CERT_DATA_GROUP> groups(2, Zero<CERT_DATA_GROUP>());
    groups[0].byID = 3;
    groups[1].byID = 7;
    std::vector<CERT_DATA_SETTING> settings(2, Zero<CERT_DATA_SETTING>());
    settings[0].dwValue = 11;
    strcpy_s(settings[0].szName, "Something");
    settings[1].dwValue = 42;
    strcpy_s(settings[1].szName, "ignore_shard_maxuser");
    std::vector<CERT_DATA_GROUP_IP> ips(3, Zero<CERT_DATA_GROUP_IP>());
    ips[0].byGroupID = 3;
    ips[0].dwIP = 0x0100007F;
    ips[1].byGroupID = 3;
    ips[1].dwIP = 0x0200A8C0;
    ips[2].byGroupID = 7;
    ips[2].dwIP = 0x0300A8C0;
    WriteList(p, groups);
    WriteList(p, settings);
    WriteList(p, ips);
    return p;
}

static void TestCertificationServer()
{
    CMassiveMsg* pIn = Loopback(BuildA003());
    CHECK(CServerProcessMain_ParseA003CertArchAndBuildTargets(pIn));
    CHECK(pIn->GetUnreadSize() == 0);
    pIn->Free();

    CHECK(g_pSelfBody && g_pSelfBody->wID == 1);
    CHECK(g_pServerBodyList->size() == 8 && g_pServerCordList->size() == 7);
    CHECK(g_pSelfBody->pModule == CertModule_FindByName("CERTIFICATION"));
    CHECK(CertModule_FindByName("gatewayserver")->byID == 6);
    CHECK(CertModule_FindByName("NoSuchServer") == NULL);
    CHECK(CertModule_FindById(7) == CertModule_FindByName("AgentServer"));

    // links and the values filled in locally
    SERVER_BODY* p2 = ServerBodyInfo_Find(2);
    SERVER_BODY* p7 = ServerBodyInfo_Find(7);
    CHECK(p2 && p2->pDivision && p2->pDivision->byID == 1);
    CHECK(p2->pDivision->wGlobalManagerID == 2);
    CHECK(ServerBodyInfo_Find(3)->pMachine->wMachineManagerID == 3);
    CHECK(p7->pShard && p7->pShard->wID == 64 && p7->pShard->pFarm->byID == 1);
    CHECK(p7->pFarm->byID == 1 && p7->pMachine->dwID == 101);
    CHECK(ServerBodyInfo_Find(99) == NULL);
    CHECK(g_pCertFarmContentList->back()->pFarm->byID == 9);
    CHECK(g_mapCertShard.size() == 1 && g_mapCertShard[64] == p7->pShard);
    CHECK(ServerCordMap_FindById(1)->dwSessionID == 0);
    CHECK(ServerCordMap_FindById(8) == NULL);

    // routes: everything goes through cord 1 (2 -> 1)
    CHECK(g_hmapServerRoute.size() == 7);
    for (WORD w = 2; w <= 8; ++w)
        CHECK(ServerBody_Find(w) == ServerCordMap_FindById(1));
    CHECK(ServerBody_Find(1) == NULL);

    std::map<WORD, int> mapDepth;
    for (WORD w = 1; w <= 8; ++w)
        mapDepth[w] = -1;
    mapDepth[2] = 1;
    BOOL bFound = FALSE;
    CHECK(ServerCord_FindRouteRecursive(p2, 8, 0, &mapDepth, &bFound) == 3);
    CHECK(bFound && mapDepth[4] == 2 && mapDepth[8] == 3);

    DWORD nCords, nConnected;
    ServerCord_CountForBody(2, &nCords, &nConnected);
    CHECK(nCords == 4 && nConnected == 0);
    ServerCordMap_FindById(2)->dwSessionID = 9;
    ServerCord_CountForBody(2, &nCords, &nConnected);
    CHECK(nCords == 4 && nConnected == 1);
    ServerCordMap_FindById(2)->dwSessionID = 0;

    CHECK(ServerCord_FindPeerBody(ServerCordMap_FindById(1), g_pSelfBody) == p2);
    CHECK(ServerBody_FindSelf(ServerCordMap_FindById(1)) == p2);

    // notify target: the GlobalManager behind the only cord
    CHECK(g_setNotifyBody.size() == 1 && *g_setNotifyBody.begin() == 2);

    // certification data
    CHECK(g_hmapPrivIP.size() == 2);
    CHECK(PrivIPMap_ContainsIP(3, 0x0100007F));
    CHECK(PrivIPMap_ContainsIP(3, 0x0200A8C0));
    CHECK(!PrivIPMap_ContainsIP(3, 0x0300A8C0));
    CHECK(PrivIPMap_ContainsIP(7, 0x0300A8C0));
    CHECK(!PrivIPMap_ContainsIP(5, 0x0100007F));
    CHECK(CertData_GetIgnoreShardMaxUserValue() == 42);
    CHECK(g_mapCertDataSetting.size() == 2 && g_mapCertDataSetting[42] != NULL);
    CHECK(g_mapCertDataGroup.size() == 2 && g_mapCertDataGroup[7]->byID == 7);
}

// Certification request of the GatewayServer at 10.0.0.101; returns the reply after the result.
static CMassiveMsg* RequestCertification(const char* pszModule, const char* pszAddress,
                                         bool* pbResult, BYTE* pbyCode)
{
    CMsg* pReq = g_Net.AllocMsg(FALSE);
    *pReq << pszModule << pszAddress;
    pReq->m_wReadPos = MSG_HEADER_SIZE;
    CMassiveMsg* pReply = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pReply->SetOpcode(0xA003);
    *pbResult = CertArch_OnCertificationRequest(pReq, pReply);
    g_Net.ReleaseMsg(pReq);
    CMassiveMsg* pIn = Loopback(pReply);
    pIn->Read(pbyCode, 1);
    return pIn;
}

static void TestCertificationRequest()
{
    bool bResult;
    BYTE byCode;
    CMassiveMsg* pIn = RequestCertification("AgentServer", "10.0.0.250", &bResult, &byCode);
    CHECK(!bResult && byCode == 2 && pIn->GetUnreadSize() == 0);
    CHECK(g_strLastLog == "cannot certify server body : [AgentServer][10.0.0.250]");
    pIn->Free();

    pIn = RequestCertification("gatewayserver", "10.0.0.101", &bResult, &byCode);
    CHECK(bResult && byCode == 1);

    // The reply is the requesting server's view; free ours and load it like body 5 would.
    ServerApp_FreeCertArchData();
    CertData_FreeRecordListsAndIPMaps();
    CHECK(g_pServerBodyList == NULL && g_mapServerBody.empty() && g_hmapServerRoute.empty());
    CHECK(g_hmapPrivIP.empty() && g_pCertDataGroupList == NULL);

    CHECK(CServerProcessMain_ParseA003CertArchAndBuildTargets(pIn));
    CHECK(pIn->GetUnreadSize() == 0);
    pIn->Free();

    CHECK(g_pSelfBody->wID == 5);
    // request body, certification server, then what the collection found
    const WORD awOrder[] = { 5, 1, 4, 7, 2, 8, 6 };
    CHECK(g_pServerBodyList->size() == 7);
    size_t i = 0;
    for (ServerBodyList::iterator it = g_pServerBodyList->begin();
         it != g_pServerBodyList->end(); ++it, ++i)
    {
        CHECK(i < 7 && (*it)->wID == awOrder[i]);
    }
    CHECK(g_pServerCordList->size() == 4);
    CHECK(ServerCordMap_FindById(3) && ServerCordMap_FindById(4) && ServerCordMap_FindById(6) &&
          ServerCordMap_FindById(7) && !ServerCordMap_FindById(1));
    CHECK(g_pCertDivisionList->size() == 1 && g_pCertFarmList->size() == 1);
    CHECK(g_pCertFarmContentList->size() == 1 && g_pCertShardList->size() == 1);
    // machines of division 1; the certification server's machine is among them
    CHECK(g_pCertMachineList->size() == 2);
    CHECK(g_pCertModuleList->size() == 8 && g_pCertContentList->size() == 1);
    CHECK(PrivIPMap_ContainsIP(3, 0x0200A8C0));
    CHECK(CertData_GetIgnoreShardMaxUserValue() == 42);

    // routes from body 5: direct to 4 and 7, the rest through 4
    CHECK(ServerBody_Find(4) == ServerCordMap_FindById(4));
    CHECK(ServerBody_Find(7) == ServerCordMap_FindById(6));
    CHECK(ServerBody_Find(2) == ServerCordMap_FindById(4));
    CHECK(ServerBody_Find(8) == ServerCordMap_FindById(4));
    // body 1 and 6 are not reachable over the sent cords
    CHECK(ServerBody_Find(1) == NULL && ServerBody_Find(6) == NULL);

    CHECK(g_setNotifyBody.size() == 2 && g_setNotifyBody.count(4) && g_setNotifyBody.count(7));
}

static void TestCords()
{
    // body 5 connected to the FarmManager (body 4) at its public address
    g_Net.m_strPeerIP = "10.0.0.100";
    g_Net.m_wPeerPort = 15882;
    g_Net.m_nSocketState = 1;
    SERVER_CORD* pCord4 = ServerCordMap_FindById(4);
    CHECK(ServerCord_OnEstablished(77, "FARMMANAGER") == pCord4);
    CHECK(pCord4->dwSessionID == 77 && g_mapSessionCord[77] == pCord4);
    CHECK(g_Net.m_Excluded.size() == 1 && g_Net.m_Excluded[0] == 77);
    CHECK(g_strLastLog == "server cord established : 77 (10.0.0.100:15882)");

    // wrong port or module: no cord
    g_Net.m_wPeerPort = 1;
    CHECK(ServerCord_OnEstablished(80, "FarmManager") == NULL);
    g_Net.m_wPeerPort = 15882;
    CHECK(ServerCord_OnEstablished(80, "AgentServer") == NULL);

    // a second session for the same cord replaces the first
    CHECK(ServerCord_OnEstablished(78, "FarmManager") == pCord4);
    CHECK(g_Net.m_Disconnected.size() == 1 && g_Net.m_Disconnected[0] == 77);
    CHECK(g_mapSessionCord.count(77) == 0 && g_mapSessionCord[78] == pCord4);
    CHECK(pCord4->dwSessionID == 78);

    // incoming from the AgentServer (7 -> 5) needs state >= 3 and an accepted socket
    g_Net.m_strPeerIP = "10.0.0.101";
    g_Net.m_nSocketState = 0;
    SERVER_CORD* pCord6 = ServerCordMap_FindById(6);
    CHECK(ServerCord_OnEstablished(90, "agentserver") == pCord6);
    CHECK(pCord6->dwSessionID == 90);

    // sending through a route
    CMsg* pMsg = g_Net.AllocMsg(FALSE);
    pMsg->SetOpcode(0x1234);
    g_Net.ReleaseSent();
    CHECK(ServerBody_SendMsg(8, pMsg));
    CHECK(g_Net.m_Sent.size() == 1 && g_Net.m_Sent[0].dwSessionID == 78);
    CHECK(pMsg->m_lRefCount == 1);
    // no route to body 1: the message is left to the caller
    CHECK(!ServerBody_SendMsg(1, pMsg));
    CHECK(pMsg->m_lRefCount == 1);
    g_Net.ReleaseSent();

    // outgoing cords without a session: cord 4 is connected now, nothing to do
    CHECK(!ServerCord_ConnectOutgoingCords());
    CHECK(g_Net.m_KeepAlive.empty());

    // state change of a body: observer, then 0x2005 to the notify bodies' routes and sessions
    CObserver observer;
    g_pServerBodyStateObserver = &observer;
    g_setNotifySession.insert(500);
    g_Net.ReleaseSent();
    CHECK(ServerBody_SetStateAndBroadcast2005(3, 4, 90));
    CHECK(observer.m_nCalls == 1 && observer.m_pBody == ServerBodyInfo_Find(4));
    CHECK(observer.m_nOld == 5 && ServerBodyInfo_Find(4)->nState == 3);
    // body 4 via session 78, body 7 is session 90 (excluded), session 500
    CHECK(g_Net.m_Sent.size() == 4);
    CHECK(g_Net.m_Sent[0].dwSessionID == 78 && g_Net.m_Sent[2].dwSessionID == 500);
    {
        CMsg* pHeader = g_Net.m_Sent[0].pMsg;
        BYTE bType;
        WORD wCount, wOpcode;
        *pHeader >> bType >> wCount >> wOpcode;
        CHECK(bType == 1 && wCount == 1 && wOpcode == 0x2005);
        std::vector<BYTE> v = SentPayload(1, 1);
        const BYTE ab[] = { 1, 0, 1, 4, 0, 3, 0, 0, 0, 2 };
        CHECK(v.size() == sizeof(ab) && memcmp(&v[0], ab, sizeof(ab)) == 0);
    }
    g_Net.ReleaseSent();
    CHECK(g_MassiveMsgPool.m_nInUse == 0);

    // unchanged state of another body: no broadcast; this server's own body always
    CHECK(ServerBody_SetStateAndBroadcast2005(3, 4, 0));
    CHECK(g_Net.m_Sent.empty());
    CHECK(ServerBody_SetStateAndBroadcast2005(4, 5, 0));
    CHECK(g_Net.m_Sent.size() == 6);
    g_Net.ReleaseSent();
    CHECK(!ServerBody_SetStateAndBroadcast2005(1, 99, 0));
    CHECK(observer.m_nCalls == 3);
    g_pServerBodyStateObserver = NULL;

    // cord status
    CHECK(ServerBody_UpdateStatusAndNotify(4, 7, 500));
    CHECK(g_Net.m_Sent.size() == 4);
    {
        std::vector<BYTE> v = SentPayload(1, 1);
        const BYTE ab[] = { 2, 0, 1, 4, 0, 0, 0, 7, 0, 0, 0, 2 };
        CHECK(v.size() == sizeof(ab) && memcmp(&v[0], ab, sizeof(ab)) == 0);
    }
    g_Net.ReleaseSent();
    CHECK(ServerBody_UpdateStatusAndNotify(4, 7, 0));
    CHECK(g_Net.m_Sent.empty());
    CHECK(!ServerBody_UpdateStatusAndNotify(999, 1, 0));

    // closing session 78: cord 4 loses its session, body 4 goes to state 0
    CHECK(ServerCord_OnClosed(12345) == NULL);
    CHECK(ServerCord_OnClosed(78) == pCord4);
    CHECK(pCord4->dwSessionID == 0 && pCord4->dwStatus == 0);
    CHECK(ServerBodyInfo_Find(4)->nState == 0);
    CHECK(g_mapSessionCord.count(78) == 0 && g_mapSessionCord.size() == 1);
    CHECK(g_strLastLog == "server cord closed : 78");
    // cord status 7 -> 0 (to 7 via 90, 500; to 4: cord without session, session 0 == excluded 0)
    // then the body state (same targets)
    CHECK(g_Net.m_Sent.size() == 8);
    g_Net.ReleaseSent();

    // cord 4 is free again: reconnect through the public address of body 4's machine
    CHECK(!ServerCord_ConnectOutgoingCords());
    CHECK(g_Net.m_KeepAlive.size() == 1);
    CHECK(g_Net.m_KeepAlive[0].strHost == "10.0.0.100" && g_Net.m_KeepAlive[0].wPort == 15882);
    CHECK(g_Net.m_KeepAlive[0].strBind == "(null)");
    pCord4->byPrivate = 1;
    g_Net.m_KeepAlive.clear();
    ServerCord_ConnectOutgoingCords();
    CHECK(g_Net.m_KeepAlive.size() == 1 && g_Net.m_KeepAlive[0].strHost == "192.168.0.100");
    CHECK(g_Net.m_KeepAlive[0].strBind == "192.168.0.101");

    // FWDATA-002: a route without a session gets the parts on session 0
    g_setNotifySession.clear();
    g_Net.ReleaseSent();
    CHECK(ServerBody_UpdateStatusAndNotify(6, 1, 90));
    CHECK(g_Net.m_Sent.size() == 2 && g_Net.m_Sent[0].dwSessionID == 0);
    g_Net.ReleaseSent();
}

static void TestCertDataSerialize()
{
    CMassiveMsg* pOut = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pOut->SetOpcode(0xA003);
    CHECK(CertData_SerializeRecordListsToMsg(pOut));
    CMassiveMsg* pIn = Loopback(pOut);
    // 3 lists: 0, n * (1 + record), 2
    CHECK(pIn->GetUnreadSize() == 3 * 2 + 2 * (1 + 289) + 2 * (1 + 324) + 3 * (1 + 5));
    BYTE ab[2];
    pIn->Read(ab, 2);
    CHECK(ab[0] == 0 && ab[1] == 1);
    CERT_DATA_GROUP group;
    pIn->Read(&group, sizeof(group));
    CHECK(group.byID == 3);

    CertData_FreeRecordListsAndIPMaps();
    pIn->Free();

    // the read side stops at any flag other than 1
    pOut = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pOut->SetOpcode(0xA003);
    BYTE abData[] = { 0, 1 };
    pOut->Write(abData, 2);
    CERT_DATA_GROUP g = Zero<CERT_DATA_GROUP>();
    g.byID = 9;
    pOut->Write(&g, sizeof(g));
    BYTE b = 2;
    pOut->Write(&b, 1);
    BYTE abSettings[] = { 0, 1 };
    pOut->Write(abSettings, 2);
    CERT_DATA_SETTING s = Zero<CERT_DATA_SETTING>();
    s.dwValue = 5;
    strcpy_s(s.szName, "Ignore_Shard_MaxUser");
    pOut->Write(&s, sizeof(s));
    b = 0;
    pOut->Write(&b, 1);
    BYTE abIPs[] = { 0, 1 };
    pOut->Write(abIPs, 2);
    CERT_DATA_GROUP_IP ip = Zero<CERT_DATA_GROUP_IP>();
    ip.byGroupID = 9;
    ip.dwIP = 0x0A0B0C0D;
    pOut->Write(&ip, sizeof(ip));
    b = 2;
    pOut->Write(&b, 1);
    pIn = Loopback(pOut);
    CHECK(CertData_ReadRecordLists(pIn));
    CHECK(pIn->GetUnreadSize() == 0);
    pIn->Free();
    CHECK(PrivIPMap_ContainsIP(9, 0x0A0B0C0D) && !PrivIPMap_ContainsIP(3, 0x0100007F));
    CHECK(CertData_GetIgnoreShardMaxUserValue() == 5);
    CertData_FreeRecordListsAndIPMaps();
    CHECK(g_pCertDataSettingList == NULL && g_mapCertDataSetting.empty());
}

static void TestModuleVersion()
{
    ModuleVersionList* pModules = new ModuleVersionList;
    MODULE_VERSION* pMod = (MODULE_VERSION*)operator new(sizeof(MODULE_VERSION));
    memset(pMod, 0, sizeof(*pMod));
    pMod->byModuleID = 9;
    pMod->dwVersion = 188;
    pModules->push_back(pMod);

    ModuleVersionFileList* pFiles = new ModuleVersionFileList;
    for (DWORD i = 0; i < 4; ++i)
    {
        MODULE_VERSION_FILE* pFile =
            (MODULE_VERSION_FILE*)operator new(sizeof(MODULE_VERSION_FILE));
        memset(pFile, 0, sizeof(*pFile));
        pFile->dwID = 10 + i;
        pFile->dwVersion = 186 + i;
        pFile->byModuleID = i == 3 ? 8 : 9;
        sprintf_s(pFile->szName, "file%u.pk2", i);
        strcpy_s(pFile->szPath, i == 2 ? "." : "Data");
        pFile->dwSize = 0x1000 * i;
        pFile->b532 = (BYTE)i;
        pFile->byValid = 1;
        pFiles->push_back(pFile);
    }
    CHECK(ModuleVersion_StoreAndIndexLists(pFiles, pModules));
    CHECK(ModuleVersion_GetLatest(9) == 188 && ModuleVersion_GetLatest(8) == 0);
    CHECK(g_hmapModuleVersionFile.size() == 4 && g_hmapModuleVersionFile[12]->szPath[0] == 0);

    CMassiveMsg* pOut = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pOut->SetOpcode(0xA100);
    ModuleVersion_WritePatchFiles(pOut, 9, 186);
    CMassiveMsg* pIn = Loopback(pOut);
    // files 11 (Data) and 12 (root), then the end byte
    const BYTE ab[] = {
        1, 11, 0, 0, 0, 9, 0, 'f', 'i', 'l', 'e', '1', '.', 'p', 'k', '2',
        4, 0, 'D', 'a', 't', 'a', 0, 0x10, 0, 0, 1,
        1, 12, 0, 0, 0, 9, 0, 'f', 'i', 'l', 'e', '2', '.', 'p', 'k', '2',
        0, 0, 0, 0x20, 0, 0, 2,
        0 };
    CHECK(pIn->GetUnreadSize() == sizeof(ab));
    BYTE abRead[sizeof(ab)];
    pIn->Read(abRead, sizeof(ab));
    CHECK(memcmp(abRead, ab, sizeof(ab)) == 0);
    pIn->Free();

    ModuleVersion_FreeAllData();
    CHECK(g_pModuleVersionList == NULL && g_pModuleVersionFileList == NULL);
    CHECK(g_mapModuleVersion.empty() && g_hmapModuleVersionFile.empty());
}

int main()
{
    g_pBSNet = &g_Net;

    CHECK(ServerName_CompareCertification() != 0);

    TestCertificationServer();
    TestCertificationRequest();
    TestCords();
    TestCertDataSerialize();
    TestModuleVersion();

    ServerApp_FreeCertArchData();
    CertData_FreeRecordListsAndIPMaps();
    CHECK(g_pServerCordList == NULL && g_mapSessionCord.empty() && g_setNotifyBody.empty());
    CHECK(g_MassiveMsgPool.m_nInUse == 0);
    g_Net.ReleaseSent();
    CHECK(g_Net.m_Pool.m_nInUse == 0);

    return TEST_RESULT("ServerDataTest");
}
