// The 2011 framework of AgentServer (JMX_FRAMEWORK_2011, JMX_ServerFramework\FRAMEWORK_2011.md).
// The test is compiled with AGENT_DEFS and links the 2011 builds of the libraries (AGENT_LIBS);
// it checks every behaviour that differs from the 2008 framework the other tests check.

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "TestCheck.h"
#include "LoopbackBind.h"

#ifndef JMX_FRAMEWORK_2011
#error Framework2011Test is compiled with JMX_FRAMEWORK_2011 (see the Makefile)
#endif

// =============================================================================================
// BSLib and BSNet

#include <stdarg.h>
#include <stdexcept>
#include <utility>

#include "BSLib/BSDebug.h"
#include "BSLib/ChunkAllocator.h"
#include "BSLib/thread/System.h"
#include "BSLib/thread/ThreadManager.h"
#include "BSLib/Util/Registry.h"
#include "BSLib/Util/StringUtil.h"
#include "BSNet/Msg.h"
#include "BSNet/MsgPool.h"
#include "BSNet/NetEngine.h"
#include "BSNet/NetEngineLog.h"
#include "BSNet/SockStream.h"

static std::string s_strLibDir;     // temporary directory, also the current directory

struct LIB_LOG
{
    DWORD       dwFlags;
    std::string strText;
};
static std::vector<LIB_LOG> s_vecLibLog;

static int LibLog(DWORD dwFlags, const char* pszFormat, ...)
{
    char sz[2048];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf_s(sz, sizeof(sz), _TRUNCATE, pszFormat, args);
    va_end(args);
    LIB_LOG log;
    log.dwFlags = dwFlags;
    log.strText = sz;
    s_vecLibLog.push_back(log);
    return 0;
}

// Flags of the logged line with exactly this text, -1 when there is none.
static DWORD LibLogFlags(const char* pszText)
{
    for (size_t i = 0; i < s_vecLibLog.size(); ++i)
    {
        if (s_vecLibLog[i].strText == pszText)
            return s_vecLibLog[i].dwFlags;
    }
    return (DWORD)-1;
}

static int FaultingLog(DWORD, const char*, ...)
{
    RaiseException(0xE0001234, 0, 0, NULL);
    return 0;
}

static std::vector<std::pair<DWORD, DWORD> > s_vecLibDump;

static void __cdecl LibDump(DWORD dwOpcode, DWORD dwCount)
{
    s_vecLibDump.push_back(std::make_pair(dwOpcode, dwCount));
}

static std::string ReadWholeFile(const std::string& strPath)
{
    std::string str;
    FILE* pFile = NULL;
    if (fopen_s(&pFile, strPath.c_str(), "rb"))
        return str;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), pFile)) > 0)
        str.append(buf, n);
    fclose(pFile);
    return str;
}

