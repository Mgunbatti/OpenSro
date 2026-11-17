// Server application layer: the application object in window mode on top of the real network
// engine, which connects to itself. Its two sessions are bound to two cords of a small
// architecture, so the framework packets (0x2001 introduction, 0x6008 relay, 0x6009 overlap jobs,
// 0x600D massive messages) travel through real sockets and the real server processes.

#include "BSLib/BSDebug.h"
#include "BSNet/NetEngine.h"
#include "BSNet/Session.h"
#include "BSNet/SockListener.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/CertData.h"
#include "ServerFramework/CrashDump.h"
#include "ServerFramework/GdiUtil.h"
#include "ServerFramework/MonitorGraph.h"
#include "ServerFramework/NTService.h"
#include "ServerFramework/OverlapJob.h"
#include "ServerFramework/ServerApp.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerFrameWindow.h"
#include "ServerFramework/ServerProcessMain.h"
#include "ServerFramework/ServerUiExtern.h"
#include "ServerFramework/Trigger.h"
#include "ServerFramework/WindowBase.h"

#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "TestCheck.h"
#include "LoopbackBind.h"

#pragma comment(lib, "comctl32.lib")

using namespace ServerFramework;

static HINSTANCE s_hInstance;
static bool s_bIsolated;
static std::string s_strTempDir;    // the module directory of the test
static CRITICAL_SECTION s_csEvents;

static void Isolate()
{
    HWINSTA hStation = CreateWindowStationA(NULL, 0, WINSTA_ALL_ACCESS, NULL);
    if (!hStation || !SetProcessWindowStation(hStation))
        return;
    HDESK hDesk = CreateDesktopA("ServerAppTest", NULL, NULL, 0, GENERIC_ALL, NULL);
    if (!hDesk || !SetThreadDesktop(hDesk))
        return;
    s_bIsolated = true;
}

static bool WaitFor(bool (*pfn)(), DWORD dwTimeout = 5000)
{
    DWORD dwStart = GetTickCount();
    while (!pfn())
    {
        if (GetTickCount() - dwStart > dwTimeout)
            return false;
        Sleep(10);
    }
    return true;
}

static std::string ReadTextFile(const std::string& strPath)
{
    std::string s;
    FILE* pFile;
    if (fopen_s(&pFile, strPath.c_str(), "rb"))
        return s;
    char sz[4096];
    size_t n;
    while ((n = fread(sz, 1, sizeof(sz), pFile)) > 0)
        s.append(sz, n);
    fclose(pFile);
    return s;
}

