// FarmManager application: CFarmManager on the real network engine, which connects to itself. A
// client task of the same engine plays the GlobalManager; packets are built byte by byte and the
// answers compared byte by byte. The test links the FarmManager variants of the framework files
// (SERVER_MODULE_NAME "FarmManager", SERVER_MAIN_PROCESS_631D=0) and is compiled with the same
// defines. The UDP ping service binds the loopback address only (bind is redirected).

#include "BSLib/BSDebug.h"
#include "BSNet/NetEngine.h"
#include "BSNet/SockListener.h"
#include "BSNet/UdpPing.h"
#include "FarmManager/FarmManager.h"
#include "FarmManager/FarmManagerMainProcess.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/ModuleVersion.h"
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

#if SERVER_MAIN_PROCESS_631D
#error FarmTest is compiled with SERVER_MAIN_PROCESS_631D=0 (see the Makefile)
#endif

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
                    std::string* pstrLine = NULL, bool bReport = true)
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
            if (bReport)
                printf("  missing log line: %s\n", strPrefix.c_str());
            return false;
        }
        Sleep(5);
    }
}

// ---------------------------------------------------------------------------------------------
// The UDP ping service binds every address of its port: kept on the loopback address.

typedef int (WSAAPI* PFN_BIND)(SOCKET s, const sockaddr* pAddr, int nLen);
static PFN_BIND s_pfnBind;
static volatile LONG s_nUdpPingBinds;

static int WSAAPI LoopbackBind(SOCKET s, const sockaddr* pAddr, int nLen)
{
    sockaddr_in addr;
    if (pAddr && nLen == sizeof(addr) && pAddr->sa_family == AF_INET)
    {
        memcpy(&addr, pAddr, sizeof(addr));
        if (addr.sin_addr.s_addr == INADDR_ANY)
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int nResult = s_pfnBind(s, (const sockaddr*)&addr, nLen);
        if (nResult == 0 && addr.sin_port == htons(UDPPING_SERVER_PORT))
            InterlockedIncrement(&s_nUdpPingBinds);
        return nResult;
    }
    return s_pfnBind(s, pAddr, nLen);
}

