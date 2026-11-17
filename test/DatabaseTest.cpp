// BSLib ODBC layer: type helpers, statement text building, record update text, connection and
// statement error paths, diagnostic logging. With the 32-bit Microsoft Access ODBC driver a
// temporary .mdb is created and queried; without it those checks are skipped.

#include "BSLib/DB/Database.h"
#include "BSLib/DB/DBQuery.h"
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

// ---------------------------------------------------------------------------------------------
// log capture: NetEngineLog (HandleSQLError) and LogPrintf (CDBQuery)

static std::vector<std::string> s_vecNetLog;

static int CaptureNetLog(DWORD dwFlags, const char* pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    const char* pszText = va_arg(args, const char*);
    va_end(args);
    if (dwFlags == 0x2000001)
        s_vecNetLog.push_back(pszText);
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
// test record: data laid out like the DB alignment rules expect

struct TEST_DATA
{
    int                 nId;        // 0
    std::string         strName;    // 4
    short               sScore;     // 32
    BYTE                byLevel;    // 34
    __int64             llExp;      // 40
    float               fRate;      // 48
    TIMESTAMP_STRUCT    ts;         // 52
};

C_ASSERT(FIELD_OFFSET(TEST_DATA, strName) == 4);
C_ASSERT(FIELD_OFFSET(TEST_DATA, sScore) == 32);
C_ASSERT(FIELD_OFFSET(TEST_DATA, byLevel) == 34);
C_ASSERT(FIELD_OFFSET(TEST_DATA, llExp) == 40);
C_ASSERT(FIELD_OFFSET(TEST_DATA, fRate) == 48);
C_ASSERT(FIELD_OFFSET(TEST_DATA, ts) == 52);

static CDBTableInfo* s_pRecordInfo = NULL;
static int s_nRecords = 0;
static int s_nAllocData = 0;

class CTestRecord : public CDBRecord
{
    BS_DECLARE_DYNCREATE(CTestRecord)

public:
    CTestRecord()
    {
        ++s_nRecords;
        m_data.nId = 0;
        m_data.sScore = 0;
        m_data.byLevel = 0;
        m_data.llExp = 0;
        m_data.fRate = 0;
        memset(&m_data.ts, 0, sizeof(m_data.ts));
    }
    virtual ~CTestRecord()
    {
        --s_nRecords;
    }

    virtual void Reserved02() {}
    virtual CDBTableInfo* GetTableInfo() { return s_pRecordInfo; }
    virtual void AllocData(int nRowSize) { s_nAllocData = nRowSize; }
    virtual void InitData() { m_data.nId = -1; }
    virtual BYTE* GetData() { return (BYTE*)&m_data; }
    virtual void SetField(const DBColumnInfo* pColumn, const void* pValue)
    {
        BYTE* p = (BYTE*)&m_data + pColumn->nOffset;
        switch (pColumn->nType)
        {
        case DBTYPE_BYTE:
            *p = *(const BYTE*)pValue;
            break;
        case DBTYPE_SHORT:
            *(short*)p = *(const short*)pValue;
            break;
        case DBTYPE_INT:
        case DBTYPE_FLOAT:
            *(int*)p = *(const int*)pValue;
            break;
        case DBTYPE_INT64:
            *(__int64*)p = *(const __int64*)pValue;
            break;
        case DBTYPE_STRING:
            ((std::string*)p)->assign((const char*)pValue);
            break;
        case DBTYPE_TIMESTAMP:
            memcpy(p, pValue, sizeof(TIMESTAMP_STRUCT));
            break;
        }
    }

public:
    TEST_DATA m_data;
};

BS_IMPLEMENT_DYNCREATE(CTestRecord, &CDBRecord::classCDBRecord)

static DBColumnInfo MakeColumn(const char* pszName, int nType, int nOffset)
{
    DBColumnInfo col;
    col.strName = pszName;
    col.nType = nType;
    col.nOffset = nOffset;
    col.strFormat = DB_ColumnTypeFormat(nType);
    return col;
}

// T: key id; group 2 name, score; group 3 level, exp, rate, ts.
static void FillTestInfo(CDBTableInfo& info, DWORD dwFlags)
{
    info.m_pRecordClass = &CTestRecord::classCTestRecord;
    info.m_strTable = "T";
    info.m_dwFlags = dwFlags;
    info.m_nRowSize = 0;
    info.m_nColumns = 0;
    info.m_pIdentity = NULL;
    info.m_pTable = NULL;
    info.m_mapGroups.clear();
    info.m_mapGroups[DBGROUP_KEY].vecColumns.push_back(MakeColumn("id", DBTYPE_INT, 0));
    info.m_mapGroups[DBGROUP_KEY].vecColumns[0].bIdentity = 1;
    info.m_mapGroups[2].vecColumns.push_back(MakeColumn("name", DBTYPE_STRING, 4));
    info.m_mapGroups[2].vecColumns.push_back(MakeColumn("score", DBTYPE_SHORT, 32));
    info.m_mapGroups[3].vecColumns.push_back(MakeColumn("level", DBTYPE_BYTE, 34));
    info.m_mapGroups[3].vecColumns.push_back(MakeColumn("exp", DBTYPE_INT64, 40));
    info.m_mapGroups[3].vecColumns.push_back(MakeColumn("rate", DBTYPE_FLOAT, 48));
    info.m_mapGroups[3].vecColumns.push_back(MakeColumn("ts", DBTYPE_TIMESTAMP, 52));
}

// Table whose fetch function records the statement text instead of running it.
class CCaptureTable : public CDBTable
{
public:
    int Capture(int nConn, const char* pszSql, CDBRecordList* pList)
    {
        m_nConn = nConn;
        m_strSql = pszSql;
        m_pList = pList;
        return 77;
    }

    int m_nConn;
    std::string m_strSql;
    CDBRecordList* m_pList;
};

// ---------------------------------------------------------------------------------------------

static void TestTypeHelpers()
{
    CHECK(DB_SqlTypeToColumnType(SQL_WVARCHAR) == DBTYPE_STRING);
    CHECK(DB_SqlTypeToColumnType(SQL_CHAR) == DBTYPE_STRING);
    CHECK(DB_SqlTypeToColumnType(SQL_VARCHAR) == DBTYPE_STRING);
    CHECK(DB_SqlTypeToColumnType(SQL_BIT) == DBTYPE_BYTE);
    CHECK(DB_SqlTypeToColumnType(SQL_TINYINT) == DBTYPE_BYTE);
    CHECK(DB_SqlTypeToColumnType(SQL_BIGINT) == DBTYPE_INT64);
    CHECK(DB_SqlTypeToColumnType(SQL_INTEGER) == DBTYPE_INT);
    CHECK(DB_SqlTypeToColumnType(SQL_SMALLINT) == DBTYPE_SHORT);
    CHECK(DB_SqlTypeToColumnType(SQL_FLOAT) == DBTYPE_DOUBLE);
    CHECK(DB_SqlTypeToColumnType(SQL_REAL) == DBTYPE_FLOAT);
    CHECK(DB_SqlTypeToColumnType(SQL_TYPE_TIMESTAMP) == DBTYPE_TIMESTAMP);
    CHECK(DB_SqlTypeToColumnType(SQL_WCHAR) == -1);
    CHECK(DB_SqlTypeToColumnType(SQL_LONGVARCHAR) == -1);
    CHECK(DB_SqlTypeToColumnType(SQL_DOUBLE) == -1);
    CHECK(DB_SqlTypeToColumnType(SQL_DECIMAL) == -1);

    static const SQLSMALLINT s_anCType[8] = { -28, -15, -16, -25, 7, -1, 1, 93 };
    static const int s_anAlign[8] = { 1, 2, 4, 8, 4, -1, 4, 2 };
    static const int s_anSize[8] = { 1, 2, 4, 8, 4, -1, 28, 16 };
    for (int i = 0; i < 8; ++i)
    {
        CHECK(DB_ColumnTypeToCType(i) == s_anCType[i]);
        CHECK(DB_ColumnTypeAlignment(i) == s_anAlign[i]);
        CHECK(DB_ColumnTypeSize(i) == s_anSize[i]);
    }
    CHECK(DB_ColumnTypeToCType(-1) == -1 && DB_ColumnTypeToCType(8) == -1);
    CHECK(DB_ColumnTypeAlignment(8) == -1 && DB_ColumnTypeSize(-1) == -1);
    CHECK(strcmp(DB_ColumnTypeFormat(0), "%u") == 0);
    CHECK(strcmp(DB_ColumnTypeFormat(1), "%d") == 0 && strcmp(DB_ColumnTypeFormat(2), "%d") == 0);
    CHECK(strcmp(DB_ColumnTypeFormat(3), "%I64d") == 0);
    CHECK(strcmp(DB_ColumnTypeFormat(4), "%f") == 0);
    CHECK(DB_ColumnTypeFormat(5) == NULL);
    CHECK(strcmp(DB_ColumnTypeFormat(6), "'%s'") == 0);
    CHECK(strcmp(DB_ColumnTypeFormat(7), "convert(datetime, '%d-%d-%d %d:%d:%d')") == 0);
    CHECK(DB_ColumnTypeFormat(-1) == NULL);

    CHECK(DB_SqlTypeToByteSize(SQL_INTEGER) == 4 && DB_SqlTypeToByteSize(SQL_REAL) == 4);
    CHECK(DB_SqlTypeToByteSize(SQL_SMALLINT) == 2);
    CHECK(DB_SqlTypeToByteSize(SQL_FLOAT) == 8 && DB_SqlTypeToByteSize(SQL_DOUBLE) == 8);
    CHECK(DB_SqlTypeToByteSize(SQL_BIGINT) == 8 && DB_SqlTypeToByteSize(SQL_TINYINT) == 1);
    CHECK(DB_SqlTypeToByteSize(SQL_TYPE_DATE) == 6 && DB_SqlTypeToByteSize(SQL_TYPE_TIME) == 6);
    CHECK(DB_SqlTypeToByteSize(SQL_TYPE_TIMESTAMP) == 16);
    CHECK(DB_SqlTypeToByteSize(SQL_CHAR) == 0 && DB_SqlTypeToByteSize(SQL_BIT) == 0);

    char sz[8] = "a  b   ";
    DB_TrimRightSpaces(sz, 7);
    CHECK(memcmp(sz, "a  b\0\0\0", 8) == 0);
    char szBlank[4] = "   ";
    DB_TrimRightSpaces(szBlank, 3);
    CHECK(szBlank[0] == ' ' && szBlank[1] == 0 && szBlank[2] == 0);

    DBColumnInfo col;
    CHECK(col.nType == -1 && col.nOffset == 0 && col.nSize == 0 && col.nBufOffset == 0);
    CHECK(col.bNullable == 0 && col.nOrdinal == -1 && col.bIdentity == 0);
    CHECK(col.strName.empty() && col.strFormat.empty());

    // the scaled clock stays inside one scaled day
    int nTime = Time_GetScaledTimeOfDay();
    CHECK(nTime >= 0 && nTime <= (int)(86400000.0 / 3.3333330154418945) + 1);
}

static void TestStatementTemplates()
{
    CDBTableInfo info;
    FillTestInfo(info, DBTABLE_UPDATE | DBTABLE_INSERT | DBTABLE_DELETE);
    info.BuildSqlTemplates(0);

    CHECK(info.m_strKeyWhere == "WHERE id=%d");
    CHECK(info.m_mapGroups[2].strUpdate == "UPDATE T SET name='%s',score=%d WHERE id=%d");
    CHECK(info.m_mapGroups[2].strSet == "name='%s',score=%d");
    CHECK(info.m_mapGroups[3].strUpdate
          == "UPDATE T SET level=%u,exp=%I64d,rate=%f,"
             "ts=convert(datetime, '%d-%d-%d %d:%d:%d') WHERE id=%d");
    CHECK(info.m_mapGroups[3].strSet
          == "level=%u,exp=%I64d,rate=%f,ts=convert(datetime, '%d-%d-%d %d:%d:%d')");
    CHECK(info.m_mapGroups[DBGROUP_KEY].strUpdate.empty());
    CHECK(info.m_strUpdateAll
          == "UPDATE T SET name='%s',score=%d,level=%u,exp=%I64d,rate=%f,"
             "ts=convert(datetime, '%d-%d-%d %d:%d:%d') WHERE id=%d");
    // the identity column is left out
    CHECK(info.m_strInsert
          == "INSERT INTO T(name,score,level,exp,rate,ts ) VALUES('%s',%d,%u,%I64d,%f,"
             "convert(datetime, '%d-%d-%d %d:%d:%d') )");
    CHECK(info.m_nColumns == 7);
    CHECK(info.m_strSelect == "SELECT id,name,score,level,exp,rate,ts FROM T WHERE id=%d");
    CHECK(info.m_strSelectNoLock
          == "SELECT id,name,score,level,exp,rate,ts FROM T WITH (NOLOCK) WHERE id=%d");
    CHECK(info.m_strDelete == "DELETE FROM T");
    CHECK(info.m_strCount == "SELECT COUNT(id) FROM T WHERE id=%d");

    // all table columns declared: SELECT *
    info.BuildSqlTemplates(7);
    CHECK(info.m_strSelect == "SELECT * FROM T WHERE id=%d");
    CHECK(info.m_strSelectNoLock == "SELECT * FROM T WITH (NOLOCK) WHERE id=%d");

    // without key columns: no WHERE, no UPDATE / COUNT text; flags off: no INSERT / DELETE
    CDBTableInfo plain;
    FillTestInfo(plain, 0);
    plain.m_mapGroups.erase(DBGROUP_KEY);
    plain.BuildSqlTemplates(0);
    CHECK(plain.m_strKeyWhere.empty() && plain.m_strUpdateAll.empty());
    CHECK(plain.m_mapGroups[2].strUpdate.empty() && plain.m_strCount.empty());
    CHECK(plain.m_strInsert.empty() && plain.m_strDelete.empty());
    CHECK(plain.m_strSelect == "SELECT name,score,level,exp,rate,ts FROM T");
    CHECK(plain.m_strSelectNoLock == "SELECT name,score,level,exp,rate,ts FROM T WITH (NOLOCK)");
    CHECK(plain.FindKeyGroup() == NULL && info.FindKeyGroup() == &info.m_mapGroups[0]);

    // a single group always selects *
    CDBTableInfo one;
    FillTestInfo(one, DBTABLE_UPDATE);
    one.m_mapGroups.erase(2);
    one.m_mapGroups.erase(3);
    one.BuildSqlTemplates(0);
    CHECK(one.m_strSelect == "SELECT * FROM T WHERE id=%d");
    CHECK(one.m_strUpdateAll == "UPDATE T SET WHERE id=%d");
}

static void TestRecordUpdate()
{
    CDBTableInfo info;
    FillTestInfo(info, DBTABLE_UPDATE);
    info.BuildSqlTemplates(0);
    s_pRecordInfo = &info;

    CTestRecord rec;
    CHECK(strcmp(rec.GetRuntimeClass()->m_lpszClassName, "CTestRecord") == 0);
    CHECK(CDBRecord::classCDBRecord.m_nObjectSize == 24);
    CHECK(CDBRecord::classCDBRecord.m_pfnCreateObject == NULL);
    CHECK(CDBRecord::classCDBRecord.m_pBaseClass == &CBase::classCBase);
    CHECK(CDBRecStream::classCDBRecStream.m_nObjectSize == 48);
    CHECK(CDBRecStream::classCDBRecStream.m_pBaseClass == &CDBRecord::classCDBRecord);
    CHECK(strcmp(CDBRecStream::classCDBRecStream.m_lpszClassName, "CDBRecStream") == 0);

    rec.m_data.nId = 7;
    rec.m_data.strName = "bob";
    rec.m_data.sScore = -3;
    rec.m_data.byLevel = 200;
    rec.m_data.llExp = 1234567890123;
    rec.m_data.fRate = 1.5f;
    TIMESTAMP_STRUCT ts = { 2008, 3, 4, 5, 6, 7, 0 };
    rec.m_data.ts = ts;

    // dirty bits use the low byte of the group number
    rec.m_qwDirty = 0;
    CHECK(!rec.IsDirty(2));
    rec.m_qwDirty = (1 << 2) | (1 << 3);
    CHECK(rec.IsDirty(2) && rec.IsDirty(3) && !rec.IsDirty(4));
    CHECK(rec.IsDirty(256 + 2) && !rec.IsDirty(64) && !rec.IsDirty(-1));
    rec.ClearDirty(64);
    rec.ClearDirty(-1);
    CHECK(rec.m_qwDirty == 12);

    std::string str = "old";
    CHECK(rec.BuildUpdate(str, 0) == 0 && str.empty());
    CHECK(rec.BuildUpdate(str, 1) == 0);
    CHECK(rec.BuildUpdate(str, 4) == 0);

    CHECK(rec.BuildUpdate(str, 2) == 1);
    CHECK(str == "UPDATE T SET name='bob',score=-3 WHERE id=7");
    CHECK(rec.m_qwDirty == 8);
    CHECK(rec.BuildUpdate(str, 2) == 0 && str.empty());

    rec.m_qwDirty = 12;
    CHECK(rec.BuildUpdate(str, -1) == 1);
    CHECK(str == "UPDATE T SET name='bob',score=-3,level=200,exp=1234567890123,rate=1.500000,"
                 "ts=convert(datetime, '2008-3-4 5:6:7') WHERE id=7");
    CHECK(rec.m_qwDirty == 0);
    // nothing dirty: success with empty text
    CHECK(rec.BuildUpdate(str, -1) == 1 && str.empty());

    rec.m_qwDirty = 8;
    CHECK(rec.BuildUpdate(str, 3) == 1);
    CHECK(str == "UPDATE T SET level=200,exp=1234567890123,rate=1.500000,"
                 "ts=convert(datetime, '2008-3-4 5:6:7') WHERE id=7");

    // only bits of groups without SET text: the trailing blank of "SET " stays
    rec.m_qwDirty = 2;
    CHECK(rec.BuildUpdate(str, -1) == 1 && str == "UPDATE T SET WHERE id=7");

    // update flag off / no key group
    info.m_dwFlags = 0;
    rec.m_qwDirty = 4;
    CHECK(rec.BuildUpdate(str, 2) == 0 && rec.m_qwDirty == 4);
    info.m_dwFlags = DBTABLE_UPDATE;
    std::vector<DBColumnInfo> vecKey = info.m_mapGroups[0].vecColumns;
    info.m_mapGroups.erase(0);
    CHECK(rec.BuildUpdate(str, 2) == 0);
    info.m_mapGroups[0].vecColumns = vecKey;

    // Update without a pool: built, then no statement
    CDBTable table;
    CHECK(rec.Update(&table, 0, 2) == -3);
    CHECK(rec.m_qwDirty == 0);
    CHECK(rec.Update(&table, 0, 2) == 0);

    rec.ClearAllDirty();
    rec.SetOwner(&table);
    CHECK(rec.GetOwner() == &table);
    rec.SetOwner(NULL);
    s_pRecordInfo = NULL;
}

static void TestTableBinding()
{
    CDBTableInfo info;
    FillTestInfo(info, DBTABLE_UPDATE | DBTABLE_DELETE);
    info.m_nRowSize = 123;
    info.BuildSqlTemplates(0);

    CCaptureTable table;
    CHECK(table.m_pSession == NULL && table.m_pInfo == NULL && table.m_pAllocator == NULL);
    CHECK(table.m_pfnFetch == NULL);
    // no pool: bound, nothing described
    CHECK(table.Open(&info, NULL, NULL) == 1);
    CHECK(table.GetInfo() == &info && info.m_pTable == &table && table.m_pAllocator != NULL);
    CHECK(table.m_pAllocator->m_pTable == &table);
    DWORD dw1 = 5, dw2 = 6;
    table.m_pAllocator->vf03(&dw1, &dw2);
    CHECK(dw1 == 0 && dw2 == 0);

    // records come from the record class of the description
    s_pRecordInfo = &info;
    CDBRecord* pRec = table.CreateRecord();
    CHECK(pRec && s_nRecords == 1 && s_nAllocData == 123);
    CHECK(pRec->GetOwner() == &table && ((CTestRecord*)pRec)->m_data.nId == -1);
    // a record of another table is freed by that table
    CCaptureTable other;
    other.Open(&info, NULL, NULL);
    info.m_pTable = &table;
    CDBRecord* pRec2 = other.CreateRecord();
    CHECK(s_nRecords == 2);
    table.FreeRecord(pRec2);
    CHECK(s_nRecords == 1 && pRec2 != NULL);
    table.FreeRecord(pRec);
    CHECK(s_nRecords == 0);
    table.FreeRecord(NULL);

    // statement text of the select functions
    table.m_pfnFetch = static_cast<CDBTable::PFN_FETCH>(&CCaptureTable::Capture);
    CDBRecordList list;
    CHECK(table.SelectAll(4, &list) == 77);
    CHECK(table.m_strSql == "SELECT id,name,score,level,exp,rate,ts FROM T ");
    CHECK(table.m_nConn == 4 && table.m_pList == &list);
    CHECK(table.SelectWhere("score > 3", 1, &list, 1) == 77);
    CHECK(table.m_strSql == "SELECT id,name,score,level,exp,rate,ts FROM T WITH (NOLOCK)  score > 3");
    table.m_strSql = "-";
    CHECK(table.SelectWhere("", 1, &list, 1) == 0 && table.m_strSql == "-");
    CHECK(table.SelectCustom("FROM U WHERE x=1", 2, &list, 0) == 77);
    CHECK(table.m_strSql == "SELECT id,name,score,level,exp,rate,ts FROM U WHERE x=1");
    CHECK(table.SelectCustom("FROM U", 2, &list, 1) == 77);
    CHECK(table.m_strSql == "SELECT id,name,score,level,exp,rate,ts FROM U");
    CHECK(table.SelectCustom("SELECT a FROM b", 2, &list, 1) == 77);
    CHECK(table.m_strSql == "SELECT a FROM b");
    CHECK(table.SelectByKey(3, &list, 0, 1, 42) == 77);
    CHECK(table.m_strSql == "SELECT id,name,score,level,exp,rate,ts FROM T WHERE id=42");
    CHECK(table.SelectByKey(3, &list, 1, 1, 43) == 77);
    CHECK(table.m_strSql
          == "SELECT id,name,score,level,exp,rate,ts FROM T WITH (NOLOCK) WHERE id=43");
    table.m_strSql = "-";
    CHECK(table.SelectByKey(3, &list, 0, 2, 1, 2) == 0 && table.m_strSql == "-");
    CHECK(table.CheckKeyArgCount(1) && !table.CheckKeyArgCount(0));

    // formatted text longer than the 3 KB buffer is refused
    std::string strLong(4000, 'x');
    info.m_strSelect = "%s";
    CHECK(table.SelectByKey(3, &list, 0, 1, strLong.c_str()) == 0 && table.m_strSql == "-");
    info.BuildSqlTemplates(0);

    // count / delete need a statement
    CHECK(table.CountByKey(0, 1, 5) == 0);
    CHECK(table.DeleteByKey(0, 1, 5) == -3);
    CHECK(table.DeleteByKey(0, 2, 5, 6) == 0);
    info.m_dwFlags = DBTABLE_UPDATE;
    CHECK(table.DeleteByKey(0, 1, 5) == 0);

    // the bound fetches need a statement as well
    CHECK(table.FetchBoundByOrdinal(0, "SELECT 1", &list) == 0);
    CHECK(table.FetchBoundSequential(0, "SELECT 1", &list) == 0);
    CHECK(CDBTable::FetchWithGetData("SELECT 1", &table, 0, &list) == 0);
    CHECK(list.empty());

    table.Close();
    CHECK(table.m_pAllocator == NULL && table.m_pInfo == NULL && info.m_pTable == NULL);
    s_pRecordInfo = NULL;
}

static void TestErrorPathsWithoutServer()
{
    // statements need a pool connection
    CHECK(CDBSessionMgr::GetConnection(NULL, 0) == NULL);
    CDBSessionMgr idle;
    CHECK(idle.m_nCount == 0 && idle.m_phDbc == NULL && !idle.IsOpen());
    CHECK(CDBSessionMgr::GetConnection(&idle, 0) == NULL);
    CHECK(CDatabase::AllocStmt(NULL, 0) == NULL);
    CHECK(CDatabase::ExecDirect(NULL, NULL, 0) == -1);
    CHECK(CDatabase::ExecDirect("", NULL, 0) == -1);
    CHECK(CDatabase::ExecDirect("SELECT 1", NULL, 0) == -3);
    DBParamList params;
    CHECK(CDatabase::ExecDirectWithParams(NULL, 0, "", &params) == -1);
    CHECK(CDatabase::ExecDirectWithParams(NULL, 0, "SELECT 1", &params) == -2);

    // no environment: no pool
    CHECK(g_hDBEnv == NULL);
    CHECK(CDatabase::CreateSession("DRIVER={SQL Server};") == NULL);

    CHECK(CDatabase::InitEnvironment("DRIVER={No Such Driver For This Test};"));
    CHECK(g_hDBEnv != NULL && g_strDBConnect == "DRIVER={No Such Driver For This Test};");
    // the default string is used for NULL and ""; the driver is not found
    CHECK(CDatabase::CreateSession(NULL) == NULL);
    CHECK(CDatabase::CreateSession("") == NULL);
    CHECK(CDatabase::CreateSession("DRIVER={Another Missing Driver};") == NULL);
    CHECK(g_setDBSessionMgr.empty());

    // a failed open frees its handles
    CDBSessionMgr pool;
    CHECK(!pool.Open("DRIVER={Another Missing Driver};"));
    CHECK(pool.m_nCount == DB_SESSION_COUNT && pool.m_phDbc == NULL);
    CDatabase::DestroySession(&pool);       // not registered: nothing happens
    CHECK(pool.m_nCount == DB_SESSION_COUNT);

    // query object: no connection
    s_vecNetLog.clear();
    CDBQuery* pQuery = new CDBQuery;
    memset(pQuery, 0, sizeof(*pQuery));
    pQuery->m_nSession = 0;
    CHECK(pQuery->ExecuteFormat("exec _Proc '%s',%d", "abc", 5) == -1);
    CHECK(strcmp(pQuery->m_szQuery, "exec _Proc 'abc',5") == 0);
    std::vector<std::string> logs = DrainLogPrintf();
    CHECK(logs.size() == 1 && logs[0] == "cannot occupy db session : exec _Proc 'abc',5");
    CHECK(s_vecNetLog.empty());
    // index out of range
    pQuery->m_pSession = &idle;
    CHECK(pQuery->Execute() == -1);
    logs = DrainLogPrintf();
    CHECK(logs.size() == 1 && logs[0] == "cannot occupy db session : exec _Proc 'abc',5");
    // text longer than the buffer is cut
    std::string strLong(0x3000, 'q');
    CHECK(pQuery->ExecuteFormat("%s", strLong.c_str()) == -1);
    CHECK(strlen(pQuery->m_szQuery) == 0x27FF);
    // the log line does not fit LogPrintf's 8 KB buffer and is dropped
    logs = DrainLogPrintf();
    CHECK(logs.empty());
    delete pQuery;

    // diagnostics: SQL_NO_DATA logs only the query
    s_vecNetLog.clear();
    CDatabase::HandleSQLError(SQL_NO_DATA, NULL, "select x");
    CHECK(s_vecNetLog.size() == 1 && s_vecNetLog[0] == "select x");
    // no diagnostic record: the query, the empty line, the query again (as a format, DB-003)
    s_vecNetLog.clear();
    CDatabase::HandleSQLError(SQL_ERROR, NULL, "update t set p=100%%");
    CHECK(s_vecNetLog.size() >= 3);
    CHECK(s_vecNetLog.size() >= 3 && s_vecNetLog[0] == "update t set p=100%%"
          && s_vecNetLog[1] == "" && s_vecNetLog[2] == "update t set p=100%");
    s_vecNetLog.clear();
    CDatabase::HandleSQLError(SQL_ERROR, NULL, NULL);
    CHECK(s_vecNetLog.size() >= 3 && s_vecNetLog[0] == "[Unknown query]"
          && s_vecNetLog[2] == "[Unknown query]");

    CDatabase::DestroyAllSessions();
    CHECK(g_hDBEnv == NULL && g_setDBSessionMgr.empty());
}

// ---------------------------------------------------------------------------------------------
// optional checks against a temporary Access database

static int s_nTriggerCalls = 0;
static std::vector<int> s_vecTriggerIds;

class CRowSink
{
public:
    int OnRow(DWORD dwQuery, DWORD)
    {
        CDBQuery* pQuery = (CDBQuery*)dwQuery;
        ++s_nTriggerCalls;
        if (pQuery->m_bRowFetched)
            s_vecTriggerIds.push_back(*(int*)pQuery->m_pBuffer);
        return s_vecTriggerIds.size() < 2 ? 1 : 0;
    }
};

#pragma pack(push, 1)
struct QUERY_ROW
{
    int     nId;
    char    szName[20];
    short   sScore;
};
#pragma pack(pop)

static void TestWithAccessDriver()
{
    char szDir[MAX_PATH];
    GetTempPathA(MAX_PATH, szDir);

    // files of earlier runs (locked until their process ended, see DB-001)
    char szPattern[MAX_PATH];
    wsprintfA(szPattern, "%sDatabaseTest_*.?db", szDir);
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
    wsprintfA(szFile, "%sDatabaseTest_%lu.mdb", szDir, GetCurrentProcessId());
    DeleteFileA(szFile);
    char szAttr[MAX_PATH + 32];
    int nLen = wsprintfA(szAttr, "CREATE_DB=\"%s\" General", szFile);
    szAttr[nLen + 1] = 0;
    if (!SQLConfigDataSource(NULL, ODBC_ADD_DSN, "Microsoft Access Driver (*.mdb)", szAttr))
    {
        printf("DatabaseTest: no 32-bit Access ODBC driver, database checks skipped\n");
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
    CHECK(g_setDBSessionMgr.size() == 1 && pSession->IsOpen());
    CHECK(pSession->m_nCount == DB_SESSION_COUNT);
    for (int i = 0; i < DB_SESSION_COUNT; ++i)
        CHECK(CDBSessionMgr::GetConnection(pSession, i) != NULL);
    CHECK(CDBSessionMgr::GetConnection(pSession, DB_SESSION_COUNT) == NULL);
    CHECK(CDBSessionMgr::GetConnection(pSession, -1) == NULL);

    CHECK(CDatabase::ExecDirect("CREATE TABLE T (id INTEGER, name VARCHAR(20), score SMALLINT, "
                                "lvl BYTE, rate REAL, ts DATETIME)", pSession, 0) == SQL_SUCCESS);
    CHECK(CDatabase::ExecDirect("INSERT INTO T VALUES (1, 'one  ', 10, 5, 1.5, #2008-03-04 05:06:07#)",
                                pSession, 0) == SQL_SUCCESS);
    CHECK(CDatabase::ExecDirect("INSERT INTO T VALUES (2, 'two', -20, 6, 2.5, #2009-01-02 03:04:05#)",
                                pSession, 0) == SQL_SUCCESS);

    // parameters
    DBParamList params;
    DB_PARAM p;
    memset(&p, 0, sizeof(p));
    p.nIOType = SQL_PARAM_INPUT;
    p.nType = DBPARAM_INTEGER;
    p.nValue = 3;
    params.push_back(p);
    p.nType = DBPARAM_SMALLINT;
    p.sValue = 30;
    params.push_back(p);
    CHECK(CDatabase::ExecDirectWithParams(pSession, 0, "INSERT INTO T (id, score, name) "
                                          "VALUES (?, ?, 'three')", &params) == SQL_SUCCESS);
    // unknown type: nothing executed, success (DB-006)
    params.front().nType = 9;
    CHECK(CDatabase::ExecDirectWithParams(pSession, 0, "DELETE FROM T", &params) == 0);

    // statement error: the query and the diagnostics are logged
    s_vecNetLog.clear();
    CHECK(CDatabase::ExecDirect("SELECT * FROM NoSuchTable", pSession, 0) == SQL_ERROR);
    CHECK(s_vecNetLog.size() >= 3 && s_vecNetLog[0] == "SELECT * FROM NoSuchTable");
    CHECK(s_vecNetLog.size() >= 3
          && s_vecNetLog[1].compare(0, 22, "SQL Error State:42S02,") == 0);
    CHECK(s_vecNetLog.size() >= 3 && s_vecNetLog[2] == "SELECT * FROM NoSuchTable");

    // query object, row buffer
    CDBQuery* pQuery = new CDBQuery;
    memset(pQuery, 0, sizeof(*pQuery));
    QUERY_ROW rows[2];
    memset(rows, 0x55, sizeof(rows));
    pQuery->m_pSession = pSession;
    pQuery->m_nSession = 0;
    pQuery->m_pBuffer = (BYTE*)rows;
    pQuery->m_nBufferSize = sizeof(rows);
    pQuery->m_nRowSize = sizeof(QUERY_ROW);
    CHECK(pQuery->ExecuteFormat("SELECT id, name, score FROM T WHERE id <= %d ORDER BY id", 2)
          == 0);
    CHECK(rows[0].nId == 1 && memcmp(rows[0].szName, "one", 4) == 0 && rows[0].sScore == 10);
    // raw column data: no terminator, the rest of the column keeps the old bytes
    CHECK(rows[1].nId == 2 && memcmp(rows[1].szName, "two", 3) == 0 && rows[1].sScore == -20);
    CHECK(rows[1].szName[3] == 0x55 && rows[1].szName[19] == 0x55);
    // more rows than the buffer holds
    s_vecNetLog.clear();
    CHECK(pQuery->ExecuteFormat("SELECT id, name, score FROM T ORDER BY id") == 1);
    CHECK(s_vecNetLog.size() >= 1 && s_vecNetLog[0] == "[Unknown query]");
    // no row
    CHECK(pQuery->ExecuteFormat("SELECT id FROM T WHERE id = 99") == 1);
    // execute error
    s_vecNetLog.clear();
    CHECK(pQuery->ExecuteFormat("SELECT * FROM NoSuchTable") == -2);
    std::vector<std::string> logs = DrainLogPrintf();
    CHECK(logs.size() == 1 && logs[0] == "execute query error : SELECT * FROM NoSuchTable");
    CHECK(s_vecNetLog.size() >= 1 && s_vecNetLog[0] == "[Unknown query]");
    // row larger than m_nRowSize
    pQuery->m_nRowSize = 8;
    CHECK(pQuery->ExecuteFormat("SELECT id, name, score FROM T") == -4);
    pQuery->m_nRowSize = sizeof(QUERY_ROW);

    // trigger: called before and after each row, 0 stops
    CRowSink sink;
    ServerFramework::CClassTrigger<CRowSink> trigger(&sink);
    trigger.m_pfnTrigger2 = &CRowSink::OnRow;
    pQuery->m_pTrigger = &trigger;
    QUERY_ROW one;
    pQuery->m_pBuffer = (BYTE*)&one;
    CHECK(pQuery->ExecuteFormat("SELECT id, name, score FROM T ORDER BY id") == 1);
    CHECK(s_nTriggerCalls == 4 && s_vecTriggerIds.size() == 2);
    CHECK(s_vecTriggerIds.size() == 2 && s_vecTriggerIds[0] == 1 && s_vecTriggerIds[1] == 2);
    delete pQuery;

    // table binding, columns described into group 1
    CDBTableInfo info;
    info.m_pRecordClass = &CTestRecord::classCTestRecord;
    info.m_strTable = "T";
    info.m_dwFlags = DBTABLE_DELETE;
    info.m_nRowSize = -1;
    info.m_nColumns = 0;
    info.m_pIdentity = NULL;
    info.m_pTable = NULL;
    info.m_mapGroups[DBGROUP_KEY];
    info.m_mapGroups[DBGROUP_ALL];
    s_pRecordInfo = &info;
    CDBTable table;
    CHECK(table.Open(&info, pSession, NULL) == 1);
    DBColumnVector& vec = info.m_mapGroups[DBGROUP_ALL].vecColumns;
    CHECK(vec.size() == 6 && info.m_nColumns == 6);
    if (vec.size() == 6)
    {
        static const char* s_apszName[6] = { "id", "name", "score", "lvl", "rate", "ts" };
        static const int s_anType[6] = { 2, 6, 1, 0, 4, 7 };
        static const int s_anOffset[6] = { 0, 4, 32, 34, 36, 40 };
        int nBufOffset = 0;
        for (int i = 0; i < 6; ++i)
        {
            CHECK(vec[i].strName == s_apszName[i]);
            CHECK(vec[i].nType == s_anType[i] && vec[i].nOffset == s_anOffset[i]);
            CHECK(vec[i].nOrdinal == i + 1 && vec[i].nBufOffset == nBufOffset);
            CHECK(vec[i].strFormat == DB_ColumnTypeFormat(s_anType[i]));
            nBufOffset += vec[i].nSize;
        }
        CHECK(info.m_nRowSize == nBufOffset);
        CHECK(info.m_strSelect == "SELECT * FROM T");
        CHECK(info.m_strSelectNoLock == "SELECT * FROM T WITH (NOLOCK)");
        CHECK(info.m_strDelete == "DELETE FROM T");
        CHECK(table.m_pfnFetch == &CDBTable::FetchBoundByOrdinal);

        // the auto layout puts rate at 36; the test record keeps llExp there, so only the
        // other columns are compared
        CDBRecordList list;
        int nSelect = table.SelectWhere("ORDER BY id", 0, &list, 0);
        CHECK(nSelect == 1 && list.size() == 3);
        if (list.size() == 3)
        {
            CTestRecord* p1 = (CTestRecord*)list.front();
            CTestRecord* p3 = (CTestRecord*)list.back();
            CHECK(p1->m_data.nId == 1 && p1->m_data.strName == "one  ");
            CHECK(p1->m_data.sScore == 10 && p1->m_data.byLevel == 5);
            CHECK(p3->m_data.nId == 3 && p3->m_data.strName == "three" && p3->m_data.byLevel == 0);
            CHECK(p1->GetOwner() == &table);
        }
        for (CDBRecordList::iterator it = list.begin(); it != list.end(); ++it)
            table.FreeRecord(*it);
        CHECK(s_nRecords == 0);

        // GetData variant
        list.clear();
        CHECK(CDBTable::FetchWithGetData("SELECT * FROM T ORDER BY id", &table, 0, &list) == 1);
        CHECK(list.size() == 3);
        if (!list.empty())
        {
            CTestRecord* p1 = (CTestRecord*)list.front();
            CHECK(p1->m_data.nId == 1 && p1->m_data.sScore == 10);
        }
        for (CDBRecordList::iterator it = list.begin(); it != list.end(); ++it)
            table.FreeRecord(*it);
        CHECK(s_nRecords == 0);
    }
    table.Close();
    s_pRecordInfo = NULL;

    // delete by key runs only the WHERE text (DB-005): the driver rejects it
    CDBTableInfo keyed;
    FillTestInfo(keyed, DBTABLE_DELETE);
    keyed.BuildSqlTemplates(0);
    CDBTable keyedTable;
    keyedTable.m_pSession = pSession;
    keyedTable.m_pInfo = &keyed;
    s_vecNetLog.clear();
    CHECK(keyedTable.DeleteByKey(0, 1, 2) == SQL_ERROR);
    CHECK(!s_vecNetLog.empty() && s_vecNetLog[0] == "WHERE id=2");
    CHECK(keyedTable.CountByKey(0, 1, 2) == 1);
    keyedTable.m_pInfo = NULL;

    // without SQLDisconnect the file stays in use (DB-001)
    CDatabase::DestroySession(pSession);
    CHECK(g_setDBSessionMgr.empty());
    CDatabase::DestroyAllSessions();
    CHECK(g_hDBEnv == NULL);
    if (!DeleteFileA(szFile))
        printf("DatabaseTest: %s stays in use until the process ends (DB-001)\n", szFile);
}

int main()
{
    CNetEngine engine;
    engine.m_Config.pfnLog = CaptureNetLog;
    g_pBSNet = &g_Net;
    LogCapture_Start();

    TestTypeHelpers();
    TestStatementTemplates();
    TestRecordUpdate();
    TestTableBinding();
    TestErrorPathsWithoutServer();
    TestWithAccessDriver();

    LogCapture_Stop();
    g_pBSNet = NULL;
    return TEST_RESULT("DatabaseTest");
}
