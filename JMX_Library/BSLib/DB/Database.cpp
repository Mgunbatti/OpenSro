#include "Database.h"

#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "BSLib/file/IFile.h"
#include "BSNet/NetEngineLog.h"

// Discarded trace output of the originals (empty function); the argument is still evaluated.
#define DB_TRACE(x) ((void)(x))

#ifdef JMX_FRAMEWORK_2011_01
// GlobalManager 0x4caa70: the January 2011 source tree and its line numbers.
static const char s_szDatabaseCpp[] =
    "D:\\WORK2005\\Source\\JMX_Library\\BSLib\\DB\\Database.cpp";
#define DB_ASSERT_LINE_STRING_SIZE  1571
#define DB_ASSERT_LINE_OFFSET       1587
#else
static const char s_szDatabaseCpp[] = "D:\\WORK2005\\JMX_Library\\BSLib\\DB\\Database.cpp";
#define DB_ASSERT_LINE_STRING_SIZE  1524
#define DB_ASSERT_LINE_OFFSET       1540
#endif

SQLHENV g_hDBEnv = NULL;
std::string g_strDBConnect;
#ifdef JMX_FRAMEWORK_2011_01
std::set<CDBSessionMgr*>* g_pSetDBSessionMgr = NULL;
#define DB_SESSION_SET (*g_pSetDBSessionMgr)
#else
std::set<CDBSessionMgr*> g_setDBSessionMgr;
#define DB_SESSION_SET g_setDBSessionMgr
#endif

// ---------------------------------------------------------------------------------------------
// Type helpers

int DB_SqlTypeToColumnType(SQLSMALLINT nSqlType)
{
    switch (nSqlType)
    {
    case SQL_WVARCHAR:
    case SQL_CHAR:
    case SQL_VARCHAR:
        return DBTYPE_STRING;
    case SQL_BIT:
    case SQL_TINYINT:
        return DBTYPE_BYTE;
    case SQL_BIGINT:
        return DBTYPE_INT64;
    case SQL_INTEGER:
        return DBTYPE_INT;
    case SQL_SMALLINT:
        return DBTYPE_SHORT;
    case SQL_FLOAT:
        return DBTYPE_DOUBLE;
    case SQL_REAL:
        return DBTYPE_FLOAT;
    case SQL_TYPE_TIMESTAMP:
        return DBTYPE_TIMESTAMP;
    default:
        return -1;
    }
}

SQLSMALLINT DB_ColumnTypeToCType(int nType)
{
    switch (nType)
    {
    case DBTYPE_BYTE:
        return SQL_C_UTINYINT;
    case DBTYPE_SHORT:
        return SQL_C_SSHORT;
    case DBTYPE_INT:
        return SQL_C_SLONG;
    case DBTYPE_INT64:
        return SQL_C_SBIGINT;
    case DBTYPE_FLOAT:
        return SQL_C_FLOAT;
    case DBTYPE_STRING:
        return SQL_C_CHAR;
    case DBTYPE_TIMESTAMP:
        return SQL_C_TYPE_TIMESTAMP;
    default:
        return -1;
    }
}

const char* DB_ColumnTypeFormat(int nType)
{
    switch (nType)
    {
    case DBTYPE_BYTE:
        return "%u";
    case DBTYPE_SHORT:
    case DBTYPE_INT:
        return "%d";
    case DBTYPE_INT64:
        return "%I64d";
    case DBTYPE_FLOAT:
        return "%f";
    case DBTYPE_STRING:
        return "'%s'";
    case DBTYPE_TIMESTAMP:
        return "convert(datetime, '%d-%d-%d %d:%d:%d')";
    default:
        return NULL;
    }
}

int DB_ColumnTypeAlignment(int nType)
{
    switch (nType)
    {
    case DBTYPE_BYTE:
        return 1;
    case DBTYPE_SHORT:
    case DBTYPE_TIMESTAMP:
        return 2;
    case DBTYPE_INT:
    case DBTYPE_FLOAT:
    case DBTYPE_STRING:
        return 4;
    case DBTYPE_INT64:
        return 8;
    default:
        return -1;
    }
}

int DB_ColumnTypeSize(int nType)
{
    switch (nType)
    {
    case DBTYPE_BYTE:
        return 1;
    case DBTYPE_SHORT:
        return 2;
    case DBTYPE_INT:
    case DBTYPE_FLOAT:
        return 4;
    case DBTYPE_INT64:
        return 8;
    case DBTYPE_STRING:
        return sizeof(std::string);
    case DBTYPE_TIMESTAMP:
        return sizeof(TIMESTAMP_STRUCT);
    default:
        return -1;
    }
}

int Time_GetScaledTimeOfDay()
{
    __time32_t t = 0;
    _time32(&t);
    long lTimezone = 0;
    _get_timezone(&lTimezone);
    DWORD dwSeconds = ((DWORD)t + 2208988800u - (DWORD)lTimezone) % 86400u;
    return (int)(__int64)((double)(dwSeconds * 1000) / 3.3333330154418945);
}

// ---------------------------------------------------------------------------------------------
// DBColumnInfo

DBColumnInfo::DBColumnInfo()
{
    nType = -1;
    nOffset = 0;
    nSize = 0;
    nBufOffset = 0;
    bNullable = 0;
    nOrdinal = -1;
    bIdentity = 0;
}

// ---------------------------------------------------------------------------------------------
// CDBSessionMgr

CDBSessionMgr::CDBSessionMgr()
{
    _tzset();
    m_phDbc = NULL;
    m_nCount = 0;
}

CDBSessionMgr::~CDBSessionMgr()
{
    Close();
}

#ifdef JMX_FRAMEWORK_2011_08
// SR_ShardManager 0x6b1740
BOOL CDBSessionMgr::Open(const char* pszConnect, int nCount)
{
    if (nCount <= 0)
        return FALSE;
#else
BOOL CDBSessionMgr::Open(const char* pszConnect)
{
    const int nCount = DB_SESSION_COUNT;
#endif
    SQLCHAR szConnOut[1024];
    SQLSMALLINT cbConnOut;

    const char* pszUse = (pszConnect && lstrlenA(pszConnect)) ? pszConnect
                                                              : g_strDBConnect.c_str();

    m_nCount = nCount;
    m_phDbc = new SQLHDBC[nCount];
    memset(m_phDbc, 0, nCount * sizeof(SQLHDBC));

    for (int i = 0; i < nCount; ++i)
    {
        if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, g_hDBEnv, &m_phDbc[i]))
            || !SQL_SUCCEEDED(SQLDriverConnect(m_phDbc[i], NULL, (SQLCHAR*)pszUse, SQL_NTS,
                                                szConnOut, 1024, &cbConnOut,
                                                SQL_DRIVER_NOPROMPT)))
        {
            Close();
            return FALSE;
        }
    }
    return TRUE;
}

void CDBSessionMgr::Close()
{
    if (m_phDbc == NULL)
        return;
    for (int i = 0; i < m_nCount; ++i)
    {
        // BUG(DB-001): the connections are freed without SQLDisconnect; SQLFreeHandle fails on
        // a connected handle (HY010), so every connection and its handle stay open.
        if (m_phDbc[i])
            SQLFreeHandle(SQL_HANDLE_DBC, m_phDbc[i]);
    }
    delete[] m_phDbc;
    m_phDbc = NULL;
}

SQLHDBC CDBSessionMgr::GetConnection(CDBSessionMgr* pSession, int nIndex)
{
    if (pSession == NULL)
        return NULL;
    if (nIndex < 0 || nIndex >= pSession->m_nCount)
        return NULL;
    return pSession->m_phDbc[nIndex];
}

// ---------------------------------------------------------------------------------------------
// CDatabase: environment and pool registry

BOOL CDatabase::InitEnvironment(const char* pszDefaultConnect)
{
#ifdef JMX_FRAMEWORK_2011_01
    // GlobalManager 0x4c8720: the registry is created first, also when the environment fails.
    if (g_pSetDBSessionMgr == NULL)
        g_pSetDBSessionMgr = new std::set<CDBSessionMgr*>;
#endif
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &g_hDBEnv)))
    {
        g_hDBEnv = NULL;
        return FALSE;
    }
    if (!SQL_SUCCEEDED(SQLSetEnvAttr(g_hDBEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3,
                                     SQL_IS_INTEGER)))
    {
        SQLFreeHandle(SQL_HANDLE_ENV, g_hDBEnv);
        g_hDBEnv = NULL;
        return FALSE;
    }
    g_strDBConnect.assign(pszDefaultConnect, strlen(pszDefaultConnect));
    return TRUE;
}

