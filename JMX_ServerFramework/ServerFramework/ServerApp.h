#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>

#include "BSLib/thread/ThreadManager.h"
#include "BSNet/IBSNet.h"
#include "BSNet/NetEngine.h"
#include "ServerFramework/LogQueue.h"
#include "ServerFramework/ServerFramework.h"

class CMsg;

// Value ServerApp_Startup gives g_ServerGlobals.dw2B4: with 1 the certification server is
// reached through a keep-alive target (reconnected by the engine) instead of a single connect
// (RequestServerCertification). A compile-time setting of the module like SERVER_MODULE_NAME:
// 0 in GatewayServer and DownloadServer, 1 in MachineManager.
#ifndef SERVER_CERTIFICATION_KEEP_ALIVE
#define SERVER_CERTIFICATION_KEEP_ALIVE 0
#endif

// Server application: the object every server module derives its application class from
// (CGatewayServer, CMachineManager, ...). The module's WinMain stores its single instance in
// g_pServerApp and calls ServerApp_Startup.

// Task IDs of the server processes (the engine's own tasks are NETENGINE_TASK_*).
enum
{
    SERVER_TASK_MAIN            = 0x01000000,   // CServerProcessMain (or the module's class)
    SERVER_TASK_OVERLAP         = 0x01000001,   // CServerProcessOverlap
};

// Messages the application object handles in OnControlMsg (posted to the application's
// completion port, passed by the server window or sent by a job).
enum
{
    SERVER_CONTROL_LOG              = 0x200A,   // DWORD flags, string: one log line
    SERVER_CONTROL_QUIT             = 0x200B,
    SERVER_CONTROL_INIT_LOCAL_DATA  = 0x200C,   // after certification
};

// Server states (SERVER_BODY::nState) set by the application.
enum
{
    SERVER_STATE_INITIALIZING   = 2,
    SERVER_STATE_CERTIFYING     = 3,
    SERVER_STATE_STOPPED        = 8,
};

// Receiver of the log lines: the server window, the console or the event log.
typedef int (__cdecl* PFN_SERVER_LOG)(DWORD dwFlags, const char* pszText);

namespace ServerFramework
{

// Life cycle (ServerApp_RunWindowMode): Initialize, InitNetEngine, the server window,
// OnWindowCreated, RegisterServerTasks, RequestServerCertification, then the message loop until
// the window closes, then Shutdown. The answer of the certification server (0xA003) makes the
// main process call SetServerState, CreateListener and, through 0x200C, InitLocalData,
// OnLocalDataInitialized and InitMonitorCounters.
class CServerApp
{
public:
    // CServerApp_ConstructGlobal_53AE78 (0x43e420)
    CServerApp();
    virtual ~CServerApp();                                                  // 0

    // Logs "initialize module" and sets state 2. True.
    virtual bool Initialize();                                              // 1  0x43e560
    // Window mode only, after the server window was created. True.
    virtual bool OnWindowCreated();                                         // 2  0x43e590
    // Engine parameter defaults (called by InitNetEngine first).
    virtual void SetDefaultEngineConfig();                                  // 3  0x43e5c0
    // Called by InitNetEngine after the framework's settings, before the engine is
    // initialised: the place to change m_EngineParam. Nothing here.
    virtual void OnEngineConfig();                                          // 4  0x43e620
    // Makes the member engine g_pBSNet, fills m_EngineParam (log receivers, maintainer timing,
    // Server.cfg options, session tasks SERVER_TASK_MAIN), initialises the engine and starts
    // the listen (1 thread), I/O (0) and maintainer (1) proactors.
    virtual bool InitNetEngine();                                           // 5  0x43e630
    // Registers and starts the overlap process (SERVER_TASK_OVERLAP), then
    // RegisterMainProcessTask, then the file transfer (m_strFileTransDir).
    virtual bool RegisterServerTasks();                                     // 6  0x43e7a0
    // Registers and starts CServerProcessMain as SERVER_TASK_MAIN. Modules register their own
    // main process class here.
    virtual bool RegisterMainProcessTask();                                 // 7  0x43e870
    // Sets state 3 and connects to the certification server of Server.cfg (keep-alive target
    // instead when g_ServerGlobals.dw2B4 is set).
    virtual bool RequestServerCertification();                              // 8  0x43e8f0
    // 0x200C, first step. True.
    virtual bool InitLocalData();                                           // 9  0x43e950
    // Listener on the port of this server's body.
    virtual bool CreateListener();                                          // 10 0x43e960
    // 0x200C, after the division ID was stored. True.
    virtual bool OnLocalDataInitialized();                                  // 11 0x43e9c0
    // Called by Shutdown after the engine is gone. True.
    virtual bool OnShutdown();                                              // 12 0x43e9d0
    // 0x200C, last step: the message and overlap job ID graphs of the monitor view.
    virtual bool InitMonitorCounters();                                     // 13 0x43ecd0
    // State 8, file transfer reset, at least 300 ms, engine shutdown, OnShutdown,
    // architecture and certification data freed.
    virtual bool Shutdown();                                                // 14 0x43e9e0
    // Diagnostic dump menu of the server window (SERVER_DUMP_CUSTOM). Nothing here.
    virtual void CustomDump();                                              // 15 0x403660
#ifdef JMX_FRAMEWORK_2011
    // AgentServer 0x401010: command 0x9C48 of the server window (CServerFrameWindow slot 17).
    // Nothing here.
    virtual void OnFrameCommand();                                          // 16
    // AgentServer 0x401020: the "Suspected User" items of the log list menu, dwCommand 0
    // register, 1 unregister, 2 show (dwReserved is 0). Nothing here.
    virtual void OnSuspectedUserCommand(DWORD dwReserved, DWORD dwCommand); // 17
#endif
    // SERVER_CONTROL_* messages; releases the message (returns ReleaseMsg's result).
    // (slots 18 / 19 with JMX_FRAMEWORK_2011)
    virtual LONG OnControlMsg(CMsg* pMsg);                                  // 16 0x43eb20
    // State of this server's body (broadcast with 0x2005), kept in m_nPendingState until the
    // architecture is known.
    virtual void SetServerState(int nState);                                // 17 0x43eca0

public:
    PFN_SERVER_LOG      m_pfnLog;           // +4
    std::string         m_strFileTransDir;  // +8   FileXfer_InitEngineAndName
    DWORD               m_dw24;             // +24
    BSNET_INIT_PARAM    m_EngineParam;      // +28
    int                 m_nPendingState;    // +BC  1 (+13C with JMX_FRAMEWORK_2011)
    CNetEngine          m_NetEngine;        // +C0    (+140 with JMX_FRAMEWORK_2011)
};

C_ASSERT(FIELD_OFFSET(CServerApp, m_strFileTransDir) == 8);
C_ASSERT(FIELD_OFFSET(CServerApp, m_EngineParam) == 0x28);
#ifdef JMX_FRAMEWORK_2011
// AgentServer 0x430640 (instance 0x52ABE0): the engine parameters are 0x114 bytes.
C_ASSERT(FIELD_OFFSET(CServerApp, m_nPendingState) == 0x13C);
C_ASSERT(FIELD_OFFSET(CServerApp, m_NetEngine) == 0x140);
C_ASSERT(sizeof(CServerApp) == 0x620);
#else
C_ASSERT(FIELD_OFFSET(CServerApp, m_nPendingState) == 0xBC);
C_ASSERT(FIELD_OFFSET(CServerApp, m_NetEngine) == 0xC0);
C_ASSERT(sizeof(CServerApp) == 0x494);
#endif

} // namespace ServerFramework