static std::vector<std::string> FindFiles(const std::string& strPattern)
{
    std::vector<std::string> v;
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(strPattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return v;
    do
    {
        v.push_back(fd.cFileName);
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    return v;
}

static void DeleteTree(const std::string& strDir)
{
    std::vector<std::string> v = FindFiles(strDir + "\\*");
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (v[i] == "." || v[i] == "..")
            continue;
        std::string strPath = strDir + "\\" + v[i];
        if (GetFileAttributesA(strPath.c_str()) & FILE_ATTRIBUTE_DIRECTORY)
            DeleteTree(strPath);
        else
            DeleteFileA(strPath.c_str());
    }
    RemoveDirectoryA(strDir.c_str());
}

// ---------------------------------------------------------------------------------------------
// Helpers without engine

static void TestHelpers()
{
    CHECK(strcmp(LogLevel_ToString(0x00000000), "notify") == 0);
    CHECK(strcmp(LogLevel_ToString(0x01000000), "warnning") == 0);
    CHECK(strcmp(LogLevel_ToString(0x02000001), "fatal") == 0);
    CHECK(strcmp(LogLevel_ToString(0x03FFFFFF), "unknown") == 0);
    CHECK(strcmp(LogLevel_ToString(0x04000000), "") == 0);

    CHECK(StrCmpI_Locale("GatewayServer", "gatewayserver") == 0);
    CHECK(StrCmpI_Locale("Agent", "Gateway") < 0 && StrCmpI_Locale("Gateway", "Agent") > 0);

    // FWAPP-003: bit subsets, not prefixes
    CHECK(Net_IsPrivateIPv4(0x0100000A));       // 10.0.0.1
    CHECK(Net_IsPrivateIPv4(0x0101A8C0));       // 192.168.1.1
    CHECK(Net_IsPrivateIPv4(0x010210AC));      // 172.16.2.1
    CHECK(Net_IsPrivateIPv4(0x01020308));       // 8.3.2.1 counts as private
    CHECK(!Net_IsPrivateIPv4(0x0100140B));      // 11.20.0.1
    CHECK(!Net_IsPrivateIPv4(0x0101A8C1));      // 193.168.1.1
    char szIP[16] = "";
    DWORD dwIP = 0;
    bool bFound = Net_GetPublicIPv4Address(szIP, &dwIP);
    CHECK(bFound == (dwIP != 0));
    if (bFound)
    {
        char szExpect[16];
        sprintf_s(szExpect, "%d.%d.%d.%d", dwIP & 0xFF, (dwIP >> 8) & 0xFF, (dwIP >> 16) & 0xFF,
                  dwIP >> 24);
        CHECK(strcmp(szIP, szExpect) == 0 && dwIP != 0x0100007F);
    }

    THREAD_AFFINITY affinity;
    ThreadAffinity_InitDefault(&affinity);
    CHECK(!affinity.bRandom && affinity.nMinCPU == -1 && affinity.nMaxCPU == -1);

    CHECK(Color_BlendPercent(RGB(0, 100, 200), RGB(200, 100, 0), 25) == RGB(50, 100, 150));
    CHECK(Color_BlendPercent(RGB(255, 255, 255), RGB(0, 0, 0), 0) == RGB(255, 255, 255));

    // the overlap job ID pool: FIFO of IDs, counters as the monitor graph reads them
    COverlapJobIDPool& pool = g_OverlapJobIDPool;
    CHECK(pool.m_dwMaxID == (DWORD)-1 && pool.m_dwGrowBy == 500);
    CHECK(pool.m_dwNextID == 501 && pool.m_deqFree.size() == 500 && pool.m_dwInUse == 0);
    CHECK(pool.m_dwStatNextID == 0 && pool.m_dwStatFree == 0);
    COverlapJob_InitializeLocalData* pJob = OverlapJob_AllocInitializeLocalData();
    CHECK(pJob->m_dwID == 1 && pool.m_dwInUse == 1 && pool.m_dwStatFree == 500);
    CHECK(g_OverlapJobInitLocalDataPool.m_nInUse == 1
          && g_OverlapJobInitLocalDataPool.m_nCapacity == 500);
    DWORD dw1, dw2, dw3;
    IdPool_GetUsageCounters(&dw1, &dw2, &dw3);
    CHECK(dw1 == 501 && dw2 == 1 && dw3 == 500);
    pJob->ReturnToPool();
    CHECK(pJob->m_dwID == 0 && pool.m_dwInUse == 0 && pool.m_deqFree.back() == 1);
    CHECK(g_OverlapJobInitLocalDataPool.m_nInUse == 0);
    pJob->ReturnToPool();       // without ID: not returned again
    CHECK(g_OverlapJobInitLocalDataPool.m_FreeQue.GetSize() == 500);
    // the free list runs dry: 500 more IDs
    std::vector<DWORD> vecIDs;
    for (int i = 0; i < 501; ++i)
        vecIDs.push_back(pool.Pop());
    CHECK(vecIDs[0] == 2 && vecIDs[498] == 500 && vecIDs[499] == 1 && vecIDs[500] == 501);
    CHECK(pool.m_dwNextID == 1001 && pool.m_dwInUse == 501);
    for (size_t i = 0; i < vecIDs.size(); ++i)
        pool.Push(vecIDs[i]);
    CHECK(pool.m_dwInUse == 0 && pool.m_dwStatFree == 1001);

    COverlapJob_RelayMsg* pRelay = OverlapJob_AllocRelayMsg();
    pRelay->Setup(NULL, 7, 0x7003, 2, 99, 0x1234);
    CHECK(pRelay->IsRelayJob() && pRelay->m_bCheckTimeout && !pRelay->m_bTimedOut);
    CHECK(pRelay->m_wTargetBody == 7 && pRelay->m_wOpcode == 0x7003 && pRelay->m_wSourceBody == 2);
    CHECK(pRelay->m_dwSourceJobID == 99 && pRelay->m_dwContext == 0x1234);
    CHECK(pRelay->m_dwTimeout == (DWORD)-1);
    pRelay->m_dwTimeout = 100;
    CHECK(!pRelay->CheckTimedOut(pRelay->m_dwStartTick + 100));
    CHECK(pRelay->CheckTimedOut(pRelay->m_dwStartTick + 101) && pRelay->m_bTimedOut);
    pRelay->ReturnToPool();
    CHECK(g_OverlapJobRelayMsgPool.m_nInUse == 0);
}

class CTriggerTarget
{
public:
    int OnFire(DWORD dw1, DWORD dw2)
    {
        m_dw1 = dw1;
        m_dw2 = dw2;
        return 5;
    }
    DWORD m_dw1;
    DWORD m_dw2;
};

static void TestTrigger()
{
    CTriggerTarget target;
    CClassTrigger<CTriggerTarget> trigger(&target);
    trigger.m_pfnTrigger = &CTriggerTarget::OnFire;
    CTrigger* p = &trigger;
    CHECK(p->OnTrigger(3, 4) == 5 && target.m_dw1 == 3 && target.m_dw2 == 4);
    C_ASSERT(sizeof(CClassTrigger<CTriggerTarget>) == 0x10);
}

static void TestServiceCommandLine()
{
    std::vector<char*> v;
    char sz1[] = "";
    CHECK(NTService_ParseCommandLine(sz1, &v) == NTSERVICE_CMD_DISPATCH && v.empty());
    char sz2[] = "   ";
    CHECK(NTService_ParseCommandLine(sz2, &v) == NTSERVICE_CMD_DISPATCH && v.empty());
    char sz3[] = "-INSTALL .\\user  secret";
    CHECK(NTService_ParseCommandLine(sz3, &v) == NTSERVICE_CMD_INSTALL && v.size() == 3);
    CHECK(strcmp(v[1], ".\\user") == 0 && strcmp(v[2], "secret") == 0);
    v.clear();
    char sz4[] = "-install NULL";
    CHECK(NTService_ParseCommandLine(sz4, &v) == NTSERVICE_CMD_INVALID && v.size() == 2);
    v.clear();
    char sz5[] = "-Uninstall extra";
    CHECK(NTService_ParseCommandLine(sz5, &v) == NTSERVICE_CMD_UNINSTALL);
    v.clear();
    char sz6[] = "-run";
    CHECK(NTService_ParseCommandLine(sz6, &v) == NTSERVICE_CMD_INVALID
          && strcmp(v[0], "-run") == 0);
    // the command line is split in place
    CHECK(sz3[8] == 0);
}

static int CaptureDump(EXCEPTION_POINTERS* pInfo)
{
    CHECK(CrashDump_TopLevelExceptionFilter(pInfo) == EXCEPTION_EXECUTE_HANDLER);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RaiseAndDump()
{
    __try
    {
        RaiseException(0xE0001234, 0, 0, NULL);
    }
    __except (CaptureDump(GetExceptionInformation()))
    {
    }
}

static std::string DumpTime(int nAddSeconds)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart += (ULONGLONG)nAddSeconds * 10000000;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    FILETIME ftLocal;
    FileTimeToLocalFileTime(&ft, &ftLocal);
    SYSTEMTIME st;
    FileTimeToSystemTime(&ftLocal, &st);
    char sz[64];
    sprintf_s(sz, "_[%04d-%02d-%02d %02d-%02d-%02d]", st.wYear, st.wMonth, st.wDay, st.wHour,
              st.wMinute, st.wSecond);
    return sz;
}

static void TestCrashDump()
{
    std::string strDir = s_strTempDir + "\\dump";
    CreateDirectoryA(strDir.c_str(), NULL);
    strcpy_s(g_ServerGlobals.szModuleDir, strDir.c_str());

    g_bCrashDumpWritten = 0;
    RaiseAndDump();
    CHECK(g_bCrashDumpWritten == 1);
    std::vector<std::string> v = FindFiles(strDir + "\\ServerAppTest_[*].dmp");
    CHECK(v.size() == 1);
    if (v.size() == 1)
    {
        WIN32_FILE_ATTRIBUTE_DATA fa;
        CHECK(GetFileAttributesExA((strDir + "\\" + v[0]).c_str(), GetFileExInfoStandard, &fa)
              && fa.nFileSizeLow > 1000);
        DeleteFileA((strDir + "\\" + v[0]).c_str());
    }
    // no error, no text file
    CHECK(FindFiles(s_strTempDir + "\\dumpServerAppTest_*").empty());

    // the dump file cannot be created (a directory has its name): the error text file is
    // created next to the module directory, without the '\' (FWAPP-002), and stays empty since
    // it could be opened (SRVFW-001)
    std::vector<std::string> vecBlock;
    for (int i = 0; i < 3; ++i)
    {
        vecBlock.push_back(strDir + "\\ServerAppTest" + DumpTime(i) + ".dmp");
        CreateDirectoryA(vecBlock.back().c_str(), NULL);
    }
    RaiseAndDump();
    v = FindFiles(s_strTempDir + "\\dumpServerAppTest_[*].txt");
    CHECK(v.size() == 1);
    if (v.size() == 1)
        CHECK(ReadTextFile(s_strTempDir + "\\" + v[0]).empty());
    for (size_t i = 0; i < vecBlock.size(); ++i)
        RemoveDirectoryA(vecBlock[i].c_str());
    // the error path leaves its file open
    _fcloseall();
    strcpy_s(g_ServerGlobals.szModuleDir, s_strTempDir.c_str());
}

static void TestAnchors()
{
    CHECK(CWindowBase::RegisterWindowClass(s_hInstance, "AnchorTest"));
    CWindowBase parent;
    RECT rc = { 0, 0, 400, 300 };
    CHECK(parent.CreateWin32Window(0, "AnchorTest", "", WS_OVERLAPPEDWINDOW, NULL, s_hInstance, rc,
                                   NULL));
    HWND hChild = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, 10, 20, 100, 50,
                                  parent.m_hWnd, NULL, s_hInstance, NULL);
    HWND hChild2 = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, 30, 40, 60, 70,
                                   parent.m_hWnd, NULL, s_hInstance, NULL);
    parent.AddAnchor(hChild, 1 | 2);
    parent.AddAnchor(hChild2, 2 | 8 | 0x20);
    CHECK(parent.m_listAnchor.size() == 2);
    MoveWindow(parent.m_hWnd, 0, 0, 500, 400, TRUE);
    parent.LayoutAnchors();
    RECT rcChild;
    GetWindowRect(hChild, &rcChild);
    MapWindowPoints(NULL, parent.m_hWnd, (POINT*)&rcChild, 2);
    // left and right distance kept: the parent grew by 100, so did the child; height kept
    CHECK(rcChild.right - rcChild.left == 200 && rcChild.bottom - rcChild.top == 50);
    // only the right edge (proportional) and bottom are anchored: the size is kept
    GetWindowRect(hChild2, &rcChild);
    MapWindowPoints(NULL, parent.m_hWnd, (POINT*)&rcChild, 2);
    CHECK(rcChild.right - rcChild.left == 60 && rcChild.bottom - rcChild.top == 70);
    std::string strText;
    SetWindowTextA(parent.m_hWnd, "anchored");
    CHECK(*parent.GetWindowTextString(&strText) == "anchored");
    parent.DestroyWindowBase();
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        ;
    UnregisterClassA("AnchorTest", s_hInstance);
}

