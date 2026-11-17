// The January 2011 framework of GlobalManager (JMX_FRAMEWORK_2011_01, the "January 2011
// (GlobalManager)" table of JMX_ServerFramework\FRAMEWORK_2011.md). The test is compiled with
// GLOBAL_DEFS and links the January builds of the libraries (GLOBAL_LIBS); it checks the
// behaviour that differs from the 2008 code DatabaseTest checks. With the 32-bit Microsoft
// Access ODBC driver a temporary .mdb is used as well; without it those checks are skipped.

#include "BSLib/DB/Database.h"
#include "BSLib/DB/DBQuery.h"
#include "BSLib/Util/TimeSpan.h"
#include "BSNet/MsgPool.h"
#include "BSNet/NetEngine.h"
#include "BSNet/NetEngineLog.h"

#include <odbcinst.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "LogCapture.h"
#include "TestCheck.h"

#pragma comment(lib, "odbccp32.lib")

#ifndef JMX_FRAMEWORK_2011_01
#error Framework201101Test is compiled with JMX_FRAMEWORK_2011_01 (see GLOBAL_DEFS in the Makefile)
#endif

// ---------------------------------------------------------------------------------------------
// log capture: NetEngineLog (HandleSQLError, formatted) and LogPrintf (CDBQuery)

static std::vector<std::string> s_vecNetLog;

static int CaptureNetLog(DWORD dwFlags, const char* pszFormat, ...)
{
    char sz[0x2400];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf_s(sz, sizeof(sz), _TRUNCATE, pszFormat, args);
    va_end(args);
    if (dwFlags == 0x2000001)
        s_vecNetLog.push_back(sz);
    return 0;
}

class CFakeNet : public IBSNet
{
public:
    CMsgPool m_Pool;

    virtual CMsg* __stdcall AllocMsg(BOOL)
    {
        CMsg* p = m_Pool.Alloc();
        InterlockedIncrement(&p->m_lRefCount);
        return p;
    }
    virtual LONG __stdcall ReleaseMsg(CMsg* p)
    {
        if (p->m_lRefCount > 0 && InterlockedDecrement(&p->m_lRefCount) == 0)
            m_Pool.Free(p);
        return 0;
    }
    virtual int __stdcall SendMsg(DWORD, CMsg*) { return 0; }
    virtual BOOL __stdcall GetSessionPeerAddress(DWORD, std::string*, ULONG*, u_short*)
    {
        return FALSE;
    }
    virtual int __stdcall DisconnectSession(DWORD, DWORD) { return 0; }
    virtual void __stdcall ExcludeSessionFromCleanup(DWORD) {}
    virtual int __stdcall GetSessionSocketState(DWORD) { return 0; }
    virtual void __stdcall AddKeepAliveTarget(const char*, u_short, const char*, DWORD, DWORD) {}
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
    virtual bool __stdcall SetWinInetMaxConnections(DWORD) { return false; }
};

static CFakeNet g_Net;

static std::vector<std::string> DrainLogPrintf()
{
    std::vector<std::string> v;
    std::vector<CAPTURED_LOG> logs = LogCapture_Drain();
    for (size_t i = 0; i < logs.size(); ++i)
    {
        if (logs[i].dwFlags == 0x2000000)
            v.push_back(logs[i].strText);
    }
    return v;
}

// ---------------------------------------------------------------------------------------------
// BSLib DB without a server

