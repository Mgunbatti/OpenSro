// GlobalManager punishment manager and user validation history (CPunishmentManager,
// CUserValidationHistoryMgr). A real engine is connected to itself: the client task records what
// is relayed to the shard managers. CDBQuery's execution is replaced (scripted results, rows
// through the trigger like the real one); the automated punishments go through the real service
// manager to its overlap jobs.

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

#include "GlobalManager/ContentUser.h"
#include "GlobalManager/GlobalManagerDB.h"
#include "GlobalManager/GlobalManagerJobs.h"
#include "GlobalManager/GlobalManagerMainFwd.h"
#include "GlobalManager/GlobalManagerSvcFwd.h"
#include "GlobalManager/ServiceManagerJobs.h"
#include "GlobalManager/ServiceManagerProcess.h"
#include "GlobalManager/PunishmentManager.h"
#include "GlobalManager/UserValidationHistory.h"
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

// Stands in for g_pGlobalManagerProcess (its owner task is NULL).
class CTestMainProcess : public CServerProcessMain
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
// Stand-in for the main process' setter (GlobalManagerJobs.obj calls it).

void GlobalManagerMain_SetLastPlayTimeReset(DWORD)
{
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

// ---------------------------------------------------------------------------------------------
// Helpers

// The data of the next relay (0x6008) at the client: target body, opcode, data.
static bool Relayed(WORD wTarget, WORD wOpcode, const Bytes& data)
{
    Bytes got;
    if (!TakePacket(s_dwClient, SERVER_MSG_RELAY_REQ, &got))
        return false;
    Bytes want;
    PutW(want, wTarget);
    PutW(want, wOpcode);
    want.insert(want.end(), data.begin(), data.end());
    return got.size() > 4 && Same(Bytes(got.begin() + 4, got.end()), want);
}

// The local time shifted by nSeconds as a timestamp (fraction 0).
static TIMESTAMP_STRUCT LocalStamp(int nSeconds)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart += (LONGLONG)nSeconds * 10000000;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    FileTimeToSystemTime(&ft, &st);
    TIMESTAMP_STRUCT ts;
    ts.year = st.wYear;
    ts.month = st.wMonth;
    ts.day = st.wDay;
    ts.hour = st.wHour;
    ts.minute = st.wMinute;
    ts.second = st.wSecond;
    ts.fraction = 0;
    return ts;
}

static Bytes PunishmentRow(DWORD dwJID, BYTE byType, const TIMESTAMP_STRUCT& tsBegin,
                           const TIMESTAMP_STRUCT& tsEnd)
{
    Bytes v;
    PutD(v, dwJID);
    PutB(v, byType);
    Put(v, &tsBegin, sizeof(tsBegin));
    Put(v, &tsEnd, sizeof(tsEnd));
    return v;
}

// 0x6317 {1, JID, type}
static Bytes Ended(DWORD dwJID, BYTE byType)
{
    Bytes v;
    PutB(v, 1);
    PutD(v, dwJID);
    PutB(v, byType);
    return v;
}

// 0x6317 {0, JID, type, SYSTEMTIME end}
static Bytes Started(DWORD dwJID, BYTE byType, const SYSTEMTIME& stEnd)
{
    Bytes v;
    PutB(v, 0);
    PutD(v, dwJID);
    PutB(v, byType);
    Put(v, &stEnd, sizeof(stEnd));
    return v;
}

static SYSTEMTIME StampToSystemTime(const TIMESTAMP_STRUCT& ts)
{
    SYSTEMTIME st;
    memset(&st, 0, sizeof(st));
    st.wYear = ts.year;
    st.wMonth = ts.month;
    st.wDay = ts.day;
    st.wHour = ts.hour;
    st.wMinute = ts.minute;
    st.wSecond = ts.second;
    return st;
}

// The SYSTEMTIME FileTimeToSystemTime gives for the timestamp (day of week filled in).
static SYSTEMTIME StampThroughFileTime(const TIMESTAMP_STRUCT& ts)
{
    FILETIME ft = Punishment_TimestampToFileTime(&ts);
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    return st;
}

