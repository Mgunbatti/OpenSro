#pragma once

#include <windows.h>
#include <sql.h>
#include <sqlext.h>

#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "BSLib/thread/ServiceObject.h"

// BSLib ODBC layer (D:\WORK2005\JMX_Library\BSLib\DB\Database.cpp).
//
// Pieces:
// - one ODBC environment (g_hDBEnv, ODBC 3) and a default connection string (g_strDBConnect),
//   set up by CDatabase::InitEnvironment;
// - CDBSessionMgr: a pool of DB_SESSION_COUNT connections opened with the same connection
//   string. Callers pick a connection by index (each worker thread uses its own index);
//   CDatabase::CreateSession opens one pool per database and keeps it in g_setDBSessionMgr;
// - statement helpers on a pool connection (CDatabase::AllocStmt, ExecDirect,
//   ExecDirectWithParams) and the common diagnostic logger CDatabase::HandleSQLError;
// - CDBQuery (DBQuery.h): the query object the server code uses for stored procedures and
//   plain selects (row buffer or per-row trigger);
// - CDBTable / CDBTableInfo / CDBRecord: a table binding that reads the column list with
//   SQLColumns, prebuilds SELECT / INSERT / UPDATE / DELETE / COUNT text and fetches rows into
//   record objects created through a _record_allocator.
//
// Every function returns the original's codes; failures are logged through NetEngineLog
// (HandleSQLError) or LogPrintf (CDBQuery).

class CDBSessionMgr;
class CDBRecord;
class CDBTableInfo;
class _record_allocator;
class __single_inheritance CDBTable;

typedef std::list<CDBRecord*> CDBRecordList;

enum
{
    DB_SESSION_COUNT        = 30,       // connections per CDBSessionMgr
    DB_STATEMENT_SIZE       = 0xC00,    // SQL text buffers of the table code
    DB_FETCH_BUFFER_SIZE    = 0x2000,   // row-wise binding buffer of one SQLFetchScroll
};

// Column type codes of the table layer (DBColumnInfo::nType).
enum
{
    DBTYPE_BYTE         = 0,    // SQL_BIT, SQL_TINYINT            "%u"
    DBTYPE_SHORT        = 1,    // SQL_SMALLINT                    "%d"
    DBTYPE_INT          = 2,    // SQL_INTEGER                     "%d"
    DBTYPE_INT64        = 3,    // SQL_BIGINT                      "%I64d"
    DBTYPE_FLOAT        = 4,    // SQL_REAL                        "%f"
    DBTYPE_DOUBLE       = 5,    // SQL_FLOAT (no format, no C type)
    DBTYPE_STRING       = 6,    // SQL_CHAR, SQL_VARCHAR, SQL_WVARCHAR; std::string in the record
    DBTYPE_TIMESTAMP    = 7,    // SQL_TYPE_TIMESTAMP              convert(datetime, ...)
};

// CDBTableInfo::m_dwFlags: which statements BuildSqlTemplates prepares.
enum
{
    DBTABLE_UPDATE      = 0x01,     // per-group "UPDATE ... SET" and m_strUpdateAll
    DBTABLE_INSERT      = 0x02,     // m_strInsert
    DBTABLE_DELETE      = 0x10,     // m_strDelete (and CDBTable::DeleteByKey)
};

// Field groups of a table (keys of CDBTableInfo::m_mapGroups).
enum
{
    DBGROUP_KEY         = 0,        // key columns: WHERE clause, COUNT
    DBGROUP_ALL         = 1,        // read-only group; the auto-describe target
    // 2..63: update groups, one dirty bit each in CDBRecord::m_qwDirty
};

// Column descriptor (84 bytes). Declared by the application for its record layout, completed by
// CDBTable::DescribeColumns from SQLColumns.
struct DBColumnInfo
{
    // DBColumnInfo_ctor (0x476610). Copy construction (0x47ca60) and destruction (0x4795a0)
    // are the implicit member-wise ones.
    DBColumnInfo();

    std::string strName;        // +00  column name
    int         nType;          // +1C  DBTYPE_*
    int         nOffset;        // +20  offset of the value in the record data (CDBRecord::GetData)
    int         nSize;          // +24  BUFFER_LENGTH from SQLColumns: bind / GetData size
    int         nBufOffset;     // +28  sum of the sizes of all previous result columns
    int         bNullable;      // +2C  NULLABLE
    int         nOrdinal;       // +30  ORDINAL_POSITION (1-based)
    int         bIdentity;      // +34  TYPE_NAME contains "identity"
    std::string strFormat;      // +38  printf format of a value in SQL text (DB_ColumnTypeFormat)
};

