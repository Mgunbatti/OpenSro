#pragma once

#include <windows.h>
#include <sql.h>

#include "BSLib/DB/Database.h"
#include "ServerFramework/Trigger.h"

// Query object of the server code (0x2824 bytes, built on the stack by the callers).
//
// Usage (certification, module version and shard service loading):
//
//     CDBQuery q;
//     q.m_pSession = <pool from CDatabase::CreateSession>;
//     q.m_pBuffer = (BYTE*)&row;  q.m_nBufferSize = sizeof(row);  q.m_nRowSize = sizeof(row);
//     q.m_pTrigger = NULL;        q.m_nSession = <connection index of the calling thread>;
//     int r = q.ExecuteFormat("exec _SomeProc '%s',%d", psz, n);
//
// Result rows are read column by column with SQLGetData as raw data (SQL_C_BINARY; timestamps
// as SQL_C_TIMESTAMP) and packed one after the other into a row of m_nRowSize bytes: fixed-size
// values take their natural size, CHAR / VARCHAR / LONGVARCHAR take the column size (trailing
// blanks cleared to 0), WCHAR types twice the column size, NULL values are zero-filled.
//
// Without a trigger row n goes to m_pBuffer + n * m_nRowSize, up to m_nBufferSize bytes. With a
// trigger every row goes to m_pBuffer, and m_pTrigger->OnTrigger2((DWORD)this, 0) is called
// twice per row: before it with m_bRowFetched == 0 (may move m_pBuffer) and after it with
// m_bRowFetched == 1 (takes the row). The trigger returns 0 to stop the query. The original
// passes only the query pointer; the forwarding CClassTrigger thunks do not care.
//
// When the first result set has no row, the following result sets are searched for one with
// columns (procedures that run other statements first).
//
// Execute results:
//     0   rows were read (all of them, or until SQLFetch ended)
//     1   no row; the row buffer is full; the trigger returned 0; no further result set
//    -1   no connection m_nSession in m_pSession ("cannot occupy db session : %s"), or no
//         statement handle ("failed : SQLAllocHandle : %s")
//    -2   SQLExecDirect failed ("execute query error : %s"), SQLMoreResults or
//         SQLNumResultCols failed, or the result has no columns
//    -3   SQLGetData failed
//    -4   a row needs more than m_nRowSize bytes
//    -5   SQLDescribeCol failed
// The lines are logged with LogPrintf(0x2000000); statement errors also go through
// CDatabase::HandleSQLError without query text.
class CDBQuery
{
public:
    // CDBSessionMgr::ExecuteQuery (0x41cb80): executes m_szQuery.
    int Execute();
    // DBQuery_FormatAndExecute (0x41ca80): _vsnprintf_s into m_szQuery (0x2800 bytes; on
    // overflow the text is cut and BSAssertFail is called), then Execute.
    int ExecuteFormat(const char* pszFormat, ...);

public:
    CDBSessionMgr*              m_pSession;         // +0000
    BYTE*                       m_pBuffer;          // +0004
    int                         m_nBufferSize;      // +0008
    int                         m_nRowSize;         // +000C
    ServerFramework::CTrigger*  m_pTrigger;         // +0010
    char                        m_szQuery[0x2800];  // +0014
    BYTE                        m_bRowFetched;      // +2814 0 before, 1 after a row (trigger)
    DWORD                       m_dwParam;          // +2818 free for the caller / trigger
    void*                       m_pUserData;        // +281C free for the caller / trigger
    int                         m_nSession;         // +2820 connection index in m_pSession
};

C_ASSERT(FIELD_OFFSET(CDBQuery, m_szQuery) == 0x14);
C_ASSERT(FIELD_OFFSET(CDBQuery, m_bRowFetched) == 0x2814);
C_ASSERT(FIELD_OFFSET(CDBQuery, m_nSession) == 0x2820);
C_ASSERT(sizeof(CDBQuery) == 0x2824);

// ODBC_SqlTypeToByteSize (0x41cb10): size of a fixed-size SQL type's value, 0 otherwise.
int DB_SqlTypeToByteSize(int nSqlType);
// StrTrimRightSpaces (0x41cae0): clears trailing blanks of the first nLen bytes of psz to 0;
// the first byte is never touched.
void DB_TrimRightSpaces(char* psz, int nLen);
