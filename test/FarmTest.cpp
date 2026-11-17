// FarmManager application: CFarmManager on the real network engine, which connects to itself. A
// client task of the same engine plays the GlobalManager; packets are built byte by byte and the
// answers compared byte by byte. The test links the 2007 builds of the libraries
// (JMX_FRAMEWORK_2007, SERVER_MODULE_NAME "FarmManager") and is compiled with the same defines;
// it checks the 2007 behaviour of the framework (the other tests check the 2008 one). The UDP
// ping service binds the loopback address only (bind is redirected).

#include "BSLib/BSDebug.h"
#include "BSNet/NetEngine.h"
#include "BSNet/SockListener.h"
#include "BSNet/UdpPing.h"
#include "FarmManager/FarmManager.h"
#include "FarmManager/FarmManagerMainProcess.h"
#include "BSNet/MsgPool.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/LogQueue.h"
#include "ServerFramework/ServerUiExtern.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerFramework/ServerArchitectureView.h"
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

#if !defined(JMX_FRAMEWORK_2007) || SERVER_MAIN_PROCESS_631D
#error FarmTest is compiled with JMX_FRAMEWORK_2007 (see the Makefile)
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

// The data of the next massive message (0x600D header {1, WORD parts, WORD opcode}, then the
// parts {0, data}) with the inner opcode wOpcode on the session.
static bool TakeMassive(DWORD dwSession, WORD wOpcode, Bytes* pData)
{
    DWORD dwStart = GetTickCount();
    for (;;)
    {
        EnterCriticalSection(&s_cs);
        size_t nHeader = s_queIn.size();
        size_t nParts = 0;
        std::vector<size_t> vecParts;
        for (size_t i = 0; i < s_queIn.size(); ++i)
        {
            const PACKET& packet = s_queIn[i];
            if (packet.dwSession != dwSession || packet.wOpcode != MASSIVE_MSG_OPCODE
                || packet.data.empty())
            {
                continue;
            }
            if (nHeader == s_queIn.size())
            {
                if (packet.data[0] == 1 && packet.data.size() == 5
                    && *(const WORD*)&packet.data[3] == wOpcode)
                {
                    nHeader = i;
                    nParts = *(const WORD*)&packet.data[1];
                }
            }
            else if (packet.data[0] == 0 && vecParts.size() < nParts)
            {
                vecParts.push_back(i);
            }
            else if (packet.data[0] == 1)
            {
                break;
            }
        }
        if (nHeader != s_queIn.size() && vecParts.size() == nParts)
        {
            pData->clear();
            for (size_t i = 0; i < vecParts.size(); ++i)
            {
                const Bytes& part = s_queIn[vecParts[i]].data;
                pData->insert(pData->end(), part.begin() + 1, part.end());
            }
            // erase back to front so the indices stay valid
            for (size_t i = vecParts.size(); i > 0; --i)
                s_queIn.erase(s_queIn.begin() + vecParts[i - 1]);
            s_queIn.erase(s_queIn.begin() + nHeader);
            LeaveCriticalSection(&s_cs);
            return true;
        }
        LeaveCriticalSection(&s_cs);
        if (GetTickCount() - dwStart > 3000)
        {
            printf("  no massive 0x%04x on session %u\n", wOpcode, dwSession);
            return false;
        }
        Sleep(5);
    }
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
    bodies.push_back(Body(3, MOD_AGENT, 15883, 5));
    // the GlobalManager connects to this farm manager; the AgentServer is behind it
    std::vector<SERVER_CORD> cords(2, Zero<SERVER_CORD>());
    cords[0].dwID = 50;
    cords[0].wFromID = 2;
    cords[0].wToID = 1;
    cords[1].dwID = 51;
    cords[1].wFromID = 3;
    cords[1].wToID = 2;

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
    CHECK(ServerBody_Find(2) == ServerCordMap_FindById(50));
    CHECK(ServerBody_Find(3) == ServerCordMap_FindById(50));
}

// Records the body state changes (instead of the main process's trigger).
struct STATE_CHANGE
{
    WORD    wBody;
    int     nState;
    int     nOldState;
};

