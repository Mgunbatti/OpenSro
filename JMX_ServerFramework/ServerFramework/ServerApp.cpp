#include "ServerApp.h"

#include <iphlpapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>

#include "BSLib/BSDebug.h"
#include "BSNet/Msg.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/CertData.h"
#include "ServerFramework/CrashDump.h"
#include "ServerFramework/FileTransMgr.h"
#include "ServerFramework/GdiUtil.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerFramework/MonitorGraph.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerFrameWindow.h"
#include "ServerFramework/ServerProcess.h"
#include "ServerFramework/ServerProcessMain.h"
#include "ServerFramework/ServerUiExtern.h"
#include "ServerFramework/WindowBase.h"

#include <strsafe.h>

using namespace ServerFramework;

// ---------------------------------------------------------------------------------------------
// Machine helpers

// BUG(FWAPP-003): the address is "private" when its bits are a subset of one of the masks, not
// when it has one of the prefixes (10.x is caught, but so are 2.x, 8.x and 0.x, while 172.16.x
// to 172.31.x only partly).
bool Net_IsPrivateIPv4(DWORD dwIP)
{
    DWORD adwMask[3] = { 0xFFFFFF0A, 0xFFFF1FAC, 0xFFFFA8C0 };
    for (DWORD i = 0; i < 3; ++i)
    {
        if (dwIP == (dwIP & adwMask[i]))
            return true;
    }
    return false;
}

static bool FormatIPv4(char* pszIP, DWORD dwIP)
{
    if (StringCchPrintfA(pszIP, 16, "%d.%d.%d.%d", dwIP & 0xFF, (dwIP >> 8) & 0xFF,
                         (dwIP >> 16) & 0xFF, dwIP >> 24))
    {
        BSAssertFail();
        return false;
    }
    return true;
}

bool Net_GetPublicIPv4Address(char* pszIP, DWORD* pdwIP)
{
    ULONG ulSize = 0;
    *pdwIP = 0;
    if (GetIpAddrTable(NULL, &ulSize, FALSE) != ERROR_INSUFFICIENT_BUFFER)
        return false;

    MIB_IPADDRTABLE* pTable = (MIB_IPADDRTABLE*)operator new(ulSize);
    if (!GetIpAddrTable(pTable, &ulSize, FALSE))
    {
        for (DWORD i = 0; i < pTable->dwNumEntries; ++i)
        {
            DWORD dwIP = pTable->table[i].dwAddr;
            if (dwIP == 0x0100007F || !dwIP)
                continue;
            bool bPrivate = Net_IsPrivateIPv4(dwIP);
            *pdwIP = dwIP;
            if (!bPrivate)
            {
                if (pszIP && !FormatIPv4(pszIP, dwIP))
                    return false;
                break;
            }
            if (pszIP && !FormatIPv4(pszIP, dwIP))
                return false;
        }
    }
    operator delete(pTable);
    return *pdwIP != 0;
}

HANDLE SingleInstance_CreateSemaphore()
{
    HANDLE hSemaphore = CreateSemaphoreA(NULL, 0, 1, SERVER_MODULE_NAME);
    if (hSemaphore && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hSemaphore);
        hSemaphore = NULL;
    }
    return hSemaphore;
}

void SingleInstance_Close(HANDLE* phSemaphore)
{
    if (*phSemaphore)
    {
        CloseHandle(*phSemaphore);
        *phSemaphore = NULL;
    }
}

DWORD FindOtherProcessByExeName(const char* pszExeName)
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (!Process32First(hSnapshot, &pe))
        return 0;
    DWORD dwProcessID = 0;
    for (;;)
    {
        if (GetCurrentProcessId() != pe.th32ProcessID && pe.th32ProcessID
            && strstr(pe.szExeFile, pszExeName) == pe.szExeFile)
        {
            dwProcessID = pe.th32ProcessID;
            break;
        }
        if (!Process32Next(hSnapshot, &pe))
            break;
    }
    CloseHandle(hSnapshot);
    return dwProcessID;
}