// ---------------------------------------------------------------------------------------------
// The application

enum
{
    TEST_OP_REQUEST         = 0x7001,   // relayed request
    TEST_OP_ANSWER          = 0x7002,   // its answer
    TEST_OP_UNROUTABLE      = 0x7003,
    TEST_OP_MASSIVE         = 0x7005,
    TEST_OP_UNKNOWN         = 0x7777,
};

struct TEST_EVENTS
{
    std::vector<std::pair<DWORD, DWORD> > vecCreated;   // session, role
    std::vector<DWORD> vecHandshaked;
    std::vector<std::pair<DWORD, DWORD> > vecLost;      // session, reason
    std::vector<DWORD> vecCordEstablished;              // cord ID
    std::vector<DWORD> vecCordClosed;
    DWORD dwRequestRelayID;
    DWORD dwRequestSession;
    DWORD dwRequestValue;
    std::string strRequestText;
    DWORD dwAnswerRelayID;
    DWORD dwAnswerContext;
    DWORD dwAnswerValue;
    int nAnswers;
    WORD wFailedOpcode;
    DWORD dwFailedContext;
    int nMassive;
    int nMassiveSize;
    bool bMassiveOk;
    int nLocalDataInits;
    int nShutdowns;
    std::vector<int> vecStates;
};

