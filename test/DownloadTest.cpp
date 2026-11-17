// DownloadServer application: CDownloadServer on the real network engine, which connects to
// itself. A client task of the same engine plays the GlobalManager (0x6305 patch relay, serving
// the patch files) and the game clients (0x6004 downloads); packets are built byte by byte and
// the transferred files are compared byte by byte. The test links the DownloadServer variants of
// the framework files that name the module (SERVER_MODULE_NAME "DownloadServer").

#include "BSLib/BSDebug.h"
#include "BSLib/file/IFile.h"
#include "BSNet/NetEngine.h"
#include "BSNet/SockListener.h"
#include "DownloadServer/DownloadServer.h"
#include "DownloadServer/DownloadServerMainProcess.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/CertData.h"
#include "ServerFramework/FileTransMgr.h"
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
#include "LoopbackBind.h"

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

static void WriteDiskFile(const std::string& strPath, const Bytes& data)
{
    FILE* pFile = NULL;
    fopen_s(&pFile, strPath.c_str(), "wb");
    CHECK(pFile != NULL);
    if (!pFile)
        return;
    fwrite(&data[0], 1, data.size(), pFile);
    fclose(pFile);
}

static Bytes ReadDiskFile(const std::string& strPath)
{
    Bytes data;
    FILE* pFile = NULL;
    fopen_s(&pFile, strPath.c_str(), "rb");
    if (!pFile)
        return data;
    BYTE buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), pFile)) > 0)
        data.insert(data.end(), buf, buf + n);
    fclose(pFile);
    return data;
}

