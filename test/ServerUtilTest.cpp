#include "BSLib/Util/TimeSpan.h"
#include "BSLib/Util/DateTimeUtil.h"
#include "BSLib/file/IFile.h"
#include "BSNet/IBSNet.h"
#include "BSNet/MsgPool.h"
#include "ServerFramework/FileTransMgr.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/MonitorGraph.h"
#include "ServerFramework/OverlapJob.h"
#include "ServerFramework/ServerConfig.h"

#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <stdexcept>
#include <vector>

#include "TestCheck.h"

#pragma comment(lib, "comctl32.lib")

using namespace ServerFramework;

// ---------------------------------------------------------------------------------------------
// Parts of the server application the tested code calls

ServerFramework::CServerFrameWindow* g_pServerFrameWindow = NULL;

static int s_nViewDummy;
static CServerMoniterView* const s_pFakeView = (CServerMoniterView*)&s_nViewDummy;

struct GRAPH_CALL
{
    bool                bAdd;
    CServerMoniterView* pView;
    DWORD               dwID;
    std::string         strName;
    MONITOR_COUNTER*    pCounter;
};

static CCriticalSectionBS s_csCalls;
static std::vector<GRAPH_CALL> s_GraphCalls;

// Stands in for the server window: only the monitor view pointer (+0xBC) is read.
static CServerMoniterView* s_apFakeFrame[0xC8 / sizeof(CServerMoniterView*)];

void MonitorView_AddGraph(CServerMoniterView* pView, DWORD dwID, const char* pszName,
                          MONITOR_COUNTER* pCounter)
{
    CSingleLockBS lock(&s_csCalls, TRUE);
    GRAPH_CALL call = { true, pView, dwID, pszName, pCounter };
    s_GraphCalls.push_back(call);
}

void MonitorView_RemoveGraph(CServerMoniterView* pView, DWORD dwID)
{
    CSingleLockBS lock(&s_csCalls, TRUE);
    GRAPH_CALL call = { false, pView, dwID, "", NULL };
    s_GraphCalls.push_back(call);
}


// ---------------------------------------------------------------------------------------------
// Engine stand-in: messages from a CMsgPool, sends and file calls are recorded.

struct SENT_MSG
{
    DWORD               dwSession;
    WORD                wOpcode;
    std::vector<BYTE>   payload;
};

class CFakeNet : public IBSNet
{
public:
    CMsgPool                m_Pool;
    std::vector<SENT_MSG>   m_Sent;
    int                     m_nAllocated;
    int                     m_nSocketState;
    BOOL                    m_bBeginResult;
    int                     m_nBeginCalls;
    DWORD                   m_dwBeginSession;
    std::string             m_strBeginPath;
    DWORD                   m_dwBeginSize;
    DWORD                   m_dwBeginOffset;
    int                     m_nBeginType;
    DWORD                   m_dwClosedSession;
    DWORD                   m_dwClosedHandle;
    DWORD                   m_dwDisconnectSession;
    DWORD                   m_dwDisconnectReason;

    CFakeNet()
    {
        m_nAllocated = 0;
        m_nSocketState = 0;
        m_bBeginResult = 1;
        m_nBeginCalls = 0;
        m_dwBeginSession = m_dwBeginSize = m_dwBeginOffset = 0;
        m_nBeginType = 0;
        m_dwClosedSession = m_dwClosedHandle = 0xFFFFFFFF;
        m_dwDisconnectSession = m_dwDisconnectReason = 0;
    }

