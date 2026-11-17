// GatewayServer application: CGatewayServer on the real network engine, which connects to
// itself. A client task of the same engine sends the client packets built byte by byte and
// records every packet the gateway sends back. The certification part (CertifierFwd.h) and the
// database layer (CDBQuery and CDatabase of BSLib DB) are replaced by the fakes at the top; the
// test object defines them, so the library's database objects are not linked.

#include "BSLib/BSDebug.h"
#include "BSNet/NetEngine.h"
#include "BSNet/SockListener.h"
#include "GatewayServer/ArticleManager.h"
#include "GatewayServer/CertifierFwd.h"
#include "BSLib/DB/DBQuery.h"
#include "GatewayServer/GatewayMainProcess.h"
#include "GatewayServer/GatewayServer.h"
#include "GatewayServer/ShardConditionManager.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <set>
#include <string>
#include <vector>

#include "LogCapture.h"
#include "TestCheck.h"

using namespace ServerFramework;

typedef std::vector<BYTE> Bytes;

static CRITICAL_SECTION s_cs;

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
    for (size_t i = 0; i < v.size(); ++i)
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
    printf("  got:  %s\n  want: %s\n", Hex(vGot).c_str(), Hex(vWant).c_str());
    return false;
}

static bool WaitFor(bool (*pfn)(), DWORD dwTimeout = 5000)
{
    DWORD dwStart = GetTickCount();
    while (!pfn())
    {
        if (GetTickCount() - dwStart > dwTimeout)
            return false;
        Sleep(5);
    }
    return true;
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

// Waits for a line starting with strPrefix after the last line found.
static bool WaitLog(const std::string& strPrefix, DWORD dwFlags = (DWORD)-1,
                    std::string* pstrLine = NULL)
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
        if (GetTickCount() - dwStart > 3000)
        {
            printf("  missing log line: %s\n", strPrefix.c_str());
            return false;
        }
        Sleep(5);
    }
}

static bool HasLogSince(size_t nFrom, const std::string& strPrefix)
{
    DrainLogs();
    for (size_t i = nFrom; i < s_vecLog.size(); ++i)
    {
        if (s_vecLog[i].strText.compare(0, strPrefix.size(), strPrefix) == 0)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Fakes of the certification part

class CIBUVProcess : public CSingletonT<CIBUVProcess>
{
};

class CIBUVIPBlockMgr : public CSingletonT<CIBUVIPBlockMgr>
{
};

class CIBUVTemporalCertParamMgr : public CSingletonT<CIBUVTemporalCertParamMgr>
{
};

// The IBUV process task: a plain process without handlers.
class CFakeIBUVProcess : public CServerProcessBase
{
    BS_DECLARE_DYNCREATE(CFakeIBUVProcess)
};

BS_IMPLEMENT_DYNCREATE(CFakeIBUVProcess, &CServerProcessBase::classCServerProcessBase)

static DWORD s_dwPrivilegedIP = 0xFFFFFFFF;
static std::vector<CERTIFY_PARAM*> s_vecReleased;
static std::vector<CERTIFY_PARAM*> s_vecImageRequests;
static int s_nCertifierPoolInits;
static bool s_bIBUVInitResult = true;
static int s_nIBUVInits;
static CTask* s_pIBUVOwner;
static DWORD s_dwIBUVReserve;
static float s_fIBUVRatio;
static int s_nIBUVStringSize;
static std::string s_strIBUVCharSet;
static DWORD s_dwIPTolerance = 0xFFFFFFFF;
static std::vector<DWORD> s_vecRemovedCaptcha;
static int s_nPrivilegedRows;

BOOL IsPrivilegedIP(DWORD dwIP)
{
    return dwIP == s_dwPrivilegedIP;
}

void CertParam_Release(CERTIFY_PARAM** ppParam)
{
    EnterCriticalSection(&s_cs);
    s_vecReleased.push_back(*ppParam);
    LeaveCriticalSection(&s_cs);
    *ppParam = NULL;
}

void RequestIBUVImage(CERTIFY_PARAM* pParam)
{
    EnterCriticalSection(&s_cs);
    s_vecImageRequests.push_back(pParam);
    LeaveCriticalSection(&s_cs);
}

bool UserCertifier_InitProcessOverlapPool()
{
    ++s_nCertifierPoolInits;
    return true;
}

CRuntimeClass* IBUVProcess_GetRuntimeClass()
{
    return &CFakeIBUVProcess::classCFakeIBUVProcess;
}

bool IBUVProcess_Initialize(CIBUVProcess* pProcess, CTask* pOwnerTask, DWORD dwQueueReserveCount,
                            float fQueuePrepareRatio, int nStringSize, const char* pszCharSet)
{
    CHECK(pProcess == CSingletonT<CIBUVProcess>::GetInstance());
    ++s_nIBUVInits;
    s_pIBUVOwner = pOwnerTask;
    s_dwIBUVReserve = dwQueueReserveCount;
    s_fIBUVRatio = fQueuePrepareRatio;
    s_nIBUVStringSize = nStringSize;
    s_strIBUVCharSet = pszCharSet;
    return s_bIBUVInitResult;
}

void IBUVIPBlockMgr_SetFailureIPTolerance(CIBUVIPBlockMgr* pMgr, DWORD dwTolerance)
{
    CHECK(pMgr == CSingletonT<CIBUVIPBlockMgr>::GetInstance());
    s_dwIPTolerance = dwTolerance;
}

void IBUVTemporalCertParamMgr_Remove(CIBUVTemporalCertParamMgr* pMgr, DWORD dwSessionID)
{
    CHECK(pMgr == CSingletonT<CIBUVTemporalCertParamMgr>::GetInstance());
    EnterCriticalSection(&s_cs);
    s_vecRemovedCaptcha.push_back(dwSessionID);
    LeaveCriticalSection(&s_cs);
}

// The original module version trigger: a zeroed row before each row, the row into the list of
// the query (dwUser 0: modules, 1: files).
int CGatewayServer::OnModuleVersionRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (pQuery->m_bRowFetched)
    {
        if (pQuery->m_dwParam)
            ((ModuleVersionFileList*)pQuery->m_pUserData)->push_back((MODULE_VERSION_FILE*)pQuery->m_pBuffer);
        else
            ((ModuleVersionList*)pQuery->m_pUserData)->push_back((MODULE_VERSION*)pQuery->m_pBuffer);
        return 1;
    }
    size_t n = pQuery->m_dwParam ? sizeof(MODULE_VERSION_FILE) : sizeof(MODULE_VERSION);
    pQuery->m_pBuffer = (BYTE*)operator new(n);
    pQuery->m_nRowSize = (DWORD)n;
    memset(pQuery->m_pBuffer, 0, n);
    return 1;
}

int CGatewayServer::OnPrivilegedIPRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (pQuery->m_bRowFetched)
        ++s_nPrivilegedRows;
    return 1;
}

int CGatewayServer::OnPrivilegedIPRow2(DWORD, DWORD)
{
    CHECK(!"second address set trigger used");
    return 1;
}

static int s_nLoginRequests;

void CGatewayMainProcess::OnMsg_6102(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    ++s_nLoginRequests;
    pMsg->SkipRemaining();
}

void CGatewayMainProcess::OnMsg_A203(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    pMsg->SkipRemaining();
}

void CGatewayMainProcess::OnMsg_A321(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    pMsg->SkipRemaining();
}

void CGatewayMainProcess::OnMsg_6323(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    pMsg->SkipRemaining();
}

// ---------------------------------------------------------------------------------------------
// Fake database: rows by statement

struct FAKE_DB
{
    BOOL bEnvResult;
    std::string strEnvConnect;
    std::vector<std::string> vecConnects;           // "" for the default connection
    std::set<std::string> setFailingConnects;
    std::vector<std::string> vecQueries;
    std::set<std::string> setFailingQueries;
    std::vector<MODULE_VERSION> vecModules;
    std::vector<MODULE_VERSION_FILE> vecFiles;
    std::vector<ARTICLE> vecArticles;
    std::vector<std::pair<WORD, BYTE> > vecShardService;
    int nPrivilegedRows;
    int nDestroyAll;
};

static FAKE_DB s_db;
static int s_nConnections;

BOOL CDatabase::InitEnvironment(const char* pszDefaultConnect)
{
    s_db.strEnvConnect = pszDefaultConnect;
    return s_db.bEnvResult;
}

CDBSessionMgr* CDatabase::CreateSession(const char* pszConnect)
{
    std::string str = pszConnect ? pszConnect : "";
    s_db.vecConnects.push_back(str);
    if (s_db.setFailingConnects.count(str))
        return NULL;
    return (CDBSessionMgr*)(DWORD_PTR)(0x1000 + ++s_nConnections);
}

void CDatabase::DestroyAllSessions()
{
    ++s_db.nDestroyAll;
}