#ifdef JMX_FRAMEWORK_2011_08
// SR_ShardManager 0x6b1ad0
CDBSessionMgr* CDatabase::CreateSession(int nCount, const char* pszConnect)
#else
CDBSessionMgr* CDatabase::CreateSession(const char* pszConnect)
#endif
{
    if (g_hDBEnv == NULL)
        return NULL;

    CDBSessionMgr* pSession = new CDBSessionMgr;
#ifdef JMX_FRAMEWORK_2011_08
    if (!pSession->Open(pszConnect, nCount))
#else
    if (!pSession->Open(pszConnect))
#endif
    {
        if (pSession)
            delete pSession;
        return NULL;
    }
#ifdef JMX_FRAMEWORK_2011_01
    // GlobalManager 0x4c8980
    g_pSetDBSessionMgr->insert(pSession);
#else
    g_setDBSessionMgr.insert(pSession);
#endif
    return pSession;
}

void CDatabase::DestroySession(CDBSessionMgr* pSession)
{
#ifdef JMX_FRAMEWORK_2011_01
    // GlobalManager 0x4c8a40
    // BUG(FW11-021): without a registry (before InitEnvironment, after DestroyAllSessions) the
    // NULL pointer is dereferenced.
    std::set<CDBSessionMgr*>& setSessions = *g_pSetDBSessionMgr;
    std::set<CDBSessionMgr*>::iterator it = setSessions.find(pSession);
    if (it == setSessions.end())
        return;
    setSessions.erase(it);
#else
    std::set<CDBSessionMgr*>::iterator it = g_setDBSessionMgr.find(pSession);
    if (it == g_setDBSessionMgr.end())
        return;
    g_setDBSessionMgr.erase(it);
#endif
    if (pSession->IsOpen())
        pSession->Close();
    if (pSession)
        delete pSession;
}

void CDatabase::DestroyAllSessions()
{
#ifdef JMX_FRAMEWORK_2011_01
    // GlobalManager 0x4c8810: nothing at all (not even the environment) without a registry;
    // the registry is deleted at the end.
    if (g_pSetDBSessionMgr == NULL)
        return;
#endif
    for (std::set<CDBSessionMgr*>::iterator it = DB_SESSION_SET.begin();
         it != DB_SESSION_SET.end(); ++it)
    {
        CDBSessionMgr* pSession = *it;
        if (pSession->IsOpen())
            pSession->Close();
        if (pSession)
            delete pSession;
    }
    DB_SESSION_SET.clear();

    if (g_hDBEnv)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, g_hDBEnv);
        g_hDBEnv = NULL;
    }
#ifdef JMX_FRAMEWORK_2011_01
    if (g_pSetDBSessionMgr)
    {
        delete g_pSetDBSessionMgr;
        g_pSetDBSessionMgr = NULL;
    }
#endif
}

// ---------------------------------------------------------------------------------------------
// CDatabase: statements

SQLHSTMT CDatabase::AllocStmt(CDBSessionMgr* pSession, int nConn)
{
    SQLHDBC hDbc = CDBSessionMgr::GetConnection(pSession, nConn);
    if (hDbc == NULL)
        return NULL;
    SQLHSTMT hStmt = NULL;
    SQLRETURN nRet = SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
    return SQL_SUCCEEDED(nRet) ? hStmt : NULL;
}

int CDatabase::ExecDirect(const char* pszSql, CDBSessionMgr* pSession, int nConn)
{
    if (pszSql == NULL || !lstrlenA(pszSql))
        return -1;
    SQLHSTMT hStmt = AllocStmt(pSession, nConn);
    if (hStmt == NULL)
        return -3;

    SQLRETURN nRet = SQLExecDirect(hStmt, (SQLCHAR*)pszSql, SQL_NTS);
    if (!SQL_SUCCEEDED(nRet) && nRet != SQL_NO_DATA)
        HandleSQLError(nRet, hStmt, pszSql);
    SQLFreeStmt(hStmt, SQL_CLOSE);
    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return nRet;
}

int CDatabase::ExecDirectWithReturnValue(CDBSessionMgr* pSession, int nConn, const char* pszSql,
                                         DWORD dwFlags, SQLPOINTER pValue)
{
    if (pszSql == NULL || !lstrlenA(pszSql))
        return -1;
    SQLHSTMT hStmt = AllocStmt(pSession, nConn);
    if (hStmt == NULL)
        return -3;

    SQLINTEGER cbValue = 0;
    if (dwFlags & DBEXEC_RETURN_INT)
    {
        if (pValue)
            SQLBindParameter(hStmt, 1, SQL_PARAM_OUTPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, pValue,
                             0, &cbValue);
    }
    else if ((dwFlags & DBEXEC_RETURN_BIGINT) && pValue)
    {
        SQLBindParameter(hStmt, 1, SQL_PARAM_OUTPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, pValue, 0,
                         &cbValue);
    }

    SQLRETURN nRet = SQLExecDirect(hStmt, (SQLCHAR*)pszSql, SQL_NTS);
    if (SQL_SUCCEEDED(nRet) && (dwFlags & (DBEXEC_RETURN_INT | DBEXEC_RETURN_BIGINT)) && pValue)
    {
        // BUG(DB-007): SQLMoreResults returning SQL_ERROR or SQL_INVALID_HANDLE repeats
        // forever.
        while (SQLMoreResults(hStmt) != SQL_NO_DATA)
            ;
        nRet = SQL_SUCCESS;
    }
    if (!SQL_SUCCEEDED(nRet) && nRet != SQL_NO_DATA)
        HandleSQLError(nRet, hStmt, pszSql);
    SQLFreeStmt(hStmt, SQL_CLOSE);
    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return nRet;
}

int CDatabase::ExecDirectWithParams(CDBSessionMgr* pSession, int nConn, const char* pszSql,
                                    DBParamList* pParams)
{
    if (pszSql == NULL || !lstrlenA(pszSql))
        return -1;
    SQLHSTMT hStmt = AllocStmt(pSession, nConn);
    if (hStmt == NULL)
        return -2;

    SQLRETURN nRet = SQL_SUCCESS;
    SQLUSMALLINT nParam = 0;
    for (DBParamList::iterator it = pParams->begin(); it != pParams->end(); ++it, ++nParam)
    {
        DB_PARAM& param = *it;
        SQLSMALLINT nCType;
        SQLSMALLINT nSqlType;
        switch (param.nType)
        {
        case DBPARAM_TINYINT:
            nCType = SQL_C_TINYINT;
            nSqlType = SQL_TINYINT;
            break;
        case DBPARAM_SMALLINT:
            nCType = SQL_C_SSHORT;
            nSqlType = SQL_SMALLINT;
            break;
        case DBPARAM_INTEGER:
            nCType = SQL_C_SLONG;
            nSqlType = SQL_INTEGER;
            break;
        case DBPARAM_BIGINT:
            nCType = SQL_C_SBIGINT;
            nSqlType = SQL_BIGINT;
            break;
        case DBPARAM_REAL:
            nCType = SQL_C_FLOAT;
            nSqlType = SQL_REAL;
            break;
        case DBPARAM_DOUBLE:
            nCType = SQL_C_DOUBLE;
            nSqlType = SQL_DOUBLE;
            break;
#ifdef JMX_FRAMEWORK_2011_01
        case DBPARAM_VARCHAR:
            // GlobalManager 0x4c9b27: the string itself, cbValue as column size, buffer length
            // and indicator.
            SQLBindParameter(hStmt, nParam + 1, param.nIOType, SQL_C_CHAR, SQL_VARCHAR,
                             param.cbValue, 0, param.pszValue, param.cbValue, &param.cbValue);
            continue;
#endif
        default:
            // BUG(DB-006): an unknown parameter type ends the function before SQLExecDirect;
            // the statement is never executed and the result is 0 (SQL_SUCCESS).
            goto done;
        }
        SQLBindParameter(hStmt, nParam + 1, param.nIOType, nCType, nSqlType, 0, 0,
                         &param.llValue, 0, &param.cbValue);
    }

    nRet = SQLExecDirect(hStmt, (SQLCHAR*)pszSql, SQL_NTS);
    if (SQL_SUCCEEDED(nRet) && pParams->size() != 0)
    {
        // BUG(DB-007): SQLMoreResults returning SQL_ERROR or SQL_INVALID_HANDLE repeats
        // forever.
        while (SQLMoreResults(hStmt) != SQL_NO_DATA)
            ;
        nRet = SQL_SUCCESS;
    }

done:
    if (!SQL_SUCCEEDED(nRet) && nRet != SQL_NO_DATA)
        HandleSQLError(nRet, hStmt, pszSql);
    SQLFreeStmt(hStmt, SQL_CLOSE);
    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return nRet;
}

