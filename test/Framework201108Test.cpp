// The August 2011 framework of SR_ShardManager (JMX_FRAMEWORK_2011_08, the "August 2011
// (SR_ShardManager)" table of JMX_ServerFramework\FRAMEWORK_2011.md). The test is compiled with
// SHARD_DEFS and links the August builds of the libraries (SHARD_LIBS); it checks the behaviour
// that differs from the July and January 2011 code Framework2011Test and Framework201101Test
// check.

#include "BSLib/ChunkAllocator.h"
#include "BSLib/DB/CustomAllocator.h"
#include "BSLib/DB/Database.h"
#include "BSNet/Msg.h"
#include "BSNet/NetEngine.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/ServerFramework.h"
#include "ServerFramework/ServerProcess.h"

#include <odbcinst.h>
#include <stdio.h>
#include <string.h>

#include "TestCheck.h"

#pragma comment(lib, "odbccp32.lib")

#ifndef JMX_FRAMEWORK_2011_08
#error Framework201108Test is compiled with JMX_FRAMEWORK_2011_08 (see SHARD_DEFS in the Makefile)
#endif

// ---------------------------------------------------------------------------------------------
// build

struct POOL_ITEM
{
    DWORD dw;
};

static void TestBuild()
{
    // The exe has the single module string "SR_ShardManager".
    CHECK(strcmp(SERVER_MODULE_NAME, "SR_ShardManager") == 0);
    // The July 2011 pools (named, 0x5C / 0x98 bytes).
    CChunkAllocatorST<POOL_ITEM> st(NULL, 0, 1000, "pool");
    CHECK(st.m_strName == "pool" && st.m_nItemsPerBlock == 1000);
    C_ASSERT(sizeof(CChunkAllocatorST<POOL_ITEM>) == 0x5C);
    C_ASSERT(sizeof(CChunkAllocatorMT<POOL_ITEM>) == 0x98);
}

// ---------------------------------------------------------------------------------------------
// BSLib DB without a server

static void TestSessionCount()
{
    CHECK(CDatabase::InitEnvironment("DRIVER={No Such Driver For This Test};"));
    // the number of connections is a parameter (January 2011: always 4)
    CDBSessionMgr pool;
    CHECK(!pool.Open("DRIVER={Another Missing Driver};", 0));
    CHECK(pool.m_nCount == 0 && pool.m_phDbc == NULL);
    CHECK(!pool.Open("DRIVER={Another Missing Driver};", -2));
    CHECK(pool.m_nCount == 0 && pool.m_phDbc == NULL);
    CHECK(!pool.Open("DRIVER={Another Missing Driver};", 3));
    CHECK(pool.m_nCount == 3 && pool.m_phDbc == NULL);
    CHECK(CDatabase::CreateSession(2, NULL) == NULL);
    CHECK(CDatabase::CreateSession(0, NULL) == NULL);
    CHECK(g_pSetDBSessionMgr != NULL && g_pSetDBSessionMgr->empty());
    CDatabase::DestroyAllSessions();
    CHECK(g_pSetDBSessionMgr == NULL && g_hDBEnv == NULL);
}

// ---------------------------------------------------------------------------------------------
// BSLib DB records: the default data slots, the dirty bits, Update on connection 0

struct SHARD_ROW
{
    int     nId;        // +0
    short   sScore;     // +4
};

static CDBTableInfo* s_pShardInfo = NULL;

// The row follows the CDBRecord part (CDBTableInfo::m_nDataOffset 24); InitData and GetData are
// the defaults of the August library.
class CShardRecord : public CDBRecord
{
public:
    virtual const char* GetTableName() { return "T"; }
    virtual CDBTableInfo* GetTableInfo() { return s_pShardInfo; }
    virtual void AllocData(int) {}
    virtual void SetField(const DBColumnInfo*, const void*) {}

    SHARD_ROW m_row;    // +18
};

C_ASSERT(FIELD_OFFSET(CShardRecord, m_row) == 24);

class CShardStream : public CDBRecStream
{
public:
    virtual const char* GetTableName() { return "T"; }
    virtual CDBTableInfo* GetTableInfo() { return s_pShardInfo; }
    virtual void AllocData(int) {}
    virtual void SetField(const DBColumnInfo*, const void*) {}
};

static DBColumnInfo MakeColumn(const char* pszName, int nType, int nOffset)
{
    DBColumnInfo col;
    col.strName = pszName;
    col.nType = nType;
    col.nOffset = nOffset;
    col.strFormat = DB_ColumnTypeFormat(nType);
    return col;
}