static void FeedRow(CDBQuery* pQuery, const void* pRow, size_t n)
{
    pQuery->m_bRowFetched = 0;
    pQuery->m_pTrigger->OnTrigger2((DWORD)pQuery, 0);
    CHECK(pQuery->m_nRowSize == n);
    memcpy(pQuery->m_pBuffer, pRow, n);
    pQuery->m_bRowFetched = 1;
    pQuery->m_pTrigger->OnTrigger2((DWORD)pQuery, 0);
}

int CDBQuery::Execute()
{
    CDBQuery* pQuery = this;
    std::string strSQL = pQuery->m_szQuery;
    s_db.vecQueries.push_back(strSQL);
    CHECK(pQuery->m_pSession != NULL && pQuery->m_nSession == 0);
    if (s_db.setFailingQueries.count(strSQL))
        return -1;
    if (strSQL == "select * from _ModuleVersion with (NOLOCK) where nValid = 1")
    {
        CHECK(pQuery->m_dwParam == 0);
        for (size_t i = 0; i < s_db.vecModules.size(); ++i)
            FeedRow(pQuery, &s_db.vecModules[i], sizeof(MODULE_VERSION));
    }
    else if (strSQL == "select * from _ModuleVersionFile with (NOLOCK) where nValid = 1")
    {
        CHECK(pQuery->m_dwParam == 1);
        for (size_t i = 0; i < s_db.vecFiles.size(); ++i)
            FeedRow(pQuery, &s_db.vecFiles[i], sizeof(MODULE_VERSION_FILE));
    }
    else if (strSQL == "exec _GetArticleData")
    {
        for (size_t i = 0; i < s_db.vecArticles.size(); ++i)
            FeedRow(pQuery, &s_db.vecArticles[i], sizeof(ARTICLE));
    }
    else if (strSQL == "select * from _PrivilegedIP")
    {
        CHECK(pQuery->m_dwParam == 2 && pQuery->m_nBufferSize == 8 && pQuery->m_nRowSize == 8);
        for (int i = 0; i < s_db.nPrivilegedRows; ++i)
        {
            pQuery->m_bRowFetched = 1;
            pQuery->m_pTrigger->OnTrigger2((DWORD)pQuery, 0);
        }
    }
    else if (strSQL.compare(0, 27, "select * from _ShardService") == 0)
    {
        CHECK(pQuery->m_nBufferSize == 3 && pQuery->m_nRowSize == 3);
        WORD wOnly = 0;
        sscanf_s(strSQL.c_str(), "select * from _ShardService where ShardID = %hu", &wOnly);
        for (size_t i = 0; i < s_db.vecShardService.size(); ++i)
        {
            if (wOnly && s_db.vecShardService[i].first != wOnly)
                continue;
            pQuery->m_bRowFetched = 0;
            pQuery->m_pTrigger->OnTrigger2((DWORD)pQuery, 0);
            BYTE* pRow = (BYTE*)pQuery->m_pBuffer;
            CHECK(pRow[0] == 0 && pRow[1] == 0 && pRow[2] == 0);
            *(WORD*)pRow = s_db.vecShardService[i].first;
            pRow[2] = s_db.vecShardService[i].second;
            pQuery->m_bRowFetched = 1;
            pQuery->m_pTrigger->OnTrigger2((DWORD)pQuery, 0);
        }
    }
    return 1;
}

int CDBQuery::ExecuteFormat(const char* pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    vsprintf_s(m_szQuery, pszFormat, args);
    va_end(args);
    return Execute();
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
    // No framework handlers: 0x6008 / 0xA008 are recorded too.
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

// Takes the first packet of the session with the opcode.
static bool TakePacket(DWORD dwSession, WORD wOpcode, Bytes* pData, DWORD dwTimeout = 3000)
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
            printf("  no packet 0x%04x on session %u\n", wOpcode, dwSession);
            return false;
        }
        Sleep(5);
    }
}

// Reassembles a massive message (0x600D header {1, WORD parts, WORD opcode}, parts {0, data}).
static bool TakeMassive(DWORD dwSession, WORD wOpcode, Bytes* pData)
{
    Bytes header;
    if (!TakePacket(dwSession, 0x600D, &header))
        return false;
    if (header.size() != 5 || header[0] != 1 || *(WORD*)&header[3] != wOpcode)
    {
        printf("  bad massive header %s\n", Hex(header).c_str());
        return false;
    }
    WORD wParts = *(WORD*)&header[1];
    pData->clear();
    for (WORD i = 0; i < wParts; ++i)
    {
        Bytes part;
        if (!TakePacket(dwSession, 0x600D, &part) || part.empty() || part[0] != 0)
            return false;
        pData->insert(pData->end(), part.begin() + 1, part.end());
    }
    return true;
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

// A message as a handler gets it from the session.
static CMsg* MakeMsg(DWORD dwSession, WORD wOpcode, const Bytes& data)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(wOpcode);
    if (!data.empty())
        pMsg->WriteBytes(&data[0], (WORD)data.size());
    pMsg->m_dwSessionID = dwSession;
    return pMsg;
}

static u_short s_wPort;