int CDatabase::HandleSQLError(SQLRETURN nRet, SQLHANDLE hStmt, const char* pszQuery)
{
    char szLog[0x2206];
    char szLine[0x2206];
    // BUG(DB-004): not initialised; without a diagnostic record the final comparisons read
    // whatever is on the stack.
    SQLCHAR szState[6];
    SQLINTEGER nNativeError;
    SQLCHAR szMessage[512];
    SQLSMALLINT cbMessage;

    const char* pszText = pszQuery;
    if (pszText == NULL)
        pszText = "[Unknown query]";
#ifdef JMX_FRAMEWORK_2011_01
    // GlobalManager 0x4cce90: the result code is logged with the query.
    NetEngineLog(0x2000001, "Query Error!!! Query Result : %d, Query : %s", (int)nRet, pszText);
#else
    NetEngineLog(0x2000001, "%s", pszText);
#endif

    if (nRet == SQL_NO_DATA)
    {
        DB_TRACE(0);
        return 0;
    }

    SQLSMALLINT nRecord = 1;
    BOOL bFirst = TRUE;
    szLine[0] = 0;
    SQLRETURN nDiag;
    do
    {
        StringCchCopyA(szLog, 0x2206, szLine);
        nDiag = SQLGetDiagRec(SQL_HANDLE_STMT, hStmt, nRecord, szState, &nNativeError,
                               szMessage, 511, &cbMessage);
        if (nDiag == SQL_NO_DATA)
        {
            ++nRecord;
        }
        else
        {
            for (;;)
            {
                ++nRecord;
                if (nDiag == SQL_ERROR || nDiag == SQL_INVALID_HANDLE)
                    break;
                if (StringCchPrintfA(szLine, 0x2206,
                                     "SQL Error State:%s, Native Error Code: %lX, ODBC Error: %s",
                                     szState, nNativeError, szMessage))
                {
                    nDiag = SQL_ERROR;
                }
                size_t nLog = strlen(szLog);
                if (nLog && strlen(szLine) + nLog + 1 >= 0x50)
                {
                    // Line full: log it, the new text starts the next one.
                    if (StringCchCatA(szLog, 0x2206, "\n"))
                        nDiag = SQL_ERROR;
                    break;
                }
                StringCchCatA(szLog, 0x2206, szLine);
                nDiag = SQLGetDiagRec(SQL_HANDLE_STMT, hStmt, nRecord, szState, &nNativeError,
                                       szMessage, 511, &cbMessage);
                if (nDiag == SQL_NO_DATA)
                {
                    ++nRecord;
                    break;
                }
            }
        }

        // BUG(DB-003): the diagnostic text and the query are passed as format strings.
        NetEngineLog(0x2000001, szLog);
        if (pszText)
            NetEngineLog(0x2000001, pszText);
        if (bFirst)
            bFirst = FALSE;
    } while (nDiag != SQL_NO_DATA && nDiag != SQL_ERROR && nDiag != SQL_INVALID_HANDLE);

    if (!lstrcmpiA((LPCSTR)szState, "08S01"))
        NetEngineLog(0x2000001, "Database Connection was lost!!! Server should be shutted down !!!");
    return lstrcmpiA((LPCSTR)szMessage, "Connection is busy");
}

// ---------------------------------------------------------------------------------------------
// CDBRecord, CDBRecStream

// The abstract record classes have no factory.
BS_IMPLEMENT_RUNTIME_CLASS(CDBRecord, NULL, NULL, &CBase::classCBase)
BS_IMPLEMENT_RUNTIME_CLASS(CDBRecStream, NULL, NULL, &CDBRecord::classCDBRecord)

CDBRecord::CDBRecord()
{
    m_dw04 = 0;
    m_qwDirty = 0;
    m_pOwner = NULL;
    m_dw14 = 0;
}

CDBRecord::~CDBRecord()
{
}

#ifdef JMX_FRAMEWORK_2011_08
// SR_ShardManager 0x404080
void CDBRecord::InitData()
{
    m_qwDirty = 0;
}

// SR_ShardManager 0x402c70
BYTE* CDBRecord::GetData()
{
    return (BYTE*)this + GetTableInfo()->m_nDataOffset;
}

// DBRecord_TrimTrailingSpaces (0x402c40): ends the fetched CHAR value at nSize - 1 and blanks
// its trailing spaces; the first character is never looked at.
static void DBRecord_TrimTrailingSpaces(char* pszValue, int nSize)
{
    if (!pszValue)
    {
        BSAssertFail();
        return;
    }
    if (!nSize)
        return;
    pszValue[nSize - 1] = 0;
    if (nSize == 1)
        return;
    for (char* p = &pszValue[nSize - 2]; p != pszValue; --p)
    {
        if (*p != ' ')
            break;
        *p = 0;
    }
}

// SR_ShardManager 0x402c90
void CDBRecord::AllocData(int nRowSize)
{
    (void)nRowSize;
    if (GetTableInfo()->m_mapGroups.size() == 0)
        BSAssertFail();
    InitData();
}

// SR_ShardManager 0x402bd0
void CDBRecord::SetField(const DBColumnInfo* pColumn, const void* pValue)
{
    if (pColumn->nType == DBTYPE_STRING)
    {
        char* pszValue = (char*)pValue;
        DBRecord_TrimTrailingSpaces(pszValue, pColumn->nSize);
        std::string* pString = (std::string*)(GetData() + pColumn->nOffset);
        pString->assign(pszValue, strlen(pszValue));
    }
    else
    {
        memcpy(GetData() + pColumn->nOffset, pValue, pColumn->nSize);
    }
}

// SR_ShardManager 0x41a890
void CDBRecord::Reserved08(DWORD dwParam)
{
    (void)dwParam;
}

// SR_ShardManager 0x61a850
CDBRecStream::CDBRecStream()
{
    m_nFieldCount = 0;
    m_nWritePos = 0;
    m_nReadPos = 0;
    m_nCapacity = 0;
    m_pBuffer = NULL;
}

// SR_ShardManager 0x61a7b0
CDBRecStream::~CDBRecStream()
{
    if (m_pBuffer)
    {
        operator delete(m_pBuffer);
        m_pBuffer = NULL;
    }
    m_nFieldCount = 0;
    m_nWritePos = 0;
    m_nReadPos = 0;
    m_nCapacity = 0;
}

// SR_ShardManager 0x61a590
void CDBRecStream::AllocData(int nRowSize)
{
    if (m_pBuffer && (DWORD)nRowSize > m_nCapacity)
    {
        operator delete(m_pBuffer);
        m_pBuffer = NULL;
    }
    if (!m_pBuffer)
    {
        m_pBuffer = (BYTE*)operator new(nRowSize);
        m_nCapacity = nRowSize;
    }
    InitData();
}

// SR_ShardManager 0x61a580
void CDBRecStream::InitData()
{
    m_nFieldCount = 0;
    m_nWritePos = 0;
    m_nReadPos = 0;
}

// SR_ShardManager 0x61a560
BYTE* CDBRecStream::GetData()
{
    if (!m_pBuffer)
        BSAssertFail();
    return m_pBuffer;
}

// SR_ShardManager 0x61a430. A string is written as its length (cut to the column size - 4),
// the column size and column size - 4 bytes of the fetch buffer (zeros for an empty string).
void CDBRecStream::SetField(const DBColumnInfo* pColumn, const void* pValue)
{
    if (pColumn->nType == DBTYPE_STRING)
    {
        char* pszValue = (char*)pValue;
        DBRecord_TrimTrailingSpaces(pszValue, pColumn->nSize);
        if (lstrlenA(pszValue) >= 0xFFFF)
            BSAssertFail();
        WORD wLength = (WORD)lstrlenA(pszValue);
        WORD wColumnSize = (WORD)pColumn->nSize;
        if ((int)wLength > pColumn->nSize - 4)
            wLength = (WORD)(pColumn->nSize - 4);
        WriteRaw(2, &wLength);
        WriteRaw(2, &wColumnSize);
        DWORD dwBytes = (DWORD)wColumnSize - 4;
        void* pZeros = NULL;
        if (wLength == 0)
        {
            if (dwBytes == 0)
                BSAssertFail();
            pZeros = operator new(dwBytes);
            memset(pZeros, 0, dwBytes);
            pszValue = (char*)pZeros;
        }
        WriteRaw((WORD)dwBytes, pszValue);
        if (pZeros)
            operator delete(pZeros);
    }
    else
    {
        WriteRaw((WORD)pColumn->nSize, pValue);
    }
}

// SR_ShardManager 0x61a510
void CDBRecStream::WriteRaw(WORD nSize, const void* pData)
{
    if (!m_pBuffer)
        BSAssertFail();
    if (m_pBuffer)
    {
        if (m_nWritePos + nSize > m_nCapacity)
        {
            BSAssertFail();
            // BUG(SM-001): rethrows without an active exception, which ends the process.
            throw;
        }
        memcpy(m_pBuffer + m_nWritePos, pData, nSize);
        m_nWritePos += nSize;
    }
}
#endif

// The shift is done on the low byte of the group number (_allshl): 0 from 64 on.
static unsigned __int64 DirtyBit(int nGroup)
{
    BYTE nShift = (BYTE)nGroup;
    if (nShift >= 64)
        return 0;
    return (unsigned __int64)1 << nShift;
}