// T: key id, group 2 score.
static void FillShardInfo(CDBTableInfo& info)
{
    info.m_pRecordClass = NULL;
    info.m_strTable = "T";
    info.m_dwFlags = DBTABLE_UPDATE;
    info.m_nRowSize = 0;
    info.m_nColumns = 0;
    info.m_pIdentity = NULL;
    info.m_pTable = NULL;
    info.m_nDataOffset = FIELD_OFFSET(CShardRecord, m_row);
    info.m_mapGroups.clear();
    info.m_mapGroups[DBGROUP_KEY].vecColumns.push_back(MakeColumn("id", DBTYPE_INT, 0));
    info.m_mapGroups[2].vecColumns.push_back(MakeColumn("score", DBTYPE_SHORT, 4));
    info.BuildSqlTemplates(0);
}

static void TestRecords()
{
    C_ASSERT(FIELD_OFFSET(CDBTableInfo, m_nDataOffset) == 0x104);
    CDBTableInfo info;
    FillShardInfo(info);
    s_pShardInfo = &info;

    CShardRecord rec;
    rec.m_row.nId = 7;
    rec.m_row.sScore = -3;
    // the default InitData clears the dirty bits, GetData is the record plus the data offset
    rec.m_qwDirty = 5;
    rec.InitData();
    CHECK(rec.m_qwDirty == 0);
    CHECK(rec.GetData() == (BYTE*)&rec.m_row);

    std::string str;
    rec.m_qwDirty = 4;
    CHECK(rec.BuildUpdate(str, 2) == 1 && str == "UPDATE T SET score=-3 WHERE id=7");
    CHECK(rec.m_qwDirty == 0);

    // ClearDirty: from 64 on an assert (no change); a negative group clears the bit of its low
    // byte (January 2011: both ignored)
    rec.m_qwDirty = 4;
    rec.ClearDirty(64);
    rec.ClearDirty(300);
    CHECK(rec.m_qwDirty == 4);
    rec.ClearDirty(-254);
    CHECK(rec.m_qwDirty == 0);
    rec.m_qwDirty = 2;
    rec.ClearDirty(-1);
    CHECK(rec.m_qwDirty == 2);

    // Update: built, then no statement (no pool)
    CDBTable table;
    rec.m_qwDirty = 4;
    CHECK(rec.Update(&table, 2) == -3);
    CHECK(rec.m_qwDirty == 0);
    CHECK(rec.Update(&table, 2) == 0);

    // the stream record's defaults: the buffer at +18, InitData clears +1C, +20 and +28
    CShardStream stream;
    BYTE abBuffer[4];
    stream.m_pBuffer = abBuffer;
    stream.m_nReadPos = 1;
    stream.m_nWritePos = 2;
    stream.m_nCapacity = 3;
    stream.m_nFieldCount = 4;
    stream.m_qwDirty = 6;
    stream.InitData();
    CHECK(stream.m_nReadPos == 0 && stream.m_nWritePos == 0 && stream.m_nFieldCount == 0);
    CHECK(stream.m_nCapacity == 3 && stream.m_qwDirty == 6);
    CHECK(stream.GetData() == abBuffer);
    stream.m_pBuffer = NULL;
    CHECK(stream.GetData() == NULL);

    // ExecDirectWithReturnValue without text / statement
    CHECK(CDatabase::ExecDirectWithReturnValue(NULL, 0, NULL, 0, NULL) == -1);
    CHECK(CDatabase::ExecDirectWithReturnValue(NULL, 0, "", DBEXEC_RETURN_INT, NULL) == -1);
    CHECK(CDatabase::ExecDirectWithReturnValue(NULL, 0, "select 1", 0, NULL) == -3);
    s_pShardInfo = NULL;
}

// ---------------------------------------------------------------------------------------------
// BSLib DB: the record allocator template (one instance per record type in SR_ShardManager)

typedef CCustomAllocator<CShardRecord, CChunkAllocatorMT<CShardRecord> > CShardAllocMT;
typedef CCustomAllocator<CShardRecord, CChunkAllocatorST<CShardRecord> > CShardAllocST;