static TEST_EVENTS s_Events;

class CTestMainProcess : public CServerProcessMain
{
    BS_DECLARE_DYNCREATE(CTestMainProcess)

public:
    virtual void InitMsgHandlers()
    {
        CServerProcessMain::InitMsgHandlers();
        RegisterHandler(TEST_OP_REQUEST, &CTestMainProcess::OnTestRequest);
        RegisterHandler(TEST_OP_ANSWER, &CTestMainProcess::OnTestAnswer);
        RegisterHandler(TEST_OP_MASSIVE, &CTestMainProcess::OnTestMassive);
    }

    virtual void OnSessionCreated(CMsg* pMsg, DWORD dw1, DWORD dw2, CMassiveMsg* pMassive)
    {
        DWORD dwSession = *(DWORD*)(pMsg->m_pData + MSG_HEADER_SIZE);
        DWORD dwRole = *(DWORD*)(pMsg->m_pData + MSG_HEADER_SIZE + 4);
        CServerProcessMain::OnSessionCreated(pMsg, dw1, dw2, pMassive);
        EnterCriticalSection(&s_csEvents);
        s_Events.vecCreated.push_back(std::make_pair(dwSession, dwRole));
        LeaveCriticalSection(&s_csEvents);
    }

    virtual void OnSessionHandshaked(CMsg* pMsg, DWORD dw1, DWORD dw2, CMassiveMsg* pMassive)
    {
        DWORD dwSession = *(DWORD*)(pMsg->m_pData + MSG_HEADER_SIZE);
        CServerProcessMain::OnSessionHandshaked(pMsg, dw1, dw2, pMassive);
        EnterCriticalSection(&s_csEvents);
        s_Events.vecHandshaked.push_back(dwSession);
        LeaveCriticalSection(&s_csEvents);
    }

    virtual void OnSessionLost(CMsg* pMsg, DWORD dw1, DWORD dw2, CMassiveMsg* pMassive)
    {
        DWORD dwSession = *(DWORD*)(pMsg->m_pData + MSG_HEADER_SIZE);
        DWORD dwReason = *(DWORD*)(pMsg->m_pData + MSG_HEADER_SIZE + 4);
        CServerProcessMain::OnSessionLost(pMsg, dw1, dw2, pMassive);
        EnterCriticalSection(&s_csEvents);
        s_Events.vecLost.push_back(std::make_pair(dwSession, dwReason));
        LeaveCriticalSection(&s_csEvents);
    }

    virtual void OnCordEstablished(SERVER_CORD* pCord, SERVER_BODY*)
    {
        EnterCriticalSection(&s_csEvents);
        s_Events.vecCordEstablished.push_back(pCord->dwID);
        LeaveCriticalSection(&s_csEvents);
    }

    virtual void OnCordClosed(SERVER_CORD* pCord, SERVER_BODY*)
    {
        EnterCriticalSection(&s_csEvents);
        s_Events.vecCordClosed.push_back(pCord->dwID);
        LeaveCriticalSection(&s_csEvents);
    }

    virtual void OnRelayFailed(WORD wOpcode, DWORD dwContext)
    {
        EnterCriticalSection(&s_csEvents);
        s_Events.wFailedOpcode = wOpcode;
        s_Events.dwFailedContext = dwContext;
        LeaveCriticalSection(&s_csEvents);
    }

    // Arrives relayed: answers through the relay.
    void OnTestRequest(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
    {
        DWORD dwValue;
        std::string strText;
        *pMsg >> dwValue >> strText;
        EnterCriticalSection(&s_csEvents);
        s_Events.dwRequestRelayID = dwRelayID;
        s_Events.dwRequestSession = pMsg->m_dwSessionID;
        s_Events.dwRequestValue = dwValue;
        s_Events.strRequestText = strText;
        LeaveCriticalSection(&s_csEvents);

        CMsg* pAnswer = g_pBSNet->AllocMsg(FALSE);
        pAnswer->SetOpcode(TEST_OP_ANSWER);
        *pAnswer << (DWORD)(dwValue + 1);
        SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pAnswer);
    }

    void OnTestAnswer(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext, CMassiveMsg*)
    {
        DWORD dwValue;
        *pMsg >> dwValue;
        EnterCriticalSection(&s_csEvents);
        s_Events.dwAnswerRelayID = dwRelayID;
        s_Events.dwAnswerContext = dwContext;
        s_Events.dwAnswerValue = dwValue;
        ++s_Events.nAnswers;
        LeaveCriticalSection(&s_csEvents);
    }

    void OnTestMassive(CMsg* pMsg, DWORD, DWORD, CMassiveMsg* pMassive)
    {
        CHECK(pMsg == NULL && pMassive != NULL);
        int nSize = pMassive->GetUnreadSize();
        bool bOk = true;
        for (int i = 0; i < nSize; ++i)
        {
            BYTE b;
            pMassive->Read(&b, 1);
            if (b != (BYTE)(i * 7))
                bOk = false;
        }
        EnterCriticalSection(&s_csEvents);
        ++s_Events.nMassive;
        s_Events.nMassiveSize = nSize;
        s_Events.bMassiveOk = bOk;
        LeaveCriticalSection(&s_csEvents);
    }
};