    virtual void __stdcall GetStatistics(DWORD dwFlags, NETENGINE_STATISTICS* p)
    {
        if (dwFlags & BSNET_STAT_MSG_POOL)
            p->dwMsgUsed = 11;
        if (dwFlags & BSNET_STAT_SOCKET_POOL)
            p->dwSocketUsed = 22;
        if (dwFlags & BSNET_STAT_SESSIONS)
            p->dwSessionUsed = 33;
        if (dwFlags & BSNET_STAT_FILES)
        {
            p->dwOpenFiles = 44;
            p->dwFileUsers = 55;
        }
        if (dwFlags & BSNET_STAT_ACCEPT)
            p->dwPendingAccepts = 66;
        if (dwFlags & BSNET_STAT_SESSION_LIST)
            p->dwLinkedSessions = 0xFFFFFFFF;
    }
    virtual CMsg* __stdcall AllocMsg(BOOL)
    {
        CMsg* p = m_Pool.Alloc();
        InterlockedIncrement(&p->m_lRefCount);
        ++m_nAllocated;
        return p;
    }
    virtual LONG __stdcall ReleaseMsg(CMsg* p)
    {
        if (p->m_lRefCount > 0 && InterlockedDecrement(&p->m_lRefCount) == 0)
        {
            m_Pool.Free(p);
            --m_nAllocated;
        }
        return 0;
    }
    virtual int __stdcall SendMsg(DWORD dwSession, CMsg* p)
    {
        SENT_MSG sent;
        sent.dwSession = dwSession;
        sent.wOpcode = p->GetOpcode();
        sent.payload.assign(p->m_pData + MSG_HEADER_SIZE,
                            p->m_pData + MSG_HEADER_SIZE + p->GetPayloadSize());
        m_Sent.push_back(sent);
        return 1;
    }
    virtual BOOL __stdcall BeginSessionFileTransfer(DWORD dwSession, std::string strPath,
                                                    DWORD dwSize, DWORD dwOffset, int nType)
    {
        ++m_nBeginCalls;
        m_dwBeginSession = dwSession;
        m_strBeginPath = strPath;
        m_dwBeginSize = dwSize;
        m_dwBeginOffset = dwOffset;
        m_nBeginType = nType;
        return m_bBeginResult;
    }
    virtual int __stdcall DisconnectSession(DWORD dwSession, DWORD dwReason)
    {
        m_dwDisconnectSession = dwSession;
        m_dwDisconnectReason = dwReason;
        return 7;
    }
    virtual int __stdcall CloseSessionTransferFile(DWORD dwSession, DWORD dwHandle)
    {
        m_dwClosedSession = dwSession;
        m_dwClosedHandle = dwHandle;
        return 0;
    }
    virtual int __stdcall GetSessionSocketState(DWORD) { return m_nSocketState; }

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
    virtual BOOL __stdcall GetSessionPeerAddress(DWORD, std::string*, ULONG*, u_short*)
    {
        return 0;
    }
    virtual CTask* __stdcall GetProactor(DWORD) { return 0; }
    virtual BOOL __stdcall StartHandshake(DWORD, SECURITY_HANDSHAKE_PARAM*) { return 0; }
    virtual BOOL __stdcall SendFile(DWORD, const std::string&, DWORD, int) { return 0; }
    virtual void __stdcall ExcludeSessionFromCleanup(DWORD) {}
    virtual BOOL __stdcall RemoveKeepAliveTarget(DWORD) { return 0; }
    virtual void __stdcall AddKeepAliveTarget(const char*, u_short, const char*, DWORD, DWORD) {}
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

// A file in a given transfer mode with a given engine handle.
class CFakeFile : public IFile
{
public:
    CFakeFile(int nMode, DWORD dwHandle)
    {
        m_nMode = nMode;
        m_dwHandle = dwHandle;
    }
    virtual DWORD ReadAt(DWORD*, void*, DWORD) { return 0; }
    virtual DWORD WriteAt(DWORD*, const void*, DWORD) { return 0; }
    virtual void Close() {}
    virtual BOOL Flush() { return TRUE; }
    virtual HANDLE GetHandle() { return NULL; }
    virtual BOOL IsOpen() { return TRUE; }
};

static DWORD ReadDword(const std::vector<BYTE>& v, size_t nOffset)
{
    DWORD dw;
    memcpy(&dw, &v[nOffset], 4);
    return dw;
}

// Temporary directory for the test, with a trailing backslash like the module directory.
static std::string MakeTempDir()
{
    char szTemp[MAX_PATH];
    GetTempPathA(MAX_PATH, szTemp);
    char szDir[MAX_PATH];
    sprintf_s(szDir, "%sServerUtilTest_%lu\\", szTemp, GetCurrentProcessId());
    CreateDirectoryA(szDir, NULL);
    return szDir;
}

static void WriteTextFile(const std::string& strPath, const char* pszText)
{
    FILE* f = NULL;
    fopen_s(&f, strPath.c_str(), "wb");
    if (f)
    {
        fwrite(pszText, 1, strlen(pszText), f);
        fclose(f);
    }
}

// ---------------------------------------------------------------------------------------------

static void TestTimeSpan()
{
    CTimeSpan span(1, 2, 3, 4);
    CHECK(span.GetTotalSeconds() == 93784);
    CHECK(span.GetDays() == 1);
    CHECK(span.GetHours() == 2);
    CHECK(span.GetMinutes() == 3);
    CHECK(span.GetSeconds() == 4);
    CHECK(span.GetTotalHours() == 26);
    CHECK(span.GetTotalMinutes() == 26 * 60 + 3);

    // C division truncates toward zero
    CTimeSpan neg(-90);
    CHECK(neg.GetTotalMinutes() == -1);
    CHECK(neg.GetMinutes() == -1);
    CHECK(neg.GetSeconds() == -30);
    CHECK(neg.GetDays() == 0);

    CTimeSpan a(100);
    CTimeSpan b(30);
    CHECK((a - b).GetTotalSeconds() == 70);
    CHECK((a + b).GetTotalSeconds() == 130);
    CTimeSpan c(a);
    c += b;
    CHECK(c.GetTotalSeconds() == 130);
    c -= CTimeSpan(200);
    CHECK(c.GetTotalSeconds() == -70);
    c = a;
    CHECK(c == a && !(c != a));
    CHECK(b < a && a > b && b <= a && a >= b && a <= c && a >= c);
    CHECK(!(a < b) && !(b > a));
}

static void TestDateTime()
{
    CDateTime dt(2024, 2, 29, 13, 45, 10);
    struct tm atm;
    memset(&atm, 0, sizeof(atm));
    atm.tm_year = 124;
    atm.tm_mon = 1;
    atm.tm_mday = 29;
    atm.tm_hour = 13;
    atm.tm_min = 45;
    atm.tm_sec = 10;
    atm.tm_isdst = -1;
    CHECK(dt.GetTime() == _mktime32(&atm));
    CHECK(dt.GetYear() == 2024);
    CHECK(dt.GetMonth() == 2);
    CHECK(dt.GetDay() == 29);
    CHECK(dt.GetHour() == 13);
    CHECK(dt.GetMinute() == 45);
    CHECK(dt.GetSecond() == 10);
    CHECK(dt.GetDayOfWeek() == 5);      // Thursday

    // arithmetic
    CDateTime next = dt + CTimeSpan(1, 0, 0, 0);
    CHECK(next.GetMonth() == 3 && next.GetDay() == 1 && next.GetHour() == 13);
    CHECK((next - dt).GetTotalSeconds() == 86400);
    CHECK((next - CTimeSpan(86400)) == dt);
    CDateTime moving(dt);
    moving += CTimeSpan(0, 0, 1, 5);
    CHECK(moving.GetMinute() == 46 && moving.GetSecond() == 15);
    moving -= CTimeSpan(65);
    CHECK(moving == dt && !(moving != dt));
    CHECK(dt < next && next > dt && dt <= next && next >= dt && dt <= moving);

    // components rolled over by mktime
    CDateTime roll(2023, 12, 31, 23, 59, 60);
    CHECK(roll.GetYear() == 2024 && roll.GetMonth() == 1 && roll.GetDay() == 1);
    CHECK(roll.GetHour() == 0 && roll.GetMinute() == 0 && roll.GetSecond() == 0);

    // DOS date and time (2-second resolution)
    WORD wDate = (WORD)(((2024 - 1980) << 9) | (2 << 5) | 29);
    WORD wTime = (WORD)((13 << 11) | (45 << 5) | (10 / 2));
    CDateTime dos(wDate, wTime);
    CHECK(dos == dt);

    // SYSTEMTIME: the day of week and milliseconds are ignored, years before 1900 give 0
    SYSTEMTIME st;
    memset(&st, 0, sizeof(st));
    st.wYear = 2024;
    st.wMonth = 2;
    st.wDay = 29;
    st.wHour = 13;
    st.wMinute = 45;
    st.wSecond = 10;
    st.wMilliseconds = 999;
    CHECK(CDateTime(st) == dt);
    st.wYear = 1899;
    CHECK(CDateTime(st).GetTime() == 0);

    SYSTEMTIME out;
    memset(&out, 0xFF, sizeof(out));
    CHECK(dt.GetAsSystemTime(out));
    CHECK(out.wYear == 2024 && out.wMonth == 2 && out.wDay == 29 && out.wDayOfWeek == 4);
    CHECK(out.wHour == 13 && out.wMinute == 45 && out.wSecond == 10 && out.wMilliseconds == 0);

    // FILETIME is UTC: build it from the current time and compare with the local result
    CDateTime now = CDateTime::GetCurrent();
    __time32_t t = now.GetTime();
    CHECK(t > 1700000000);
    ULONGLONG ull = (ULONGLONG)t * 10000000 + 116444736000000000ULL;
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)ull;
    ft.dwHighDateTime = (DWORD)(ull >> 32);
    CHECK(CDateTime(ft).GetTime() == t);

