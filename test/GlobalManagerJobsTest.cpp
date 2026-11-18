// GlobalManager overlap jobs (GlobalManagerJobs, ServiceManagerJobs) and the SMC log. A real
// engine is connected to itself: the client task records what the jobs send to the GlobalManager
// side of the connection (answers, massive replies, relays). CDBQuery's execution is replaced
// (scripted results, rows through the trigger like the real one); the main process' setter and
// the punishment manager are stand-ins.

#include "LoopbackBind.h"

#include <winsock2.h>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>

#include "TestCheck.h"
#include "TestClock.h"

#include "GlobalManager/ContentUser.h"
#include "GlobalManager/GlobalManagerDB.h"
#include "GlobalManager/GlobalManagerJobs.h"
#include "GlobalManager/GlobalManagerMainProcess.h"
#include "GlobalManager/PunishmentManager.h"
#include "GlobalManager/GlobalManagerMainFwd.h"
#include "GlobalManager/GlobalManagerSvcFwd.h"
#include "GlobalManager/ServiceManagerJobs.h"
#include "GlobalManager/ServiceManagerProcess.h"
#include "BSLib/BSDebug.h"
#include "BSLib/DB/DBQuery.h"
#include "BSNet/NetEngine.h"
#include "BSNet/SockListener.h"
#include "BSNet/security/Security.h"
#include "ServerFramework/CertData.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerProcess.h"

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

// A record of n bytes: (seed + i) & 0xFF.
static Bytes Pattern(size_t n, BYTE bySeed)
{
    Bytes v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = (BYTE)(bySeed + i);
    return v;
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
    size_t i = 0;
    while (i < vGot.size() && i < vWant.size() && vGot[i] == vWant[i])
        ++i;
    printf("  differ at %u\n  got:  %s(%u)\n  want: %s(%u)\n", (unsigned)i, Hex(vGot).c_str(),
           (unsigned)vGot.size(), Hex(vWant).c_str(), (unsigned)vWant.size());
    return false;
}

// ---------------------------------------------------------------------------------------------
// Tasks: both record every message; the server task's job records the jobs sent to it.

enum
{
    TEST_TASK_SERVER    = 0x01000000,
    TEST_TASK_CLIENT    = 0x01000100,
};

struct PACKET
{
    DWORD   dwTask;
    DWORD   dwSession;
    WORD    wOpcode;
    Bytes   data;
};

static std::deque<PACKET> s_queIn;

class CTestTask : public CServerProcessBase
{
    BS_DECLARE_DYNCREATE(CTestTask)

public:
    virtual void InitMsgHandlers()
    {
    }

    virtual void OnUnhandledMsg(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
    {
        PACKET packet;
        packet.dwTask = (DWORD)this;
        packet.dwSession = pMsg->m_dwSessionID;
        packet.wOpcode = pMsg->GetOpcode();
        packet.data.assign(pMsg->m_pData + pMsg->m_wReadPos, pMsg->m_pData + pMsg->m_wWritePos);
        pMsg->SkipRemaining();
        EnterCriticalSection(&s_cs);
        s_queIn.push_back(packet);
        LeaveCriticalSection(&s_cs);
    }

    virtual bool SendOverlapJob(COverlapJob_Local* pJob, CTask* pTask)
    {
        m_vecJobs.push_back(pJob);
        m_vecJobTasks.push_back(pTask);
        return true;
    }

    std::vector<COverlapJob_Local*> m_vecJobs;
    std::vector<CTask*>             m_vecJobTasks;
};

BS_IMPLEMENT_DYNCREATE(CTestTask, &CServerProcessBase::classCServerProcessBase)

// g_pGlobalManagerProcess: the real main process, not started (its owner task is not set).
class CTestMainProcess : public CGlobalManagerMainProcess
{
};

static CNetEngine s_Engine;
static CTestTask* s_pMain;      // the server task's job
static CTask* s_pTask;          // the server task
static DWORD s_dwClient;        // client side of the connection
static DWORD s_dwServer;        // GlobalManager side
static const DWORD CLOSED_SESSION = 40;    // in the session table (50), never used

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

static bool NoPackets(DWORD dwWait = 200)
{
    Sleep(dwWait);
    EnterCriticalSection(&s_cs);
    bool b = s_queIn.empty();
    for (size_t i = 0; i < s_queIn.size(); ++i)
        printf("  unexpected packet 0x%04x\n", s_queIn[i].wOpcode);
    s_queIn.clear();
    LeaveCriticalSection(&s_cs);
    return b;
}

// The next packet with the opcode at the client.
static bool Packet(WORD wOpcode, const Bytes& want)
{
    Bytes got;
    if (!TakePacket(s_dwClient, wOpcode, &got))
        return false;
    return Same(got, want);
}

// The data of the next massive message with the inner opcode at the client.
static bool Massive(WORD wOpcode, const Bytes& want)
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
            if (packet.dwSession != s_dwClient || packet.wOpcode != MASSIVE_MSG_OPCODE
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
        }
        if (nHeader != s_queIn.size() && vecParts.size() == nParts)
        {
            Bytes got;
            for (size_t i = 0; i < vecParts.size(); ++i)
            {
                const Bytes& part = s_queIn[vecParts[i]].data;
                got.insert(got.end(), part.begin() + 1, part.end());
            }
            for (size_t i = vecParts.size(); i > 0; --i)
                s_queIn.erase(s_queIn.begin() + vecParts[i - 1]);
            s_queIn.erase(s_queIn.begin() + nHeader);
            LeaveCriticalSection(&s_cs);
            return Same(got, want);
        }
        LeaveCriticalSection(&s_cs);
        if (GetTickCount() - dwStart > 3000)
        {
            printf("  no massive 0x%04x\n", wOpcode);
            return false;
        }
        Sleep(5);
    }
}

// 0xA008 {1, relay ID, opcode, data} at the client.
static bool RelayReply(DWORD dwRelayID, WORD wOpcode, const Bytes& data)
{
    Bytes want;
    PutB(want, 1);
    PutD(want, dwRelayID);
    PutW(want, wOpcode);
    want.insert(want.end(), data.begin(), data.end());
    return Packet(SERVER_MSG_RELAY_ACK, want);
}

static CMsg* Req(const Bytes& data)
{
    CMsg* p = g_pBSNet->AllocMsg(FALSE);
    p->SetOpcode(0x1234);
    if (!data.empty())
        p->WriteBytes(&data[0], (WORD)data.size());
    p->m_dwSessionID = s_dwServer;
    return p;
}

// ---------------------------------------------------------------------------------------------
// Query double: ExecuteFormat formats like the original, then answers from the script (rows
// go through the trigger when there is one).

struct QUERY_ANSWER
{
    int                 nResult;
    std::vector<Bytes>  rows;
};

struct QUERY_CALL
{
    std::string     strSQL;
    CDBSessionMgr*  pSession;
    int             nSession;
};

static std::deque<QUERY_ANSWER> s_deqAnswer;
static std::vector<QUERY_CALL> s_vecCall;

static void Answer(int nResult, const Bytes& row = Bytes())
{
    QUERY_ANSWER a;
    a.nResult = nResult;
    if (!row.empty())
        a.rows.push_back(row);
    s_deqAnswer.push_back(a);
}

static void AnswerRows(int nResult, const std::vector<Bytes>& rows)
{
    QUERY_ANSWER a;
    a.nResult = nResult;
    a.rows = rows;
    s_deqAnswer.push_back(a);
}

int CDBQuery::Execute()
{
    QUERY_CALL c;
    c.strSQL = m_szQuery;
    c.pSession = m_pSession;
    c.nSession = m_nSession;
    s_vecCall.push_back(c);
    if (s_deqAnswer.empty())
    {
        printf("  unexpected query: %s\n", m_szQuery);
        ++g_nFailed;
        return -2;
    }
    QUERY_ANSWER a = s_deqAnswer.front();
    s_deqAnswer.pop_front();
    for (size_t i = 0; i < a.rows.size(); ++i)
    {
        if (m_pTrigger)
        {
            m_bRowFetched = 0;
            m_pTrigger->OnTrigger2((DWORD)this, 0);
        }
        CHECK((int)a.rows[i].size() <= m_nRowSize);
        memcpy(m_pBuffer, &a.rows[i][0], a.rows[i].size());
        if (m_pTrigger)
        {
            m_bRowFetched = 1;
            m_pTrigger->OnTrigger2((DWORD)this, 0);
        }
    }
    return a.nResult;
}