static std::vector<STATE_CHANGE> s_vecStates;

class CStateRecorder : public CTrigger
{
public:
    virtual int OnTrigger(DWORD dwParam1, DWORD dwParam2)
    {
        SERVER_BODY* pBody = (SERVER_BODY*)dwParam1;
        STATE_CHANGE change = { pBody->wID, pBody->nState, (int)dwParam2 };
        EnterCriticalSection(&s_cs);
        s_vecStates.push_back(change);
        LeaveCriticalSection(&s_cs);
        return 0;
    }

    virtual int OnTrigger2(DWORD, DWORD)
    {
        return 0;
    }
};

static CStateRecorder s_StateRecorder;

static size_t StateCount()
{
    EnterCriticalSection(&s_cs);
    size_t n = s_vecStates.size();
    LeaveCriticalSection(&s_cs);
    return n;
}

static bool WaitStates(size_t nCount)
{
    DWORD dwStart = GetTickCount();
    while (StateCount() < nCount)
    {
        if (GetTickCount() - dwStart > 3000)
            return false;
        Sleep(5);
    }
    return true;
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

    // 2007 framework: UDP ping port 15850 (0x459820), 8 state icons (0x441790)
    CHECK(UDPPING_SERVER_PORT == 15850);
    CHECK(ARCH_VIEW_ICON_COUNT == 8 && sizeof(CServerArchitectureView) == 0x180);

    // Net_IsPrivateIPv4 (0x40a4d0): 10/8 and 172.16/12 only (addresses in network order)
    CHECK(Net_IsPrivateIPv4(0x0403020A));
    CHECK(Net_IsPrivateIPv4(0x02011FAC));
    CHECK(!Net_IsPrivateIPv4(0x0201A8C0));
    CHECK(!Net_IsPrivateIPv4(0x0100007F));

    // LogLevel_ToString (0x409220): levels above 3 are "unknown"
    CHECK(strcmp(LogLevel_ToString(0x0000000), "notify") == 0);
    CHECK(strcmp(LogLevel_ToString(0x2000001), "fatal") == 0);
    CHECK(strcmp(LogLevel_ToString(0x4000000), "unknown") == 0);
    CHECK(strcmp(LogLevel_ToString(0xFF000000), "unknown") == 0);

    // LogPrintf (0x40a630): a 0x2800 buffer, so 0x27FF characters fit (without an engine the
    // line only goes to the thread trace); 0x2800 do not
    CHECK(g_pBSNet == NULL);
    std::string strLong(0x27FF, 'x');
    CHECK(LogPrintf(0, "%s", strLong.c_str()) == TRUE);
    strLong += 'y';
    CHECK(LogPrintf(0, "%s", strLong.c_str()) == FALSE);

    // Server.cfg: only the "FarmManager" block counts; there is no WritePerfLogToText option
    // (0x40a7b0), the key is kept like any unknown one
    std::string strCfg = s_strDir + "\\Server.cfg";
    WriteDiskFile(strCfg,
                  "common { DumpMsgPool 0 }\r\n"
                  "GatewayServer { ReportConnectionLog 0 FARM_KEY 1 }\r\n"
                  "FarmManager { ReportConnectionLog 1 FARM_KEY 2 WritePerfLogToText 1 }\r\n");
    g_ServerGlobals.bReportConnectionLog = FALSE;
    g_ServerGlobals.bWritePerfLogToText = FALSE;
    CHECK(ServerConfig_Load());
    CHECK(g_ServerGlobals.bReportConnectionLog == TRUE);
    CHECK(g_ServerGlobals.bWritePerfLogToText == FALSE);
    CHECK(g_ServerGlobals.mapExtra.size() == 2 && g_ServerGlobals.mapExtra["FARM_KEY"] == "2");
    CHECK(g_ServerGlobals.mapExtra["WritePerfLogToText"] == "1");
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

    // the message pool does not track the messages (0x4672b0 / 0x466220)
    CMsgPool* pPool = g_pBSNet->GetMsgPool();
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    CHECK(pMsg->m_lInUse == 1 && pPool->m_setInUse.empty());
    g_pBSNet->ReleaseMsg(pMsg);
    CHECK(pMsg->m_lInUse == 0 && pPool->m_setInUse.empty());
}