static void TestDatabaseWithoutServer()
{
    // 4 connections per pool (2008: 30)
    CHECK(DB_SESSION_COUNT == 4);
    CHECK(DBPARAM_VARCHAR == 6);
    C_ASSERT(sizeof(DB_PARAM) == 0x18);
    C_ASSERT(FIELD_OFFSET(DB_PARAM, pszValue) == 8);

    // the registry exists only between InitEnvironment and DestroyAllSessions
    CHECK(g_pSetDBSessionMgr == NULL && g_hDBEnv == NULL);
    // without a registry DestroyAllSessions does nothing, not even free the environment
    CHECK(SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &g_hDBEnv)));
    SQLHENV hEnv = g_hDBEnv;
    CDatabase::DestroyAllSessions();
    CHECK(g_hDBEnv == hEnv && g_pSetDBSessionMgr == NULL);
    SQLFreeHandle(SQL_HANDLE_ENV, g_hDBEnv);
    g_hDBEnv = NULL;

    CHECK(CDatabase::InitEnvironment("DRIVER={No Such Driver For This Test};"));
    CHECK(g_pSetDBSessionMgr != NULL && g_hDBEnv != NULL);
    std::set<CDBSessionMgr*>* pSet = g_pSetDBSessionMgr;
    // a second call keeps the registry
    CHECK(CDatabase::InitEnvironment("DRIVER={No Such Driver For This Test};"));
    CHECK(g_pSetDBSessionMgr == pSet);
    CHECK(CDatabase::CreateSession(NULL) == NULL);
    CHECK(g_pSetDBSessionMgr->empty());

    // a failed open frees its handles; the pool had 4 slots
    CDBSessionMgr pool;
    CHECK(!pool.Open("DRIVER={Another Missing Driver};"));
    CHECK(pool.m_nCount == 4 && pool.m_phDbc == NULL);
    CDatabase::DestroySession(&pool);       // not registered: nothing happens
    CHECK(pool.m_nCount == 4);

    // HandleSQLError: the first line has the result code
    s_vecNetLog.clear();
    CDatabase::HandleSQLError(SQL_NO_DATA, NULL, "select x");
    CHECK(s_vecNetLog.size() == 1
          && s_vecNetLog[0] == "Query Error!!! Query Result : 100, Query : select x");
    s_vecNetLog.clear();
    CDatabase::HandleSQLError(SQL_ERROR, NULL, NULL);
    CHECK(s_vecNetLog.size() >= 3
          && s_vecNetLog[0] == "Query Error!!! Query Result : -1, Query : [Unknown query]");
    CHECK(s_vecNetLog.size() >= 3 && s_vecNetLog[2] == "[Unknown query]");

    // query object without a connection: the statement handle starts NULL and is not freed
    CDBQuery* pQuery = new CDBQuery;
    memset(pQuery, 0, sizeof(*pQuery));
    CHECK(pQuery->ExecuteFormat("exec _Proc %d", 7) == -1);
    std::vector<std::string> logs = DrainLogPrintf();
    CHECK(logs.size() == 1 && logs[0] == "cannot occupy db session : exec _Proc 7");
    delete pQuery;

    // DestroyAllSessions deletes the registry
    CDatabase::DestroyAllSessions();
    CHECK(g_pSetDBSessionMgr == NULL && g_hDBEnv == NULL);
    // and returns at once now
    CDatabase::DestroyAllSessions();
    CHECK(g_pSetDBSessionMgr == NULL);
}

// ---------------------------------------------------------------------------------------------
// BSLib Util: CDateTime::Format (new in the January 2011 library, no define)

static void TestDateTimeFormat()
{
    CDateTime dt(2011, 1, 24, 5, 6, 7);
    CHECK(dt.Format() == "2011-01-24 05:06:07");
    CDateTime dt2(1999, 12, 31, 23, 59, 58);
    CHECK(dt2.Format() == "1999-12-31 23:59:58");
    CHECK(dt2.GetYear() == 1999 && dt2.GetSecond() == 58);
}

// ---------------------------------------------------------------------------------------------
// optional checks against a temporary Access database

#pragma pack(push, 1)
struct QUERY_ROW
{
    int     nId;
    char    szName[20];
};
#pragma pack(pop)