BS_IMPLEMENT_DYNCREATE(CTestMainProcess, &CServerProcessMain::classCServerProcessMain)

class CTestServerApp : public CServerApp
{
public:
    CTestServerApp() : m_hWorker(NULL) {}

    virtual bool RegisterMainProcessTask()
    {
        THREAD_AFFINITY affinity = { FALSE, 0, 0 };
        return g_pBSNet->RegisterProactor(SERVER_TASK_MAIN, NULL,
                                          &CTestMainProcess::classCTestMainProcess, NULL)
            && g_pBSNet->StartProactorWorkers(SERVER_TASK_MAIN, 1, 0, 0, 0, &affinity);
    }

    // The certification server is this process itself, which is not certified yet: its
    // listener closes the session. Then the test runs on a worker thread.
    virtual bool RequestServerCertification();

    virtual bool InitLocalData()
    {
        EnterCriticalSection(&s_csEvents);
        ++s_Events.nLocalDataInits;
        LeaveCriticalSection(&s_csEvents);
        return true;
    }

    virtual void SetServerState(int nState)
    {
        EnterCriticalSection(&s_csEvents);
        s_Events.vecStates.push_back(nState);
        LeaveCriticalSection(&s_csEvents);
        CServerApp::SetServerState(nState);
    }

    virtual bool Shutdown()
    {
        ++s_Events.nShutdowns;
        return CServerApp::Shutdown();
    }

    HANDLE  m_hWorker;
    u_short m_wPort;
};

static CTestServerApp* s_pApp;

static CServerProcessMain* MainProcess()
{
    return static_cast<CServerProcessMain*>(g_pBSNet->GetProactor(SERVER_TASK_MAIN)->GetJob());
}

// Text of every line in the server window's log list (read from this thread).
static std::vector<std::string> LogLines(int nColumn = 1)
{
    std::vector<std::string> v;
    HWND hList = g_pServerFrameWindow->m_hLogList;
    int n = (int)SendMessageA(hList, LVM_GETITEMCOUNT, 0, 0);
    for (int i = 0; i < n; ++i)
        v.push_back(ListView_GetItemTextString(hList, i, nColumn));
    return v;
}

static int s_nLogIndex;
static std::string s_strLogPrefix;

static bool HasLogLine()
{
    std::vector<std::string> v = LogLines();
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (v[i].compare(0, s_strLogPrefix.size(), s_strLogPrefix) == 0)
        {
            s_nLogIndex = (int)i;
            return true;
        }
    }
    return false;
}

static bool WaitLog(const std::string& strPrefix)
{
    s_strLogPrefix = strPrefix;
    return WaitFor(HasLogLine);
}

static size_t s_nWanted;

static bool EnoughCreated()
{
    EnterCriticalSection(&s_csEvents);
    bool b = s_Events.vecCreated.size() >= s_nWanted;
    LeaveCriticalSection(&s_csEvents);
    return b;
}

static bool EnoughLost()
{
    EnterCriticalSection(&s_csEvents);
    bool b = s_Events.vecLost.size() >= s_nWanted;
    LeaveCriticalSection(&s_csEvents);
    return b;
}

static bool EnoughCords()
{
    EnterCriticalSection(&s_csEvents);
    bool b = s_Events.vecCordEstablished.size() >= s_nWanted;
    LeaveCriticalSection(&s_csEvents);
    return b;
}

static bool EnoughClosedCords()
{
    EnterCriticalSection(&s_csEvents);
    bool b = s_Events.vecCordClosed.size() >= s_nWanted;
    LeaveCriticalSection(&s_csEvents);
    return b;
}

static bool AnswerArrived()
{
    EnterCriticalSection(&s_csEvents);
    bool b = s_Events.nAnswers > 0;
    LeaveCriticalSection(&s_csEvents);
    return b;
}

static bool RelayFailed()
{
    EnterCriticalSection(&s_csEvents);
    bool b = s_Events.wFailedOpcode != 0;
    LeaveCriticalSection(&s_csEvents);
    return b;
}

static bool MassiveArrived()
{
    EnterCriticalSection(&s_csEvents);
    bool b = s_Events.nMassive > 0;
    LeaveCriticalSection(&s_csEvents);
    return b;
}

static bool NoJobs()
{
    CServerProcessMain* p = MainProcess();
    CSingleLockBS lock(&p->m_OverlapJobManager.m_cs, TRUE);
    return p->m_OverlapJobManager.m_mapJob.empty();
}

// ---------------------------------------------------------------------------------------------
// The architecture: body 1 (this server) and body 2, both of module "ServerAppTest" on
// 127.0.0.1 with the listener's port; cord 10 from 1 to 2, cord 11 from 2 to 1. A connection to
// the own listener gives cord 10 its outgoing and cord 11 its incoming session.

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

template <class T>
static T Zero()
{
    T t;
    memset(&t, 0, sizeof(t));
    return t;
}

