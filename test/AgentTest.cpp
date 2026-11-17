// AgentServer application: CAgentServer on the real network engine, which connects to itself. A
// client task of the same engine plays the GlobalManager (the game server and the gateway are
// bodies behind it) and the game clients; packets are built byte by byte and the answers
// compared byte by byte. XTrap4Server.dll is the stand-in AgentTestXTrap.dll (copied into the
// working directory), or missing.

#include "BSLib/BSDebug.h"
#include "BSNet/NetEngine.h"
#include "BSNet/SockListener.h"
#include "AgentServer/AgentMainProcess.h"
#include "AgentServer/AgentServer.h"
#include "AgentServer/Token.h"
#include "AgentServer/User.h"
#include "AgentServer/XTrap.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/LogQueue.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"

#include <stdio.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>

#include "LogCapture.h"
#include "TestCheck.h"

using namespace ServerFramework;

typedef std::vector<BYTE> Bytes;

static CRITICAL_SECTION s_cs;
static std::string s_strDir;
static std::string s_strBin;

// ---------------------------------------------------------------------------------------------
// Byte helpers

static void Put(Bytes& v, const void* p, size_t n)
{
    v.insert(v.end(), (const BYTE*)p, (const BYTE*)p + n);
}

static void PutB(Bytes& v, BYTE b)
{
    v.push_back(b);
}

static void PutW(Bytes& v, WORD w)
{
    Put(v, &w, 2);
}

static void PutD(Bytes& v, DWORD d)
{
    Put(v, &d, 4);
}

static void PutS(Bytes& v, const char* psz)
{
    PutW(v, (WORD)strlen(psz));
    Put(v, psz, strlen(psz));
}

static std::string Hex(const Bytes& v)
{
    std::string s;
    char sz[4];
    for (size_t i = 0; i < v.size() && i < 64; ++i)
    {
        sprintf_s(sz, "%02x ", v[i]);
        s += sz;
    }
    return s;
}

static bool Same(const Bytes& vGot, const Bytes& vWant)
{
    if (vGot == vWant)
        return true;
    printf("  got:  %s(%u)\n  want: %s(%u)\n", Hex(vGot).c_str(), (unsigned)vGot.size(),
           Hex(vWant).c_str(), (unsigned)vWant.size());
    return false;
}

static void WriteDiskFile(const std::string& strPath, const void* p, size_t n)
{
    FILE* pFile = NULL;
    fopen_s(&pFile, strPath.c_str(), "wb");
    CHECK(pFile != NULL);
    if (!pFile)
        return;
    fwrite(p, 1, n, pFile);
    fclose(pFile);
}

// ---------------------------------------------------------------------------------------------
// Log lines

static std::vector<CAPTURED_LOG> s_vecLog;
static size_t s_nLogChecked;

static void DrainLogs()
{
    std::vector<CAPTURED_LOG> v = LogCapture_Drain();
    s_vecLog.insert(s_vecLog.end(), v.begin(), v.end());
}

static void SkipLogs()
{
    DrainLogs();
    s_nLogChecked = s_vecLog.size();
}

// Waits for a line starting with strPrefix after the last line found.
static bool WaitLog(const std::string& strPrefix, DWORD dwFlags = (DWORD)-1,
                    std::string* pstrLine = NULL, bool bReport = true, DWORD dwTimeout = 3000)
{
    DWORD dwStart = GetTickCount();
    for (;;)
    {
        DrainLogs();
        for (size_t i = s_nLogChecked; i < s_vecLog.size(); ++i)
        {
            if (s_vecLog[i].strText.compare(0, strPrefix.size(), strPrefix) == 0
                && (dwFlags == (DWORD)-1 || s_vecLog[i].dwFlags == dwFlags))
            {
                s_nLogChecked = i + 1;
                if (pstrLine)
                    *pstrLine = s_vecLog[i].strText;
                return true;
            }
        }
        if (GetTickCount() - dwStart > dwTimeout)
        {
            if (bReport)
                printf("  missing log line: %s\n", strPrefix.c_str());
            return false;
        }
        Sleep(5);
    }
}

// ---------------------------------------------------------------------------------------------
// The client task: records every packet and session event it gets.

enum
{
    TEST_TASK_CLIENT    = 0x01000100,
};

struct PACKET
{
    DWORD   dwSession;
    WORD    wOpcode;
    Bytes   data;
};

static std::deque<PACKET> s_queIn;

class CTestClient : public CServerProcessBase
{
    BS_DECLARE_DYNCREATE(CTestClient)

public:
    virtual void InitMsgHandlers()
    {
    }

    virtual void OnUnhandledMsg(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
    {
        PACKET packet;
        packet.dwSession = pMsg->m_dwSessionID;
        packet.wOpcode = pMsg->GetOpcode();
        packet.data.assign(pMsg->m_pData + pMsg->m_wReadPos, pMsg->m_pData + pMsg->m_wWritePos);
        pMsg->SkipRemaining();
        EnterCriticalSection(&s_cs);
        s_queIn.push_back(packet);
        LeaveCriticalSection(&s_cs);
    }
};

BS_IMPLEMENT_DYNCREATE(CTestClient, &CServerProcessBase::classCServerProcessBase)

static bool TakePacket(DWORD dwSession, WORD wOpcode, Bytes* pData, DWORD dwTimeout = 3000,
                       bool bReport = true)
{
    DWORD dwStart = GetTickCount();
    for (;;)
    {
        EnterCriticalSection(&s_cs);
        for (std::deque<PACKET>::iterator it = s_queIn.begin(); it != s_queIn.end(); ++it)
        {
            if (it->dwSession == dwSession && it->wOpcode == wOpcode)
            {
                if (pData)
                    *pData = it->data;
                s_queIn.erase(it);
                LeaveCriticalSection(&s_cs);
                return true;
            }
        }
        LeaveCriticalSection(&s_cs);
        if (GetTickCount() - dwStart > dwTimeout)
        {
            if (bReport)
                printf("  no packet 0x%04x on session %u\n", wOpcode, dwSession);
            return false;
        }
        Sleep(5);
    }
}

static bool NoPacket(DWORD dwSession, WORD wOpcode, DWORD dwWait = 200)
{
    return !TakePacket(dwSession, wOpcode, NULL, dwWait, false);
}

static void ClearPackets()
{
    Sleep(100);
    EnterCriticalSection(&s_cs);
    s_queIn.clear();
    LeaveCriticalSection(&s_cs);
}

static void SendPacket(DWORD dwSession, WORD wOpcode, const Bytes& data)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(wOpcode);
    if (!data.empty())
        pMsg->WriteBytes(&data[0], (WORD)data.size());
    g_pBSNet->SendMsg(dwSession, pMsg);
    g_pBSNet->ReleaseMsg(pMsg);
}

// The data of the next massive message with the inner opcode wOpcode on the session.
static bool TakeMassive(DWORD dwSession, WORD wOpcode, Bytes* pData)
{
    DWORD dwStart = GetTickCount();
    for (;;)
    {
        EnterCriticalSection(&s_cs);
        size_t nHeader = s_queIn.size();
        size_t nParts = 0;
        std::vector<size_t> vecParts;
        for (size_t i = 0; i < s_queIn.size(); ++i)
        {
            const PACKET& packet = s_queIn[i];
            if (packet.dwSession != dwSession || packet.wOpcode != MASSIVE_MSG_OPCODE
                || packet.data.empty())
            {
                continue;
            }
            if (nHeader == s_queIn.size())
            {
                if (packet.data[0] == 1 && packet.data.size() == 5
                    && *(const WORD*)&packet.data[3] == wOpcode)
                {
                    nHeader = i;
                    nParts = *(const WORD*)&packet.data[1];
                }
            }
            else if (packet.data[0] == 0 && vecParts.size() < nParts)
            {
                vecParts.push_back(i);
            }
            else if (packet.data[0] == 1)
            {
                break;
            }
        }
        if (nHeader != s_queIn.size() && vecParts.size() == nParts)
        {
            pData->clear();
            for (size_t i = 0; i < vecParts.size(); ++i)
            {
                const Bytes& part = s_queIn[vecParts[i]].data;
                pData->insert(pData->end(), part.begin() + 1, part.end());
            }
            for (size_t i = vecParts.size(); i > 0; --i)
                s_queIn.erase(s_queIn.begin() + vecParts[i - 1]);
            s_queIn.erase(s_queIn.begin() + nHeader);
            LeaveCriticalSection(&s_cs);
            return true;
        }
        LeaveCriticalSection(&s_cs);
        if (GetTickCount() - dwStart > 3000)
        {
            printf("  no massive 0x%04x on session %u\n", wOpcode, dwSession);
            return false;
        }
        Sleep(5);
    }
}

static u_short s_wPort;

struct CLIENT
{
    DWORD dwClient;     // the client task's session
    DWORD dwServer;     // the agent's session of the same connection
};

static CLIENT Connect()
{
    CLIENT c = { 0, 0 };
    c.dwClient = g_pBSNet->Connect("127.0.0.1", s_wPort, NULL, TEST_TASK_CLIENT, 0);
    CHECK(c.dwClient != 0);
    std::string strLine;
    CHECK(WaitLog("session created [", 0, &strLine));
    sscanf_s(strLine.c_str(), "session created [%u]", &c.dwServer);
    CHECK(c.dwServer != 0 && c.dwServer != c.dwClient);
    CHECK(TakePacket(c.dwClient, 0x1001, NULL));
    CHECK(TakePacket(c.dwClient, 0x1003, NULL));
    return c;
}