BOOL CDBRecord::IsDirty(int nGroup) const
{
    return (m_qwDirty & DirtyBit(nGroup)) != 0;
}

void CDBRecord::ClearDirty(int nGroup)
{
#ifdef JMX_FRAMEWORK_2011_08
    // SR_ShardManager 0x43f920: a group from 64 on asserts; a negative one clears the bit of
    // its low byte like DirtyBit (January 2011: both ignored).
    if (nGroup >= 64)
    {
        BSAssertFail();
        return;
    }
#else
    if ((unsigned)nGroup > 63)
        return;
#endif
    m_qwDirty &= ~DirtyBit(nGroup);
}

// The original pushes the column values onto the machine stack in reverse order and passes
// them to StringCchPrintfA; this keeps the same layout in a buffer used as the va_list.
class CDBArgStack
{
public:
    explicit CDBArgStack(size_t nDwords)
        : m_vec(nDwords ? nDwords : 1), m_nTop(m_vec.size())
    {
    }

    void Push(DWORD dw)
    {
        m_vec[--m_nTop] = dw;
    }

    // Pushes the value of one column the way the original's switch does.
    void PushColumn(const DBColumnInfo& col, const BYTE* pData)
    {
        const BYTE* p = pData + col.nOffset;
        switch (col.nType)
        {
        case DBTYPE_INT64:
            Push(((const DWORD*)p)[1]);
            Push(((const DWORD*)p)[0]);
            break;
        case DBTYPE_STRING:
            Push((DWORD)((const std::string*)p)->c_str());
            break;
        case DBTYPE_FLOAT:
            {
                double d = *(const float*)p;
                Push(((const DWORD*)&d)[1]);
                Push(((const DWORD*)&d)[0]);
            }
            break;
        case DBTYPE_TIMESTAMP:
            {
                const WORD* pw = (const WORD*)p;
                for (int i = 5; i >= 0; --i)
                    Push(pw[i]);
            }
            break;
        default:
            {
                DWORD dw = *(const DWORD*)p;
                if (col.nType == DBTYPE_SHORT)
                {
                    dw &= 0xFFFF;
                    if (dw & 0x8000)
                        dw |= 0xFFFF0000;
                }
                else if (col.nType == DBTYPE_BYTE)
                {
                    dw &= 0xFF;
                }
                Push(dw);
            }
            break;
        }
    }

    // Pushes a column group last column first.
    void PushColumns(const DBColumnVector& vec, const BYTE* pData)
    {
        for (DBColumnVector::const_reverse_iterator it = vec.rbegin(); it != vec.rend(); ++it)
            PushColumn(*it, pData);
    }

    va_list Args()
    {
        return (va_list)&m_vec[m_nTop];
    }

private:
    std::vector<DWORD> m_vec;
    size_t m_nTop;
};

// Six dwords per column are enough for every type.
static size_t ArgStackSize(CDBTableInfo* pInfo)
{
    size_t n = 0;
    for (DBFieldGroupMap::iterator it = pInfo->m_mapGroups.begin();
         it != pInfo->m_mapGroups.end(); ++it)
    {
        n += it->second.vecColumns.size();
    }
    return (n * 2 + 1) * 6;
}

static HRESULT PrintfArgs(char* pszDest, const char* pszFormat, va_list args)
{
    return StringCchVPrintfA(pszDest, DB_STATEMENT_SIZE, pszFormat, args);
}

int CDBRecord::BuildUpdate(std::string& strOut, int nGroup)
{
    char szText[DB_STATEMENT_SIZE];

    strOut.clear();
    CDBTableInfo* pInfo = GetTableInfo();
    if (!(pInfo->m_dwFlags & DBTABLE_UPDATE) || nGroup == 0)
        return 0;
    DB_FIELD_GROUP* pKey = pInfo->FindKeyGroup();
    if (pKey == NULL)
        return 0;
    if (nGroup != -1 && (nGroup == DBGROUP_ALL || !IsDirty(nGroup)))
        return 0;

    BYTE* pData = GetData();
    CDBArgStack args(ArgStackSize(pInfo));

    if (nGroup != -1)
    {
        if (IsDirty(nGroup))
        {
            args.PushColumns(pKey->vecColumns, pData);
            DB_FIELD_GROUP& group = pInfo->m_mapGroups.find(nGroup)->second;
            args.PushColumns(group.vecColumns, pData);
            if (PrintfArgs(szText, group.strUpdate.c_str(), args.Args()))
                return 0;
            strOut.assign(szText);
        }
#ifdef JMX_FRAMEWORK_2011_08
        // SR_ShardManager 0x6b52aa: a negative group asserts instead of being cleared.
        if (nGroup < 0)
            BSAssertFail();
        else
            ClearDirty(nGroup);
#else
        ClearDirty(nGroup);
#endif
        return 1;
    }

    if (m_qwDirty == 0)
    {
        m_qwDirty = 0;
        return 1;
    }

    args.PushColumns(pKey->vecColumns, pData);
    if (PrintfArgs(szText, pInfo->m_strKeyWhere.c_str(), args.Args()))
        return 0;
    std::string strWhere(szText);
    szText[0] = 0;
    if (StringCchPrintfA(szText, DB_STATEMENT_SIZE, "UPDATE %s SET ", pInfo->m_strTable.c_str()))
        return 0;
    strOut.assign(szText);

    for (DBFieldGroupMap::iterator it = pInfo->m_mapGroups.begin();
         it != pInfo->m_mapGroups.end(); ++it)
    {
        int nKey = it->first;
        if (nKey == DBGROUP_KEY || nKey == DBGROUP_ALL || !IsDirty(nKey))
            continue;
        szText[0] = 0;
        DB_FIELD_GROUP& group = it->second;
        // The values of the earlier groups stay below on the stack.
        args.PushColumns(group.vecColumns, pData);
        if (PrintfArgs(szText, group.strSet.c_str(), args.Args()))
            return 0;
        strOut.append(szText);
        strOut.append(",");
    }

    strOut.at(strOut.size() - 1) = ' ';
    strOut.append(strWhere);
    m_qwDirty = 0;
    return 1;
}

#ifdef JMX_FRAMEWORK_2011_08
// SR_ShardManager 0x6b2f60: always connection 0, through ExecDirectWithReturnValue without an
// output parameter.
int CDBRecord::Update(CDBTable* pTable, int nGroup)
{
    std::string strSql;
    if (BuildUpdate(strSql, nGroup) != 1)
        return 0;
    return CDatabase::ExecDirectWithReturnValue(pTable->m_pSession, 0, strSql.c_str(), 0, NULL);
}
#else
int CDBRecord::Update(CDBTable* pTable, int nConn, int nGroup)
{
    std::string strSql;
    if (BuildUpdate(strSql, nGroup) != 1)
        return 0;
    return CDatabase::ExecDirect(strSql.c_str(), pTable->m_pSession, nConn);
}
#endif

// ---------------------------------------------------------------------------------------------
// _record_allocator

_record_allocator::_record_allocator(CDBTable* pTable)
{
    m_pTable = pTable;
}

_record_allocator::~_record_allocator()
{
}

CDBRecord* _record_allocator::AllocRecord()
{
    return (CDBRecord*)m_pTable->m_pInfo->m_pRecordClass->CreateObject();
}

// CRuntimeClass deleter (0x487ed0): m_pfnDeleteObject inside a structured exception handler.
static void RuntimeClass_DeleteObject(CRuntimeClass* pClass, CBase* pObject)
{
    if (pClass->m_pfnDeleteObject == NULL)
    {
        DB_TRACE(pClass->m_lpszClassName);
        return;
    }
    __try
    {
        (*pClass->m_pfnDeleteObject)(pObject);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        DB_TRACE(pClass->m_lpszClassName);
    }
}

void _record_allocator::FreeRecord(CDBRecord* pRecord)
{
    RuntimeClass_DeleteObject(m_pTable->m_pInfo->m_pRecordClass, pRecord);
}

void _record_allocator::vf03(DWORD* pdw1, DWORD* pdw2)
{
    *pdw1 = 0;
    *pdw2 = 0;
}

// ---------------------------------------------------------------------------------------------
// CDBTableInfo

DB_FIELD_GROUP* CDBTableInfo::FindKeyGroup()
{
    DBFieldGroupMap::iterator it = m_mapGroups.find(DBGROUP_KEY);
    if (it == m_mapGroups.end())
        return NULL;
    return &it->second;
}

