#include "DBQuery.h"

#include <stdarg.h>
#include <string.h>

#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerDataExtern.h"

int DB_SqlTypeToByteSize(int nSqlType)
{
    switch (nSqlType)
    {
    case SQL_INTEGER:
    case SQL_REAL:
        return 4;
    case SQL_SMALLINT:
        return 2;
    case SQL_FLOAT:
    case SQL_DOUBLE:
    case SQL_BIGINT:
        return 8;
    case SQL_TYPE_DATE:
    case SQL_TYPE_TIME:
        return 6;
    case SQL_TYPE_TIMESTAMP:
        return 16;
    case SQL_TINYINT:
        return 1;
    default:
        return 0;
    }
}

void DB_TrimRightSpaces(char* psz, int nLen)
{
    char* p = psz + nLen - 1;
    while (p != psz && *p == ' ')
    {
        *p = 0;
        --p;
    }
}

static BOOL IsWideCharType(SQLSMALLINT nType)
{
    return nType == SQL_WCHAR || nType == SQL_WVARCHAR || nType == SQL_WLONGVARCHAR;
}

static BOOL IsCharType(SQLSMALLINT nType)
{
    return nType == SQL_CHAR || nType == SQL_VARCHAR || nType == SQL_LONGVARCHAR;
}

// The query text HandleSQLError logs: the January 2011 library (GlobalManager 0x4869e0) passes
// m_szQuery, the 2008 one NULL ("[Unknown query]").
#ifdef JMX_FRAMEWORK_2011_01
#define DBQUERY_ERROR_TEXT m_szQuery
#else
#define DBQUERY_ERROR_TEXT NULL
#endif