static void TestRecordAllocator()
{
    C_ASSERT(sizeof(CShardAllocMT) == 0xA0);
    C_ASSERT(sizeof(CShardAllocST) == 0x64);
    C_ASSERT(FIELD_OFFSET(CShardAllocMT, m_Pool) == 8);
    C_ASSERT(FIELD_OFFSET(CShardAllocMT, m_Pool.m_cs) == 8 + 0x5C);

    CDBTableInfo info;
    FillShardInfo(info);
    s_pShardInfo = &info;
    CDBTable table;

    // the pool is named after the table, 1000 records per block
    CShardAllocMT* pMT = new CShardAllocMT(&table, &info);
    CHECK(pMT->m_pTable == &table);
    CHECK(pMT->m_Pool.m_strName == "T" && pMT->m_Pool.m_nItemsPerBlock == 1000);
    CHECK(pMT->m_Pool.m_pfnInit == NULL && pMT->m_Pool.m_dwInitParam == 0);
    CShardAllocST st(&table, NULL);
    CHECK(st.m_Pool.m_strName == "Unknown DB Table" && st.m_Pool.m_nItemsPerBlock == 1000);

    // through the base class slots
    _record_allocator* pAlloc = pMT;
    DWORD dwTotal = 9;
    DWORD dwUsed = 9;
    pAlloc->vf03(&dwTotal, &dwUsed);
    CHECK(dwTotal == 0 && dwUsed == 0);
    CDBRecord* p1 = pAlloc->AllocRecord();
    CDBRecord* p2 = pAlloc->AllocRecord();
    CHECK(p1 != NULL && (CShardRecord*)p2 == (CShardRecord*)p1 + 1);
    pAlloc->vf03(&dwTotal, &dwUsed);
    CHECK(dwTotal == 1000 && dwUsed == 2);
    pAlloc->FreeRecord(p1);
    pAlloc->vf03(&dwTotal, &dwUsed);
    CHECK(dwTotal == 1000 && dwUsed == 1);
    // the freed record goes to the end of the free list
    CHECK(pMT->m_Pool.m_FreeQue.GetSize() == 999 && pMT->m_Pool.m_FreeQue.m_Que.back() == p1);
    // NULL asserts and changes nothing
    pAlloc->FreeRecord(NULL);
    pAlloc->vf03(&dwTotal, &dwUsed);
    CHECK(dwUsed == 1 && pMT->m_Pool.m_FreeQue.GetSize() == 999);

    // a full free list drops the record but counts it as returned
    CDBRecord* p3 = st.AllocRecord();
    CHECK(p3 != NULL);
    size_t nFree = st.m_Pool.m_FreeQue.GetSize();
    st.m_Pool.m_FreeQue.m_nMaxSize = (LONG)nFree;
    st.FreeRecord(p3);
    st.vf03(&dwTotal, &dwUsed);
    CHECK(dwTotal == 1000 && dwUsed == 0 && st.m_Pool.m_FreeQue.GetSize() == nFree);

    // the table creates and frees its records through the allocator
    CHECK(table.Open(&info, NULL, pMT) == 1 && table.m_pAllocator == pMT);
    CDBRecord* pRec = table.CreateRecord();
    CHECK((CShardRecord*)pRec == (CShardRecord*)p2 + 1 && pRec->m_pOwner == &table && pRec->m_qwDirty == 0);
    pAlloc->vf03(&dwTotal, &dwUsed);
    CHECK(dwUsed == 2);
    table.FreeRecord(pRec);
    CHECK(pRec->m_pOwner == NULL);
    pAlloc->vf03(&dwTotal, &dwUsed);
    CHECK(dwUsed == 1);
    // the table deletes the allocator
    table.Close();
    CHECK(table.m_pAllocator == NULL);
    s_pShardInfo = NULL;
}

// ---------------------------------------------------------------------------------------------
// BSNet: CMsg::CopyPayloadFrom (new, no define)