    // only the time value is copied
    CDateTime copy;
    copy = dt;
    CHECK(copy.GetTime() == dt.GetTime());
    copy = (__time32_t)12345;
    CHECK(copy.GetTime() == 12345);
}

// Days from 1900-01-01 to the given date.
static long DaysSince1900(int nYear, int nMonth, int nDay)
{
    static const int s_anDays[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    long lDays = 0;
    for (int y = 1900; y < nYear; ++y)
        lDays += ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
    lDays += s_anDays[nMonth - 1] + nDay - 1;
    if (nMonth > 2 && ((nYear % 4 == 0 && nYear % 100 != 0) || nYear % 400 == 0))
        ++lDays;
    return lDays;
}

static void TestDateTimeUtil()
{
    // smalldatetime: days since 1900 / minute of the day, standard time only
    SYSTEMTIME stBefore;
    GetLocalTime(&stBefore);
    DWORD dwSmall = GetLocalTimeAsSmallDateTime();
    long lMinute = stBefore.wHour * 60 + stBefore.wMinute;
    long lDays = DaysSince1900(stBefore.wYear, stBefore.wMonth, stBefore.wDay);
    long lGot = (long)(dwSmall >> 16) * 1440 + (long)(dwSmall & 0xFFFF);
    long lWant = lDays * 1440 + lMinute;
    TIME_ZONE_INFORMATION tzi;
    bool bDaylight = GetTimeZoneInformation(&tzi) == TIME_ZONE_ID_DAYLIGHT;
    long lDiff = lWant - lGot;
    if (bDaylight)
        lDiff += tzi.DaylightBias;      // the bug: no DST correction
    CHECK(lDiff == 0 || lDiff == -1);      // the minute may change in between
    CHECK((dwSmall & 0xFFFF) < 1440);

    // long date, a space, time
    SYSTEMTIME st;
    memset(&st, 0, sizeof(st));
    st.wYear = 2024;
    st.wMonth = 2;
    st.wDay = 29;
    st.wDayOfWeek = 4;
    st.wHour = 13;
    st.wMinute = 45;
    st.wSecond = 10;
    char szFmt[MAX_PATH];
    char szDate[MAX_PATH];
    char szTime[MAX_PATH];
    GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP | LOCALE_SLONGDATE, szFmt, MAX_PATH);
    GetDateFormatA(LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP, &st, szFmt, szDate, MAX_PATH);
    GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP | LOCALE_STIMEFORMAT, szFmt, MAX_PATH);
    GetTimeFormatA(LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP, &st, szFmt, szTime, MAX_PATH);
    std::string strWant = std::string(szDate) + " " + szTime;
    CHECK(Time_FormatLocalDateTime(&st) == strWant);
    CHECK(strstr(Time_FormatLocalDateTime(&st).c_str(), "2024") != NULL);

    // date and time picker pair in a hidden window
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_DATE_CLASSES };
    CHECK(InitCommonControlsEx(&icc));
    HWND hParent = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 300, 100, NULL, NULL,
                                   NULL, NULL);
    HWND hDate = CreateWindowExA(0, DATETIMEPICK_CLASSA, "", WS_CHILD | DTS_SHORTDATEFORMAT,
                                 0, 0, 150, 20, hParent, (HMENU)101, NULL, NULL);
    HWND hTime = CreateWindowExA(0, DATETIMEPICK_CLASSA, "", WS_CHILD | DTS_TIMEFORMAT, 150, 0,
                                 150, 20, hParent, (HMENU)102, NULL, NULL);
    CHECK(hParent && hDate && hTime);

    SYSTEMTIME stDate = st;
    Dlg_SetDateTimePickers(hParent, 101, 102, &stDate);
    SYSTEMTIME stTime = st;
    stTime.wYear = 2020;
    stTime.wHour = 7;
    stTime.wMinute = 8;
    stTime.wSecond = 9;
    SendMessageA(hTime, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&stTime);

    SYSTEMTIME stGot;
    memset(&stGot, 0, sizeof(stGot));
    CHECK(Dlg_GetDateTimeFromPickers(hParent, 101, 102, &stGot) == &stGot);
    CHECK(stGot.wYear == 2024 && stGot.wMonth == 2 && stGot.wDay == 29);
    CHECK(stGot.wHour == 7 && stGot.wMinute == 8 && stGot.wSecond == 9);
    CHECK(DateTimePicker_FormatDateTimeString(hDate, hTime) == "2024-2-29 07:08:09");

    DestroyWindow(hParent);
}