static Punishment_Info_Internal* FindPunishment(DWORD dwJID, BYTE byType)
{
    PunishmentMap& map = g_PunishmentManager.m_mapPunishment;
    for (PunishmentMap::iterator it = map.lower_bound(dwJID); it != map.upper_bound(dwJID); ++it)
    {
        if (it->second->byType == byType)
            return it->second;
    }
    return NULL;
}

static void ReleasePunishments()
{
    PunishmentMap& map = g_PunishmentManager.m_mapPunishment;
    for (PunishmentMap::iterator it = map.begin(); it != map.end(); ++it)
        it->second->Release();
    map.clear();
}

static void RegisterUser(DWORD dwJID, WORD wShardID, const char* pszID)
{
    sContentUser user;
    user.dwIP = 0x0100000A + dwJID;
    user.dwJID = dwJID;
    user.strID = pszID;
    user.wShardID = wShardID;
    user.wAgentBodyID = 5;
    user.PlayTime.Clear();
    CHECK(g_ContentUserManager.Register(&user) == 0);
}

// ---------------------------------------------------------------------------------------------
// Punishment manager

static void TestPunishmentTime()
{
    TIMESTAMP_STRUCT ts = {2010, 12, 31, 23, 59, 58, 123};
    FILETIME ft = Punishment_TimestampToFileTime(&ts);
    SYSTEMTIME st;
    CHECK(FileTimeToSystemTime(&ft, &st));
    CHECK(st.wYear == 2010 && st.wMonth == 12 && st.wDay == 31 && st.wHour == 23
          && st.wMinute == 59 && st.wSecond == 58 && st.wMilliseconds == 0);
    // An invalid date keeps the start of the SYSTEMTIME (year | month << 16, day << 16).
    TIMESTAMP_STRUCT bad = {2010, 13, 7, 0, 0, 0, 0};
    ft = Punishment_TimestampToFileTime(&bad);
    CHECK(ft.dwLowDateTime == (2010 | (13 << 16)) && ft.dwHighDateTime == (7 << 16));
    SYSTEMTIME st2;
    memset(&st2, 0xCC, sizeof(st2));
    CHECK(Punishment_TimestampToSystemTime(&st2, &ts) == &st2);
    CHECK(st2.wYear == 2010 && st2.wMonth == 12 && st2.wDayOfWeek == 0 && st2.wDay == 31
          && st2.wSecond == 58 && st2.wMilliseconds == 0);
    FILETIME ftNow = Punishment_GetLocalFileTime();
    TIMESTAMP_STRUCT tsNow = LocalStamp(0);
    FILETIME ftStamp = Punishment_TimestampToFileTime(&tsNow);
    ULARGE_INTEGER a, b;
    a.LowPart = ftNow.dwLowDateTime;
    a.HighPart = ftNow.dwHighDateTime;
    b.LowPart = ftStamp.dwLowDateTime;
    b.HighPart = ftStamp.dwHighDateTime;
    CHECK(a.QuadPart + 10000000 >= b.QuadPart && a.QuadPart < b.QuadPart + 30000000);
}