int CDBQuery::Execute()
{
    int nResult = -1;
#ifdef JMX_FRAMEWORK_2011_01
    // GlobalManager 0x4869e0: initialised, freed only when allocated (DB-002 fixed).
    SQLHSTMT hStmt = NULL;
#else
    // BUG(DB-002): not initialised; the termination handler frees it even when no statement
    // was allocated.
    SQLHSTMT hStmt;
#endif

    __try
    {
        SQLHDBC hDbc = CDBSessionMgr::GetConnection(m_pSession, m_nSession);
        if (hDbc == NULL)
        {
            LogPrintf(0x2000000, "cannot occupy db session : %s", m_szQuery);
            return -1;
        }
        SQLRETURN nLastRet = SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
        if (!SQL_SUCCEEDED(nLastRet))
        {
            LogPrintf(0x2000000, "failed : SQLAllocHandle : %s", m_szQuery);
            return -1;
        }

        SQLUINTEGER nRowsFetched = 0;
        nLastRet = SQLSetStmtAttr(hStmt, SQL_ATTR_ROWS_FETCHED_PTR, &nRowsFetched, 0);
        SQLRETURN nRet = SQLExecDirect(hStmt, (SQLCHAR*)m_szQuery, SQL_NTS);
        nLastRet = nRet;
        if (!SQL_SUCCEEDED(nRet) && nRet != SQL_NO_DATA)
        {
            LogPrintf(0x2000000, "execute query error : %s", m_szQuery);
            CDatabase::HandleSQLError(nRet, hStmt, DBQUERY_ERROR_TEXT);
            nResult = -2;
            __leave;
        }

        SQLSMALLINT nColumns = 0;
        nRet = SQLFetch(hStmt);
        nLastRet = nRet;
        if (!SQL_SUCCEEDED(nRet))
        {
            // no row: look for a later result set with columns
            for (;;)
            {
                nRet = SQLMoreResults(hStmt);
                nLastRet = nRet;
                if (!SQL_SUCCEEDED(nRet))
                {
                    if (nRet != SQL_NO_DATA)
                    {
                        CDatabase::HandleSQLError(nRet, hStmt, DBQUERY_ERROR_TEXT);
                        nResult = -2;
                        __leave;
                    }
                    nResult = 1;
                    __leave;
                }
                nRet = SQLNumResultCols(hStmt, &nColumns);
                nLastRet = nRet;
                if (nColumns != 0)
                    break;
            }
            if (!SQL_SUCCEEDED(nRet))
            {
                nResult = 1;
                __leave;
            }
            nRet = SQLFetch(hStmt);
            nLastRet = nRet;
            if (!SQL_SUCCEEDED(nRet))
            {
                CDatabase::HandleSQLError(nRet, hStmt, DBQUERY_ERROR_TEXT);
                nResult = 1;
                __leave;
            }
        }

        nResult = 0;
        nRet = SQLNumResultCols(hStmt, &nColumns);
        nLastRet = nRet;
        if (!SQL_SUCCEEDED(nRet) || nColumns == 0)
        {
            CDatabase::HandleSQLError(nRet, hStmt, DBQUERY_ERROR_TEXT);
            nResult = -2;
            __leave;
        }

        SQLUINTEGER nColumnSize = 0;
        SQLSMALLINT nDataType = 0;
        SQLSMALLINT nNullable = 0;
        SQLINTEGER nIndicator = 0;
        int nRow = 0;
        for (;;)
        {
            BYTE* p;
            if (m_pTrigger)
            {
                m_bRowFetched = 0;
                if (!m_pTrigger->OnTrigger2((DWORD)this, 0))
                {
                    CDatabase::HandleSQLError(nLastRet, hStmt, DBQUERY_ERROR_TEXT);
                    nResult = 1;
                    __leave;
                }
                p = m_pBuffer;
            }
            else
            {
                if (nRow >= m_nBufferSize / m_nRowSize)
                {
                    CDatabase::HandleSQLError(nLastRet, hStmt, DBQUERY_ERROR_TEXT);
                    nResult = 1;
                    __leave;
                }
                p = m_pBuffer + nRow * m_nRowSize;
            }

            int nUsed = 0;
            for (int nColumn = 1; nColumn <= nColumns; ++nColumn)
            {
                nRet = SQLDescribeCol(hStmt, (SQLUSMALLINT)nColumn, NULL, 0, NULL, &nDataType,
                                      &nColumnSize, NULL, &nNullable);
                nLastRet = nRet;
                if (!SQL_SUCCEEDED(nRet))
                {
                    CDatabase::HandleSQLError(nRet, hStmt, DBQUERY_ERROR_TEXT);
                    nResult = -5;
                    __leave;
                }
                nRet = SQLGetData(hStmt, (SQLUSMALLINT)nColumn,
                                  nDataType != SQL_TYPE_TIMESTAMP ? SQL_C_BINARY : SQL_TIMESTAMP,
                                  p, m_nRowSize - nUsed, &nIndicator);
                nLastRet = nRet;
                if (!SQL_SUCCEEDED(nRet))
                {
                    CDatabase::HandleSQLError(nRet, hStmt, DBQUERY_ERROR_TEXT);
                    nResult = -3;
                    __leave;
                }

                if (nIndicator == SQL_NULL_DATA && nNullable == SQL_NULLABLE)
                {
                    int nSize = DB_SqlTypeToByteSize(nDataType);
                    nIndicator = nSize;
                    if (IsWideCharType(nDataType))
                    {
                        *(WORD*)p = 0;
                        nIndicator = 2 * nColumnSize;
                    }
                    else if (IsCharType(nDataType))
                    {
                        *p = 0;
                        nIndicator = nColumnSize;
                    }
                    else
                    {
                        memset(p, 0, nSize);
                    }
                }
                else if (IsWideCharType(nDataType))
                {
                    nIndicator = 2 * nColumnSize;
                }
                else if (IsCharType(nDataType))
                {
                    // BUG(DB-011): trims from the full data length, also past the end of a
                    // truncated value.
                    DB_TrimRightSpaces((char*)p, nIndicator);
                    nIndicator = nColumnSize;
                }
                nUsed += nIndicator;
                p += nIndicator;
                if (nColumn + 1 <= nColumns && nUsed > m_nRowSize - 1)
                {
                    BSAssertFail();
                    CDatabase::HandleSQLError(nLastRet, hStmt, DBQUERY_ERROR_TEXT);
                    nResult = -4;
                    __leave;
                }
            }

            ++nRow;
            if (m_pTrigger)
            {
                m_bRowFetched = 1;
                if (!m_pTrigger->OnTrigger2((DWORD)this, 0))
                {
                    CDatabase::HandleSQLError(nLastRet, hStmt, DBQUERY_ERROR_TEXT);
                    nResult = 1;
                    __leave;
                }
            }
            if (!SQL_SUCCEEDED(SQLFetch(hStmt)))
                __leave;
        }
    }
    __finally
    {
#ifdef JMX_FRAMEWORK_2011_01
        if (hStmt)
        {
            SQLFreeStmt(hStmt, SQL_CLOSE);
            SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        }
#else
        SQLFreeStmt(hStmt, SQL_CLOSE);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
#endif
    }
    return nResult;
}

int CDBQuery::ExecuteFormat(const char* pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    int n = _vsnprintf_s(m_szQuery, sizeof(m_szQuery), sizeof(m_szQuery) - 1, pszFormat, args);
    va_end(args);
    if (n < 0 || n > (int)sizeof(m_szQuery) - 1)
    {
        m_szQuery[sizeof(m_szQuery) - 1] = 0;
        BSAssertFail();
    }
    else if (n == sizeof(m_szQuery) - 1)
    {
        m_szQuery[sizeof(m_szQuery) - 1] = 0;
    }
    return Execute();
}