static const char s_szServerCfg[] =
    "// Server.cfg for the test\r\n"
    "/* other modules\r\n   are skipped */\r\n"
    "FarmManager { certification \"9.9.9.9\", 1  MaxSendQueDepth 9 }\r\n"
    "common\r\n"
    "{\r\n"
    "    certification \"127.0.0.1\", 15880\r\n"
    "    certification_ip_bind \"10.0.0.2\"\r\n"
    "}\r\n"
    "GATEWAYSERVER\r\n"
    "{\r\n"
    "    debug_option_debugger_present { DEBUG_OPTION_ASSERT_SHOW_CALLSTACK\r\n"
    "        debug_option_assert_write_minidump }\r\n"
    "    debug_option_stand_alone { DEBUG_OPTION_ASSERT_CALL_CALLBACK }\r\n"
    "    netengine_debug_option_stand_alone { DEBUG_OPTION_ASSERT_ADVANCE_BREAK }\r\n"
    "    DumpMsgPool 1\r\n"
    "    ReportConnectionLog 2\r\n"
    "    MaxSendQueDepth 300\r\n"
    "    PromptAtClose 1\r\n"
    "    WritePerfLogToText 1\r\n"
    "    CustomKey \"some value\"\r\n"
    "    Other 12\r\n"
    "}\r\n";