static void TestPunishmentLoad()
{
    CHECK(g_pPunishmentManager == &g_PunishmentManager);
    TIMESTAMP_STRUCT tsPast = LocalStamp(-3600);
    TIMESTAMP_STRUCT tsPast2 = LocalStamp(-60);
    TIMESTAMP_STRUCT tsFuture = LocalStamp(3600);
    TIMESTAMP_STRUCT tsFuture2 = LocalStamp(7200);

    // A failed query.
    Answer(-2);
    CHECK(!g_PunishmentManager.Load(s_pTask));
    CHECK(Query("exec _QueryActivePunishmet", ACCOUNT_DB));
    CHECK(g_PunishmentManager.m_mapPunishment.empty());

    std::vector<Bytes> rows;
    rows.push_back(PunishmentRow(100, 3, tsPast, tsFuture));
    rows.push_back(PunishmentRow(100, 4, tsFuture, tsFuture2));
    rows.push_back(PunishmentRow(101, 3, tsPast, tsPast2));
    AnswerRows(0, rows);
    CHECK(g_PunishmentManager.Load(s_pTask));
    CHECK(Query("exec _QueryActivePunishmet", ACCOUNT_DB));
    CHECK(g_PunishmentManager.m_pTask == s_pTask);
    CHECK(g_PunishmentManager.m_mapPunishment.size() == 3);
    CHECK(g_PunishmentInfoPool.m_nInUse == 3);
    Punishment_Info_Internal* p = FindPunishment(100, 3);
    CHECK(p && p->bActive == 1 && p->dwJID == 100 && p->bInUse == 1);
    if (p)
    {
        FILETIME ft = Punishment_TimestampToFileTime(&tsFuture);
        CHECK(CompareFileTime(&p->ftEnd, &ft) == 0);
    }
    p = FindPunishment(100, 4);
    CHECK(p && p->bActive == 0);
    p = FindPunishment(101, 3);
    CHECK(p && p->bActive == 1);

    // 0x6318: the active punishments of an account (type 3 at 0, type 4 at 17).
    BYTE abOut[PUNISHMENT_ACTIVE_BUFFER + 34];
    memset(abOut, 0xEE, sizeof(abOut));
    BYTE* pOut = abOut + 34;
    CHECK(g_PunishmentManager.GetActive(100, pOut) == 1);
    SYSTEMTIME stEnd = StampThroughFileTime(tsFuture);
    CHECK(pOut[0] == 1 && memcmp(pOut + 1, &stEnd, sizeof(stEnd)) == 0);
    CHECK(pOut[17] == 0);
    for (int i = 18; i < PUNISHMENT_ACTIVE_BUFFER; ++i)
        CHECK(pOut[i] == 0);
    CHECK(abOut[0] == 0xEE && abOut[33] == 0xEE);
    CHECK(g_PunishmentManager.GetActive(999, pOut) == 0);
    CHECK(pOut[0] == 0);
    CHECK(NoQueries() && NoPackets());
}

static void TestPunishmentAdd()
{
    TIMESTAMP_STRUCT tsPast = LocalStamp(-120);
    TIMESTAMP_STRUCT tsEnd = LocalStamp(86400);
    TIMESTAMP_STRUCT tsFuture = LocalStamp(3600);
    RegisterUser(100, 10, "punished");      // shard 10, managed by body 7
    // Types 1 and 2 are refused.
    CHECK(!PunishmentManager_Add(g_pPunishmentManager, 100, 1, &tsPast, &tsEnd));
    CHECK(!PunishmentManager_Add(g_pPunishmentManager, 100, 2, &tsPast, &tsEnd));
    CHECK(g_PunishmentManager.m_mapPunishment.size() == 3);
    // A new type 3 punishment replaces the loaded one: ended, then started (the end as given,
    // day of week 0).
    CHECK(PunishmentManager_Add(g_pPunishmentManager, 100, 3, &tsPast, &tsEnd));
    CHECK(Relayed(7, 0x6317, Ended(100, 3)));
    CHECK(Relayed(7, 0x6317, Started(100, 3, StampToSystemTime(tsEnd))));
    CHECK(g_PunishmentManager.m_mapPunishment.size() == 3);
    CHECK(g_PunishmentInfoPool.m_nInUse == 3);
    Punishment_Info_Internal* p = FindPunishment(100, 3);
    CHECK(p && p->bActive == 1);
    // A future one is not started; the replaced type 4 is told as ended.
    CHECK(PunishmentManager_Add(g_pPunishmentManager, 100, 4, &tsFuture, &tsEnd));
    CHECK(Relayed(7, 0x6317, Ended(100, 4)));
    p = FindPunishment(100, 4);
    CHECK(p && p->bActive == 0);
    // Accounts that are not logged in are not told.
    CHECK(PunishmentManager_Add(g_pPunishmentManager, 102, 3, &tsPast, &tsEnd));
    CHECK(g_PunishmentManager.m_mapPunishment.size() == 4);
    CHECK(NoPackets());
    // RemoveByType removes only the type.
    CHECK(g_PunishmentManager.RemoveByType(100, 4));
    CHECK(!g_PunishmentManager.RemoveByType(100, 4));
    CHECK(!g_PunishmentManager.RemoveByType(555, 3));
    CHECK(FindPunishment(100, 3) != NULL && FindPunishment(100, 4) == NULL);
    CHECK(g_PunishmentInfoPool.m_nInUse == 3);
    ReleasePunishments();
    CHECK(g_PunishmentInfoPool.m_nInUse == 0);
    DWORD dwIP;
    CHECK(g_ContentUserManager.Unregister(100, &dwIP));
    CHECK(NoQueries() && NoPackets());
}