static void TestWithAccessDriver()
{
    char szDir[MAX_PATH];
    GetTempPathA(MAX_PATH, szDir);
    char szPattern[MAX_PATH];
    wsprintfA(szPattern, "%sFramework201101Test_*.?db", szDir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(szPattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            char szOld[MAX_PATH];
            wsprintfA(szOld, "%s%s", szDir, fd.cFileName);
            DeleteFileA(szOld);
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    char szFile[MAX_PATH];
    wsprintfA(szFile, "%sFramework201101Test_%lu.mdb", szDir, GetCurrentProcessId());
    DeleteFileA(szFile);
    char szAttr[MAX_PATH + 32];
    int nLen = wsprintfA(szAttr, "CREATE_DB=\"%s\" General", szFile);
    szAttr[nLen + 1] = 0;
    if (!SQLConfigDataSource(NULL, ODBC_ADD_DSN, "Microsoft Access Driver (*.mdb)", szAttr))
    {
        printf("Framework201101Test: no 32-bit Access ODBC driver, database checks skipped\n");
        return;
    }

    char szConnect[MAX_PATH + 64];
    wsprintfA(szConnect, "Driver={Microsoft Access Driver (*.mdb)};Dbq=%s;", szFile);
    CHECK(CDatabase::InitEnvironment(szConnect));
    CDBSessionMgr* pSession = CDatabase::CreateSession(NULL);
    CHECK(pSession != NULL);
    if (!pSession)
    {
        CDatabase::DestroyAllSessions();
        return;
    }
    CHECK(g_pSetDBSessionMgr->size() == 1 && pSession->m_nCount == 4);
    for (int i = 0; i < 4; ++i)
        CHECK(CDBSessionMgr::GetConnection(pSession, i) != NULL);
    CHECK(CDBSessionMgr::GetConnection(pSession, 4) == NULL);

    CHECK(CDatabase::ExecDirect("CREATE TABLE T (id INTEGER, name VARCHAR(20))", pSession, 0)
          == SQL_SUCCESS);

    // string parameter (type 6)
    DBParamList params;
    DB_PARAM p;
    memset(&p, 0, sizeof(p));
    p.nIOType = SQL_PARAM_INPUT;
    p.nType = DBPARAM_INTEGER;
    p.nValue = 1;
    params.push_back(p);
    char szName[] = "january";
    p.nType = DBPARAM_VARCHAR;
    p.pszValue = szName;
    p.cbValue = 7;
    params.push_back(p);
    CHECK(CDatabase::ExecDirectWithParams(pSession, 0, "INSERT INTO T (id, name) VALUES (?, ?)",
                                          &params) == SQL_SUCCESS);

    CDBQuery* pQuery = new CDBQuery;
    memset(pQuery, 0, sizeof(*pQuery));
    QUERY_ROW row;
    memset(&row, 0, sizeof(row));
    pQuery->m_pSession = pSession;
    pQuery->m_nSession = 0;
    pQuery->m_pBuffer = (BYTE*)&row;
    pQuery->m_nBufferSize = sizeof(row);
    pQuery->m_nRowSize = sizeof(row);
    CHECK(pQuery->ExecuteFormat("SELECT id, name FROM T") == 0);
    CHECK(row.nId == 1 && memcmp(row.szName, "january", 7) == 0);

    // the query text reaches HandleSQLError (2008: "[Unknown query]")
    s_vecNetLog.clear();
    CHECK(pQuery->ExecuteFormat("SELECT * FROM NoSuchTable") == -2);
    std::vector<std::string> logs = DrainLogPrintf();
    CHECK(logs.size() == 1 && logs[0] == "execute query error : SELECT * FROM NoSuchTable");
    CHECK(s_vecNetLog.size() >= 3 && s_vecNetLog[0] == "Query Error!!! Query Result : -1, "
                                                       "Query : SELECT * FROM NoSuchTable");
    CHECK(s_vecNetLog.size() >= 3 && s_vecNetLog[2] == "SELECT * FROM NoSuchTable");
    delete pQuery;

    CDatabase::DestroySession(pSession);
    CHECK(g_pSetDBSessionMgr != NULL && g_pSetDBSessionMgr->empty());
    CDatabase::DestroyAllSessions();
    CHECK(g_hDBEnv == NULL && g_pSetDBSessionMgr == NULL);
    // the file stays in use until the process ends (DB-001)
    DeleteFileA(szFile);
}

int main()
{
    CNetEngine engine;
    engine.m_Config.pfnLog = CaptureNetLog;
    g_pBSNet = &g_Net;
    LogCapture_Start();

    TestDateTimeFormat();
    TestDatabaseWithoutServer();
    TestWithAccessDriver();

    LogCapture_Stop();
    g_pBSNet = NULL;
    return TEST_RESULT("Framework201101Test");
}