static Bytes Pattern(size_t nSize, BYTE bSeed)
{
    Bytes data(nSize);
    for (size_t i = 0; i < nSize; ++i)
        data[i] = (BYTE)(bSeed + i * 7 + (i >> 8));
    return data;
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

// Takes the first packet of the session with the opcode.
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

// A message as a handler gets it from the session.
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
    DWORD dwServer;     // the download server's session of the same connection
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

static bool Closed(const CLIENT& c)
{
    return TakePacket(c.dwClient, 0x1002, NULL);
}

static CDownloadServerMainProcess* Main()
{
    return static_cast<CDownloadServerMainProcess*>(
        g_pBSNet->GetProactor(SERVER_TASK_MAIN)->GetJob());
}

static DWORD SessionsUsed()
{
    NETENGINE_STATISTICS st;
    g_pBSNet->GetStatistics(BSNET_STAT_SESSIONS, &st);
    return st.dwSessionUsed;
}

// ---------------------------------------------------------------------------------------------
// Architecture: body 1 is this download server, body 2 the GlobalManager.

enum
{
    MOD_DOWNLOAD = 11, MOD_GLOBAL_MANAGER = 3, MOD_CLIENT = 9,
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
    modules.push_back(Module(MOD_DOWNLOAD, "DownloadServer"));
    modules.push_back(Module(MOD_GLOBAL_MANAGER, "GlobalManager"));
    modules.push_back(Module(MOD_CLIENT, "SR_Client"));
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
    bodies.push_back(Body(1, MOD_DOWNLOAD, 15881, 3));
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
    // the object before start-up
    CHECK(g_DownloadServer.m_strFileTransDir == "download_files");
    CHECK(g_dwDownloadUserLimit == (DWORD)-1);
    CHECK(sizeof(CDownloadServerMainProcess) == 0x400CC);
    CHECK(CDownloadServerMainProcess::classCDownloadServerMainProcess.m_nObjectSize == 0x400CC);
    CHECK(strcmp(CDownloadServerMainProcess::classCDownloadServerMainProcess.m_lpszClassName,
                 "CDownloadServerMainProcess") == 0);

    // Server.cfg: the "DownloadServer" block counts, other modules' blocks are skipped
    std::string strCfg = s_strDir + "\\Server.cfg";
    const char szCfg[] =
        "common { DumpMsgPool 0 }\r\n"
        "GatewayServer { USER_LIMIT 9  OtherKey x }\r\n"
        "DownloadServer { USER_LIMIT 2 ReportConnectionLog 1 }\r\n";
    WriteDiskFile(strCfg, Bytes(szCfg, szCfg + sizeof(szCfg) - 1));
    CHECK(ServerConfig_Load());
    CHECK(g_ServerGlobals.bReportConnectionLog == TRUE);
    CHECK(strcmp(DownloadServer_FindUserLimitConfig(), "2") == 0);
    CHECK(g_ServerGlobals.mapExtra.find("OtherKey") == g_ServerGlobals.mapExtra.end());
    g_ServerGlobals.mapExtra.clear();
    CHECK(DownloadServer_FindUserLimitConfig() == NULL);
    g_ServerGlobals.mapExtra["user_limit"] = "5";
    CHECK(DownloadServer_FindUserLimitConfig() == NULL);
    g_ServerGlobals.mapExtra.clear();
    DeleteFileA(strCfg.c_str());

    // the setting lookup by name (DownloadServer's NameList4D2494_FindIdByName)
    CertDataSettingList settings;
    CERT_DATA_SETTING aSetting[2];
    memset(aSetting, 0, sizeof(aSetting));
    aSetting[0].dwValue = 7;
    strcpy_s(aSetting[0].szName, "Ignore_Shard_MaxUser");
    aSetting[1].dwValue = 9;
    strcpy_s(aSetting[1].szName, "Other");
    settings.push_back(&aSetting[0]);
    settings.push_back(&aSetting[1]);
    CertDataSettingList* pOldSettings = g_pCertDataSettingList;
    g_pCertDataSettingList = &settings;
    CHECK(CertData_GetSettingValue("other") == 9);
    CHECK(CertData_GetSettingValue("Missing") == 0);
    CHECK(CertData_GetIgnoreShardMaxUserValue() == 7);
    g_pCertDataSettingList = pOldSettings;

    // the module name of this build
    CHECK(ServerName_CompareCertification() != 0);
    HANDLE hSemaphore = SingleInstance_CreateSemaphore();
    CHECK(hSemaphore != NULL);
    HANDLE hOpen = OpenSemaphoreA(SYNCHRONIZE, FALSE, "DownloadServer");
    CHECK(hOpen != NULL);
    CHECK(SingleInstance_CreateSemaphore() == NULL);
    if (hOpen)
        CloseHandle(hOpen);
    SingleInstance_Close(&hSemaphore);
}

static void TestStartup()
{
    CHECK(g_DownloadServer.InitNetEngine());
    BSNET_INIT_PARAM& p = g_DownloadServer.m_EngineParam;
    MEMORYSTATUS status;
    GlobalMemoryStatus(&status);
    CHECK(p.dwFlags == 0x33 && p.nAcceptCount == 100 && p.dwMaxConnectTime == 60);
    CHECK(p.dwFileCacheLimit == (DWORD)(status.dwTotalPhys >> 2));
    CHECK(p.nSendFileType == 2 && p.nRecvFileType == 1 && p.bSecurityMode == 3);
    CHECK(p.aSocket[1].nSendQueueLimit == -1 && p.aSocket[0].dwSessionParam == SERVER_TASK_MAIN);
    // OnEngineConfig after the framework's maintainer settings
    CHECK(p.Maintainer.aJobs[0].dwPeriod == 300000 && p.Maintainer.aJobs[0].dwParam == 300000);
    CHECK(p.Maintainer.aJobs[2].dwPeriod == 1000);
    CHECK(g_DownloadServer.RegisterServerTasks());
    CHECK(Main()->GetRuntimeClass()
          == &CDownloadServerMainProcess::classCDownloadServerMainProcess);
    CHECK(g_strFileTransDir == "download_files");
    CHECK(g_FileTransMgr.m_dwFlags == (FILETRANS_FLAG_SEND | FILETRANS_FLAG_RECV));

    SetUpArchitecture();

    // without USER_LIMIT: unlimited, state 5
    CHECK(g_DownloadServer.OnLocalDataInitialized());
    CHECK(WaitLog("Initializing Local Data...", 0));
    CHECK(WaitLog("Max Concurrent User Count: Unlimited", 0x2000000));
    CHECK(g_pSelfBody->nState == 5);
    CHECK(g_dwDownloadUserLimit == (DWORD)-1);
    CHECK(g_DownloadServer.InitMonitorCounters());
}

static void TestUserLimit()
{
    // no session yet: limit 0 is not exceeded, the base handler reads the data (session 0:
    // DisconnectSession ignores it)
    CHECK(SessionsUsed() == 0);
    Bytes data;
    PutD(data, 0);
    PutD(data, 0);
    g_dwDownloadUserLimit = 0;
    CMsg* pMsg = MakeMsg(0, 0x1001, data);
    Main()->OnSessionCreated(pMsg, 0, 0, NULL);
    CHECK(pMsg->GetUnreadSize() == 0 && pMsg->m_dw4160 == 0);
    g_pBSNet->ReleaseMsg(pMsg);
    data.clear();
    PutD(data, 0);
    PutD(data, 1);
    pMsg = MakeMsg(0, 0x1001, data);
    Main()->OnSessionCreated(pMsg, 0, 0, NULL);
    CHECK(pMsg->GetUnreadSize() == 0 && pMsg->m_dw4160 == 0);
    g_pBSNet->ReleaseMsg(pMsg);

    // USER_LIMIT from Server.cfg
    DWORD dwUsed = SessionsUsed();
    char sz[16];
    sprintf_s(sz, "%u", dwUsed + 1);
    g_ServerGlobals.mapExtra["USER_LIMIT"] = sz;
    CHECK(g_DownloadServer.OnLocalDataInitialized());
    CHECK(g_dwDownloadUserLimit == dwUsed + 1);
    char szLine[64];
    sprintf_s(szLine, "Max Concurrent User Count Overrided: %u", dwUsed + 1);
    CHECK(WaitLog(szLine, 0x2000000));

    // the accepted session is the second one of the connection: over the limit, closed
    DrainLogs();
    size_t nLog = s_vecLog.size();
    DWORD dwClient = g_pBSNet->Connect("127.0.0.1", s_wPort, NULL, TEST_TASK_CLIENT, 0);
    CHECK(dwClient != 0);
    CHECK(TakePacket(dwClient, 0x1001, NULL));
    CHECK(TakePacket(dwClient, 0x1002, NULL));
    CHECK(!TakePacket(dwClient, 0x1003, NULL, 100, false));
    CHECK(!HasLogSince(nLog, "session created ["));
    // DL-002: the refused session's 0x1001 is left unread
    CHECK(WaitLog("msg is not completely used : ", 0x1000000));

    // one more allowed: the connection is set up
    g_dwDownloadUserLimit = SessionsUsed() + 2;
    CLIENT c = Connect();

    // over the limit the 0x1001 data is only peeked: read position and CMsg +4160 restored
    g_dwDownloadUserLimit = 1;
    data.clear();
    PutD(data, 0);
    PutD(data, 0);
    pMsg = MakeMsg(0, 0x1001, data);
    Main()->OnSessionCreated(pMsg, 0, 0, NULL);
    CHECK(pMsg->GetUnreadSize() == 8 && pMsg->m_dw4160 == 0);
    g_pBSNet->ReleaseMsg(pMsg);
    // a connected session (role 1) is never refused
    data.clear();
    PutD(data, 0);
    PutD(data, 1);
    pMsg = MakeMsg(0, 0x1001, data);
    Main()->OnSessionCreated(pMsg, 0, 0, NULL);
    CHECK(pMsg->GetUnreadSize() == 0 && pMsg->m_dw4160 == 0);
    g_pBSNet->ReleaseMsg(pMsg);
    g_pBSNet->DisconnectSession(c.dwClient, 1);
    CHECK(Closed(c));

    g_ServerGlobals.mapExtra.clear();
    g_dwDownloadUserLimit = (DWORD)-1;
}

// The patch files of the relay: module 9, byte 9 = 4, so the directory is "9_4".
struct PATCH_FILE
{
    DWORD       dwID;
    const char* pszName;
    const char* pszPath;
    Bytes       data;
};

static std::vector<PATCH_FILE> s_vecPatch;

static MODULE_VERSION_FILE Record(const PATCH_FILE& f)
{
    MODULE_VERSION_FILE r = Zero<MODULE_VERSION_FILE>();
    r.dwID = f.dwID;
    r.dwVersion = 301;
    r.ab8[1] = 4;
    r.byModuleID = MOD_CLIENT;
    strcpy_s(r.szName, f.pszName);
    strcpy_s(r.szPath, f.pszPath);
    r.dwSize = (DWORD)f.data.size();
    r.byValid = 1;
    return r;
}

// Sends a massive message as the framework does: header {1, WORD parts, WORD opcode}, then
// parts {0, data}.
static void SendMassive(DWORD dwSession, WORD wOpcode, const Bytes& data)
{
    const size_t nPart = 3000;
    WORD wParts = (WORD)((data.size() + nPart - 1) / nPart);
    if (!wParts)
        wParts = 1;
    Bytes header;
    PutB(header, 1);
    PutW(header, wParts);
    PutW(header, wOpcode);
    SendPacket(dwSession, 0x600D, header);
    for (WORD i = 0; i < wParts; ++i)
    {
        Bytes part;
        PutB(part, 0);
        size_t nFrom = i * nPart;
        size_t nTo = nFrom + nPart < data.size() ? nFrom + nPart : data.size();
        if (nTo > nFrom)
            part.insert(part.end(), data.begin() + nFrom, data.begin() + nTo);
        SendPacket(dwSession, 0x600D, part);
    }
}

// The IFile* of a 0x1005 event {DWORD session, DWORD file}.
static IFile* FileOf(const Bytes& event)
{
    return (IFile*)(DWORD_PTR)*(const DWORD*)&event[4];
}

static size_t s_nQueueWanted;

static bool RecvQueueAt()
{
    return g_FileTransMgr.m_RecvQueue.size() == s_nQueueWanted;
}

// The GlobalManager side: serves the requested file and confirms it with 0xA004 {1}.
static void ServeRequest(const CLIENT& gm, const PATCH_FILE& f, size_t nLeft)
{
    Bytes req;
    CHECK(TakePacket(gm.dwClient, FILETRANS_OPCODE_REQ, &req));
    Bytes want;
    PutD(want, f.dwID);
    PutD(want, 0);
    CHECK(Same(req, want));
    std::string strSrc = s_strDir + "\\src_" + f.pszName;
    CHECK(g_pBSNet->SendFile(gm.dwClient, strSrc, 0, -1) == TRUE);
    Bytes done;
    CHECK(TakePacket(gm.dwClient, 0x1005, &done));
    if (done.size() == 8)
        g_pBSNet->CloseSessionTransferFile(0, FileOf(done)->GetFileHandle());
    s_nQueueWanted = nLeft;
    CHECK(WaitFor(RecvQueueAt));
    Bytes ack;
    PutB(ack, 1);
    SendPacket(gm.dwClient, FILETRANS_OPCODE_ACK, ack);
}

static void TestPatchRelay(const CLIENT& gm)
{
    PATCH_FILE a = { 7001, "a.txt", "data", Pattern(5000, 3) };
    PATCH_FILE b = { 7002, "b.bin", "", Pattern(70000, 11) };
    s_vecPatch.push_back(a);
    s_vecPatch.push_back(b);
    for (size_t i = 0; i < s_vecPatch.size(); ++i)
        WriteDiskFile(s_strDir + "\\src_" + s_vecPatch[i].pszName, s_vecPatch[i].data);

    Bytes data;
    PutW(data, 2);
    for (size_t i = 0; i < s_vecPatch.size(); ++i)
    {
        MODULE_VERSION_FILE r = Record(s_vecPatch[i]);
        Put(data, &r, sizeof(r));
    }
    CHECK(data.size() == 2 + 2 * 550);
    SendMassive(gm.dwClient, DOWNLOAD_MSG_PATCH_RELAY_REQ, data);

    Bytes ack;
    CHECK(TakePacket(gm.dwClient, DOWNLOAD_MSG_PATCH_RELAY_ACK, &ack));
    CHECK(Same(ack, Bytes(1, 1)));
    CHECK(WaitLog("start patch relay : 2", 0));
    CHECK(Main()->m_wPatchFileCount == 2);
    CHECK(g_pModuleVersionFileList && g_pModuleVersionFileList->size() == 2);
    CHECK(g_pModuleVersionList && g_pModuleVersionList->empty());
    CHECK(g_FileTransMgr.m_dwServerSession == gm.dwServer);

    ServeRequest(gm, s_vecPatch[0], 1);
    ServeRequest(gm, s_vecPatch[1], 0);
    CHECK(!TakePacket(gm.dwClient, FILETRANS_OPCODE_REQ, NULL, 200, false));

    std::string strBase = s_strDir + "\\download_files\\9_4\\";
    CHECK(Same(ReadDiskFile(strBase + "data\\a.txt"), s_vecPatch[0].data));
    CHECK(Same(ReadDiskFile(strBase + "b.bin"), s_vecPatch[1].data));
}

// A game client downloads a patch file with 0x6004 {DWORD ID, DWORD offset}.
static void TestClientDownload()
{
    CLIENT player = Connect();
    for (size_t i = 0; i < s_vecPatch.size(); ++i)
    {
        const PATCH_FILE& f = s_vecPatch[i];
        std::string strOut = s_strDir + "\\dl_" + f.pszName;
        CHECK(g_pBSNet->BeginSessionFileTransfer(player.dwClient, strOut, (DWORD)f.data.size(),
                                                 0, -1) == TRUE);
        Bytes req;
        PutD(req, f.dwID);
        PutD(req, 0);
        SendPacket(player.dwClient, FILETRANS_OPCODE_REQ, req);
        Bytes done;
        CHECK(TakePacket(player.dwClient, 0x1005, &done, 5000));
        if (done.size() == 8)
            g_pBSNet->CloseSessionTransferFile(0, FileOf(done)->GetFileHandle());
        Bytes ack;
        CHECK(TakePacket(player.dwClient, FILETRANS_OPCODE_ACK, &ack));
        CHECK(Same(ack, Bytes(1, 1)));
        CHECK(Same(ReadDiskFile(strOut), f.data));
        DeleteFileA(strOut.c_str());
    }
    // an unknown file
    Bytes req;
    PutD(req, 9999);
    PutD(req, 0);
    SendPacket(player.dwClient, FILETRANS_OPCODE_REQ, req);
    Bytes ack;
    CHECK(TakePacket(player.dwClient, FILETRANS_OPCODE_ACK, &ack));
    CHECK(Same(ack, Bytes(1, 0)));
    CHECK(WaitLog("client requested to transfer abnormal file : 9999", 0x2000000));
    g_pBSNet->DisconnectSession(player.dwClient, 1);
    CHECK(Closed(player));
}

static void TestServerPackets(const CLIENT& gm)
{
    // DL-001: an empty relay list; the subdirectory name is undefined and the fetch asserts
    Bytes data;
    PutW(data, 0);
    SendMassive(gm.dwClient, DOWNLOAD_MSG_PATCH_RELAY_REQ, data);
    Bytes ack;
    CHECK(TakePacket(gm.dwClient, DOWNLOAD_MSG_PATCH_RELAY_ACK, &ack));
    CHECK(Same(ack, Bytes(1, 1)));
    CHECK(WaitLog("start patch relay : 0", 0));
    CHECK(Main()->m_wPatchFileCount == 0);
    CHECK(g_pModuleVersionFileList && g_pModuleVersionFileList->empty());
    CHECK(g_FileTransMgr.m_RecvQueue.empty() && g_FileTransMgr.m_dwServerSession == 0);
    CHECK(!TakePacket(gm.dwClient, FILETRANS_OPCODE_REQ, NULL, 200, false));

    // 0xA306 from the DivisionManager
    MODULE_VERSION version = Zero<MODULE_VERSION>();
    version.byModuleID = MOD_CLIENT;
    version.dwVersion = 400;
    MODULE_VERSION_FILE file = Record(s_vecPatch[0]);
    CMassiveMsg* p = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    p->SetOpcode(DOWNLOAD_MSG_MODULE_VERSION_ACK);
    BYTE b = 1;
    p->Write(&b, 1);
    p->Write(&b, 1);
    p->Write(&version, sizeof(version));
    b = 0;
    p->Write(&b, 1);
    b = 1;
    p->Write(&b, 1);
    p->Write(&file, sizeof(file));
    b = 0;
    p->Write(&b, 1);
    p->BeginRead();
    Main()->OnMsg_A306(NULL, 0, 0, p);
    p->Free();
    CHECK(WaitLog("retrieve module version data from divisionmanager", 0));
    CHECK(ModuleVersion_GetLatest(MOD_CLIENT) == 400);
    CHECK(g_pModuleVersionFileList->size() == 1);
    p = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    b = 2;
    p->Write(&b, 1);
    p->BeginRead();
    Main()->OnMsg_A306(NULL, 0, 0, p);
    p->Free();
    CHECK(WaitLog("cannot retrieve module version data from divisionmanager", 0x2000000));
    CHECK(ModuleVersion_GetLatest(MOD_CLIENT) == 400);

    // a cord to another module: nothing
    SERVER_CORD cord = Zero<SERVER_CORD>();
    cord.dwSessionID = gm.dwServer;
    SERVER_BODY peer = Zero<SERVER_BODY>();
    peer.pModule = CertModule_FindByName("SR_Client");
    Main()->OnCordEstablished(&cord, &peer);
    CHECK(!TakePacket(gm.dwClient, DOWNLOAD_MSG_PATCH_RESET, NULL, 200, false));
    CHECK(g_pModuleVersionList != NULL);
    // the cord to the GlobalManager: the data is dropped and asked for with 0x6306
    peer.pModule = CertModule_FindByName("GlobalManager");
    CHECK(peer.pModule != NULL);
    Main()->OnCordEstablished(&cord, &peer);
    Bytes reset;
    CHECK(TakePacket(gm.dwClient, DOWNLOAD_MSG_PATCH_RESET, &reset));
    CHECK(reset.empty());
    CHECK(g_pModuleVersionList == NULL && g_pModuleVersionFileList == NULL);

    // 0x6308 (relayed): the client sessions and the files are closed, 0xA309 {1}
    CLIENT player = Connect();
    std::string strOut = s_strDir + "\\open.bin";
    CHECK(g_pBSNet->BeginSessionFileTransfer(gm.dwClient, strOut, 100, 0, -1) == TRUE);
    NETENGINE_STATISTICS st;
    g_pBSNet->GetStatistics(BSNET_STAT_FILES, &st);
    CHECK(st.dwOpenFiles > 0);
    g_pBSNet->CloseSessionTransferFile(gm.dwClient, 0);
    g_pBSNet->ExcludeSessionFromCleanup(gm.dwServer);
    g_pBSNet->ExcludeSessionFromCleanup(gm.dwClient);
    CMsg* pMsg = MakeMsg(gm.dwServer, DOWNLOAD_MSG_PATCH_DONE_REQ, Bytes());
    Main()->OnMsg_6308(pMsg, 0x55, 0, NULL);
    g_pBSNet->ReleaseMsg(pMsg);
    Bytes answer;
    CHECK(TakePacket(gm.dwClient, SERVER_MSG_RELAY_ACK, &answer));
    Bytes want;
    PutB(want, 1);
    PutD(want, 0x55);
    PutW(want, DOWNLOAD_MSG_PATCH_DONE_ACK);
    PutB(want, 1);
    CHECK(Same(answer, want));
    CHECK(WaitLog("preparing patch is completed !!!", 0));
    CHECK(Closed(player));
    g_pBSNet->GetStatistics(BSNET_STAT_FILES, &st);
    CHECK(st.dwOpenFiles == 0);
    DeleteFileA(strOut.c_str());
}

static void TestShutdown(const CLIENT& gm)
{
    g_pBSNet->DisconnectSession(gm.dwClient, 1);
    CHECK(Closed(gm));
    DrainLogs();
    CHECK(g_DownloadServer.Shutdown());
    CHECK(g_pBSNet == NULL && g_pServerBodyList == NULL);
    CHECK(g_FileTransMgr.m_RecvQueue.empty() && g_FileTransMgr.m_dwFlags == 0);
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
    CHECK(LoopbackBind_Install() == 1);
    BSDebugSetFlags(0, 0);
    InitializeCriticalSection(&s_cs);
    char szTemp[MAX_PATH];
    GetTempPathA(MAX_PATH, szTemp);
    char szDir[MAX_PATH];
    sprintf_s(szDir, "%sDownloadTest_%u", szTemp, GetCurrentProcessId());
    CreateDirectoryA(szDir, NULL);
    s_strDir = szDir;
    strcpy_s(g_ServerGlobals.szModuleDir, szDir);
    strcpy_s(g_ServerGlobals.szServiceName, "DownloadServer");
    g_ServerGlobals.hInstance = GetModuleHandleA(NULL);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    g_ServerGlobals.dwProcessorCount = si.dwNumberOfProcessors;
    g_ServerGlobals.nMaxSendQueDepth = -1;
    g_pServerApp = &g_DownloadServer;

    TestConfig();

    LogCapture_Start();
    TestStartup();

    CHECK(g_pBSNet->Listen("127.0.0.1", 0));
    sockaddr_in local;
    g_DownloadServer.m_NetEngine.m_pListener->GetLocalAddr(&local);
    s_wPort = ntohs(local.sin_port);
    CHECK(g_pBSNet->RegisterProactor(TEST_TASK_CLIENT, NULL, &CTestClient::classCTestClient,
                                     NULL));
    CHECK(g_pBSNet->StartProactorWorkers(TEST_TASK_CLIENT, 1, 0, 0, 0, NULL));

    TestUserLimit();
    CLIENT gm = Connect();
    TestPatchRelay(gm);
    TestClientDownload();
    TestServerPackets(gm);
    TestShutdown(gm);
    CloseHandle(g_hServerIOCP);
    g_hServerIOCP = NULL;

    _fcloseall();
    RemoveTree(s_strDir);
    DeleteCriticalSection(&s_cs);
    return TEST_RESULT("DownloadTest");
}
