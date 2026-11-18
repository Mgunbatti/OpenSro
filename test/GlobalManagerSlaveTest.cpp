// GlobalManager slave (CGlobalManagerSlave), the IP country table (CIPCountryTable) and the CAS
// chat manager. The table is fed with small temporary files in the RIR and country code
// formats. A real engine is connected to itself: the client task records what is relayed to the
// bodies, the server task (standing in for the main process) records what is put into its
// queue. CDBQuery's execution is replaced (scripted results, rows through the trigger).

#include "LoopbackBind.h"

#include <winsock2.h>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

#include "LogCapture.h"
#include "TestCheck.h"

#include "GlobalManager/CASChat.h"
#include "GlobalManager/GlobalManagerDB.h"
#include "GlobalManager/GlobalManagerJobs.h"
#include "GlobalManager/GlobalManagerMainFwd.h"
#include "GlobalManager/GlobalManagerSlave.h"
#include "GlobalManager/IPCountryTable.h"
#include "BSLib/BSDebug.h"
#include "BSLib/DB/DBQuery.h"
#include "BSLib/thread/Task.h"
#include "BSNet/NetEngine.h"
#include "BSNet/SockListener.h"
#include "BSNet/security/Security.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerProcess.h"

using namespace ServerFramework;

typedef std::vector<BYTE> Bytes;

static CRITICAL_SECTION s_cs;
static std::string s_strDir;

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
    size_t i = 0;
    while (i < vGot.size() && i < vWant.size() && vGot[i] == vWant[i])
        ++i;
    printf("  differ at %u\n  got:  %s(%u)\n  want: %s(%u)\n", (unsigned)i, Hex(vGot).c_str(),
           (unsigned)vGot.size(), Hex(vWant).c_str(), (unsigned)vWant.size());
    return false;
}

static void WriteDiskFile(const std::string& strName, const std::string& strData)
{
    std::string strPath = s_strDir + "\\" + strName;
    FILE* pFile = NULL;
    fopen_s(&pFile, strPath.c_str(), "wb");
    CHECK(pFile != NULL);
    if (!pFile)
        return;
    fwrite(strData.data(), 1, strData.size(), pFile);
    fclose(pFile);
}

static std::string DirFile(const char* pszName)
{
    return s_strDir + "\\" + pszName;
}

// ---------------------------------------------------------------------------------------------
// Invalid CRT parameters: counted, and thrown as an int when s_bThrowInvalid is set (like the
// handler BSInstallSETranslator installs).

static int s_nInvalidParameter = 0;
static bool s_bThrowInvalid = true;

static void __cdecl InvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int,
                                     uintptr_t)
{
    ++s_nInvalidParameter;
    if (s_bThrowInvalid)
        throw 7;
}

// ---------------------------------------------------------------------------------------------
// Logs

static std::vector<std::string> s_vecLog;

static void CollectLogs()
{
    std::vector<CAPTURED_LOG> v = LogCapture_Drain();
    for (size_t i = 0; i < v.size(); ++i)
        s_vecLog.push_back(v[i].strText);
}

// True when a line starting with pszText was logged (it is removed).
static bool Logged(const char* pszText)
{
    CollectLogs();
    for (std::vector<std::string>::iterator it = s_vecLog.begin(); it != s_vecLog.end(); ++it)
    {
        if (it->compare(0, strlen(pszText), pszText) == 0)
        {
            s_vecLog.erase(it);
            return true;
        }
    }
    printf("  not logged: %s\n", pszText);
    return false;
}

static void ClearLogs()
{
    CollectLogs();
    s_vecLog.clear();
}

// ---------------------------------------------------------------------------------------------
// Tasks: both record every message.

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
};

BS_IMPLEMENT_DYNCREATE(CTestTask, &CServerProcessBase::classCServerProcessBase)

static CNetEngine s_Engine;
static CTestTask* s_pMain;      // the server task's job (the main process)
static DWORD s_dwClient;        // client side of the connection
static DWORD s_dwServer;        // GlobalManager side