// BUG(FWAPP-011): the "<module>_<x>" directory is formatted but never appended.
bool PatchFile_BuildFullPath(std::string* pPath, const char* pszSubDir, DWORD dwFileID)
{
    stdext::hash_map<DWORD, MODULE_VERSION_FILE*>::iterator it =
        g_hmapModuleVersionFile.find(dwFileID);
    if (it == g_hmapModuleVersionFile.end())
        return false;
    MODULE_VERSION_FILE* pFile = it->second;
    if (!pFile)
        return false;

    pPath->assign(g_ServerGlobals.szModuleDir, strlen(g_ServerGlobals.szModuleDir));
    if (pszSubDir && strlen(pszSubDir))
    {
        pPath->append("\\", 1);
        pPath->append(pszSubDir, strlen(pszSubDir));
    }
    pPath->append("\\", 1);
    char szDir[50];
    if (StringCchPrintfA(szDir, 50, "%d_%d", pFile->byModuleID, pFile->ab8[1]))
    {
        BSAssertFail();
        return false;
    }
    if (strlen(pFile->szPath))
    {
        pPath->append("\\", 1);
        pPath->append(pFile->szPath, strlen(pFile->szPath));
    }
    pPath->append("\\", 1);
    pPath->append(pFile->szName, strlen(pFile->szName));
    return true;
}

// ---------------------------------------------------------------------------------------------
// File transfer of served files

BOOL ServerProcess_SendFileTo_Impl(const char* pszFile, DWORD dwSessionID, DWORD dwOffset)
{
    if (!(g_FileTransMgr.m_dwFlags & FILETRANS_FLAG_SEND))
        return FALSE;
    std::string strFile(pszFile);
    return g_FileTransMgr.m_pEngine->SendFile(dwSessionID, strFile, dwOffset, -1);
}

// The std::string handed to the log as variadic argument (FWAPP-012).
struct STRING_BYTES
{
    BYTE ab[sizeof(std::string)];
};

// BUG(FWAPP-012): the error log passes the std::string object itself for "%s".
int ServerProcess_SendFileTo(const char* pszSubDir, const char* pszName, DWORD dwSessionID,
                             const char* pszPath, DWORD dwOffset)
{
    std::string strFile;
    strFile.assign(g_ServerGlobals.szModuleDir, strlen(g_ServerGlobals.szModuleDir));
    if (g_strFileTransDir.size())
    {
        if (strFile.size())
            strFile.append("\\", 1);
        strFile.append(g_strFileTransDir, 0, std::string::npos);
    }
    if (pszSubDir && strlen(pszSubDir))
    {
        if (strFile.size())
            strFile.append("\\", 1);
        strFile.append(pszSubDir, strlen(pszSubDir));
    }
    if (pszPath && strlen(pszPath))
    {
        if (strFile.size())
            strFile.append("\\", 1);
        strFile.append(pszPath, strlen(pszPath));
    }
    if (strFile.size())
        strFile.append("\\", 1);
    strFile.append(pszName, strlen(pszName));

    if (ServerProcess_SendFileTo_Impl(strFile.c_str(), dwSessionID, dwOffset))
        return TRUE;
    STRING_BYTES raw;
    memcpy(&raw, &strFile, sizeof(raw));
    LogPrintf(0x2000000, "error : SendFileTo : %s", raw);
    return FALSE;
}

// ---------------------------------------------------------------------------------------------
// CServerApp

CServerApp::CServerApp()
{
    m_pfnLog = NULL;
    m_nPendingState = 1;
    m_dw24 = 0;
}

CServerApp::~CServerApp()
{
}

bool CServerApp::Initialize()
{
    LogPrintf(0, "initialize module");
    SetServerState(SERVER_STATE_INITIALIZING);
    return true;
}

bool CServerApp::OnWindowCreated()
{
    return true;
}