// Names of the files in the directory that match the pattern.
static std::vector<std::string> FindFiles(const std::string& strDir, const char* pszPattern)
{
    std::vector<std::string> v;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((strDir + pszPattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return v;
    do
    {
        v.push_back(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return v;
}

struct POOL_ITEM
{
    void Init(DWORD dwParam) { m_dwInit = dwParam; }
    DWORD m_dwInit;
};

static void TestLibraryLayout2011()
{
    CHECK(sizeof(CChunkAllocatorST<POOL_ITEM>) == 0x5C);
    CHECK(sizeof(CChunkAllocatorMT<POOL_ITEM>) == 0x98);
    CHECK(FIELD_OFFSET(CChunkAllocatorST<POOL_ITEM>, m_strName) == 0x40);
    CHECK(FIELD_OFFSET(CChunkAllocatorMT<POOL_ITEM>, m_cs) == 0x5C);
    CHECK(sizeof(CMsgPool) == 0xA4 && FIELD_OFFSET(CMsgPool, m_setInUse) == 0x98);
    CHECK(sizeof(CSessionTable) == 0xC8 && FIELD_OFFSET(CSessionTable, m_vecSessions) == 0xB8);
    CHECK(sizeof(BSNET_INIT_PARAM) == 0x114);
    CHECK(sizeof(CNetEngine) == 0x4E0);
    CHECK(FIELD_OFFSET(CNetEngine, m_csKeepAlive) == 0x11C);
    CHECK(FIELD_OFFSET(CNetEngine, m_ConnectPool) == 0x198);
    CHECK(FIELD_OFFSET(CNetEngine, m_SessionTable) == 0x360);
    CHECK(FIELD_OFFSET(CNetEngine, m_MsgPool) == 0x430);
    CHECK(FIELD_OFFSET(CNetEngine, m_listKeepAlive) == 0x4D4);
    CHECK(sizeof(CRegistry) == 8);
    CHECK(sizeof(CInternalException) == 8 && sizeof(CMsgException) == 0xC);

    BSNET_INIT_PARAM param;
    bool bZero = true;
    for (int i = 0; i < 0x80; ++i)
        bZero = bZero && param.szMsgPoolName[i] == 0;
    CHECK(bZero && param.bSecurityMode == 1 && param.dwMaxConnectTime == 60);
}

static void TestChunkAllocatorName()
{
    CChunkAllocatorST<POOL_ITEM> a1;
    CHECK(a1.m_strName == "Unknown");
    CChunkAllocatorST<POOL_ITEM> a2(&POOL_ITEM::Init, 7, 3, "");
    CHECK(a2.m_strName == "Unknown" && a2.m_nItemsPerBlock == 3 && a2.m_dwInitParam == 7);
    CChunkAllocatorMT<POOL_ITEM> a3(NULL, 0, 500, "Pool::Name");
    CHECK(a3.m_strName == "Pool::Name" && a3.m_nItemsPerBlock == 500);
    a3.SetName(NULL);
    CHECK(a3.m_strName == "Unknown");
    a3.SetName("Other");
    CHECK(a3.m_strName == "Other");

    // the pool still works, with the init function
    POOL_ITEM* p = a2.Alloc();
    CHECK(p && p->m_dwInit == 7 && a2.m_nCapacity == 3 && a2.m_nInUse == 1);
    a2.Free(p);
    CHECK(a2.m_nInUse == 0);

    // the failure log of AllocItemBlock (unreachable: new throws) uses 0x2000000
    CHECK(CHUNK_ALLOCATOR_LOG_FLAGS == 0x2000000);
}

static void TestMsgException()
{
    CMsg msg;
    msg.Reset();
    msg << (DWORD)0x11223344;
    DWORD dw = 0;
    msg >> dw;
    CHECK(dw == 0x11223344);

    bool bThrown = false;
    try
    {
        msg >> dw;
    }
    catch (CInternalException& e)
    {
        CMsgException* pMsgEx = dynamic_cast<CMsgException*>(&e);
        bThrown = pMsgEx && strcmp(e.GetName(), "CMsgException") == 0 && pMsgEx->m_dw4 == 1
                  && pMsgEx->m_dw8 == 1;
    }
    CHECK(bThrown);

    // reading from the end of the payload
    CMsg msg2;
    msg2.Reset();
    msg2 << (WORD)0x1234;
    msg2.m_bReadForward = 0;
    unsigned __int64 qw = 0;
    bThrown = false;
    try
    {
        msg2 >> qw;
    }
    catch (CMsgException& e)
    {
        bThrown = strcmp(e.GetName(), "CMsgException") == 0;
    }
    CHECK(bThrown);
    WORD w = 0;
    msg2 >> w;
    CHECK(w == 0x1234 && msg2.GetPayloadSize() == 0);

    CInternalException base;
    CHECK(strcmp(base.GetName(), "CInternalException") == 0);
}

static void TestStringUtil()
{
    CHECK(UTF8_GetCharLength('a') == 1 && UTF8_GetCharLength(0xC3) == 2);
    CHECK(UTF8_GetCharLength(0xE4) == 3 && UTF8_GetCharLength(0xF0) == 4);
    CHECK(UTF8_GetCharLength(0x80) == 0 && UTF8_GetCharLength(0xF8) == 0);

    std::string s1("a\xC3\xA9\xE4\xB8\x80");
    CHECK(UTF8_GetCharCount(s1) == 3);
    CHECK(UTF8_GetCharCount(std::string()) == 0);
    CHECK(UTF8_GetCharStartBefore(std::string("abcdefg"), 5) == 4);
    CHECK(UTF8_GetCharStartBefore(s1, 2) == 1);
    CHECK(UTF8_GetCharStartBefore(s1, 100) == 3);
    CHECK(UTF8_GetDisplayWidth(s1) == 5);
    CHECK(UTF8_GetDisplayWidth(std::string("ab\x80" "cd")) == 2);

    CHECK(UTF8_TruncateToWidth(std::string("abc"), 5) == "abc");
    CHECK(UTF8_TruncateToWidth(std::string("abcdef"), 3) == "abc");
    // a wide character may end one column beyond the width
    CHECK(UTF8_TruncateToWidth(std::string("a\xE4\xB8\x80\xE4\xB8\x80" "b"), 2)
          == "a\xE4\xB8\x80");

    // a lead byte past the end: the checked iterator throws
    bool bThrown = false;
    try
    {
        UTF8_GetCharCount(std::string("a\xE4"));
    }
    catch (std::out_of_range&)
    {
        bThrown = true;
    }
    CHECK(bThrown);

    CHECK(!UTF8_IsUtf8(std::string("abc")));
    CHECK(UTF8_IsUtf8(std::string("a\xC3\xA9")));
    CHECK(UTF8_IsUtf8(std::string("\xF0\x9F\x98\x80")));
    CHECK(!UTF8_IsUtf8(std::string("\xC3")));                  // cut off before any sequence
    CHECK(UTF8_IsUtf8(std::string("a\xC3\xA9\xE4\xB8")));      // cut off after one
    CHECK(!UTF8_IsUtf8(std::string("\x80" "a")));
    CHECK(!UTF8_IsUtf8(std::string("\xC3\xA9\xC3" "a")));
    CHECK(!UTF8_IsUtf8(std::string("\xFE\x80")));
    CHECK(!UTF8_IsUtf8(std::string("\xC3\xA9\0b", 4)));

    CHECK(MultiByteToWString("abc", 3) == L"abc");
    CHECK(MultiByteToWString("", 0) == L"");
    CHECK(MultiByteToWString("abc", -1).size() == 3);
    CHECK(WideCharToString(L"xyz", 3) == "xyz");
    CHECK(WideCharToString(L"", 0) == "");
    CHECK(WideCharToString(L"xyz", -1).size() == 3);

    char sz[8];
    memset(sz, 0, sizeof(sz));
    CHECK(WideCharToMultiByte_wrap(L"xy", 2, sz, sizeof(sz)) && strcmp(sz, "xy") == 0);
    CHECK(!WideCharToMultiByte_wrap(L"abcdef", 6, sz, 2));
}

static const char s_szTestRegKey[] = "Software\\JMXFramework2011Test";

static void TestRegistry()
{
    HKEY hKey = NULL;
    CHECK(RegCreateKeyA(HKEY_CURRENT_USER, s_szTestRegKey, &hKey) == ERROR_SUCCESS);
    RegCloseKey(hKey);

    {
        CRegistry reg;
        DWORD dw = 0;
        std::string str;
        char sz[256];
        BYTE ab[4];
        CHECK(reg.m_hKey == NULL);
        CHECK(!reg.SetDword("a", 1) && !reg.SetString("b", "x") && !reg.SetBinary("c", ab, 4));
        CHECK(!reg.QueryDword("a", &dw) && !reg.QueryString("b", &str));
        CHECK(!reg.QueryBinary("c", ab, 4) && !reg.QueryString("b", sz));
        CHECK(reg.Close());
        CHECK(!reg.Open("Software\\JMXFramework2011Test\\Missing"));

        CHECK(reg.Open(s_szTestRegKey) && reg.m_hKey != NULL);
        CHECK(reg.SetDword("Dword", 0x12345678));
        CHECK(reg.QueryDword("Dword", &dw) && dw == 0x12345678);
        CHECK(!reg.QueryDword("NoValue", &dw));

        CHECK(reg.SetString("String", "hello"));
        DWORD dwType = 0;
        DWORD cb = 0;
        CHECK(RegQueryValueExA(reg.m_hKey, "String", NULL, &dwType, NULL, &cb) == 0);
        CHECK(dwType == REG_SZ);
        CHECK(reg.QueryString("String", &str) && str == "hello");
        CHECK(reg.QueryString("String", sz) && strcmp(sz, "hello") == 0);

        const BYTE abData[5] = { 1, 2, 0, 4, 5 };
        BYTE abRead[5] = { 0 };
        CHECK(reg.SetBinary("Binary", abData, 5));
        CHECK(reg.QueryBinary("Binary", abRead, 5) && memcmp(abRead, abData, 5) == 0);
        CHECK(!reg.QueryBinary("Binary", abRead, 4));       // too small

        // 254 characters fit the buffer of 255, 255 do not
        std::string strLong(254, 'q');
        CHECK(reg.SetString("Long", strLong.c_str()));
        str.clear();
        CHECK(reg.QueryString("Long", &str) && str == strLong);
        CHECK(reg.QueryString("Long", sz) && sz == strLong);
        strLong.append(10, 'q');
        CHECK(reg.SetString("Longer", strLong.c_str()));
        CHECK(!reg.QueryString("Longer", sz));               // ERROR_MORE_DATA
    }   // the destructor closes the key

    // the WinINet limits, on the test key
    CHECK(!NetEngine_SetWinInetMaxConnections("Software\\JMXFramework2011Test\\Missing"));
    {
        CRegistry reg;
        CHECK(reg.Open(s_szTestRegKey) && reg.SetDword("MaxConnectionsPerServer", 100));
        CHECK(reg.SetDword("MaxConnectionsPer1_OServer", 4));
    }
    CHECK(NetEngine_SetWinInetMaxConnections(s_szTestRegKey));
    {
        CRegistry reg;
        DWORD dw1 = 0;
        DWORD dw2 = 0;
        DWORD dw3 = 0;
        CHECK(reg.Open(s_szTestRegKey));
        CHECK(reg.QueryDword("MaxConnectionsPerServer", &dw1) && dw1 == 100);
        CHECK(reg.QueryDword("MaxConnectionsPer1_OServer", &dw2) && dw2 == 100);
        CHECK(!reg.QueryDword("MaxConnectionsPer1_0Server", &dw3));     // sic: letter O
        reg.Close();
    }

    CHECK(RegDeleteKeyA(HKEY_CURRENT_USER, s_szTestRegKey) == ERROR_SUCCESS);
}

// Engine whose slot 41 does not touch the registry.
static int s_nSlot41Calls = 0;

class CSlot41Probe : public CNetEngine
{
public:
    virtual bool __stdcall SetWinInetMaxConnections(DWORD dwUnused)
    {
        ++s_nSlot41Calls;
        return dwUnused == 7;
    }
};

typedef bool (__stdcall* PFN_SLOT41)(IBSNet* pThis, DWORD dwUnused);

static void TestEngineVtable()
{
    CSlot41Probe* pProbe = new CSlot41Probe;
    IBSNet* pNet = pProbe;
    void** pVtbl = *(void***)pNet;
    CHECK(((PFN_SLOT41)pVtbl[41])(pNet, 7) && s_nSlot41Calls == 1);
    CHECK(pNet->SetWinInetMaxConnections(7) && s_nSlot41Calls == 2);
    delete pProbe;
    g_pNetEngine = NULL;
    g_pNetEngineDatagram = NULL;
}

// Messages from the pool with opcode wOpcode and the payload of a log line (DWORD flags, string).
static CMsg* NewLogMsg(CMsgPool* pPool, WORD wOpcode, const std::string& strText)
{
    CMsg* pMsg = pPool->Alloc();
    pMsg->SetOpcode(wOpcode);
    *pMsg << (DWORD)0x2000001;
    *pMsg << strText;
    return pMsg;
}

static void TestMsgPool2011(CNetEngine* pEngine)
{
    // texts of the double allocation and double free
    CMsgPool pool;
    CHECK(pool.m_strName == "NetEngine::MsgPool");
    pool.m_nItemsPerBlock = 1;
    CMsg* p = pool.Alloc();
    p->SetOpcode(0x1234);
    pool.m_FreeQue.Push(p);
    s_vecLibLog.clear();
    CMsg* q = pool.Alloc();
    CHECK(q && q != p && pool.m_nCapacity == 2);
    CHECK(LibLogFlags("someone try to allocate msg which is already in use !!! [0x1234]")
          == 0x2000001);
    pool.Free(q);
    s_vecLibLog.clear();
    pool.Free(q);
    CHECK(LibLogFlags("someone try to free msg already freed !!! [0x0]") == 0x2000001);
    CHECK(pool.m_nInUse == 1);

    // a failing block allocation writes the messages in use to MsgAllocatorException.log
    CMsg* p2 = pool.Alloc();
    p2->SetOpcode(0x2209);
    *p2 << (DWORD)0;
    *p2 << (WORD)0x3005;
    CHECK(pool.m_setInUse.size() == 2);
    pool.m_nItemsPerBlock = 0x7C000;                // the block of messages cannot be allocated
    DeleteFileA("MsgAllocatorException.log");
    CHECK(pool.Alloc() == NULL);
    CHECK(ReadWholeFile(s_strLibDir + "MsgAllocatorException.log")
          == "[NetEngine::MsgPool] \tMsgID : 0x1234 \t 1\r\r\n"
             "[NetEngine::MsgPool] \tMsgID : 0x3005 \t 1\r\r\n"
             "Total Count 2\r\r\n\r\r\n");
    CHECK(DeleteFileA("MsgAllocatorException.log"));
    pool.m_nItemsPerBlock = 1;
    pool.Free(p);
    pool.Free(p2);

    // CNetEngine::AllocMsg returns NULL instead of crashing
    pEngine->m_MsgPool.m_nItemsPerBlock = 0x7C000;
    CHECK(pEngine->AllocMsg(FALSE) == NULL);
    CHECK(DeleteFileA("MsgAllocatorException.log"));
    pEngine->m_MsgPool.m_nItemsPerBlock = 500;
    CMsg* pMsg = pEngine->AllocMsg(TRUE);
    CHECK(pMsg && pMsg->m_lRefCount == 1 && (*pMsg->m_pSize & MSG_ENCRYPTED));
    pEngine->ReleaseMsg(pMsg);

    // DumpMsgPool forwards the threshold: 0x200A texts once an opcode is above half of it
    CMsgPool& mp = pEngine->m_MsgPool;
    mp.m_dw4 = (DWORD)(DWORD_PTR)LibDump;
    std::string strText(600, 'x');
    std::vector<CMsg*> vec;
    for (int i = 0; i < 6005; ++i)
        vec.push_back(NewLogMsg(&mp, 0x200A, strText));
    vec.push_back(NewLogMsg(&mp, 0x7002, "y"));

    s_vecLibDump.clear();
    pEngine->DumpMsgPool(1, 10000);                 // threshold too small: no file
    CHECK(FindFiles(s_strLibDir, "*_LogContent.txt").empty());
    CHECK(s_vecLibDump.size() == 3
          && s_vecLibDump[0] == std::make_pair((DWORD)0x200A, (DWORD)6005));
    CHECK(s_vecLibDump[1] == std::make_pair((DWORD)0x7002, (DWORD)1));
    CHECK(s_vecLibDump[2] == std::make_pair((DWORD)0xFFFF, (DWORD)6006));

    s_vecLibDump.clear();
    pEngine->DumpMsgPool(0, 10002);                 // flag 1 not set
    CHECK(s_vecLibDump.empty() && FindFiles(s_strLibDir, "*_LogContent.txt").empty());

    SYSTEMTIME st;
    GetLocalTime(&st);
    pEngine->DumpMsgPool(1, 10002);
    CHECK(s_vecLibDump.size() == 3);
    std::vector<std::string> vecFiles = FindFiles(s_strLibDir, "*_LogContent.txt");
    CHECK(vecFiles.size() == 1);
    if (vecFiles.size() == 1)
    {
        // MM-DD-hh-mm (the minute may have passed since st)
        CHECK(vecFiles[0].size() == 26 && vecFiles[0].substr(11) == "_LogContent.txt");
        char szPrefix[16];
        sprintf_s(szPrefix, "%02d-%02d-", st.wMonth, st.wDay);
        CHECK(vecFiles[0].compare(0, 6, szPrefix) == 0);

        std::string str = ReadWholeFile(s_strLibDir + vecFiles[0]);
        // counts 5002..6005 are above 5001; at most 1000 lines of up to 500 characters
        size_t nLines = 0;
        bool bFormat = true;
        size_t nPos = 0;
        while (nPos < str.size())
        {
            size_t nEnd = str.find("\r\n", nPos);
            if (nEnd == std::string::npos)
            {
                bFormat = false;
                break;
            }
            std::string strLine = str.substr(nPos, nEnd - nPos);
            // YYYY-MM-DD\thh:mm:ss\t<500 x>
            bFormat = bFormat && strLine.size() == 20 + 500 && strLine[4] == '-'
                      && strLine[10] == '\t' && strLine[13] == ':' && strLine[19] == '\t'
                      && strLine.substr(20) == std::string(500, 'x');
            ++nLines;
            nPos = nEnd + 2;
        }
        CHECK(bFormat && nLines == 1000);
        CHECK(DeleteFileA((s_strLibDir + vecFiles[0]).c_str()));
    }

    for (size_t i = 0; i < vec.size(); ++i)
        mp.Free(vec[i]);
    mp.m_dw4 = 0;
}

static void TestNetEngine2011()
{
    CNetEngine* pEngine = new CNetEngine;
    CHECK(g_pNetEngine == pEngine);
    CHECK(pEngine->m_ConnectPool.m_strName == "NetEngine::ActiveSocketPool");
    CHECK(pEngine->m_AcceptPool.m_strName == "NetEngine::PassiveSocketPool");
    CHECK(pEngine->m_DatagramPool.m_strName == "Unknown");
    CHECK(pEngine->m_SessionTable.m_SessionPool.m_strName == "NetEngine::SessionPool");
    CHECK(pEngine->m_MsgPool.m_strName == "NetEngine::MsgPool");
    pEngine->m_Config.pfnLog = LibLog;

    // NetEngineLog: no engine, faulting receiver
    g_pNetEngine = NULL;
    NetEngineLog(0, "no engine %d", 1);
    g_pNetEngine = pEngine;
    pEngine->m_Config.pfnLog = FaultingLog;
    NetEngineLog(0, "fault");                       // the fault is caught
    pEngine->m_Config.pfnLog = LibLog;
    s_vecLibLog.clear();
    NetEngineLog(5, "%s-%d", "text", 3);
    CHECK(LibLogFlags("text-3") == 5);

    TestMsgPool2011(pEngine);

    // NERR_PEER_DOESNT_RESPONSE is logged as 0x2000001
    {
        SOCKET hDummy = socket(AF_INET, SOCK_STREAM, 0);
        CSockStream* p = new CSockStream;
        p->m_nRole = SOCK_ROLE_CONNECTED;
        p->m_hSocket = hDummy;
        p->SetupHandlersAndQueue();
        CHECK(p->m_pfnSend == &CSockStream::SendQueuedWithLimit);
        p->m_ovSend.nUsed = 5;
        pEngine->m_Config.bSecurityMode = 0;
        pEngine->m_Config.aSocket[1].nSendQueueLimit = 1;
        DWORD dwInUse = pEngine->m_MsgPool.m_nInUse;
        CMsg* apMsg[3];
        for (int i = 0; i < 3; ++i)
        {
            apMsg[i] = pEngine->AllocMsg(FALSE);
            apMsg[i]->SetOpcode(0x4000);
            *apMsg[i] << (BYTE)1;
        }
        s_vecLibLog.clear();
        CHECK(p->SendQueuedWithLimit(apMsg[0]) == 0);
        CHECK(p->SendQueuedWithLimit(apMsg[1]) == 0);
        CHECK(p->SendQueuedWithLimit(apMsg[2]) == SOCK_ERR_PEER_NOT_RESPONDING);
        CHECK(LibLogFlags("NERR_PEER_DOESNT_RESPONSE - 1:1") == 0x2000001);
        pEngine->m_Config.aSocket[1].nSendQueueLimit = -1;
        pEngine->m_Config.bSecurityMode = 1;
        for (int i = 0; i < 3; ++i)
            pEngine->ReleaseMsg(apMsg[i]);
        p->m_pSession = NULL;
        delete p;                                       // returns the queued messages
        pEngine->ReleaseMsg(apMsg[2]);                  // SOCK-003: the extra reference
        CHECK(pEngine->m_MsgPool.m_nInUse == dwInUse);
    }

    // Initialize names the message pool of a server after the parameter block
    BSNET_INIT_PARAM param;
    param.dwFlags = BSNET_FLAG_SERVER;
    param.pfnLog = LibLog;
    param.Maintainer.dwFlags = 0;                   // no maintainer
    strcpy_s(param.szMsgPoolName, "AgentServer");
    CHECK(pEngine->Initialize(param) == TRUE);
    CHECK(pEngine->m_MsgPool.m_strName == "AgentServer");
    CHECK(strcmp(pEngine->m_Config.szMsgPoolName, "AgentServer") == 0);
    CHECK(pEngine->m_MsgPool.m_nItemsPerBlock == 3000);
    CHECK(pEngine->UnregisterProactor(NETENGINE_TASK_LISTEN));
    CHECK(pEngine->UnregisterProactor(NETENGINE_TASK_IO));

    param.szMsgPoolName[0] = 0;
    CHECK(pEngine->Initialize(param) == TRUE);
    CHECK(pEngine->m_MsgPool.m_strName == "Unknown");
    CHECK(pEngine->UnregisterProactor(NETENGINE_TASK_LISTEN));
    CHECK(pEngine->UnregisterProactor(NETENGINE_TASK_IO));

    // a client keeps the name
    pEngine->m_MsgPool.SetName("Kept");
    param.dwFlags = 0;
    strcpy_s(param.szMsgPoolName, "Ignored");
    CHECK(pEngine->Initialize(param) == TRUE);
    CHECK(pEngine->m_MsgPool.m_strName == "Kept" && pEngine->m_MsgPool.m_nItemsPerBlock == 500);
    CHECK(pEngine->UnregisterProactor(NETENGINE_TASK_IO));

    delete pEngine;
    g_pNetEngine = NULL;
    g_pNetEngineDatagram = NULL;
}

// GetSystemInfo replacement reporting no processor (reaches the CLAMP() log).
static VOID WINAPI NoProcessorSystemInfo(LPSYSTEM_INFO pInfo)
{
    memset(pInfo, 0, sizeof(*pInfo));
}

static int PatchImport(void* pOld, void* pNew)
{
    BYTE* pBase = (BYTE*)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS* pNt = (IMAGE_NT_HEADERS*)(pBase + ((IMAGE_DOS_HEADER*)pBase)->e_lfanew);
    IMAGE_DATA_DIRECTORY& dir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    void** ppEntry = (void**)(pBase + dir.VirtualAddress);
    int nPatched = 0;
    for (DWORD i = 0; i < dir.Size / sizeof(void*); ++i)
    {
        if (ppEntry[i] != pOld)
            continue;
        DWORD dwOld;
        VirtualProtect(&ppEntry[i], sizeof(void*), PAGE_READWRITE, &dwOld);
        ppEntry[i] = pNew;
        VirtualProtect(&ppEntry[i], sizeof(void*), dwOld, &dwOld);
        ++nPatched;
    }
    return nPatched;
}

static void TestThreadManagerClamp()
{
    CNetEngine* pEngine = new CNetEngine;           // receiver of NetEngineLog
    pEngine->m_Config.pfnLog = LibLog;
    void* pfnReal = (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetSystemInfo");
    CHECK(PatchImport(pfnReal, (void*)NoProcessorSystemInfo) == 1);

    CThreadManager mgr;
    THREAD_AFFINITY aff;
    aff.bRandom = FALSE;
    aff.nMinCPU = 3;
    aff.nMaxCPU = 4;
    s_vecLibLog.clear();
    CHECK(mgr.CreateThreads(&aff, 0, NULL, NULL, 0) == TRUE);
    CHECK(PatchImport((void*)NoProcessorSystemInfo, pfnReal) == 1);
    CHECK(s_vecLibLog.size() == 2);
    CHECK(LibLogFlags("CLAMP() ==> min(0.000) exceeded max(-1.000) value), File: "
                      "D:\\WORK2005\\Source\\JMX_Library\\BSLib\\thread\\threadmanager.cpp, "
                      "Line: 94") == 0x2000001);
    CHECK(LibLogFlags("CLAMP() ==> min(0.000) exceeded max(-1.000) value), File: "
                      "D:\\WORK2005\\Source\\JMX_Library\\BSLib\\thread\\threadmanager.cpp, "
                      "Line: 95") == 0x2000001);

    delete pEngine;
    g_pNetEngine = NULL;
    g_pNetEngineDatagram = NULL;
}

static void TestFileAppendString()
{
    std::string strFile = s_strLibDir + "append.txt";
    File_AppendString(strFile.c_str(), "ab");
    File_AppendString(strFile.c_str(), "cd\n");
    File_AppendString((s_strLibDir + "nodir\\x.txt").c_str(), "lost");      // nothing
    CHECK(ReadWholeFile(strFile) == "abcd\r\n");
    CHECK(DeleteFileA(strFile.c_str()));
}

// Minidumps next to the exe are numbered 1, 2, ... in 2011.
static void TestMiniDumpCounter()
{
    char szModule[MAX_PATH];
    GetModuleFileNameA(NULL, szModule, MAX_PATH);
    std::string strExe = szModule;
    size_t nSlash = strExe.rfind('\\');
    std::string strDir = strExe.substr(0, nSlash + 1);
    std::string strName = strExe.substr(nSlash + 1);
    strName = strName.substr(0, strName.rfind('.'));
    std::string strPattern = strName + "_[*]_*.dmp";

    std::vector<std::string> vecBefore = FindFiles(strDir, strPattern.c_str());
    CHECK(BSDebugWriteMiniDump(NULL) == EXCEPTION_EXECUTE_HANDLER);
    CHECK(BSDebugWriteMiniDump(NULL) == EXCEPTION_EXECUTE_HANDLER);
    std::vector<std::string> vecAfter = FindFiles(strDir, strPattern.c_str());
    int nOne = 0;
    int nTwo = 0;
    for (size_t i = 0; i < vecAfter.size(); ++i)
    {
        bool bOld = false;
        for (size_t j = 0; j < vecBefore.size(); ++j)
            bOld = bOld || vecBefore[j] == vecAfter[i];
        if (bOld)
            continue;
        const std::string& s = vecAfter[i];
        if (s.size() > 7 && s.compare(s.size() - 7, 7, "]_1.dmp") == 0)
            ++nOne;
        if (s.size() > 7 && s.compare(s.size() - 7, 7, "]_2.dmp") == 0)
            ++nTwo;
        DeleteFileA((strDir + s).c_str());
    }
    CHECK(nOne == 1 && nTwo == 1);
}

// Answers the assert dialog with OK and keeps its text.
static char s_szBoxText[4096];

static DWORD WINAPI DismissAssertBox(LPVOID)
{
    for (int i = 0; i < 500; ++i)
    {
        HWND hWnd = FindWindowA("#32770", "BSLib Debug Module");
        if (hWnd)
        {
            Sleep(100);
            GetDlgItemTextA(hWnd, 0xFFFF, s_szBoxText, sizeof(s_szBoxText));
            PostMessageA(hWnd, WM_COMMAND, IDOK, 0);
            return 1;
        }
        Sleep(20);
    }
    return 0;
}

static int s_nAssertLine = 0;
static std::string s_strAssertFile;
static std::string s_strAssertExpr;

static void AssertCallback(int nLine, const char* pszFile, const char* pszExpr)
{
    s_nAssertLine = nLine;
    s_strAssertFile = pszFile;
    s_strAssertExpr = pszExpr;
}

// A deeper stack for the call stack text (more than the 2008 limit of 520 characters).
static volatile int s_nDepth = 0;

static __declspec(noinline) bool AssertWAtDepth(int n)
{
    if (n > 0)
    {
        bool b = AssertWAtDepth(n - 1);
        ++s_nDepth;
        return b;
    }
    return BSAssertDialogW(7, L"wfile.cpp", L"expr %d", 42);
}

static void TestAssertDialogW()
{
    DWORD adwSaved[2] = { g_dwBSDebugFlags[0], g_dwBSDebugFlags[1] };
    BSDebugSetAssertCallback(AssertCallback);

    // no file name: false before anything is shown
    BSDebugSetFlags(BSDBG_CALLBACK, BSDBG_CALLBACK);
    g_bBSDebugAsserted = FALSE;
    s_nAssertLine = 0;
    CHECK(!BSAssertDialogW(1, NULL, L"x"));
    CHECK(g_bBSDebugAsserted && s_nAssertLine == 0);
    // the text does not fit 0x400 characters
    std::wstring strLong(0x400, L'w');
    CHECK(!BSAssertDialogW(1, L"f", strLong.c_str()));
    CHECK(s_nAssertLine == 0);

    // call stack, no minidump (they exclude each other)
    char szModule[MAX_PATH];
    GetModuleFileNameA(NULL, szModule, MAX_PATH);
    std::string strDir = szModule;
    strDir = strDir.substr(0, strDir.rfind('\\') + 1);
    size_t nDumps = FindFiles(strDir, "*.dmp").size();

    DWORD dwFlags = BSDBG_CALLSTACK | BSDBG_MINIDUMP | BSDBG_CALLBACK | BSDBG_DIALOG_CONTINUE;
    BSDebugSetFlags(dwFlags, dwFlags);
    s_szBoxText[0] = 0;
    HANDLE hThread = CreateThread(NULL, 0, DismissAssertBox, NULL, 0, NULL);
    CHECK(AssertWAtDepth(20));
    WaitForSingleObject(hThread, 15000);
    CloseHandle(hThread);
    CHECK(s_nAssertLine == 7 && s_strAssertFile == "wfile.cpp" && s_strAssertExpr == "expr 42");
    CHECK(FindFiles(strDir, "*.dmp").size() == nDumps);
    std::string strBox = s_szBoxText;
    const char szHead[] = "Debug Assertion Failed.\n\nExpression : expr 42\nLine : 7\n"
                          "File : wfile.cpp\n\nCallStack:\n";
    const char szTail[] = "\n\n(press \"ok\" to advance, press \"cancel\" to exit)";
    CHECK(strBox.compare(0, strlen(szHead), szHead) == 0);
    CHECK(strBox.size() > strlen(szTail)
          && strBox.compare(strBox.size() - strlen(szTail), strlen(szTail), szTail) == 0);

    BSDebugSetAssertCallback(NULL);
    BSDebugSetFlags(adwSaved[0], adwSaved[1]);
    g_bBSDebugAsserted = FALSE;
}

static void TestLibrary2011()
{
    char szTemp[MAX_PATH];
    GetTempPathA(MAX_PATH, szTemp);
    char szDir[MAX_PATH];
    sprintf_s(szDir, "%sFramework2011Test_%lu\\", szTemp, GetCurrentProcessId());
    CreateDirectoryA(szDir, NULL);
    s_strLibDir = szDir;
    char szOldDir[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, szOldDir);
    SetCurrentDirectoryA(szDir);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    TestLibraryLayout2011();
    TestChunkAllocatorName();
    TestMsgException();
    TestStringUtil();
    TestRegistry();
    TestEngineVtable();
    TestNetEngine2011();
    TestThreadManagerClamp();
    TestFileAppendString();
    TestMiniDumpCounter();
    TestAssertDialogW();

    SetCurrentDirectoryA(szOldDir);
    CHECK(RemoveDirectoryA(szDir));
}

// =============================================================================================
// ServerFramework
//
// The application object runs on the real engine (InitNetEngine, a listener on 127.0.0.1 and a
// session connected to it); the log lines are read back from the completion port. Message
// boxes, the popup menu, the language ID and the font creation are redirected through the
// import table of the test exe.

#include <commctrl.h>
#include <map>

#include "BSLib/BSDebug.h"
#include "BSNet/NetEngine.h"
#include "BSNet/SockListener.h"
#include "ServerFramework/CrashDump.h"
#include "ServerFramework/LogQueue.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/MonitorGraph.h"
#include "ServerFramework/OverlapJob.h"
#include "ServerFramework/ServerApp.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerFrameWindow.h"
#include "ServerFramework/ServerMoniterView.h"
#include "ServerFramework/ServerProcess.h"
#include "ServerFramework/ServerProcessMain.h"
#include "ServerFramework/ServerUiExtern.h"
#include "ServerFramework/WindowBase.h"

#include "LogCapture.h"

using namespace ServerFramework;

static std::string s_strFwDir;

// ---------------------------------------------------------------------------------------------
// Import table redirection

struct FW11_BOX
{
    std::string strText;
    std::string strCaption;
    UINT        uType;
};

struct FW11_MENU_ITEM
{
    UINT        uFlags;
    UINT_PTR    uID;
    std::string strText;
};

static std::vector<FW11_BOX>        s_vecFwBoxes;
static std::vector<FW11_MENU_ITEM>  s_vecFwMenu;
static UINT                         s_uFwTrackFlags;
static BOOL                         s_nFwTrackResult;
static LANGID                       s_wFwLang;          // 0: the real one
static bool                         s_bFwFailCreateFont;

typedef BOOL (WINAPI* PFN_APPENDMENUA)(HMENU, UINT, UINT_PTR, LPCSTR);
typedef LANGID (WINAPI* PFN_GETUSERDEFAULTLANGID)();
typedef HFONT (WINAPI* PFN_CREATEFONTA)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD,
                                        DWORD, DWORD, DWORD, DWORD, LPCSTR);
static PFN_APPENDMENUA          s_pfnAppendMenuA;
static PFN_GETUSERDEFAULTLANGID s_pfnGetUserDefaultLangID;
static PFN_CREATEFONTA          s_pfnCreateFontA;

static int WINAPI Fw11MessageBoxA(HWND, LPCSTR pszText, LPCSTR pszCaption, UINT uType)
{
    FW11_BOX box;
    box.strText = pszText ? pszText : "";
    box.strCaption = pszCaption ? pszCaption : "";
    box.uType = uType;
    s_vecFwBoxes.push_back(box);
    return IDOK;
}

static BOOL WINAPI Fw11TrackPopupMenu(HMENU, UINT uFlags, int, int, int, HWND, const RECT*)
{
    s_uFwTrackFlags = uFlags;
    return s_nFwTrackResult;
}

static BOOL WINAPI Fw11AppendMenuA(HMENU hMenu, UINT uFlags, UINT_PTR uID, LPCSTR pszText)
{
    FW11_MENU_ITEM item;
    item.uFlags = uFlags;
    item.uID = uID;
    item.strText = pszText ? pszText : "";
    s_vecFwMenu.push_back(item);
    return s_pfnAppendMenuA(hMenu, uFlags, uID, pszText);
}

static LANGID WINAPI Fw11GetUserDefaultLangID()
{
    return s_wFwLang ? s_wFwLang : s_pfnGetUserDefaultLangID();
}

static HFONT WINAPI Fw11CreateFontA(int n1, int n2, int n3, int n4, int n5, DWORD d6, DWORD d7,
                                    DWORD d8, DWORD d9, DWORD d10, DWORD d11, DWORD d12,
                                    DWORD d13, LPCSTR pszFace)
{
    if (s_bFwFailCreateFont)
        return NULL;
    return s_pfnCreateFontA(n1, n2, n3, n4, n5, d6, d7, d8, d9, d10, d11, d12, d13, pszFace);
}

static int Fw11HookImport(const char* pszDll, const char* pszName, void* pfnNew)
{
    void* pfnOld = (void*)GetProcAddress(LoadLibraryA(pszDll), pszName);
    BYTE* pBase = (BYTE*)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS* pNt = (IMAGE_NT_HEADERS*)(pBase + ((IMAGE_DOS_HEADER*)pBase)->e_lfanew);
    IMAGE_DATA_DIRECTORY& dir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    void** ppEntry = (void**)(pBase + dir.VirtualAddress);
    int nPatched = 0;
    for (DWORD i = 0; i < dir.Size / sizeof(void*); ++i)
    {
        if (ppEntry[i] != pfnOld)
            continue;
        DWORD dwOld;
        VirtualProtect(&ppEntry[i], sizeof(void*), PAGE_READWRITE, &dwOld);
        ppEntry[i] = pfnNew;
        VirtualProtect(&ppEntry[i], sizeof(void*), dwOld, &dwOld);
        ++nPatched;
    }
    return nPatched;
}

static void Fw11HookImports()
{
    s_pfnAppendMenuA = (PFN_APPENDMENUA)GetProcAddress(LoadLibraryA("user32.dll"),
                                                       "AppendMenuA");
    s_pfnGetUserDefaultLangID = (PFN_GETUSERDEFAULTLANGID)GetProcAddress(
        LoadLibraryA("kernel32.dll"), "GetUserDefaultLangID");
    s_pfnCreateFontA = (PFN_CREATEFONTA)GetProcAddress(LoadLibraryA("gdi32.dll"), "CreateFontA");
    CHECK(Fw11HookImport("user32.dll", "MessageBoxA", (void*)Fw11MessageBoxA) == 1);
    CHECK(Fw11HookImport("user32.dll", "TrackPopupMenu", (void*)Fw11TrackPopupMenu) == 1);
    CHECK(Fw11HookImport("user32.dll", "AppendMenuA", (void*)Fw11AppendMenuA) == 1);
    CHECK(Fw11HookImport("kernel32.dll", "GetUserDefaultLangID",
                         (void*)Fw11GetUserDefaultLangID) == 1);
    CHECK(Fw11HookImport("gdi32.dll", "CreateFontA", (void*)Fw11CreateFontA) == 1);
}

static bool Fw11Boxes(const char* psz1, const char* psz2 = NULL)
{
    size_t n = psz2 ? 2 : psz1 ? 1 : 0;
    bool bOk = s_vecFwBoxes.size() == n
        && (!psz1 || s_vecFwBoxes[0].strText == psz1)
        && (!psz2 || s_vecFwBoxes[1].strText == psz2);
    s_vecFwBoxes.clear();
    return bOk;
}

// ---------------------------------------------------------------------------------------------
// Files

static std::string Fw11ReadFile(const std::string& strPath)
{
    std::string str;
    FILE* pFile = NULL;
    if (fopen_s(&pFile, strPath.c_str(), "rb") || !pFile)
        return str;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), pFile)) > 0)
        str.append(buf, n);
    fclose(pFile);
    return str;
}