int CDBQuery::ExecuteFormat(const char* pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf_s(m_szQuery, sizeof(m_szQuery), sizeof(m_szQuery) - 1, pszFormat, args);
    va_end(args);
    return Execute();
}

static CDBSessionMgr* const DEFAULT_DB = (CDBSessionMgr*)0x1111;
static CDBSessionMgr* const ACCOUNT_DB = (CDBSessionMgr*)0x2222;
static CDBSessionMgr* const SILK_DB = (CDBSessionMgr*)0x3333;

static bool Query(const char* pszSQL, CDBSessionMgr* pSession, int nSession = 0)
{
    if (s_vecCall.empty())
    {
        printf("  no query (%s)\n", pszSQL);
        return false;
    }
    QUERY_CALL c = s_vecCall.front();
    s_vecCall.erase(s_vecCall.begin());
    bool b = c.strSQL == pszSQL && c.pSession == pSession && c.nSession == nSession;
    if (!b)
        printf("  query: %s (%p %d)\n  want:  %s\n", c.strSQL.c_str(), c.pSession, c.nSession,
               pszSQL);
    return b;
}

static bool NoQueries()
{
    bool b = s_vecCall.empty() && s_deqAnswer.empty();
    for (size_t i = 0; i < s_vecCall.size(); ++i)
        printf("  extra query: %s\n", s_vecCall[i].strSQL.c_str());
    s_vecCall.clear();
    s_deqAnswer.clear();
    return b;
}

// ---------------------------------------------------------------------------------------------
// Architecture: body 1 this GlobalManager, 2 a GatewayServer, 3 a DownloadServer (both
// reachable), 4 a GatewayServer without connection, 5 and 6 shard bodies of shard 10 (states 5
// and 3), 7 the shard manager of shard 11.

static CERT_MODULE s_aModule[4];
static SERVER_BODY s_aBody[7];
static SERVER_CORD s_CordUp;
static SERVER_CORD s_CordDown;
static CERT_SHARD s_aShard[2];
static ServerBodyList s_listBody;
static CertShardList s_listShard;

static void AddModule(int i, BYTE byID, const char* pszName, const char* pszLower)
{
    s_aModule[i].byID = byID;
    strcpy_s(s_aModule[i].szName, pszName);
    g_mapCertModuleByName[pszLower] = &s_aModule[i];
}

static void AddBody(int i, WORD wID, int nModule, int nState, SERVER_CORD* pRoute,
                    CERT_SHARD* pShard)
{
    SERVER_BODY& b = s_aBody[i];
    b.wID = wID;
    b.byModuleID = s_aModule[nModule].byID;
    b.pModule = &s_aModule[nModule];
    b.nState = nState;
    b.pShard = pShard;
    s_listBody.push_back(&b);
    g_mapServerBody[wID] = &b;
    if (pRoute)
        g_hmapServerRoute[wID] = pRoute;
}

static void SetupArchitecture()
{
    AddModule(0, 1, "GatewayServer", "gatewayserver");
    AddModule(1, 2, "DownloadServer", "downloadserver");
    AddModule(2, 3, "GlobalManager", "globalmanager");
    AddModule(3, 4, "ShardManager", "shardmanager");
    s_CordUp.dwSessionID = s_dwServer;
    s_CordDown.dwSessionID = 0;
    s_aShard[0].wID = 10;
    s_aShard[0].w548 = 1000;
    s_aShard[0].w557 = 250;
    s_aShard[0].wManagerID = 7;
    s_aShard[1].wID = 11;
    s_aShard[1].w548 = 200;
    s_aShard[1].w557 = 30;
    s_aShard[1].wManagerID = 7;
    AddBody(0, 1, 2, 5, NULL, NULL);
    AddBody(1, 2, 0, 5, &s_CordUp, NULL);
    AddBody(2, 3, 1, 5, &s_CordUp, NULL);
    AddBody(3, 4, 0, 4, &s_CordDown, NULL);
    AddBody(4, 5, 3, 5, &s_CordUp, &s_aShard[0]);
    AddBody(5, 6, 3, 3, &s_CordUp, &s_aShard[0]);
    AddBody(6, 7, 3, 4, &s_CordUp, &s_aShard[1]);
    g_pServerBodyList = &s_listBody;
    g_pSelfBody = &s_aBody[0];
    g_mapCertShard[10] = &s_aShard[0];
    g_mapCertShard[11] = &s_aShard[1];
    s_listShard.push_back(&s_aShard[0]);
    s_listShard.push_back(&s_aShard[1]);
    g_pCertShardList = &s_listShard;
}