void CServerApp::SetDefaultEngineConfig()
{
    m_EngineParam.aSocket[0].nSendQueueLimit = -1;
    m_EngineParam.aSocket[1].nSendQueueLimit = -1;
    m_EngineParam.dwFlags = 3;
    m_EngineParam.nAcceptCount = 10;
    m_EngineParam.dwMaxConnectTime = 60;
    m_EngineParam.dw8C = 0x112;
    m_EngineParam.dw90 = 0x201;
    m_EngineParam.aSocket[1].dwSendMode = 1;
    m_EngineParam.aSocket[0].dwSendMode = 0;
    m_EngineParam.dwFileCacheLimit = 0;
    m_EngineParam.nSendFileType = 1;
    m_EngineParam.nRecvFileType = 1;
}

void CServerApp::OnEngineConfig()
{
}

// Ideal processor of the server threads: CPU 1 (nCPU, or 0 for RegisterMainProcessTask) when
// there is more than one, the last one otherwise.
static void MakeAffinity(THREAD_AFFINITY* pAffinity, int nCPU)
{
    int nCount = (int)g_ServerGlobals.dwProcessorCount;
    pAffinity->bRandom = FALSE;
    pAffinity->nMinCPU = nCount > nCPU ? nCPU : nCount - 1;
    pAffinity->nMaxCPU = nCount > nCPU ? nCPU : nCount - 1;
}

bool CServerApp::InitNetEngine()
{
    g_pBSNet = &m_NetEngine;
    SetDefaultEngineConfig();
    m_EngineParam.aSocket[0].dwSessionParam = SERVER_TASK_MAIN;
    m_EngineParam.aSocket[1].dwSessionParam = SERVER_TASK_MAIN;
    m_EngineParam.aSocket[1].dwSendMode = 1;
    m_EngineParam.aSocket[0].dwSendMode = 1;
    m_EngineParam.aSocket[0].nSendQueueLimit = g_ServerGlobals.nMaxSendQueDepth;
    m_EngineParam.Maintainer.dwWaitTimeout = 1000;
    m_EngineParam.Maintainer.aJobs[2].dwPeriod = 1000;
    m_EngineParam.Maintainer.dwFlags = MAINTAINER_CONFIG::FLAG_REAP_SESSIONS
                                       | MAINTAINER_CONFIG::FLAG_KEEP_ALIVE;
    m_EngineParam.Maintainer.aJobs[0].dwPeriod = 10000;
    m_EngineParam.dw8C = g_ServerGlobals.dwDebugOptionDebuggerPresent;
    m_EngineParam.dw90 = g_ServerGlobals.dwDebugOptionStandAlone;
    m_EngineParam.pfnLog = LogPrintf;
    OnEngineConfig();
    m_EngineParam.pfnDumpMsgPool = ServerApp_LogMsgPoolEntry;
    if (!g_pBSNet->Initialize(m_EngineParam))
        return false;

    THREAD_AFFINITY affinity;
    MakeAffinity(&affinity, 1);
    return g_pBSNet->StartProactorWorkers(NETENGINE_TASK_LISTEN, 1, 0, 0, 0, &affinity)
        && g_pBSNet->StartProactorWorkers(NETENGINE_TASK_IO, 0, 0, 0, 0, &affinity)
        && g_pBSNet->StartProactorWorkers(NETENGINE_TASK_MAINTAINER, 1, 0, 0, 0, &affinity);
}

bool CServerApp::RegisterServerTasks()
{
    if (!g_pBSNet)
        BSAssertFail();
    THREAD_AFFINITY affinity;
    MakeAffinity(&affinity, 1);
    if (!g_pBSNet->RegisterProactor(SERVER_TASK_OVERLAP, NULL,
                                    &CServerProcessOverlap::classCServerProcessOverlap, NULL)
        || !g_pBSNet->StartProactorWorkers(SERVER_TASK_OVERLAP, 1, 0, 0, 0, &affinity)
        || !RegisterMainProcessTask())
    {
        return false;
    }
    if (!FileXfer_InitEngineAndName(m_strFileTransDir.c_str()))
        return false;
    LogPrintf(0, "successfully server task registered");
    return true;
}