static void Fw11WriteFile(const std::string& strPath, const char* pszText)
{
    FILE* pFile = NULL;
    fopen_s(&pFile, strPath.c_str(), "wb");
    CHECK(pFile != NULL);
    if (!pFile)
        return;
    fwrite(pszText, 1, strlen(pszText), pFile);
    fclose(pFile);
}

// "YYYY-MM-DD<tab>hh:mm:ss<tab>" at the start of the text.
static bool Fw11IsTimestamp(const std::string& str)
{
    if (str.size() < 20)
        return false;
    for (size_t i = 0; i < 20; ++i)
    {
        char c = str[i];
        bool bOk;
        if (i == 4 || i == 7)
            bOk = c == '-';
        else if (i == 10 || i == 19)
            bOk = c == '\t';
        else if (i == 13 || i == 16)
            bOk = c == ':';
        else
            bOk = c >= '0' && c <= '9';
        if (!bOk)
            return false;
    }
    return true;
}

static int Fw11DeleteFiles(const std::string& strPattern)
{
    std::string strDir = strPattern.substr(0, strPattern.rfind('\\') + 1);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(strPattern.c_str(), &fd);
    int n = 0;
    if (hFind == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        DeleteFileA((strDir + fd.cFileName).c_str());
        ++n;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    return n;
}

// ---------------------------------------------------------------------------------------------
// The application object

class CFw11ProbeApp : public CServerApp
{
public:
    CFw11ProbeApp() : m_nFrameCommand(0), m_nCustomDump(0) {}
    virtual void CustomDump() { ++m_nCustomDump; }
    virtual void OnFrameCommand() { ++m_nFrameCommand; }
    virtual void OnSuspectedUserCommand(DWORD dwReserved, DWORD dwCommand)
    {
        m_vecSuspected.push_back(dwReserved << 16 | dwCommand);
    }

    int                 m_nFrameCommand;
    int                 m_nCustomDump;
    std::vector<DWORD>  m_vecSuspected;
};

class CFw11ProbeFrame : public CServerFrameWindow
{
public:
    virtual LRESULT OnFrameCommand(WPARAM, LPARAM) { return 7; }
};

static CFw11ProbeApp* s_pFwApp;

static std::vector<DWORD> Fw11VtableDiff(void* p1, void* p2, int nSlots)
{
    void** pv1 = *(void***)p1;
    void** pv2 = *(void***)p2;
    std::vector<DWORD> v;
    for (int i = 0; i < nSlots; ++i)
    {
        if (pv1[i] != pv2[i])
            v.push_back(i);
    }
    return v;
}

static void TestFw11Layout()
{
    CHECK(sizeof(CServerApp) == 0x620);
    CHECK(FIELD_OFFSET(CServerApp, m_EngineParam) == 0x28);
    CHECK(FIELD_OFFSET(CServerApp, m_nPendingState) == 0x13C);
    CHECK(FIELD_OFFSET(CServerApp, m_NetEngine) == 0x140);
    CHECK(sizeof(CServerFrameWindow) == 0xC8);
    CHECK(sizeof(CServerProcessMain) == 0x400C8);

    // CServerApp: 20 slots; the probe overrides the dtor, CustomDump (15) and the new 16 / 17
    CServerApp* pBase = new CServerApp;
    s_pFwApp = new CFw11ProbeApp;
    std::vector<DWORD> v = Fw11VtableDiff(pBase, s_pFwApp, 20);
    CHECK(v.size() == 4 && v[0] == 0 && v[1] == 15 && v[2] == 16 && v[3] == 17);
    delete pBase;
    // OnControlMsg and SetServerState are slots 18 and 19
    void** pvApp = *(void***)s_pFwApp;
    CServerApp* pApp = s_pFwApp;
    g_pServerApp = s_pFwApp;
    ServerApp_OnFrameCommand();
    ServerApp_OnSuspectedUserCommand(2);
    pApp->OnFrameCommand();
    CHECK(s_pFwApp->m_nFrameCommand == 2);
    CHECK(s_pFwApp->m_vecSuspected.size() == 1 && s_pFwApp->m_vecSuspected[0] == 2);
    s_pFwApp->m_vecSuspected.clear();
    s_pFwApp->m_nFrameCommand = 0;
    CHECK(pvApp[18] != pvApp[19] && pvApp[16] != pvApp[18]);

    // CServerFrameWindow: 21 slots, the new OnFrameCommand is 17
    CServerFrameWindow* pFrame = new CServerFrameWindow;
    CFw11ProbeFrame* pProbe = new CFw11ProbeFrame;
    v = Fw11VtableDiff(pFrame, pProbe, 21);
    CHECK(v.size() == 2 && v[0] == 0 && v[1] == 17);
    CHECK(pFrame->OnFrameCommand(0, 0) == 0);
    CHECK(s_pFwApp->m_nFrameCommand == 1);
    s_pFwApp->m_nFrameCommand = 0;
    delete pProbe;
    delete pFrame;

    // the framework's pools are named
    CHECK(g_MassiveMsgPool.m_strName == "CMassiveMsg");
    CHECK(g_OverlapJobRelayMsgPool.m_strName == "COverlapJob_RelayMsg");
    CHECK(g_OverlapJobInitLocalDataPool.m_strName == "COverlapJob_InitializeLocalData");
}

static CMsg* Fw11Msg(WORD wOpcode)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(wOpcode);
    return pMsg;
}