C_ASSERT(sizeof(std::string) == 28);
C_ASSERT(FIELD_OFFSET(DBColumnInfo, nType) == 0x1C);
C_ASSERT(FIELD_OFFSET(DBColumnInfo, nOrdinal) == 0x30);
C_ASSERT(FIELD_OFFSET(DBColumnInfo, strFormat) == 0x38);
C_ASSERT(sizeof(DBColumnInfo) == 84);

typedef std::vector<DBColumnInfo> DBColumnVector;

// One field group: the columns and, for update groups, the prepared statement parts.
struct DB_FIELD_GROUP
{
    std::string     strUpdate;      // +00  "UPDATE t SET a=%d,b='%s' WHERE k=%d"
    std::string     strSet;         // +1C  "a=%d,b='%s'"
    DBColumnVector  vecColumns;     // +38
};

C_ASSERT(sizeof(DBColumnVector) == 16);
C_ASSERT(sizeof(DB_FIELD_GROUP) == 0x48);

typedef std::map<int, DB_FIELD_GROUP> DBFieldGroupMap;
C_ASSERT(sizeof(DBFieldGroupMap) == 12);

// Table description shared by the binding and the record class. The application fills
// m_pRecordClass, m_strTable, m_dwFlags and m_mapGroups; CDBTable::Open / DescribeColumns
// complete the column data, the row size and the statement text.
class CDBTableInfo
{
public:
    // CDBTable::FindField (0x476690): the value of group DBGROUP_KEY, NULL when there is none.
    DB_FIELD_GROUP* FindKeyGroup();

    // CDBTable::BuildSqlTemplates (0x479630): prepares the statement text from the groups.
    // nDescribed is the number of result columns SQLColumns reported.
    void BuildSqlTemplates(int nDescribed);

public:
    CRuntimeClass*  m_pRecordClass;     // +00  records are created / deleted through it
    std::string     m_strTable;         // +04  table name
    std::string     m_strUpdateAll;     // +20  "UPDATE t SET <all non-key columns> WHERE ..."
    std::string     m_strInsert;        // +3C  "INSERT INTO t(a,b ) VALUES(%d,'%s' )"
    std::string     m_strSelect;        // +58  "SELECT ... FROM t [WHERE ...]"
    std::string     m_strSelectNoLock;  // +74  m_strSelect with " WITH (NOLOCK)" after the table
    std::string     m_strDelete;        // +90  "DELETE FROM t"
    std::string     m_strCount;         // +AC  "SELECT COUNT(k) FROM t WHERE ..."
    std::string     m_strKeyWhere;      // +C8  "WHERE k1=%d AND k2='%s'"
    DBFieldGroupMap m_mapGroups;        // +E4
    int             m_nRowSize;         // +F0  sum of the column sizes (BUFFER_LENGTH)
    DWORD           m_dwFlags;          // +F4  DBTABLE_*
    int             m_nColumns;         // +F8  columns over all groups
    DBColumnInfo*   m_pIdentity;        // +FC  identity column, if any
    CDBTable*       m_pTable;           // +100 binding that uses this description
};

C_ASSERT(FIELD_OFFSET(CDBTableInfo, m_strKeyWhere) == 0xC8);
C_ASSERT(FIELD_OFFSET(CDBTableInfo, m_mapGroups) == 0xE4);
C_ASSERT(FIELD_OFFSET(CDBTableInfo, m_nRowSize) == 0xF0);
C_ASSERT(FIELD_OFFSET(CDBTableInfo, m_pTable) == 0x100);

// Base of the record objects (24 bytes). The Gateway contains no record class, so the
// constructor and slot 2 are not known; the slots the library calls are 3..7.
class CDBRecord : public CBase
{
    BS_DECLARE_RUNTIME_CLASS(CDBRecord)                                     // 0

public:
    CDBRecord();
    virtual ~CDBRecord();                                                   // 1