// Runs the overlap side and checks the steps' results.
static bool Run(COverlapJob_Local* pJob, bool bPrepare, bool bExecute)
{
    bool bP = pJob->Prepare();
    bool bE = bP && pJob->Execute();
    if (bP != bPrepare || (bP && bE != bExecute))
    {
        printf("  Prepare %d Execute %d\n", bP, bE);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Main process jobs

static void TestCheckCharName()
{
    Bytes data;
    PutB(data, 3);
    PutB(data, 4);
    PutB(data, 5);
    PutS(data, "Hero");
    CMsg* pMsg = Req(data);
    COverlapJob_CheckCharName* pJob = OverlapJob_AllocCheckCharName();
    CHECK(pJob->m_dwID != 0);
    pJob->Setup(s_pTask, pMsg, s_dwServer, 77);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(pJob->m_pOwner == s_pTask && pJob->m_strName == "Hero" && pJob->m_dwRelayID == 77);
    Answer(0, Bytes(1, 0));
    CHECK(Run(pJob, true, true));
    CHECK(Query("exec _CheckCharName 3,'Hero',4,5", DEFAULT_DB));
    CHECK(pJob->OnComplete());
    CHECK(RelayReply(77, 0xA20B, Bytes(1, 1)));
    // A failed query is error 2.
    Answer(-2);
    CHECK(Run(pJob, true, true));
    CHECK(Query("exec _CheckCharName 3,'Hero',4,5", DEFAULT_DB));
    CHECK(pJob->OnComplete());
    Bytes want;
    PutB(want, 2);
    PutB(want, 2);
    CHECK(RelayReply(77, 0xA20B, want));
    CHECK(pJob->OnFail());
    // The session closed.
    pJob->m_dwSessionID = CLOSED_SESSION;
    CHECK(!pJob->Prepare());
    CHECK(!pJob->OnComplete());
    pJob->ReturnToPool();
    CHECK(g_CheckCharNameJobPool.m_nInUse == 0);
    CHECK(g_CheckCharNameJobPool.m_strName == "COverlapJob_CheckCharName");
    CHECK(NoQueries() && NoPackets());
}

static Bytes CashRow(DWORD dwID, DWORD dwJID, BYTE b10)
{
    Bytes v;
    PutD(v, dwID);
    PutD(v, dwJID);
    PutD(v, 0x11111111);
    PutD(v, 0x22222222);
    PutB(v, b10);
    PutB(v, 0x44);
    return v;
}

static void TestRefreshCommercialUserCash()
{
    // Users 100 (shard 10) and 101 (shard 12, unknown) are logged in.
    sContentUser user;
    user.dwIP = 0x0100000A;
    user.dwJID = 100;
    user.strID = "cash";
    user.wShardID = 10;
    user.wAgentBodyID = 5;
    user.PlayTime.Clear();
    CHECK(g_ContentUserManager.Register(&user) == 0);
    user.dwJID = 101;
    user.wShardID = 12;
    CHECK(g_ContentUserManager.Register(&user) == 0);

    COverlapJob_RefreshCommercialUser_Cash* pJob = OverlapJob_AllocRefreshCommercialUserCash();
    pJob->Init(s_pTask);
    std::vector<Bytes> rows;
    rows.push_back(CashRow(5, 100, 1));
    rows.push_back(CashRow(7, 999, 2));
    rows.push_back(CashRow(9, 101, 3));
    AnswerRows(0, rows);
    Answer(1);
    CHECK(Run(pJob, true, true));
    CHECK(Query("select * from SK_SilkChange_BY_Web", SILK_DB));
    CHECK(Query("delete SK_SilkChange_BY_Web where ID between 5 and 9", SILK_DB));
    CHECK(pJob->m_listRow.size() == 3 && g_CUCashChangedPool.m_nInUse == 3);
    CHECK(pJob->OnComplete());
    CHECK(pJob->m_listRow.empty() && g_CUCashChangedPool.m_nInUse == 0);
    // 0x6A00 {JID, DWORD, DWORD, BYTE, BYTE} relayed to the shard manager (body 7).
    Bytes got;
    CHECK(TakePacket(s_dwClient, SERVER_MSG_RELAY_REQ, &got));
    Bytes want;
    PutW(want, 7);
    PutW(want, 0x6A00);
    Bytes row = CashRow(5, 100, 1);
    want.insert(want.end(), row.begin() + 4, row.end());
    CHECK(got.size() > 4 && Same(Bytes(got.begin() + 4, got.end()), want));
    // Without rows nothing is deleted; a failed select is a failure.
    Answer(1);
    CHECK(Run(pJob, true, true));
    CHECK(Query("select * from SK_SilkChange_BY_Web", SILK_DB));
    Answer(-2);
    CHECK(Run(pJob, true, false));
    CHECK(Query("select * from SK_SilkChange_BY_Web", SILK_DB));
    // OnFail releases the rows.
    AnswerRows(0, rows);
    Answer(-2);
    CHECK(Run(pJob, true, false));
    CHECK(Query("select * from SK_SilkChange_BY_Web", SILK_DB));
    CHECK(Query("delete SK_SilkChange_BY_Web where ID between 5 and 9", SILK_DB));
    CHECK(g_CUCashChangedPool.m_nInUse == 3);
    CHECK(pJob->OnFail());
    CHECK(g_CUCashChangedPool.m_nInUse == 0 && pJob->m_listRow.empty());
    pJob->ReturnToPool();
    CHECK(g_RefreshCommercialUserCashJobPool.m_nInUse == 0);
    DWORD dwIP;
    CHECK(g_ContentUserManager.Unregister(100, &dwIP));
    CHECK(g_ContentUserManager.Unregister(101, &dwIP));
    CHECK(NoQueries() && NoPackets());
}

static void TestRefreshShardStatus()
{
    COverlapJob_RefreshShardStatus* pJob = OverlapJob_AllocRefreshShardStatus();
    pJob->Init(s_pTask);
    CHECK(pJob->m_listShard.size() == 2);
    CHECK(pJob->m_listShard.front().byStatus == 1 && pJob->m_listShard.front().byRatio == 25);
    CHECK(pJob->m_listShard.back().byStatus == 2 && pJob->m_listShard.back().byRatio == 15);
    Answer(1);
    Answer(1);
    CHECK(Run(pJob, true, true));
    CHECK(Query("update _Shard set nStatus = 1, nCurrentUserRatio = 25 where nID = 10",
                ACCOUNT_DB));
    CHECK(Query("update _Shard set nStatus = 2, nCurrentUserRatio = 15 where nID = 11",
                ACCOUNT_DB));
    CHECK(pJob->m_listShard.empty());
    // A failed update keeps the list; no body up is status 0.
    s_aBody[4].nState = 2;
    pJob->Init(s_pTask);
    CHECK(pJob->m_listShard.front().byStatus == 0);
    s_aBody[4].nState = 5;
    Answer(-2);
    CHECK(Run(pJob, true, false));
    CHECK(Query("update _Shard set nStatus = 0, nCurrentUserRatio = 25 where nID = 10",
                ACCOUNT_DB));
    CHECK(pJob->m_listShard.size() == 2);
    CHECK(pJob->OnComplete() && pJob->OnFail());
    pJob->ReturnToPool();
    CHECK(g_RefreshShardStatusJobPool.m_nInUse == 0);
    CHECK(NoQueries());
}

static void TestManageShardCharacterData()
{
    Bytes data;
    PutD(data, 55);
    PutS(data, "Old");
    PutS(data, "New");
    COverlapJob_ManageShardCharacterData* pJob = OverlapJob_AllocManageShardCharacterData();
    CMsg* pMsg = Req(data);
    CHECK(pJob->Setup(s_pTask, pMsg, 88, 0, 10));
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(pJob->m_dwSessionID == s_dwServer && pJob->m_strName2.empty());
    Answer(1);
    CHECK(Run(pJob, true, true));
    CHECK(Query("exec _ManageShardCharName 0, 55, 10, 'Old', ''", ACCOUNT_DB));
    CHECK(pJob->OnComplete());
    CHECK(RelayReply(88, 0xA20E, Bytes(1, 1)));
    pMsg = Req(data);
    CHECK(pJob->Setup(s_pTask, pMsg, 89, 2, 11));
    g_pBSNet->ReleaseMsg(pMsg);
    Answer(-2);
    CHECK(Run(pJob, true, false));
    CHECK(Query("exec _ManageShardCharName 2, 55, 11, 'Old', 'New'", ACCOUNT_DB));
    CHECK(pJob->OnFail());
    CHECK(RelayReply(89, 0xA210, Bytes(1, 2)));
    pJob->m_byType = 1;
    CHECK(pJob->OnComplete());
    CHECK(RelayReply(89, 0xA20F, Bytes(1, 1)));
    // Unknown type: no answer.
    pJob->m_byType = 3;
    CHECK(pJob->OnComplete());
    // Empty names and unknown types are refused.
    Bytes empty;
    PutD(empty, 55);
    PutS(empty, "");
    pMsg = Req(empty);
    CHECK(!pJob->Setup(s_pTask, pMsg, 90, 1, 10));
    g_pBSNet->ReleaseMsg(pMsg);
    pMsg = Req(data);
    CHECK(!pJob->Setup(s_pTask, pMsg, 90, 3, 10));
    g_pBSNet->ReleaseMsg(pMsg);
    pJob->ReturnToPool();
    CHECK(g_ManageShardCharacterDataJobPool.m_nInUse == 0);
    CHECK(NoQueries() && NoPackets());
}

static void TestRefreshShardUser()
{
    TIMESTAMP_STRUCT ts = {2011, 1, 24, 5, 6, 7, 0};
    std::string str;
    GM_FormatTimestamp(&str, &ts);
    CHECK(str == "2011-1-24 5:6:7");
    WaitClearOfMidnight(30);        // the date of today is checked in the queries below
    GM_GetLocalTimestamp(&ts);
    SYSTEMTIME st;
    GetLocalTime(&st);
    CHECK(ts.year == st.wYear && ts.fraction == 0);

    COverlapJob_RefreshShardUser* pJob = OverlapJob_AllocRefreshShardUser();
    pJob->Init(s_pTask);
    CHECK(pJob->m_listShard.size() == 2 && pJob->m_listShard.front().wUserCount == 250);
    Answer(1);
    Answer(1);
    CHECK(Run(pJob, true, true));
    CHECK(pJob->m_listShard.empty());
    char szDate[32];
    sprintf_s(szDate, "%d-%d-%d ", st.wYear, st.wMonth, st.wDay);
    for (int i = 0; i < 2; ++i)
    {
        CHECK(s_vecCall.size() > 0);
        if (s_vecCall.empty())
            break;
        std::string strWant = i ? "exec _AddCurrentUserCount 11, 30, '"
                                : "exec _AddCurrentUserCount 10, 250, '";
        const std::string& strSQL = s_vecCall[0].strSQL;
        CHECK(strSQL.compare(0, strWant.size(), strWant) == 0);
        CHECK(strSQL.find(szDate) == strWant.size() && strSQL[strSQL.size() - 1] == '\'');
        CHECK(s_vecCall[0].pSession == DEFAULT_DB && s_vecCall[0].nSession == 0);
        s_vecCall.erase(s_vecCall.begin());
    }
    pJob->Init(s_pTask);
    Answer(-1);
    CHECK(Run(pJob, true, false));
    CHECK(pJob->m_listShard.size() == 2);
    s_vecCall.clear();
    CHECK(pJob->OnComplete() && pJob->OnFail());
    pJob->ReturnToPool();
    CHECK(g_RefreshShardUserJobPool.m_nInUse == 0);
    CHECK(NoQueries());
}

static void TestResetPlayTime()
{
    COverlapJob_ResetPlayTimeOfGlobalServer* pJob = OverlapJob_AllocResetPlayTime();
    CHECK(pJob->Setup(0, 0));
    CHECK(pJob->m_pOwner == g_pGlobalManagerProcess->GetOwner());
    DWORD dwTime = 12345;
    Bytes row;
    PutD(row, dwTime);
    Answer(0, row);
    CHECK(Run(pJob, true, true));
    CHECK(Query("select * from _WriteOutResetPlayTime", ACCOUNT_DB, 3));
    g_pGlobalManagerProcess->m_dwLastPlayTimeReset = 0;
    CHECK(pJob->OnComplete());
    CHECK(g_pGlobalManagerProcess->m_dwLastPlayTimeReset == 12345);
    CHECK(pJob->Setup(1, 777));
    Answer(1);
    CHECK(Run(pJob, true, true));
    CHECK(Query("update _WriteOutResetPlayTime set LatestResetTime = 777", ACCOUNT_DB, 3));
    g_pGlobalManagerProcess->m_dwLastPlayTimeReset = 0;
    CHECK(pJob->OnComplete() && pJob->OnFail());
    CHECK(g_pGlobalManagerProcess->m_dwLastPlayTimeReset == 0);
    Answer(-2);
    CHECK(Run(pJob, true, false));
    s_vecCall.clear();
    CHECK(!pJob->Setup(2, 0));
    pJob->ReturnToPool();
    CHECK(g_ResetPlayTimeJobPool.m_nInUse == 0);
    CHECK(NoQueries());
}

static void TestCustomLogin()
{
    COverlapJob_CustomLoginJob* pJob = OverlapJob_AllocCustomLoginJob();
    pJob->Init(s_pTask);
    CHECK(pJob->m_dw20 == 0xFFFFFFFF);
    CHECK(Run(pJob, true, false));
    pJob->m_map[3] = 4;
    pJob->m_map[1] = 2;
    CHECK(Run(pJob, true, false));      // m_dw20 is -1
    pJob->m_dw20 = 9;
    Answer(-2);
    Answer(1);
    CHECK(Run(pJob, true, true));
    CHECK(Query("exec _AddLoginHistory 1,2,9", ACCOUNT_DB));
    CHECK(Query("exec _AddLoginHistory 3,4,9", ACCOUNT_DB));
    CHECK(pJob->m_map.empty());
    CHECK(pJob->OnComplete() && pJob->OnFail());
    pJob->ReturnToPool();
    CHECK(g_CustomLoginJobPool.m_nInUse == 0);
    CHECK(NoQueries());
}

// ---------------------------------------------------------------------------------------------
// Service manager jobs

static Bytes Rows(BYTE byStart, const std::vector<Bytes>& rows, BYTE byEnd = 0)
{
    Bytes v;
    PutB(v, byStart);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        PutB(v, 1);
        Put(v, &rows[i][0], rows[i].size());
    }
    PutB(v, byEnd);
    return v;
}

// (BYTE 1, row)..., byEnd
static Bytes RowsOnly(const std::vector<Bytes>& rows, BYTE byEnd = 0)
{
    Bytes v = Rows(0, rows, byEnd);
    v.erase(v.begin());
    return v;
}

static Bytes Cat(const Bytes& a, const Bytes& b)
{
    Bytes v = a;
    v.insert(v.end(), b.begin(), b.end());
    return v;
}

static void TestModuleVersionData()
{
    std::vector<Bytes> mods(1, Pattern(332, 1));
    std::vector<Bytes> files;
    files.push_back(Pattern(550, 2));
    files.push_back(Pattern(550, 3));
    SM_JOB* pJob = OverlapJob_AllocModuleVersionData();
    OverlapJob_SetupModuleVersionData(s_pTask, pJob, s_dwServer);
    CHECK(pJob->m_pOwner == s_pTask);
    AnswerRows(0, mods);
    AnswerRows(0, files);
    CHECK(Run(pJob, true, true));
    CHECK(Query("select * from _ModuleVersion with (NOLOCK) where nValid = 1", DEFAULT_DB));
    CHECK(Query("select * from _ModuleVersionFile with (NOLOCK) where nValid = 1", DEFAULT_DB));
    CHECK(pJob->OnComplete());
    CHECK(Massive(0xA306, Cat(Rows(1, mods), RowsOnly(files))));
    pJob->ReturnToPool();
    // A failed file query; OnFail frees the reply.
    pJob = OverlapJob_AllocModuleVersionData();
    OverlapJob_SetupModuleVersionData(s_pTask, pJob, s_dwServer);
    Answer(1);
    Answer(-2);
    CHECK(Run(pJob, true, false));
    CHECK(Query("select * from _ModuleVersion with (NOLOCK) where nValid = 1", DEFAULT_DB));
    CHECK(Query("select * from _ModuleVersionFile with (NOLOCK) where nValid = 1", DEFAULT_DB));
    CHECK(pJob->OnFail());
    // Closed session: Prepare frees the reply.
    OverlapJob_SetupModuleVersionData(s_pTask, pJob, CLOSED_SESSION);
    CHECK(Run(pJob, false, false));
    pJob->ReturnToPool();
    CHECK(g_ModuleVersionDataJobPool.m_nInUse == 0);
    CHECK(g_ModuleVersionDataJobPool.m_strName == "COverlapJob_ModuleVersionData");
    CHECK(NoQueries() && NoPackets());
}

static void TestSMModuleVersion()
{
    std::vector<Bytes> mods;
    mods.push_back(Pattern(332, 5));
    mods.push_back(Pattern(332, 6));
    SM_JOB* pJob = OverlapJob_AllocSM_ModuleVersion();
    OverlapJob_SetupSM_ModuleVersion(s_pTask, pJob, s_dwServer);
    AnswerRows(0, mods);
    CHECK(Run(pJob, true, true));
    CHECK(Query("select * from _ModuleVersion with (NOLOCK) where nValid = 1", DEFAULT_DB));
    CHECK(pJob->OnComplete());
    CHECK(Massive(0xB003, Rows(1, mods)));
    pJob->ReturnToPool();
    pJob = OverlapJob_AllocSM_ModuleVersion();
    OverlapJob_SetupSM_ModuleVersion(s_pTask, pJob, s_dwServer);
    Answer(-2);
    CHECK(Run(pJob, true, false));
    s_vecCall.clear();
    CHECK(pJob->OnFail());
    CHECK(Massive(0xB003, Bytes(1, 2)));
    pJob->ReturnToPool();
    CHECK(g_SMModuleVersionJobPool.m_nInUse == 0);

    std::vector<Bytes> files(1, Pattern(550, 7));
    pJob = OverlapJob_AllocSM_ModuleVersionFile();
    OverlapJob_SetupSM_ModuleVersionFile(s_pTask, pJob, s_dwServer, 4, 22);
    AnswerRows(0, files);
    CHECK(Run(pJob, true, true));
    CHECK(Query("select * from _ModuleVersionFile with (NOLOCK) where nModuleID = 4 and "
                "nContentID = 22 and nValid = 1", DEFAULT_DB));
    CHECK(pJob->OnComplete());
    CHECK(Massive(0xB004, Rows(1, files)));
    pJob->ReturnToPool();
    pJob = OverlapJob_AllocSM_ModuleVersionFile();
    OverlapJob_SetupSM_ModuleVersionFile(s_pTask, pJob, s_dwServer, 4, 22);
    Answer(-2);
    CHECK(Run(pJob, true, false));
    s_vecCall.clear();
    CHECK(pJob->OnFail());
    CHECK(Massive(0xB004, Bytes(1, 2)));
    pJob->ReturnToPool();
    CHECK(g_SMModuleVersionFileJobPool.m_nInUse == 0);
    CHECK(NoQueries() && NoPackets());
}

static void SetFile(MODULE_VERSION_FILE& f, DWORD dwVersion, const char* pszName,
                    const char* pszPath)
{
    memset(&f, 0, sizeof(f));
    BYTE* p = (BYTE*)&f;
    f.dwVersion = dwVersion;
    f.ab8[0] = 1;
    f.ab8[1] = 22;
    f.byModuleID = 4;
    strcpy_s(f.szName, pszName);
    strcpy_s(f.szPath, pszPath);
    *(DWORD*)(p + 0x20B) = 1000;
    p[0x20F] = 5;
    *(DWORD*)(p + 0x210) = 6;
    p[0x214] = 7;
    WORD aDate[6] = {2011, 1, 24, 3, 4, 5};
    memcpy(p + 0x215, aDate, sizeof(aDate));
}

static void TestRecordPatchData()
{
    MODULE_VERSION module;
    memset(&module, 0, sizeof(module));
    module.ab0[4] = 9;
    module.ab0[5] = 22;
    module.byModuleID = 4;
    module.dwVersion = 0x1234;
    strcpy_s((char*)&module + 11, 64, "1.0.1");
    strcpy_s((char*)&module + 75, 64, "desc");
    MODULE_VERSION_FILE aFile[3];
    SetFile(aFile[0], 101, "a.exe", "");
    SetFile(aFile[1], 102, "b.dat", "data");
    SetFile(aFile[2], 103, "c.dat", "data");
    ModuleVersionFileList files;
    for (int i = 0; i < 3; ++i)
        files.push_back(&aFile[i]);

    SM_JOB* pJob = OverlapJob_AllocRecordPatchData();
    OverlapJob_SetupRecordPatchData(s_pTask, pJob, &module, &files, s_dwServer);
    for (int i = 0; i < 8; ++i)
        Answer(1);
    CHECK(Run(pJob, true, true));
    const char* apszName[3] = {"a.exe", "b.dat", "c.dat"};
    const char* apszPath[3] = {".", "data", "data"};
    for (int i = 0; i < 3; ++i)
    {
        char sz[512];
        sprintf_s(sz, "update _ModuleVersionFile set nValid = 0 where nModuleID = 4 and "
                      "nContentID = 22 and szFilename = '%s' and szPath = '%s' and nValid = 1",
                  apszName[i], apszPath[i]);
        CHECK(Query(sz, DEFAULT_DB));
        sprintf_s(sz, "insert _ModuleVersionFile values( %d, 1, 22, 4, '%s', '%s', 1000, 5, 6, "
                      "7, '2011-01-24 03:04:05.000', 1)", 101 + i, apszName[i], apszPath[i]);
        CHECK(Query(sz, DEFAULT_DB));
    }
    CHECK(Query("update _ModuleVersion set nValid = 0 where nModuleID = 4 and nContentID = 22",
                DEFAULT_DB));
    CHECK(Query("insert _ModuleVersion values( 9, 22, 4, 4660, '1.0.1', 'desc', 1)",
                DEFAULT_DB));
    // Progress after 33, 66 and 100 percent; each message has the previous value.
    BYTE abPercent[3] = {0, 33, 66};
    for (int i = 0; i < 3; ++i)
    {
        Bytes want;
        PutB(want, 2);
        PutB(want, 2);
        PutB(want, abPercent[i]);
        CHECK(Packet(0x3101, want));
    }
    // The module data goes to the reachable GatewayServer and DownloadServer.
    s_pMain->m_vecJobs.clear();
    s_pMain->m_vecJobTasks.clear();
    CHECK(pJob->OnComplete());
    CHECK(Packet(0x3102, Bytes(1, 1)));
    CHECK(s_pMain->m_vecJobs.size() == 2);
    for (size_t i = 0; i < s_pMain->m_vecJobs.size(); ++i)
    {
        COverlapJob_ModuleVersionData* p = (COverlapJob_ModuleVersionData*)s_pMain->m_vecJobs[i];
        CHECK(p->m_dwSessionID == s_dwServer && p->m_pOwner == s_pTask);
        CHECK(s_pMain->m_vecJobTasks[i] == NULL);
        p->OnFail();
        p->ReturnToPool();
    }
    s_pMain->m_vecJobs.clear();
    s_pMain->m_vecJobTasks.clear();
    CHECK(pJob->OnFail());
    Bytes want;
    PutB(want, 2);
    PutB(want, 4);
    CHECK(Packet(0x3102, want));
    // A failed insert; a closed session is forgotten.
    Answer(1);
    Answer(-2);
    CHECK(Run(pJob, true, false));
    s_vecCall.clear();
    OverlapJob_SetupRecordPatchData(s_pTask, pJob, &module, &files, CLOSED_SESSION);
    CHECK(Run(pJob, true, false));
    CHECK(((COverlapJob_RecordPatchData*)pJob)->m_dwSessionID == 0);
    CHECK(pJob->OnFail());
    pJob->ReturnToPool();
    CHECK(g_RecordPatchDataJobPool.m_nInUse == 0);
    CHECK(NoQueries() && NoPackets());
}

static void TestRefreshSecurityData()
{
    Bytes setting = Pattern(324, 1);
    Bytes group = Pattern(289, 2);
    Bytes ip = Pattern(5, 3);
    SM_JOB* pJob = OverlapJob_AllocRefreshSecurityData();
    OverlapJob_SetupRefreshSecurityData(s_pTask, pJob, s_dwServer);
    Answer(0, setting);
    Answer(0, group);
    Answer(0, ip);
    CHECK(Run(pJob, true, true));
    CHECK(Query("select * from _SecurityDescription with (NOLOCK)", DEFAULT_DB));
    CHECK(Query("select * from _SecurityDescriptionGroup with (NOLOCK)", DEFAULT_DB));
    CHECK(Query("select * from _SecurityDescriptionGroupAssign with (NOLOCK)", DEFAULT_DB));
    CHECK(g_pCertDataSettingList && g_pCertDataSettingList->size() == 1);
    CHECK(g_pCertDataGroupList && g_pCertDataGroupList->size() == 1);
    CHECK(g_pCertDataGroupIPList && g_pCertDataGroupIPList->size() == 1);
    CHECK(pJob->OnComplete());
    Bytes want(1, 1);
    want = Cat(want, Rows(0, std::vector<Bytes>(1, group), 2));
    want = Cat(want, Rows(0, std::vector<Bytes>(1, setting), 2));
    want = Cat(want, Rows(0, std::vector<Bytes>(1, ip), 2));
    CHECK(Massive(0x3306, want));
    Answer(0);
    Answer(-2);
    CHECK(Run(pJob, true, false));
    s_vecCall.clear();
    CHECK(pJob->OnFail());
    CHECK(Massive(0x3306, Bytes(1, 2)));
    pJob->ReturnToPool();
    CHECK(g_RefreshSecurityDataJobPool.m_nInUse == 0);
    CHECK(NoQueries() && NoPackets());
}

// A single-query SMC job: rows, then a failure. The reply has nBefore (the data written before
// the rows, after BYTE 1).
static void CheckReplyJob(SM_JOB* pJob, SM_JOB* pFailJob, WORD wOpcode, const char* pszSQL,
                          CDBSessionMgr* pSession, size_t nRow, const Bytes& failWant)
{
    std::vector<Bytes> rows;
    rows.push_back(Pattern(nRow, 9));
    rows.push_back(Pattern(nRow, 10));
    AnswerRows(0, rows);
    CHECK(Run(pJob, true, true));
    CHECK(Query(pszSQL, pSession));
    CHECK(pJob->OnComplete());
    CHECK(Massive(wOpcode, Rows(1, rows)));
    pJob->ReturnToPool();
    Answer(-2);
    CHECK(Run(pFailJob, true, false));
    CHECK(Query(pszSQL, pSession));
    CHECK(pFailJob->OnFail());
    CHECK(Massive(wOpcode, failWant));
    pFailJob->ReturnToPool();
}

static void TestSMQueries()
{
    Bytes fail1;
    PutB(fail1, 2);
    PutB(fail1, 1);
    SM_JOB* pJob = OverlapJob_AllocQueryUser();
    OverlapJob_SetupQueryUser(s_pTask, pJob, "admin", s_dwServer);
    SM_JOB* pFail = OverlapJob_AllocQueryUser();
    OverlapJob_SetupQueryUser(s_pTask, pFail, "admin", s_dwServer);
    CheckReplyJob(pJob, pFail, 0xB400, "exec _QueryUser 'admin'", DEFAULT_DB, 0x206, fail1);
    CHECK(g_QueryUserJobPool.m_nInUse == 0);

    std::string strID("op");
    pJob = OverlapJob_AllocQuerySMLog();
    OverlapJob_SetupQuerySMLog(s_pTask, pJob, "2011-01-01", "2011-01-02", s_dwServer, &strID, 3);
    pFail = OverlapJob_AllocQuerySMLog();
    OverlapJob_SetupQuerySMLog(s_pTask, pFail, "2011-01-01", "2011-01-02", s_dwServer, &strID,
                               3);
    CheckReplyJob(pJob, pFail, 0xB404, "exec _GetSMLog 3, 'op', '2011-01-01', '2011-01-02'",
                  DEFAULT_DB, 401, Bytes(1, 2));
    CHECK(g_QuerySMLogJobPool.m_nInUse == 0);

    pJob = OverlapJob_AllocQueryContentUserAccount();
    OverlapJob_SetupQueryContentUserAccount(s_pTask, pJob, "ab*", s_dwServer, 1);
    pFail = OverlapJob_AllocQueryContentUserAccount();
    OverlapJob_SetupQueryContentUserAccount(s_pTask, pFail, "ab*", s_dwServer, 1);
    CheckReplyJob(pJob, pFail, 0xB406, "exec _ViewContentUser 1,'ab*'", ACCOUNT_DB, 165,
                  Bytes(1, 2));
    CHECK(g_QueryContentUserAccountJobPool.m_nInUse == 0);

    pJob = OverlapJob_AllocQueryCharOwnedUser();
    OverlapJob_SetupQueryCharOwnedUser(s_pTask, pJob, "Hero", s_dwServer);
    pFail = OverlapJob_AllocQueryCharOwnedUser();
    OverlapJob_SetupQueryCharOwnedUser(s_pTask, pFail, "Hero", s_dwServer);
    CheckReplyJob(pJob, pFail, 0xB409, "exec _QueryCharOwnedUser 'Hero'", ACCOUNT_DB, 194,
                  Bytes(1, 2));
    CHECK(g_QueryCharOwnedUserJobPool.m_nInUse == 0);

    pJob = OverlapJob_AllocQueryUserByUserID();
    OverlapJob_SetupQueryUserByUserID(s_pTask, pJob, "admin", s_dwServer);
    pFail = OverlapJob_AllocQueryUserByUserID();
    OverlapJob_SetupQueryUserByUserID(s_pTask, pFail, "admin", s_dwServer);
    CheckReplyJob(pJob, pFail, 0xB409, "exec _QueryUserByUserID 'admin'", ACCOUNT_DB, 194,
                  Bytes(1, 2));
    CHECK(g_QueryUserByUserIDJobPool.m_nInUse == 0);

    // Concurrent user log: the shard first, a failure ends the reply with 0.
    std::string strBegin("2011"), strEnd("2012");
    pJob = OverlapJob_AllocConcurrentUserLogJob();
    OverlapJob_SetupConcurrentUserLogJob(s_pTask, pJob, s_dwServer, 10, 1, &strBegin, &strEnd);
    std::vector<Bytes> rows(1, Pattern(38, 4));
    AnswerRows(0, rows);
    CHECK(Run(pJob, true, true));
    CHECK(Query("exec _GetConcurrentUserLog 10, 1, '2011', '2012' ", DEFAULT_DB));
    CHECK(pJob->OnComplete());
    Bytes want;
    PutW(want, 10);
    CHECK(Massive(0xB412, Cat(want, RowsOnly(rows))));
    OverlapJob_SetupConcurrentUserLogJob(s_pTask, pJob, s_dwServer, 10, 1, &strBegin, &strEnd);
    Answer(-2);
    CHECK(Run(pJob, true, false));
    s_vecCall.clear();
    CHECK(pJob->OnFail());
    PutB(want, 0);
    CHECK(Massive(0xB412, want));
    pJob->ReturnToPool();
    CHECK(g_ConcurrentUserLogJobPool.m_nInUse == 0);
    CHECK(NoQueries() && NoPackets());
}

static void TestQueryPunishment()
{
    SM_JOB* pJob = OverlapJob_AllocQueryPunishment();
    OverlapJob_SetupQueryPunishment(s_pTask, pJob, "bad", s_dwServer);
    Bytes jid;
    PutD(jid, 42);
    Bytes info(128, 0);
    memcpy(&info[0], "Name", 4);
    memcpy(&info[0x14], "Second", 6);
    memcpy(&info[0x2A], "Third", 5);
    memcpy(&info[0x3A], "Fourth", 6);
    std::vector<Bytes> rows(1, Pattern(2124, 8));
    Answer(0, jid);
    Answer(0, info);
    AnswerRows(0, rows);
    CHECK(Run(pJob, true, true));
    CHECK(Query("exec _GetUserJID 'bad'", ACCOUNT_DB));
    CHECK(Query("exec _GetUserInfo 42", ACCOUNT_DB));
    CHECK(Query("select * from _Punishment where UserJID = 42", ACCOUNT_DB));
    CHECK(pJob->OnComplete());
    Bytes want(1, 1);
    PutD(want, 42);
    PutS(want, "Name");
    PutS(want, "Second");
    PutS(want, "Second");
    PutS(want, "Third");
    PutS(want, "Fourth");
    CHECK(Massive(0xB407, Cat(want, RowsOnly(rows))));
    // JID -1: no further query.
    OverlapJob_SetupQueryPunishment(s_pTask, pJob, "bad", s_dwServer);
    Bytes none;
    PutD(none, 0xFFFFFFFF);
    Answer(0, none);
    CHECK(Run(pJob, true, false));
    CHECK(Query("exec _GetUserJID 'bad'", ACCOUNT_DB));
    CHECK(pJob->OnFail());
    CHECK(Massive(0xB407, Bytes(1, 2)));
    // A failed user info query.
    OverlapJob_SetupQueryPunishment(s_pTask, pJob, "bad", s_dwServer);
    Answer(0, jid);
    Answer(-3);
    CHECK(Run(pJob, true, false));
    CHECK(Query("exec _GetUserJID 'bad'", ACCOUNT_DB));
    CHECK(Query("exec _GetUserInfo 42", ACCOUNT_DB));
    pJob->ReturnToPool();
    CHECK(g_QueryPunishmentJobPool.m_nInUse == 0);
    CHECK(NoQueries() && NoPackets());
}

static SYSTEMTIME Date(WORD wYear, WORD wSecond)
{
    TIMESTAMP_STRUCT ts = {wYear, 2, 3, 4, 5, wSecond, 0};
    SYSTEMTIME st;
    memcpy(&st, &ts, sizeof(st));
    return st;
}

static void TestPunishmentJob()
{
    const char* pszRegister = "EXEC _RegisterPunishment 100,3,'s1',10,'s2','s3','s4','s5','s6',"
                              "'2011-02-03 04:05:00','2012-02-03 04:05:01',"
                              "'2013-02-03 04:05:02','2014-02-03 04:05:03'";
    const char* pszUpdate = "EXEC _UpdatePunishment 7,100,3,'s1', 10, 's2', 's3', 's4', 's5', "
                            "'s6', '2011-02-03 04:05:00', '2012-02-03 04:05:01', "
                            "'2013-02-03 04:05:02', '2014-02-03 04:05:03'";
    SM_JOB* pJob = OverlapJob_AllocPunishmentJob();
    COverlapJob_PunishmentJob* p = (COverlapJob_PunishmentJob*)pJob;
    struct CASE
    {
        BYTE    byJob;
        int     nQuery;
        int     nRow;
        bool    bOK;
        BYTE    byError;
    };
    const CASE aCase[] = {
        {0, 0, 0, true, 0},
        {0, 0, -1, false, 2},
        {0, 0, -5, false, 1},
        {0, -2, 0, false, 1},
        {1, 0, 3, true, 0},
        {1, 0, -1, false, 3},
        {1, 0, -7, false, 1},
        {1, -2, 0, false, 1},
        {2, 0, 0, false, 4},
    };
    for (size_t i = 0; i < sizeof(aCase) / sizeof(aCase[0]); ++i)
    {
        const CASE& c = aCase[i];
        OverlapJob_SetupPunishmentJob(s_pTask, pJob, "s1", "s2", s_dwServer, c.byJob, 7, 100, 3,
                                      10, "s3", "s4", "s5", "s6", Date(2011, 0), Date(2012, 1),
                                      Date(2013, 2), Date(2014, 3));
        p->m_byError = 0;
        if (c.byJob < 2)
        {
            Bytes row;
            PutD(row, (DWORD)c.nRow);
            Answer(c.nQuery, row);
        }
        CHECK(Run(pJob, true, c.bOK));
        CHECK(p->m_byError == c.byError);
        if (c.byJob < 2)
            CHECK(Query(c.byJob ? pszUpdate : pszRegister, ACCOUNT_DB));
        if (c.bOK)
        {
            CHECK(pJob->OnComplete());
            Bytes want;
            PutB(want, 1);
            PutB(want, c.byJob);
            CHECK(Packet(0xB408, want));
            // The punishment manager got the second and third date.
            PunishmentMap& map = g_PunishmentManager.m_mapPunishment;
            CHECK(map.size() == 1 && map.begin()->first == 100);
            Punishment_Info_Internal* pInfo = map.begin()->second;
            SYSTEMTIME stBegin, stEnd;
            FileTimeToSystemTime(&pInfo->ftBegin, &stBegin);
            FileTimeToSystemTime(&pInfo->ftEnd, &stEnd);
            CHECK(pInfo->byType == 3 && stBegin.wYear == 2012 && stBegin.wSecond == 1
                  && stEnd.wYear == 2013 && stEnd.wSecond == 2);
            pInfo->Release();
            map.clear();
        }
        else
        {
            CHECK(pJob->OnFail());
            Bytes want;
            PutB(want, 2);
            PutB(want, c.byJob);
            PutB(want, c.byError);
            CHECK(Packet(0xB408, want));
        }
    }
    pJob->ReturnToPool();
    CHECK(g_PunishmentJobPool.m_nInUse == 0);

    std::string strID("victim"), str2("a"), str3("b"), str4("c");
    pJob = OverlapJob_AllocAutomatedPunishmentJob();
    COverlapJob_AutomatedPunishmentJob* pAuto = (COverlapJob_AutomatedPunishmentJob*)pJob;
    OverlapJob_SetupAutomatedPunishmentJob(s_pTask, pJob, &strID, &str2, &str3, &str4, 9);
    CHECK(pAuto->m_nResult == 0 && pAuto->m_strUserID == "victim" && pAuto->m_dwParam == 9);
    const char* pszAuto = "EXEC _RegisterAutomatedPunishment 'victim',1,'a','b','c',9";
    Bytes row;
    PutD(row, 5);
    Answer(0, row);
    CHECK(Run(pJob, true, true));
    CHECK(Query(pszAuto, ACCOUNT_DB));
    CHECK(pAuto->m_nResult == 5);
    row.clear();
    PutD(row, (DWORD)-3);
    Answer(0, row);
    CHECK(Run(pJob, true, false));
    CHECK(Query(pszAuto, ACCOUNT_DB));
    CHECK(pAuto->m_nResult == 5);
    Answer(-2);
    CHECK(Run(pJob, true, false));
    CHECK(Query(pszAuto, ACCOUNT_DB));
    CHECK(pJob->OnComplete() && pJob->OnFail());
    pJob->ReturnToPool();
    CHECK(g_AutomatedPunishmentJobPool.m_nInUse == 0);
    CHECK(NoQueries() && NoPackets());
}

static void TestPreparePatch()
{
    std::vector<Bytes> mods(1, Pattern(332, 11));
    std::vector<Bytes> files;
    files.push_back(Pattern(550, 12));
    files.push_back(Pattern(550, 13));
    const char* pszModule = "select * from _ModuleVersion with(NOLOCK) where nModuleID = 4 "
                            "and nContentID = 22 and nValid = 1";
    const char* pszFile = "select * from _ModuleVersionFile with(NOLOCK) where nModuleID = 4 "
                          "and nContentID = 22 and nValid = 1";
    SM_JOB* pJob = OverlapJob_AllocPreparePatch();
    OverlapJob_SetupPreparePatch(s_pTask, pJob, s_dwServer, 4, 22);
    AnswerRows(0, mods);
    AnswerRows(0, files);
    CHECK(Run(pJob, true, true));
    CHECK(Query(pszModule, DEFAULT_DB));
    CHECK(Query(pszFile, DEFAULT_DB));
    CHECK(pJob->OnComplete());
    CHECK(Massive(0xB104, Cat(Rows(1, mods), RowsOnly(files))));
    // No version row, a failed file query.
    OverlapJob_SetupPreparePatch(s_pTask, pJob, s_dwServer, 4, 22);
    Answer(1);
    CHECK(Run(pJob, true, false));
    CHECK(Query(pszModule, DEFAULT_DB));
    CHECK(pJob->OnFail());
    Bytes want;
    PutB(want, 2);
    PutB(want, 3);
    CHECK(Massive(0xB104, want));
    OverlapJob_SetupPreparePatch(s_pTask, pJob, s_dwServer, 4, 22);
    AnswerRows(0, mods);
    Answer(-2);
    CHECK(Run(pJob, true, false));
    CHECK(Query(pszModule, DEFAULT_DB));
    CHECK(Query(pszFile, DEFAULT_DB));
    CHECK(pJob->OnFail());
    CHECK(Massive(0xB104, want));
    // A closed session.
    OverlapJob_SetupPreparePatch(s_pTask, pJob, CLOSED_SESSION, 4, 22);
    CHECK(!pJob->Prepare());
    ((COverlapJob_PreparePatch*)pJob)->m_pReply->Free();
    pJob->ReturnToPool();
    CHECK(g_PreparePatchJobPool.m_nInUse == 0);
    CHECK(NoQueries() && NoPackets());
}

static void TestArticleJob()
{
    Bytes row(1125, 0);
    *(DWORD*)&row[0] = 5;
    row[4] = 1;
    memcpy(&row[5], "Hi", 2);
    memcpy(&row[85], "Body", 4);
    Bytes date = Pattern(16, 20);
    memcpy(&row[1109], &date[0], 16);
    SM_JOB* pJob = OverlapJob_AllocArticleJob();
    OverlapJob_SetupArticleJob(s_pTask, pJob, 0, NULL, s_dwServer, 22, 0, NULL);
    Answer(0, row);
    CHECK(Run(pJob, true, true));
    CHECK(Query("select * from _Notice where ContentID = 22 order by EditDate desc",
                DEFAULT_DB));
    CHECK(pJob->OnComplete());
    Bytes want;
    PutB(want, 1);
    PutB(want, 1);
    PutD(want, 5);
    PutB(want, 1);
    PutS(want, "Hi");
    PutS(want, "Body");
    Put(want, &date[0], 16);
    PutB(want, 0);
    CHECK(Massive(0xB206, want));
    OverlapJob_SetupArticleJob(s_pTask, pJob, 0, NULL, s_dwServer, 22, 0, NULL);
    Answer(-2);
    CHECK(Run(pJob, true, false));
    s_vecCall.clear();
    CHECK(pJob->OnFail());
    CHECK(Massive(0xB206, Bytes(1, 2)));
    // Add (quotes become blanks), delete, edit.
    OverlapJob_SetupArticleJob(s_pTask, pJob, 1, "it's", s_dwServer, 22, 0, "say \"x\"");
    Answer(1);
    CHECK(Run(pJob, true, true));
    CHECK(Query("exec _AddArticleData 22,'it s','say  x '", DEFAULT_DB));
    CHECK(pJob->OnComplete());
    CHECK(Packet(0xB207, Bytes(1, 1)));
    OverlapJob_SetupArticleJob(s_pTask, pJob, 2, NULL, s_dwServer, 0, 77, NULL);
    Answer(-2);
    CHECK(Run(pJob, true, false));
    CHECK(Query("exec _DeleteArticleData 77", DEFAULT_DB));
    CHECK(pJob->OnFail());
    CHECK(Packet(0xB208, Bytes(1, 2)));
    OverlapJob_SetupArticleJob(s_pTask, pJob, 3, "T", s_dwServer, 0, 77, "X");
    Answer(1);
    CHECK(Run(pJob, true, true));
    CHECK(Query("exec _UpdateArticleData 77,'T','X'", DEFAULT_DB));
    CHECK(pJob->OnComplete());
    CHECK(Packet(0xB209, Bytes(1, 1)));
    // Unknown type: no query.
    OverlapJob_SetupArticleJob(s_pTask, pJob, 4, NULL, s_dwServer, 0, 77, NULL);
    CHECK(Run(pJob, true, true));
    pJob->ReturnToPool();
    CHECK(g_ArticleJobPool.m_nInUse == 0);
    CHECK(NoQueries() && NoPackets());
}

static void TestSMLog()
{
    CHECK(SMLog_GetAnswerType(0x8001) == 0x17 && SMLog_GetAnswerType(0x8004) == 0x14);
    CHECK(SMLog_GetAnswerType(0x8007) == 0x16 && SMLog_GetAnswerType(0x800A) == 0x10);
    CHECK(SMLog_GetAnswerType(0x8010) == 0x15 && SMLog_GetAnswerType(0x8003) == 0xFF);
    CHECK(SMLog_GetAnswerType(0x8000) == 0xFF && SMLog_GetAnswerType(0x8011) == 0xFF);

    CServiceManagerProcess* pSM = new CServiceManagerProcess;
    pSM->Init(s_pTask);
    sServiceManagerUser* pUser = ServiceManagerUserPool_Alloc();
    pUser->strID = "op";
    pUser->dwSessionID = s_dwServer;
    pSM->m_mapUser[s_dwServer] = pUser;
    s_pMain->m_vecJobs.clear();
    s_pMain->m_vecJobTasks.clear();
    pSM->WriteLog(s_dwServer, 11, "Add Article %s", "x");
    pSM->WriteQueryLog(s_dwServer, "Query %d", 5);
    pSM->WriteLog(12345, 11, "nobody");
    std::string strText("text"), strUser("someone");
    pSM->WriteUserLog(&strText, &strUser);
    CHECK(s_pMain->m_vecJobs.size() == 3);
    const char* apszSQL[3] = {
        "insert _SMCLog values ('op', 11, 'Add Article x', default)",
        "insert _SMCLog values ('op', 255, 'Query 5', default)",
        "insert _SMCLog values ('someone', 1, 'text', default)",
    };
    for (size_t i = 0; i < s_pMain->m_vecJobs.size() && i < 3; ++i)
    {
        COverlapJob_Local* pJob = s_pMain->m_vecJobs[i];
        CHECK(s_pMain->m_vecJobTasks[i] == NULL && pJob->m_pOwner == s_pTask);
        Answer(-2);
        CHECK(Run(pJob, true, true));
        CHECK(Query(apszSQL[i], DEFAULT_DB));
        CHECK(pJob->OnComplete() && pJob->OnFail());
        pJob->ReturnToPool();
    }
    CHECK(g_SMLogJobPool.m_nInUse == 0);
    // The service manager posts the real jobs.
    s_pMain->m_vecJobs.clear();
    pSM->RefreshSecurityData(s_dwServer);
    CHECK(s_pMain->m_vecJobs.size() == 1);
    if (s_pMain->m_vecJobs.size() == 1)
    {
        COverlapJob_RefreshSecurityData_n_SendToSM* pJob =
            (COverlapJob_RefreshSecurityData_n_SendToSM*)s_pMain->m_vecJobs[0];
        CHECK(pJob->m_dwSessionID == s_dwServer && pJob->m_pOwner == s_pTask);
        pJob->ReturnToPool();
    }
    std::string str2("a"), str3("b"), str4("c");
    CHECK(pSM->RegisterAutomatedPunishment(&strUser, &str2, &str3, &str4, 9));
    CHECK(s_pMain->m_vecJobs.size() == 2);
    if (s_pMain->m_vecJobs.size() == 2)
    {
        COverlapJob_AutomatedPunishmentJob* pJob =
            (COverlapJob_AutomatedPunishmentJob*)s_pMain->m_vecJobs[1];
        CHECK(pJob->m_strUserID == "someone" && pJob->m_str4 == "c" && pJob->m_dwParam == 9);
        pJob->ReturnToPool();
    }
    s_pMain->m_vecJobs.clear();
    s_pMain->m_vecJobTasks.clear();
    delete pSM;
    CHECK(NoQueries() && NoPackets());
}

// ---------------------------------------------------------------------------------------------

static void Startup()
{
    BSNET_INIT_PARAM param;
    param.dwFlags = BSNET_FLAG_SERVER;
    param.nAcceptCount = 4;
    param.aSocket[0].dwSessionParam = TEST_TASK_SERVER;
    param.aSocket[1].dwSessionParam = TEST_TASK_CLIENT;
    CHECK(s_Engine.Initialize(param));
    g_pBSNet = &s_Engine;
    CHECK(s_Engine.StartProactorWorkers(NETENGINE_TASK_LISTEN, 1, 0, 0, 0, NULL));
    CHECK(s_Engine.StartProactorWorkers(NETENGINE_TASK_IO, 1, 0, 0, 0, NULL));
    CHECK(s_Engine.RegisterProactor(TEST_TASK_SERVER, NULL, &CTestTask::classCTestTask, NULL));
    CHECK(s_Engine.RegisterProactor(TEST_TASK_CLIENT, NULL, &CTestTask::classCTestTask, NULL));
    CHECK(s_Engine.StartProactorWorkers(TEST_TASK_SERVER, 1, 0, 0, 0, NULL));
    CHECK(s_Engine.StartProactorWorkers(TEST_TASK_CLIENT, 1, 0, 0, 0, NULL));
    s_pTask = s_Engine.GetProactor(TEST_TASK_SERVER);
    s_pMain = (CTestTask*)s_pTask->GetJob();
    CHECK(s_Engine.Listen("127.0.0.1", 0));
    sockaddr_in local;
    s_Engine.m_pListener->GetLocalAddr(&local);
    s_dwClient = s_Engine.Connect("127.0.0.1", ntohs(local.sin_port), NULL, TEST_TASK_CLIENT, 0);
    CHECK(s_dwClient != 0);
    // The server task gets 0x1001 {session, role} for the accepted session.
    DWORD dwStart = GetTickCount();
    while (GetTickCount() - dwStart < 3000 && !s_dwServer)
    {
        EnterCriticalSection(&s_cs);
        for (std::deque<PACKET>::iterator it = s_queIn.begin(); it != s_queIn.end(); ++it)
        {
            if (it->dwTask == (DWORD)s_pMain && it->wOpcode == 0x1001 && it->data.size() >= 4)
                s_dwServer = *(const DWORD*)&it->data[0];
        }
        LeaveCriticalSection(&s_cs);
        Sleep(5);
    }
    CHECK(s_dwServer != 0 && s_dwServer != s_dwClient);
    SECURITY_HANDSHAKE_PARAM hs;
    memset(&hs, 0, sizeof(hs));
    hs.bOptions = 6;
    hs.dwCountSeed = 7;
    hs.dwCrcSeed = 9;
    for (int i = 0; i < 8; ++i)
        hs.BlowFishKey[i] = (BYTE)(i + 1);
    CHECK(s_Engine.StartHandshake(s_dwServer, &hs));
    CHECK(TakePacket(s_dwClient, 0x1003, NULL));
    Sleep(300);
    EnterCriticalSection(&s_cs);
    s_queIn.clear();
    LeaveCriticalSection(&s_cs);

    SetupArchitecture();
    g_pDefaultDBSession = DEFAULT_DB;
    g_pAccountDBSession = ACCOUNT_DB;
    g_pSilkDBSession = SILK_DB;
    g_pGlobalManagerProcess = new CTestMainProcess;
}

int main()
{
    CHECK(LoopbackBind_Install() == 1);
    setvbuf(stdout, NULL, _IONBF, 0);
    BSDebugSetFlags(0, 0);
    InitializeCriticalSection(&s_cs);

    Startup();
    TestCheckCharName();
    TestRefreshCommercialUserCash();
    TestRefreshShardStatus();
    TestManageShardCharacterData();
    TestRefreshShardUser();
    TestResetPlayTime();
    TestCustomLogin();
    TestModuleVersionData();
    TestSMModuleVersion();
    TestRecordPatchData();
    TestRefreshSecurityData();
    TestSMQueries();
    TestQueryPunishment();
    TestPunishmentJob();
    TestPreparePatch();
    TestArticleJob();
    TestSMLog();

    s_Engine.Shutdown();
    g_pBSNet = NULL;
    DeleteCriticalSection(&s_cs);
    return TEST_RESULT("GlobalManagerJobsTest");
}
