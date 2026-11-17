// MachineManager application: CMachineManager on the real network engine, which connects to
// itself. A client task of the same engine plays the GlobalManager; the handlers get packets
// built byte by byte and their relay answers are compared byte by byte. The server modules it
// starts and stops are copies of MachineTestChild.exe (built with this test) in a temp
// directory, under names no other program has. The test links the MachineManager variants of
// the framework files that name the module (SERVER_MODULE_NAME "MachineManager").

#include "BSLib/BSDebug.h"
#include "BSNet/NetEngine.h"
#include "BSNet/SockListener.h"
#include "MachineManager/MachineManager.h"
#include "MachineManager/MachineManagerMainProcess.h"
#include "MachineManager/PerfCounter.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"

#include <stdio.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>

#include "LogCapture.h"
#include "TestCheck.h"

using namespace ServerFramework;

typedef std::vector<BYTE> Bytes;

static CRITICAL_SECTION s_cs;
static std::string s_strDir;

// ---------------------------------------------------------------------------------------------
// Byte and file helpers

static void Put(Bytes& v, const void* p, size_t n)
{
    v.insert(v.end(), (const BYTE*)p, (const BYTE*)p + n);
}

static void PutB(Bytes& v, BYTE b)
{
    v.push_back(b);
}

static void PutW(Bytes& v, WORD w)
{
    Put(v, &w, 2);
}

static void PutD(Bytes& v, DWORD d)
{
    Put(v, &d, 4);
}

static std::string Hex(const Bytes& v)
{
    std::string s;
    char sz[4];
    for (size_t i = 0; i < v.size() && i < 64; ++i)
    {
        sprintf_s(sz, "%02x ", v[i]);
        s += sz;
    }
    return s;
}

static bool Same(const Bytes& vGot, const Bytes& vWant)
{
    if (vGot == vWant)
        return true;
    printf("  got:  %s(%u)\n  want: %s(%u)\n", Hex(vGot).c_str(), (unsigned)vGot.size(),
           Hex(vWant).c_str(), (unsigned)vWant.size());
    return false;
}

static void WriteDiskFile(const std::string& strPath, const char* pszText)
{
    FILE* pFile = NULL;
    fopen_s(&pFile, strPath.c_str(), "wb");
    CHECK(pFile != NULL);
    if (!pFile)
        return;
    fwrite(pszText, 1, strlen(pszText), pFile);
    fclose(pFile);
}