static void SetUpArchitecture(u_short wPort)
{
    std::vector<CERT_MODULE> modules(1, Zero<CERT_MODULE>());
    modules[0].byID = 1;
    strcpy_s(modules[0].szName, "ServerAppTest");
    std::vector<CERT_CONTENT> contents(1, Zero<CERT_CONTENT>());
    contents[0].byID = 3;
    std::vector<CERT_DIVISION> divisions(1, Zero<CERT_DIVISION>());
    divisions[0].byID = 1;
    std::vector<CERT_FARM> farms(1, Zero<CERT_FARM>());
    farms[0].byID = 1;
    farms[0].byDivisionID = 1;
    std::vector<CERT_FARM_CONTENT> farmContents(1, Zero<CERT_FARM_CONTENT>());
    farmContents[0].byFarmID = 1;
    farmContents[0].byContentID = 3;
    std::vector<CERT_SHARD> shards;
    std::vector<CERT_MACHINE> machines(1, Zero<CERT_MACHINE>());
    machines[0].dwID = 100;
    machines[0].byDivisionID = 1;
    strcpy_s(machines[0].szPublicIP, "127.0.0.1");
    strcpy_s(machines[0].szPrivateIP, "127.0.0.1");
    std::vector<SERVER_BODY> bodies(2, Zero<SERVER_BODY>());
    for (int i = 0; i < 2; ++i)
    {
        bodies[i].wID = (WORD)(i + 1);
        bodies[i].byDivisionID = 1;
        bodies[i].dwMachineID = 100;
        bodies[i].byModuleID = 1;
        bodies[i].wPort = wPort;
        bodies[i].nState = 4;
    }
    bodies[1].wParentID = 1;
    std::vector<SERVER_CORD> cords(2, Zero<SERVER_CORD>());
    cords[0].dwID = 10;
    cords[0].wFromID = 1;
    cords[0].wToID = 2;
    cords[1].dwID = 11;
    cords[1].wFromID = 2;
    cords[1].wToID = 1;

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
    CHECK(g_pSelfBody && g_pSelfBody->wID == 1 && g_pServerCordList->size() == 2);
}

// ---------------------------------------------------------------------------------------------
// Test scenario, on its own thread while the window mode runs

class CTestJob : public COverlapJob_Local
{
public:
    enum { MODE_OK, MODE_PREPARE_FAILS, MODE_EXECUTE_FAULTS };

    CTestJob(int nMode) : m_nMode(nMode), m_nResult(0), m_nReturned(0), m_dwExecThread(0) {}

    virtual void ReturnToPool()
    {
        ReleaseId();
        InterlockedIncrement(&m_nReturned);
    }
    virtual bool Prepare()
    {
        return m_nMode != MODE_PREPARE_FAILS;
    }
    virtual bool Execute()
    {
        m_dwExecThread = GetCurrentThreadId();
        if (m_nMode == MODE_EXECUTE_FAULTS)
            *(volatile int*)0 = 1;
        return true;
    }
    virtual bool OnComplete()
    {
        m_nResult = 1;
        return true;
    }
    virtual bool OnFail()
    {
        m_nResult = 2;
        return true;
    }

    int             m_nMode;
    volatile LONG   m_nResult;
    volatile LONG   m_nReturned;
    DWORD           m_dwExecThread;
};

static CTestJob* s_pWaitJob;

static bool JobReturned()
{
    return s_pWaitJob->m_nReturned != 0;
}

static void RunJob(CTestJob* pJob, int nExpectResult, LONG nExpectState)
{
    CServerProcessMain* pMain = MainProcess();
    pJob->AcquireId();
    pJob->Init((CTask*)pMain->m_pOwner);
    CHECK(pMain->SendOverlapJob(pJob, NULL));
    s_pWaitJob = pJob;
    CHECK(WaitFor(JobReturned));
    CHECK(pJob->m_nResult == nExpectResult && pJob->m_nState == nExpectState);
    CHECK(pJob->m_dwID == 0);
}

static bool InitLocalDataDone()
{
    EnterCriticalSection(&s_csEvents);
    bool b = s_Events.nLocalDataInits > 0;
    LeaveCriticalSection(&s_csEvents);
    return b && g_OverlapJobInitLocalDataPool.m_nInUse == 0;
}