void CDBTableInfo::BuildSqlTemplates(int nDescribed)
{
    char szText[DB_STATEMENT_SIZE];
    char szPart[DB_STATEMENT_SIZE];

    DB_TRACE(0);
    DB_TRACE(m_strTable.c_str());
    DB_TRACE(0);
    std::string strNoLock(" WITH (NOLOCK)");
    std::string strSpace(" ");
    std::string strFirstKey;

    // key clause
    DBFieldGroupMap::iterator itKey = m_mapGroups.find(DBGROUP_KEY);
    if (itKey != m_mapGroups.end())
    {
        DBColumnVector& vecKey = itKey->second.vecColumns;
        if (vecKey.size())
        {
            StringCchPrintfA(szPart, DB_STATEMENT_SIZE, "WHERE ");
            for (DBColumnVector::iterator it = vecKey.begin(); it != vecKey.end(); ++it)
            {
                StringCchPrintfA(szPart, DB_STATEMENT_SIZE, "%s%s=%s", szPart,
                                 it->strName.c_str(), it->strFormat.c_str());
                if (strFirstKey.empty())
                    strFirstKey.assign(it->strName.c_str());
                if (&*it == &vecKey.back())
                    break;
                StringCchPrintfA(szPart, DB_STATEMENT_SIZE, "%s AND ", szPart);
            }
            m_strKeyWhere.assign(szPart);
        }
    }

    // UPDATE text of each update group
    if ((m_dwFlags & DBTABLE_UPDATE) && m_strKeyWhere.size())
    {
        szPart[0] = 0;
        memset(szPart + 1, 0, sizeof(szPart) - 1);
        for (DBFieldGroupMap::iterator it = m_mapGroups.begin(); it != m_mapGroups.end(); ++it)
        {
            if (it->first == DBGROUP_KEY || it->first == DBGROUP_ALL)
                continue;
            StringCchPrintfA(szText, DB_STATEMENT_SIZE, "UPDATE %s SET ", m_strTable.c_str());
            DB_FIELD_GROUP& group = it->second;
            DBColumnVector& vec = group.vecColumns;
            for (DBColumnVector::iterator itCol = vec.begin(); itCol != vec.end(); ++itCol)
            {
                StringCchPrintfA(szText, DB_STATEMENT_SIZE, "%s%s=%s", szText,
                                 itCol->strName.c_str(), itCol->strFormat.c_str());
                StringCchPrintfA(szPart, DB_STATEMENT_SIZE, "%s%s=%s", szPart,
                                 itCol->strName.c_str(), itCol->strFormat.c_str());
                if (&*itCol == &vec.back())
                    break;
                StringCchPrintfA(szText, DB_STATEMENT_SIZE, "%s,", szText);
                StringCchPrintfA(szPart, DB_STATEMENT_SIZE, "%s,", szPart);
            }
            group.strUpdate.assign(szText);
            group.strSet.assign(szPart);
            if (m_strKeyWhere.size())
            {
                group.strUpdate.append(strSpace);
                group.strUpdate.append(m_strKeyWhere);
            }
            DB_TRACE(group.strUpdate.c_str());
            DB_TRACE(group.strSet.c_str());
            szText[0] = 0;
            szPart[0] = 0;
        }
    }

    // UPDATE text over all groups
    if ((m_dwFlags & DBTABLE_UPDATE) && m_strKeyWhere.size())
    {
        StringCchPrintfA(szText, DB_STATEMENT_SIZE, "UPDATE %s SET ", m_strTable.c_str());
        for (DBFieldGroupMap::iterator it = m_mapGroups.begin(); it != m_mapGroups.end(); ++it)
        {
            if (it->first == DBGROUP_KEY)
                continue;
            DBColumnVector& vec = it->second.vecColumns;
            for (DBColumnVector::iterator itCol = vec.begin(); itCol != vec.end(); ++itCol)
            {
                StringCchPrintfA(szText, DB_STATEMENT_SIZE, "%s%s=%s,", szText,
                                 itCol->strName.c_str(), itCol->strFormat.c_str());
            }
        }
        szText[lstrlenA(szText) - 1] = ' ';
        m_strUpdateAll.assign(szText);
        if (m_strKeyWhere.size())
            m_strUpdateAll.append(m_strKeyWhere);
        DB_TRACE(m_strUpdateAll.c_str());
    }

    // INSERT text (identity columns left out)
    if (m_dwFlags & DBTABLE_INSERT)
    {
        StringCchPrintfA(szText, DB_STATEMENT_SIZE, "INSERT INTO %s(", m_strTable.c_str());
        DBFieldGroupMap::iterator it;
        for (it = m_mapGroups.begin(); it != m_mapGroups.end(); ++it)
        {
            DBColumnVector& vec = it->second.vecColumns;
            for (DBColumnVector::iterator itCol = vec.begin(); itCol != vec.end(); ++itCol)
            {
                if (itCol->bIdentity != 1)
                {
                    StringCchPrintfA(szText, DB_STATEMENT_SIZE, "%s%s,", szText,
                                     itCol->strName.c_str());
                }
            }
        }
        szText[lstrlenA(szText) - 1] = ' ';
        StringCchPrintfA(szText, DB_STATEMENT_SIZE, "%s) VALUES(", szText);
        for (it = m_mapGroups.begin(); it != m_mapGroups.end(); ++it)
        {
            DBColumnVector& vec = it->second.vecColumns;
            for (DBColumnVector::iterator itCol = vec.begin(); itCol != vec.end(); ++itCol)
            {
                if (itCol->bIdentity != 1)
                {
                    StringCchPrintfA(szText, DB_STATEMENT_SIZE, "%s%s,", szText,
                                     itCol->strFormat.c_str());
                }
            }
        }
        szText[lstrlenA(szText) - 1] = ' ';
        StringCchPrintfA(szText, DB_STATEMENT_SIZE, "%s)", szText);
        m_strInsert.assign(szText);
        DB_TRACE(m_strInsert.c_str());
    }

    m_nColumns = 0;
    for (DBFieldGroupMap::iterator it = m_mapGroups.begin(); it != m_mapGroups.end(); ++it)
        m_nColumns += (int)it->second.vecColumns.size();

    // SELECT text: the declared columns when they differ from the table's
    if (m_mapGroups.size() > 1 && m_nColumns != nDescribed)
    {
        StringCchPrintfA(szText, DB_STATEMENT_SIZE, "SELECT ");
        for (DBFieldGroupMap::iterator it = m_mapGroups.begin(); it != m_mapGroups.end(); ++it)
        {
            DBColumnVector& vec = it->second.vecColumns;
            for (DBColumnVector::iterator itCol = vec.begin(); itCol != vec.end(); ++itCol)
            {
                StringCchPrintfA(szText, DB_STATEMENT_SIZE, "%s%s,", szText,
                                 itCol->strName.c_str());
            }
        }
        szText[lstrlenA(szText) - 1] = ' ';
        StringCchPrintfA(szText, DB_STATEMENT_SIZE, "%sFROM %s", szText, m_strTable.c_str());
    }
    else
    {
        StringCchPrintfA(szText, DB_STATEMENT_SIZE, "SELECT * FROM %s", m_strTable.c_str());
    }
    m_strSelect.assign(szText);
    m_strSelectNoLock.assign(m_strSelect, 0, std::string::npos);
    m_strSelectNoLock.append(strNoLock);
    if (m_strKeyWhere.size())
    {
        m_strSelect.append(strSpace);
        m_strSelect.append(m_strKeyWhere);
        m_strSelectNoLock.append(strSpace);
        m_strSelectNoLock.append(m_strKeyWhere);
    }
    DB_TRACE(m_strSelect.c_str());
    DB_TRACE(m_strSelectNoLock.c_str());

    if (m_dwFlags & DBTABLE_DELETE)
    {
        StringCchPrintfA(szText, DB_STATEMENT_SIZE, "DELETE FROM %s", m_strTable.c_str());
        m_strDelete.assign(szText);
        DB_TRACE(m_strDelete.c_str());
    }

    if (m_strKeyWhere.size())
    {
        StringCchPrintfA(szText, DB_STATEMENT_SIZE, "SELECT COUNT(%s) ", strFirstKey.c_str());
        StringCchPrintfA(szText, DB_STATEMENT_SIZE, "%sFROM %s", szText, m_strTable.c_str());
        m_strCount.assign(szText);
        if (m_strKeyWhere.size())
        {
            m_strCount.append(strSpace);
            m_strCount.append(m_strKeyWhere);
        }
        DB_TRACE(m_strCount.c_str());
    }
}

// ---------------------------------------------------------------------------------------------
// CDBTable

CDBTable::CDBTable()
{
    m_pSession = NULL;
    m_pInfo = NULL;
    m_pAllocator = NULL;
    m_pfnFetch = NULL;
}

CDBTable::~CDBTable()
{
    Close();
}

void CDBTable::Close()
{
    m_pSession = NULL;
    if (m_pInfo)
        m_pInfo->m_pTable = NULL;
    m_pInfo = NULL;
    if (m_pAllocator)
    {
        delete m_pAllocator;
        m_pAllocator = NULL;
    }
}