static bool WaitFor(bool (*pfn)(), DWORD dwTimeout = 5000)
{
    DWORD dwStart = GetTickCount();
    while (!pfn())
    {
        if (GetTickCount() - dwStart > dwTimeout)
            return false;
        Sleep(5);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Log lines

static std::vector<CAPTURED_LOG> s_vecLog;
static size_t s_nLogChecked;

static void DrainLogs()
{
    std::vector<CAPTURED_LOG> v = LogCapture_Drain();
    s_vecLog.insert(s_vecLog.end(), v.begin(), v.end());
}

// Waits for a line starting with strPrefix after the last line found.
static bool WaitLog(const std::string& strPrefix, DWORD dwFlags = (DWORD)-1,
                    std::string* pstrLine = NULL)
{
    DWORD dwStart = GetTickCount();
    for (;;)
    {
        DrainLogs();
        for (size_t i = s_nLogChecked; i < s_vecLog.size(); ++i)
        {
            if (s_vecLog[i].strText.compare(0, strPrefix.size(), strPrefix) == 0
                && (dwFlags == (DWORD)-1 || s_vecLog[i].dwFlags == dwFlags))
            {
                s_nLogChecked = i + 1;
                if (pstrLine)
                    *pstrLine = s_vecLog[i].strText;
                return true;
            }
        }
        if (GetTickCount() - dwStart > 3000)
        {
            printf("  missing log line: %s\n", strPrefix.c_str());
            return false;
        }
        Sleep(5);
    }
}

static bool HasLogSince(size_t nFrom, const std::string& strPrefix)
{
    DrainLogs();
    for (size_t i = nFrom; i < s_vecLog.size(); ++i)
    {
        if (s_vecLog[i].strText.compare(0, strPrefix.size(), strPrefix) == 0)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// The client task: records every packet and session event it gets.

enum
{
    TEST_TASK_CLIENT    = 0x01000100,
};

struct PACKET
{
    DWORD   dwSession;
    WORD    wOpcode;
    Bytes   data;
};

static std::deque<PACKET> s_queIn;

class CTestClient : public CServerProcessBase
{
    BS_DECLARE_DYNCREATE(CTestClient)

public:
    virtual void InitMsgHandlers()
    {
    }

    virtual void OnUnhandledMsg(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
    {
        PACKET packet;
        packet.dwSession = pMsg->m_dwSessionID;
        packet.wOpcode = pMsg->GetOpcode();
        packet.data.assign(pMsg->m_pData + pMsg->m_wReadPos, pMsg->m_pData + pMsg->m_wWritePos);
        pMsg->SkipRemaining();
        EnterCriticalSection(&s_cs);
        s_queIn.push_back(packet);
        LeaveCriticalSection(&s_cs);
    }
};

BS_IMPLEMENT_DYNCREATE(CTestClient, &CServerProcessBase::classCServerProcessBase)

static bool TakePacket(DWORD dwSession, WORD wOpcode, Bytes* pData, DWORD dwTimeout = 3000,
                       bool bReport = true)
{
    DWORD dwStart = GetTickCount();
    for (;;)
    {
        EnterCriticalSection(&s_cs);
        for (std::deque<PACKET>::iterator it = s_queIn.begin(); it != s_queIn.end(); ++it)
        {
            if (it->dwSession == dwSession && it->wOpcode == wOpcode)
            {
                if (pData)
                    *pData = it->data;
                s_queIn.erase(it);
                LeaveCriticalSection(&s_cs);
                return true;
            }
        }
        LeaveCriticalSection(&s_cs);
        if (GetTickCount() - dwStart > dwTimeout)
        {
            if (bReport)
                printf("  no packet 0x%04x on session %u\n", wOpcode, dwSession);
            return false;
        }
        Sleep(5);
    }
}

static void SendPacket(DWORD dwSession, WORD wOpcode, const Bytes& data)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(wOpcode);
    if (!data.empty())
        pMsg->WriteBytes(&data[0], (WORD)data.size());
    g_pBSNet->SendMsg(dwSession, pMsg);
    g_pBSNet->ReleaseMsg(pMsg);
}

static CMsg* MakeMsg(DWORD dwSession, WORD wOpcode, const Bytes& data)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(wOpcode);
    if (!data.empty())
        pMsg->WriteBytes(&data[0], (WORD)data.size());
    pMsg->m_dwSessionID = dwSession;
    return pMsg;
}

static u_short s_wPort;

struct CLIENT
{
    DWORD dwClient;     // the client task's session
    DWORD dwServer;     // the machine manager's session of the same connection
};

static CLIENT Connect()
{
    CLIENT c = { 0, 0 };
    c.dwClient = g_pBSNet->Connect("127.0.0.1", s_wPort, NULL, TEST_TASK_CLIENT, 0);
    CHECK(c.dwClient != 0);
    std::string strLine;
    CHECK(WaitLog("session created [", 0, &strLine));
    sscanf_s(strLine.c_str(), "session created [%u]", &c.dwServer);
    CHECK(c.dwServer != 0 && c.dwServer != c.dwClient);
    CHECK(TakePacket(c.dwClient, 0x1001, NULL));
    CHECK(TakePacket(c.dwClient, 0x1003, NULL));
    return c;
}

static CMachineManagerMainProcess* Main()
{
    return static_cast<CMachineManagerMainProcess*>(
        g_pBSNet->GetProactor(SERVER_TASK_MAIN)->GetJob());
}

// ---------------------------------------------------------------------------------------------
// Test modules: copies of MachineTestChild.exe

enum
{
    MOD_MACHINE = 12, MOD_GLOBAL_MANAGER = 3,
    MOD_WND = 40, MOD_NOWND = 41, MOD_MISSING = 42, MOD_UNKNOWN = 99,
};

static char s_szWnd[64];        // window, closes on WM_CLOSE
static char s_szNoWnd[64];      // no window, has to be terminated
static char s_szMissing[64];    // no exe

static std::string ExePath(const char* pszName)
{
    return s_strDir + "\\" + pszName + ".exe";
}

static void CopyChild(const char* pszName)
{
    char szSelf[MAX_PATH];
    GetModuleFileNameA(NULL, szSelf, MAX_PATH);
    std::string strChild = szSelf;
    strChild = strChild.substr(0, strChild.rfind('\\')) + "\\MachineTestChild.exe";
    CHECK(CopyFileA(strChild.c_str(), ExePath(pszName).c_str(), FALSE) != FALSE);
}

// Process of a test module, opened to watch it; NULL if none runs.
static HANDLE OpenModule(const char* pszName)
{
    DWORD dwID = FindOtherProcessByExeName(pszName);
    if (!dwID)
        return NULL;
    return OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, dwID);
}

static const char* s_pszWaitName;

static bool ModuleUp()
{
    if (!SingleInstance_Exists(s_pszWaitName))
        return false;
    return strstr(s_pszWaitName, "NoWnd") || FindWindowA(s_pszWaitName, s_pszWaitName);
}

static bool WaitModuleUp(const char* pszName)
{
    s_pszWaitName = pszName;
    return WaitFor(ModuleUp);
}

static DWORD ExitCodeOf(HANDLE hProcess)
{
    DWORD dwCode = STILL_ACTIVE;
    if (WaitForSingleObject(hProcess, 5000) == WAIT_OBJECT_0)
        GetExitCodeProcess(hProcess, &dwCode);
    return dwCode;
}

// Ends whatever is left of the test modules (only these names, which are unique to this run).
static void KillModules()
{
    const char* apsz[] = { s_szWnd, s_szNoWnd };
    for (int i = 0; i < 2; ++i)
    {
        DWORD dwID;
        while ((dwID = FindOtherProcessByExeName(apsz[i])) != 0)
        {
            HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, dwID);
            if (!h)
                break;
            TerminateProcess(h, 1);
            WaitForSingleObject(h, 5000);
            CloseHandle(h);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Architecture: body 1 is this machine manager, body 2 the GlobalManager.

template <class T>
static T Zero()
{
    T t;
    memset(&t, 0, sizeof(t));
    return t;
}

template <class T>
static void WriteList(CMassiveMsg* p, const std::vector<T>& v)
{
    BYTE b = 0;
    p->Write(&b, 1);
    for (size_t i = 0; i < v.size(); ++i)
    {
        b = 1;
        p->Write(&b, 1);
        p->Write(&v[i], sizeof(T));
    }
    b = 2;
    p->Write(&b, 1);
}

static CERT_MODULE Module(BYTE byID, const char* pszName)
{
    CERT_MODULE m = Zero<CERT_MODULE>();
    m.byID = byID;
    strcpy_s(m.szName, pszName);
    return m;
}

static SERVER_BODY Body(WORD wID, BYTE byModule, WORD wPort, int nState)
{
    SERVER_BODY b = Zero<SERVER_BODY>();
    b.wID = wID;
    b.byDivisionID = 3;
    b.dwMachineID = 100;
    b.byModuleID = byModule;
    b.wPort = wPort;
    b.nState = nState;
    return b;
}

static void SetUpArchitecture()
{
    std::vector<CERT_MODULE> modules;
    modules.push_back(Module(MOD_MACHINE, "MachineManager"));
    modules.push_back(Module(MOD_GLOBAL_MANAGER, "GlobalManager"));
    modules.push_back(Module(MOD_WND, s_szWnd));
    modules.push_back(Module(MOD_NOWND, s_szNoWnd));
    modules.push_back(Module(MOD_MISSING, s_szMissing));
    std::vector<CERT_CONTENT> contents(1, Zero<CERT_CONTENT>());
    contents[0].byID = 22;
    std::vector<CERT_DIVISION> divisions(1, Zero<CERT_DIVISION>());
    divisions[0].byID = 3;
    std::vector<CERT_FARM> farms;
    std::vector<CERT_FARM_CONTENT> farmContents;
    std::vector<CERT_SHARD> shards;
    std::vector<CERT_MACHINE> machines(1, Zero<CERT_MACHINE>());
    machines[0].dwID = 100;
    machines[0].byDivisionID = 3;
    strcpy_s(machines[0].szPublicIP, "127.0.0.1");
    strcpy_s(machines[0].szPrivateIP, "127.0.0.1");
    std::vector<SERVER_BODY> bodies;
    bodies.push_back(Body(1, MOD_MACHINE, 15881, 3));
    bodies.push_back(Body(2, MOD_GLOBAL_MANAGER, 15882, 5));
    std::vector<SERVER_CORD> cords(1, Zero<SERVER_CORD>());
    cords[0].dwID = 50;
    cords[0].wFromID = 1;
    cords[0].wToID = 2;

    CMassiveMsg* p = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    p->SetOpcode(0xA003);
    WriteList(p, modules);
    WriteList(p, contents);
    WriteList(p, divisions);
    WriteList(p, farms);
    WriteList(p, farmContents);
    WriteList(p, shards);
    WriteList(p, machines);
    WriteList(p, bodies);
    WriteList(p, cords);
    BYTE bNoCertData = 0;
    p->Write(&bNoCertData, 1);
    p->BeginRead();
    CHECK(CServerProcessMain_ParseA003CertArchAndBuildTargets(p));
    p->Free();
    CHECK(g_pSelfBody && g_pSelfBody->wID == 1);
}

// ---------------------------------------------------------------------------------------------
// Tests

static void TestConfig()
{
    CHECK(sizeof(CMachineManagerMainProcess) == 0x400C8);
    CHECK(CMachineManagerMainProcess::classCMachineManagerMainProcess.m_nObjectSize == 0x400C8);
    CHECK(strcmp(CMachineManagerMainProcess::classCMachineManagerMainProcess.m_lpszClassName,
                 "CMachineManagerMainProcess") == 0);
    CHECK(g_MachineManager.m_strFileTransDir.empty());

    // Server.cfg: only the "MachineManager" block counts
    std::string strCfg = s_strDir + "\\Server.cfg";
    WriteDiskFile(strCfg,
                  "common { DumpMsgPool 0 }\r\n"
                  "GatewayServer { ShutdownProcessTimeout 9 }\r\n"
                  "MachineManager { ShutdownProcessTimeout 250 ReportConnectionLog 1 }\r\n");
    CHECK(ServerConfig_Load());
    CHECK(g_ServerGlobals.bReportConnectionLog == TRUE);
    CHECK(MachineManager_FindShutdownProcessTimeoutConfig() != NULL
          && strcmp(MachineManager_FindShutdownProcessTimeoutConfig(), "250") == 0);
    g_ServerGlobals.mapExtra.clear();
    CHECK(MachineManager_FindShutdownProcessTimeoutConfig() == NULL);
    g_ServerGlobals.mapExtra["shutdownprocesstimeout"] = "5";
    CHECK(MachineManager_FindShutdownProcessTimeoutConfig() == NULL);
    g_ServerGlobals.mapExtra.clear();
    DeleteFileA(strCfg.c_str());

    // the module name of this build, the single-instance semaphore
    CHECK(ServerName_CompareCertification() != 0);
    HANDLE hSemaphore = SingleInstance_CreateSemaphore();
    CHECK(hSemaphore != NULL);
    HANDLE hOpen = OpenSemaphoreA(SYNCHRONIZE, FALSE, "MachineManager");
    CHECK(hOpen != NULL);
    if (hOpen)
        CloseHandle(hOpen);
    CHECK(SingleInstance_Exists("MachineManager") == TRUE);
    SingleInstance_Close(&hSemaphore);
    // a missing semaphore is created and closed again
    CHECK(SingleInstance_Exists(s_szWnd) == FALSE);
    CHECK(OpenSemaphoreA(SYNCHRONIZE, FALSE, s_szWnd) == NULL);

    // the exe path
    char szPath[MAX_PATH];
    CHECK(MachineManager_BuildModuleExePath(szPath, "GlobalManager"));
    CHECK(strcmp(szPath, (s_strDir + "\\GlobalManager.exe").c_str()) == 0);
    std::string strLong(250, 'x');
    char szSaved[MAX_PATH];
    strcpy_s(szSaved, g_ServerGlobals.szModuleDir);
    strcpy_s(g_ServerGlobals.szModuleDir, strLong.c_str());
    CHECK(!MachineManager_BuildModuleExePath(szPath, "GlobalManager"));
    CHECK(strcmp(szPath, (strLong + "\\GlobalMa").c_str()) == 0);
    strcpy_s(g_ServerGlobals.szModuleDir, szSaved);
}

// Whether PDH accepts the counter path on this machine (checked with a query of our own).
static bool PdhAccepts(int nIndex)
{
    PDH_HQUERY hQuery;
    if (PdhOpenQueryA(NULL, 0, &hQuery) != ERROR_SUCCESS)
        return false;
    PDH_HCOUNTER hCounter;
    bool bOk = PdhAddCounterA(hQuery, g_apszPerfCounterPath[nIndex], 0, &hCounter)
        == ERROR_SUCCESS;
    PdhCloseQuery(hQuery);
    return bOk;
}

// The statistics of the counter's ring as PDH computes them, 0 for a failed value (the committed
// bytes of a large machine do not fit a LONG).
static bool StatsMatch(const PERF_COUNTER* p)
{
    PDH_STATISTICS st;
    if (PdhComputeCounterStatistics(p->hCounter, PDH_FMT_LONG, p->nFirst, p->nCount,
                                    (PPDH_RAW_COUNTER)p->aRaw, &st) != ERROR_SUCCESS)
    {
        return false;
    }
    return p->lMin == (st.min.CStatus == ERROR_SUCCESS ? st.min.longValue : 0)
        && p->lMax == (st.max.CStatus == ERROR_SUCCESS ? st.max.longValue : 0)
        && p->lMean == (st.mean.CStatus == ERROR_SUCCESS ? st.mean.longValue : 0)
        && (p->lMax == 0 || (p->lMin <= p->lMean && p->lMean <= p->lMax));
}

static void TestPerfCounters()
{
    CHECK(strcmp(g_apszPerfCounterPath[0], "\\Processor(_Total)\\% Processor Time") == 0);
    CHECK(strcmp(g_apszPerfCounterPath[1], "\\Memory\\Committed Bytes") == 0);
    CHECK(PerfCounter_OpenQuery());
    CHECK(g_hPerfQuery != NULL);
    CHECK(!PerfCounter_Add(PERF_COUNTER_PROCESSOR));
    CHECK(!PerfCounter_Add(PERF_COUNTER_MEMORY));
    bool bMemory = PdhAccepts(PERF_COUNTER_MEMORY);
    CHECK(bMemory);
    CHECK((g_mapPerfCounter.count(PERF_COUNTER_PROCESSOR) != 0)
          == PdhAccepts(PERF_COUNTER_PROCESSOR));
    CHECK((g_mapPerfCounter.count(PERF_COUNTER_MEMORY) != 0) == bMemory);
    for (std::map<int, PERF_COUNTER*>::iterator it = g_mapPerfCounter.begin();
         it != g_mapPerfCounter.end(); ++it)
    {
        PERF_COUNTER* p = it->second;
        CHECK(p->hCounter != NULL && p->dw0 == 0 && p->lValue == 0 && p->nNext == 0
              && p->nFirst == 0 && p->nCount == 0 && p->lMin == 0 && p->lMax == 0
              && p->lMean == 0);
    }

    // an unknown index leaves the outputs alone
    LONG a = 11, b = 12, c = 13, d = 14;
    PerfCounter_GetValues(5, &a, &b, &c, &d);
    CHECK(a == 11 && b == 12 && c == 13 && d == 14);

    // the sample ring of the committed memory (an instant counter)
    if (bMemory)
    {
        PERF_COUNTER* p = g_mapPerfCounter[PERF_COUNTER_MEMORY];
        PerfCounter_CollectAll();
        CHECK(p->lValue > 0);
        CHECK(p->nCount == 1 && p->nNext == 1 && p->nFirst == 0);
        CHECK(StatsMatch(p));
        for (int i = 0; i < 20; ++i)
            PerfCounter_CollectAll();
        CHECK(p->nCount == 20 && p->nNext == 1 && p->nFirst == 1);
        CHECK(StatsMatch(p));
        PerfCounter_GetValues(PERF_COUNTER_MEMORY, &a, &b, &c, &d);
        CHECK(a == p->lValue && b == p->lMin && c == p->lMax && d == p->lMean);
    }

    // MM-001: the query handle is dropped but the query stays open
    PDH_HQUERY hQuery = g_hPerfQuery;
    PerfCounter_CloseAll();
    CHECK(g_hPerfQuery == NULL && g_mapPerfCounter.empty());
    CHECK(PdhCollectQueryData(hQuery) == ERROR_SUCCESS);
    CHECK(PdhCloseQuery(hQuery) == ERROR_SUCCESS);
    // nothing open: nothing to close
    PerfCounter_CloseAll();
    CHECK(g_hPerfQuery == NULL);
}

static void TestStartup()
{
    CHECK(g_MachineManager.InitNetEngine());
    BSNET_INIT_PARAM& p = g_MachineManager.m_EngineParam;
    CHECK(p.bSecurityMode == 3);
    CHECK(p.dwFlags == 3 && p.nAcceptCount == 10 && p.dwMaxConnectTime == 60);
    CHECK(p.dwFileCacheLimit == 0 && p.nSendFileType == 1 && p.nRecvFileType == 1);
    CHECK(p.aSocket[1].nSendQueueLimit == -1 && p.aSocket[0].dwSessionParam == SERVER_TASK_MAIN);
    CHECK(g_MachineManager.RegisterServerTasks());
    CHECK(Main()->GetRuntimeClass()
          == &CMachineManagerMainProcess::classCMachineManagerMainProcess);

    SetUpArchitecture();
    CHECK(g_MachineManager.OnLocalDataInitialized());
    CHECK(g_pSelfBody->nState == 5);
    CHECK(g_hPerfQuery != NULL);
    CHECK(g_mapPerfCounter.count(PERF_COUNTER_MEMORY) == 1);
}

static Bytes RelayAck(DWORD dwRelayID, WORD wOpcode, BYTE byResult)
{
    Bytes v;
    PutB(v, 1);
    PutD(v, dwRelayID);
    PutW(v, wOpcode);
    PutB(v, byResult);
    return v;
}

static Bytes RelayAck(DWORD dwRelayID, WORD wOpcode, BYTE byResult, BYTE byError)
{
    Bytes v = RelayAck(dwRelayID, wOpcode, byResult);
    PutB(v, byError);
    return v;
}

typedef void (CMachineManagerMainProcess::*PFN_HANDLER)(CMsg*, DWORD, DWORD, CMassiveMsg*);

// Calls the handler with {BYTE module} from the GlobalManager's session as relay 0x77 and
// returns the relay answer.
static Bytes Call(const CLIENT& gm, PFN_HANDLER pfn, WORD wOpcode, BYTE byModule)
{
    Bytes data;
    PutB(data, byModule);
    CMsg* pMsg = MakeMsg(gm.dwServer, wOpcode, data);
    (Main()->*pfn)(pMsg, 0x77, 0, NULL);
    CHECK(pMsg->GetUnreadSize() == 0);
    g_pBSNet->ReleaseMsg(pMsg);
    Bytes answer;
    CHECK(TakePacket(gm.dwClient, SERVER_MSG_RELAY_ACK, &answer, 8000));
    return answer;
}

static void TestErrors(const CLIENT& gm)
{
    PFN_HANDLER pfnStart = &CMachineManagerMainProcess::OnMsg_6301;
    PFN_HANDLER pfnStop = &CMachineManagerMainProcess::OnMsg_6302;

    // not certified
    SERVER_BODY* pSelf = g_pSelfBody;
    g_pSelfBody = NULL;
    CHECK(Same(Call(gm, pfnStart, 0x6301, MOD_WND), RelayAck(0x77, 0xA301, 2, 3)));
    CHECK(Same(Call(gm, pfnStop, 0x6302, MOD_WND), RelayAck(0x77, 0xA302, 2, 3)));
    g_pSelfBody = pSelf;

    // unknown module
    CHECK(Same(Call(gm, pfnStart, 0x6301, MOD_UNKNOWN), RelayAck(0x77, 0xA301, 2, 1)));
    CHECK(Same(Call(gm, pfnStop, 0x6302, MOD_UNKNOWN), RelayAck(0x77, 0xA302, 2, 1)));

    // not running
    CHECK(Same(Call(gm, pfnStop, 0x6302, MOD_WND), RelayAck(0x77, 0xA302, 2, 2)));

    // no exe
    CHECK(Same(Call(gm, pfnStart, 0x6301, MOD_MISSING), RelayAck(0x77, 0xA301, 2, 2)));
    CHECK(WaitLog("invalid path : " + ExePath(s_szMissing), 0x2000000));

    // the path does not fit: logged, then the start is tried with the cut path
    char szSaved[MAX_PATH];
    strcpy_s(szSaved, g_ServerGlobals.szModuleDir);
    std::string strLong = s_strDir + "\\" + std::string(250 - s_strDir.size(), 'y');
    strcpy_s(g_ServerGlobals.szModuleDir, strLong.c_str());
    CHECK(Same(Call(gm, pfnStart, 0x6301, MOD_WND), RelayAck(0x77, 0xA301, 2, 2)));
    CHECK(WaitLog("GetProcessName() Failed!", 0x1000000));
    CHECK(WaitLog("invalid path : " + (strLong + "\\" + s_szWnd).substr(0, MAX_PATH - 1),
                  0x2000000));
    strcpy_s(g_ServerGlobals.szModuleDir, szSaved);

    // the handlers are registered: 0x6302 as a plain packet of the session (relay ID 0)
    Bytes data;
    PutB(data, MOD_UNKNOWN);
    SendPacket(gm.dwClient, 0x6302, data);
    Bytes answer;
    CHECK(TakePacket(gm.dwClient, SERVER_MSG_RELAY_ACK, &answer));
    CHECK(Same(answer, RelayAck(0, 0xA302, 2, 1)));
}

static void TestStartStop(const CLIENT& gm)
{
    PFN_HANDLER pfnStart = &CMachineManagerMainProcess::OnMsg_6301;
    PFN_HANDLER pfnStop = &CMachineManagerMainProcess::OnMsg_6302;
    g_ServerGlobals.mapExtra["ShutdownProcessTimeout"] = "8000";

    // start: <module dir>\<name>.exe with the module directory as current directory
    size_t nLog = s_vecLog.size();
    CHECK(Same(Call(gm, pfnStart, 0x6301, MOD_WND), RelayAck(0x77, 0xA301, 1)));
    CHECK(WaitModuleUp(s_szWnd));
    HANDLE hFirst = OpenModule(s_szWnd);
    CHECK(hFirst != NULL);

    // start again: the running instance gets WM_CLOSE and ends, a new one is started
    CHECK(Same(Call(gm, pfnStart, 0x6301, MOD_WND), RelayAck(0x77, 0xA301, 1)));
    if (hFirst)
    {
        CHECK(ExitCodeOf(hFirst) == 7);
        CloseHandle(hFirst);
    }
    CHECK(WaitModuleUp(s_szWnd));
    HANDLE hSecond = OpenModule(s_szWnd);
    CHECK(hSecond != NULL);

    // stop: WM_CLOSE, ended before the timeout
    CHECK(Same(Call(gm, pfnStop, 0x6302, MOD_WND), RelayAck(0x77, 0xA302, 1)));
    if (hSecond)
    {
        CHECK(ExitCodeOf(hSecond) == 7);
        CloseHandle(hSecond);
    }
    CHECK(!HasLogSince(nLog, "terminate server process abnormaly"));
    CHECK(SingleInstance_Exists(s_szWnd) == FALSE);
    CHECK(Same(Call(gm, pfnStop, 0x6302, MOD_WND), RelayAck(0x77, 0xA302, 2, 2)));

    // without a window: found by exe name, terminated after ShutdownProcessTimeout
    g_ServerGlobals.mapExtra["ShutdownProcessTimeout"] = "300";
    CHECK(Same(Call(gm, pfnStart, 0x6301, MOD_NOWND), RelayAck(0x77, 0xA301, 1)));
    CHECK(WaitModuleUp(s_szNoWnd));
    HANDLE hNoWnd = OpenModule(s_szNoWnd);
    CHECK(hNoWnd != NULL);
    MSG msg;
    while (PeekMessageA(&msg, NULL, WM_CLOSE, WM_CLOSE, PM_REMOVE))
    {
    }
    DWORD dwStart = GetTickCount();
    CHECK(Same(Call(gm, pfnStop, 0x6302, MOD_NOWND), RelayAck(0x77, 0xA302, 1)));
    CHECK(GetTickCount() - dwStart >= 250);
    CHECK(WaitLog(std::string("terminate server process abnormaly : ") + s_szNoWnd, 0x1000000));
    if (hNoWnd)
    {
        CHECK(ExitCodeOf(hNoWnd) == (DWORD)-1);
        CloseHandle(hNoWnd);
    }
    // MM-003: the WM_CLOSE went to the calling thread
    CHECK(PeekMessageA(&msg, NULL, WM_CLOSE, WM_CLOSE, PM_REMOVE) && msg.hwnd == NULL);
    CHECK(!MachineManager_CloseOrTerminateProcess(s_szNoWnd));
    g_ServerGlobals.mapExtra.clear();
}

static void TestPerfStats(const CLIENT& gm)
{
    // as a plain packet of the session: collected, then {1, 2 * (value, min, max, mean)}
    SendPacket(gm.dwClient, 0x6304, Bytes());
    Bytes answer;
    CHECK(TakePacket(gm.dwClient, SERVER_MSG_RELAY_ACK, &answer));
    Bytes want;
    PutB(want, 1);
    PutD(want, 0);
    PutW(want, 0xA304);
    PutB(want, 1);
    for (int i = 0; i < 2; ++i)
    {
        LONG a = 0, b = 0, c = 0, d = 0;
        PerfCounter_GetValues(i, &a, &b, &c, &d);
        PutD(want, a);
        PutD(want, b);
        PutD(want, c);
        PutD(want, d);
    }
    CHECK(Same(answer, want));
    CHECK(g_mapPerfCounter[PERF_COUNTER_MEMORY]->nCount == 1);
    CHECK(g_mapPerfCounter[PERF_COUNTER_MEMORY]->lValue > 0);

    // without counters: zeros
    PDH_HQUERY hQuery = g_hPerfQuery;
    PerfCounter_CloseAll();
    PdhCloseQuery(hQuery);
    CMsg* pMsg = MakeMsg(gm.dwServer, 0x6304, Bytes());
    Main()->OnMsg_6304(pMsg, 0x99, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(TakePacket(gm.dwClient, SERVER_MSG_RELAY_ACK, &answer));
    want.clear();
    PutB(want, 1);
    PutD(want, 0x99);
    PutW(want, 0xA304);
    PutB(want, 1);
    want.insert(want.end(), 32, 0);
    CHECK(Same(answer, want));
    // open again for the shutdown
    PerfCounter_OpenQuery();
    PerfCounter_Add(PERF_COUNTER_MEMORY);
}

static void TestShutdown(const CLIENT& gm)
{
    g_pBSNet->DisconnectSession(gm.dwClient, 1);
    CHECK(TakePacket(gm.dwClient, 0x1002, NULL));
    DrainLogs();
    PDH_HQUERY hQuery = g_hPerfQuery;
    CHECK(g_MachineManager.Shutdown());
    CHECK(g_pBSNet == NULL && g_pServerBodyList == NULL);
    CHECK(g_hPerfQuery == NULL && g_mapPerfCounter.empty());
    PdhCloseQuery(hQuery);
    g_pSelfBody = NULL;
    g_pServerBodyStateObserver = NULL;
}

static void RemoveTree(const std::string& strDir)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((strDir + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            std::string strName = fd.cFileName;
            if (strName == "." || strName == "..")
                continue;
            std::string strPath = strDir + "\\" + strName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                RemoveTree(strPath);
            else
                DeleteFileA(strPath.c_str());
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(strDir.c_str());
}

int main()
{
    BSDebugSetFlags(0, 0);
    InitializeCriticalSection(&s_cs);
    char szTemp[MAX_PATH];
    GetTempPathA(MAX_PATH, szTemp);
    char szDir[MAX_PATH];
    sprintf_s(szDir, "%sMachineTest_%u", szTemp, GetCurrentProcessId());
    CreateDirectoryA(szDir, NULL);
    s_strDir = szDir;
    sprintf_s(s_szWnd, "MMTestWnd_%08u_X", GetCurrentProcessId());
    sprintf_s(s_szNoWnd, "MMTestNoWnd_%08u_X", GetCurrentProcessId());
    sprintf_s(s_szMissing, "MMTestMissing_%08u_X", GetCurrentProcessId());
    CopyChild(s_szWnd);
    CopyChild(s_szNoWnd);
    strcpy_s(g_ServerGlobals.szModuleDir, szDir);
    strcpy_s(g_ServerGlobals.szServiceName, "MachineManager");
    g_ServerGlobals.hInstance = GetModuleHandleA(NULL);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    g_ServerGlobals.dwProcessorCount = si.dwNumberOfProcessors;
    g_ServerGlobals.nMaxSendQueDepth = -1;
    g_ServerGlobals.bReportConnectionLog = TRUE;
    g_pServerApp = &g_MachineManager;

    TestConfig();
    TestPerfCounters();

    LogCapture_Start();
    TestStartup();

    CHECK(g_pBSNet->Listen("127.0.0.1", 0));
    sockaddr_in local;
    g_MachineManager.m_NetEngine.m_pListener->GetLocalAddr(&local);
    s_wPort = ntohs(local.sin_port);
    CHECK(g_pBSNet->RegisterProactor(TEST_TASK_CLIENT, NULL, &CTestClient::classCTestClient,
                                     NULL));
    CHECK(g_pBSNet->StartProactorWorkers(TEST_TASK_CLIENT, 1, 0, 0, 0, NULL));

    CLIENT gm = Connect();
    TestErrors(gm);
    TestStartStop(gm);
    KillModules();
    TestPerfStats(gm);
    TestShutdown(gm);
    CloseHandle(g_hServerIOCP);
    g_hServerIOCP = NULL;

    KillModules();
    RemoveTree(s_strDir);
    DeleteCriticalSection(&s_cs);
    return TEST_RESULT("MachineTest");
}