static std::vector<std::pair<DWORD, std::string> > s_vecShown;

static int __cdecl ShowLog(DWORD dwFlags, const char* pszText)
{
    s_vecShown.push_back(std::make_pair(dwFlags, std::string(pszText)));
    return 0;
}

static std::string ReadDiskFile(const std::string& strPath)
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

// OnControlMsg 0x200A (0x409b50): only 0x2000001 goes to the fatal log; every line is shown.
static void TestControlLog()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char szFatal[MAX_PATH];
    sprintf_s(szFatal, "%s\\%04d-%02d-%02d_FatalLog.txt", s_strDir.c_str(), st.wYear, st.wMonth,
              st.wDay);
    DeleteFileA(szFatal);
    PFN_SERVER_LOG pfnSaved = g_FarmManager.m_pfnLog;
    g_FarmManager.m_pfnLog = ShowLog;
    const DWORD adwFlags[3] = { 0x3000000, 0x2000001, 0 };
    const char* apszText[3] = { "perf line", "fatal line", "notify line" };
    for (int i = 0; i < 3; ++i)
    {
        CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
        pMsg->SetOpcode(SERVER_CONTROL_LOG);
        pMsg->WriteBytes(&adwFlags[i], 4);
        *pMsg << apszText[i];
        g_FarmManager.OnControlMsg(pMsg);
    }
    g_FarmManager.m_pfnLog = pfnSaved;
    CHECK(s_vecShown.size() == 3);
    for (size_t i = 0; i < s_vecShown.size() && i < 3; ++i)
        CHECK(s_vecShown[i].first == adwFlags[i] && s_vecShown[i].second == apszText[i]);
    std::string strFatal = ReadDiskFile(szFatal);
    CHECK(strFatal.find("\tfatal line\r\n") != std::string::npos);
    CHECK(strFatal.find("perf line") == std::string::npos);
    CHECK(strFatal.find("notify line") == std::string::npos);
    DeleteFileA(szFatal);
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

static Bytes Introduction(const char* pszModule)
{
    Bytes v;
    PutW(v, (WORD)strlen(pszModule));
    Put(v, pszModule, strlen(pszModule));
    PutB(v, 0);
    return v;
}