int CDBTable::Open(CDBTableInfo* pInfo, CDBSessionMgr* pSession, _record_allocator* pAllocator)
{
    if (m_pAllocator)
    {
        delete m_pAllocator;
        m_pAllocator = NULL;
    }
    m_pSession = pSession;
    m_pInfo = pInfo;
    pInfo->m_pTable = this;
    if (pAllocator)
        m_pAllocator = pAllocator;
    else
        m_pAllocator = new _record_allocator(this);

    if (m_pSession)
        return DescribeColumns();

    DB_TRACE(0);
    DB_TRACE(pInfo->m_strTable.c_str());
    DB_TRACE(0);
    DB_TRACE(pInfo->m_strTable.c_str());
    return 1;
}

CDBRecord* CDBTable::CreateRecord()
{
    CDBRecord* pRecord = m_pAllocator->AllocRecord();
    pRecord->AllocData(m_pInfo->m_nRowSize);
    pRecord->InitData();
    pRecord->m_pOwner = this;
    return pRecord;
}

void CDBTable::FreeRecord(CDBRecord* pRecord)
{
    if (pRecord == NULL || m_pAllocator == NULL || pRecord->m_pOwner == NULL)
        return;
    if (pRecord->m_pOwner == this)
    {
        pRecord->m_pOwner = NULL;
        m_pAllocator->FreeRecord(pRecord);
        return;
    }
    __try
    {
        pRecord->m_pOwner->FreeRecord(pRecord);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

BOOL CDBTable::CheckKeyArgCount(int nArgs)
{
    DB_FIELD_GROUP* pKey = m_pInfo->FindKeyGroup();
    if (pKey == NULL)
        return nArgs == 0;
    return (int)pKey->vecColumns.size() == nArgs;
}

// Fetch helpers: the shared tail of the three fetch functions.
static int FinishFetch(SQLRETURN nRet, SQLHSTMT hStmt, const char* pszSql, int nResult)
{
    if (!SQL_SUCCEEDED(nRet) && nRet != SQL_NO_DATA)
        CDatabase::HandleSQLError(nRet, hStmt, pszSql);
    if (nRet == SQL_NO_DATA)
        nResult = 1;
    return nResult;
}

// Clears a bound value the driver reported as NULL.
static void ClearNullValue(const DBColumnInfo& col, BYTE* pValue)
{
    if (col.bNullable && *(SQLINTEGER*)(pValue + col.nSize) == SQL_NULL_DATA)
    {
        if (col.nType == DBTYPE_STRING)
            *pValue = 0;
        else
            memset(pValue, 0, col.nSize);
    }
}

int CDBTable::FetchBoundByOrdinal(int nConn, const char* pszSql, CDBRecordList* pList)
{
    BYTE abBuffer[DB_FETCH_BUFFER_SIZE];

    // every value is followed by its 4-byte indicator
    int nRowSize = m_pInfo->m_nRowSize + 4 * m_pInfo->m_nColumns;
    SQLUINTEGER nRows = DB_FETCH_BUFFER_SIZE / (unsigned)nRowSize;
    if (nRows == 0)
        return 0;
    int nResult = 0;
    SQLUINTEGER nFetched = 0;
    SQLHSTMT hStmt = CDatabase::AllocStmt(m_pSession, nConn);
    if (hStmt == NULL)
        return 0;

    SQLSetStmtAttr(hStmt, SQL_ATTR_ROW_BIND_TYPE, (SQLPOINTER)nRowSize, 0);
    SQLSetStmtAttr(hStmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)nRows, 0);
    SQLSetStmtAttr(hStmt, SQL_ATTR_ROWS_FETCHED_PTR, &nFetched, 0);

    SQLRETURN nRet;
    DBFieldGroupMap& groups = m_pInfo->m_mapGroups;
    for (DBFieldGroupMap::iterator it = groups.begin(); it != groups.end(); ++it)
    {
        DBColumnVector& vec = it->second.vecColumns;
        for (DBColumnVector::iterator col = vec.begin(); col != vec.end(); ++col)
        {
            BYTE* pValue = abBuffer + col->nBufOffset + 4 * (col->nOrdinal - 1);
            nRet = SQLBindCol(hStmt, (SQLUSMALLINT)col->nOrdinal, DB_ColumnTypeToCType(col->nType),
                              pValue, col->nSize, (SQLINTEGER*)(pValue + col->nSize));
            if (!SQL_SUCCEEDED(nRet))
                goto finish;
        }
    }

    nRet = SQLExecDirect(hStmt, (SQLCHAR*)pszSql, SQL_NTS);
    if (!SQL_SUCCEEDED(nRet))
    {
        if (pszSql && lstrlenA(pszSql) > 0)
            DB_TRACE(pszSql);
        goto finish;
    }

    // BUG(DB-012): only SQL_NO_DATA ends the loop; a failing SQLFetchScroll repeats forever.
    for (nRet = SQLFetchScroll(hStmt, SQL_FETCH_NEXT, 0); nRet != SQL_NO_DATA;
         nRet = SQLFetchScroll(hStmt, SQL_FETCH_NEXT, 0))
    {
        if (nFetched > nRows)
            goto finish;
        BYTE* pRow = abBuffer;
        for (SQLUINTEGER nRow = 0; nRow < nFetched; ++nRow)
        {
            CDBRecord* pRecord = CreateRecord();
            for (DBFieldGroupMap::iterator it = groups.begin(); it != groups.end(); ++it)
            {
                DBColumnVector& vec = it->second.vecColumns;
                for (DBColumnVector::iterator col = vec.begin(); col != vec.end(); ++col)
                {
                    BYTE* pValue = pRow + col->nBufOffset + 4 * (col->nOrdinal - 1);
                    ClearNullValue(*col, pValue);
                    pRecord->SetField(&*col, pValue);
                }
            }
            pList->push_back(pRecord);
            pRow += nRowSize;
        }
    }
    nResult = 1;

finish:
    nResult = FinishFetch(nRet, hStmt, pszSql, nResult);
    SQLCloseCursor(hStmt);
    SQLFreeStmt(hStmt, SQL_CLOSE);
    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return nResult;
}

int CDBTable::FetchBoundSequential(int nConn, const char* pszSql, CDBRecordList* pList)
{
    BYTE abBuffer[DB_FETCH_BUFFER_SIZE];

    int nRowSize = m_pInfo->m_nRowSize + 4 * m_pInfo->m_nColumns;
    SQLUINTEGER nRows = DB_FETCH_BUFFER_SIZE / (unsigned)nRowSize;
    if (nRows == 0)
        return 0;
    int nResult = 0;
    SQLUINTEGER nFetched = 0;
    SQLHSTMT hStmt = CDatabase::AllocStmt(m_pSession, nConn);
    if (hStmt == NULL)
        return 0;

    SQLSetStmtAttr(hStmt, SQL_ATTR_ROW_BIND_TYPE, (SQLPOINTER)nRowSize, 0);
    SQLSetStmtAttr(hStmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)nRows, 0);
    SQLSetStmtAttr(hStmt, SQL_ATTR_ROWS_FETCHED_PTR, &nFetched, 0);

    SQLRETURN nRet;
    DBFieldGroupMap& groups = m_pInfo->m_mapGroups;
    BYTE* pValue = abBuffer;
    SQLUSMALLINT nColumn = 1;
    for (DBFieldGroupMap::iterator it = groups.begin(); it != groups.end(); ++it)
    {
        DBColumnVector& vec = it->second.vecColumns;
        for (DBColumnVector::iterator col = vec.begin(); col != vec.end(); ++col)
        {
            nRet = SQLBindCol(hStmt, nColumn, DB_ColumnTypeToCType(col->nType), pValue,
                              col->nSize, (SQLINTEGER*)(pValue + col->nSize));
            pValue += col->nSize + 4;
            if (!SQL_SUCCEEDED(nRet))
                goto finish;
            ++nColumn;
        }
    }

    nRet = SQLExecDirect(hStmt, (SQLCHAR*)pszSql, SQL_NTS);
    if (!SQL_SUCCEEDED(nRet))
    {
        if (pszSql && lstrlenA(pszSql) > 0)
            DB_TRACE(pszSql);
        goto finish;
    }

    nRet = SQLFetchScroll(hStmt, SQL_FETCH_NEXT, 0);
    if (nRet != SQL_NO_DATA)
    {
        do
        {
            if (nFetched > nRows)
                goto finish;
            pValue = abBuffer;
            for (SQLUINTEGER nRow = 0; nRow < nFetched; ++nRow)
            {
                CDBRecord* pRecord = CreateRecord();
                for (DBFieldGroupMap::iterator it = groups.begin(); it != groups.end(); ++it)
                {
                    DBColumnVector& vec = it->second.vecColumns;
                    for (DBColumnVector::iterator col = vec.begin(); col != vec.end(); ++col)
                    {
                        ClearNullValue(*col, pValue);
                        pRecord->SetField(&*col, pValue);
                        pValue += col->nSize + 4;
                    }
                }
                pList->push_back(pRecord);
            }
            nRet = SQLFetchScroll(hStmt, SQL_FETCH_NEXT, 0);
        } while (nRet != SQL_NO_DATA);     // BUG(DB-012)
    }
    nResult = 1;

finish:
    nResult = FinishFetch(nRet, hStmt, pszSql, nResult);
    SQLCloseCursor(hStmt);
    SQLFreeStmt(hStmt, SQL_CLOSE);
    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return nResult;
}