// ServerApp_Startup (0x41d280): process start of a server module (WinMain). Fills
// g_ServerGlobals, checks for a second instance, loads ServerFrameworkRes.dll and Server.cfg,
// finds the public address, installs the crash dump filter and runs the window mode. Errors go
// to a message box and <module dir>\StartupError_<module>.txt. Returns 0, or -1 on errors.
int ServerApp_Startup(HINSTANCE hInstance, const char* pszCmdLine);

// ServerApp_RunWindowMode (0x453630): see CServerApp. 1 after the window closed, -1 when a
// step failed (Shutdown was called).
int ServerApp_RunWindowMode();
// ServerApp_RunConsoleMode (0x4539c0, not referenced): the same with a console instead of the
// window; ends when the completion port gets an empty packet (Ctrl+C).
int ServerApp_RunConsoleMode();

// HandlerRoutine (0x4539a0): Ctrl+C / close of the console ends the console mode.
BOOL WINAPI ServerApp_ConsoleCtrlHandler(DWORD dwCtrlType);

// Net_IsPrivateIPv4 (0x41d630)
bool Net_IsPrivateIPv4(DWORD dwIP);
// Net_GetPublicIPv4Address (0x41d670): the first address that is not loopback, 0 or private
// (else the last private one), as DWORD and, if pszIP is set, dotted string (16 bytes).
bool Net_GetPublicIPv4Address(char* pszIP, DWORD* pdwIP);

// SingleInstance_CreateSemaphore (0x452f20): semaphore named after the module; NULL when it
// exists already.
#ifdef JMX_FRAMEWORK_2011
// AgentServer 0x44f1e0: the name is passed ("Global\" + module name).
HANDLE SingleInstance_CreateSemaphore(const char* pszName);
#else
HANDLE SingleInstance_CreateSemaphore();
#endif
// CloseHandle_wrap (0x452f60)
void SingleInstance_Close(HANDLE* phSemaphore);
// NamedSemaphore_Exists (MachineManager 0x42d400, not linked into GatewayServer): TRUE when a
// semaphore with the name exists, i.e. the server module of that name is running (see
// SingleInstance_CreateSemaphore). A missing semaphore is created and closed again.
BOOL SingleInstance_Exists(const char* pszName);
// FindOtherProcessByExeName (0x4278e0, not referenced): ID of another process whose exe name
// starts with pszExeName, 0 if none.
DWORD FindOtherProcessByExeName(const char* pszExeName);

// PatchFile_BuildFullPath (0x427530, not referenced): local path of a patch file record.
bool PatchFile_BuildFullPath(std::string* pPath, const char* pszSubDir, DWORD dwFileID);

// Struct12_Init_0_FF_FF (0x43ed10, not referenced)
inline void ThreadAffinity_InitDefault(THREAD_AFFINITY* pAffinity)
{
    pAffinity->bRandom = FALSE;
    pAffinity->nMinCPU = -1;
    pAffinity->nMaxCPU = -1;
}

// ServerProcess_SendFileTo (0x45a260): sends the file pszName of the directory
// <module dir>\<g_strFileTransDir>\<pszSubDir>\<pszPath> (empty parts left out) to the session,
// starting at dwOffset.
int ServerProcess_SendFileTo(const char* pszSubDir, const char* pszName, DWORD dwSessionID,
                             const char* pszPath, DWORD dwOffset);
// ServerProcess_SendFileTo_Impl (0x45ac90): engine SendFile when the file transfer may send.
BOOL ServerProcess_SendFileTo_Impl(const char* pszFile, DWORD dwSessionID, DWORD dwOffset);