bool CServerApp::RegisterMainProcessTask()
{
    THREAD_AFFINITY affinity;
    MakeAffinity(&affinity, 0);
    return g_pBSNet->RegisterProactor(SERVER_TASK_MAIN, NULL,
                                      &CServerProcessMain::classCServerProcessMain, NULL)
        && g_pBSNet->StartProactorWorkers(SERVER_TASK_MAIN, 1, 0, 0, 0, &affinity);
}

bool CServerApp::RequestServerCertification()
{
    LogPrintf(0, "request server certification");
    SetServerState(SERVER_STATE_CERTIFYING);
    if (g_ServerGlobals.dw2B4)
    {
        g_pBSNet->AddKeepAliveTarget(g_ServerGlobals.szCertificationIP,
                                     (u_short)g_ServerGlobals.nCertificationPort,
                                     g_ServerGlobals.szCertificationBindIP, 0, 0);
        return true;
    }
    return g_pBSNet->Connect(g_ServerGlobals.szCertificationIP,
                             (u_short)g_ServerGlobals.nCertificationPort,
                             g_ServerGlobals.szCertificationBindIP, 0, 0) != 0;
}

bool CServerApp::InitLocalData()
{
    return true;
}

bool CServerApp::CreateListener()
{
    if (!g_pBSNet)
        BSAssertFail();
    if (!g_pSelfBody)
        BSAssertFail();
    if (!g_pBSNet->Listen(NULL, g_pSelfBody->wPort))
    {
        LogPrintf(0x2000000, "cannot create listener : port (%d)", g_pSelfBody->wPort);
        return false;
    }
    return true;
}

bool CServerApp::OnLocalDataInitialized()
{
    return true;
}

bool CServerApp::OnShutdown()
{
    return true;
}

bool CServerApp::InitMonitorCounters()
{
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_MSG);
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_OVERLAP);
    return true;
}

bool CServerApp::Shutdown()
{
    DWORD dwStart = GetTickCount();
    SetServerState(SERVER_STATE_STOPPED);
    FileXfer_ResetQueueAndState_Global();
    if (GetTickCount() - dwStart < 300)
        Sleep(300);
    if (g_pBSNet)
    {
        g_pBSNet->Shutdown();
        g_pBSNet = NULL;
    }
    OnShutdown();
    ServerApp_FreeCertArchData();
    CertData_FreeRecordListsAndIPMaps();
    return true;
}

void CServerApp::CustomDump()
{
}

LONG CServerApp::OnControlMsg(CMsg* pMsg)
{
    switch (pMsg->GetOpcode())
    {
    case SERVER_CONTROL_LOG:
        {
            if (!m_pfnLog)
                BSAssertFail();
            std::string strText;
            DWORD dwFlags;
            *pMsg >> dwFlags;
            *pMsg >> strText;
            if (dwFlags == 0x2000001 || dwFlags == 0x3000000)
                FatalLog_AppendLine(strText.c_str());
            if (dwFlags != 0x3000000)
                m_pfnLog(dwFlags, strText.c_str());
        }
        break;

    case SERVER_CONTROL_QUIT:
        ServerApp_PostQuit();
        break;

    case SERVER_CONTROL_INIT_LOCAL_DATA:
        InitLocalData();
        g_dwSelfDivisionID = g_pSelfBody->byDivisionID;
        OnLocalDataInitialized();
        InitMonitorCounters();
        break;
    }
    if (!g_pBSNet)
        BSAssertFail();
    return g_pBSNet->ReleaseMsg(pMsg);
}

void CServerApp::SetServerState(int nState)
{
    if (g_pSelfBody)
    {
        ServerBody_SetStateAndBroadcast2005(nState, g_pSelfBody->wID, 0);
        return;
    }
    m_nPendingState = nState;
}

// ---------------------------------------------------------------------------------------------
// Run modes