int CDBTable::FetchWithGetData(const char* pszSql, CDBTable* pTable, int nConn,
                               CDBRecordList* pList)
{
    BYTE abValue[DB_STATEMENT_SIZE];
    SQLSMALLINT nColumns;
    SQLSMALLINT nDataType;
    SQLUINTEGER nColumnSize;
    SQLSMALLINT nNullable;
    SQLINTEGER nIndicator;

    int nResult = 0;
    SQLHSTMT hStmt = CDatabase::AllocStmt(pTable->m_pSession, nConn);
    if (hStmt == NULL)
        return 0;

    SQLUINTEGER nFetchedUnused = 0;
    SQLSetStmtAttr(hStmt, SQL_ATTR_ROWS_FETCHED_PTR, &nFetchedUnused, 0);
    SQLRETURN nRet = SQLExecDirect(hStmt, (SQLCHAR*)pszSql, SQL_NTS);
    if (!SQL_SUCCEEDED(nRet))
        goto finish;
    nRet = SQLFetch(hStmt);
    if (!SQL_SUCCEEDED(nRet))
        goto finish;
    nRet = SQLNumResultCols(hStmt, &nColumns);
    if (!SQL_SUCCEEDED(nRet) || nColumns == 0)
        goto finish;

    for (;;)
    {
        CDBRecord* pRecord = pTable->CreateRecord();
        SQLUSMALLINT nColumn = 1;
        DBFieldGroupMap& groups = pTable->m_pInfo->m_mapGroups;
        for (DBFieldGroupMap::iterator it = groups.begin(); it != groups.end(); ++it)
        {
            DBColumnVector& vec = it->second.vecColumns;
            for (DBColumnVector::iterator col = vec.begin(); col != vec.end(); ++col)
            {
                // The column name goes into the value buffer first.
                SQLDescribeCol(hStmt, nColumn, abValue, DB_STATEMENT_SIZE, NULL, &nDataType,
                                &nColumnSize, NULL, &nNullable);
                if (col->nType == DBTYPE_STRING)
                {
                    if (nColumnSize > DB_STATEMENT_SIZE)
                        nColumnSize = DB_STATEMENT_SIZE;
                    nRet = SQLGetData(hStmt, nColumn, SQL_C_CHAR, abValue, nColumnSize,
                                      &nIndicator);
                    if (nIndicator == SQL_NULL_DATA)
                        abValue[0] = 0;
                }
                else
                {
                    nRet = SQLGetData(hStmt, nColumn, DB_ColumnTypeToCType(col->nType), abValue,
                                      nColumnSize, &nIndicator);
                    if (nIndicator == SQL_NULL_DATA)
                        memset(abValue, 0, nColumnSize);
                }
                // BUG(DB-009): the record is neither stored nor freed on a failed SQLGetData.
                if (!SQL_SUCCEEDED(nRet))
                    goto finish;
                pRecord->SetField(&*col, abValue);
                ++nColumn;
            }
        }
        pList->push_back(pRecord);
        // Any SQLFetch failure ends the loop as success; nRet keeps the last SQLGetData result.
        if (!SQL_SUCCEEDED(SQLFetch(hStmt)))
            break;
    }
    nResult = 1;

finish:
    nResult = FinishFetch(nRet, hStmt, pszSql, nResult);
    SQLFreeStmt(hStmt, SQL_CLOSE);
    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return nResult;
}

int CDBTable::DescribeColumns()
{
    char szColumn[129];
    char szTypeName[129];
    SQLINTEGER cbColumn;
    SQLSMALLINT nDataType;
    SQLINTEGER cbDataType;
    SQLINTEGER cbTypeName;
    SQLINTEGER nColumnSize;
    SQLINTEGER cbColumnSize;
    SQLINTEGER nBufferLength;
    SQLINTEGER cbBufferLength;
    SQLSMALLINT nNullable;
    SQLINTEGER cbNullable;
    SQLINTEGER nOrdinal;
    SQLINTEGER cbOrdinal;

    szColumn[0] = 0;
    memset(szColumn + 1, 0, 128);
    szTypeName[0] = 0;
    memset(szTypeName + 1, 0, 128);

    int nResult = 0;
    int nPrevOffset = 0;
    int nPrevSize = 0;
    int nDescribed = 0;
    SQLHSTMT hStmt = CDatabase::AllocStmt(m_pSession, 0);
    SQLRETURN nRet = SQLColumns(hStmt, NULL, 0, NULL, 0, (SQLCHAR*)m_pInfo->m_strTable.c_str(),
                                 SQL_NTS, NULL, 0);
    if (SQL_SUCCEEDED(nRet))
    {
        SQLBindCol(hStmt, 4, SQL_C_CHAR, szColumn, 129, &cbColumn);
        SQLBindCol(hStmt, 5, SQL_C_SSHORT, &nDataType, 0, &cbDataType);
        SQLBindCol(hStmt, 6, SQL_C_CHAR, szTypeName, 129, &cbTypeName);
        SQLBindCol(hStmt, 7, SQL_C_SLONG, &nColumnSize, 0, &cbColumnSize);
        SQLBindCol(hStmt, 8, SQL_C_SLONG, &nBufferLength, 0, &cbBufferLength);
        SQLBindCol(hStmt, 11, SQL_C_SSHORT, &nNullable, 0, &cbNullable);
        SQLBindCol(hStmt, 17, SQL_C_SLONG, &nOrdinal, 0, &cbOrdinal);

        DBFieldGroupMap& groups = m_pInfo->m_mapGroups;
        BOOL bAuto = groups.size() == 2;
        m_pInfo->m_nRowSize = 0;
        int nBufOffset = 0;

        nRet = SQLFetch(hStmt);
        if (!lstrlenA(szColumn))
        {
            // BUG(DB-008): a table without columns returns here and leaks the statement.
            return 0;
        }
        while (SQL_SUCCEEDED(nRet))
        {
            ++nDescribed;
            if (bAuto)
            {
                DB_FIELD_GROUP& group = groups.find(DBGROUP_ALL)->second;
                DBColumnInfo col;
                col.bNullable = nNullable;
                col.nOrdinal = nOrdinal;
                col.bIdentity = strstr(szTypeName, "identity") != NULL;
                col.nSize = nBufferLength;
                col.nBufOffset = nBufOffset;
                col.strName.assign(szColumn, strlen(szColumn));
                col.nType = DB_SqlTypeToColumnType(nDataType);
                // BUG(DB-010): types without a format (SQL_FLOAT, unknown types) crash in strlen.
                const char* pszFormat = DB_ColumnTypeFormat(col.nType);
                col.strFormat.assign(pszFormat, strlen(pszFormat));
                int nAlign = DB_ColumnTypeAlignment(col.nType);
                col.nOffset = nAlign * ((nAlign + nPrevSize + nPrevOffset - 1) / nAlign);
                nPrevSize = DB_ColumnTypeSize(col.nType);
                nPrevOffset = col.nOffset;
                group.vecColumns.push_back(col);
                if (col.bIdentity == 1)
                    m_pInfo->m_pIdentity = &group.vecColumns.back();
                m_pInfo->m_nRowSize += nBufferLength;
            }
            else
            {
                // first declared column of that name, in group order
                DBColumnInfo* pCol = NULL;
                for (DBFieldGroupMap::iterator it = groups.begin();
                     it != groups.end() && pCol == NULL; ++it)
                {
                    DBColumnVector& vec = it->second.vecColumns;
                    for (DBColumnVector::iterator col = vec.begin(); col != vec.end(); ++col)
                    {
                        const char* pszName = col->strName.c_str();
                        int nLen = lstrlenA(szColumn);
                        if (CompareStringA(0x400, NORM_IGNORECASE | NORM_IGNOREKANATYPE, pszName,
                                           lstrlenA(pszName), szColumn, nLen) == CSTR_EQUAL)
                        {
                            pCol = &*col;
                            break;
                        }
                    }
                }
                if (pCol)
                {
                    pCol->bNullable = nNullable;
                    pCol->nOrdinal = nOrdinal;
                    pCol->bIdentity = strstr(szTypeName, "identity") != NULL;
                    if (pCol->nSize && pCol->nType == DBTYPE_STRING
                        && pCol->nSize != nBufferLength)
                    {
                        // "string column whose length differs from the declared one!"
                        if (!BSAssertDialog(DB_ASSERT_LINE_STRING_SIZE, s_szDatabaseCpp,
                                            "\xbd\xba\xc6\xae\xb8\xb5 \xc4\xc3\xb7\xb3\xc0\xce"
                                            "\xb5\xa5 \xb1\xe6\xc0\xcc\xb0\xa1 \xb8\xed\xbd\xc3"
                                            "\xc0\xfb\xc0\xb8\xb7\xce \xc1\xa4\xc7\xd8\xc1\xd8 "
                                            "\xb0\xcd\xb0\xfa \xb4\xd9\xb8\xa3\xb4\xd9!\r\n"
                                            "[TblName:%s, ColName:%s]",
                                            m_pInfo->m_strTable.c_str(), szColumn))
                        {
                            __debugbreak();
                        }
                    }
                    pCol->nSize = nBufferLength;
                    pCol->nBufOffset = nBufOffset;
                    pCol->nType = DB_SqlTypeToColumnType(nDataType);
                    // BUG(DB-010)
                    const char* pszFormat = DB_ColumnTypeFormat(pCol->nType);
                    pCol->strFormat.assign(pszFormat, strlen(pszFormat));
                    int nAlign = DB_ColumnTypeAlignment(pCol->nType);
                    if (nAlign * ((nAlign + nPrevSize + nPrevOffset - 1) / nAlign)
                        != pCol->nOffset)
                    {
                        // "a declared column was not bound! [%s::%s] a column that should have
                        // been bound before is missing, or its name differs from the DB"
                        if (!BSAssertDialog(DB_ASSERT_LINE_OFFSET, s_szDatabaseCpp,
                                            "\xbf\xc0\xc8\xaa! \xbc\xb1\xbe\xf0\xc0\xba \xc7\xcf"
                                            "\xb0\xed Bind \xbd\xc3\xc5\xb0\xc1\xf6 \xbe\xca\xc0"
                                            "\xba \xc4\xc3\xb7\xb3\xc0\xcc \xc0\xd6\xb4\xd9! \n"
                                            " \t\t[%s::%s] \xc0\xcc\xc0\xfc\xbf\xa1 Bind \x89"
                                            "\xe7\xbe\xee\xbe\xdf \xc7\xd2 \xc4\xc3\xb7\xb3\xc0"
                                            "\xcc \xba\xfc\xc1\xb3\xb0\xc5\xb3\xaa DB\xbf\xcd "
                                            "\xc4\xc3\xb7\xb3 \xc0\xcc\xb8\xa7\xc0\xcc \xb4\xde"
                                            "\xb6\xf3\xbc\xad \xb9\xd9\xc0\xce\xb5\xf9 \xbd\xc7"
                                            "\xc6\xd0\xc7\xdf\xb4\xd9!\n",
                                            m_pInfo->m_strTable.c_str(), szColumn))
                        {
                            __debugbreak();
                        }
                    }
                    nPrevSize = DB_ColumnTypeSize(pCol->nType);
                    nPrevOffset = pCol->nOffset;
                    if (pCol->bIdentity == 1)
                        m_pInfo->m_pIdentity = pCol;
                    m_pInfo->m_nRowSize += nBufferLength;
                }
            }
            // unmatched columns still take their place in the result row
            nBufOffset += nBufferLength;
            nRet = SQLFetch(hStmt);
            if (!lstrlenA(szColumn))
                return 0;
        }
        nResult = 1;
    }

    if (hStmt)
    {
        SQLFreeStmt(hStmt, SQL_CLOSE);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    }
    if (nResult == 1)
    {
        m_pInfo->BuildSqlTemplates(nDescribed);
        if (m_pInfo->m_nColumns == nDescribed)
            m_pfnFetch = &CDBTable::FetchBoundByOrdinal;
        else
            m_pfnFetch = &CDBTable::FetchBoundSequential;
    }
    return nResult;
}

