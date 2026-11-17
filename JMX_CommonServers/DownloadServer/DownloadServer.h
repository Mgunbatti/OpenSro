#pragma once

#include <winsock2.h>
#include <windows.h>

#include "ServerFramework/ServerApp.h"

// DownloadServer: serves the patch files to the game clients. The GlobalManager hands it the
// list of patch files (0x6305); the DownloadServer fetches the files from that session with the
// framework file transfer (0x6004 / 0xA004) into <module dir>\download_files\<module>_<x>\ and
// the clients then request them with 0x6004 {DWORD file ID, DWORD offset}. Server.cfg
// "USER_LIMIT" limits the number of sessions.

// The application object.
class CDownloadServer : public ServerFramework::CServerApp
{
public:
    // CDownloadServer_GlobalCtor (0x401200): the file transfer directory "download_files", the
    // session limit unlimited.
    CDownloadServer();
    // 0x401280
    virtual ~CDownloadServer();                                             // 0

    // InitDefaultSettings (0x4013c0): engine flags 0x33, 100 pending accepts, a file cache of a
    // quarter of the physical memory, served files as type 2, security mode 3.
    virtual void SetDefaultEngineConfig();                                  // 3
    // vf04 (0x401440): stale sessions are reaped every 300 s after 300 s.
    virtual void OnEngineConfig();                                          // 4
    // CreateMainProcess (0x401450): structured exceptions become C++ exceptions, then the main
    // process (CDownloadServerMainProcess).
    virtual bool RegisterMainProcessTask();                                 // 7
    // InitializeLocalData_UserLimit (0x401350): state 5 (in service), then Server.cfg
    // USER_LIMIT. True.
    virtual bool OnLocalDataInitialized();                                  // 11
    // vf13 (0x4014a0): the session count graph. True.
    virtual bool InitMonitorCounters();                                     // 13
};

C_ASSERT(sizeof(CDownloadServer) == 0x494);

// 0x513538, g_pServerApp.
extern CDownloadServer g_DownloadServer;

// 0x4D23F8: the most sessions accepted from clients; (DWORD)-1 for no limit.
extern DWORD g_dwDownloadUserLimit;

// CDownloadServer_FindUserLimitConfig (0x4039c0): value of "USER_LIMIT" of the module's
// Server.cfg block, NULL without one.
const char* DownloadServer_FindUserLimitConfig();
