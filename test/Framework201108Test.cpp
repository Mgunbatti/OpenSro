// The August 2011 framework of SR_ShardManager (JMX_FRAMEWORK_2011_08, the "August 2011
// (SR_ShardManager)" table of JMX_ServerFramework\FRAMEWORK_2011.md). The test is compiled with
// SHARD_DEFS and links the August builds of the libraries (SHARD_LIBS); it checks the behaviour
// that differs from the July and January 2011 code Framework2011Test and Framework201101Test
// check.

#include "BSLib/ChunkAllocator.h"
#include "BSLib/DB/Database.h"
#include "ServerFramework/CertArch.h"

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
    TestWithAccessDriver();
    return TEST_RESULT("Framework201108Test");
}