int CDBTable::SelectAll(int nConn, CDBRecordList* pList)
{
    std::string strSql;
    strSql.assign(m_pInfo->m_strSelect, 0, std::string::npos);
    if (m_pInfo->m_strKeyWhere.size())
        strSql.assign(strSql.substr(0, strSql.find("WHERE", 0, 5)), 0, std::string::npos);
    return (this->*m_pfnFetch)(nConn, strSql.c_str(), pList);
}

int CDBTable::SelectCustom(const std::string& strCondition, int nConn, CDBRecordList* pList,
                           int nTemplate)
{
    std::string strSql;
    std::string strLower;
    StringToLowerMbcs(strCondition.c_str(), strLower);
    if (strstr(strLower.c_str(), "select "))
    {
        strSql.assign(strCondition, 0, std::string::npos);
    }
    else
    {
        strSql.assign(nTemplate == 1 ? m_pInfo->m_strSelectNoLock : m_pInfo->m_strSelect, 0,
                      std::string::npos);
        strSql.assign(strSql.substr(0, strSql.find(" FROM", 0, 5)), 0, std::string::npos);
        strSql.append(" ", 1);
        strSql.append(strCondition, 0, std::string::npos);
    }
    return (this->*m_pfnFetch)(nConn, strSql.c_str(), pList);
}

int CDBTable::SelectWhere(const char* pszClause, int nConn, CDBRecordList* pList, int nTemplate)
{
    int nLen = lstrlenA(pszClause);
    if (nLen == 0)
        return nLen;

    std::string strSql;
    strSql.assign(nTemplate == 1 ? m_pInfo->m_strSelectNoLock : m_pInfo->m_strSelect, 0,
                  std::string::npos);
    if (m_pInfo->m_strKeyWhere.size())
        strSql.assign(strSql.substr(0, strSql.find("WHERE", 0, 5)), 0, std::string::npos);
    strSql.append(" ", 1);
    strSql.append(pszClause, strlen(pszClause));
    return (this->*m_pfnFetch)(nConn, strSql.c_str(), pList);
}

// _vsnprintf_s into a 3 KB statement buffer; FALSE when the text does not fit.
static BOOL FormatStatement(char* pszText, const char* pszFormat, va_list args)
{
    int n = _vsnprintf_s(pszText, DB_STATEMENT_SIZE, DB_STATEMENT_SIZE - 1, pszFormat, args);
    if (n < 0 || n > DB_STATEMENT_SIZE - 1)
        return FALSE;
    if (n == DB_STATEMENT_SIZE - 1)
        pszText[DB_STATEMENT_SIZE - 1] = 0;
    return TRUE;
}

int CDBTable::SelectByKey(int nConn, CDBRecordList* pList, int nTemplate, int nArgs, ...)
{
    char szSql[DB_STATEMENT_SIZE];
    if (!CheckKeyArgCount(nArgs))
        return 0;

    const std::string& strFormat =
        nTemplate == 1 ? m_pInfo->m_strSelectNoLock : m_pInfo->m_strSelect;
    va_list args;
    va_start(args, nArgs);
    BOOL bOk = FormatStatement(szSql, strFormat.c_str(), args);
    va_end(args);
    if (!bOk)
        return 0;
    return (this->*m_pfnFetch)(nConn, szSql, pList);
}

DWORD CDBTable::CountByKey(int nConn, int nArgs, ...)
{
    char szSql[DB_STATEMENT_SIZE];
    if (!CheckKeyArgCount(nArgs))
        return 0;

    va_list args;
    va_start(args, nArgs);
    BOOL bOk = FormatStatement(szSql, m_pInfo->m_strCount.c_str(), args);
    va_end(args);
    if (!bOk)
        return 0;

    SQLUINTEGER nCount = 0;
    SQLINTEGER cbCount = 0;
    SQLHSTMT hStmt = CDatabase::AllocStmt(m_pSession, nConn);
    if (hStmt == NULL)
        return 0;
    SQLBindCol(hStmt, 1, SQL_C_ULONG, &nCount, 0, &cbCount);
    if (SQL_SUCCEEDED(SQLExecDirect(hStmt, (SQLCHAR*)szSql, SQL_NTS)))
        SQLFetch(hStmt);
    SQLFreeStmt(hStmt, SQL_CLOSE);
    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return nCount;
}

int CDBTable::DeleteByKey(int nConn, int nArgs, ...)
{
    char szSql[DB_STATEMENT_SIZE];
    if (!(m_pInfo->m_dwFlags & DBTABLE_DELETE) || !m_pInfo->FindKeyGroup()
        || !m_pInfo->m_strKeyWhere.size() || !CheckKeyArgCount(nArgs))
    {
        return 0;
    }

    va_list args;
    va_start(args, nArgs);
    BOOL bOk = FormatStatement(szSql, m_pInfo->m_strKeyWhere.c_str(), args);
    va_end(args);
    if (!bOk)
        return 0;

    std::string strSpace(" ");
    std::string strWhere(szSql);
    std::string strDelete(m_pInfo->m_strDelete);
    strDelete.append(strSpace, 0, std::string::npos);
    strDelete.append(strWhere, 0, std::string::npos);
    // BUG(DB-005): executes the formatted key clause alone instead of strDelete.
    return CDatabase::ExecDirect(szSql, m_pSession, nConn);
}