// The cord to the GlobalManager through the 2007 ServerCord_OnEstablished / OnClosed and the
// 0x6005 query of OnMsg_2001 (0x4134f0, 0x4138b0, 0x4029c0).
static void TestCords(const CLIENT& gm)
{
    SERVER_CORD* pCord = ServerCordMap_FindById(50);
    CHECK(pCord->dwSessionID == 0);

    // 0x2001 {"GlobalManager", 0} on the accepted session binds cord 50
    SendPacket(gm.dwClient, 0x2001, Introduction("GlobalManager"));
    std::string strLine;
    char szPrefix[160];
    sprintf_s(szPrefix, "server cord established : %u (127.0.0.1:", gm.dwServer);
    CHECK(WaitLog(szPrefix, 0, &strLine));
    CHECK(pCord->dwSessionID == gm.dwServer);
    Bytes data;
    CHECK(TakePacket(gm.dwClient, 0x2001, &data));
    CHECK(Same(data, Introduction("FarmManager")));
    CHECK(TakePacket(gm.dwClient, FARM_MSG_PATCH_RESET, &data));
    // this server's state (after a cord status record, if the session gets those)
    for (int i = 0; i < 3; ++i)
    {
        CHECK(TakeMassive(gm.dwClient, SERVER_STATUS_OPCODE, &data));
        if (data.empty() || data[0] == 1)
            break;
    }
    Bytes want;
    PutB(want, 1);
    PutB(want, 0);
    PutB(want, 1);
    PutW(want, 1);
    PutD(want, (DWORD)g_pSelfBody->nState);
    PutB(want, 2);
    CHECK(Same(data, want));
    // the query: bodies 2 and 3 (both routed through cord 50), then their cords
    CHECK(TakeMassive(gm.dwClient, 0x6005, &data));
    want.clear();
    PutB(want, 3);
    PutB(want, 0);
    PutB(want, 1);
    PutW(want, 2);
    PutB(want, 1);
    PutW(want, 3);
    PutB(want, 2);
    PutB(want, 0);
    PutB(want, 1);
    PutD(want, 50);
    PutB(want, 1);
    PutD(want, 51);
    PutB(want, 1);
    PutD(want, 51);
    PutB(want, 2);
    CHECK(Same(data, want));

    // a second GlobalManager session for the same cord: the NEW one is disconnected, the cord
    // keeps the first; FM-001: the cord is still treated as established (0x6306 again on the
    // first session)
    CLIENT second = Connect();
    SendPacket(second.dwClient, 0x2001, Introduction("GlobalManager"));
    sprintf_s(szPrefix, "server cord already established : created session overlapped : %u, "
              "127.0.0.1(", second.dwServer);
    CHECK(WaitLog(szPrefix, 0x1000000, &strLine));
    unsigned nPort = 0;
    CHECK(sscanf_s(strLine.c_str() + strlen(szPrefix), "%u)", &nPort) == 1 && nPort != 0);
    CHECK(TakePacket(second.dwClient, 0x1002, NULL));
    CHECK(pCord->dwSessionID == gm.dwServer);
    CHECK(g_mapSessionCord.count(gm.dwServer) == 1 && g_mapSessionCord.count(second.dwServer) == 0);
    CHECK(TakePacket(gm.dwClient, FARM_MSG_PATCH_RESET, NULL));

    // the cord closes: every body routed through it goes down (2 and 3, not only the peer)
    IServerBodyStateObserver* pSaved = g_pServerBodyStateObserver;
    s_vecStates.clear();
    g_pServerBodyStateObserver = &s_StateRecorder;
    g_pBSNet->DisconnectSession(gm.dwClient, 1);
    CHECK(TakePacket(gm.dwClient, 0x1002, NULL));
    sprintf_s(szPrefix, "server cord closed : %u", gm.dwServer);
    CHECK(WaitLog(szPrefix, 0));
    CHECK(WaitStates(2));
    EnterCriticalSection(&s_cs);
    CHECK(s_vecStates.size() == 2);
    if (s_vecStates.size() == 2)
    {
        CHECK(s_vecStates[0].wBody == 2 && s_vecStates[0].nState == 0
              && s_vecStates[0].nOldState == 5);
        CHECK(s_vecStates[1].wBody == 3 && s_vecStates[1].nState == 0
              && s_vecStates[1].nOldState == 5);
    }
    LeaveCriticalSection(&s_cs);
    CHECK(pCord->dwSessionID == 0 && pCord->dwStatus == 0);
    CHECK(ServerCordMap_FindById(51)->dwStatus == 0);
    CHECK(g_mapSessionCord.empty());
    g_pServerBodyStateObserver = pSaved;
}

// Shutdown (0x409a10) sets state 7.
static void TestShutdown()
{
    DrainLogs();
    s_vecStates.clear();
    g_pServerBodyStateObserver = &s_StateRecorder;
    CHECK(g_FarmManager.Shutdown());
    CHECK(g_pBSNet == NULL && g_pServerBodyList == NULL);
    CHECK(s_vecStates.size() >= 1);
    if (!s_vecStates.empty())
        CHECK(s_vecStates[0].wBody == 1 && s_vecStates[0].nState == 7);
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

    TestControlLog();
    CLIENT gm = Connect();
    TestHandlers(gm);
    TestCordEstablished(gm);
    TestCords(gm);
    TestShutdown();
    CloseHandle(g_hServerIOCP);
    g_hServerIOCP = NULL;

    RemoveDirectoryA(s_strDir.c_str());
    DeleteCriticalSection(&s_cs);
    return TEST_RESULT("FarmTest");
}