// The next packet with the opcode for the task (any session) or on the session.
static bool TakePacketEx(DWORD dwTask, DWORD dwSession, WORD wOpcode, Bytes* pData,
                         DWORD dwTimeout = 3000)
{
    DWORD dwStart = GetTickCount();
    for (;;)
    {
        EnterCriticalSection(&s_cs);
        for (std::deque<PACKET>::iterator it = s_queIn.begin(); it != s_queIn.end(); ++it)
        {
            if ((dwTask ? it->dwTask == dwTask : it->dwSession == dwSession)
                && it->wOpcode == wOpcode)
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
            printf("  no packet 0x%04x\n", wOpcode);
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

// The data of the next relay (0x6008) at the client: target body, opcode, data.
static bool Relayed(WORD wTarget, WORD wOpcode, const Bytes& data)
{
    Bytes got;
    if (!TakePacketEx(0, s_dwClient, SERVER_MSG_RELAY_REQ, &got))
        return false;
    Bytes want;
    PutW(want, wTarget);
    PutW(want, wOpcode);
    want.insert(want.end(), data.begin(), data.end());
    return got.size() > 4 && Same(Bytes(got.begin() + 4, got.end()), want);
}

// The next message put into the main task's queue.
static bool Posted(WORD wOpcode, const Bytes& want)
{
    Bytes got;
    if (!TakePacketEx((DWORD)s_pMain, 0, wOpcode, &got))
        return false;
    return Same(got, want);
}

static CMsg* Req(const Bytes& data, WORD wOpcode = 0x6319)
{
    CMsg* p = g_pBSNet->AllocMsg(FALSE);
    p->SetOpcode(wOpcode);
    if (!data.empty())
        p->WriteBytes(&data[0], (WORD)data.size());
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
        CHECK((int)a.rows[i].size() <= m_nRowSize && (int)a.rows[i].size() <= m_nBufferSize);
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

static bool Query(const std::string& strSQL, CDBSessionMgr* pSession, int nSession)
{
    if (s_vecCall.empty())
    {
        printf("  no query (%s)\n", strSQL.c_str());
        return false;
    }
    QUERY_CALL c = s_vecCall.front();
    s_vecCall.erase(s_vecCall.begin());
    bool b = c.strSQL == strSQL && c.pSession == pSession && c.nSession == nSession;
    if (!b)
        printf("  query: %s (%p %d)\n  want:  %s\n", c.strSQL.c_str(), c.pSession, c.nSession,
               strSQL.c_str());
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

// The poll queries as the slave builds them: decoded from the stored form, then formatted.
static std::string PollQuery(bool bFirst, __int64 qwBuyNo)
{
    char szFormat[0x2800];
    strcpy_s(szFormat, bFirst ? CGlobalManagerSlave::s_szEncodedFirstQuery
                              : CGlobalManagerSlave::s_szEncodedPollQuery);
    CGlobalManagerSlave::DecodeText(szFormat);
    char szQuery[0x2800];
    if (bFirst)
        sprintf_s(szQuery, szFormat, qwBuyNo);
    else
        sprintf_s(szQuery, szFormat, qwBuyNo, qwBuyNo, qwBuyNo, qwBuyNo);
    return szQuery;
}

// ---------------------------------------------------------------------------------------------
// Architecture: body 1 this GlobalManager (division 3, certified by body 2), 2 its parent and
// 5 a shard body, both reachable through the test connection.

static CERT_MODULE s_aModule[2];
static SERVER_BODY s_aBody[3];
static SERVER_CORD s_CordUp;
static ServerBodyList s_listBody;

static void AddBody(int i, WORD wID, int nModule, SERVER_CORD* pRoute)
{
    SERVER_BODY& b = s_aBody[i];
    b.wID = wID;
    b.byModuleID = s_aModule[nModule].byID;
    b.pModule = &s_aModule[nModule];
    b.nState = 5;
    s_listBody.push_back(&b);
    g_mapServerBody[wID] = &b;
    if (pRoute)
        g_hmapServerRoute[wID] = pRoute;
}

static void SetupArchitecture()
{
    s_aModule[0].byID = 3;
    strcpy_s(s_aModule[0].szName, "GlobalManager");
    s_aModule[1].byID = 4;
    strcpy_s(s_aModule[1].szName, "ShardManager");
    s_CordUp.dwSessionID = s_dwServer;
    AddBody(0, 1, 0, NULL);
    AddBody(1, 2, 1, &s_CordUp);
    AddBody(2, 5, 1, &s_CordUp);
    s_aBody[0].byDivisionID = 3;
    s_aBody[0].wParentID = 2;
    g_pServerBodyList = &s_listBody;
    g_pSelfBody = &s_aBody[0];
}

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
    s_pMain = (CTestTask*)s_Engine.GetProactor(TEST_TASK_SERVER)->GetJob();
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
    CHECK(TakePacketEx(0, s_dwClient, 0x1003, NULL));
    Sleep(300);
    EnterCriticalSection(&s_cs);
    s_queIn.clear();
    LeaveCriticalSection(&s_cs);

    SetupArchitecture();
    g_pDefaultDBSession = DEFAULT_DB;
    g_pAccountDBSession = ACCOUNT_DB;
    g_pSilkDBSession = SILK_DB;
    // The main process' queue is the server task's (only GetOwner is called).
    // Only GetOwner is used: the test task stands in for the main process.
    g_pGlobalManagerProcess = (CGlobalManagerMainProcess*)(CServerProcessBase*)s_pMain;
}

// ---------------------------------------------------------------------------------------------
// IP country table

static DWORD IP(BYTE a, BYTE b, BYTE c, BYTE d)
{
    return a | ((DWORD)b << 8) | ((DWORD)c << 16) | ((DWORD)d << 24);
}

static bool Block(CIPCountryTable* pTable, DWORD dwKey, const char* pszCode, DWORD dwCount)
{
    IPCountryBlockMap::iterator it = pTable->m_mapBlock.find(dwKey);
    if (it == pTable->m_mapBlock.end())
    {
        printf("  no block %08x\n", dwKey);
        return false;
    }
    bool b = strcmp(it->second->szCode, pszCode) == 0 && it->second->dwIP == dwKey
             && it->second->dwCount == dwCount;
    if (!b)
        printf("  block %08x: %s %08x %u\n", dwKey, it->second->szCode, it->second->dwIP,
               it->second->dwCount);
    return b;
}

static std::string Code(CIPCountryTable* pTable, DWORD dwIP)
{
    char sz[8];
    memset(sz, 0x55, sizeof(sz));
    pTable->FindCountryCode(sz, dwIP);
    return sz;
}

static void TestIPTokens()
{
    // NextToken: the text before the delimiter, the position after it.
    char sz[64];
    DWORD bEnd = 0;
    const char* p = CIPCountryTable::NextToken("|", "ab|cd", sz, &bEnd);
    CHECK(strcmp(sz, "ab") == 0 && strcmp(p, "cd") == 0 && bEnd == 0);
    p = CIPCountryTable::NextToken("\r\n", "l1\r\nl2\r\n", sz, &bEnd);
    CHECK(strcmp(sz, "l1") == 0 && strcmp(p, "l2\r\n") == 0 && bEnd == 0);
    p = CIPCountryTable::NextToken("|", "|x", sz, &bEnd);
    CHECK(sz[0] == 0 && strcmp(p, "x") == 0 && bEnd == 0);
    // Without a delimiter the text is returned and the output stays as it was.
    strcpy_s(sz, "old");
    const char* pszText = "last";
    p = CIPCountryTable::NextToken("|", pszText, sz, &bEnd);
    CHECK(p == pszText && bEnd == 1 && strcmp(sz, "old") == 0);
    bEnd = 0;
    p = CIPCountryTable::NextToken("\r\n", "a\rb", sz, &bEnd);
    CHECK(bEnd == 1 && strcmp(sz, "old") == 0);
    bEnd = 0;
    pszText = "";
    p = CIPCountryTable::NextToken("|", pszText, sz, &bEnd);
    CHECK(p == pszText && bEnd == 1);

    // Addresses in network byte order; every part is cut to a byte.
    DWORD dwIP = 0;
    CIPCountryTable::ParseDottedIP("1.2.3.4", &dwIP);
    CHECK(dwIP == 0x04030201);
    CIPCountryTable::ParseDottedIP("10.0.255.7", &dwIP);
    CHECK(dwIP == 0x07FF000A);
    CIPCountryTable::ParseDottedIP("300.1.1.1", &dwIP);
    CHECK(dwIP == 0x0101012C);
    CIPCountryTable::ParseDottedIP("9.8", &dwIP);
    // Missing parts repeat the last one read (the output is left as it was).
    CHECK(dwIP == 0x08090909);

    CHECK(CIPCountryTable::IsClassfulBlock(0x100) && CIPCountryTable::IsClassfulBlock(0x10000)
          && CIPCountryTable::IsClassfulBlock(0x1000000));
    CHECK(!CIPCountryTable::IsClassfulBlock(0x200) && !CIPCountryTable::IsClassfulBlock(0)
          && !CIPCountryTable::IsClassfulBlock(0xFF));
}

static void TestIPRecords()
{
    CIPCountryTable* pTable = new CIPCountryTable;
    pTable->Reset();
    CHECK(pTable->m_mapBlock.empty() && pTable->m_pCursor == NULL && pTable->m_szLine[0] == 0
          && pTable->m_nIPv4Count == 0 && pTable->m_liRIRSize.QuadPart == 0);

    // A classful block is stored as it is.
    IP_COUNTRY_RECORD rec;
    memset(&rec, 0x77, sizeof(rec));
    pTable->ParseRIRRecord("apnic|JP|ipv4|1.0.16.0|256|20110412|allocated", &rec);
    CHECK(strcmp(rec.szCode, "JP") == 0 && rec.dwIP == IP(1, 0, 16, 0) && rec.dwCount == 256);
    CHECK(pTable->m_mapBlock.size() == 1 && Block(pTable, IP(1, 0, 16, 0), "JP", 256));
    // Other counts are split into /24, /16 or /8 blocks.
    pTable->ParseRIRRecord("apnic|KR|ipv4|1.11.0.0|768|20110412|allocated", NULL);
    CHECK(pTable->m_mapBlock.size() == 4 && Block(pTable, IP(1, 11, 0, 0), "KR", 256)
          && Block(pTable, IP(1, 11, 1, 0), "KR", 256) && Block(pTable, IP(1, 11, 2, 0), "KR", 256));
    pTable->ParseRIRRecord("ripencc|DE|ipv4|2.0.0.0|131072|20100712|allocated", NULL);
    CHECK(pTable->m_mapBlock.size() == 6 && Block(pTable, IP(2, 0, 0, 0), "DE", 0x10000)
          && Block(pTable, IP(2, 1, 0, 0), "DE", 0x10000));
    // Below 256 and above 16777216 addresses nothing is stored.
    pTable->ParseRIRRecord("arin|US|ipv4|3.0.0.0|33554432|19880223|assigned", NULL);
    pTable->ParseRIRRecord("arin|GB|ipv4|5.0.0.0|100|19880223|assigned", NULL);
    CHECK(pTable->m_mapBlock.size() == 6);
    // An address that is stored already keeps its block.
    pTable->ParseRIRRecord("apnic|CN|ipv4|1.0.16.0|256|20110412|allocated", NULL);
    CHECK(pTable->m_mapBlock.size() == 6 && Block(pTable, IP(1, 0, 16, 0), "JP", 256));

    // Lookups: the /24, /16 and /8 key of the address, the first block at or after it.
    CHECK(Code(pTable, IP(1, 0, 16, 9)) == "JP");
    CHECK(Code(pTable, IP(1, 11, 2, 200)) == "KR");
    CHECK(Code(pTable, IP(2, 1, 77, 1)) == "DE");
    CHECK(Code(pTable, IP(7, 7, 7, 7)) == "**");
    // The keys are compared in network byte order as numbers: for 1.0.17.0 the /16 key finds
    // the block of 2.0.0.0.
    CHECK(Code(pTable, IP(1, 0, 17, 0)) == "**");
    CHECK(pTable->FindInBlock(IP(1, 0, 0, 0), IP(1, 0, 17, 0), (char*)&rec) == 0);
    CHECK(strcmp(rec.szCode, "**") == 0);

    // Country names.
    pTable->m_mapCountryName["JP"] = "Japan";
    std::string strName = "x";
    pTable->GetCountryName("JP", &strName);
    CHECK(strName == "Japan");
    pTable->GetCountryName("XX", &strName);
    CHECK(strName == "**");

    // Clear frees the blocks; Reset only forgets them.
    pTable->Clear();
    CHECK(pTable->m_mapBlock.empty() && pTable->m_mapCountryName.empty());

    // With a single block the end of the block counts as inside (the /16 key finds it).
    pTable->ParseRIRRecord("apnic|JP|ipv4|1.0.16.0|256|20110412|allocated", NULL);
    CHECK(Code(pTable, IP(1, 0, 17, 0)) == "JP");
    CHECK(Code(pTable, IP(1, 0, 17, 1)) == "**");
    CHECK(Code(pTable, IP(1, 0, 15, 255)) == "**");
    pTable->Clear();

    // Summary lines: the type is the code; asn fits.
    IP_COUNTRY_RECORD summary;
    memset(&summary, 0, sizeof(summary));
    pTable->ParseRIRRecord("apnic|*|asn|*|2|summary", &summary);
    CHECK(strcmp(summary.szCode, "asn") == 0 && summary.dwIP == 0 && summary.dwCount == 2);
    CHECK(pTable->m_mapBlock.empty());
    pTable->ParseRIRSummary(&summary);
    CHECK(pTable->m_nASNCount == 2 && pTable->m_vecSummaryType.size() == 1
          && pTable->m_vecSummaryType[0] == "asn");
    // GM-021: a summary with more than 256 entries is split into blocks from address 0.
    pTable->ParseRIRRecord("apnic|*|asn|*|600|summary", &summary);
    CHECK(pTable->m_mapBlock.size() == 3 && Block(pTable, IP(0, 0, 0, 0), "asn", 256)
          && Block(pTable, IP(0, 0, 1, 0), "asn", 256) && Block(pTable, IP(0, 0, 2, 0), "asn", 256));
    CHECK(Code(pTable, IP(0, 0, 1, 7)) == "asn");
    pTable->Clear();

    // GM-020: "ipv4" does not fit the code. strcpy_s fails: with a throwing handler the line
    // throws, otherwise the code is empty and the summary is not counted.
    s_nInvalidParameter = 0;
    s_bThrowInvalid = true;
    bool bThrown = false;
    try
    {
        pTable->ParseRIRRecord("apnic|*|ipv4|*|23456|summary", &summary);
    }
    catch (int)
    {
        bThrown = true;
    }
    CHECK(bThrown && s_nInvalidParameter == 1 && summary.szCode[0] == 0);
    CHECK(pTable->m_mapBlock.empty());
    s_bThrowInvalid = false;
    pTable->ParseRIRRecord("apnic|*|ipv6|*|300|summary", &summary);
    CHECK(s_nInvalidParameter == 2 && summary.szCode[0] == 0 && summary.dwCount == 300);
    CHECK(pTable->m_mapBlock.size() == 2 && Block(pTable, IP(0, 0, 1, 0), "", 256));
    pTable->m_vecSummaryType.clear();
    pTable->ParseRIRSummary(&summary);
    CHECK(pTable->m_vecSummaryType.empty() && pTable->m_nIPv6Count == 0);
    s_bThrowInvalid = true;
    pTable->Clear();
    delete pTable;
}

// The country code file: the count line, then the lines (at least 240, or a message box).
static std::string CountryFile(int nExtra)
{
    std::string s;
    char sz[64];
    sprintf_s(sz, "%d\r\n", 3 + nExtra);
    s += sz;
    s += "JP\tJapan\r\n";
    s += "KR\r\n";
    s += "AA\tBB\tCC\r\n";
    for (int i = 0; i < nExtra; ++i)
    {
        sprintf_s(sz, "C%03d\tCountry %d\r\n", i, i);
        s += sz;
    }
    return s;
}

static const char s_szRIRHeader[] =
    "# a comment\r\n"
    "#\r\n"
    "2|apnic|20110101|9|19830613|20101231|+1000\r\n";

static std::string AsnRIRFile()
{
    std::string s = s_szRIRHeader;
    s += "apnic|*|asn|*|2|summary\r\n";
    s += "apnic|*|asn|*|1|summary\r\n";
    s += "apnic|*|asn|*|1|summary\r\n";
    s += "apnic|JP|asn|173|1|20020801|allocated\r\n";
    s += "apnic|KR|asn|1781|1|20020801|allocated\r\n";
    s += "apnic|AU|asn|1221|1|20000131|allocated\r\n";
    return s;
}

static std::string StandardRIRFile()
{
    std::string s = s_szRIRHeader;
    s += "apnic|*|asn|*|1|summary\r\n";
    s += "apnic|*|ipv4|*|2|summary\r\n";
    s += "apnic|*|ipv6|*|1|summary\r\n";
    s += "apnic|JP|asn|173|1|20020801|allocated\r\n";
    s += "apnic|JP|ipv4|1.0.16.0|256|20110412|allocated\r\n";
    s += "apnic|KR|ipv4|1.11.0.0|768|20110412|allocated\r\n";
    s += "apnic|JP|ipv6|2001:200::|35|19990813|allocated\r\n";
    return s;
}

static void TestIPFiles()
{
    CIPCountryTable* pTable = new CIPCountryTable;
    pTable->Reset();

    // The country code file.
    WriteDiskFile("country.txt", CountryFile(240));
    CHECK(pTable->LoadCountryCodeFile(DirFile("country.txt").c_str()) == 1);
    CHECK(pTable->m_mapCountryName.size() == 243);
    CHECK(pTable->m_mapCountryName["JP"] == "Japan");
    // A line without a tab: empty code, the line as name; two tabs: no name.
    CHECK(pTable->m_mapCountryName[""] == "KR");
    CHECK(pTable->m_mapCountryName["AA"] == "");
    CHECK(pTable->m_mapCountryName["C239"] == "Country 239");
    CHECK(pTable->m_pCountryBuffer != NULL && pTable->m_hCountryFile == NULL
          && pTable->m_liCountrySize.QuadPart == (LONGLONG)CountryFile(240).size());
    CHECK(pTable->LoadCountryCodeFile(DirFile("nothing.txt").c_str()) == 0);
    pTable->CloseFile();
    CHECK(pTable->m_pCountryBuffer == NULL && pTable->m_pRIRBuffer == NULL);

    // A delegation file whose summary types are all asn: the asn lines are skipped.
    WriteDiskFile("asn.txt", AsnRIRFile());
    CHECK(pTable->LoadRIRFile(DirFile("asn.txt").c_str()) == 1);
    CHECK(pTable->m_nASNCount == 1 && pTable->m_vecSummaryType.empty());
    CHECK(pTable->m_mapBlock.empty() && pTable->m_pRIRBuffer != NULL);
    // The skip moves 30 characters into the line before the line end is searched.
    CHECK(strcmp(pTable->m_szLine, "llocated") == 0);
    CHECK(pTable->LoadRIRFile(DirFile("nothing.txt").c_str()) == 0);
    ClearLogs();
    CHECK(pTable->LoadRIRFileTimed(DirFile("asn.txt").c_str()) == 1);
    std::string strLog = "File Loading Time : (" + DirFile("asn.txt") + ") ";
    CHECK(Logged(strLog.c_str()));
    pTable->CloseFile();

    // GM-020: a file with the usual summary lines throws at the ipv4 summary.
    WriteDiskFile("apnic.txt", StandardRIRFile());
    s_nInvalidParameter = 0;
    bool bThrown = false;
    try
    {
        pTable->LoadRIRFile(DirFile("apnic.txt").c_str());
    }
    catch (int)
    {
        bThrown = true;
    }
    CHECK(bThrown && s_nInvalidParameter == 1);
    CHECK(pTable->m_vecSummaryType.size() == 1 && pTable->m_mapBlock.empty());
    pTable->CloseFile();
    // When the handler returns, only "asn" is a summary type and the second pass has none.
    s_bThrowInvalid = false;
    bool bOutOfRange = false;
    try
    {
        pTable->LoadRIRFile(DirFile("apnic.txt").c_str());
    }
    catch (std::out_of_range&)
    {
        bOutOfRange = true;
    }
    CHECK(bOutOfRange && s_nInvalidParameter == 3 && pTable->m_vecSummaryType.empty());
    s_bThrowInvalid = true;
    pTable->CloseFile();

    pTable->Clear();
    delete pTable;

    // GetInstance creates the table once.
    CHECK(g_pIPCountryTable == NULL);
    CIPCountryTable* pInstance = CIPCountryTable::GetInstance();
    CHECK(pInstance != NULL && pInstance == g_pIPCountryTable
          && CIPCountryTable::GetInstance() == pInstance);
    delete pInstance;
    g_pIPCountryTable = NULL;
}

// ---------------------------------------------------------------------------------------------
// CGlobalManagerSlave

static void TestSlaveBasics()
{
    CHECK(strcmp(CGlobalManagerSlave::classCGlobalManagerSlave.m_lpszClassName,
                 "CGlobalManagerSlave") == 0);
    CHECK(CGlobalManagerSlave::classCGlobalManagerSlave.m_nObjectSize == 0x400E8);
    CHECK(CGlobalManagerSlave::classCGlobalManagerSlave.m_pBaseClass
          == &CServerProcessBase::classCServerProcessBase);

    CHECK(g_pGlobalManagerSlave == NULL);
    CBase* pObject = CGlobalManagerSlave::classCGlobalManagerSlave.m_pfnCreateObject();
    CGlobalManagerSlave* pSlave = (CGlobalManagerSlave*)pObject;
    CHECK(pSlave && g_pGlobalManagerSlave == pSlave);
    CHECK(pSlave->GetRuntimeClass() == &CGlobalManagerSlave::classCGlobalManagerSlave);
    CHECK(pSlave->m_pMainProcess == NULL && !pSlave->m_bRIRLoaded && !pSlave->m_bSilkHistoryEnabled
          && pSlave->m_qwLastSilkPoll == 0 && pSlave->m_qwLastBuyNo == 0
          && pSlave->m_mapIPStatistics.empty());
    // The destructor creates the table to delete it; the pointer keeps its value.
    CHECK(g_pIPCountryTable == NULL);
    CGlobalManagerSlave::classCGlobalManagerSlave.m_pfnDeleteObject(pObject);
    CHECK(g_pGlobalManagerSlave == NULL && g_pIPCountryTable != NULL);
    g_pIPCountryTable = NULL;

    // The stored queries: XOR 0x90 up to the first 0 they produce.
    char sz[0x2800];
    strcpy_s(sz, CGlobalManagerSlave::s_szEncodedFirstQuery);
    CHECK(strlen(sz) == 102);
    CGlobalManagerSlave::DecodeText(sz);
    CHECK(strlen(sz) == 102 && strncmp(sz, "select ", 7) == 0);
    int nArgs = 0;
    for (const char* p = sz; *p; ++p)
        nArgs += *p == '%';
    CHECK(nArgs == 1 && strstr(sz, "%I64d") != NULL);
    strcpy_s(sz, CGlobalManagerSlave::s_szEncodedPollQuery);
    CHECK(strlen(sz) == 437);
    CGlobalManagerSlave::DecodeText(sz);
    nArgs = 0;
    for (const char* p = sz; *p; ++p)
        nArgs += *p == '%';
    CHECK(strlen(sz) == 437 && nArgs == 4);
    char szShort[] = { (char)0xF1, (char)0x90, (char)0xF2, 0 };
    CGlobalManagerSlave::DecodeText(szShort);
    CHECK(szShort[0] == 'a' && szShort[1] == 0 && szShort[2] == (char)0xF2);

    // The packed time of the silk history.
    TIMESTAMP_STRUCT ts = { 2011, 3, 15, 13, 45, 30, 999 };
    SILK_HISTORY_TIME time;
    *(DWORD*)&time = 0;
    GMSlave_PackTimestamp(&time, &ts);
    CHECK(*(DWORD*)&time == (11 | (3 << 6) | (15 << 10) | (13 << 15) | (45 << 20) | (30u << 26)));

    // The performance counter in ms.
    LARGE_INTEGER* pliFrequency = GMSlave_GetPerfFrequency();
    CHECK(pliFrequency == GMSlave_GetPerfFrequency() && pliFrequency->QuadPart > 0);
    __int64 qwMs = GMSlave_GetPerfCounterMs(pliFrequency);
    Sleep(50);
    __int64 qwElapsed = GMSlave_GetPerfCounterMs(pliFrequency) - qwMs;
    CHECK(qwElapsed >= 40 && qwElapsed < 1000);
}

static void TestSlaveSilkPoll()
{
    CGlobalManagerSlave* pSlave = new CGlobalManagerSlave;
    CHECK(pSlave->Initialize(NULL, NULL));
    pSlave->m_pMainProcess = s_pMain;

    // 0x6319 {2, flag, string}: the purchase number; flag 16 enables the poll.
    Bytes req;
    PutB(req, 2);
    PutB(req, 3);
    PutS(req, "77");
    CMsg* pMsg = Req(req);
    pSlave->Dispatch(pMsg, 0, 0, NULL);
    CHECK(pMsg->GetUnreadSize() == 0);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(pSlave->m_qwLastBuyNo == 77 && !pSlave->m_bSilkHistoryEnabled);
    req.clear();
    PutB(req, 2);
    PutB(req, 16);
    PutS(req, "12345678901");
    pMsg = Req(req);
    pSlave->Dispatch(pMsg, 0, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(pSlave->m_qwLastBuyNo == 12345678901LL && pSlave->m_bSilkHistoryEnabled);

    // The first poll: the highest purchase number from the one set on, then the four values.
    pSlave->m_qwLastBuyNo = 40;
    Bytes first;
    PutD(first, 50);
    PutD(first, 0);
    PutD(first, 0);
    PutD(first, 0);
    Answer(0, first);
    Bytes row;
    PutD(row, 100);
    PutD(row, 200);
    PutD(row, 1300);
    PutD(row, 5);
    Answer(0, row);
    pSlave->PollSilkHistory();
    CHECK(Query(PollQuery(true, 40), SILK_DB, 2));
    CHECK(Query(PollQuery(false, 50), SILK_DB, 2));
    Bytes want;
    PutB(want, 3);
    Put(want, &row[0], row.size());
    CHECK(Relayed(2, 0x231B, want));
    CHECK(pSlave->m_qwLastBuyNo == 1300 && pSlave->m_qwLastSilkPoll != 0);
    CHECK(!pSlave->IsSilkHistoryDue());
    // Not due again for five minutes.
    pSlave->PollSilkHistory();
    CHECK(NoQueries());

    // Later polls only run the second query; without a newer purchase the number stays.
    unsigned __int64 qwNow = GMSlave_GetPerfCounterMs(GMSlave_GetPerfFrequency());
    pSlave->m_qwLastSilkPoll = qwNow - 299000;
    CHECK(!pSlave->IsSilkHistoryDue());
    pSlave->m_qwLastSilkPoll = qwNow - 300500;
    CHECK(pSlave->IsSilkHistoryDue());
    row.clear();
    PutD(row, 7);
    PutD(row, 8);
    PutD(row, 0);
    PutD(row, 0);
    Answer(0, row);
    pSlave->PollSilkHistory();
    CHECK(Query(PollQuery(false, 1300), SILK_DB, 2));
    want.clear();
    PutB(want, 3);
    PutD(want, 7);
    PutD(want, 8);
    PutD(want, 1300);
    PutD(want, 0);
    CHECK(Relayed(2, 0x231B, want));
    CHECK(pSlave->m_qwLastBuyNo == 1300 && !pSlave->IsSilkHistoryDue());

    // GM-025: a failed query keeps the poll due; the next pass runs it again.
    ClearLogs();
    pSlave->m_qwLastSilkPoll = qwNow - 400000;
    unsigned __int64 qwOld = pSlave->m_qwLastSilkPoll;
    Answer(-2);
    Answer(-2);
    pSlave->PollSilkHistory();
    pSlave->PollSilkHistory();
    CHECK(Query(PollQuery(false, 1300), SILK_DB, 2) && Query(PollQuery(false, 1300), SILK_DB, 2));
    CHECK(pSlave->m_qwLastSilkPoll == qwOld);
    CHECK(Logged("You cannot support Silk History Function for User... (ErrCode:0x71)"));
    pSlave->m_qwLastSilkPoll = 0;
    Answer(-1);
    pSlave->PollSilkHistory();
    CHECK(Query(PollQuery(true, 1300), SILK_DB, 2) && NoQueries());
    CHECK(pSlave->m_qwLastSilkPoll == 0 && pSlave->m_qwLastBuyNo == 1300);
    CHECK(Logged("You cannot support Silk History Function for User... (ErrCode:0xA8)"));

    // Without the silk DB nothing happens; without the main process nothing is sent and the
    // poll stays due.
    g_pSilkDBSession = NULL;
    pSlave->PollSilkHistory();
    CHECK(NoQueries());
    g_pSilkDBSession = SILK_DB;
    pSlave->m_pMainProcess = NULL;
    first.clear();
    PutD(first, 0);
    PutD(first, 0);
    PutD(first, 0);
    PutD(first, 0);
    Answer(0, first);
    Answer(0, row);
    pSlave->PollSilkHistory();
    CHECK(Query(PollQuery(true, 1300), SILK_DB, 2) && Query(PollQuery(false, 0), SILK_DB, 2));
    CHECK(pSlave->m_qwLastSilkPoll == 0 && pSlave->m_qwLastBuyNo == 0);
    CHECK(NoPackets() && NoQueries());

    delete pSlave;
    g_pIPCountryTable = NULL;
}

static Bytes SilkRow(DWORD dw0, WORD wYear, WORD wMonth, WORD wDay, WORD wHour, WORD wMinute,
                     WORD wSecond)
{
    SILK_HISTORY_ROW row;
    row.dw0 = dw0;
    row.ts.year = wYear;
    row.ts.month = wMonth;
    row.ts.day = wDay;
    row.ts.hour = wHour;
    row.ts.minute = wMinute;
    row.ts.second = wSecond;
    row.ts.fraction = 12345;
    Bytes v;
    Put(v, &row, sizeof(row));
    return v;
}

static void TestSlaveSilkHistory()
{
    CGlobalManagerSlave* pSlave = new CGlobalManagerSlave;
    CHECK(pSlave->Initialize(NULL, NULL));
    pSlave->m_pMainProcess = s_pMain;

    // 0x6319 {1, WORD body, 14 bytes}: _GetSilkHistory with the second and the last two
    // values; 0xA31A {DWORD 1, the 14 bytes, 0, WORD count, entries} relayed to the body.
    SILK_HISTORY_REQ silk;
    silk.dw0 = 0x01020304;
    silk.dw4 = 4567;
    silk.dw8 = 0x0A0B0C0D;
    silk.byC = 9;
    silk.byD = 200;
    Bytes req;
    PutB(req, 1);
    PutW(req, 5);
    Put(req, &silk, sizeof(silk));
    std::vector<Bytes> rows;
    rows.push_back(SilkRow(0x11, 2010, 12, 31, 23, 59, 58));
    rows.push_back(SilkRow(0x22, 2011, 1, 2, 3, 4, 5));
    AnswerRows(0, rows);
    CMsg* pMsg = Req(req);
    pSlave->Dispatch(pMsg, 0, 0, NULL);
    CHECK(pMsg->GetUnreadSize() == 0);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(Query("exec _GetSilkHistory 4567, 9, 200", SILK_DB, 2));
    Bytes want;
    PutD(want, 1);
    Put(want, &silk, sizeof(silk));
    PutB(want, 0);
    PutW(want, 2);
    PutD(want, 0x11);
    PutD(want, 10 | (12 << 6) | (31 << 10) | (23 << 15) | (59 << 20) | (58u << 26));
    PutD(want, 0x22);
    PutD(want, 11 | (1 << 6) | (2 << 10) | (3 << 15) | (4 << 20) | (5u << 26));
    CHECK(Relayed(5, 0xA31A, want));

    // No rows: count 0; a failed query: 0xF6.
    Answer(1);
    pMsg = Req(req);
    pSlave->Dispatch(pMsg, 0, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(Query("exec _GetSilkHistory 4567, 9, 200", SILK_DB, 2));
    want.clear();
    PutD(want, 1);
    Put(want, &silk, sizeof(silk));
    PutB(want, 0);
    PutW(want, 0);
    CHECK(Relayed(5, 0xA31A, want));
    Answer(-2);
    pMsg = Req(req);
    pSlave->Dispatch(pMsg, 0, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(Query("exec _GetSilkHistory 4567, 9, 200", SILK_DB, 2));
    want.clear();
    PutD(want, 1);
    Put(want, &silk, sizeof(silk));
    PutB(want, 0xF6);
    CHECK(Relayed(5, 0xA31A, want));

    // Without the main process the answer is not sent.
    pSlave->m_pMainProcess = NULL;
    Answer(0, rows[0]);
    pMsg = Req(req);
    pSlave->Dispatch(pMsg, 0, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(Query("exec _GetSilkHistory 4567, 9, 200", SILK_DB, 2));
    CHECK(NoPackets() && NoQueries());

    // Other operations and details are logged.
    ClearLogs();
    req.clear();
    PutB(req, 9);
    pMsg = Req(req);
    pSlave->Dispatch(pMsg, 0, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(Logged("Unhandled GlobalManager Slave Job Detected!!! ( OperationType : 9 )"));
    req.clear();
    PutB(req, 0);
    PutB(req, 0x1F);
    pMsg = Req(req);
    pSlave->Dispatch(pMsg, 0, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(Logged("Unhandled GlobalManager Slave Job Detected!!! ( DetailType : 1F, 514 )"));
    // Other opcodes: the opcode is logged, the data stays.
    req.clear();
    PutD(req, 1);
    pMsg = Req(req, 0x1234);
    pSlave->Dispatch(pMsg, 0, 0, NULL);
    CHECK(pMsg->GetUnreadSize() == 4);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(Logged("CGlobalManagerSlave::Unhandled Msg: 0x1234"));
    // A cut job throws.
    req.clear();
    PutB(req, 1);
    PutW(req, 5);
    pMsg = Req(req);
    bool bThrown = false;
    try
    {
        pSlave->Dispatch(pMsg, 0, 0, NULL);
    }
    catch (CMsgException&)
    {
        bThrown = true;
    }
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(bThrown && NoQueries() && NoPackets());

    delete pSlave;
    g_pIPCountryTable = NULL;
}

static Bytes StatRow(DWORD dwIP)
{
    Bytes v;
    PutD(v, 0x99);
    PutD(v, dwIP);
    return v;
}

static void TestSlaveIPStatistics()
{
    CGlobalManagerSlave* pSlave = new CGlobalManagerSlave;
    CHECK(pSlave->Initialize(NULL, NULL));
    CIPCountryTable* pTable = CIPCountryTable::GetInstance();
    pTable->Reset();
    pTable->ParseRIRRecord("apnic|JP|ipv4|1.0.16.0|256|20110412|allocated", NULL);
    pTable->m_mapCountryName["JP"] = "Japan";

    // 0x6319 {0, 16, from, to, DWORD request}: _IPStatistics on the default DB, the rows
    // counted per country; 0xA319 {0x11, request, count, (name, count)...} into the main task's
    // queue. GM-022: every row counts for the empty code ("**").
    Bytes req;
    PutB(req, 0);
    PutB(req, 16);
    PutS(req, "2011-01-01");
    PutS(req, "2011-02-01");
    PutD(req, 0x55);
    std::vector<Bytes> rows;
    rows.push_back(StatRow(IP(1, 0, 16, 5)));
    rows.push_back(StatRow(IP(9, 9, 9, 9)));
    rows.push_back(StatRow(IP(1, 0, 16, 6)));
    AnswerRows(0, rows);
    CMsg* pMsg = Req(req);
    pSlave->Dispatch(pMsg, 0, 0, NULL);
    CHECK(pMsg->GetUnreadSize() == 0);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(Query("exec _IPStatistics\t'2011-01-01', '2011-02-01'", DEFAULT_DB, 2));
    Bytes want;
    PutB(want, 0x11);
    PutD(want, 0x55);
    PutD(want, 1);
    PutS(want, "**");
    PutD(want, 3);
    CHECK(Posted(0xA319, want));
    CHECK(pSlave->m_mapIPStatistics.size() == 1
          && pSlave->m_mapIPStatistics[""]->dwCount == 3
          && pSlave->m_mapIPStatistics[""]->szCode[0] == 0);

    // The next request starts again; a name for the empty code is used.
    pTable->m_mapCountryName[""] = "Nowhere";
    rows.pop_back();
    AnswerRows(0, rows);
    pMsg = Req(req);
    pSlave->Dispatch(pMsg, 0, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(Query("exec _IPStatistics\t'2011-01-01', '2011-02-01'", DEFAULT_DB, 2));
    want.clear();
    PutB(want, 0x11);
    PutD(want, 0x55);
    PutD(want, 1);
    PutS(want, "Nowhere");
    PutD(want, 2);
    CHECK(Posted(0xA319, want));
    // No rows: no entries.
    Answer(1);
    pMsg = Req(req);
    pSlave->Dispatch(pMsg, 0, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(Query("exec _IPStatistics\t'2011-01-01', '2011-02-01'", DEFAULT_DB, 2));
    want.clear();
    PutB(want, 0x11);
    PutD(want, 0x55);
    PutD(want, 0);
    CHECK(Posted(0xA319, want));
    // GM-023: a failed query is logged; its answer is never sent.
    ClearLogs();
    Answer(-2);
    pMsg = Req(req);
    pSlave->Dispatch(pMsg, 0, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(Query("exec _IPStatistics\t'2011-01-01', '2011-02-01'", DEFAULT_DB, 2));
    CHECK(Logged("CGlobalManagerSlave::OnUSER_IPRANGE_STATISTICS_RESULT_REQ (exec _IPStatistics\t"
                 "'2011-01-01', '2011-02-01')"));
    CHECK(NoPackets() && NoQueries());

    // The destructor frees the statistics and the table.
    delete pSlave;
    g_pIPCountryTable = NULL;
}

static void TestSlaveLoadRIRFiles()
{
    CGlobalManagerSlave* pSlave = new CGlobalManagerSlave;
    CHECK(pSlave->Initialize(NULL, NULL));
    char szOldDir[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, szOldDir);
    SetCurrentDirectoryA(s_strDir.c_str());

    // Server.cfg is read again; the files are taken from the current directory.
    WriteDiskFile("Server.cfg",
                  "common { DumpMsgPool 0 }\r\n"
                  "GlobalManager { CountryCodeFile \"country.txt\" RIRFileNum 3 "
                  "RIRFile1 \"asn.txt\" RIRFile3 \"missing.txt\" }\r\n");
    WriteDiskFile("country.txt", CountryFile(240));
    WriteDiskFile("asn.txt", AsnRIRFile());
    g_ServerGlobals.mapExtra["Stale"] = "1";
    CIPCountryTable* pTable = CIPCountryTable::GetInstance();
    pTable->Reset();
    pTable->ParseRIRRecord("apnic|JP|ipv4|1.0.16.0|256|20110412|allocated", NULL);
    ClearLogs();
    Bytes req;
    PutB(req, 0);
    PutB(req, 1);
    PutD(req, 0x77);
    CMsg* pMsg = Req(req);
    pSlave->Dispatch(pMsg, 0, 0, NULL);
    CHECK(pMsg->GetUnreadSize() == 0);
    g_pBSNet->ReleaseMsg(pMsg);
    Bytes want;
    PutB(want, 2);
    PutD(want, 0x77);
    PutB(want, 3);
    CHECK(Posted(0xA319, want));
    CHECK(pSlave->m_bRIRLoaded);
    CHECK(g_ServerGlobals.mapExtra.find("Stale") == g_ServerGlobals.mapExtra.end());
    CHECK(g_ServerGlobals.mapExtra["RIRFileNum"] == "3");
    CHECK(g_pIPCountryTable == pTable && pTable->m_mapBlock.empty()
          && pTable->m_mapCountryName.size() == 243 && pTable->m_mapCountryName["JP"] == "Japan");
    CHECK(pTable->m_pCountryBuffer == NULL && pTable->m_pRIRBuffer == NULL
          && pTable->m_nASNCount == 1);
    CHECK(Logged("Load RIR Files Start"));
    CHECK(Logged(("File Loading Time : (" + DirFile("asn.txt") + ")").c_str()));
    CHECK(Logged(("Loading IP Table is Failed (" + DirFile("missing.txt") + ")").c_str()));
    CHECK(Logged("Load RIR Files End ("));

    // GM-020: the usual RIR files throw out of the job; nothing is answered.
    WriteDiskFile("Server.cfg",
                  "GlobalManager { RIRFileNum 1 RIRFile1 \"apnic.txt\" }\r\n");
    WriteDiskFile("apnic.txt", StandardRIRFile());
    pSlave->m_bRIRLoaded = FALSE;
    pMsg = Req(req);
    bool bThrown = false;
    try
    {
        pSlave->Dispatch(pMsg, 0, 0, NULL);
    }
    catch (int)
    {
        bThrown = true;
    }
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(bThrown && !pSlave->m_bRIRLoaded && pTable->m_pRIRBuffer != NULL);
    CHECK(NoPackets());
    pTable->CloseFile();

    SetCurrentDirectoryA(szOldDir);
    g_ServerGlobals.mapExtra.clear();
    delete pSlave;
    g_pIPCountryTable = NULL;
}

// The slave's loop on its own thread with a task of its own.
struct RUN_PARAM
{
    CGlobalManagerSlave*    pSlave;
    volatile LONG           lStop;
    int                     nIndex;
};

static DWORD WINAPI RunThread(LPVOID pParam)
{
    RUN_PARAM* p = (RUN_PARAM*)pParam;
    return p->pSlave->Run(&p->lStop, p->nIndex);
}

static void TestSlaveRun()
{
    CGlobalManagerSlave* pSlave = new CGlobalManagerSlave;
    CTask* pTask = new CTask;
    pTask->m_pQueue = new CQueMT<CMsg>;
    CHECK(pSlave->Initialize(pTask, NULL));
    CHECK(pSlave->GetOwner() == pTask);
    pSlave->m_pMainProcess = s_pMain;

    // A set stop flag returns at once.
    RUN_PARAM run;
    run.pSlave = pSlave;
    run.lStop = 1;
    run.nIndex = 3;
    ClearLogs();
    CHECK(pSlave->Run(&run.lStop, 3) == 0);
    CollectLogs();
    CHECK(s_vecLog.empty());

    // The messages are dispatched; once enabled the poll runs on the same loop.
    Bytes first;
    PutD(first, 60);
    PutD(first, 0);
    PutD(first, 0);
    PutD(first, 0);
    Answer(0, first);
    Bytes row;
    PutD(row, 1);
    PutD(row, 2);
    PutD(row, 61);
    PutD(row, 3);
    Answer(0, row);
    run.lStop = 0;
    HANDLE hThread = CreateThread(NULL, 0, RunThread, &run, 0, NULL);
    Bytes req;
    PutD(req, 1);
    pTask->m_pQueue->Push(Req(req, 0x1234));
    req.clear();
    PutB(req, 2);
    PutB(req, 16);
    PutS(req, "59");
    pTask->m_pQueue->Push(Req(req));
    Bytes want;
    PutB(want, 3);
    Put(want, &row[0], row.size());
    CHECK(Relayed(2, 0x231B, want));
    CHECK(Query(PollQuery(true, 59), SILK_DB, 2) && Query(PollQuery(false, 60), SILK_DB, 2));
    CHECK(pSlave->m_bSilkHistoryEnabled && pSlave->m_qwLastBuyNo == 61);
    CHECK(Logged("CGlobalManagerSlave::Unhandled Msg: 0x1234"));
    CHECK(pTask->m_pQueue->GetSize() == 0);

    // The stop flag ends the loop.
    InterlockedExchange(&run.lStop, 1);
    CHECK(WaitForSingleObject(hThread, 3000) == WAIT_OBJECT_0);
    DWORD dwExit = 1;
    GetExitCodeThread(hThread, &dwExit);
    CloseHandle(hThread);
    CHECK(dwExit == 0 && Logged("GlobalManagerSlave Thread Closed!"));

    // So does a stopped queue that still counts a message.
    run.lStop = 0;
    pSlave->m_bSilkHistoryEnabled = FALSE;
    pTask->m_pQueue->Push(Req(req));
    pTask->m_pQueue->Stop();
    hThread = CreateThread(NULL, 0, RunThread, &run, 0, NULL);
    CHECK(WaitForSingleObject(hThread, 3000) == WAIT_OBJECT_0);
    CloseHandle(hThread);
    CHECK(Logged("GlobalManagerSlave Thread Closed!"));
    CHECK(NoQueries() && NoPackets());

    delete pSlave;
    g_pIPCountryTable = NULL;
    delete pTask;
}

// ---------------------------------------------------------------------------------------------
// CAS chat manager: only its construction and destruction run in the original.

static void TestCASChatManager()
{
    CHECK(g_CASChatManager.m_IDPool.m_dwMaxID == 1000 && g_CASChatManager.m_IDPool.m_dwNextID == 500
          && g_CASChatManager.m_IDPool.m_deqFree.size() == 500
          && g_CASChatManager.m_IDPool.m_deqFree.front() == 0
          && g_CASChatManager.m_IDPool.m_deqFree.back() == 499
          && g_CASChatManager.m_IDPool.m_dwInUse == 0);
    CHECK(g_CASChatManager.m_pProcess == NULL && g_CASChatManager.m_mapOwner.empty()
          && g_CASChatManager.m_mapSession.empty());

    // The destructor deletes the sessions (and skips empty entries).
    CCASChatManager* pManager = new CCASChatManager;
    CASChatSession* pSession = new CASChatSession;
    pSession->strText.assign(100, 'x');
    pManager->m_mapSession[pManager->m_IDPool.Pop()] = pSession;
    pManager->m_mapSession[7] = NULL;
    pManager->m_mapOwner[3] = 0;
    CHECK(pManager->m_mapSession.size() == 2 && pManager->m_IDPool.m_dwInUse == 1);
    delete pManager;
}

static void Trace(const char* psz)
{
    char sz[4];
    if (GetEnvironmentVariableA("SLAVE_TEST_TRACE", sz, sizeof(sz)))
        printf("%s\n", psz);
}

int main()
{
    CHECK(LoopbackBind_Install() == 1);
    setvbuf(stdout, NULL, _IONBF, 0);
    BSDebugSetFlags(0, 0);
    InitializeCriticalSection(&s_cs);
    _set_invalid_parameter_handler(InvalidParameter);
    char szTemp[MAX_PATH];
    GetTempPathA(MAX_PATH, szTemp);
    char szDir[MAX_PATH];
    sprintf_s(szDir, "%sGlobalManagerSlaveTest_%u", szTemp, GetCurrentProcessId());
    CreateDirectoryA(szDir, NULL);
    s_strDir = szDir;
    strcpy_s(g_ServerGlobals.szModuleDir, szDir);

    Startup();
    LogCapture_Start();
    Trace("TestIPTokens"); TestIPTokens();
    Trace("TestIPRecords"); TestIPRecords();
    Trace("TestIPFiles"); TestIPFiles();
    Trace("TestSlaveBasics"); TestSlaveBasics();
    Trace("TestSlaveSilkPoll"); TestSlaveSilkPoll();
    Trace("TestSlaveSilkHistory"); TestSlaveSilkHistory();
    Trace("TestSlaveIPStatistics"); TestSlaveIPStatistics();
    Trace("TestSlaveLoadRIRFiles"); TestSlaveLoadRIRFiles();
    Trace("TestSlaveRun"); TestSlaveRun();
    Trace("TestCASChatManager"); TestCASChatManager();
    LogCapture_Stop();

    s_Engine.Shutdown();
    g_pBSNet = NULL;
    const char* apszFile[] = { "Server.cfg", "country.txt", "asn.txt", "apnic.txt" };
    for (int i = 0; i < 4; ++i)
        DeleteFileA(DirFile(apszFile[i]).c_str());
    RemoveDirectoryA(szDir);
    DeleteCriticalSection(&s_cs);
    return TEST_RESULT("GlobalManagerSlaveTest");
}