static CAgentMainProcess* Main()
{
    return static_cast<CAgentMainProcess*>(g_pBSNet->GetProactor(SERVER_TASK_MAIN)->GetJob());
}

// ---------------------------------------------------------------------------------------------
// Architecture: body 1 is this AgentServer (shard 10), 2 the GlobalManager (connects to it), 3
// the shard's game server and 4 a gateway, both behind the GlobalManager.

enum
{
    MOD_AGENT = 9, MOD_GLOBAL_MANAGER = 3, MOD_GAME = 12, MOD_GATEWAY = 6, MOD_FARM_MANAGER = 7,
    TEST_CONTENT = 22, TEST_SHARD = 10,
    BODY_AGENT = 1, BODY_GM = 2, BODY_GAME = 3, BODY_GATEWAY = 4,
};

template <class T>
static T Zero()
{
    T t;
    memset(&t, 0, sizeof(t));
    return t;
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

static CERT_MODULE Module(BYTE byID, const char* pszName)
{
    CERT_MODULE m = Zero<CERT_MODULE>();
    m.byID = byID;
    strcpy_s(m.szName, pszName);
    return m;
}

static SERVER_BODY Body(WORD wID, BYTE byModule, BYTE byFarm, WORD wShard, WORD wPort)
{
    SERVER_BODY b = Zero<SERVER_BODY>();
    b.wID = wID;
    b.byDivisionID = 3;
    b.byFarmID = byFarm;
    b.wShardID = wShard;
    b.dwMachineID = 100;
    b.byModuleID = byModule;
    b.wPort = wPort;
    b.nState = 5;
    return b;
}

static void SetUpArchitecture()
{
    std::vector<CERT_MODULE> modules;
    modules.push_back(Module(MOD_AGENT, "AgentServer"));
    modules.push_back(Module(MOD_GLOBAL_MANAGER, "GlobalManager"));
    modules.push_back(Module(MOD_GAME, "SR_GameServer"));
    modules.push_back(Module(MOD_GATEWAY, "GatewayServer"));
    modules.push_back(Module(MOD_FARM_MANAGER, "FarmManager"));
    std::vector<CERT_CONTENT> contents(1, Zero<CERT_CONTENT>());
    contents[0].byID = TEST_CONTENT;
    std::vector<CERT_DIVISION> divisions(1, Zero<CERT_DIVISION>());
    divisions[0].byID = 3;
    std::vector<CERT_FARM> farms(1, Zero<CERT_FARM>());
    farms[0].byID = 1;
    farms[0].byDivisionID = 3;
    std::vector<CERT_FARM_CONTENT> farmContents;
    std::vector<CERT_SHARD> shards(1, Zero<CERT_SHARD>());
    shards[0].wID = TEST_SHARD;
    shards[0].byFarmID = 1;
    shards[0].ab3[0] = TEST_CONTENT;
    shards[0].wManagerID = BODY_GAME;
    std::vector<CERT_MACHINE> machines(1, Zero<CERT_MACHINE>());
    machines[0].dwID = 100;
    machines[0].byDivisionID = 3;
    strcpy_s(machines[0].szPublicIP, "127.0.0.1");
    strcpy_s(machines[0].szPrivateIP, "127.0.0.1");
    std::vector<SERVER_BODY> bodies;
    bodies.push_back(Body(BODY_AGENT, MOD_AGENT, 1, TEST_SHARD, 15884));
    bodies.push_back(Body(BODY_GM, MOD_GLOBAL_MANAGER, 0, 0, 15882));
    bodies.push_back(Body(BODY_GAME, MOD_GAME, 1, TEST_SHARD, 15883));
    bodies.push_back(Body(BODY_GATEWAY, MOD_GATEWAY, 0, 0, 15779));
    bodies[0].nState = 3;
    std::vector<SERVER_CORD> cords(3, Zero<SERVER_CORD>());
    cords[0].dwID = 50;
    cords[0].wFromID = BODY_GM;
    cords[0].wToID = BODY_AGENT;
    cords[1].dwID = 51;
    cords[1].wFromID = BODY_GAME;
    cords[1].wToID = BODY_GM;
    cords[2].dwID = 52;
    cords[2].wFromID = BODY_GATEWAY;
    cords[2].wToID = BODY_GM;

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
    BYTE bNoCertData = 0;
    p->Write(&bNoCertData, 1);
    p->BeginRead();
    CHECK(CServerProcessMain_ParseA003CertArchAndBuildTargets(p));
    p->Free();
    CHECK(g_pSelfBody && g_pSelfBody->wID == BODY_AGENT);
    CHECK(g_pSelfBody && g_pSelfBody->pShard && g_pSelfBody->pShard->wID == TEST_SHARD);
    CHECK(g_pSelfBody && g_pSelfBody->pDivision
          && g_pSelfBody->pDivision->wGlobalManagerID == BODY_GM);
    CHECK(ServerBody_Find(BODY_GAME) == ServerCordMap_FindById(50));
}

// ---------------------------------------------------------------------------------------------
// The fake XTrap DLL

struct FAKE_XTRAP_STATE
{
    DWORD   nStart;
    DWORD   adwStartArg[2];
    BYTE    abMapFirst[2];
    void*   pMap;
    void*   pStartReserved;
    DWORD   nSetActiveCode;
    DWORD   dwActiveCode;
    DWORD   dwActiveCodeResult;
    DWORD   nSessionInit;
    DWORD   dwSessionInitResult;
    DWORD   nStep1;
    DWORD   nStep3;
    void*   pStep3Session;
    BYTE    abStep3[1024];
    DWORD   dwStep1Result;          // returned by XTrap_CS_Step1
};

static FAKE_XTRAP_STATE* s_pXTrap;

static bool CopyBinFile(const char* pszName, const std::string& strTo)
{
    std::string strFrom = s_strBin + "\\" + pszName;
    return CopyFileA(strFrom.c_str(), strTo.c_str(), FALSE) != FALSE;
}

// ---------------------------------------------------------------------------------------------
// Unit parts (no engine)

static void TestConfig()
{
    CHECK(sizeof(CAgentMainProcess) == 0x400D4);
    CHECK(CAgentMainProcess::classCAgentMainProcess.m_nObjectSize == 0x400D4);
    CHECK(strcmp(CAgentMainProcess::classCAgentMainProcess.m_lpszClassName,
                 "CAgentMainProcess") == 0);
    CHECK(sizeof(sUser) == 0x190 && sizeof(CToken) == 0x58);
    CHECK(sizeof(CUserManager) == 0x1C && sizeof(CTokenManager) == 0x10);

    // the dynamic initializers
    CHECK(g_UserManager.m_bTrafficFilter == FALSE);
    CHECK(g_UserManager.m_dwTrafficMaxPackets == 100 && g_UserManager.m_dwTrafficPeriod == 60000);
    CHECK(g_TokenIDPool.m_dwNextID == 501 && g_TokenIDPool.m_deqFree.size() == 500);
    CHECK(g_TokenIDPool.m_dwGrowBy == 500 && g_TokenIDPool.m_dwInUse == 0);
    CHECK(g_UserPool.m_nItemsPerBlock == 500);
    CHECK(g_AgentMainProcessUnused.m_mapBroadcast.empty());

    // vf03 (0x4014a0)
    g_AgentServer.SetDefaultEngineConfig();
    BSNET_INIT_PARAM& p = g_AgentServer.m_EngineParam;
    CHECK(p.bSecurityMode == 2 && p.dwFlags == 0x13 && p.nAcceptCount == 100);
    CHECK(p.dwMaxConnectTime == 60 && p.dw8C == 0x112 && p.dw90 == 0x201);
    CHECK(p.aSocket[0].nSendQueueLimit == -1 && p.aSocket[1].nSendQueueLimit == -1);
    CHECK(p.aSocket[0].dwSendMode == 0 && p.aSocket[1].dwSendMode == 1);
    CHECK(p.dwFileCacheLimit == 0 && p.nSendFileType == 1 && p.nRecvFileType == 1);

    // ServerConfig_FindExtraValue (0x40d840)
    g_ServerGlobals.mapExtra["AGENT_KEY"] = "value";
    CHECK(strcmp(ServerConfig_FindExtraValue("AGENT_KEY"), "value") == 0);
    CHECK(ServerConfig_FindExtraValue("agent_key") == NULL);
    g_ServerGlobals.mapExtra.clear();

    // the counter callbacks (0x4015a0 / 0x4015d0 / 0x4015e0)
    MONITOR_COUNTER counter;
    memset(&counter, 0, sizeof(counter));
    g_dwAgentMsgSentCount = 7;
    g_dwAgentMsgSentBytes = 300;
    counter.dwID = 0;
    CHECK(AgentServer_ReadMsgSentCounter(&counter) && counter.fValue == 7.0f);
    CHECK(g_dwAgentMsgSentCount == 0 && g_dwAgentMsgSentBytes == 300);
    counter.dwID = 1;
    CHECK(AgentServer_ReadMsgSentCounter(&counter) && counter.fValue == 300.0f);
    CHECK(g_dwAgentMsgSentBytes == 0);
    counter.dwID = 2;
    counter.fValue = 5.0f;
    CHECK(AgentServer_ReadMsgSentCounter(&counter) && counter.fValue == 5.0f);
    CHECK(AgentServer_ReadUserCount(&counter) && counter.fValue == 0.0f);
    CHECK(AgentServer_ReadTokenCount(&counter) && counter.fValue == 0.0f);
}

static void TestTokens()
{
    // a token takes an ID of the pool and gives it back
    CToken* pToken = new CToken;
    CHECK(pToken->m_dwTokenID == 1 && g_TokenIDPool.m_dwInUse == 1);
    pToken->m_dwClientIP = 0x0100007F;
    pToken->m_strID = "Name";
    pToken->m_strPassword = "Secret";
    pToken->m_dwJID = 4711;
    pToken->m_bySecPrimary = 3;
    pToken->m_bySecContent = 4;
    pToken->m_w50 = 0x1234;
    pToken->m_dw54 = 0x89ABCDEF;
    CHECK(g_TokenManager.Add(pToken));
    CHECK(!g_TokenManager.Add(pToken));
    WORD w;
    DWORD dw;
    CHECK(g_TokenManager.GetExtra(1, &w, &dw) == TRUE && w == 0x1234 && dw == 0x89ABCDEF);
    w = 9;
    dw = 9;
    CHECK(g_TokenManager.GetExtra(2, &w, &dw) == FALSE && w == 0 && dw == 0);

    // the check is case-sensitive and uses the token up even when it fails
    DWORD dwJID = 0;
    BYTE b1 = 0;
    BYTE b2 = 0;
    CHECK(!g_TokenManager.Consume(1, 0x0100007F, "name", "Secret", &dwJID, &b1, &b2));
    CHECK(g_TokenManager.m_mapToken.empty() && g_TokenIDPool.m_dwInUse == 0 && dwJID == 0);
    CHECK(!g_TokenManager.Consume(1, 0x0100007F, "Name", "Secret", &dwJID, &b1, &b2));

    pToken = new CToken;
    pToken->m_dwClientIP = 0x0100007F;
    pToken->m_strID = "Name";
    pToken->m_strPassword = "Secret";
    pToken->m_dwJID = 4711;
    pToken->m_bySecPrimary = 3;
    pToken->m_bySecContent = 4;
    DWORD dwID = pToken->m_dwTokenID;
    CHECK(dwID == 2);
    CHECK(g_TokenManager.Add(pToken));
    CHECK(!g_TokenManager.Consume(dwID, 0x0200007F, "Name", "Secret", &dwJID, &b1, &b2));
    pToken = new CToken;
    pToken->m_dwClientIP = 0x0100007F;
    pToken->m_strID = "Name";
    pToken->m_strPassword = "Secret";
    pToken->m_dwJID = 4711;
    pToken->m_bySecPrimary = 3;
    pToken->m_bySecContent = 4;
    dwID = pToken->m_dwTokenID;
    g_TokenManager.Add(pToken);
    CHECK(g_TokenManager.Consume(dwID, 0x0100007F, "Name", "Secret", &dwJID, &b1, &b2));
    CHECK(dwJID == 4711 && b1 == 3 && b2 == 4 && g_TokenManager.m_mapToken.empty());

    g_TokenManager.Add(new CToken);
    g_TokenManager.Add(new CToken);
    CHECK(g_TokenIDPool.m_dwInUse == 2);
    g_TokenManager.RemoveAll();
    CHECK(g_TokenManager.m_mapToken.empty() && g_TokenIDPool.m_dwInUse == 0);
}

static void TestUsers()
{
    sUser* pUser = UserPool_Alloc();
    CHECK(pUser->bInUse == 1 && g_UserPool.m_nInUse == 1);
    CHECK(pUser->dwMacLow == 0 && pUser->wMacHigh == 0);
    pUser->dwSessionID = 900;
    pUser->dwJID = 77;
    CHECK(g_UserManager.Register(pUser));
    sUser* pSecond = UserPool_Alloc();
    pSecond->dwSessionID = 900;
    pSecond->dwJID = 78;
    CHECK(!g_UserManager.Register(pSecond));
    pSecond->Release();
    CHECK(g_UserPool.m_nInUse == 1);
    pSecond->Release();
    CHECK(g_UserPool.m_nInUse == 1);
    CHECK(g_UserManager.Find(900) == pUser && g_UserManager.Find(901) == NULL);
    CHECK(g_UserManager.FindByJID(77) == pUser && g_UserManager.FindByJID(78) == NULL);
    CHECK(g_UserManager.Unregister(900) && !g_UserManager.Unregister(900));
    CHECK(g_UserPool.m_nInUse == 0 && pUser->bInUse == 0);

    // traffic filter (0x40bfc0): the n-th packet inside the period fails
    pUser = UserPool_Alloc();
    pUser->strID = "flood";
    pUser->dwIP = 0x04030201;
    pUser->ResetTrafficCounter();
    CHECK(g_UserManager.CheckTraffic(pUser) && pUser->dwRecvCount == 0);
    g_UserManager.m_bTrafficFilter = TRUE;
    g_UserManager.m_dwTrafficMaxPackets = 3;
    g_UserManager.m_dwTrafficPeriod = 60000;
    CHECK(g_UserManager.CheckTraffic(NULL));
    CHECK(g_UserManager.CheckTraffic(pUser));
    CHECK(g_UserManager.CheckTraffic(pUser));
    CHECK(!g_UserManager.CheckTraffic(pUser) && pUser->dwRecvCount == 3);
    CHECK(g_UserManager.CheckTraffic(pUser) && pUser->dwRecvCount == 4);
    // after the period the count starts again
    pUser->ResetTrafficCounter();
    pUser->dwRecvTick -= 70000;
    CHECK(g_UserManager.CheckTraffic(pUser) && g_UserManager.CheckTraffic(pUser));
    CHECK(g_UserManager.CheckTraffic(pUser) && pUser->dwRecvCount == 0);
    g_UserManager.m_bTrafficFilter = FALSE;
    pUser->Release();

    // XTrap_IsStepDue (0x404820)
    CHECK(XTrap_IsStepDue(NULL) == 0);
    pUser = UserPool_Alloc();
    pUser->dwXTrapTick = GetTickCount();
    CHECK(XTrap_IsStepDue(pUser) == 0);
    pUser->dwXTrapTick = GetTickCount() - XTRAP_STEP_INTERVAL - 100;
    CHECK(XTrap_IsStepDue(pUser) == 1 && GetTickCount() - pUser->dwXTrapTick < 1000);
    pUser->Release();
}

// ---------------------------------------------------------------------------------------------
// Engine parts

static void TestStartup()
{
    CHECK(g_AgentServer.InitNetEngine());
    CHECK(g_AgentServer.m_EngineParam.bSecurityMode == 2);
    CHECK(g_AgentServer.RegisterServerTasks());
    CHECK(Main()->GetRuntimeClass() == &CAgentMainProcess::classCAgentMainProcess);
    // the token timer of vf02 (timer 2, 3000 ms)
    ServerProcessTimerMap::iterator it = Main()->m_mapTimer.find(AGENT_TOKEN_TIMER_ID);
    CHECK(it != Main()->m_mapTimer.end() && it->second.dwPeriod == 3000);
    SetUpArchitecture();
}

// XTrap_Initialize (0x401110) and XTrap_LoadDll (0x40de70) without the files, then the
// application's start (vf11) with the stand-in DLL.
static void TestXTrapStart()
{
    SetCurrentDirectoryA(s_strDir.c_str());
    BYTE abMap[XTRAP_MAP_SIZE];
    CHECK(XTrap_Initialize() == 1);
    CHECK(WaitLog("[For Xtrap] Failed to open XTrap map1 file!!", 0x2000000));
    memset(abMap, 0x11, sizeof(abMap));
    WriteDiskFile(s_strDir + "\\map1.cs3", abMap, sizeof(abMap));
    CHECK(XTrap_Initialize() == 1);
    CHECK(WaitLog("[For Xtrap] Failed to open XTrap map2 file!!", 0x2000000));
    memset(abMap, 0x22, sizeof(abMap));
    WriteDiskFile(s_strDir + "\\map2.cs3", abMap, sizeof(abMap));
    CHECK(XTrap_Initialize() == 1);
    CHECK(WaitLog("[For Xtrap] Failed to XTrap Load DLL!!", 0x2000000));
    CHECK(XTrap_LoadDll() == (XTRAP_LOAD_FAILED | ERROR_MOD_NOT_FOUND));
    CHECK(g_abXTrapMap[0][0] == 0x11 && g_abXTrapMap[1][XTRAP_MAP_SIZE - 1] == 0x22);

    // a DLL without XTrap_S_RecvGamePacket
    std::string strDll = s_strDir + "\\XTrap4Server.Dll";
    CHECK(CopyBinFile("AgentTestXTrapMissing.dll", strDll));
    CHECK(XTrap_LoadDll() == XTRAP_LOAD_MISSING_PROC);
    CHECK(g_hXTrapModule != NULL && g_pfnXTrap_S_Start != NULL);
    CHECK(g_pfnXTrap_S_RecvGamePacket == NULL);
    FreeLibrary(g_hXTrapModule);
    DeleteFileA(strDll.c_str());

    // the start of the application: state 4, the traffic filter settings, XTrap runs
    CHECK(CopyBinFile("AgentTestXTrap.dll", strDll));
    g_ServerGlobals.mapExtra["traffic_filter_activity"] = "SERVICE_ON";
    g_ServerGlobals.mapExtra["traffic_filter_packet_count"] = "5";
    g_ServerGlobals.mapExtra["traffic_filter_check_perid"] = "1234";
    SkipLogs();
    CHECK(g_AgentServer.OnLocalDataInitialized());
    CHECK(g_pSelfBody->nState == 4);
    CHECK(WaitLog("Traffic Filter Turned On !!!", 0x2000000));
    CHECK(WaitLog("Traffic Filter Configuration [5 Packets / 1234 ms]", 0x2000000));
    CHECK(WaitLog("###   XTrap Initialized Sucess   ###", 0x2000000));
    CHECK(g_UserManager.m_bTrafficFilter && g_UserManager.m_dwTrafficMaxPackets == 5
          && g_UserManager.m_dwTrafficPeriod == 1234);
    s_pXTrap = NULL;
    if (g_hXTrapModule)
    {
        typedef FAKE_XTRAP_STATE* (__cdecl* PFN_STATE)();
        PFN_STATE pfn = (PFN_STATE)GetProcAddress(g_hXTrapModule, "FakeXTrap_GetState");
        CHECK(pfn != NULL);
        if (pfn)
            s_pXTrap = pfn();
    }
    CHECK(s_pXTrap != NULL);
    if (s_pXTrap)
    {
        CHECK(s_pXTrap->nStart == 1 && s_pXTrap->adwStartArg[0] == 600
              && s_pXTrap->adwStartArg[1] == 2);
        CHECK(s_pXTrap->pMap == g_abXTrapMap && s_pXTrap->pStartReserved == NULL);
        CHECK(s_pXTrap->abMapFirst[0] == 0x11 && s_pXTrap->abMapFirst[1] == 0x22);
        CHECK(s_pXTrap->nSetActiveCode == 1 && s_pXTrap->dwActiveCode == 0x0FFFFFC3);
        // a failing XTrap_S_SetActiveCode deactivates
        s_pXTrap->dwActiveCodeResult = 5;
        CHECK(XTrap_Initialize() == 5);
        s_pXTrap->dwActiveCodeResult = 0;
    }

    // the filter off (anything else than service_on)
    g_ServerGlobals.mapExtra["traffic_filter_activity"] = "service_off";
    g_UserManager.LoadTrafficFilterConfig();
    CHECK(WaitLog("Traffic Filter Turned Off !!!", 0x1000000));
    CHECK(!g_UserManager.m_bTrafficFilter && g_UserManager.m_dwTrafficMaxPackets == 100
          && g_UserManager.m_dwTrafficPeriod == 60000);
    g_ServerGlobals.mapExtra.clear();
    SetCurrentDirectoryA(s_strBin.c_str());
}

static Bytes Introduction(const char* pszModule)
{
    Bytes v;
    PutS(v, pszModule);
    PutB(v, 0);
    return v;
}

// 0x6008 {relay ID, target body, opcode, data} from the GlobalManager session.
static void SendRelay(const CLIENT& gm, DWORD dwRelayID, WORD wTarget, WORD wOpcode,
                      const Bytes& data)
{
    Bytes v;
    PutD(v, dwRelayID);
    PutW(v, wTarget);
    PutW(v, wOpcode);
    v.insert(v.end(), data.begin(), data.end());
    SendPacket(gm.dwClient, 0x6008, v);
}

// 0xA008 {1, relay ID, opcode, data}: the answer to a relay of the agent.
static void SendRelayAck(const CLIENT& gm, DWORD dwRelayID, WORD wOpcode, const Bytes& data,
                         BYTE bResult = 1)
{
    Bytes v;
    PutB(v, bResult);
    PutD(v, dwRelayID);
    PutW(v, wOpcode);
    v.insert(v.end(), data.begin(), data.end());
    SendPacket(gm.dwClient, 0xA008, v);
}

static Bytes RelayAck(DWORD dwRelayID, WORD wOpcode)
{
    Bytes v;
    PutB(v, 1);
    PutD(v, dwRelayID);
    PutW(v, wOpcode);
    return v;
}

// The next 0x6008 of the agent on the GlobalManager session: checks target and opcode, returns
// the relay ID and the data.
static bool TakeRelay(const CLIENT& gm, WORD wTarget, WORD wOpcode, DWORD* pdwRelayID,
                      Bytes* pData)
{
    Bytes v;
    if (!TakePacket(gm.dwClient, 0x6008, &v))
        return false;
    if (v.size() < 8 || *(WORD*)&v[4] != wTarget || *(WORD*)&v[6] != wOpcode)
    {
        printf("  unexpected relay: %s\n", Hex(v).c_str());
        return false;
    }
    *pdwRelayID = *(DWORD*)&v[0];
    pData->assign(v.begin() + 8, v.end());
    return true;
}

static bool TakeMassiveRelay(const CLIENT& gm, WORD wTarget, WORD wOpcode, Bytes* pData)
{
    Bytes v;
    if (!TakeMassive(gm.dwClient, 0x6008, &v))
        return false;
    if (v.size() < 8 || *(WORD*)&v[4] != wTarget || *(WORD*)&v[6] != wOpcode)
    {
        printf("  unexpected massive relay: %s\n", Hex(v).c_str());
        return false;
    }
    pData->assign(v.begin() + 8, v.end());
    return true;
}

static CLIENT s_gm;
static DWORD s_dwRelay = 1000;
static const DWORD LOCALHOST = 0x0100007F;

// 0x6203 from the gateway (relayed): a token; the token ID of the 0xA203 answer.
static DWORD MakeToken(const char* pszID, const char* pszPassword, DWORD dwJID,
                       DWORD dwIP = LOCALHOST)
{
    Bytes v;
    PutD(v, dwIP);
    PutS(v, pszID);
    PutS(v, pszPassword);
    PutD(v, dwJID);
    PutB(v, 3);
    PutB(v, 1);
    PutW(v, 0x5150);
    PutD(v, 0x01020304);
    DWORD dwRelay = ++s_dwRelay;
    SendRelay(s_gm, dwRelay, BODY_AGENT, 0x6203, v);
    Bytes answer;
    if (!TakePacket(s_gm.dwClient, 0xA008, &answer))
        return 0;
    Bytes want = RelayAck(dwRelay, 0xA203);
    PutB(want, 1);
    if (answer.size() != 14)
    {
        CHECK(Same(answer, want));
        return 0;
    }
    DWORD dwToken = *(DWORD*)&answer[8];
    PutD(want, dwToken);
    PutW(want, BODY_AGENT);
    CHECK(Same(answer, want));
    return dwToken;
}

static Bytes LoginPacket(DWORD dwToken, const char* pszID, const char* pszPassword,
                         BYTE byContent = TEST_CONTENT)
{
    Bytes v;
    PutD(v, dwToken);
    PutS(v, pszID);
    PutS(v, pszPassword);
    PutB(v, byContent);
    PutD(v, 0x44332211);
    PutW(v, 0x6655);
    return v;
}

static Bytes LoginAnswer(BYTE b1, BYTE b2 = 0, bool bTwo = true)
{
    Bytes v;
    PutB(v, b1);
    if (bTwo)
        PutB(v, b2);
    return v;
}

// A full login: 0x6103 -> 0x6201 to the GlobalManager -> 0xA201 -> 0x6204 to the game server
// -> 0xA204 -> 0xA103 {1}.
static bool Login(const CLIENT& c, const char* pszID, DWORD dwJID)
{
    DWORD dwToken = MakeToken(pszID, "pw", dwJID);
    SendPacket(c.dwClient, 0x6103, LoginPacket(dwToken, pszID, "pw"));
    DWORD dwRelay;
    Bytes data;
    if (!TakeRelay(s_gm, BODY_GM, 0x6201, &dwRelay, &data))
        return false;
    Bytes want;
    PutD(want, LOCALHOST);
    PutD(want, dwJID);
    PutS(want, pszID);
    PutB(want, TEST_CONTENT);
    PutW(want, TEST_SHARD);
    PutW(want, BODY_AGENT);
    PutD(want, 0x44332211);
    PutW(want, 0x6655);
    PutW(want, 0x5150);
    PutD(want, 0x01020304);
    CHECK(Same(data, want));
    Bytes ack;
    PutD(ack, dwJID);
    PutB(ack, 1);
    PutB(ack, 0x42);
    PutW(ack, 0x7788);
    SendRelayAck(s_gm, dwRelay, 0xA201, ack);
    if (!TakeRelay(s_gm, BODY_GAME, 0x6204, &dwRelay, &data))
        return false;
    want.clear();
    PutD(want, dwJID);
    PutD(want, c.dwServer);
    PutB(want, 3);
    PutB(want, 1);
    PutS(want, pszID);
    PutD(want, LOCALHOST);
    PutB(want, 0x42);
    PutD(want, 0x44332211);
    PutW(want, 0x6655);
    PutW(want, 0x7788);
    CHECK(Same(data, want));
    Bytes ok;
    PutB(ok, 1);
    SendRelayAck(s_gm, dwRelay, 0xA204, ok);
    Bytes answer;
    if (!TakePacket(c.dwClient, 0xA103, &answer))
        return false;
    return Same(answer, LoginAnswer(1, 0, false));
}

static bool Closed(const CLIENT& c)
{
    return TakePacket(c.dwClient, 0x1002, NULL);
}

static sUser* WaitUser(DWORD dwSession, bool bPresent)
{
    DWORD dwStart = GetTickCount();
    for (;;)
    {
        sUser* p = g_UserManager.Find(dwSession);
        if ((p != NULL) == bPresent || GetTickCount() - dwStart > 2000)
            return p;
        Sleep(5);
    }
}

static void TestCord()
{
    s_gm = Connect();
    SendPacket(s_gm.dwClient, 0x2001, Introduction("GlobalManager"));
    char szPrefix[160];
    sprintf_s(szPrefix, "server cord established : %u (127.0.0.1:", s_gm.dwServer);
    CHECK(WaitLog(szPrefix));
    CHECK(ServerCordMap_FindById(50)->dwSessionID == s_gm.dwServer);
    g_pSelfBody->nState = 5;
    ClearPackets();
}

// 0x6103 failures.
static void TestLoginFailures()
{
    // another content: closed without answer
    CLIENT c = Connect();
    SkipLogs();
    SendPacket(c.dwClient, 0x6103, LoginPacket(1, "x", "y", 23));
    CHECK(WaitLog("Someone tried to login using strange client that has another content id",
                  0x2000001));
    CHECK(Closed(c));
    CHECK(NoPacket(c.dwClient, 0xA103));

    // unknown token: {2, 3}
    c = Connect();
    SendPacket(c.dwClient, 0x6103, LoginPacket(999, "x", "y"));
    Bytes answer;
    CHECK(TakePacket(c.dwClient, 0xA103, &answer) && Same(answer, LoginAnswer(2, 3)));
    CHECK(Closed(c));

    // wrong password: {2, 1}, the token is gone
    DWORD dwToken = MakeToken("alpha", "pw", 11);
    CHECK(dwToken != 0 && g_TokenManager.m_mapToken.size() == 1);
    c = Connect();
    SendPacket(c.dwClient, 0x6103, LoginPacket(dwToken, "alpha", "PW"));
    CHECK(TakePacket(c.dwClient, 0xA103, &answer) && Same(answer, LoginAnswer(2, 1)));
    CHECK(Closed(c));
    CHECK(g_TokenManager.m_mapToken.empty());

    // a token of another address: {2, 1}
    dwToken = MakeToken("alpha", "pw", 11, 0x0200007F);
    c = Connect();
    SendPacket(c.dwClient, 0x6103, LoginPacket(dwToken, "alpha", "pw"));
    CHECK(TakePacket(c.dwClient, 0xA103, &answer) && Same(answer, LoginAnswer(2, 1)));
    CHECK(Closed(c));

    // not in service: {2, 3} (no filtering either)
    dwToken = MakeToken("alpha", "pw", 11);
    g_pSelfBody->nState = 4;
    c = Connect();
    SendPacket(c.dwClient, 0x6103, LoginPacket(dwToken, "alpha", "pw"));
    CHECK(TakePacket(c.dwClient, 0xA103, &answer) && Same(answer, LoginAnswer(2, 3)));
    CHECK(Closed(c));
    g_pSelfBody->nState = 5;
    CHECK(g_TokenManager.m_mapToken.size() == 1);

    // the token timeout (timer 2, 10 s)
    CHECK(Main()->OnTimer(1));
    CHECK(g_TokenManager.m_mapToken.size() == 1);
    CToken* pToken = g_TokenManager.m_mapToken.begin()->second;
    pToken->m_dwCreateTick -= 20000;
    char szPrefix[96];
    sprintf_s(szPrefix, "Token Timeout! [token id: %u, jid: 11, elapsed: ", dwToken);
    CHECK(Main()->OnTimer(AGENT_TOKEN_TIMER_ID));
    CHECK(WaitLog(szPrefix, 0x1000000));
    CHECK(g_TokenManager.m_mapToken.empty());

    // the GlobalManager refuses: error 2 -> 5, 6 -> 6, others -> 3
    const BYTE abError[3] = { 2, 6, 9 };
    const BYTE abWant[3] = { 5, 6, 3 };
    for (int i = 0; i < 3; ++i)
    {
        c = Connect();
        dwToken = MakeToken("beta", "pw", 12);
        SendPacket(c.dwClient, 0x6103, LoginPacket(dwToken, "beta", "pw"));
        DWORD dwRelay;
        Bytes data;
        CHECK(TakeRelay(s_gm, BODY_GM, 0x6201, &dwRelay, &data));
        CHECK(WaitUser(c.dwServer, true) != NULL);
        Bytes ack;
        PutD(ack, 12);
        PutB(ack, 2);
        PutB(ack, abError[i]);
        SendRelayAck(s_gm, dwRelay, 0xA201, ack);
        CHECK(TakePacket(c.dwClient, 0xA103, &answer)
              && Same(answer, LoginAnswer(2, abWant[i])));
        CHECK(WaitUser(c.dwServer, false) == NULL);
        g_pBSNet->DisconnectSession(c.dwClient, 1);
        CHECK(Closed(c));
    }

    // the relay to the GlobalManager fails (0xA008 {2}): OnRelayFailed {2, 3}
    c = Connect();
    dwToken = MakeToken("gamma", "pw", 13);
    SendPacket(c.dwClient, 0x6103, LoginPacket(dwToken, "gamma", "pw"));
    DWORD dwRelay;
    Bytes data;
    CHECK(TakeRelay(s_gm, BODY_GM, 0x6201, &dwRelay, &data));
    SendRelayAck(s_gm, dwRelay, 0x6201, Bytes(), 2);
    CHECK(TakePacket(c.dwClient, 0xA103, &answer) && Same(answer, LoginAnswer(2, 3)));
    CHECK(WaitUser(c.dwServer, false) == NULL);
    g_pBSNet->DisconnectSession(c.dwClient, 1);
    CHECK(Closed(c));

    // the game server refuses: error 2 -> 2, 3 -> 4, others -> 3, logout and close
    const BYTE abShardError[3] = { 2, 3, 7 };
    const BYTE abShardWant[3] = { 2, 4, 3 };
    for (int i = 0; i < 3; ++i)
    {
        c = Connect();
        dwToken = MakeToken("delta", "pw", 14);
        SendPacket(c.dwClient, 0x6103, LoginPacket(dwToken, "delta", "pw"));
        CHECK(TakeRelay(s_gm, BODY_GM, 0x6201, &dwRelay, &data));
        Bytes ack;
        PutD(ack, 14);
        PutB(ack, 1);
        PutB(ack, 0);
        PutW(ack, 0);
        SendRelayAck(s_gm, dwRelay, 0xA201, ack);
        CHECK(TakeRelay(s_gm, BODY_GAME, 0x6204, &dwRelay, &data));
        ack.clear();
        PutB(ack, 2);
        PutB(ack, abShardError[i]);
        SendRelayAck(s_gm, dwRelay, 0xA204, ack);
        CHECK(TakePacket(c.dwClient, 0xA103, &answer)
              && Same(answer, LoginAnswer(2, abShardWant[i])));
        Bytes logout;
        CHECK(TakeMassiveRelay(s_gm, BODY_GM, 0x6202, &logout));
        Bytes want;
        PutW(want, 1);
        PutD(want, 14);
        CHECK(Same(logout, want));
        CHECK(Closed(c));
        CHECK(WaitUser(c.dwServer, false) == NULL);
    }
    ClearPackets();
}

static void TestLoggedIn()
{
    CLIENT c = Connect();
    CHECK(Login(c, "hero", 100));
    sUser* pUser = WaitUser(c.dwServer, true);
    CHECK(pUser != NULL);
    if (!pUser)
        return;
    CHECK(pUser->dwJID == 100 && pUser->strID == "hero" && pUser->dwIP == LOCALHOST);
    CHECK(pUser->wHomeBodyID == BODY_GAME && pUser->wTargetBodyID == BODY_GAME);
    CHECK(pUser->bySecPrimary == 3 && pUser->bySecContent == 1 && pUser->byLoginResult == 0x42);
    CHECK(pUser->dwMacLow == 0x44332211 && pUser->wMacHigh == 0x6655);
    CHECK(pUser->abXTrapSession[0] == pUser->abXTrapSession[319]);
    MONITOR_COUNTER counter;
    memset(&counter, 0, sizeof(counter));
    CHECK(AgentServer_ReadUserCount(&counter) && counter.fValue == 1.0f);

    // a second login on the same session: {2, 2}
    DWORD dwToken = MakeToken("hero", "pw", 100);
    SendPacket(c.dwClient, 0x6103, LoginPacket(dwToken, "hero", "pw"));
    Bytes answer;
    CHECK(TakePacket(c.dwClient, 0xA103, &answer) && Same(answer, LoginAnswer(2, 2)));
    CHECK(WaitLog("user register duplicated : ", 0x1000000));
    CHECK(Closed(c));
    // the lost session: logout to the GlobalManager and the game server
    Bytes data;
    CHECK(TakeMassiveRelay(s_gm, BODY_GM, 0x6202, &data));
    Bytes want;
    PutW(want, 1);
    PutD(want, 100);
    CHECK(Same(data, want));
    DWORD dwRelay;
    CHECK(TakeRelay(s_gm, BODY_GAME, 0x6205, &dwRelay, &data));
    want.clear();
    PutD(want, 100);
    CHECK(Same(data, want));
    CHECK(WaitUser(c.dwServer, false) == NULL);

    ClearPackets();
}

// A user's packet as the message loop hands it to OnUnhandledMsg (vf09), with the reference the
// loop would release (AS-006). Over the test engine a received client message forwarded on the
// cord makes the test side close the cord (also with an extra reference), so the test passes
// messages of its own.
static void ForwardAsUser(DWORD dwSession, WORD wOpcode, const Bytes& data)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(wOpcode);
    pMsg->WriteBytes(&data[0], (WORD)data.size());
    pMsg->m_dwSessionID = dwSession;
    InterlockedIncrement(&pMsg->m_lRefCount);
    Main()->OnUnhandledMsg(pMsg, 0, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
}

static void TestForward()
{
    CLIENT c = Connect();
    CHECK(Login(c, "hero", 101));
    // a game packet goes to the game server with the JID at the end
    Bytes game;
    PutD(game, 0xCAFEBABE);
    PutB(game, 9);
    ForwardAsUser(c.dwServer, 0x7021, game);
    Bytes data;
    CHECK(TakePacket(s_gm.dwClient, 0x7021, &data));
    Bytes want = game;
    PutD(want, 101);
    CHECK(Same(data, want));
    // a massive message of a user is dropped
    CMassiveMsg* pMassive = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pMassive->SetOpcode(0x7021);
    pMassive->m_dw36 = c.dwServer;
    Main()->OnUnhandledMsg(NULL, 0, 0, pMassive);
    pMassive->Free();
    CHECK(NoPacket(s_gm.dwClient, 0x7021));

    // the traffic filter: the 3rd packet within the period closes the session
    g_UserManager.m_dwTrafficMaxPackets = 3;
    g_UserManager.m_dwTrafficPeriod = 60000;
    sUser* pUser = g_UserManager.Find(c.dwServer);
    if (pUser)
        pUser->ResetTrafficCounter();
    g_UserManager.m_bTrafficFilter = TRUE;
    SkipLogs();
    ForwardAsUser(c.dwServer, 0x7021, game);
    ForwardAsUser(c.dwServer, 0x7021, game);
    ForwardAsUser(c.dwServer, 0x7021, game);
    CHECK(TakePacket(s_gm.dwClient, 0x7021, NULL) && TakePacket(s_gm.dwClient, 0x7021, NULL));
    CHECK(WaitLog("[TrafficFilter] hero (RecvCnt : 3, Tic : ", 0x2000001));
    CHECK(WaitLog("[TrafficFilter] Heavy traffic is occured by [hero]. disconnected.",
                  0x2000001));
    CHECK(Closed(c));
    CHECK(NoPacket(s_gm.dwClient, 0x7021));
    g_UserManager.m_bTrafficFilter = FALSE;
    ClearPackets();
}

// Client sessions while in service: only some opcodes may come before the login.
// The suspect report (0x401df0): logged by the original, NOPs in the patched exe.
static void CheckSuspectLog(const char* pszLine)
{
#ifdef AGENT_XTRAP_DETECTION_OFF
    (void)pszLine;
    CHECK(!WaitLog("WARNING!! A SUSPECT DETECTED!!!", (DWORD)-1, NULL, false, 300));
#else
    CHECK(WaitLog(pszLine, 0x2000001));
#endif
}

static void TestDispatchFilter()
{
    CLIENT c = Connect();
    SkipLogs();
    // 0x7001 (bit 0x1000): no user -> the base handler
    SendPacket(c.dwClient, 0x7001, Bytes(3, 0));
    CHECK(WaitLog("unhandled msg received CAgentMainProcess : ", 0x1000000));
    // 0x2002 is allowed
    SendPacket(c.dwClient, 0x2002, Bytes());
    CHECK(NoPacket(c.dwClient, 0x1002, 300));
    // a server opcode from a client: closed, not handled
    size_t nTokens = g_TokenManager.m_mapToken.size();
    Bytes token;
    PutD(token, LOCALHOST);
    PutS(token, "evil");
    PutS(token, "x");
    PutD(token, 1);
    PutB(token, 3);
    PutB(token, 3);
    PutW(token, 0);
    PutD(token, 0);
    SkipLogs();
    SendPacket(c.dwClient, 0x6203, token);
    CHECK(Closed(c));
    CheckSuspectLog("WARNING!! A SUSPECT DETECTED!!! MsgID[0x6203] IP[127.0.0.1]");
    CHECK(g_TokenManager.m_mapToken.size() == nTokens);
    // an opcode with 0x8000 / 0x0800 (even with bit 0x1000)
    c = Connect();
    SendPacket(c.dwClient, 0xB001, Bytes(1, 0));
    CHECK(Closed(c));
    c = Connect();
    SendPacket(c.dwClient, 0x7801, Bytes(1, 0));
    CHECK(Closed(c));
    // out of service the filter is off
    g_pSelfBody->nState = 4;
    c = Connect();
    SkipLogs();
    SendPacket(c.dwClient, 0xB001, Bytes(1, 0));
    CHECK(WaitLog("unhandled msg received CAgentMainProcess : ", 0x1000000));
    CHECK(NoPacket(c.dwClient, 0x1002, 300));
    g_pSelfBody->nState = 5;
    g_pBSNet->DisconnectSession(c.dwClient, 1);
    CHECK(Closed(c));

    // a handler that throws: the session is closed (catch of vf03). A full packet cannot take
    // the JID the forward appends.
    c = Connect();
    CHECK(Login(c, "full", 102));
    Bytes big(4090, 0x33);
    SkipLogs();
    SendPacket(c.dwClient, 0x7022, big);
    CHECK(Closed(c));
    CheckSuspectLog("WARNING!! A SUSPECT DETECTED!!! JID[102] IP[127.0.0.1] UserID[full] "
                    "MsgID[0x7022]");
    ClearPackets();
}

// Relays of the game server / GlobalManager to clients.
static void TestRelays()
{
    CLIENT c = Connect();
    CHECK(Login(c, "relay", 200));
    g_dwAgentMsgSentCount = 0;
    g_dwAgentMsgSentBytes = 0;

    // 0x2209 {session, opcode, data}
    Bytes v;
    PutD(v, c.dwServer);
    PutW(v, 0x3013);
    PutD(v, 0x11223344);
    SendPacket(s_gm.dwClient, 0x2209, v);
    Bytes data;
    CHECK(TakePacket(c.dwClient, 0x3013, &data));
    Bytes want;
    PutD(want, 0x11223344);
    CHECK(Same(data, want));

    // 0x220F + 0x220E
    v.clear();
    PutW(v, 0x3014);
    PutW(v, 2);
    PutD(v, c.dwServer);
    PutD(v, c.dwServer);
    SendPacket(s_gm.dwClient, 0x220F, v);
    v.clear();
    PutW(v, 0x3015);
    PutW(v, 0);
    SkipLogs();
    SendPacket(s_gm.dwClient, 0x220F, v);
    CHECK(WaitLog("RELAY_MSG_TO_CLIENT_MULTI_LIST failed : realmsgid is different [0x3015] - "
                  "[0x3014]", 0x2000000));
    v.clear();
    PutB(v, 0x77);
    SendPacket(s_gm.dwClient, 0x220E, v);
    CHECK(TakePacket(c.dwClient, 0x3014, &data) && Same(data, v));
    CHECK(TakePacket(c.dwClient, 0x3014, &data) && Same(data, v));
    // nothing collected now
    SendPacket(s_gm.dwClient, 0x220E, v);
    CHECK(WaitLog("RELAY_MSG_TO_CLIENT_MULTI_DATA : realmsgid is null or target is empty",
                  0x2000000));
    CHECK(NoPacket(c.dwClient, 0x3014));

    // 0x220A {data, sessions, WORD n, WORD opcode}; AS-002: the 0 stops the walk
    v.clear();
    PutD(v, 0xDEADBEEF);
    PutD(v, c.dwServer);
    PutD(v, 0);
    PutD(v, c.dwServer);
    PutW(v, 3);
    PutW(v, 0x3016);
    SendPacket(s_gm.dwClient, 0x220A, v);
    CHECK(TakePacket(c.dwClient, 0x3016, &data));
    want.clear();
    PutD(want, 0xDEADBEEF);
    CHECK(Same(data, want));
    CHECK(WaitLog("there is invalid session id at relay msg (multi list) : real_msg_id[0x3016]",
                  0x2000000));
    CHECK(WaitLog("there is invalid session id at relay msg (multi list) : real_msg_id[0x3016]",
                  0x2000000));
    CHECK(NoPacket(c.dwClient, 0x3016));
    // 1 + 2 + 1 messages to the client, 10 + 7 + 7 + 10 bytes
    Sleep(100);
    CHECK(g_dwAgentMsgSentCount == 4);
    CHECK(g_dwAgentMsgSentBytes == 10 + 7 + 7 + 10);

    // 0x6902 / 0xA902 billing notice
    v.clear();
    PutD(v, 200);
    PutB(v, 5);
    PutB(v, 6);
    PutD(v, 0x01010101);
    DWORD dwRelay = ++s_dwRelay;
    SendRelay(s_gm, dwRelay, BODY_AGENT, 0x6902, v);
    CHECK(TakePacket(c.dwClient, 0x6902, &data));
    want.clear();
    PutB(want, 5);
    PutB(want, 6);
    PutD(want, 0x01010101);
    CHECK(Same(data, want));
    CHECK(TakePacket(s_gm.dwClient, 0xA008, &data));
    want = RelayAck(dwRelay, 0xA902);
    PutB(want, 1);
    CHECK(Same(data, want));

    // 0x6207 move: failures, then the move through 0x6208 / 0xA208
    struct MOVE_CASE
    {
        DWORD   dwSession;
        DWORD   dwJID;
        WORD    wBody;
        BYTE    byError;
    };
    const MOVE_CASE aCase[4] = {
        { 12345, 200, BODY_GATEWAY, 1 },
        { c.dwServer, 201, BODY_GATEWAY, 5 },
        { c.dwServer, 200, BODY_GAME, 3 },
        { c.dwServer, 200, 99, 2 },
    };
    for (int i = 0; i < 4; ++i)
    {
        v.clear();
        PutD(v, aCase[i].dwSession);
        PutD(v, aCase[i].dwJID);
        PutW(v, aCase[i].wBody);
        dwRelay = ++s_dwRelay;
        SendRelay(s_gm, dwRelay, BODY_AGENT, 0x6207, v);
        CHECK(TakePacket(s_gm.dwClient, 0xA008, &data));
        want = RelayAck(dwRelay, 0xA207);
        PutD(want, aCase[i].dwJID);
        PutB(want, 2);
        PutB(want, aCase[i].byError);
        CHECK(Same(data, want));
    }
    for (int i = 0; i < 2; ++i)
    {
        v.clear();
        PutD(v, c.dwServer);
        PutD(v, 200);
        PutW(v, BODY_GATEWAY);
        dwRelay = ++s_dwRelay;
        SendRelay(s_gm, dwRelay, BODY_AGENT, 0x6207, v);
        DWORD dwMove;
        CHECK(TakeRelay(s_gm, BODY_GATEWAY, 0x6208, &dwMove, &data));
        want.clear();
        PutD(want, 200);
        PutD(want, c.dwServer);
        CHECK(Same(data, want));
        Bytes result;
        PutB(result, i ? 1 : 0);
        SendRelayAck(s_gm, dwMove, 0xA208, result);
        CHECK(TakePacket(s_gm.dwClient, 0xA008, &data));
        want = RelayAck(dwRelay, 0xA207);
        PutD(want, 200);
        if (i)
        {
            PutB(want, 1);
        }
        else
        {
            PutB(want, 2);
            PutB(want, 4);
        }
        CHECK(Same(data, want));
    }
    sUser* pUser = g_UserManager.Find(c.dwServer);
    CHECK(pUser && pUser->wTargetBodyID == BODY_GATEWAY && pUser->wHomeBodyID == BODY_GAME);

    // a failed 0x6208 relay (OnRelayFailed): {JID, 2, 2} to the requester
    AGENT_MOVE_REQUEST* pRequest = new AGENT_MOVE_REQUEST;
    pRequest->dwSessionID = s_gm.dwServer;
    pRequest->dwUserSession = c.dwServer;
    pRequest->dwJID = 200;
    pRequest->wBodyID = BODY_GATEWAY;
    pRequest->dwRelayID = 4242;
    Main()->OnRelayFailed(0x6208, (DWORD)pRequest);
    CHECK(TakePacket(s_gm.dwClient, 0xA008, &data));
    want = RelayAck(4242, 0xA207);
    PutD(want, 200);
    PutB(want, 2);
    PutB(want, 2);
    CHECK(Same(data, want));
    SkipLogs();
    Main()->OnRelayFailed(0x6203, 0x55);
    CHECK(WaitLog("unhandled relay msg error : wRealMsgID : 0x6203 : dwParam : 0x55", 0x1000000));
    Main()->OnRelayFailed(0x6202, 0x55);
    Main()->OnRelayFailed(0x6205, 0x55);
    CHECK(!WaitLog("unhandled relay msg error", (DWORD)-1, NULL, false, 200));

    // the failed 0x6204 relay of the user: {2, 3}, logout, the user is dropped (not closed)
    Main()->OnRelayFailed(0x6204, c.dwServer);
    Bytes answer;
    CHECK(TakePacket(c.dwClient, 0xA103, &answer) && Same(answer, LoginAnswer(2, 3)));
    CHECK(TakeMassiveRelay(s_gm, BODY_GM, 0x6202, &data));
    want.clear();
    PutW(want, 1);
    PutD(want, 200);
    CHECK(Same(data, want));
    CHECK(g_UserManager.Find(c.dwServer) == NULL);
    CHECK(NoPacket(c.dwClient, 0x1002));
    g_pBSNet->DisconnectSession(c.dwClient, 1);
    CHECK(Closed(c));
    ClearPackets();
}

// Bans and kicks of the GlobalManager.
static void TestBans()
{
    // 0x620D by JID
    CLIENT c = Connect();
    CHECK(Login(c, "banned", 300));
    Bytes v;
    PutD(v, 300);
    DWORD dwRelay = ++s_dwRelay;
    SkipLogs();
    SendRelay(s_gm, dwRelay, BODY_AGENT, 0x620D, v);
    Bytes data;
    CHECK(TakePacket(s_gm.dwClient, 0xA008, &data));
    Bytes want = RelayAck(dwRelay, 0xA20D);
    PutB(want, 1);
    CHECK(Same(data, want));
    CHECK(WaitLog("ban user from division manager : banned", 0));
    CHECK(Closed(c));
    CHECK(TakeMassiveRelay(s_gm, BODY_GM, 0x6202, &data));
    DWORD dwLogout;
    CHECK(TakeRelay(s_gm, BODY_GAME, 0x6205, &dwLogout, &data));
    CHECK(WaitUser(c.dwServer, false) == NULL);
    dwRelay = ++s_dwRelay;
    SendRelay(s_gm, dwRelay, BODY_AGENT, 0x620D, v);
    CHECK(TakePacket(s_gm.dwClient, 0xA008, &data));
    want = RelayAck(dwRelay, 0xA20D);
    PutB(want, 2);
    PutD(want, 300);
    CHECK(Same(data, want));

    // 0x6905 (billing)
    c = Connect();
    CHECK(Login(c, "billing", 301));
    v.clear();
    PutD(v, 301);
    dwRelay = ++s_dwRelay;
    SendRelay(s_gm, dwRelay, BODY_AGENT, 0x6905, v);
    CHECK(TakePacket(s_gm.dwClient, 0xA008, &data));
    want = RelayAck(dwRelay, 0xA905);
    PutB(want, 1);
    CHECK(Same(data, want));
    CHECK(WaitLog("ban user from division manager<billing> : billing", 0));
    CHECK(Closed(c));
    CHECK(WaitUser(c.dwServer, false) == NULL);
    dwRelay = ++s_dwRelay;
    SendRelay(s_gm, dwRelay, BODY_AGENT, 0x6905, v);
    CHECK(TakePacket(s_gm.dwClient, 0xA008, &data));
    want = RelayAck(dwRelay, 0xA905);
    PutB(want, 2);
    CHECK(Same(data, want));
    ClearPackets();

    // 0x2206 kick: the JIDs (0 for an unknown session) go to the GlobalManager
    c = Connect();
    CHECK(Login(c, "kicked", 302));
    v.clear();
    PutW(v, 2);
    PutD(v, c.dwServer);
    PutD(v, 77777);
    SendPacket(s_gm.dwClient, 0x2206, v);
    CHECK(TakeMassiveRelay(s_gm, BODY_GM, 0x6202, &data));
    want.clear();
    PutW(want, 2);
    PutD(want, 302);
    PutD(want, 0);
    CHECK(Same(data, want));
    CHECK(Closed(c));
    CHECK(g_UserManager.Find(c.dwServer) == NULL);
    ClearPackets();

    // the cord to the game server closes: its users are dropped, state 4
    c = Connect();
    CHECK(Login(c, "blind", 303));
    SERVER_BODY* pGame = ServerBodyInfo_Find(BODY_GAME);
    SERVER_BODY* pFarm = ServerBodyInfo_Find(BODY_GATEWAY);
    CHECK(pGame && pFarm);
    SERVER_CORD* pCord = ServerCordMap_FindById(50);
    // a FarmManager peer changes nothing
    CERT_MODULE* pModule = pFarm->pModule;
    pFarm->pModule = CertModule_FindByName("FarmManager");
    Main()->OnCordClosed(pCord, pFarm);
    pFarm->pModule = pModule;
    CHECK(g_UserManager.Find(c.dwServer) != NULL);
    SkipLogs();
    Main()->OnCordClosed(pCord, pGame);
    CHECK(WaitLog("3:SR_GameServer is blind : 1 users are banned", 0x2000001));
    CHECK(g_UserManager.Find(c.dwServer) == NULL);
    CHECK(g_pSelfBody->nState == 4);
    CHECK(TakeMassiveRelay(s_gm, BODY_GM, 0x6202, &data));
    want.clear();
    PutW(want, 1);
    PutD(want, 303);
    CHECK(Same(data, want));
    CHECK(Closed(c));
    g_pSelfBody->nState = 5;
    ClearPackets();
}

// XTrap challenges and answers.
static void TestXTrap()
{
    CHECK(s_pXTrap != NULL);
    if (!s_pXTrap)
        return;
    CLIENT c = Connect();
    DWORD nInit = s_pXTrap->nSessionInit;
    CHECK(Login(c, "trap", 400));
    CHECK(s_pXTrap->nSessionInit == nInit + 1);
    sUser* pUser = g_UserManager.Find(c.dwServer);
    CHECK(pUser != NULL);
    if (!pUser)
        return;
    BYTE bySession = pUser->abXTrapSession[0];
    // due: the next message of the process sends the challenge
    pUser->dwXTrapTick = GetTickCount() - XTRAP_STEP_INTERVAL - 10;
    g_dwXTrapLastBroadcastTick = 0;
    SendPacket(c.dwClient, 0x2002, Bytes());
    Bytes data;
    CHECK(TakePacket(c.dwClient, 0x2113, &data));
    Bytes want;
    PutB(want, 1);
    PutB(want, 1);
    for (int i = 0; i < 1024; ++i)
        PutB(want, (BYTE)(bySession + i));
    CHECK(Same(data, want));
    CHECK(GetTickCount() - pUser->dwXTrapTick < 2000);

    // the answer {2, 2, data}
    Bytes answer;
    PutB(answer, 2);
    PutB(answer, 2);
    for (int i = 0; i < 1024; ++i)
        PutB(answer, (BYTE)(i * 7));
    DWORD nStep3 = s_pXTrap->nStep3;
    SendPacket(c.dwClient, 0x2113, answer);
    DWORD dwStart = GetTickCount();
    while (s_pXTrap->nStep3 == nStep3 && GetTickCount() - dwStart < 2000)
        Sleep(5);
    CHECK(s_pXTrap->nStep3 == nStep3 + 1 && s_pXTrap->pStep3Session == pUser->abXTrapSession);
    CHECK(s_pXTrap->abStep3[1] == 7 && s_pXTrap->abStep3[1023] == (BYTE)(1023 * 7));
    // step 3 data with another step byte is ignored, another type is logged
    answer[1] = 3;
    SendPacket(c.dwClient, 0x2113, answer);
    Bytes wrong(1, 1);
    SkipLogs();
    SendPacket(c.dwClient, 0x2113, wrong);
    CHECK(WaitLog("[For Xtrap] \xc0\xdf\xb8\xf8\xb5\xc8 \xc6\xd0\xc5\xb6 \xc0\xfc\xbc\xdb!!",
                  0x2000000));
    CHECK(s_pXTrap->nStep3 == nStep3 + 1);

    // a challenge that XTrap_CS_Step1 reports as failed (not 0): the challenge is sent, then the
    // original logs "Hack Detect" and closes the session; the patched exe ignores the result
    s_pXTrap->dwStep1Result = 1;
    pUser->dwXTrapTick = GetTickCount() - XTRAP_STEP_INTERVAL - 10;
    g_dwXTrapLastBroadcastTick = 0;
    SkipLogs();
    SendPacket(c.dwClient, 0x2002, Bytes());
    CHECK(TakePacket(c.dwClient, 0x2113, &data) && data.size() == 1026);
    s_pXTrap->dwStep1Result = 0;
#ifdef AGENT_XTRAP_DETECTION_OFF
    CHECK(!WaitLog("[For Xtrap] Hack Detect!!", (DWORD)-1, NULL, false, 300));
    CHECK(NoPacket(c.dwClient, 0x1002, 300));
    g_pBSNet->DisconnectSession(c.dwClient, 1);
#else
    CHECK(WaitLog("[For Xtrap] Hack Detect!! trap", 0x2000000));
    CHECK(WaitLog("[For Xtrap] Hack Detect!! trap", 0x2000001));
#endif
    CHECK(Closed(c));
    ClearPackets();

    // a failing XTrap_S_SessionInit is logged, the login goes on
    s_pXTrap->dwSessionInitResult = 1;
    c = Connect();
    SkipLogs();
    CHECK(Login(c, "untrapped", 401));
    CHECK(WaitLog("[For Xtrap] SessionInit \xbd\xc7\xc6\xd0 \xc0\xaf\xc0\xfa : untrapped ",
                  0x2000000));
    CHECK(WaitLog("[For Xtrap] SessionInit \xbd\xc7\xc6\xd0 \xc0\xaf\xc0\xfa : untrapped ",
                  0x2000001));
    s_pXTrap->dwSessionInitResult = 0;
    g_pBSNet->DisconnectSession(c.dwClient, 1);
    CHECK(Closed(c));
    ClearPackets();
}

// LogRegisterFailure (0x402680, not referenced)
static void TestRegisterFailureLog()
{
    SkipLogs();
    AgentMainProcess_LogRegisterFailure(0xA201, 4);
    CHECK(WaitLog("Msg[FRAMEWORKMSG_REGISTER_CONTENT_USER_ACK] Failed - Block Gratis User",
                  0x2000001));
    AgentMainProcess_LogRegisterFailure(0xA201, 7);
    CHECK(WaitLog("Msg[FRAMEWORKMSG_REGISTER_CONTENT_USER_ACK] Failed - Unknown ErrorCode!"));
    AgentMainProcess_LogRegisterFailure(0xA204, 3);
    CHECK(WaitLog("Msg[FRAMEWORKMSG_REGISTER_SHARD_USER_ACK] Failed - MaxUser Exceeded"));
    AgentMainProcess_LogRegisterFailure(0x1234, 1);
    CHECK(WaitLog("Msg[] Failed - ErrorCode Undefined"));
}

static void TestShutdown()
{
    // a user, a token and a multi-session relay left over
    sUser* pUser = UserPool_Alloc();
    pUser->dwSessionID = 88888;
    pUser->dwJID = 500;
    CHECK(g_UserManager.Register(pUser));
    CHECK(g_TokenManager.Add(new CToken));
    AGENT_BROADCAST* pBroadcast = new AGENT_BROADCAST;
    pBroadcast->wRealMsgID = 0x3020;
    pBroadcast->vecTarget.push_back(88888);
    Main()->m_mapBroadcast.insert(AgentBroadcastMap::value_type(77777, pBroadcast));
    CHECK(!g_UserManager.m_mapUser.empty() && !g_TokenManager.m_mapToken.empty());
    CHECK(g_AgentServer.Shutdown());
    CHECK(g_pBSNet == NULL);
    CHECK(g_UserManager.m_mapUser.empty() && g_TokenManager.m_mapToken.empty());
    CHECK(g_TokenIDPool.m_dwInUse == 0);
    g_pSelfBody = NULL;
    g_pServerBodyStateObserver = NULL;
}

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);
    BSDebugSetFlags(0, 0);
    InitializeCriticalSection(&s_cs);
    char szTemp[MAX_PATH];
    GetTempPathA(MAX_PATH, szTemp);
    char szDir[MAX_PATH];
    sprintf_s(szDir, "%sAgentTest_%u", szTemp, GetCurrentProcessId());
    CreateDirectoryA(szDir, NULL);
    s_strDir = szDir;
    char szExe[MAX_PATH];
    GetModuleFileNameA(NULL, szExe, MAX_PATH);
    *strrchr(szExe, '\\') = 0;
    s_strBin = szExe;
    strcpy_s(g_ServerGlobals.szModuleDir, szDir);
    strcpy_s(g_ServerGlobals.szServiceName, "AgentServer");
    g_ServerGlobals.hInstance = GetModuleHandleA(NULL);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    g_ServerGlobals.dwProcessorCount = si.dwNumberOfProcessors;
    g_ServerGlobals.nMaxSendQueDepth = -1;
    g_ServerGlobals.bReportConnectionLog = TRUE;
    g_pServerApp = &g_AgentServer;

    TestConfig();
    TestTokens();
    TestUsers();

    LogCapture_Start();
    TestStartup();
    CHECK(g_pBSNet->Listen("127.0.0.1", 0));
    sockaddr_in local;
    g_AgentServer.m_NetEngine.m_pListener->GetLocalAddr(&local);
    s_wPort = ntohs(local.sin_port);
    CHECK(g_pBSNet->RegisterProactor(TEST_TASK_CLIENT, NULL, &CTestClient::classCTestClient,
                                     NULL));
    CHECK(g_pBSNet->StartProactorWorkers(TEST_TASK_CLIENT, 1, 0, 0, 0, NULL));

    TestXTrapStart();
    TestCord();
    TestLoginFailures();
    TestLoggedIn();
    TestDispatchFilter();
    TestRelays();
    TestBans();
    TestXTrap();
    TestRegisterFailureLog();
    TestForward();
    TestShutdown();
    CloseHandle(g_hServerIOCP);
    g_hServerIOCP = NULL;

    DeleteFileA((s_strDir + "\\map1.cs3").c_str());
    DeleteFileA((s_strDir + "\\map2.cs3").c_str());
    if (g_hXTrapModule)
        FreeLibrary(g_hXTrapModule);
    DeleteFileA((s_strDir + "\\XTrap4Server.Dll").c_str());
    RemoveDirectoryA(s_strDir.c_str());
    DeleteCriticalSection(&s_cs);
#ifdef AGENT_XTRAP_DETECTION_OFF
    return TEST_RESULT("AgentTestNoDetect");
#else
    return TEST_RESULT("AgentTest");
#endif
}