int ServerApp_RunWindowMode()
{
    if (!g_pServerApp)
        BSAssertFail();
    g_pServerApp->m_pfnLog = LogView_AppendLine_wrap;
    if (!g_pServerApp->Initialize() || !g_pServerApp->InitNetEngine())
    {
        g_pServerApp->Shutdown();
        return -1;
    }

    CServerFrameWindow* pFrame = new CServerFrameWindow;
    g_pServerFrameWindow = pFrame;
    if (!pFrame->CreateFrame())
    {
        // BUG(FWAPP-010): g_pServerFrameWindow keeps pointing to the deleted window.
        if (g_pServerFrameWindow)
            delete g_pServerFrameWindow;
        g_pServerApp->Shutdown();
        return -1;
    }

    g_pServerApp->OnWindowCreated();
    MonitorGraph_StartUpdateThread();
    g_hServerFrameWnd = g_pServerFrameWindow->m_hWnd;
    if (!g_pServerApp->RegisterServerTasks())
    {
        g_pServerApp->Shutdown();
        return -1;
    }
    g_hServerIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!g_pServerApp->RequestServerCertification())
    {
        g_pServerApp->Shutdown();
        return -1;
    }

    DWORD dwBytes = 0;
    ULONG_PTR ulKey = 0;
    LPOVERLAPPED pOverlapped = NULL;
    MSG msg;
    PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE);
    while (msg.message != WM_QUIT)
    {
        if (!IsWindow(g_hServerFrameWnd))
            break;
        if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            if (WindowHandlerList_Dispatch(msg.message, msg.wParam, msg.lParam) == -1)
                DispatchMessageA(&msg);
        }
        else
        {
            dwBytes = 0;
            pOverlapped = NULL;
            if (GetQueuedCompletionStatus(g_hServerIOCP, &dwBytes, &ulKey, &pOverlapped, 10))
            {
                g_pServerApp->OnControlMsg((CMsg*)pOverlapped);
            }
            else if (GetLastError() != WAIT_TIMEOUT)
            {
                if (!ulKey || !dwBytes)
                    break;
                g_pServerApp->OnControlMsg((CMsg*)pOverlapped);
            }
        }
    }

    g_pServerFrameWindow->Destroy();
    if (g_pServerFrameWindow)
        delete g_pServerFrameWindow;
    g_pServerFrameWindow = NULL;
    MonitorGraph_StopUpdateThreadAndFreeCounters();
    g_pServerApp->Shutdown();
    CloseHandle(g_hServerIOCP);
    return 1;
}

BOOL WINAPI ServerApp_ConsoleCtrlHandler(DWORD)
{
    return PostQueuedCompletionStatus(g_hServerIOCP, 0, 0, NULL);
}

int ServerApp_RunConsoleMode()
{
    if (!g_pServerApp)
        BSAssertFail();
    g_pServerApp->m_pfnLog = Console_WriteLogLine;
    if (!g_pServerApp->Initialize() || !AllocConsole())
    {
        g_pServerApp->Shutdown();
        return -1;
    }

    HWND hConsole = GetConsoleWindow();
    g_hServerConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    g_hServerIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    SetConsoleCtrlHandler(ServerApp_ConsoleCtrlHandler, TRUE);
    SetWindowTextA(hConsole, g_ServerGlobals.szServiceName);
    SetForegroundWindow(hConsole);
    HFONT hFont = CreateFontA(12, 0, 0, 0, 0, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "arial");
    SendMessageA(hConsole, WM_SETFONT, (WPARAM)hFont, 0);
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), 0);
    if (!g_pServerApp->InitNetEngine() || !g_pServerApp->RegisterServerTasks()
        || !g_pServerApp->RequestServerCertification())
    {
        g_pServerApp->Shutdown();
        return -1;
    }

    DWORD dwBytes = 0;
    ULONG_PTR ulKey;
    LPOVERLAPPED pOverlapped = NULL;
    while (GetQueuedCompletionStatus(g_hServerIOCP, &dwBytes, &ulKey, &pOverlapped, INFINITE)
           && dwBytes)
    {
        g_pServerApp->OnControlMsg((CMsg*)pOverlapped);
        dwBytes = 0;
        pOverlapped = NULL;
    }
    g_pServerApp->Shutdown();
    DeleteObject(hFont);
    CloseHandle(g_hServerIOCP);
    return 1;
}

// ---------------------------------------------------------------------------------------------
// Startup