static void TestMsgCopy()
{
    CMsg src;
    src.Reset();
    src.SetOpcode(0x3510);
    DWORD dw = 0x11223344;
    WORD w = 0x5566;
    src.Write(dw);
    src.Write(w);
    src.m_wReadPos = MSG_HEADER_SIZE + 4;

    // mode 1: the unread part; the source is read to the end
    CMsg dst;
    dst.Reset();
    BYTE b = 0x77;
    dst.Write(b);
    dst.CopyPayloadFrom(&src, 1);
    CHECK(dst.GetPayloadSize() == 3 && dst.m_pData[6] == 0x77);
    CHECK(*(WORD*)(dst.m_pData + 7) == 0x5566);
    CHECK(src.m_wReadPos == src.m_wWritePos && src.GetPayloadSize() == 6);
    // nothing unread: nothing appended
    dst.CopyPayloadFrom(&src, 1);
    CHECK(dst.GetPayloadSize() == 3);

    // other modes: the whole payload, also when it was read already
    CMsg whole;
    whole.Reset();
    whole.CopyPayloadFrom(&src, 0);
    CHECK(whole.GetPayloadSize() == 6 && *(DWORD*)(whole.m_pData + 6) == 0x11223344);
    CHECK(*(WORD*)(whole.m_pData + 10) == 0x5566 && whole.GetOpcode() == 0);

    // no room: (int)0 thrown after the source was marked as read
    CMsg full;
    full.Reset();
    full.m_wWritePos = (WORD)(full.m_nCapacity - 2);
    src.m_wReadPos = MSG_HEADER_SIZE;
    int nThrown = -1;
    try
    {
        full.CopyPayloadFrom(&src, 1);
    }
    catch (int n)
    {
        nThrown = n;
    }
    CHECK(nThrown == 0 && src.m_wReadPos == src.m_wWritePos);
    CHECK(full.m_wWritePos == full.m_nCapacity - 2);
}

// The forward-only reads crash on a short packet (no C++ objects here because of __try).
static BOOL ShortForwardReadCrashes(CMsg* pMsg)
{
    __try
    {
        pMsg->ReadForwardOnly<DWORD>();
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                : EXCEPTION_CONTINUE_SEARCH)
    {
        return TRUE;
    }
    return FALSE;
}

static void TestMsgForwardOnly()
{
    CMsg msg;
    msg.Reset();
    DWORD dw = 0x01020304;
    WORD w = 0x0506;
    BYTE b = 7;
    msg.Write(dw);
    msg.Write(w);
    msg.Write(b);
    // the direction flag is ignored
    msg.m_bReadForward = 0;
    CHECK(msg.ReadForwardOnly<DWORD>() == 0x01020304);
    CHECK(msg.ReadForwardOnly<WORD>() == 0x0506);
    CHECK(msg.ReadForwardOnly<BYTE>() == 7);
    CHECK(msg.m_wReadPos == msg.m_wWritePos && msg.GetPayloadSize() == 7);
    CHECK(ShortForwardReadCrashes(&msg));
    CHECK(msg.m_wReadPos == msg.m_wWritePos);
}

// ---------------------------------------------------------------------------------------------
// ServerFramework: the 0x2209 relay header (new, no define)

static void TestRelayHeader()
{
    CNetEngine engine;
    IBSNet* pOld = g_pBSNet;
    g_pBSNet = &engine;
    CMsg* pMsg = ServerFramework::Msg_Build2209(0x3013, 0x12345678);
    CHECK(pMsg != NULL);
    if (pMsg)
    {
        CHECK(pMsg->GetOpcode() == 0x2209 && pMsg->GetPayloadSize() == 6);
        CHECK(*(DWORD*)(pMsg->m_pData + 6) == 0x12345678);
        CHECK(*(WORD*)(pMsg->m_pData + 10) == 0x3013);
        g_pBSNet->ReleaseMsg(pMsg);
    }
    g_pBSNet = pOld;
}

// ---------------------------------------------------------------------------------------------
// ServerFramework: the division group SR_ShardManager's code asks for (new, no define)

static void TestDivisionGroup()
{
    DWORD dwOld = g_dwSelfDivisionID;
    int nIn = 0;
    for (DWORD dw = 0; dw < 64; ++dw)
    {
        g_dwSelfDivisionID = dw;
        BOOL bIn = SelfDivision_IsInGroup3();
        BOOL bExpected = dw == 3 || dw == 5 || dw == 9 || dw == 10 || dw == 26 || dw == 27
                         || dw == 34;
        CHECK(!bIn == !bExpected);
        if (bIn)
            ++nIn;
    }
    CHECK(nIn == 7);
    g_dwSelfDivisionID = dwOld;
}

// ---------------------------------------------------------------------------------------------
// optional checks against a temporary Access database

static char s_szAccessFile[MAX_PATH];