struct CLIENT
{
    DWORD dwClient;     // the client task's session
    DWORD dwServer;     // the gateway's session of the same connection
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

// The gateway closes the connection: the client loses its session.
static bool Closed(const CLIENT& c)
{
    return TakePacket(c.dwClient, 0x1002, NULL);
}

static CGatewayMainProcess* Gateway()
{
    return static_cast<CGatewayMainProcess*>(
        g_pBSNet->GetProactor(SERVER_TASK_MAIN)->GetJob());
}

// ---------------------------------------------------------------------------------------------
// Architecture

enum
{
    MOD_GATEWAY = 1, MOD_AGENT = 10, MOD_DOWNLOAD = 11, MOD_FARM_MANAGER = 12, MOD_GAME = 13,
    MOD_CLIENT = 20,
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

static CERT_SHARD Shard(WORD wID, BYTE byFarm, const char* pszName, WORD wLimit)
{
    CERT_SHARD s = Zero<CERT_SHARD>();
    s.wID = wID;
    s.byFarmID = byFarm;
    s.ab3[0] = 0x33;
    strcpy_s((char*)s.ab3 + 1, 32, pszName);
    s.w548 = wLimit;
    return s;
}

static SERVER_BODY Body(WORD wID, BYTE byModule, DWORD dwMachine, WORD wShard, BYTE byFarm,
                        WORD wPort, int nState)
{
    SERVER_BODY b = Zero<SERVER_BODY>();
    b.wID = wID;
    b.byDivisionID = 3;
    b.byFarmID = byFarm;
    b.wShardID = wShard;
    b.dwMachineID = dwMachine;
    b.byModuleID = byModule;
    b.wPort = wPort;
    b.nState = nState;
    return b;
}

// Body 1 is this gateway (division 3). Shards 10 "Alpha" (agents 2 in state 5 and 3 in state
// 4), 11 "Beta" (agent 4, game server 8 in state 5) and 12 "Gamma" (agent 9); download servers
// 5 (state 5) and 6 (state 4); FarmManager 7 of farm 1. Cord 1 from body 1 to body 2.
static void SetUpArchitecture()
{
    std::vector<CERT_MODULE> modules;
    modules.push_back(Module(MOD_GATEWAY, "GatewayServer"));
    modules.push_back(Module(MOD_AGENT, "AgentServer"));
    modules.push_back(Module(MOD_DOWNLOAD, "DownloadServer"));
    modules.push_back(Module(MOD_FARM_MANAGER, "FarmManager"));
    modules.push_back(Module(MOD_GAME, "SR_GameServer"));
    modules.push_back(Module(MOD_CLIENT, "SR_Client"));
    std::vector<CERT_CONTENT> contents(1, Zero<CERT_CONTENT>());
    contents[0].byID = 22;
    std::vector<CERT_DIVISION> divisions(1, Zero<CERT_DIVISION>());
    divisions[0].byID = 3;
    strcpy_s((char*)divisions[0].ab1 + 32, 64, "DSN=GatewayTest");
    std::vector<CERT_FARM> farms(2, Zero<CERT_FARM>());
    farms[0].byID = 1;
    farms[0].byDivisionID = 3;
    strcpy_s((char*)farms[0].ab2, 32, "FarmOne");
    farms[1].byID = 2;
    farms[1].byDivisionID = 3;
    strcpy_s((char*)farms[1].ab2, 32, "F2");
    std::vector<CERT_FARM_CONTENT> farmContents(1, Zero<CERT_FARM_CONTENT>());
    farmContents[0].byFarmID = 1;
    farmContents[0].byContentID = 22;
    std::vector<CERT_SHARD> shards;
    shards.push_back(Shard(10, 1, "Alpha", 1000));
    shards.push_back(Shard(11, 2, "Beta", 2000));
    shards.push_back(Shard(12, 1, "Gamma", 3000));
    std::vector<CERT_MACHINE> machines(3, Zero<CERT_MACHINE>());
    const char* aszIP[3] = { "127.0.0.1", "10.1.2.3", "10.4.5.6" };
    for (int i = 0; i < 3; ++i)
    {
        machines[i].dwID = 100 + i;
        machines[i].byDivisionID = 3;
        strcpy_s(machines[i].szPublicIP, aszIP[i]);
        strcpy_s(machines[i].szPrivateIP, aszIP[i]);
    }
    std::vector<SERVER_BODY> bodies;
    bodies.push_back(Body(1, MOD_GATEWAY, 100, 0, 0, 0, 5));
    bodies.push_back(Body(2, MOD_AGENT, 100, 10, 1, 15884, 5));
    bodies.push_back(Body(3, MOD_AGENT, 100, 10, 1, 15885, 4));
    bodies.push_back(Body(4, MOD_AGENT, 100, 11, 2, 15886, 5));
    bodies.push_back(Body(5, MOD_DOWNLOAD, 101, 0, 0, 15881, 5));
    bodies.push_back(Body(6, MOD_DOWNLOAD, 101, 0, 0, 15882, 4));
    bodies.push_back(Body(7, MOD_FARM_MANAGER, 102, 0, 1, 15883, 5));
    bodies.push_back(Body(8, MOD_GAME, 100, 11, 2, 15887, 5));
    bodies.push_back(Body(9, MOD_AGENT, 100, 12, 1, 15888, 5));
    std::vector<SERVER_CORD> cords(1, Zero<SERVER_CORD>());
    cords[0].dwID = 50;
    cords[0].wFromID = 1;
    cords[0].wToID = 2;

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
    CHECK(g_pSelfBody && g_pSelfBody->wID == 1 && g_pSelfBody->byDivisionID == 3);
}

static SERVER_BODY* BodyOf(WORD wID)
{
    return ServerBodyInfo_Find(wID);
}

// ---------------------------------------------------------------------------------------------
// Database rows of the start-up

enum
{
    LATEST_VERSION = 300,
    FILE_ID = 7001,
};

static void SetUpDatabase()
{
    s_db.bEnvResult = TRUE;
    MODULE_VERSION mv = Zero<MODULE_VERSION>();
    mv.ab0[5] = 22;
    mv.byModuleID = MOD_CLIENT;
    mv.dwVersion = LATEST_VERSION;
    strcpy_s((char*)mv.ab11, 32, "SR_Client");
    s_db.vecModules.push_back(mv);
    MODULE_VERSION_FILE f = Zero<MODULE_VERSION_FILE>();
    f.dwID = FILE_ID;
    f.dwVersion = LATEST_VERSION;
    f.byModuleID = MOD_CLIENT;
    strcpy_s(f.szName, "sro_client.exe");
    strcpy_s(f.szPath, ".");
    f.dwSize = 123456;
    f.b532 = 1;
    f.byValid = 1;
    s_db.vecFiles.push_back(f);
    f.dwID = FILE_ID + 1;                   // older than every client asking
    f.dwVersion = 100;
    strcpy_s(f.szName, "old.pk2");
    s_db.vecFiles.push_back(f);

    const char* aszArticle[3][2] = { { "Welcome", "Have fun" }, { "Gamma", "" },
                                     { "Maintenance", "Thursday" } };
    BYTE abType[3] = { 1, 2, 1 };
    for (int i = 0; i < 3; ++i)
    {
        ARTICLE a = Zero<ARTICLE>();
        a.byType = abType[i];
        strcpy_s(a.szTitle, aszArticle[i][0]);
        strcpy_s(a.szText, aszArticle[i][1]);
        for (int j = 0; j < 16; ++j)
            a.abTime[j] = (BYTE)(0xA0 + i * 16 + j);
        s_db.vecArticles.push_back(a);
    }
    s_db.vecShardService.push_back(std::make_pair((WORD)10, (BYTE)SHARD_SERVICE_OPERATING));
    s_db.vecShardService.push_back(std::make_pair((WORD)11, (BYTE)SHARD_SERVICE_EXAMINATION));
    s_db.vecShardService.push_back(std::make_pair((WORD)12, (BYTE)SHARD_SERVICE_HIDDEN));
    s_db.vecShardService.push_back(std::make_pair((WORD)77, (BYTE)SHARD_SERVICE_HIDDEN));
    s_db.nPrivilegedRows = 2;

    g_ServerGlobals.mapExtra["LastFullVersion_SR_Client"] = "290";
    g_ServerGlobals.mapExtra["IBUVCharacterSet"] = "ABCDEFGH";
    g_ServerGlobals.mapExtra["IBUVStringSize"] = "5";
    g_ServerGlobals.mapExtra["IBUVFailureIPTolerance"] = "3";
}

// ---------------------------------------------------------------------------------------------
// The test stays on the loopback address: bind() of this executable (import address table)
// turns INADDR_ANY into 127.0.0.1.

typedef int (WSAAPI* PFN_BIND)(SOCKET s, const sockaddr* pAddr, int nLen);
static PFN_BIND s_pfnBind;
static volatile LONG s_nUdpPingBinds;

static int WSAAPI LoopbackBind(SOCKET s, const sockaddr* pAddr, int nLen)
{
    sockaddr_in addr;
    if (pAddr && nLen == sizeof(addr) && pAddr->sa_family == AF_INET)
    {
        memcpy(&addr, pAddr, sizeof(addr));
        if (addr.sin_addr.s_addr == INADDR_ANY)
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int nResult = s_pfnBind(s, (const sockaddr*)&addr, nLen);
        if (nResult == 0 && addr.sin_port == htons(UDPPING_SERVER_PORT))
            InterlockedIncrement(&s_nUdpPingBinds);
        return nResult;
    }
    return s_pfnBind(s, pAddr, nLen);
}

static void HookBindToLoopback()
{
    s_pfnBind = (PFN_BIND)GetProcAddress(LoadLibraryA("ws2_32.dll"), "bind");
    BYTE* pBase = (BYTE*)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS* pNt = (IMAGE_NT_HEADERS*)(pBase + ((IMAGE_DOS_HEADER*)pBase)->e_lfanew);
    IMAGE_DATA_DIRECTORY& dir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    void** ppEntry = (void**)(pBase + dir.VirtualAddress);
    int nPatched = 0;
    for (DWORD i = 0; i < dir.Size / sizeof(void*); ++i)
    {
        if (ppEntry[i] != (void*)s_pfnBind)
            continue;
        DWORD dwOld;
        VirtualProtect(&ppEntry[i], sizeof(void*), PAGE_READWRITE, &dwOld);
        ppEntry[i] = (void*)LoopbackBind;
        VirtualProtect(&ppEntry[i], sizeof(void*), dwOld, &dwOld);
        ++nPatched;
    }
    CHECK(nPatched == 1);
}

// ---------------------------------------------------------------------------------------------
// Tests

static void TestHelpers()
{
    std::string str;
    IPv4ToString(&str, 0x0403020A);
    CHECK(str == "10.2.3.4");
    CHECK(GatewayMainProcess_IsClientRequestBlocked() == 0);

    str = "a'b\"c' ";
    String_ReplaceQuotesWithSpace(&str);
    CHECK(str == "a b c  ");
    // GW-005: a quote before a byte >= 0x80 stays
    str = "x'\xb0\xa1y";
    String_ReplaceQuotesWithSpace(&str);
    CHECK(str == "x'\xb0\xa1y");
    str = "";
    String_ReplaceQuotesWithSpace(&str);
    CHECK(str.empty());

    g_ServerGlobals.mapExtra["SomeKey"] = "SomeValue";
    CHECK(strcmp(ServerConfig_FindExtraValue("SomeKey"), "SomeValue") == 0);
    CHECK(ServerConfig_FindExtraValue("somekey") == NULL);
    g_ServerGlobals.mapExtra.erase("SomeKey");

    // the monitor graph counters
    MONITOR_COUNTER counter;
    memset(&counter, 0, sizeof(counter));
    g_dwGatewaySessionCreated = 7;
    counter.dwID = 0;
    CHECK(GatewayServer_ReadStatCounter(&counter) && counter.fValue == 7.0f);
    CHECK(g_dwGatewaySessionCreated == 0);
    g_dwGatewaySessionLost = 0x80000000;
    counter.dwID = 1;
    CHECK(GatewayServer_ReadStatCounter(&counter) && counter.fValue == 2147483648.0f);
    CHECK(g_dwGatewaySessionLost == 0);
    counter.dwID = 2;
    counter.fValue = 5;
    CHECK(GatewayServer_ReadStatCounter(&counter) && counter.fValue == 0.0f);
    g_CertifierProcessPool.dwElapsedSum = 100;
    g_CertifierProcessPool.dwElapsedCount = 3;
    CHECK(GatewayServer_ReadStatCounter(&counter) && counter.fValue == 33.0f);
    CHECK(g_CertifierProcessPool.dwElapsedSum == 0 && g_CertifierProcessPool.dwElapsedCount == 0);
    counter.dwID = 3;
    counter.fValue = 9;
    CHECK(GatewayServer_ReadStatCounter(&counter) && counter.fValue == 9.0f);

    // the trigger of the shard map needs the wide member pointers of the original (0x18 bytes)
    CHECK(sizeof(CClassTrigger<CShardConditionManager>) == 0x18);
    CHECK(sizeof(CClassTrigger<CArticleManager>) == 0x10);
    CHECK(g_GatewayServer.m_dw24 == 100 * 1024 * 1024);
}

static void TestStartup()
{
    // engine and processes
    CHECK(g_GatewayServer.InitNetEngine());
    BSNET_INIT_PARAM& p = g_GatewayServer.m_EngineParam;
    CHECK(p.dwFlags == 0x13 && p.nAcceptCount == 100 && p.dwMaxConnectTime == 60);
    // dw8C / dw90 are replaced by the Server.cfg debug options afterwards
    CHECK(p.bSecurityMode == 3 && p.dw8C == g_ServerGlobals.dwDebugOptionDebuggerPresent);
    CHECK(p.nSendFileType == 1 && p.nRecvFileType == 1 && p.dwFileCacheLimit == 0);
    CHECK(p.aSocket[1].nSendQueueLimit == -1 && p.aSocket[0].dwSessionParam == SERVER_TASK_MAIN);
    CHECK(g_GatewayServer.RegisterServerTasks());
    CHECK(s_nCertifierPoolInits == 1);
    CHECK(g_pBSNet->GetProactor(GATEWAY_TASK_IBUV) != NULL);
    CHECK(g_pBSNet->GetProactor(GATEWAY_TASK_IBUV)->GetJob()->GetRuntimeClass()
          == &CFakeIBUVProcess::classCFakeIBUVProcess);
    CHECK(Gateway()->GetRuntimeClass() == &CGatewayMainProcess::classCGatewayMainProcess);
    CHECK(strcmp(CGatewayMainProcess::classCGatewayMainProcess.m_lpszClassName,
                 "CGatewayMainProcess") == 0);
    CHECK(CGatewayMainProcess::classCGatewayMainProcess.m_nObjectSize == 0x400DC);
    CHECK(WaitLog("UDP Ping Service was Started!", 0));
    CHECK(Gateway()->m_UdpPing.m_hThread != NULL);
    // the echo service answers a "REQ" datagram with "ACK" (when the port was free)
    if (s_nUdpPingBinds == 1)
    {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in to;
        memset(&to, 0, sizeof(to));
        to.sin_family = AF_INET;
        to.sin_port = htons(UDPPING_SERVER_PORT);
        to.sin_addr.s_addr = inet_addr("127.0.0.1");
        CHECK(UdpPing_SendReq(s, (const sockaddr*)&to, 0x11, 0x22) == UDPPING_PACKET_SIZE);
        DWORD dwTimeout = 2000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&dwTimeout, sizeof(dwTimeout));
        char buf[32];
        CHECK(recv(s, buf, sizeof(buf), 0) == UDPPING_PACKET_SIZE);
        CHECK(memcmp(buf, "ACK", 4) == 0 && *(DWORD*)(buf + 8) == 0x11
              && *(DWORD*)(buf + 12) == 0x22);
        closesocket(s);
    }
    else
    {
        printf("GatewayTest: UDP ping port %d in use, echo not checked\n", UDPPING_SERVER_PORT);
    }