    virtual void Reserved02() = 0;                                          // 2
    virtual CDBTableInfo* GetTableInfo() = 0;                               // 3
    // Called once after creation with CDBTableInfo::m_nRowSize.
    virtual void AllocData(int nRowSize) = 0;                               // 4
    virtual void InitData() = 0;                                            // 5
    // Record data; DBColumnInfo::nOffset is relative to it.
    virtual BYTE* GetData() = 0;                                            // 6
    // Stores one fetched column value (pValue points into the fetch buffer).
    virtual void SetField(const DBColumnInfo* pColumn, const void* pValue) = 0;   // 7

    // Dirty bits of the update groups (inlined helpers of BuildUpdate).
    BOOL IsDirty(int nGroup) const;                     // 0x476880
    void ClearDirty(int nGroup);                        // 0x4768b0
    void ClearAllDirty()                                // Obj_ClearFields8_C (0x4768a0)
    {
        m_qwDirty = 0;
    }
    void SetOwner(CDBTable* pOwner)                     // Obj::SetField_0x10 (0x476860)
    {
        m_pOwner = pOwner;
    }
    CDBTable* GetOwner() const                          // Obj::GetField_0x10_13 (0x476870)
    {
        return m_pOwner;
    }

    // CDBTable::BuildUpdate (0x47a610): UPDATE text for one dirty group (nGroup >= 2) or, with
    // nGroup == -1, one statement for all dirty groups. 1 with strOut set (the dirty bits are
    // cleared), 0 when nothing can be built.
    int BuildUpdate(std::string& strOut, int nGroup);
    // CDBTable::ExecUpdate (0x4788a0): BuildUpdate + CDatabase::ExecDirect on the table's pool.
    int Update(CDBTable* pTable, int nConn, int nGroup);

public:
    DWORD               m_dw04;         // +04
    unsigned __int64    m_qwDirty;      // +08  bit n: group n changed
    CDBTable*           m_pOwner;       // +10  table that created the record
    DWORD               m_dw14;         // +14
};

C_ASSERT(FIELD_OFFSET(CDBRecord, m_qwDirty) == 8);
C_ASSERT(FIELD_OFFSET(CDBRecord, m_pOwner) == 0x10);
C_ASSERT(sizeof(CDBRecord) == 24);

// Stream record (48 bytes): only its runtime class survives in the Gateway.
class CDBRecStream : public CDBRecord
{
    BS_DECLARE_RUNTIME_CLASS(CDBRecStream)

public:
    DWORD m_dw18;       // +18
    DWORD m_dw1C;       // +1C
    DWORD m_dw20;       // +20
    DWORD m_dw24;       // +24
    DWORD m_dw28;       // +28
    DWORD m_dw2C;       // +2C
};

C_ASSERT(sizeof(CDBRecStream) == 48);

// Record factory of a table (RTTI name _record_allocator). The default one creates and deletes
// records through the record class of the table description.
class _record_allocator
{
public:
    explicit _record_allocator(CDBTable* pTable);           // 0x476900
    virtual ~_record_allocator();                           // 0 (0x476970, 0x476910)

    virtual CDBRecord* AllocRecord();                       // 1 (0x476920)
    virtual void FreeRecord(CDBRecord* pRecord);            // 2 (0x476930)
    // Sets both values to 0.
    virtual void vf03(DWORD* pdw1, DWORD* pdw2);            // 3 (0x476950)

public:
    CDBTable* m_pTable;     // +4
};

C_ASSERT(sizeof(_record_allocator) == 8);

// Table binding (20 bytes).
class CDBTable
{
public:
    // Row fetch used by the Select functions: FetchBoundByOrdinal or FetchBoundSequential,
    // chosen by DescribeColumns.
    typedef int (CDBTable::*PFN_FETCH)(int nConn, const char* pszSql, CDBRecordList* pList);

    CDBTable();                                             // 0x476ed0
    virtual ~CDBTable();                                    // 0 (0x476ef0, 0x476f10)

    // 0x476f50: binds pInfo and the allocator (a _record_allocator for this table when NULL)
    // and describes the columns when pSession is set. DescribeColumns' result, or 1.
    int Open(CDBTableInfo* pInfo, CDBSessionMgr* pSession, _record_allocator* pAllocator);
    // CDBTable_ReleaseBinding (0x476f20).
    void Close();

    CDBTableInfo* GetInfo() const                           // Obj::GetField_0x8_21 (0x476800)
    {
        return m_pInfo;
    }