// Creates an empty .mdb and returns its connection string; FALSE without the 32-bit driver.
static BOOL CreateAccessDatabase(char* pszConnect)
{
    char szDir[MAX_PATH];
    GetTempPathA(MAX_PATH, szDir);
    char szPattern[MAX_PATH];
    wsprintfA(szPattern, "%sFramework201108Test_*.?db", szDir);
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

    wsprintfA(s_szAccessFile, "%sFramework201108Test_%lu.mdb", szDir, GetCurrentProcessId());
    DeleteFileA(s_szAccessFile);
    char szAttr[MAX_PATH + 32];
    int nLen = wsprintfA(szAttr, "CREATE_DB=\"%s\" General", s_szAccessFile);
    szAttr[nLen + 1] = 0;
    if (!SQLConfigDataSource(NULL, ODBC_ADD_DSN, "Microsoft Access Driver (*.mdb)", szAttr))
        return FALSE;
    wsprintfA(pszConnect, "Driver={Microsoft Access Driver (*.mdb)};Dbq=%s;", s_szAccessFile);
    return TRUE;
}

static void TestWithAccessDriver()
{
    char szConnect[MAX_PATH + 64];
    if (!CreateAccessDatabase(szConnect))
    {
        printf("Framework201108Test: no 32-bit Access ODBC driver, database checks skipped\n");
        return;
    }
    CHECK(CDatabase::InitEnvironment(szConnect));
    CDBSessionMgr* pSession = CDatabase::CreateSession(2, NULL);
    CHECK(pSession != NULL);
    if (!pSession)
    {
        CDatabase::DestroyAllSessions();
        return;
    }
    CHECK(g_pSetDBSessionMgr->size() == 1 && pSession->m_nCount == 2);
    CHECK(CDBSessionMgr::GetConnection(pSession, 0) != NULL);
    CHECK(CDBSessionMgr::GetConnection(pSession, 1) != NULL);
    CHECK(CDBSessionMgr::GetConnection(pSession, 2) == NULL);

    // ExecDirectWithReturnValue without an output parameter
    CHECK(CDatabase::ExecDirectWithReturnValue(pSession, 0, "CREATE TABLE T (id INTEGER, "
                                               "score SMALLINT)", 0, NULL) == SQL_SUCCESS);
    CHECK(CDatabase::ExecDirectWithReturnValue(pSession, 0, "INSERT INTO T VALUES (7, 1)",
                                               DBEXEC_RETURN_INT, NULL) == SQL_SUCCESS);
    CHECK(CDatabase::ExecDirectWithReturnValue(pSession, 0, "SELECT * FROM NoSuchTable", 0,
                                               NULL) == SQL_ERROR);
    CHECK(CDatabase::ExecDirectWithReturnValue(pSession, 2, "SELECT * FROM T", 0, NULL) == -3);

    // Update runs the statement on connection 0 (the Access driver shows the changes of one
    // connection to another only later, so everything here uses connection 0)
    CDBTableInfo info;
    FillShardInfo(info);
    s_pShardInfo = &info;
    CDBTable table;
    table.m_pSession = pSession;
    CShardRecord rec;
    rec.m_row.nId = 7;
    rec.m_row.sScore = -3;
    rec.m_qwDirty = 4;
    CHECK(rec.Update(&table, 2) == SQL_SUCCESS && rec.m_qwDirty == 0);
    SQLHSTMT hStmt = CDatabase::AllocStmt(pSession, 0);
    CHECK(hStmt != NULL);
    SQLSMALLINT sScore = 0;
    SQLINTEGER cb = 0;
    CHECK(SQL_SUCCEEDED(SQLExecDirect(hStmt, (SQLCHAR*)"SELECT score FROM T WHERE id=7",
                                      SQL_NTS)));
    CHECK(SQL_SUCCEEDED(SQLFetch(hStmt)));
    CHECK(SQL_SUCCEEDED(SQLGetData(hStmt, 1, SQL_C_SSHORT, &sScore, 0, &cb)) && sScore == -3);
    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    s_pShardInfo = NULL;

    CDatabase::DestroySession(pSession);
    CDatabase::DestroyAllSessions();
    CHECK(g_hDBEnv == NULL && g_pSetDBSessionMgr == NULL);
    // the file stays in use until the process ends (DB-001)
    DeleteFileA(s_szAccessFile);
}

int main()
{
    TestBuild();
    TestSessionCount();
    TestRecords();
    TestRecordAllocator();
    TestMsgCopy();
    TestDivisionGroup();
    TestMsgForwardOnly();
    TestRelayHeader();
    TestWithAccessDriver();
    return TEST_RESULT("Framework201108Test");
}