static int StartupError(HANDLE* phSemaphore, const char* pszBoxFormat, const char* pszFileText)
{
    BSMessageBoxPrintf(pszBoxFormat, SERVER_MODULE_NAME);
    ServerApp_WriteStartupErrorFile(SERVER_MODULE_NAME, pszFileText);
    SingleInstance_Close(phSemaphore);
    return -1;
}

// BUG(FWAPP-013): the service name is copied with a limit of 256 characters into its 64 byte
// field.
// BUG(FWAPP-014): a second instance writes its startup error file before the module directory
// is known, i.e. to "\StartupError_<module>.txt" on the current drive.
int ServerApp_Startup(HINSTANCE hInstance, const char* pszCmdLine)
{
    srand(GetTickCount());
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    g_ServerGlobals.dwProcessorCount = si.dwNumberOfProcessors;
    g_ServerGlobals.dwMainThreadID = GetCurrentThreadId();
    g_ServerGlobals.bIOCPMode = FALSE;
    g_ServerGlobals.hInstance = hInstance;
    if (StringCchCopyA(g_ServerGlobals.szServiceName, 256, SERVER_MODULE_NAME)
        || StringCchCopyA(g_ServerGlobals.szCommandLine, MAX_PATH, pszCmdLine))
    {
        BSAssertFail();
        return -1;
    }

    HANDLE hSemaphore = SingleInstance_CreateSemaphore();
    if (!hSemaphore)
    {
        return StartupError(&hSemaphore, "cannot create semaphore : module already executing",
                            "cannot create semaphore : module already executing");
    }

    char szPath[MAX_PATH];
    char szDrive[MAX_PATH];
    char szDir[MAX_PATH];
    char szName[MAX_PATH];
    char szExt[MAX_PATH];
    GetModuleFileNameA(hInstance, szPath, MAX_PATH);
    _splitpath_s(szPath, szDrive, MAX_PATH, szDir, MAX_PATH, szName, MAX_PATH, szExt, MAX_PATH);
    szDir[strlen(szDir) - 1] = 0;
    if (StringCchPrintfA(g_ServerGlobals.szModuleDir, MAX_PATH, "%s%s", szDrive, szDir))
    {
        BSAssertFail();
        SingleInstance_Close(&hSemaphore);
        return -1;
    }

    g_ServerGlobals.dwDebugOptionDebuggerPresent = 0x112;
    g_ServerGlobals.dwDebugOptionStandAlone = 0x201;
    g_ServerGlobals.bDumpMsgPool = FALSE;
    g_ServerGlobals.nMaxSendQueDepth = -1;
    g_ServerGlobals.bPromptAtClose = FALSE;
    g_ServerGlobals.dw2B4 = 0;
    g_ServerGlobals.bWritePerfLogToText = FALSE;
    g_hServerFrameworkRes = LoadLibraryA("ServerFrameworkRes.dll");
    if (!g_hServerFrameworkRes)
    {
        return StartupError(&hSemaphore, "cannot load ServerFrameworkRes.dll",
                            "cannot load ServerFrameworkRes.dll");
    }
    if (!ServerConfig_Load())
    {
        return StartupError(&hSemaphore,
                            "can't find server.cfg or failed to parse server config block [%s]",
                            "paring config error");
    }
    if (!strlen(g_ServerGlobals.szCertificationIP) && ServerName_CompareCertification())
    {
        return StartupError(&hSemaphore, "there is no assigned ip for certifying server [%s]",
                            "certification ip is void");
    }
    if (!Net_GetPublicIPv4Address(g_ServerGlobals.szPublicIP, &g_ServerGlobals.dwPublicIP))
    {
        return StartupError(&hSemaphore, "failed to identify public ip of this machine",
                            "cannot identity machine public ip");
    }

    CrashDump_InstallFilter();
    BSDebugSetFlags(g_ServerGlobals.dwDebugOptionDebuggerPresent,
                    g_ServerGlobals.dwDebugOptionStandAlone);
    ServerApp_RunWindowMode();
    FreeLibrary(g_hServerFrameworkRes);
    SingleInstance_Close(&hSemaphore);
    return 0;
}