static std::vector<CAPTURED_LOG> s_vecFwLogs;

static void Fw11Drain()
{
    s_vecFwLogs = LogCapture_Drain();
}

static bool Fw11Logged(DWORD dwFlags, const std::string& strText)
{
    for (size_t i = 0; i < s_vecFwLogs.size(); ++i)
    {
        if (s_vecFwLogs[i].dwFlags == dwFlags && s_vecFwLogs[i].strText == strText)
            return true;
    }
    return false;
}

static void TestFw11Engine()
{
    // InitNetEngine names the message pool after the module (BSNET_INIT_PARAM +0x94)
    CHECK(s_pFwApp->m_EngineParam.szMsgPoolName[0] == 0);
    CHECK(s_pFwApp->InitNetEngine());
    CHECK(g_pBSNet == &s_pFwApp->m_NetEngine);
    CHECK(strcmp(s_pFwApp->m_EngineParam.szMsgPoolName, "AgentServer") == 0);
    CHECK(s_pFwApp->m_NetEngine.m_MsgPool.m_strName == "AgentServer");
    Fw11Drain();
}

static int s_nFwLogCalls;
static DWORD s_dwFwLogFlags;
static std::string s_strFwLogText;

static int __cdecl Fw11LogReceiver(DWORD dwFlags, const char* pszText)
{
    ++s_nFwLogCalls;
    s_dwFwLogFlags = dwFlags;
    s_strFwLogText = pszText;
    return 0;
}