    SetUpArchitecture();
    SetUpDatabase();

    // failures before the module version data
    size_t nLog = 0;
    s_db.bEnvResult = FALSE;
    CHECK(!g_GatewayServer.OnLocalDataInitialized());
    CHECK(s_db.strEnvConnect == "DSN=GatewayTest" && s_db.vecConnects.empty());
    CHECK(g_GatewayServer.m_mapLastFullVersion.size() == 1
          && g_GatewayServer.m_mapLastFullVersion["SR_Client"] == 290);
    CHECK(WaitLog("FullVersion Setup [SR_Client] : Version [290]", 0));
    s_db.bEnvResult = TRUE;

    s_db.setFailingConnects.insert("");
    CHECK(!g_GatewayServer.OnLocalDataInitialized());
    CHECK(WaitLog("cannot connect DB", 0x2000000));
    CHECK(WaitLog("cannot load module version data from DB", 0x2000000));
    s_db.setFailingConnects.clear();

    // division 2: the account database gets the built-in password after "PWD="
    g_pSelfBody->byDivisionID = 2;
    CHECK(!g_GatewayServer.OnLocalDataInitialized());
    CHECK(WaitLog("DB Connected.", 0));
    CHECK(WaitLog("AccountDBConfig required !!!", 0x2000000));
    CHECK(WaitLog("cannot load module version data from DB", 0x2000000));
    g_ServerGlobals.mapExtra["AccountDBConfig"] = "DSN=Account;UID=sa;";
    DrainLogs();
    nLog = s_vecLog.size();
    CHECK(!g_GatewayServer.OnLocalDataInitialized());
    CHECK(WaitLog("invalid account db connection string !!!", 0x2000000));
    Sleep(50);
    CHECK(!HasLogSince(nLog, "cannot load module version data"));
    g_ServerGlobals.mapExtra["AccountDBConfig"] = "DSN=Account;PWD=;UID=sa";
    s_db.setFailingConnects.insert("DSN=Account;PWD=;UID=sa");
    CHECK(!g_GatewayServer.OnLocalDataInitialized());
    // GW-004: the connection string with the password goes to the log
    CHECK(WaitLog("cannot connect to account DB : DSN=Account;PWD=;UID=sa", 0x2000000));
    CHECK(WaitLog("cannot load module version data from DB", 0x2000000));
    CHECK(s_db.vecConnects.back() == "DSN=Account;PWD=;UID=sa");
    s_db.setFailingConnects.clear();

    // divisions 1 and 11 connect to AccountDBConfig as it is
    g_pSelfBody->byDivisionID = 11;
    s_db.setFailingConnects.insert("DSN=Account;PWD=;UID=sa");
    CHECK(!g_GatewayServer.OnLocalDataInitialized());
    CHECK(WaitLog("cannot connect to DB : DSN=Account;PWD=;UID=sa", 0x2000000));
    s_db.setFailingConnects.clear();
    g_ServerGlobals.mapExtra.erase("AccountDBConfig");
    g_pSelfBody->byDivisionID = 1;
    CHECK(!g_GatewayServer.OnLocalDataInitialized());
    CHECK(WaitLog("need AccountDBConfig !!!", 0x2000000));
    g_pSelfBody->byDivisionID = 3;

    s_db.setFailingQueries.insert("select * from _ModuleVersionFile with (NOLOCK) where nValid = 1");
    CHECK(!g_GatewayServer.OnLocalDataInitialized());
    CHECK(WaitLog("cannot loading _ModuleVersionFile Table", 0x2000001));
    CHECK(WaitLog("cannot load module version data from DB", 0x2000000));
    CHECK(g_pModuleVersionList == NULL);
    s_db.setFailingQueries.clear();