static void HookBindToLoopback()
{
    s_pfnBind = (PFN_BIND)GetProcAddress(LoadLibraryA("ws2_32.dll"), "bind");
    BYTE* pBase = (BYTE*)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS* pNt = (IMAGE_NT_HEADERS*)(pBase + ((IMAGE_DOS_HEADER*)pBase)->e_lfanew);
    IMAGE_DATA_DIRECTORY& dir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    void** ppEntry = (void**)(pBase + dir.VirtualAddress);
    int nPatched = 0;
    for (DWORD i = 0; i < dir.Size / sizeof(void*); ++i)
    {
        if (ppEntry[i] != (void*)s_pfnBind)
            continue;
        DWORD dwOld;
        VirtualProtect(&ppEntry[i], sizeof(void*), PAGE_READWRITE, &dwOld);
        ppEntry[i] = (void*)LoopbackBind;
        VirtualProtect(&ppEntry[i], sizeof(void*), dwOld, &dwOld);
        ++nPatched;
    }
    CHECK(nPatched == 1);
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

static u_short s_wPort;

struct CLIENT
{
    DWORD dwClient;     // the client task's session
    DWORD dwServer;     // the farm manager's session of the same connection
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

static CFarmManagerMainProcess* Main()
{
    return static_cast<CFarmManagerMainProcess*>(
        g_pBSNet->GetProactor(SERVER_TASK_MAIN)->GetJob());
}

// ---------------------------------------------------------------------------------------------
// Architecture: body 1 is this farm manager, body 2 the GlobalManager.

enum
{
    MOD_FARM_MANAGER = 7, MOD_GLOBAL_MANAGER = 3, MOD_AGENT = 9,
};

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
    modules.push_back(Module(MOD_FARM_MANAGER, "FarmManager"));
    modules.push_back(Module(MOD_GLOBAL_MANAGER, "GlobalManager"));
    modules.push_back(Module(MOD_AGENT, "AgentServer"));
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
    bodies.push_back(Body(1, MOD_FARM_MANAGER, 15881, 3));
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

// The 2007 framework layout: no OnMsg_631D slot, OnCordEstablished in slot 40.
class CSlotProbe : public CServerProcessMain
{
public:
    virtual void OnCordEstablished(SERVER_CORD*, SERVER_BODY*)
    {
    }
};

static void TestConfig()
{
    CHECK(sizeof(CFarmManagerMainProcess) == 0x400DC);
    CHECK(CFarmManagerMainProcess::classCFarmManagerMainProcess.m_nObjectSize == 0x400DC);
    CHECK(strcmp(CFarmManagerMainProcess::classCFarmManagerMainProcess.m_lpszClassName,
                 "CFarmManagerMainProcess") == 0);
    CHECK(sizeof(CFarmManager) == 0x494);
    CHECK(g_FarmManager.m_strFileTransDir.empty());

    // the slot of OnCordEstablished: the vtable of a probe differs from the base's only there
    CServerProcessMain* pBase = new CServerProcessMain;
    CSlotProbe* pProbe = new CSlotProbe;
    void** pBaseVtbl = *(void***)pBase;
    void** pProbeVtbl = *(void***)pProbe;
    int nDiff = -1;
    int nCount = 0;
    for (int i = 2; i < 48; ++i)
    {
        if (pBaseVtbl[i] != pProbeVtbl[i])
        {
            nDiff = i;
            ++nCount;
        }
    }
    CHECK(nCount == 1 && nDiff == 40);
    delete pProbe;
    delete pBase;
    g_pServerBodyStateObserver = NULL;

    // Server.cfg: only the "FarmManager" block counts
    std::string strCfg = s_strDir + "\\Server.cfg";
    WriteDiskFile(strCfg,
                  "common { DumpMsgPool 0 }\r\n"
                  "GatewayServer { ReportConnectionLog 0 FARM_KEY 1 }\r\n"
                  "FarmManager { ReportConnectionLog 1 FARM_KEY 2 }\r\n");
    g_ServerGlobals.bReportConnectionLog = FALSE;
    CHECK(ServerConfig_Load());
    CHECK(g_ServerGlobals.bReportConnectionLog == TRUE);
    CHECK(g_ServerGlobals.mapExtra.size() == 1 && g_ServerGlobals.mapExtra["FARM_KEY"] == "2");
    g_ServerGlobals.mapExtra.clear();
    DeleteFileA(strCfg.c_str());

    // the module name of this build and the single-instance semaphore (skipped while a real
    // FarmManager runs on this machine)
    CHECK(ServerName_CompareCertification() != 0);
    HANDLE hOther = OpenSemaphoreA(SYNCHRONIZE, FALSE, "FarmManager");
    if (hOther)
    {
        CloseHandle(hOther);
        printf("  a FarmManager runs here: semaphore check skipped\n");
    }
    else
    {
        HANDLE hSemaphore = SingleInstance_CreateSemaphore();
        CHECK(hSemaphore != NULL);
        HANDLE hOpen = OpenSemaphoreA(SYNCHRONIZE, FALSE, "FarmManager");
        CHECK(hOpen != NULL);
        if (hOpen)
            CloseHandle(hOpen);
        SingleInstance_Close(&hSemaphore);
    }
}

static void TestStartup()
{
    CHECK(g_FarmManager.InitNetEngine());
    BSNET_INIT_PARAM& p = g_FarmManager.m_EngineParam;
    CHECK(p.bSecurityMode == 3);
    CHECK(p.dwFlags == 3 && p.nAcceptCount == 10 && p.dwMaxConnectTime == 60);
    CHECK(p.dwFileCacheLimit == 0 && p.nSendFileType == 1 && p.nRecvFileType == 1);
    CHECK(p.aSocket[1].nSendQueueLimit == -1 && p.aSocket[0].dwSessionParam == SERVER_TASK_MAIN);

    LONG nBindsBefore = s_nUdpPingBinds;
    CHECK(g_FarmManager.RegisterServerTasks());
    CHECK(Main()->GetRuntimeClass() == &CFarmManagerMainProcess::classCFarmManagerMainProcess);

    // Initialize opens the service, the message loop starts it and logs the result
    if (s_nUdpPingBinds == nBindsBefore + 1)
    {
        CHECK(WaitLog("UDP Ping Service was Started!", 0));
        CHECK(Main()->m_UdpPing.m_hThread != NULL && !Main()->m_UdpPing.m_bSuspended);
        SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in to;
        memset(&to, 0, sizeof(to));
        to.sin_family = AF_INET;
        to.sin_port = htons(UDPPING_SERVER_PORT);
        to.sin_addr.s_addr = inet_addr("127.0.0.1");
        CHECK(UdpPing_SendReq(s, (const sockaddr*)&to, 0x11, 0x22) == UDPPING_PACKET_SIZE);
        DWORD dwTimeout = 2000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&dwTimeout, sizeof(dwTimeout));
        char buf[32];
        CHECK(recv(s, buf, sizeof(buf), 0) == UDPPING_PACKET_SIZE);
        CHECK(memcmp(buf, "ACK", 4) == 0 && *(DWORD*)(buf + 8) == 0x11
              && *(DWORD*)(buf + 12) == 0x22);
        closesocket(s);
    }
    else
    {
        // the loopback port is taken by another program: the start fails
        printf("  UDP ping port in use: echo check skipped\n");
        CHECK(WaitLog("Failed To Start UDP Ping Service!", 0x2000000));
    }

    SetUpArchitecture();
    CHECK(g_FarmManager.OnLocalDataInitialized());
    CHECK(g_pSelfBody->nState == 5);
}

static Bytes RelayAck(DWORD dwRelayID, WORD wOpcode)
{
    Bytes v;
    PutB(v, 1);
    PutD(v, dwRelayID);
    PutW(v, wOpcode);
    return v;
}

static void TestHandlers(const CLIENT& gm)
{
    // 0x6300: the framework handler answers 0xA008 {1, 0, 0xA300, 1}
    SendPacket(gm.dwClient, 0x6300, Bytes());
    Bytes answer;
    CHECK(TakePacket(gm.dwClient, SERVER_MSG_RELAY_ACK, &answer));
    Bytes want = RelayAck(0, 0xA300);
    PutB(want, 1);
    CHECK(Same(answer, want));

    // 0x6303 {2}: CheckModeChange (slot 44 here) allows state 5 -> 4:
    // 0xA008 {1, 0, 0xA303, 1, 0, 2}
    Bytes data;
    PutB(data, 2);
    SendPacket(gm.dwClient, 0x6303, data);
    CHECK(TakePacket(gm.dwClient, SERVER_MSG_RELAY_ACK, &answer));
    want = RelayAck(0, 0xA303);
    PutB(want, 1);
    PutB(want, 0);
    PutB(want, 2);
    CHECK(Same(answer, want));
    CHECK(g_pSelfBody->nState == 4);
    // 0x6303 {2} again: refused with check 3, the state stays
    SendPacket(gm.dwClient, 0x6303, data);
    CHECK(TakePacket(gm.dwClient, SERVER_MSG_RELAY_ACK, &answer));
    want = RelayAck(0, 0xA303);
    PutB(want, 2);
    PutB(want, 3);
    PutB(want, 2);
    CHECK(Same(answer, want));
    CHECK(g_pSelfBody->nState == 4);

    // 0x631D is not handled by the 2007 framework: logged, no state 7
    PutB(data, 0x44);
    SendPacket(gm.dwClient, 0x631D, data);
    CHECK(WaitLog("unhandled msg received CFarmManagerMainProcess : ", 0x1000000));
    CHECK(g_pSelfBody->nState == 4);
}

static void TestCordEstablished(const CLIENT& gm)
{
    // some patch file data that the cord to the GlobalManager drops
    g_pModuleVersionList = new ModuleVersionList;
    g_pModuleVersionFileList = new ModuleVersionFileList;

    // a cord to another module: nothing
    SERVER_CORD cord = Zero<SERVER_CORD>();
    cord.dwSessionID = gm.dwServer;
    SERVER_BODY peer = Zero<SERVER_BODY>();
    peer.pModule = CertModule_FindByName("AgentServer");
    CHECK(peer.pModule != NULL);
    Main()->OnCordEstablished(&cord, &peer);
    CHECK(!TakePacket(gm.dwClient, FARM_MSG_PATCH_RESET, NULL, 200, false));
    CHECK(g_pModuleVersionList != NULL && g_pModuleVersionFileList != NULL);

    // the cord to the GlobalManager: the data is dropped and asked for with 0x6306 (no data)
    peer.pModule = CertModule_FindByName("GlobalManager");
    CHECK(peer.pModule != NULL);
    Main()->OnCordEstablished(&cord, &peer);
    Bytes reset;
    CHECK(TakePacket(gm.dwClient, FARM_MSG_PATCH_RESET, &reset));
    CHECK(reset.empty());
    CHECK(g_pModuleVersionList == NULL && g_pModuleVersionFileList == NULL);

    // the same through the vtable slot the framework calls
    CServerProcessMain* pMain = Main();
    pMain->OnCordEstablished(&cord, &peer);
    CHECK(TakePacket(gm.dwClient, FARM_MSG_PATCH_RESET, &reset));
}

static void TestShutdown(const CLIENT& gm)
{
    g_pBSNet->DisconnectSession(gm.dwClient, 1);
    CHECK(TakePacket(gm.dwClient, 0x1002, NULL));
    DrainLogs();
    CHECK(g_FarmManager.Shutdown());
    CHECK(g_pBSNet == NULL && g_pServerBodyList == NULL);
    g_pSelfBody = NULL;
    g_pServerBodyStateObserver = NULL;
}

int main()
{
    BSDebugSetFlags(0, 0);
    InitializeCriticalSection(&s_cs);
    HookBindToLoopback();
    char szTemp[MAX_PATH];
    GetTempPathA(MAX_PATH, szTemp);
    char szDir[MAX_PATH];
    sprintf_s(szDir, "%sFarmTest_%u", szTemp, GetCurrentProcessId());
    CreateDirectoryA(szDir, NULL);
    s_strDir = szDir;
    strcpy_s(g_ServerGlobals.szModuleDir, szDir);
    strcpy_s(g_ServerGlobals.szServiceName, "FarmManager");
    g_ServerGlobals.hInstance = GetModuleHandleA(NULL);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    g_ServerGlobals.dwProcessorCount = si.dwNumberOfProcessors;
    g_ServerGlobals.nMaxSendQueDepth = -1;
    g_pServerApp = &g_FarmManager;

    TestConfig();

    LogCapture_Start();
    TestStartup();

    CHECK(g_pBSNet->Listen("127.0.0.1", 0));
    sockaddr_in local;
    g_FarmManager.m_NetEngine.m_pListener->GetLocalAddr(&local);
    s_wPort = ntohs(local.sin_port);
    CHECK(g_pBSNet->RegisterProactor(TEST_TASK_CLIENT, NULL, &CTestClient::classCTestClient,
                                     NULL));
    CHECK(g_pBSNet->StartProactorWorkers(TEST_TASK_CLIENT, 1, 0, 0, 0, NULL));

    CLIENT gm = Connect();
    TestHandlers(gm);
    TestCordEstablished(gm);
    TestShutdown(gm);
    CloseHandle(g_hServerIOCP);
    g_hServerIOCP = NULL;

    RemoveDirectoryA(s_strDir.c_str());
    DeleteCriticalSection(&s_cs);
    return TEST_RESULT("FarmTest");
}