static void TestPunishmentTick()
{
    RegisterUser(101, 11, "ending");        // shard 11, managed by body 7
    RegisterUser(102, 10, "starting");
    TIMESTAMP_STRUCT tsPast = LocalStamp(-120);
    TIMESTAMP_STRUCT tsAlmost = LocalStamp(1);
    TIMESTAMP_STRUCT tsEnd = LocalStamp(3600);
    // 101 active, ends now; 102 begins in a second. The map has the two nodes, 101 the root
    // with 102 as its right child (the loop goes on from the erased node, see GM-015).
    CHECK(PunishmentManager_Add(g_pPunishmentManager, 101, 4, &tsPast, &tsPast));
    CHECK(Relayed(7, 0x6317, Started(101, 4, StampToSystemTime(tsPast))));
    CHECK(PunishmentManager_Add(g_pPunishmentManager, 102, 3, &tsAlmost, &tsEnd));
    CHECK(NoPackets(50));
    Sleep(2100);
    g_PunishmentManager.Tick();
    CHECK(Relayed(7, 0x6317, Ended(101, 4)));
    CHECK(Relayed(7, 0x6317, Started(102, 3, StampThroughFileTime(tsEnd))));
    CHECK(g_PunishmentManager.m_mapPunishment.size() == 1);
    CHECK(g_PunishmentInfoPool.m_nInUse == 1);
    Punishment_Info_Internal* p = FindPunishment(102, 3);
    CHECK(p && p->bActive == 1);
    // Nothing else happens until the end.
    g_PunishmentManager.Tick();
    CHECK(NoPackets(50));
    ReleasePunishments();
    DWORD dwIP;
    CHECK(g_ContentUserManager.Unregister(101, &dwIP));
    CHECK(g_ContentUserManager.Unregister(102, &dwIP));
    CHECK(NoQueries() && NoPackets());
}

// ---------------------------------------------------------------------------------------------
// User validation history

static COverlapJob_AutomatedPunishmentJob* TakeAutoJob()
{
    if (s_pMain->m_vecJobs.size() != 1)
    {
        printf("  %u jobs\n", (unsigned)s_pMain->m_vecJobs.size());
        s_pMain->m_vecJobs.clear();
        s_pMain->m_vecJobTasks.clear();
        return NULL;
    }
    COverlapJob_AutomatedPunishmentJob* pJob =
        (COverlapJob_AutomatedPunishmentJob*)s_pMain->m_vecJobs[0];
    s_pMain->m_vecJobs.clear();
    s_pMain->m_vecJobTasks.clear();
    return pJob;
}