    // 0x478e40: SQLColumns on the table. With exactly two groups the result columns are
    // appended to group DBGROUP_ALL, otherwise they complete the declared columns of the
    // same name. Then BuildSqlTemplates and the fetch function. 1 on success, 0 otherwise.
    int DescribeColumns();

    // Row-wise bound fetch, columns bound by ordinal / one after the other (0x477050,
    // 0x477460). Records are appended to *pList. 1 when all rows were read, 0 otherwise.
    int FetchBoundByOrdinal(int nConn, const char* pszSql, CDBRecordList* pList);
    int FetchBoundSequential(int nConn, const char* pszSql, CDBRecordList* pList);
    // 0x477870: SQLFetch / SQLGetData per column. Not used by the Gateway.
    static int FetchWithGetData(const char* pszSql, CDBTable* pTable, int nConn,
                                CDBRecordList* pList);

    // CreateBoundHelper (0x477c70) / CDBObject_DetachFromOwner (0x477ca0).
    CDBRecord* CreateRecord();
    void FreeRecord(CDBRecord* pRecord);

    // ExecQueryStripWhere (0x477fb0): m_strSelect without its WHERE part.
    int SelectAll(int nConn, CDBRecordList* pList);
    // 0x4780f0: strCondition as query when it contains "select ", otherwise the select list of
    // the template (m_strSelectNoLock for nTemplate 1, else m_strSelect) followed by it.
    int SelectCustom(const std::string& strCondition, int nConn, CDBRecordList* pList,
                     int nTemplate);
    // 0x4782e0: the template without its WHERE part, a blank and pszClause. 0 for "".
    int SelectWhere(const char* pszClause, int nConn, CDBRecordList* pList, int nTemplate);
    // 0x478470: the template formatted with the key values (nArgs must match the key group).
    int SelectByKey(int nConn, CDBRecordList* pList, int nTemplate, int nArgs, ...);
    // 0x4785a0: m_strCount formatted with the key values; the count, 0 on failure.
    DWORD CountByKey(int nConn, int nArgs, ...);
    // 0x4786e0: delete by key (DBTABLE_DELETE). See DB-005.
    int DeleteByKey(int nConn, int nArgs, ...);
    // CDBTable_ColumnCountEquals (0x476810).
    BOOL CheckKeyArgCount(int nArgs);

public:
    CDBSessionMgr*      m_pSession;     // +04
    CDBTableInfo*       m_pInfo;        // +08
    _record_allocator*  m_pAllocator;   // +0C
    PFN_FETCH           m_pfnFetch;     // +10
};

C_ASSERT(sizeof(CDBTable::PFN_FETCH) == 4);
C_ASSERT(FIELD_OFFSET(CDBTable, m_pfnFetch) == 0x10);
C_ASSERT(sizeof(CDBTable) == 0x14);

// Connection pool (RTTI name CDBSessionMgr, 12 bytes).
class CDBSessionMgr
{
public:
    CDBSessionMgr();                                        // 0x4769a0
    virtual ~CDBSessionMgr();                               // 0 (0x4769c0, 0x4769e0)

    // DBConnPool_OpenConnections (0x4769f0): allocates DB_SESSION_COUNT connection handles
    // and connects each with SQLDriverConnect (pszConnect, or g_strDBConnect when NULL or
    // empty). On the first failure all handles are freed and FALSE is returned.
    BOOL Open(const char* pszConnect);
    // DBConnPool_FreeHandles (0x476b00). See DB-001.
    void Close();
    // DBConn_HasHandle (0x476b50).
    BOOL IsOpen() const
    {
        return m_phDbc != NULL;
    }
    // PtrArray_GetAt (0x476e10): connection nIndex of pSession; NULL for a NULL pool or an
    // index out of range.
    static SQLHDBC GetConnection(CDBSessionMgr* pSession, int nIndex);

public:
    int         m_nCount;       // +4
    SQLHDBC*    m_phDbc;        // +8
};

C_ASSERT(sizeof(CDBSessionMgr) == 12);