static void TestServerConfig(const std::string& strDir)
{
    // tokenizer
    char szTok[256];
    const char* pszText = "a{b}\"q r\"//x\n/*c*/ -5 ,x/y";
    const char* aWant[] = { "a", "{", "b", "}", "q r", "-", "5", ",", "x", "/", "y" };
    char* p = (char*)pszText;
    for (int i = 0; i < 11; ++i)
    {
        p = Cfg_ReadNextToken(p, szTok);
        CHECK(p != NULL && strcmp(szTok, aWant[i]) == 0);
    }
    CHECK(Cfg_ReadNextToken(p, szTok) == NULL && szTok[0] == 0);
    CHECK(Cfg_ReadNextToken(NULL, szTok) == NULL && szTok[0] == 0);
    // bytes >= 0x80 separate tokens; an unterminated string ends the text
    p = Cfg_ReadNextToken("\xB0" "ab\xB0" "cd", szTok);
    CHECK(p != NULL && strcmp(szTok, "ab") == 0);
    p = Cfg_ReadNextToken(p, szTok);
    CHECK(p != NULL && strcmp(szTok, "cd") == 0);
    CHECK(Cfg_ReadNextToken("\"abc", szTok) == NULL && strcmp(szTok, "abc") == 0);
    CHECK(Cfg_ReadNextToken("\"\"x", szTok) != NULL && szTok[0] == 0);
    CHECK(Cfg_ReadNextToken("ab\"c d", szTok) != NULL && strcmp(szTok, "ab\"c") == 0);

    // no file
    strcpy_s(g_ServerGlobals.szModuleDir, (strDir + "missing\\").c_str());
    CHECK(!ServerConfig_Load());

    // defaults as ServerApp_Startup sets them
    strcpy_s(g_ServerGlobals.szModuleDir, strDir.c_str());
    g_ServerGlobals.dwDebugOptionDebuggerPresent = 0x112;
    g_ServerGlobals.dwDebugOptionStandAlone = 0x201;
    g_ServerGlobals.nMaxSendQueDepth = -1;
    WriteTextFile(strDir + "Server.cfg", s_szServerCfg);
    CHECK(ServerConfig_Load());
    CHECK(strcmp(g_ServerGlobals.szCertificationIP, "127.0.0.1") == 0);
    CHECK(strcmp(g_ServerGlobals.szCertificationBindIP, "10.0.0.2") == 0);
    CHECK(g_ServerGlobals.nCertificationPort == 15880);
    CHECK(g_ServerGlobals.dwDebugOptionDebuggerPresent == (0x008 | 0x200));
    CHECK(g_ServerGlobals.dwDebugOptionStandAlone == 0x400);
    CHECK(g_ServerGlobals.dwNetEngineDebugOptionDebuggerPresent == 0);
    CHECK(g_ServerGlobals.dwNetEngineDebugOptionStandAlone == 0x010);
    CHECK(g_ServerGlobals.bDumpMsgPool == TRUE);
    CHECK(g_ServerGlobals.bReportConnectionLog == FALSE);
    CHECK(g_ServerGlobals.nMaxSendQueDepth == 300);
    CHECK(g_ServerGlobals.bPromptAtClose == TRUE);
    CHECK(g_ServerGlobals.bWritePerfLogToText == TRUE);
    CHECK(g_ServerGlobals.mapExtra.size() == 2);
    CHECK(g_ServerGlobals.mapExtra["CustomKey"] == "some value");
    CHECK(g_ServerGlobals.mapExtra["Other"] == "12");

    // '-' is a token of its own: "-1" reads as 0 and "1" becomes a key, after which keys and
    // values are out of step; a key without a value ends the parse
    g_ServerGlobals.mapExtra.clear();
    WriteTextFile(strDir + "Server.cfg",
                  "common { MaxSendQueDepth -1 PromptAtClose 0 } GatewayServer { Lonely");
    CHECK(ServerConfig_Load());
    CHECK(g_ServerGlobals.nMaxSendQueDepth == 0);
    CHECK(g_ServerGlobals.mapExtra.size() == 3);
    CHECK(g_ServerGlobals.mapExtra["1"] == "PromptAtClose");
    CHECK(g_ServerGlobals.mapExtra["0"] == "}");
    CHECK(g_ServerGlobals.mapExtra["GatewayServer"] == "{");
    CHECK(g_ServerGlobals.bPromptAtClose == TRUE);

    // a block name without "{" stops the parse (still true)
    g_ServerGlobals.nCertificationPort = 0;
    WriteTextFile(strDir + "Server.cfg",
                  "common certification \"1.1.1.1\", 5 } common { certification \"2.2.2.2\", 6 }");
    CHECK(ServerConfig_Load());
    CHECK(g_ServerGlobals.nCertificationPort == 0);

    // FWUTIL-001: a certification address longer than 15 characters runs into the bind address
    WriteTextFile(strDir + "Server.cfg",
                  "common { certification_ip_bind \"10.0.0.2\" "
                  "certification \"aaaaaaaaaaaaaaaaBBBB\", 7 }");
    CHECK(ServerConfig_Load());
    CHECK(strcmp(g_ServerGlobals.szCertificationBindIP, "BBBB") == 0);
    CHECK(g_ServerGlobals.nCertificationPort == 7);
    DeleteFileA((strDir + "Server.cfg").c_str());

    // engine parameter defaults by engine flags
    BSNET_INIT_PARAM param;
    Cfg_InitDefaultsFromFlags(&param, BSNET_FLAG_SERVER | BSNET_FLAG_LARGE);
    CHECK(param.dwFlags == 0x11 && param.nAcceptCount == 100 && param.dwMaxConnectTime == 60);
    CHECK(param.aSocket[0].nSendQueueLimit == -1 && param.aSocket[1].nSendQueueLimit == -1);
    CHECK(param.dwFileCacheLimit == 0 && param.nSendFileType == 1 && param.nRecvFileType == 1);
    CHECK(param.dw8C == 0x112 && param.dw90 == 0x201);
    CHECK(param.aSocket[0].dwSendMode == 0 && param.aSocket[1].dwSendMode == 1);

    BSNET_INIT_PARAM client;
    client.dwMaxConnectTime = 1234;
    Cfg_InitDefaultsFromFlags(&client, 0x20);
    MEMORYSTATUS ms;
    GlobalMemoryStatus(&ms);
    CHECK(client.dwMaxConnectTime == 1234 && client.nAcceptCount == 0);
    CHECK(client.dwFileCacheLimit == ms.dwTotalPhys >> 2);
    CHECK(client.nSendFileType == 2 && client.nRecvFileType == 1);
    CHECK(client.dw90 == 0x12A);
    Cfg_InitDefaultsFromFlags(&client, 0);
    CHECK(client.nRecvFileType == 0 && client.dwFileCacheLimit == 0);
}