static DWORD WINAPI ScenarioThread(LPVOID)
{
    CNetEngine* pEngine = &s_pApp->m_NetEngine;

    // 1. the uncertified connection: the accepting side closes it
    s_nWanted = 2;
    CHECK(WaitFor(EnoughCreated));
    s_nWanted = 2;
    CHECK(WaitFor(EnoughLost));
    CHECK(WaitLog("g_pServerBodyOfMyself IS NULL. Session Closed!"));
    CHECK(LogLines(0)[s_nLogIndex] == "warnning");
    CHECK(WaitLog("request server certification"));
    EnterCriticalSection(&s_csEvents);
    CHECK(s_Events.vecHandshaked.empty());
    CHECK(s_Events.vecStates.size() >= 2 && s_Events.vecStates[0] == 2
          && s_Events.vecStates[1] == 3);
    LeaveCriticalSection(&s_csEvents);
    CHECK(s_pApp->m_nPendingState == 3);

    // 2. certified: both sessions of a new connection become cords
    SetUpArchitecture(s_pApp->m_wPort);
    DWORD dwClient = pEngine->Connect("127.0.0.1", s_pApp->m_wPort, NULL, 0, 0);
    CHECK(dwClient != 0);
    s_nWanted = 2;
    CHECK(WaitFor(EnoughCords));
    SERVER_CORD* pOut = ServerCordMap_FindById(10);
    SERVER_CORD* pIn = ServerCordMap_FindById(11);
    CHECK(pOut && pOut->dwSessionID == dwClient && pOut->dwStatus == 2);
    CHECK(pIn && pIn->dwSessionID && pIn->dwSessionID != dwClient && pIn->dwStatus == 2);
    DWORD dwServer = pIn ? pIn->dwSessionID : 0;
    EnterCriticalSection(&s_csEvents);
    CHECK(s_Events.vecCreated.size() == 4 && s_Events.vecHandshaked.size() == 2);
    LeaveCriticalSection(&s_csEvents);
    Sleep(200);     // the status messages of the introduction settle

    // 3. relay: a request for body 1 routed out through cord 10 arrives on cord 11, is handled
    // there as this server's own and answered through the relay
    g_hmapServerRoute[1] = pOut;
    CMsg* pReq = g_pBSNet->AllocMsg(FALSE);
    pReq->SetOpcode(TEST_OP_REQUEST);
    *pReq << (DWORD)0x1234567 << "hello";
    InterlockedIncrement(&pReq->m_lRefCount);
    CHECK(RelayMsgToBody_6008(MainProcess(), pReq, 1, 0, 0, 0x55));
    CHECK(pReq->m_lRefCount == 1 && pReq->GetUnreadSize() == 0);
    g_pBSNet->ReleaseMsg(pReq);
    CHECK(WaitFor(AnswerArrived));
    EnterCriticalSection(&s_csEvents);
    CHECK(s_Events.dwRequestValue == 0x1234567 && s_Events.strRequestText == "hello");
    CHECK(s_Events.dwRequestSession == dwServer);
    CHECK(s_Events.dwAnswerRelayID == s_Events.dwRequestRelayID && s_Events.dwAnswerRelayID);
    CHECK(s_Events.dwAnswerContext == 0x55 && s_Events.dwAnswerValue == 0x1234568);
    LeaveCriticalSection(&s_csEvents);
    CHECK(WaitFor(NoJobs));
    CHECK(g_OverlapJobRelayMsgPool.m_nInUse == 0);
    // no route: nothing is sent, the message is released
    pReq = g_pBSNet->AllocMsg(FALSE);
    pReq->SetOpcode(TEST_OP_REQUEST);
    InterlockedIncrement(&pReq->m_lRefCount);
    CHECK(!RelayMsgToBody_6008(MainProcess(), pReq, 9, 0, 0, 0));
    CHECK(pReq->m_lRefCount == 1);
    g_pBSNet->ReleaseMsg(pReq);
    CHECK(NoJobs());

    // 4. a relay the other side cannot route: 0xA008 {2} makes the job fail
    COverlapJob_RelayMsg* pJob = OverlapJob_AllocRelayMsg();
    pJob->Setup((CTask*)MainProcess()->m_pOwner, 3, TEST_OP_UNROUTABLE, 0, 0, 0x66);
    MainProcess()->m_OverlapJobManager.Insert(pJob);
    CMsg* pRaw = g_pBSNet->AllocMsg(FALSE);
    pRaw->SetOpcode(SERVER_MSG_RELAY_REQ);
    *pRaw << pJob->m_dwID << (WORD)3 << (WORD)TEST_OP_UNROUTABLE << (DWORD)1;
    CHECK(g_pBSNet->SendMsg(dwClient, pRaw) == 0);
    g_pBSNet->ReleaseMsg(pRaw);
    CHECK(WaitFor(RelayFailed));
    CHECK(s_Events.wFailedOpcode == TEST_OP_UNROUTABLE && s_Events.dwFailedContext == 0x66);
    CHECK(WaitFor(NoJobs));
    CHECK(g_OverlapJobRelayMsgPool.m_nInUse == 0);

    // 5. local overlap jobs on the overlap process
    CTestJob jobOk(CTestJob::MODE_OK);
    RunJob(&jobOk, 1, OVERLAP_JOB_STATE_DONE);
    CTask* pOverlapTask = g_pBSNet->GetProactor(SERVER_TASK_OVERLAP);
    CHECK(MainProcess()->m_pOverlapTask == pOverlapTask);
    CHECK(jobOk.m_dwExecThread && jobOk.m_dwExecThread != GetCurrentThreadId());
    CTestJob jobPrepare(CTestJob::MODE_PREPARE_FAILS);
    RunJob(&jobPrepare, 2, OVERLAP_JOB_STATE_PREPARE);
    CTestJob jobFault(CTestJob::MODE_EXECUTE_FAULTS);
    g_bCrashDumpWritten = 0;
    RunJob(&jobFault, 2, OVERLAP_JOB_STATE_EXECUTE);
    CHECK(g_bCrashDumpWritten == 1);
    CHECK(WaitLog("local overlap job exception!! : CServerProcessOverlap"));
    CHECK(FindFiles(s_strTempDir + "\\ServerAppTest_[*].dmp").size() == 1);
    // 0x200C through the job the certification answer sends: on the overlap thread
    COverlapJob_InitializeLocalData* pInit = OverlapJob_AllocInitializeLocalData();
    pInit->Init((CTask*)MainProcess()->m_pOwner);
    CHECK(MainProcess()->SendOverlapJob(pInit, NULL));
    CHECK(WaitFor(InitLocalDataDone));
    CHECK(g_dwSelfDivisionID == 1);
    CHECK(WaitFor(NoJobs));

    // 6. massive message: parts reassembled by the receiving main process
    CMassiveMsg* pMassive = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pMassive->SetOpcode(TEST_OP_MASSIVE);
    for (int i = 0; i < 10000; ++i)
    {
        BYTE b = (BYTE)(i * 7);
        pMassive->Write(&b, 1);
    }
    pMassive->SendTo(dwClient, FALSE);
    CHECK(WaitFor(MassiveArrived));
    CHECK(s_Events.nMassiveSize == 10000 && s_Events.bMassiveOk);
    CHECK(MainProcess()->m_mapMassive.empty());

    // 7. unhandled packet, direct log lines, fatal log file
    CMsg* pUnknown = g_pBSNet->AllocMsg(FALSE);
    pUnknown->SetOpcode(TEST_OP_UNKNOWN);
    *pUnknown << (DWORD)1;
    g_pBSNet->SendMsg(dwClient, pUnknown);
    g_pBSNet->ReleaseMsg(pUnknown);
    CHECK(WaitLog("unhandled msg received CTestMainProcess : Req:0x777 (7), 4 - 4"));
    LogPrintf(0x2000001, "fatal line %d", 7);
    LogPrintf(0x3000000, "fatal file only");
    LogPrintf(0, "marker");
    CHECK(WaitLog("fatal line 7"));
    CHECK(LogLines(0)[s_nLogIndex] == "fatal");
    CHECK(WaitLog("marker"));
    SYSTEMTIME st;
    GetLocalTime(&st);
    char szFatal[64];
    sprintf_s(szFatal, "\\%04d-%02d-%02d_FatalLog.txt", st.wYear, st.wMonth, st.wDay);
    std::string strFatal = ReadTextFile(s_strTempDir + szFatal);
    // text mode: "\n" becomes "\r\n"
    CHECK(strFatal.find("\tfatal line 7\r\n") != std::string::npos);
    CHECK(strFatal.find("\tfatal file only\r\n") != std::string::npos);
    s_strLogPrefix = "fatal file only";
    CHECK(!HasLogLine());

    // 8. the client closes: both cords close
    g_pBSNet->DisconnectSession(dwClient, 1);
    s_nWanted = 2;
    CHECK(WaitFor(EnoughClosedCords));
    CHECK(pOut->dwSessionID == 0 && pIn->dwSessionID == 0);
    CHECK(g_mapSessionCord.empty());

    // 9. quit: the window closes and the application shuts down
    ServerApp_PostQuit();
    return 0;
}