static void TestFw11Logs()
{
    // LogFile_AppendTimestampedLine: "date\ttime\ttext\r\n"
    std::string strFile = s_strFwDir + "\\timestamped.log";
    CHECK(!LogFile_AppendTimestampedLine(NULL, "x"));
    CHECK(!LogFile_AppendTimestampedLine(strFile.c_str(), NULL));
    CHECK(LogFile_AppendTimestampedLine(strFile.c_str(), "value %d %s", 5, "five"));
    std::string str = Fw11ReadFile(strFile);
    // (the file is opened in text mode: the line ends in CR CR LF)
    CHECK(str.size() == 20 + 12 + 3 && Fw11IsTimestamp(str)
          && str.substr(20) == "value 5 five\r\r\n");
    std::string strLong(0x1000, 'x');
    CHECK(!LogFile_AppendTimestampedLine(strFile.c_str(), "%s", strLong.c_str()));
    strLong.resize(0xFFF);
    CHECK(LogFile_AppendTimestampedLine(strFile.c_str(), "%s", strLong.c_str()));
    str = Fw11ReadFile(strFile);
    CHECK(str.size() == 35 + 20 + 0xFFF + 3 && str.substr(35 + 20, 0xFFF) == strLong);
    DeleteFileA(strFile.c_str());

    // 0x200A: the fatal log line carries the module name
    s_pFwApp->m_pfnLog = Fw11LogReceiver;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char szFatal[MAX_PATH];
    sprintf_s(szFatal, "%s\\%04d-%02d-%02d_FatalLog.txt", s_strFwDir.c_str(), st.wYear,
              st.wMonth, st.wDay);
    DeleteFileA(szFatal);
    CMsg* pMsg = Fw11Msg(SERVER_CONTROL_LOG);
    DWORD dwFlags = 0x2000001;
    pMsg->WriteBytes(&dwFlags, 4);
    *pMsg << std::string("fatal text");
    s_pFwApp->OnControlMsg(pMsg);
    CHECK(s_nFwLogCalls == 1 && s_dwFwLogFlags == 0x2000001 && s_strFwLogText == "fatal text");
    str = Fw11ReadFile(szFatal);
    CHECK(Fw11IsTimestamp(str) && str.substr(20) == "[AgentServer]\tfatal text\r\n");
    DeleteFileA(szFatal);

    // 0x200C ends with "<module> is initialized successfully"
    SERVER_BODY self;
    memset(&self, 0, sizeof(self));
    self.wID = 1;
    self.byDivisionID = 5;
    g_pSelfBody = &self;
    Fw11Drain();
    s_pFwApp->OnControlMsg(Fw11Msg(SERVER_CONTROL_INIT_LOCAL_DATA));
    CHECK(g_dwSelfDivisionID == 5);
    Fw11Drain();
    CHECK(s_vecFwLogs.size() == 1
          && Fw11Logged(0x2000001, "AgentServer is initialized successfully"));
    g_pSelfBody = NULL;
    DeleteFileA(szFatal);
}