// List element of CDatabase::ExecDirectWithParams.
enum
{
    DBPARAM_TINYINT     = 0,    // SQL_C_TINYINT  / SQL_TINYINT
    DBPARAM_SMALLINT    = 1,    // SQL_C_SSHORT   / SQL_SMALLINT
    DBPARAM_INTEGER     = 2,    // SQL_C_SLONG    / SQL_INTEGER
    DBPARAM_BIGINT      = 3,    // SQL_C_SBIGINT  / SQL_BIGINT
    DBPARAM_REAL        = 4,    // SQL_C_FLOAT    / SQL_REAL
    DBPARAM_DOUBLE      = 5,    // SQL_C_DOUBLE   / SQL_DOUBLE
};

struct DB_PARAM
{
    int         nType;          // +00  DBPARAM_*
    SQLSMALLINT nIOType;        // +04  SQL_PARAM_INPUT / SQL_PARAM_OUTPUT / ...
    union                       // +08  value (bound by address)
    {
        BYTE        byValue;
        short       sValue;
        int         nValue;
        __int64     llValue;
        float       fValue;
        double      dValue;
    };
    SQLINTEGER  cbValue;        // +10  length / indicator
};

C_ASSERT(sizeof(DB_PARAM) == 0x18);
typedef std::list<DB_PARAM> DBParamList;

// Environment, pool registry and statement helpers.
class CDatabase
{
public:
    // DB_InitODBCEnv (0x476b80): allocates g_hDBEnv with ODBC version 3 and stores
    // pszDefaultConnect as g_strDBConnect. FALSE (and no environment) on failure.
    static BOOL InitEnvironment(const char* pszDefaultConnect);
    // DBConnMap_CreateConnection (0x476ce0): a new open pool registered in g_setDBSessionMgr,
    // NULL without environment or when a connection fails.
    static CDBSessionMgr* CreateSession(const char* pszConnect);
    // DBConnMap53BA0C_RemoveAndDelete (0x476da0).
    static void DestroySession(CDBSessionMgr* pSession);
    // DBConnMap_DestroyAll (0x476bf0): closes and deletes every pool, then frees g_hDBEnv.
    static void DestroyAllSessions();

    // 0x477c30: statement on connection nConn of pSession, NULL on failure.
    static SQLHSTMT AllocStmt(CDBSessionMgr* pSession, int nConn);
    // CDatabase_ExecDirectNoResult (0x477f30): executes pszSql without reading results.
    // -1 empty text, -3 no statement, otherwise the SQLExecDirect result.
    static int ExecDirect(const char* pszSql, CDBSessionMgr* pSession, int nConn);
    // CDatabase_ExecDirectWithParams (0x477d60): binds the list elements as parameters 1..n
    // and executes; with parameters the remaining results are skipped (output parameters).
    // -1 empty text, -2 no statement, otherwise the last result. See DB-006, DB-007.
    static int ExecDirectWithParams(CDBSessionMgr* pSession, int nConn, const char* pszSql,
                                    DBParamList* pParams);

    // 0x47b200: logs the query and the statement diagnostics (lines of up to 80 characters)
    // with NetEngineLog(0x2000001), plus "Database Connection was lost!!! ..." for SQLSTATE
    // 08S01. Returns lstrcmpiA(message, "Connection is busy"); nothing but the query is
    // logged for SQL_NO_DATA. See DB-003, DB-004.
    static int HandleSQLError(SQLRETURN nRet, SQLHANDLE hStmt, const char* pszQuery);
};

// Type helpers.
int DB_SqlTypeToColumnType(SQLSMALLINT nSqlType);   // 0x47b580: DBTYPE_*, -1 unknown
SQLSMALLINT DB_ColumnTypeToCType(int nType);        // DBType_ToSqlCType (0x47b520), -1 unknown
const char* DB_ColumnTypeFormat(int nType);         // DBColType_GetSqlFormatSpec (0x4766f0)
int DB_ColumnTypeAlignment(int nType);              // DBColType_GetAlignment (0x476750)
int DB_ColumnTypeSize(int nType);                   // DBColType_GetNativeSize (0x4767a0)

// Time_GetScaledTimeOfDay (0x47b6e0): ((time - timezone + 2208988800) mod 86400) * 1000 /
// 3.333333: milliseconds of the day, scaled.
int Time_GetScaledTimeOfDay();

// Globals.
extern SQLHENV g_hDBEnv;                            // 0x4F9D4C
extern std::string g_strDBConnect;                  // 0x4F8378
extern std::set<CDBSessionMgr*> g_setDBSessionMgr;  // 0x53BA0C