static volatile LONG s_lCustomUpdates;
static HANDLE s_hCustomUpdated;

static bool __cdecl CustomCounterUpdate(MONITOR_COUNTER* pCounter)
{
    pCounter->fValue = 5.5f;
    InterlockedIncrement(&s_lCustomUpdates);
    SetEvent(s_hCustomUpdated);
    return true;
}

static void TestMonitorGraph(CFakeNet& net)
{
    MONITOR_COUNTER custom;
    memset(&custom, 0, sizeof(custom));
    custom.pfnUpdate = CustomCounterUpdate;

    // no server window: nothing happens
    MonitorGraph_StartUpdateThread();
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_MSG);
    MonitorGraph_AddCounterLocked(0x9000, "Custom", &custom);
    CHECK(s_GraphCalls.empty() && custom.dwMaxSamples == 0);

    s_apFakeFrame[0xBC / sizeof(CServerMoniterView*)] = s_pFakeView;
    g_pServerFrameWindow = (CServerFrameWindow*)s_apFakeFrame;
    // the overlap job ID counters the graph reads
    ServerFramework::g_OverlapJobIDPool.m_dwStatNextID = 1;
    ServerFramework::g_OverlapJobIDPool.m_dwInUse = 42;
    ServerFramework::g_OverlapJobIDPool.m_dwStatFree = 3;
    // the view is only known once the update thread was started
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_MSG);
    CHECK(s_GraphCalls.empty());

    s_hCustomUpdated = CreateEventA(NULL, FALSE, FALSE, NULL);
    MonitorGraph_StartUpdateThread();
    CHECK(g_pMonitorGraphView == s_pFakeView);

    static const DWORD s_adwID[] =
    {
        MONITOR_COUNTER_MSG, MONITOR_COUNTER_SESSION, MONITOR_COUNTER_SOCK_TCP,
        MONITOR_COUNTER_CLASS_LINK, MONITOR_COUNTER_ACCEPT, MONITOR_COUNTER_USED_FILE,
        MONITOR_COUNTER_CACHED_FILE, MONITOR_COUNTER_MASSIVE_MSG, MONITOR_COUNTER_OVERLAP,
        0x2010008,
    };
    static const char* s_apszName[] =
    {
        "MsgCount", "SessionCount", "SockTCPCount", "ClassLinkCount", "AcceptCount",
        "UsedFileCount", "CachedFileCount", "MassiveMsgCount", "OverlapCount",
        "\xC0\xCC\xB0\xD4\xB9\xB9\xB3\xC4?",
    };
    static const float s_afValue[] =
    {
        11, 33, 22, 4294967295.0f, 66, 55, 44, 2, 42, -1,
    };
    for (int i = 0; i < 10; ++i)
        MonitorGraph_RegisterNetEngineCounter(s_adwID[i]);
    CHECK(s_GraphCalls.size() == 10);

    CMassiveMsg* pM1 = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    CMassiveMsg* pM2 = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    for (size_t i = 0; i < s_GraphCalls.size() && i < 10; ++i)
    {
        GRAPH_CALL call;
        {
            CSingleLockBS lock(&s_csCalls, TRUE);
            call = s_GraphCalls[i];
        }
        CHECK(call.bAdd && call.pView == s_pFakeView && call.dwID == s_adwID[i]);
        CHECK(call.strName == s_apszName[i]);
        MONITOR_COUNTER* pCounter = call.pCounter;
        CHECK(pCounter->dwID == s_adwID[i] && pCounter->dwMaxSamples == 0);
        CHECK(pCounter->pfnUpdate == MonitorGraph_ReadNetEngineCounter);
        pCounter->fValue = -1;
        CHECK(MonitorGraph_ReadNetEngineCounter(pCounter));
        CHECK(pCounter->fValue == s_afValue[i]);
    }
    pM1->Free();
    pM2->Free();

    // without an engine the counters are not read
    MONITOR_COUNTER loose;
    memset(&loose, 0, sizeof(loose));
    loose.dwID = MONITOR_COUNTER_MSG;
    loose.fValue = -1;
    g_pBSNet = NULL;
    CHECK(!MonitorGraph_ReadNetEngineCounter(&loose));
    CHECK(loose.fValue == -1);
    g_pBSNet = &net;
    CHECK(MonitorGraph_ReadNetEngineCounter(&loose) && loose.fValue == 11);

    // a counter of the caller: 100 samples, updated by the thread about once a second
    MonitorGraph_AddCounterLocked(0x9000, "Custom", &custom);
    CHECK(custom.dwMaxSamples == 100);
    CHECK(s_GraphCalls.size() == 11 && s_GraphCalls[10].strName == "Custom");
    CHECK(s_GraphCalls[10].pCounter == &custom);
    MonitorGraph_AddCounterLocked(0x9000, "Again", &custom);     // asserts, nothing added
    CHECK(s_GraphCalls.size() == 11);
    CHECK(WaitForSingleObject(s_hCustomUpdated, 3000) == WAIT_OBJECT_0);
    CHECK(custom.fValue == 5.5f);

    MonitorGraph_RemoveCounterLocked(0x9001);                   // unknown: nothing
    CHECK(s_GraphCalls.size() == 11);
    MonitorGraph_RemoveCounterLocked(0x9000);
    CHECK(s_GraphCalls.size() == 12 && !s_GraphCalls[11].bAdd && s_GraphCalls[11].dwID == 0x9000);
    // let a running update pass finish, then the counter must not be updated any more
    Sleep(100);
    s_lCustomUpdates = 0;
    Sleep(2200);
    CHECK(s_lCustomUpdates == 0);

    // stop deletes the registered counters (the removed one is not touched)
    MonitorGraph_StopUpdateThreadAndFreeCounters();
    MonitorGraph_StopUpdateThreadAndFreeCounters();     // the stale event handle is only set
    CHECK(custom.dwMaxSamples == 100 && custom.pfnUpdate == CustomCounterUpdate);
    // the map is empty again: the same ID can be registered once more
    s_GraphCalls.clear();
    MonitorGraph_AddCounterLocked(0x9000, "Custom", &custom);
    CHECK(s_GraphCalls.size() == 1);
    MonitorGraph_RemoveCounterLocked(0x9000);

    // without the window Remove does nothing
    g_pServerFrameWindow = NULL;
    MonitorGraph_AddCounterLocked(0x9002, "x", &custom);
    MonitorGraph_RemoveCounterLocked(0x9000);
    CHECK(s_GraphCalls.size() == 2);
    CloseHandle(s_hCustomUpdated);
}