static void TestFw11MsgDescription()
{
    // a session of the engine: the description names the peer address
    CHECK(g_pBSNet->RegisterProactor(SERVER_TASK_MAIN, NULL,
                                     &CServerProcessOverlap::classCServerProcessOverlap, NULL));
    CHECK(g_pBSNet->Listen("127.0.0.1", 0));
    sockaddr_in local;
    s_pFwApp->m_NetEngine.m_pListener->GetLocalAddr(&local);
    DWORD dwSession = g_pBSNet->Connect("127.0.0.1", ntohs(local.sin_port), NULL,
                                        SERVER_TASK_MAIN, 0);
    CHECK(dwSession != 0);

    char szDesc[256];
    CMsg* pMsg = Fw11Msg(0x6101);
    DWORD dw = 0x11223344;
    pMsg->WriteBytes(&dw, 4);
    BYTE b;
    *pMsg >> b;
    CHECK(strcmp(Msg_FormatDebugString(szDesc, pMsg),
                 "IP: Req:0x0101 (0x0000) (MsgType:6), 4 - 3") == 0);
    pMsg->m_dwSessionID = dwSession;
    CHECK(Msg_FormatDebugString(szDesc, pMsg) == szDesc);
    CHECK(strcmp(szDesc, "IP:127.0.0.1 Req:0x0101 (0x0000) (MsgType:6), 4 - 3") == 0);

    // CheckMsgFullyConsumed and OnUnhandledMsg use the 2011 description
    Fw11Drain();
    CheckMsgFullyConsumed(pMsg);
    CHECK(pMsg->GetUnreadSize() == 0);
    Fw11Drain();
    CHECK(s_vecFwLogs.size() == 1 && Fw11Logged(0x1000000, "msg is not completely used : "
          "IP:127.0.0.1 Req:0x0101 (0x0000) (MsgType:6), 4 - 3"));
    g_pBSNet->ReleaseMsg(pMsg);

    pMsg = Fw11Msg(0x2209);
    pMsg->WriteBytes(&dw, 4);
    WORD wInner = 0x7777;
    pMsg->WriteBytes(&wInner, 2);
    CServerProcessOverlap* pProcess = new CServerProcessOverlap;
    pProcess->OnUnhandledMsg(pMsg, 0, 0, NULL);
    CHECK(pMsg->GetUnreadSize() == 0);
    Fw11Drain();
    CHECK(s_vecFwLogs.size() == 1
          && Fw11Logged(0x1000000, "unhandled msg received CServerProcessOverlap : "
                        "IP: NoDir:0x0209 (0x7777) (MsgType:2), 6 - 6"));
    pMsg->SetOpcode(0x8002);
    CHECK(strcmp(Msg_FormatDebugString(szDesc, pMsg),
                 "IP: Ack:0x0002 (0x0000) (MsgType:8), 6 - 0") == 0);
    g_pBSNet->ReleaseMsg(pMsg);

    // RegisterTimer: a new entry with the current tick; an existing ID is kept
    DWORD dwTick = GetTickCount();
    pProcess->RegisterTimer(7, 1234, &CServerProcessBase::OnOverlapTimer);
    pProcess->RegisterTimer(7, 99, NULL);
    ServerProcessTimerMap::iterator it = pProcess->m_mapTimer.find(7);
    CHECK(it != pProcess->m_mapTimer.end() && pProcess->m_mapTimer.size() == 1);
    if (it != pProcess->m_mapTimer.end())
    {
        CHECK(it->second.dwPeriod == 1234
              && it->second.pfnTimer == &CServerProcessBase::OnOverlapTimer);
        CHECK(it->second.dwLastTick - dwTick < 1000);
    }
    delete pProcess;
}