static void TestUserValidationHistory()
{
    CUserValidationHistoryMgr& mgr = g_UserValidationHistoryMgr;
    CHECK(g_pUserValidationHistoryMgr == &mgr);
    CHECK(mgr.m_strPunisher == "AutomatedPunisher");
    for (int i = 0; i < UV_CHECK_COUNT; ++i)
    {
        CHECK(mgr.m_aConfig[i].dwTolerance == 3 && mgr.m_aConfig[i].dwBlockTimeMin == 10);
        CHECK(mgr.m_aConfig[i].strGuide == "PunishmentGuide");
        CHECK(mgr.m_aConfig[i].strDescription == "PunishmentDesc");
    }

    CServiceManagerProcess* pSM = new CServiceManagerProcess;
    pSM->Init(s_pTask);
    s_pMain->m_vecJobs.clear();
    s_pMain->m_vecJobTasks.clear();
    CHECK(mgr.SetPunisher(pSM, "Punisher"));
    CHECK(mgr.m_pSM == pSM && mgr.m_strPunisher == "Punisher");
    mgr.SetConfig(UV_CHECK_IBUV, "guide", "desc", 3, 15);
    mgr.SetConfig(UV_CHECK_MOTP, "g2", "d2", 1, 5);

    // The third failure is punished; the history of the check is removed.
    std::string strUser("victim");
    DWORD dwCount = 0;
    CHECK(!mgr.RecordFailure(&strUser, UV_CHECK_IBUV, &dwCount) && dwCount == 1);
    CHECK(!mgr.RecordFailure(&strUser, UV_CHECK_IBUV, &dwCount) && dwCount == 2);
    CHECK(s_pMain->m_vecJobs.empty());
    CHECK(g_UVFailureHistoryPool.m_nInUse == 1);
    CHECK(mgr.RecordFailure(&strUser, UV_CHECK_IBUV, &dwCount) && dwCount == 3);
    CHECK(mgr.m_aMap[UV_CHECK_IBUV].empty() && g_UVFailureHistoryPool.m_nInUse == 0);
    COverlapJob_AutomatedPunishmentJob* pJob = TakeAutoJob();
    CHECK(pJob != NULL);
    if (pJob)
    {
        CHECK(pJob->m_pOwner == s_pTask);
        CHECK(pJob->m_strUserID == "victim" && pJob->m_str2 == "Punisher");
        CHECK(pJob->m_str3 == "guide" && pJob->m_str4 == "desc" && pJob->m_dwParam == 15);
        pJob->ReturnToPool();
    }
    CHECK(g_AutomatedPunishmentJobPool.m_nInUse == 0);
    // The first failure is never punished, even with tolerance 1.
    CHECK(!mgr.RecordFailure(&strUser, UV_CHECK_MOTP, &dwCount) && dwCount == 1);
    CHECK(mgr.RecordFailure(&strUser, UV_CHECK_MOTP, &dwCount) && dwCount == 2);
    pJob = TakeAutoJob();
    CHECK(pJob && pJob->m_str3 == "g2" && pJob->m_dwParam == 5);
    if (pJob)
        pJob->ReturnToPool();

    // Clear removes the account from every check; other accounts stay.
    std::string strOther("other");
    CHECK(!mgr.RecordFailure(&strUser, UV_CHECK_LOGIN, &dwCount) && dwCount == 1);
    CHECK(!mgr.RecordFailure(&strUser, UV_CHECK_IBUV, &dwCount) && dwCount == 1);
    CHECK(!mgr.RecordFailure(&strOther, UV_CHECK_IBUV, &dwCount) && dwCount == 1);
    CHECK(g_UVFailureHistoryPool.m_nInUse == 3);
    mgr.Clear(&strUser);
    CHECK(mgr.m_aMap[UV_CHECK_LOGIN].empty() && mgr.m_aMap[UV_CHECK_IBUV].size() == 1);
    CHECK(g_UVFailureHistoryPool.m_nInUse == 1);
    mgr.Clear(&strUser);
    CHECK(g_UVFailureHistoryPool.m_nInUse == 1);

    // The day check clears everything when the day of the week changed.
    mgr.CheckDay();
    CHECK(g_UVFailureHistoryPool.m_nInUse == 1);
    mgr.m_dtLastCheck = CDateTime::GetCurrent().GetTime() - 86400;
    mgr.CheckDay();
    CHECK(g_UVFailureHistoryPool.m_nInUse == 0 && mgr.m_aMap[UV_CHECK_IBUV].empty());
    CHECK(mgr.m_dtLastCheck.GetTime() >= CDateTime::GetCurrent().GetTime() - 5);

    // ClearAll releases the histories of every check.
    CHECK(!mgr.RecordFailure(&strUser, UV_CHECK_LOGIN, &dwCount));
    CHECK(!mgr.RecordFailure(&strOther, UV_CHECK_MOTP, &dwCount));
    mgr.ClearAll();
    CHECK(g_UVFailureHistoryPool.m_nInUse == 0);
    CHECK(s_pMain->m_vecJobs.empty());
    delete pSM;
    CHECK(NoQueries() && NoPackets());
}

int main()
{
    CHECK(LoopbackBind_Install() == 1);
    setvbuf(stdout, NULL, _IONBF, 0);
    BSDebugSetFlags(0, 0);
    InitializeCriticalSection(&s_cs);

    Startup();
    TestPunishmentTime();
    TestPunishmentLoad();
    TestPunishmentAdd();
    TestPunishmentTick();
    TestUserValidationHistory();

    s_Engine.Shutdown();
    g_pBSNet = NULL;
    DeleteCriticalSection(&s_cs);
    return TEST_RESULT("GlobalManagerPunishmentTest");
}