static void TestFileTransMgr(CFakeNet& net, const std::string& strDir)
{
    strcpy_s(g_ServerGlobals.szModuleDir, strDir.c_str());
    CHECK(g_FileTransMgr.m_dwFlags == 0 && g_FileTransMgr.m_pEngine == NULL);
    CHECK(FileXfer_InitEngineAndName("Down"));
    CHECK(g_FileTransMgr.m_pEngine == &net);
    CHECK(g_FileTransMgr.m_dwFlags == (FILETRANS_FLAG_SEND | FILETRANS_FLAG_RECV));
    CHECK(g_strFileTransDir == "Down");

    // nothing pending: asserts and fails
    CHECK(g_FileTransMgr.StartRecv(55) == -1);
    CHECK(net.m_nBeginCalls == 0);

    MODULE_VERSION_FILE* pA = new MODULE_VERSION_FILE;
    MODULE_VERSION_FILE* pB = new MODULE_VERSION_FILE;
    memset(pA, 0, sizeof(*pA));
    memset(pB, 0, sizeof(*pB));
    pA->dwID = 7;
    strcpy_s(pA->szName, "a.txt");
    strcpy_s(pA->szPath, "sub");
    pA->dwSize = 100;
    pB->dwID = 8;
    strcpy_s(pB->szName, "b.bin");
    pB->dwSize = 5;
    ModuleVersionFileList files;
    files.push_back(pA);
    files.push_back(pB);

    // the target files, where the paths point to (the module directory ends in '\', so the
    // paths contain "\\")
    CreateDirectoryA((strDir + "Down").c_str(), NULL);
    CreateDirectoryA((strDir + "Down\\patch").c_str(), NULL);
    CreateDirectoryA((strDir + "Down\\patch\\sub").c_str(), NULL);
    WriteTextFile(strDir + "Down\\patch\\sub\\a.txt", "a");
    WriteTextFile(strDir + "Down\\patch\\b.bin", "b");

    CHECK(g_FileTransMgr.BuildRecvQueueAndStart(55, "patch", &files));
    CHECK(g_FileTransPendingList.size() == 2);
    const FILE_TRANS_ENTRY& e1 = g_FileTransPendingList.front();
    const FILE_TRANS_ENTRY& e2 = g_FileTransPendingList.back();
    CHECK(std::string(e1.pszPath) == strDir + "\\Down\\patch\\sub\\a.txt");
    CHECK(std::string(e2.pszPath) == strDir + "\\Down\\patch\\b.bin");
    CHECK(GetFileAttributesA(e1.pszPath) != INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesA(e2.pszPath) != INVALID_FILE_ATTRIBUTES);
    CHECK(e1.dwFileID == 7 && e1.dwSize == 100 && e1.bC == 0 && e1.dwOffset == 0);
    CHECK(e1.pSource == pA && e2.pSource == pB && e2.dwFileID == 8);

    // the first file is requested from the session
    CHECK(g_FileTransMgr.m_dwServerSession == 55 && g_FileTransMgr.m_RecvQueue.size() == 2);
    CHECK(g_FileTransMgr.m_RecvQueue.front().pszPath == e1.pszPath);
    CHECK(net.m_nBeginCalls == 1 && net.m_dwBeginSession == 55);
    CHECK(net.m_strBeginPath == e1.pszPath && net.m_dwBeginSize == 100);
    CHECK(net.m_dwBeginOffset == 0 && net.m_nBeginType == -1);
    CHECK(net.m_Sent.size() == 1 && net.m_Sent[0].dwSession == 55);
    CHECK(net.m_Sent[0].wOpcode == FILETRANS_OPCODE_REQ && net.m_Sent[0].payload.size() == 8);
    CHECK(ReadDword(net.m_Sent[0].payload, 0) == 7 && ReadDword(net.m_Sent[0].payload, 4) == 0);
    CHECK(net.m_nAllocated == 0);

    // a received file: the front is dropped and the engine file closed
    CFakeFile received(1, 0x99);
    CHECK(g_FileTransMgr.OnFileDone(&received, 55) == 1);
    CHECK(net.m_dwClosedSession == 0 && net.m_dwClosedHandle == 0x99);
    CHECK(g_FileTransMgr.m_RecvQueue.size() == 1);
    CHECK(g_FileTransMgr.SendNextQueuedFile() == 1);
    CHECK(net.m_nBeginCalls == 2 && net.m_strBeginPath == e2.pszPath && net.m_dwBeginSize == 5);
    CHECK(net.m_Sent.size() == 2 && ReadDword(net.m_Sent[1].payload, 0) == 8);
    CHECK(g_FileTransMgr.OnFileDone(&received, 55) == 0);
    CHECK(g_FileTransMgr.SendNextQueuedFile() == 0);
    CHECK(net.m_nBeginCalls == 2);

    // with the queue empty, a received-file event throws when the front is read
    bool bThrown = false;
    try
    {
        g_FileTransMgr.OnFileDone(&received, 55);
    }
    catch (std::exception&)
    {
        bThrown = true;
    }
    CHECK(bThrown);

    // a served file is confirmed with 0xA004 {1} while the session exists
    CFakeFile served(0, 0x77);
    CHECK(g_FileTransMgr.OnFileDone(&served, 56) == 0);
    CHECK(net.m_Sent.size() == 3 && net.m_Sent[2].dwSession == 56);
    CHECK(net.m_Sent[2].wOpcode == FILETRANS_OPCODE_ACK && net.m_Sent[2].payload.size() == 1);
    CHECK(net.m_Sent[2].payload[0] == 1 && net.m_dwClosedHandle == 0x77);
    net.m_nSocketState = -1;
    CHECK(g_FileTransMgr.OnFileDone(&served, 56) == 0);
    CHECK(net.m_Sent.size() == 3);
    net.m_nSocketState = 0;
    CHECK(net.m_nAllocated == 0);

    // a file in no mode: nothing but closing the handle
    CFakeFile closed(-1, 0x55);
    CHECK(g_FileTransMgr.OnFileDone(&closed, 56) == -1);
    CHECK(net.m_dwClosedHandle == 0x55);

    // the engine refuses the transfer: -1, the queue stays
    net.m_bBeginResult = 0;
    CHECK(g_FileTransMgr.StartRecv(57) == -1);
    CHECK(g_FileTransMgr.m_RecvQueue.size() == 2 && g_FileTransMgr.m_dwServerSession == 57);
    CHECK(net.m_Sent.size() == 3);
    net.m_bBeginResult = 1;
    CHECK(!g_FileTransMgr.BuildRecvQueueAndStart(0, NULL, &files));     // no session
    CHECK(g_FileTransMgr.m_RecvQueue.empty() && g_FileTransPendingList.size() == 2);
    CHECK(std::string(g_FileTransPendingList.front().pszPath) ==
          strDir + "\\Down\\sub\\a.txt");

    // a failed transfer empties the queue and drops the session
    CHECK(g_FileTransMgr.StartRecv(58) == 2);
    CHECK(g_FileTransMgr.OnFileFailed(58) == 7);
    CHECK(g_FileTransMgr.m_RecvQueue.empty());
    CHECK(net.m_dwDisconnectSession == 58 && net.m_dwDisconnectReason == 1);

    // without receive mode nothing starts
    CHECK(g_FileTransMgr.StartRecv(58) == 2);
    CHECK(g_FileTransMgr.m_RecvQueue.size() == 2);
    FileXfer_ResetQueueAndState_Global();
    CHECK(g_FileTransMgr.m_dwFlags == 0 && g_FileTransMgr.m_dwServerSession == 0);
    CHECK(g_FileTransMgr.m_RecvQueue.empty());
    CHECK(g_FileTransMgr.StartRecv(59) == -1);
    CHECK(g_FileTransMgr.m_pEngine == &net);

    // an empty transfer directory and no sub directory leave those parts out
    g_strFileTransDir.clear();
    g_FileTransMgr.Init();
    CHECK(g_FileTransMgr.BuildRecvQueueAndStart(60, "", &files));
    CHECK(std::string(g_FileTransPendingList.back().pszPath) == strDir + "\\b.bin");
    g_FileTransMgr.Reset();

    DeleteFileA((strDir + "Down\\patch\\sub\\a.txt").c_str());
    DeleteFileA((strDir + "Down\\patch\\b.bin").c_str());
    RemoveDirectoryA((strDir + "Down\\patch\\sub").c_str());
    RemoveDirectoryA((strDir + "Down\\patch").c_str());
    RemoveDirectoryA((strDir + "Down").c_str());
    delete pA;
    delete pB;
}

int main()
{
    CFakeNet net;
    g_pBSNet = &net;
    std::string strDir = MakeTempDir();

    TestTimeSpan();
    TestDateTime();
    TestDateTimeUtil();
    TestServerConfig(strDir);
    TestMonitorGraph(net);
    TestFileTransMgr(net, strDir);

    RemoveDirectoryA(strDir.c_str());
    return TEST_RESULT("ServerUtilTest");
}