bool CTestServerApp::RequestServerCertification()
{
    // listener of this process, which is the certification server of Server.cfg
    if (!g_pBSNet->Listen("127.0.0.1", 0))
        return false;
    sockaddr_in addr;
    m_NetEngine.m_pListener->GetLocalAddr(&addr);
    m_wPort = ntohs(addr.sin_port);
    strcpy_s(g_ServerGlobals.szCertificationIP, "127.0.0.1");
    g_ServerGlobals.nCertificationPort = m_wPort;
    if (!CServerApp::RequestServerCertification())
        return false;
    m_hWorker = CreateThread(NULL, 0, ScenarioThread, NULL, 0, NULL);
    return true;
}

static void TestWindowMode()
{
    g_hServerIOCP = NULL;
    g_hServerFrameWnd = NULL;
    g_ServerGlobals.bReportConnectionLog = TRUE;
    g_ServerGlobals.nMaxSendQueDepth = -1;
    CTestServerApp* pApp = new CTestServerApp;
    s_pApp = pApp;
    g_pServerApp = pApp;

    CHECK(ServerApp_RunWindowMode() == 1);

    // the scenario has posted the quit
    if (pApp->m_hWorker)
    {
        CHECK(WaitForSingleObject(pApp->m_hWorker, 0) == WAIT_OBJECT_0);
        CloseHandle(pApp->m_hWorker);
    }
    CHECK(s_Events.nShutdowns == 1);
    CHECK(s_Events.vecStates.back() == SERVER_STATE_STOPPED);
    CHECK(g_pBSNet == NULL && g_pServerFrameWindow == NULL);
    CHECK(g_pServerBodyList == NULL && g_mapSessionCord.empty());
    g_pSelfBody = NULL;
    CHECK(pApp->m_EngineParam.pfnLog == LogPrintf);
    CHECK(pApp->m_EngineParam.aSocket[0].dwSessionParam == SERVER_TASK_MAIN);
    CHECK(pApp->m_EngineParam.Maintainer.dwWaitTimeout == 1000);
    CHECK(pApp->m_EngineParam.dwFlags == 3 && pApp->m_EngineParam.nAcceptCount == 10);
    CHECK(g_OverlapJobRelayMsgPool.m_nInUse == 0 && g_OverlapJobInitLocalDataPool.m_nInUse == 0);
    CHECK(g_OverlapJobIDPool.m_dwInUse == 0);
    // FWAPP-009: the trigger of the deleted main process is still registered
    g_pServerBodyStateObserver = NULL;
    g_pServerApp = NULL;
    delete pApp;
    UnregisterClassA("ServerAppTest", s_hInstance);
    UnregisterClassA("ServerArchitectureView", s_hInstance);
    UnregisterClassA("ServerMoniterView", s_hInstance);
}

int main()
{
    CHECK(LoopbackBind_Install() == 1);
    BSDebugSetFlags(0, 0);
    InitializeCriticalSection(&s_csEvents);
    s_hInstance = GetModuleHandleA(NULL);

    char szTemp[MAX_PATH];
    GetTempPathA(MAX_PATH, szTemp);
    char szDir[MAX_PATH];
    sprintf_s(szDir, "%sServerAppTest_%u", szTemp, GetCurrentProcessId());
    s_strTempDir = szDir;
    CreateDirectoryA(szDir, NULL);
    strcpy_s(g_ServerGlobals.szModuleDir, szDir);
    strcpy_s(g_ServerGlobals.szServiceName, "ServerAppTest");
    g_ServerGlobals.hInstance = s_hInstance;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    g_ServerGlobals.dwProcessorCount = si.dwNumberOfProcessors;

    TestHelpers();
    TestTrigger();
    TestServiceCommandLine();
    TestCrashDump();

    Isolate();
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);
    if (s_bIsolated)
    {
        TestAnchors();
        TestWindowMode();
    }
    else
    {
        printf("ServerAppTest: window tests skipped (no own window station / desktop)\n");
    }

    _fcloseall();
    DeleteTree(s_strTempDir);
    DeleteCriticalSection(&s_csEvents);
    return TEST_RESULT("ServerAppTest");
}