    // the start-up
    s_db.vecQueries.clear();
    s_db.vecConnects.clear();
    g_ServerGlobals.mapExtra["AccountDBConfig"] = "ignored in division 3";
    CHECK(g_GatewayServer.OnLocalDataInitialized());
    CHECK(s_db.vecConnects.size() == 1 && s_db.vecConnects[0].empty());
    CHECK(g_pGatewayDBSession != NULL && g_pAccountDBSession == g_pGatewayDBSession);
    CHECK(s_db.vecQueries.size() == 5);
    CHECK(s_db.vecQueries[2] == "select * from _PrivilegedIP");
    CHECK(s_db.vecQueries[3] == "exec _GetArticleData");
    CHECK(s_db.vecQueries[4] == "select * from _ShardService");
    CHECK(s_nPrivilegedRows == 2);
    CHECK(WaitLog("version : 22, 300, SR_Client", 0));
    CHECK(ModuleVersion_GetLatest(MOD_CLIENT) == LATEST_VERSION);
    CHECK(g_listFarmManagerAddr.size() == 1
          && g_listFarmManagerAddr.front().byFarmID == 1
          && g_listFarmManagerAddr.front().dwIP == inet_addr("10.4.5.6"));
    CHECK(WaitLog("shard service : 10,51", 0));
    CHECK(WaitLog("shard Alpha is ready for gateway service", 0));
    CHECK(WaitLog("shard Gamma is ready for gateway service", 0));
    CHECK(g_ShardConditionManager.size() == 3);
    SHARD_CONDITION* pAlpha = ShardMap_Find(10);
    CHECK(pAlpha && pAlpha->listAgent.size() == 2 && pAlpha->listBody.size() == 2);
    SHARD_CONDITION* pBeta = ShardMap_Find(11);
    CHECK(pBeta && pBeta->listAgent.size() == 1 && pBeta->listBody.size() == 2);
    CHECK(ShardMap_Find(13) == NULL);
    CHECK(pAlpha->pShard->b556 == 0 && pBeta->pShard->b556 == 1);
    CHECK(pAlpha->byServiceType == SHARD_SERVICE_OPERATING);
    CHECK(pBeta->byServiceType == SHARD_SERVICE_EXAMINATION);
    CHECK(ShardMap_Find(12)->byServiceType == SHARD_SERVICE_HIDDEN);
    CHECK(g_DownloadServerList.list.size() == 2);
    CHECK(WaitLog("article loaded : 3", 0));
    CHECK(WaitLog("shard service type refreshed : [10]Alpha -> Operating", 0));
    CHECK(WaitLog("shard service type refreshed : [11]Beta -> Examination", 0));
    CHECK(WaitLog("shard service type refreshed : [12]Gamma -> Hidden", 0));
    CHECK(g_ArticleManager.m_listArticle.size() == 3);
    CHECK(WaitLog("IBUV Initialization Start Now!", 0));
    CHECK(WaitLog("IBUV: StringSize: 5, CharSet: ABCDEFGH", 0x2000001));
    CHECK(s_nIBUVInits == 1 && s_pIBUVOwner == g_pBSNet->GetProactor(SERVER_TASK_MAIN));
    CHECK(s_dwIBUVReserve == 20000 && s_fIBUVRatio == 0.05f && s_nIBUVStringSize == 5);
    CHECK(s_strIBUVCharSet == "ABCDEFGH" && s_dwIPTolerance == 3);
    CHECK(WaitLog("IBUV Process Initialization Successfully Finished!", 0));
    CHECK(g_pSelfBody->nState == 4);
    CHECK(ClientVersion_IsBelowRequired(CertModule_FindByName("SR_Client"), 289));
    CHECK(!ClientVersion_IsBelowRequired(CertModule_FindByName("SR_Client"), 290));
    CHECK(!ClientVersion_IsBelowRequired(CertModule_FindByName("AgentServer"), 1));
}

static void TestRoundRobin()
{
    // AgentServers of shard 10: body 2 in state 5, body 3 in state 4
    CHECK(ShardService_PickAgentServerRR(10) == BodyOf(2));
    CHECK(ShardService_PickAgentServerRR(10) == BodyOf(2));
    BodyOf(3)->nState = 5;
    CHECK(ShardService_PickAgentServerRR(10) == BodyOf(3));
    CHECK(ShardService_PickAgentServerRR(10) == BodyOf(2));
    BodyOf(2)->nState = 4;
    BodyOf(3)->nState = 4;
    CHECK(ShardService_PickAgentServerRR(10) == NULL);
    CHECK(ShardService_PickAgentServerRR(99) == NULL);
    BodyOf(2)->nState = 5;
    BodyOf(3)->nState = 4;

    CHECK(ServerBody_FindByPortAndPublicIP("10.1.2.3", 15882) == BodyOf(6));
    CHECK(ServerBody_FindByPortAndPublicIP("10.1.2.3", 15884) == NULL);
    CHECK(ServerBody_FindByPortAndPublicIP("10.1.2.4", 15882) == NULL);

    // download servers: body 5 in state 5, body 6 in state 4
    CHECK(SelectPatchServer_RoundRobin() == BodyOf(5));
    CHECK(SelectPatchServer_RoundRobin() == BodyOf(5));
    BodyOf(6)->nState = 5;
    CHECK(SelectPatchServer_RoundRobin() == BodyOf(6));
    CHECK(SelectPatchServer_RoundRobin() == BodyOf(5));
    BodyOf(5)->nState = 4;
    BodyOf(6)->nState = 4;
    CHECK(SelectPatchServer_RoundRobin() == NULL);
    BodyOf(5)->nState = 5;
}

// The expected 0xA100 patch answer.
static Bytes PatchAnswer(SERVER_BODY* pServer)
{
    Bytes v;
    PutB(v, 2);
    PutB(v, 2);
    PutS(v, pServer->pMachine->szPublicIP);
    PutW(v, pServer->wPort);
    PutD(v, LATEST_VERSION);
    PutB(v, 1);
    PutD(v, FILE_ID);
    PutS(v, "sro_client.exe");
    PutS(v, "");
    PutD(v, 123456);
    PutB(v, 1);
    PutB(v, 0);
    return v;
}

static Bytes VersionRequest(BYTE byContent, const char* pszModule, DWORD dwVersion)
{
    Bytes v;
    PutB(v, byContent);
    PutS(v, pszModule);
    PutD(v, dwVersion);
    return v;
}

static Bytes AskVersion(const CLIENT& c, const Bytes& request)
{
    SendPacket(c.dwClient, GATEWAY_MSG_VERSION_REQ, request);
    Bytes answer;
    CHECK(TakeMassive(c.dwClient, GATEWAY_MSG_VERSION_ACK, &answer));
    return answer;
}

static Bytes B2(BYTE b1, BYTE b2)
{
    Bytes v;
    PutB(v, b1);
    PutB(v, b2);
    return v;
}

static void TestVersionCheck()
{
    CLIENT c = Connect();
    Bytes one(1, 1);

    // state 4 after the start-up: not in service
    CHECK(Same(AskVersion(c, VersionRequest(22, "SR_Client", LATEST_VERSION)), B2(2, 4)));
    // a privileged address passes
    s_dwPrivilegedIP = inet_addr("127.0.0.1");
    CHECK(Same(AskVersion(c, VersionRequest(22, "SR_Client", LATEST_VERSION)), one));
    CHECK(WaitLog("[GatewaySvr] Official IP Detected, Official IP Pass the 'In Service' Check "
                  "(127.0.0.1)", 0x2000001));
    s_dwPrivilegedIP = 0xFFFFFFFF;
    g_pSelfBody->nState = 5;

    CHECK(Same(AskVersion(c, VersionRequest(22, "sr_client", LATEST_VERSION)), one));
    CHECK(Same(AskVersion(c, VersionRequest(22, "SR_Client", LATEST_VERSION - 1)),
               PatchAnswer(BodyOf(5))));
    CHECK(Same(AskVersion(c, VersionRequest(22, "SR_Client", 290)), PatchAnswer(BodyOf(5))));
    // older than LastFullVersion_SR_Client
    CHECK(Same(AskVersion(c, VersionRequest(22, "SR_Client", 289)), B2(2, 5)));
    // newer than the latest version
    CHECK(Same(AskVersion(c, VersionRequest(22, "SR_Client", LATEST_VERSION + 1)), B2(2, 1)));
    CHECK(WaitLog("client requested higher version than the latest version known to server! "
                  "about to exploit?: [contents: 22, module: SR_Client, requested ver: 301, "
                  "latest ver: 300", 0));
    CHECK(WaitLog("source ip for trying to exploit : 127.0.0.1", 0x2000001));
    // unknown module, version 0
    CHECK(Same(AskVersion(c, VersionRequest(23, "Nope", 5)), B2(2, 1)));
    CHECK(WaitLog("someone tried to login with unknown client: [content id: 23, module name: "
                  "Nope]", 0));
    CHECK(WaitLog("source ip for trying to exploit : 127.0.0.1", 0x2000001));
    CHECK(Same(AskVersion(c, VersionRequest(22, "SR_Client", 0)), B2(2, 1)));
    // no download server in state 5
    BodyOf(5)->nState = 4;
    CHECK(Same(AskVersion(c, VersionRequest(22, "SR_Client", 295)), B2(2, 3)));
    CHECK(WaitLog("can't find any available patch server!!!", 0));
    BodyOf(5)->nState = 5;

    // malformed requests close the session
    Bytes empty;
    SendPacket(c.dwClient, GATEWAY_MSG_VERSION_REQ, empty);
    CHECK(WaitLog("certificate client module : invalid msg data (0)", 0x1000000));
    CHECK(Closed(c));
    c = Connect();
    Bytes shortModule;
    PutB(shortModule, 22);
    PutW(shortModule, 0);
    SendPacket(c.dwClient, GATEWAY_MSG_VERSION_REQ, shortModule);
    CHECK(WaitLog("certificate client module : invalid msg data (1)", 0x1000000));
    CHECK(Closed(c));
    c = Connect();
    Bytes shortVersion = VersionRequest(22, "SR_Client", 1);
    shortVersion.resize(shortVersion.size() - 1);
    SendPacket(c.dwClient, GATEWAY_MSG_VERSION_REQ, shortVersion);
    CHECK(WaitLog("certificate client module : invalid msg data (2)", 0x1000000));
    CHECK(Closed(c));
}

// The expected 0xA101 answer.
static Bytes ShardList(bool bPrivileged, bool bFarmByte)
{
    Bytes v;
    PutB(v, 1);
    PutB(v, 1);
    PutS(v, "FarmOne");
    PutB(v, 1);
    PutB(v, 2);
    PutS(v, "F2");
    PutB(v, 0);
    struct { WORD wID; const char* pszName; WORD wUsers; WORD wLimit; BYTE byReady;
             BYTE byFarm; } shards[3] =
    {
        { 10, "Alpha", 77, 1000, ShardMap_Find(10)->pShard->b556, 1 },
        { 11, "Beta", 88, 2000, (BYTE)(bPrivileged ? 1 : 0), 2 },
        { 12, "Gamma", 0, 3000, 1, 1 },
    };
    for (int i = 0; i < 3; ++i)
    {
        if (i == 2 && !bPrivileged)
            continue;
        PutB(v, 1);
        PutW(v, shards[i].wID);
        PutS(v, shards[i].pszName);
        PutW(v, shards[i].wUsers);
        PutW(v, shards[i].wLimit);
        PutB(v, shards[i].byReady);
        if (bFarmByte)
            PutB(v, shards[i].byFarm);
    }
    PutB(v, 0);
    return v;
}

static void TestShardList()
{
    CLIENT c = Connect();
    // user counts
    Bytes users;
    PutB(users, 2);
    PutW(users, 10);
    PutW(users, 77);
    PutW(users, 11);
    PutW(users, 88);
    SendPacket(c.dwClient, GATEWAY_MSG_SHARD_USERS, users);

    Bytes empty;
    Bytes answer;
    SendPacket(c.dwClient, GATEWAY_MSG_SHARD_LIST_REQ, empty);
    CHECK(TakePacket(c.dwClient, GATEWAY_MSG_SHARD_LIST_ACK, &answer));
    CHECK(Same(answer, ShardList(false, true)));
    CHECK(ShardMap_Find(10)->pShard->w557 == 77);

    s_dwPrivilegedIP = inet_addr("127.0.0.1");
    SendPacket(c.dwClient, GATEWAY_MSG_SHARD_LIST_REQ, empty);
    CHECK(TakePacket(c.dwClient, GATEWAY_MSG_SHARD_LIST_ACK, &answer));
    CHECK(Same(answer, ShardList(true, true)));
    s_dwPrivilegedIP = 0xFFFFFFFF;

    // the farm byte only in some divisions
    g_pSelfBody->byDivisionID = 1;
    SendPacket(c.dwClient, GATEWAY_MSG_SHARD_LIST_REQ, empty);
    CHECK(TakePacket(c.dwClient, GATEWAY_MSG_SHARD_LIST_ACK, &answer));
    CHECK(Same(answer, ShardList(false, false)));
    g_pSelfBody->byDivisionID = 3;

    // a body of shard 10 changes its state: the gateway recomputes the ready flags
    CHECK(ShardMap_Find(10)->pShard->b556 == 0);
    CHECK(g_pServerBodyStateObserver != NULL);
    ServerBody_SetStateAndBroadcast2005(5, 3, 0);
    CHECK(ShardMap_Find(10)->pShard->b556 == 1);
    SendPacket(c.dwClient, GATEWAY_MSG_SHARD_LIST_REQ, empty);
    CHECK(TakePacket(c.dwClient, GATEWAY_MSG_SHARD_LIST_ACK, &answer));
    CHECK(Same(answer, ShardList(false, true)));
    ServerBody_SetStateAndBroadcast2005(4, 3, 0);
    CHECK(ShardMap_Find(10)->pShard->b556 == 0);
    // a body outside the shards does not touch them
    ShardMap_Find(10)->pShard->b556 = 9;
    ServerBody_SetStateAndBroadcast2005(4, 5, 0);
    CHECK(ShardMap_Find(10)->pShard->b556 == 9);
    ServerBody_SetStateAndBroadcast2005(5, 5, 0);
    ShardMap_RecomputeReadyFlags();

    // the FarmManagers
    SendPacket(c.dwClient, GATEWAY_MSG_FARM_MANAGER_REQ, empty);
    CHECK(TakePacket(c.dwClient, GATEWAY_MSG_FARM_MANAGER_ACK, &answer));
    Bytes farm;
    PutB(farm, 1);
    PutB(farm, 1);
    PutD(farm, inet_addr("10.4.5.6"));
    CHECK(Same(answer, farm));

    // service type of one shard, relayed request answered through 0xA008
    s_db.vecShardService[1].second = SHARD_SERVICE_OPERATING;
    Bytes reload;
    PutW(reload, 11);
    SendPacket(c.dwClient, GATEWAY_MSG_SHARD_SERVICE_REQ, reload);
    CHECK(TakePacket(c.dwClient, SERVER_MSG_RELAY_ACK, &answer));
    Bytes ack;
    PutB(ack, 1);
    PutD(ack, 0);
    PutW(ack, GATEWAY_MSG_SHARD_SERVICE_ACK);
    PutB(ack, 1);
    CHECK(Same(answer, ack));
    CHECK(WaitLog("shard service type refreshed : [11]Beta -> Operating", 0));
    CHECK(s_db.vecQueries.back() == "select * from _ShardService where ShardID = 11");
    CHECK(ShardMap_Find(11)->byServiceType == SHARD_SERVICE_OPERATING);
    s_db.setFailingQueries.insert("select * from _ShardService where ShardID = 11");
    CHECK(!DB_RefreshShardServiceType(11));
    CHECK(WaitLog("refresh shard service type failed!!!", 0x2000000));
    s_db.setFailingQueries.clear();
    s_db.vecShardService[1].second = SHARD_SERVICE_EXAMINATION;
    CHECK(DB_RefreshShardServiceType(11));
    CHECK(ShardMap_Find(11)->byServiceType == SHARD_SERVICE_EXAMINATION);

    // GW-001: an unknown shard in the user counts faults the handler, the session is closed
    Bytes bad;
    PutB(bad, 1);
    PutW(bad, 99);
    PutW(bad, 1);
    SendPacket(c.dwClient, GATEWAY_MSG_SHARD_USERS, bad);
    CHECK(Closed(c));
}

static Bytes Article(const char* pszTitle, const char* pszText, int nIndex)
{
    Bytes v;
    PutS(v, pszTitle);
    PutS(v, pszText);
    for (int j = 0; j < 16; ++j)
        PutB(v, (BYTE)(0xA0 + nIndex * 16 + j));
    return v;
}

static void TestNotices()
{
    CLIENT c = Connect();
    Bytes request(1, 1);
    Bytes answer;
    SendPacket(c.dwClient, GATEWAY_MSG_NOTICE_REQ, request);
    CHECK(TakeMassive(c.dwClient, GATEWAY_MSG_NOTICE_ACK, &answer));
    Bytes want(1, 2);
    Bytes a = Article("Welcome", "Have fun", 0);
    want.insert(want.end(), a.begin(), a.end());
    a = Article("Maintenance", "Thursday", 2);
    want.insert(want.end(), a.begin(), a.end());
    CHECK(Same(answer, want));

    // without a type: 2 outside division 1
    Bytes empty;
    SendPacket(c.dwClient, GATEWAY_MSG_NOTICE_REQ, empty);
    CHECK(TakeMassive(c.dwClient, GATEWAY_MSG_NOTICE_ACK, &answer));
    want.assign(1, 1);
    a = Article("Gamma", "", 1);
    want.insert(want.end(), a.begin(), a.end());
    CHECK(Same(answer, want));
    g_pSelfBody->byDivisionID = 1;
    SendPacket(c.dwClient, GATEWAY_MSG_NOTICE_REQ, empty);
    CHECK(TakeMassive(c.dwClient, GATEWAY_MSG_NOTICE_ACK, &answer));
    CHECK(answer.size() > 1 && answer[0] == 2);
    g_pSelfBody->byDivisionID = 3;
    request[0] = 9;
    SendPacket(c.dwClient, GATEWAY_MSG_NOTICE_REQ, request);
    CHECK(TakeMassive(c.dwClient, GATEWAY_MSG_NOTICE_ACK, &answer));
    CHECK(Same(answer, Bytes(1, 0)));

    // reload through a relayed request: 0xA008 {1, relay ID, 0xA307}
    s_db.vecArticles.pop_back();
    CMsg* pMsg = MakeMsg(c.dwServer, GATEWAY_MSG_RELOAD_ARTICLE_REQ, empty);
    Gateway()->OnMsg_6307(pMsg, 0x1234, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(TakePacket(c.dwClient, SERVER_MSG_RELAY_ACK, &answer));
    Bytes ack;
    PutB(ack, 1);
    PutD(ack, 0x1234);
    PutW(ack, GATEWAY_MSG_RELOAD_ARTICLE_ACK);
    CHECK(Same(answer, ack));
    CHECK(WaitLog("article loaded : 2", 0));
    s_db.setFailingQueries.insert("exec _GetArticleData");
    CHECK(!CArticleManager_ReloadFromDB());
    CHECK(WaitLog("article load failed !!", 0x2000000));
    CHECK(g_ArticleManager.m_listArticle.empty());
    s_db.setFailingQueries.clear();
    CHECK(CArticleManager_ReloadFromDB());

    // the patch is ready: the client sessions are closed (servers' sessions are excluded from
    // the cleanup), state 4, 0xA309 {1}
    CLIENT player = Connect();
    g_pBSNet->ExcludeSessionFromCleanup(c.dwServer);
    g_pSelfBody->nState = 5;
    pMsg = MakeMsg(c.dwServer, GATEWAY_MSG_PATCH_DONE_REQ, empty);
    Gateway()->OnMsg_6308(pMsg, 0x55, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(TakePacket(c.dwClient, SERVER_MSG_RELAY_ACK, &answer));
    ack.clear();
    PutB(ack, 1);
    PutD(ack, 0x55);
    PutW(ack, GATEWAY_MSG_PATCH_DONE_ACK);
    PutB(ack, 1);
    CHECK(Same(answer, ack));
    CHECK(WaitLog("preparing patch is completed !!!", 0));
    CHECK(g_pSelfBody->nState == 4);
    CHECK(Closed(player));
    g_pSelfBody->nState = 5;
    g_pBSNet->DisconnectSession(c.dwClient, 1);
    CHECK(Closed(c));
}

static void TestModuleVersionData()
{
    std::vector<MODULE_VERSION> modules(1, s_db.vecModules[0]);
    modules[0].dwVersion = 400;
    CMassiveMsg* p = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    p->SetOpcode(GATEWAY_MSG_MODULE_VERSION_ACK);
    BYTE b = 1;
    p->Write(&b, 1);
    p->Write(&b, 1);
    p->Write(&modules[0], sizeof(MODULE_VERSION));
    b = 0;
    p->Write(&b, 1);
    b = 1;
    p->Write(&b, 1);
    p->Write(&s_db.vecFiles[0], sizeof(MODULE_VERSION_FILE));
    b = 0;
    p->Write(&b, 1);
    p->BeginRead();
    Gateway()->OnMsg_A306(NULL, 0, 0, p);
    p->Free();
    CHECK(WaitLog("retrieve module version data from divisionmanager", 0));
    CHECK(ModuleVersion_GetLatest(MOD_CLIENT) == 400);
    CHECK(g_pModuleVersionFileList->size() == 1);

    p = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    p->SetOpcode(GATEWAY_MSG_MODULE_VERSION_ACK);
    b = 2;
    p->Write(&b, 1);
    p->BeginRead();
    Gateway()->OnMsg_A306(NULL, 0, 0, p);
    p->Free();
    CHECK(WaitLog("cannot retrieve module version data from divisionmanager", 0x2000000));
    CHECK(ModuleVersion_GetLatest(MOD_CLIENT) == 400);
}

static CERTIFY_PARAM* NewParam(const CLIENT& c)
{
    CERTIFY_PARAM* p = new CERTIFY_PARAM;
    p->dw0 = 4711;
    p->dwType = 1;
    p->strID = "tester";
    p->str24 = "secret";
    p->b40 = 0x40;
    p->b41 = 0x41;
    p->dwSessionID = c.dwServer;
    p->dw258 = 0x258;
    p->pAgentBody = BodyOf(2);
    p->pIBUVData = NULL;
    p->w26C = 0x26C;
    p->dw270 = 0x270;
    return p;
}

static bool Released(CERTIFY_PARAM* p)
{
    EnterCriticalSection(&s_cs);
    bool b = !s_vecReleased.empty() && s_vecReleased.back() == p;
    LeaveCriticalSection(&s_cs);
    return b;
}

// A login fails: 0xA102 to the client, the session closes, the parameter is released.
static void ExpectFailure(const CLIENT& c, CERTIFY_PARAM* p, const Bytes& want)
{
    Bytes answer;
    CHECK(TakePacket(c.dwClient, GATEWAY_MSG_LOGIN_ACK, &answer));
    CHECK(Same(answer, want));
    CHECK(Closed(c));
    CHECK(Released(p));
    delete p;
}

static void TestLoginResults()
{
    // 0xA200 error codes (GlobalManager): {DWORD, BYTE result, BYTE error}
    const BYTE abError[8][3] =
    {
        { 1, 3, 0 }, { 2, 8, 0 }, { 3, 2, 2 }, { 4, 2, 4 }, { 5, 6, 0 }, { 6, 9, 0 },
        { 7, 6, 0 }, { 0, 6, 0 },
    };
    for (int i = 0; i < 8; ++i)
    {
        CLIENT c = Connect();
        CERTIFY_PARAM* p = NewParam(c);
        Bytes data;
        PutD(data, 0x11223344);
        PutB(data, 2);
        PutB(data, abError[i][0]);
        CMsg* pMsg = MakeMsg(0, GATEWAY_MSG_FIND_USER_ACK, data);
        Gateway()->OnMsg_A200(pMsg, 0, (DWORD)p, NULL);
        CHECK(pMsg->GetUnreadSize() == 0);
        g_pBSNet->ReleaseMsg(pMsg);
        Bytes want = B2(2, abError[i][1]);
        if (abError[i][2])
            PutB(want, abError[i][2]);
        ExpectFailure(c, p, want);
        if (i >= 6)
        {
            char sz[64];
            sprintf_s(sz, "FIND_CONTENT_USER_ACK: Unknown ErrorCode : %d", abError[i][0]);
            CHECK(WaitLog(sz, 0x1000000));
        }
    }
    // result 1: a captcha image is requested; without a parameter nothing happens
    {
        CLIENT c = Connect();
        CERTIFY_PARAM* p = NewParam(c);
        Bytes data;
        PutD(data, 1);
        PutB(data, 1);
        CMsg* pMsg = MakeMsg(0, GATEWAY_MSG_FIND_USER_ACK, data);
        Gateway()->OnMsg_A200(pMsg, 0, (DWORD)p, NULL);
        g_pBSNet->ReleaseMsg(pMsg);
        CHECK(s_vecImageRequests.size() == 1 && s_vecImageRequests.back() == p);
        data.push_back(5);
        pMsg = MakeMsg(0, GATEWAY_MSG_FIND_USER_ACK, data);
        size_t nReleased = s_vecReleased.size();
        Gateway()->OnMsg_A200(pMsg, 0, 0, NULL);
        CHECK(pMsg->GetUnreadSize() == 1);
        g_pBSNet->ReleaseMsg(pMsg);
        CHECK(s_vecReleased.size() == nReleased && s_vecImageRequests.size() == 1);

        // 0xA111 kind 0: wrong password, the session stays without the close flag
        Bytes fail;
        PutS(fail, "tester");
        PutB(fail, 0);
        PutB(fail, 0);
        PutD(fail, 2);
        PutD(fail, 5);
        pMsg = MakeMsg(0, GATEWAY_MSG_VALIDATION_FAIL_ACK, fail);
        Gateway()->OnMsg_A111(pMsg, 0, (DWORD)p, NULL);
        g_pBSNet->ReleaseMsg(pMsg);
        Bytes answer;
        CHECK(TakePacket(c.dwClient, GATEWAY_MSG_LOGIN_ACK, &answer));
        Bytes want = B2(2, 1);
        PutD(want, 2);
        PutD(want, 5);
        CHECK(Same(answer, want));
        CHECK(s_vecReleased.size() == nReleased);
        // kind 1: wrong captcha, 0xA323 and a new image
        fail[8] = 1;
        pMsg = MakeMsg(0, GATEWAY_MSG_VALIDATION_FAIL_ACK, fail);
        Gateway()->OnMsg_A111(pMsg, 0, (DWORD)p, NULL);
        g_pBSNet->ReleaseMsg(pMsg);
        CHECK(TakePacket(c.dwClient, GATEWAY_MSG_IBUV_ANSWER_ACK, &answer));
        want.clear();
        PutB(want, 2);
        PutD(want, 2);
        PutD(want, 5);
        CHECK(Same(answer, want));
        CHECK(s_vecImageRequests.size() == 2 && s_vecReleased.size() == nReleased);
        // kind 1 with the close flag
        fail[9] = 1;
        pMsg = MakeMsg(0, GATEWAY_MSG_VALIDATION_FAIL_ACK, fail);
        Gateway()->OnMsg_A111(pMsg, 0, (DWORD)p, NULL);
        g_pBSNet->ReleaseMsg(pMsg);
        CHECK(TakePacket(c.dwClient, GATEWAY_MSG_IBUV_ANSWER_ACK, &answer));
        CHECK(Same(answer, want));
        CHECK(Closed(c));
        CHECK(Released(p) && s_vecImageRequests.size() == 2);
        delete p;
    }
    // kind 0 with the close flag
    {
        CLIENT c = Connect();
        CERTIFY_PARAM* p = NewParam(c);
        Bytes fail;
        PutS(fail, "tester");
        PutB(fail, 0);
        PutB(fail, 1);
        PutD(fail, 5);
        PutD(fail, 5);
        CMsg* pMsg = MakeMsg(0, GATEWAY_MSG_VALIDATION_FAIL_ACK, fail);
        Gateway()->OnMsg_A111(pMsg, 0, (DWORD)p, NULL);
        g_pBSNet->ReleaseMsg(pMsg);
        Bytes want = B2(2, 1);
        PutD(want, 5);
        PutD(want, 5);
        ExpectFailure(c, p, want);
    }

    // relays of a login that fail
    const WORD awOpcode[4] = { GATEWAY_MSG_AGENT_LOGIN_REQ, GATEWAY_MSG_FIND_USER_ACK,
                               GATEWAY_MSG_VALIDATION_FAIL_REQ, GATEWAY_MSG_VALIDATION_OK_REQ };
    for (int i = 0; i < 4; ++i)
    {
        CLIENT c = Connect();
        CERTIFY_PARAM* p = NewParam(c);
        Gateway()->OnRelayFailed(awOpcode[i], (DWORD)p);
        if (i == 2)
            CHECK(WaitLog("Unhandled Process User Validation Failure Req!", 0x1000000));
        if (i == 3)
            CHECK(WaitLog("Unhandled Process User Validation Success Req!", 0x1000000));
        ExpectFailure(c, p, B2(2, 6));
    }
    size_t nReleased = s_vecReleased.size();
    Gateway()->OnRelayFailed(0x7001, 0x99);
    Gateway()->OnRelayFailed(0x6110, 0x98);
    CHECK(WaitLog("unhandled relay msg error : wRealMsgID : 0x7001 : dwParam : 0x99", 0x1000000));
    CHECK(WaitLog("unhandled relay msg error : wRealMsgID : 0x6110 : dwParam : 0x98", 0x1000000));
    CHECK(s_vecReleased.size() == nReleased);

    // 0xA112: no route to the AgentServer
    {
        CLIENT c = Connect();
        CERTIFY_PARAM* p = NewParam(c);
        Bytes ok;
        PutS(ok, "tester");
        CMsg* pMsg = MakeMsg(0, GATEWAY_MSG_VALIDATION_OK_ACK, ok);
        Gateway()->OnMsg_A112(pMsg, 0, (DWORD)p, NULL);
        g_pBSNet->ReleaseMsg(pMsg);
        ExpectFailure(c, p, B2(2, 6));
        CHECK(WaitLog("OnFRAMEWORKMSG_PROCESS_USER_VALIDATION_SUCCESS_ACK RelayMsg Failed "
                      "(AgentSvrBodyID : 2, User Info (4711, tester)", 0x2000001));
    }
    // the route to body 2 goes through this client: the login arrives as 0x6008
    {
        CLIENT c = Connect();
        CERTIFY_PARAM* p = NewParam(c);
        SERVER_CORD* pCord = ServerCordMap_FindById(50);
        CHECK(pCord != NULL);
        pCord->dwSessionID = c.dwServer;
        g_hmapServerRoute[2] = pCord;
        Bytes ok;
        PutS(ok, "tester");
        CMsg* pMsg = MakeMsg(0, GATEWAY_MSG_VALIDATION_OK_ACK, ok);
        size_t nReleased2 = s_vecReleased.size();
        Gateway()->OnMsg_A112(pMsg, 0, (DWORD)p, NULL);
        g_pBSNet->ReleaseMsg(pMsg);
        Bytes relay;
        CHECK(TakePacket(c.dwClient, SERVER_MSG_RELAY_REQ, &relay));
        Bytes want;
        PutD(want, relay.size() >= 4 ? *(DWORD*)&relay[0] : 0);
        PutW(want, 2);
        PutW(want, GATEWAY_MSG_AGENT_LOGIN_REQ);
        PutD(want, 0x258);
        PutS(want, "tester");
        PutS(want, "secret");
        PutD(want, 4711);
        PutB(want, 0x40);
        PutB(want, 0x41);
        PutW(want, 0x26C);
        PutD(want, 0x270);
        CHECK(Same(relay, want));
        CHECK(relay.size() >= 4 && *(DWORD*)&relay[0] != 0);
        CHECK(s_vecReleased.size() == nReleased2);
        g_hmapServerRoute.erase(2);
        pCord->dwSessionID = 0;
        g_pBSNet->DisconnectSession(c.dwClient, 1);
        CHECK(Closed(c));
        delete p;
    }
}

static bool AllLost()
{
    return g_dwGatewaySessionLost == g_dwGatewaySessionCreated;
}

static void TestSessionEvents()
{
    // every connection of the earlier tests is gone on the gateway's side too
    CHECK(WaitFor(AllLost));
    DrainLogs();
    DWORD dwCreated = g_dwGatewaySessionCreated;
    DWORD dwLost = g_dwGatewaySessionLost;
    CLIENT c = Connect();
    CHECK(g_dwGatewaySessionCreated == dwCreated + 1);
    size_t nRemoved = s_vecRemovedCaptcha.size();
    g_pBSNet->DisconnectSession(c.dwClient, 1);
    CHECK(Closed(c));
    std::string strLost = "Session Lost [";
    char sz[16];
    sprintf_s(sz, "%u]", c.dwServer);
    CHECK(WaitLog(strLost + sz));
    DWORD dwStart = GetTickCount();
    while (s_vecRemovedCaptcha.size() == nRemoved && GetTickCount() - dwStart < 3000)
        Sleep(5);
    CHECK(g_dwGatewaySessionLost == dwLost + 1);
    EnterCriticalSection(&s_cs);
    CHECK(s_vecRemovedCaptcha.size() == nRemoved + 1 && s_vecRemovedCaptcha.back() == c.dwServer);
    LeaveCriticalSection(&s_cs);
}

static void TestShutdown()
{
    CHECK(g_GatewayServer.InitMonitorCounters());
    CHECK(!g_GatewayServer.OnShutdown());
    CHECK(g_ShardConditionManager.empty());
    CHECK(g_DownloadServerList.list.empty()
          && g_DownloadServerList.itNext == g_DownloadServerList.list.end());
    CHECK(g_pModuleVersionList == NULL && s_db.nDestroyAll == 1);
    CHECK(SelectPatchServer_RoundRobin() == NULL);

    // GW-002: a shard without AgentServer ends the shard map build
    BodyOf(4)->pModule = CertModule_FindByName("SR_GameServer");
    CHECK(!ShardService_BuildMap());
    CHECK(WaitLog("there is unfurnished shard does not have any agent server : 11 - Beta",
                  0x2000000));
    CHECK(g_ShardConditionManager.size() == 1 && ShardMap_Find(12) == NULL);
    ShardServiceMap_DestroyAll();
    BodyOf(4)->pModule = CertModule_FindByName("AgentServer");

    // the log lines are engine messages: read them while the engine is there
    DrainLogs();
    CHECK(g_GatewayServer.Shutdown());
    CHECK(g_pBSNet == NULL && g_pServerBodyList == NULL);
    CHECK(s_db.nDestroyAll == 2);
    g_pSelfBody = NULL;
    g_pServerBodyStateObserver = NULL;
}

int main()
{
    BSDebugSetFlags(0, 0);
    InitializeCriticalSection(&s_cs);
    char szTemp[MAX_PATH];
    GetTempPathA(MAX_PATH, szTemp);
    char szDir[MAX_PATH];
    sprintf_s(szDir, "%sGatewayTest_%u", szTemp, GetCurrentProcessId());
    CreateDirectoryA(szDir, NULL);
    strcpy_s(g_ServerGlobals.szModuleDir, szDir);
    strcpy_s(g_ServerGlobals.szServiceName, "GatewayServer");
    g_ServerGlobals.hInstance = GetModuleHandleA(NULL);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    g_ServerGlobals.dwProcessorCount = si.dwNumberOfProcessors;
    g_ServerGlobals.nMaxSendQueDepth = -1;
    g_ServerGlobals.bReportConnectionLog = TRUE;
    g_pServerApp = &g_GatewayServer;

    // The UDP ping service binds every address of port 15851: kept on the loopback address.
    HookBindToLoopback();

    CIBUVProcess* pIBUV = new CIBUVProcess;
    CIBUVIPBlockMgr* pBlock = new CIBUVIPBlockMgr;
    CIBUVTemporalCertParamMgr* pTemporal = new CIBUVTemporalCertParamMgr;

    TestHelpers();

    LogCapture_Start();
    TestStartup();
    TestRoundRobin();

    CHECK(g_pBSNet->Listen("127.0.0.1", 0));
    sockaddr_in local;
    g_GatewayServer.m_NetEngine.m_pListener->GetLocalAddr(&local);
    s_wPort = ntohs(local.sin_port);
    CHECK(g_pBSNet->RegisterProactor(TEST_TASK_CLIENT, NULL, &CTestClient::classCTestClient,
                                     NULL));
    CHECK(g_pBSNet->StartProactorWorkers(TEST_TASK_CLIENT, 1, 0, 0, 0, NULL));

    TestVersionCheck();
    TestShardList();
    TestNotices();
    TestModuleVersionData();
    TestLoginResults();
    TestSessionEvents();
    TestShutdown();
    CloseHandle(g_hServerIOCP);
    g_hServerIOCP = NULL;

    delete pTemporal;
    delete pBlock;
    delete pIBUV;
    _fcloseall();
    RemoveDirectoryA(szDir);
    DeleteCriticalSection(&s_cs);
    return TEST_RESULT("GatewayTest");
}