static void TestFw11MainProcess()
{
    CServerProcessMain* pMain = new CServerProcessMain;

    // OnSessionLost: every reason is logged with 0x2000001
    g_ServerGlobals.bReportConnectionLog = TRUE;
    static const char* const s_apszReason[5] = {
        "Session Lost [77]: NETENGINE_ERROR-", "Session Lost [77]: INTENTIONAL_KILL",
        "Session Lost [77]: STALE_SESSION", "Session Lost [77]: CLOSED_BY_PEER",
        "Session Lost [77]: ENGINE_SHUTDOWN" };
    for (DWORD i = 0; i < 5; ++i)
    {
        CMsg* pMsg = Fw11Msg(SESSION_EVENT_CLOSED);
        DWORD dwSession = 77;
        DWORD dwReason = i ? i : 0x50000;
        pMsg->WriteBytes(&dwSession, 4);
        pMsg->WriteBytes(&dwReason, 4);
        Fw11Drain();
        pMain->OnSessionLost(pMsg, 0, 0, NULL);
        g_pBSNet->ReleaseMsg(pMsg);
        Fw11Drain();
        std::string strText = s_apszReason[i];
        if (!i)
            strText += g_apszNetEngineError[5];
        CHECK(s_vecFwLogs.size() == 1 && Fw11Logged(0x2000001, strText));
    }
    g_ServerGlobals.bReportConnectionLog = FALSE;

    // OnMsg_2311: the shard name is cut to 31 characters
    static CERT_SHARD s_Shard;
    memset(&s_Shard, 0xCC, sizeof(s_Shard));
    s_Shard.wID = 9;
    strcpy_s((char*)s_Shard.ab3 + 1, 32, "old");
    g_mapCertShard[9] = &s_Shard;
    SERVER_BODY self;
    memset(&self, 0, sizeof(self));
    self.wID = 1;
    g_pSelfBody = &self;
    ServerBodyList lstBodies;
    lstBodies.push_back(&self);
    g_pServerBodyList = &lstBodies;
    std::string strName = "0123456789012345678901234567890123456789";
    CMsg* pMsg = Fw11Msg(0x2311);
    WORD wShard = 9;
    BYTE bType = 0;
    pMsg->WriteBytes(&wShard, 2);
    pMsg->WriteBytes(&bType, 1);
    *pMsg << strName;
    Fw11Drain();
    pMain->OnMsg_2311(pMsg, 0, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
    Fw11Drain();
    CHECK(Fw11Logged(0, "shard [9] name changed : old -> " + strName));
    const char* pszName = (const char*)s_Shard.ab3 + 1;
    CHECK(strName.compare(0, 31, pszName) == 0 && strlen(pszName) == 31);
    CHECK(s_Shard.ab3[1 + 32] == 0xCC && s_Shard.ab3[1 + 33] == 0xCC);
    pMsg = Fw11Msg(0x2311);
    pMsg->WriteBytes(&wShard, 2);
    pMsg->WriteBytes(&bType, 1);
    *pMsg << std::string("short");
    pMain->OnMsg_2311(pMsg, 0, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(strcmp(pszName, "short") == 0);
    g_mapCertShard.erase(9);
    g_pServerBodyList = NULL;
    g_pSelfBody = NULL;
    delete pMain;
    g_pServerBodyStateObserver = NULL;
    Fw11Drain();
}

static void TestFw11TaskRegistration()
{
    // SERVER_TASK_MAIN is taken (TestFw11MsgDescription): every failure is shown
    s_vecFwBoxes.clear();
    CHECK(!s_pFwApp->RegisterMainProcessTask());
    CHECK(Fw11Boxes("It has been failed to register task CServerProcessMain"));
    CHECK(!s_pFwApp->RegisterServerTasks());
    CHECK(Fw11Boxes("It has been failed to register task CServerProcessMain",
                    "It has been failed to initialize server task main"));
    // now the overlap task exists as well
    CHECK(g_pBSNet->GetProactor(SERVER_TASK_OVERLAP) != NULL);
    CHECK(!s_pFwApp->RegisterServerTasks());
    CHECK(s_vecFwBoxes.size() == 1 && s_vecFwBoxes[0].strCaption == "BSObj Plugin");
    CHECK(Fw11Boxes("It has been failed to register task"));
    Fw11Drain();
    CHECK(!Fw11Logged(0, "successfully server task registered"));
}

static void TestFw11Config()
{
    std::string strCfg = s_strFwDir + "\\Server.cfg";
    Fw11WriteFile(strCfg,
                  "common { certification \"1.2.3.4\", 15880 certification_ip_bind \"5.6.7.8\" }"
                  "\r\nAgentServer { MsgCountOfDumpTrigger 7 MsgDumpTriggerElapseTick 50 }\r\n");
    g_ServerGlobals.mapExtra.clear();
    CHECK(ServerConfig_Load());
    CHECK(strcmp(g_ServerGlobals.szCertificationIP, "1.2.3.4") == 0
          && g_ServerGlobals.nCertificationPort == 15880
          && strcmp(g_ServerGlobals.szCertificationBindIP, "5.6.7.8") == 0);
    CHECK(g_ServerGlobals.mapExtra.size() == 2);
    const char* psz = ServerConfig_FindExtraValue("MsgCountOfDumpTrigger");
    CHECK(psz && strcmp(psz, "7") == 0);
    CHECK(ServerConfig_FindExtraValue("NoSuchKey") == NULL);
    g_nMsgCountOfDumpTrigger = 0;
    g_dwMsgDumpTriggerElapseTick = 0;
    MonitorGraph_ReadMsgDumpConfig();
    CHECK(g_nMsgCountOfDumpTrigger == 7 && g_dwMsgDumpTriggerElapseTick == 50);
    g_ServerGlobals.mapExtra.clear();
    MonitorGraph_ReadMsgDumpConfig();
    CHECK(g_nMsgCountOfDumpTrigger == 7 && g_dwMsgDumpTriggerElapseTick == 10000);

    // certification addresses are copied with their field size: 15 characters fit, 16 fail
    Fw11WriteFile(strCfg, "common { certification \"123.123.123.123\", 1 "
                          "certification_ip_bind \"123.123.123.124\" }\r\n");
    CHECK(ServerConfig_Load());
    CHECK(strcmp(g_ServerGlobals.szCertificationIP, "123.123.123.123") == 0);
    CHECK(strcmp(g_ServerGlobals.szCertificationBindIP, "123.123.123.124") == 0);
    Fw11WriteFile(strCfg, "common { certification \"123.123.123.1234\", 2 }\r\n");
    CHECK(!ServerConfig_Load());
    CHECK(strcmp(g_ServerGlobals.szCertificationIP, "123.123.123.123") == 0);
    CHECK(g_ServerGlobals.nCertificationPort == 1);
    Fw11WriteFile(strCfg, "common { certification_ip_bind \"123.123.123.1255\" }\r\n");
    CHECK(!ServerConfig_Load());
    CHECK(strcmp(g_ServerGlobals.szCertificationBindIP, "123.123.123.125") == 0);
    DeleteFileA(strCfg.c_str());
    g_ServerGlobals.mapExtra.clear();
    g_ServerGlobals.szCertificationIP[0] = 0;
    g_ServerGlobals.szCertificationBindIP[0] = 0;
}

static void TestFw11MsgDump()
{
    CMsg* pHeld = Fw11Msg(0x1234);
    MONITOR_COUNTER counter;
    memset(&counter, 0, sizeof(counter));
    counter.dwID = MONITOR_COUNTER_MSG;
    NETENGINE_STATISTICS stats;

    // trigger reached, elapse passed: header and the pool dump
    g_nMsgCountOfDumpTrigger = 1;
    g_dwMsgDumpTriggerElapseTick = 0;
    Fw11Drain();
    memset(&stats, 0, sizeof(stats));
    g_pBSNet->GetStatistics(BSNET_STAT_MSG_POOL, &stats);
    CHECK(MonitorGraph_ReadNetEngineCounter(&counter));
    // (the overlap process posts its timer messages meanwhile)
    CHECK(stats.dwMsgUsed >= 1 && counter.fValue >= 1.0f
          && counter.fValue <= (float)stats.dwMsgUsed + 2.0f);
    Fw11Drain();
    CHECK(!s_vecFwLogs.empty() && s_vecFwLogs[0].dwFlags == 0x2000001
          && s_vecFwLogs[0].strText == "============== Msg Usage ================");
    bool bOpcode = false;
    for (size_t i = 0; i < s_vecFwLogs.size(); ++i)
    {
        if (s_vecFwLogs[i].strText.find("[MsgID: 0x1234 >> Count: ") == 0)
            bOpcode = true;
    }
    CHECK(bOpcode);

    // within the elapse time, without a trigger or below it: no dump
    g_dwMsgDumpTriggerElapseTick = 100000;
    CHECK(MonitorGraph_ReadNetEngineCounter(&counter));
    g_dwMsgDumpTriggerElapseTick = 0;
    g_nMsgCountOfDumpTrigger = 0;
    CHECK(MonitorGraph_ReadNetEngineCounter(&counter));
    g_nMsgCountOfDumpTrigger = 100000;
    CHECK(MonitorGraph_ReadNetEngineCounter(&counter));
    Fw11Drain();
    CHECK(s_vecFwLogs.empty());
    CHECK(counter.fValue >= 1.0f);
    g_nMsgCountOfDumpTrigger = 0;
    g_pBSNet->ReleaseMsg(pHeld);
}

static void TestFw11Fonts()
{
    static const struct
    {
        LANGID      wLang;
        const char* pszFace;
    } s_aFont[] = {
        { 0x419, "Tahoma" }, { 0x41E, "Microsoft Sans Serif" }, { 0x804, "SimSun" },
        { 0x412, "Gulim" }, { 0x404, "PMingliu" }, { 0x411, "MS Gothic" }, { 0x409, "Gulim" },
        { 0x41F, "Gulim" }, { 0x418, "Gulim" },
    };
    for (size_t i = 0; i < sizeof(s_aFont) / sizeof(s_aFont[0]); ++i)
    {
        s_wFwLang = s_aFont[i].wLang;
        char szFace[255];
        Win32_GetLocaleFontName(szFace, sizeof(szFace));
        CHECK(strcmp(szFace, s_aFont[i].pszFace) == 0);
    }

    // a font that cannot be created is reported
    s_vecFwBoxes.clear();
    s_wFwLang = 0x419;
    s_bFwFailCreateFont = true;
    Win32Dialog_SetLocaleFont(NULL, 12);
    s_bFwFailCreateFont = false;
    CHECK(s_vecFwBoxes.size() == 1);
    if (s_vecFwBoxes.size() == 1)
    {
        CHECK(s_vecFwBoxes[0].strText == "Cannot find proper font (LangID : 0x419, FontName : "
                                         "Tahoma), please contact to Joymax");
        CHECK(s_vecFwBoxes[0].strCaption == "Error" && s_vecFwBoxes[0].uType == MB_ICONWARNING);
    }
    s_vecFwBoxes.clear();
    Win32Dialog_SetLocaleFont(NULL, 12);
    CHECK(s_vecFwBoxes.empty());
    s_wFwLang = 0;
}

// The windows go first: the monitor view would otherwise get WM_DESTROY after it was deleted
// (FWUI-002).
static void Fw11DestroyFrame(CServerFrameWindow* pFrame)
{
    HWND hMoniter = pFrame->m_pMoniterView ? pFrame->m_pMoniterView->m_hWnd : NULL;
    if (pFrame->m_hWnd)
        DestroyWindow(pFrame->m_hWnd);
    pFrame->Destroy();
    if (hMoniter)
        g_mapWindow.erase(hMoniter);
    delete pFrame;
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
    {
    }
    // the next CreateFrame registers the classes again
    HINSTANCE hInstance = g_ServerGlobals.hInstance;
    UnregisterClassA(g_ServerGlobals.szServiceName, hInstance);
    UnregisterClassA("ServerArchitectureView", hInstance);
    UnregisterClassA("ServerMoniterView", hInstance);
}

static void TestFw11Frame()
{
    strcpy_s(g_ServerGlobals.szServiceName, "AgentServer");
    g_ServerGlobals.hInstance = GetModuleHandleA(NULL);
    // the architecture view paints this server's body
    static CERT_MODULE s_Module;
    static CERT_MACHINE s_Machine;
    static SERVER_BODY s_Self;
    static ServerBodyList s_lstBody;
    static ServerCordList s_lstCord;
    memset(&s_Module, 0, sizeof(s_Module));
    strcpy_s(s_Module.szName, "AgentServer");
    memset(&s_Machine, 0, sizeof(s_Machine));
    memset(&s_Self, 0, sizeof(s_Self));
    s_Self.wID = 1;
    s_Self.pModule = &s_Module;
    s_Self.pMachine = &s_Machine;
    s_lstBody.push_back(&s_Self);
    g_pServerBodyList = &s_lstBody;
    g_pServerCordList = &s_lstCord;
    g_pSelfBody = &s_Self;

    CServerFrameWindow* pFrame = new CServerFrameWindow;
    g_pServerFrameWindow = pFrame;
    CHECK(pFrame->CreateFrame());
    CHECK(pFrame->m_mapCmdHandler.size() == 5
          && pFrame->m_mapCmdHandler.find(IDM_FRAME_COMMAND) != pFrame->m_mapCmdHandler.end());
    SendMessageA(pFrame->m_hWnd, WM_COMMAND, IDM_FRAME_COMMAND, 0);
    CHECK(s_pFwApp->m_nFrameCommand == 1);

    // log list menu: three items for the application, the command is returned
    static const struct
    {
        UINT        uFlags;
        UINT        uID;
        const char* pszText;
    } s_aItem[] = {
        { MF_STRING, 0x100, "Save && Clear" }, { MF_SEPARATOR, 0, "" },
        { MF_STRING, 0x101, "Save" }, { MF_STRING, 0x102, "Clear" },
        { MF_STRING, 0x103, "Copy SelectedItem To Clipboard" }, { MF_SEPARATOR, 0, "" },
        { MF_STRING, 0x104, "Register Suspected User" },
        { MF_STRING, 0x105, "Unregister Suspected User" },
        { MF_STRING, 0x106, "Show Registered Suspected Users" },
    };
    NMHDR hdr;
    hdr.hwndFrom = pFrame->m_hLogList;
    hdr.idFrom = IDC_SERVER_LOG_LIST;
    hdr.code = NM_RCLICK;
    for (BOOL nCmd = 0x104; nCmd <= 0x107; ++nCmd)
    {
        s_vecFwMenu.clear();
        s_nFwTrackResult = nCmd == 0x107 ? 0 : nCmd;
        s_uFwTrackFlags = 0;
        CHECK(pFrame->OnNotify(IDC_SERVER_LOG_LIST, (LPARAM)&hdr) == 0);
        CHECK(s_uFwTrackFlags == TPM_RETURNCMD);
        CHECK(s_vecFwMenu.size() == 9);
        for (size_t i = 0; i < s_vecFwMenu.size() && i < 9; ++i)
        {
            CHECK(s_vecFwMenu[i].uFlags == s_aItem[i].uFlags
                  && s_vecFwMenu[i].uID == s_aItem[i].uID
                  && s_vecFwMenu[i].strText == s_aItem[i].pszText);
        }
    }
    CHECK(s_pFwApp->m_vecSuspected.size() == 3 && s_pFwApp->m_vecSuspected[0] == 0
          && s_pFwApp->m_vecSuspected[1] == 1 && s_pFwApp->m_vecSuspected[2] == 2);

    // diagnostic dump: the headers go to the fatal log
    pFrame->m_dwDumpFlags = SERVER_DUMP_ENGINE_VERSION | SERVER_DUMP_MSG_USAGE
                            | SERVER_DUMP_CUSTOM;
    Fw11Drain();
    CHECK(pFrame->OnDump(0, 0) == 0);
    Fw11Drain();
    CHECK(Fw11Logged(0x2000001, "============== Engine Version ================"));
    CHECK(Fw11Logged(0x2000001, "============== Msg Usage ================"));
    CHECK(Fw11Logged(0x2000001, "============== Custom Dump ================"));
    CHECK(!Fw11Logged(0, "============== Custom Dump ================"));
    CHECK(s_pFwApp->m_nCustomDump == 1);

    // the monitor thread reads the dump settings when it starts
    g_ServerGlobals.mapExtra["MsgDumpTriggerElapseTick"] = "77";
    MonitorGraph_StartUpdateThread();
    CHECK(g_dwMsgDumpTriggerElapseTick == 77);
    MonitorGraph_StopUpdateThreadAndFreeCounters();
    g_ServerGlobals.mapExtra.clear();

    Fw11DestroyFrame(pFrame);
    g_pServerFrameWindow = NULL;
    g_pServerBodyList = NULL;
    g_pServerCordList = NULL;
    g_pSelfBody = NULL;
    Fw11Drain();

    // monitor graph names: at most 63 characters, nothing written past the field
    CMoniterGraphData graph;
    MONITOR_COUNTER counter;
    memset(&counter, 0, sizeof(counter));
    counter.dwSampleCount = 5;
    BYTE abAfter[sizeof(CMoniterGraphData)];
    memcpy(abAfter, &graph, sizeof(graph));
    std::string strName(70, 'n');
    graph.Init(strName.c_str(), 0x123456, &counter);
    CHECK(strlen(graph.m_szName) == 63 && strName.compare(0, 63, graph.m_szName) == 0);
    CHECK(memcmp(abAfter + 68, (BYTE*)&graph + 68, 92 - 68) == 0);
    CHECK(graph.m_pCounter == &counter && counter.dwSampleCount == 0
          && graph.m_clrLine == 0x123456);
}

// The filter is called from an exception filter of this thread.
static LONG s_lFwFilterResult;

static LONG Fw11CallFilter(EXCEPTION_POINTERS* pExceptionInfo)
{
    s_lFwFilterResult = CrashDump_TopLevelExceptionFilter(pExceptionInfo);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void Fw11RaiseForDump()
{
    __try
    {
        RaiseException(0xE0001111, 0, 0, NULL);
    }
    __except (Fw11CallFilter(GetExceptionInformation()))
    {
    }
}

static int Fw11CountLines(const std::string& str, const char* pszLine)
{
    int n = 0;
    for (size_t i = str.find(pszLine); i != std::string::npos; i = str.find(pszLine, i + 1))
        ++n;
    return n;
}

static void TestFw11CrashDump()
{
    char szOldDir[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, szOldDir);
    SetCurrentDirectoryA(s_strFwDir.c_str());
    std::string strLog = s_strFwDir + "\\ExceptionHandlerInfo.log";
    DeleteFileA(strLog.c_str());
    std::string strDumps = s_strFwDir + "\\AgentServer_[*].dmp";

    CHECK(CrashDump_TopLevelExceptionFilter(NULL) == EXCEPTION_EXECUTE_HANDLER);
    CHECK(GetFileAttributesA(strLog.c_str()) == INVALID_FILE_ATTRIBUTES);

    g_bCrashDumpWritten = 0;
    s_lFwFilterResult = 0;
    Fw11RaiseForDump();
    CHECK(s_lFwFilterResult == EXCEPTION_EXECUTE_HANDLER && g_bCrashDumpWritten == 1);
    std::string str = Fw11ReadFile(strLog);
    CHECK(Fw11IsTimestamp(str) && str.substr(20) == "TopLevelFilter has been called!\r\r\n");
    CHECK(Fw11DeleteFiles(strDumps) == 1);

    // a stack overflow is dumped from a thread of its own
    EXCEPTION_RECORD rec;
    memset(&rec, 0, sizeof(rec));
    rec.ExceptionCode = EXCEPTION_STACK_OVERFLOW;
    CONTEXT ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_FULL;
    GetThreadContext(GetCurrentThread(), &ctx);
    EXCEPTION_POINTERS ep;
    ep.ExceptionRecord = &rec;
    ep.ContextRecord = &ctx;
    g_bCrashDumpWritten = 0;
    CHECK(CrashDump_TopLevelExceptionFilter(&ep) == EXCEPTION_EXECUTE_HANDLER);
    CHECK(g_bCrashDumpWritten == 1);
    str = Fw11ReadFile(strLog);
    CHECK(Fw11CountLines(str, "\tTopLevelFilter has been called!\r\r\n") == 2);
    CHECK(Fw11DeleteFiles(strDumps) == 1);
    Fw11DeleteFiles(s_strFwDir + "\\AgentServer_[*].txt");
    DeleteFileA(strLog.c_str());
    SetCurrentDirectoryA(szOldDir);
}

static void TestFw11Shutdown()
{
    LogCapture_Stop();
    CHECK(s_pFwApp->Shutdown());
    CHECK(g_pBSNet == NULL);
}

// RunWindowMode with failing steps (no engine is started).
class CFw11RunApp : public CServerApp
{
public:
    CFw11RunApp(bool bTasks) : m_bTasks(bTasks), m_nShutdown(0), m_nCertification(0),
                               m_hPortAtTasks(NULL) {}
    virtual bool Initialize() { return true; }
    virtual bool InitNetEngine() { return true; }
    virtual bool RegisterServerTasks()
    {
        m_hPortAtTasks = g_hServerIOCP;
        return m_bTasks;
    }
    virtual bool RequestServerCertification()
    {
        ++m_nCertification;
        return false;
    }
    virtual bool Shutdown()
    {
        ++m_nShutdown;
        return true;
    }

    bool    m_bTasks;
    int     m_nShutdown;
    int     m_nCertification;
    HANDLE  m_hPortAtTasks;
};

static void Fw11RunWindowMode(bool bTasks, const char* pszClass, const char* pszBox)
{
    strcpy_s(g_ServerGlobals.szServiceName, pszClass);
    CFw11RunApp* pApp = new CFw11RunApp(bTasks);
    g_pServerApp = pApp;
    s_vecFwBoxes.clear();
    CHECK(ServerApp_RunWindowMode() == -1);
    CHECK(pApp->m_hPortAtTasks != NULL && pApp->m_hPortAtTasks == g_hServerIOCP);
    CHECK(pApp->m_nShutdown == 1 && pApp->m_nCertification == (bTasks ? 1 : 0));
    CHECK(Fw11Boxes(pszBox));
    MonitorGraph_StopUpdateThreadAndFreeCounters();
    if (g_pServerFrameWindow)
    {
        Fw11DestroyFrame(g_pServerFrameWindow);
        g_pServerFrameWindow = NULL;
    }
    if (g_hServerIOCP)
        CloseHandle(g_hServerIOCP);
    g_hServerIOCP = NULL;
    g_pServerApp = NULL;
    delete pApp;
}

static void TestFw11Startup()
{
    Fw11RunWindowMode(false, "Fw11RunA", "It has been failed to Initialize Server Task");
    Fw11RunWindowMode(true, "Fw11RunB", "It has been failed to Request Certification");

    // the semaphore lives in the global namespace
    HANDLE hSemaphore = SingleInstance_CreateSemaphore("Global\\Fw11TestSemaphore");
    CHECK(hSemaphore != NULL);
    CHECK(SingleInstance_CreateSemaphore("Global\\Fw11TestSemaphore") == NULL);
    SingleInstance_Close(&hSemaphore);

    HANDLE hOther = OpenSemaphoreA(SYNCHRONIZE, FALSE, "Global\\AgentServer");
    if (hOther)
    {
        CloseHandle(hOther);
        printf("  an AgentServer runs here: startup semaphore check skipped\n");
        return;
    }
    HANDLE hTaken = CreateSemaphoreA(NULL, 0, 1, "Global\\AgentServer");
    CHECK(hTaken != NULL);
    std::string strError = s_strFwDir + "\\StartupError_AgentServer.txt";
    DeleteFileA(strError.c_str());
    s_vecFwBoxes.clear();
    CHECK(ServerApp_Startup(GetModuleHandleA(NULL), "cmd") == -1);
    CHECK(strcmp(g_ServerGlobals.szServiceName, "AgentServer") == 0);
    CHECK(Fw11Boxes("cannot create semaphore : module already executing"));
    CHECK(Fw11ReadFile(strError) == "cannot create semaphore : module already executing\r\n");
    DeleteFileA(strError.c_str());
    CloseHandle(hTaken);
}

static void TestFramework2011()
{
    char szTemp[MAX_PATH];
    GetTempPathA(MAX_PATH, szTemp);
    char szDir[MAX_PATH];
    sprintf_s(szDir, "%sFramework2011Test_%u", szTemp, GetCurrentProcessId());
    CreateDirectoryA(szDir, NULL);
    s_strFwDir = szDir;
    strcpy_s(g_ServerGlobals.szModuleDir, szDir);
    strcpy_s(g_ServerGlobals.szServiceName, "AgentServer");
    g_ServerGlobals.hInstance = GetModuleHandleA(NULL);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    g_ServerGlobals.dwProcessorCount = si.dwNumberOfProcessors;
    g_ServerGlobals.nMaxSendQueDepth = -1;
    BSDebugSetFlags(0, 0);
    Fw11HookImports();

    TestFw11Layout();
    LogCapture_Start();
    TestFw11Engine();
    TestFw11Logs();
    TestFw11MsgDescription();
    TestFw11MainProcess();
    TestFw11TaskRegistration();
    TestFw11Config();
    TestFw11MsgDump();
    TestFw11Fonts();
    TestFw11Frame();
    TestFw11CrashDump();
    TestFw11Shutdown();
    TestFw11Startup();
    g_pServerApp = NULL;
    RemoveDirectoryA(s_strFwDir.c_str());
}

int main()
{
    CHECK(LoopbackBind_Install() == 1);
    TestLibrary2011();
    TestFramework2011();
    return TEST_RESULT("Framework2011Test");
}
