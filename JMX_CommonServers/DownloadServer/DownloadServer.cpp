#include "DownloadServer.h"

#include <stdlib.h>
#include <map>
#include <string>

#include "BSLib/BSDebug.h"
#include "DownloadServerMainProcess.h"
#include "ServerFramework/MonitorGraph.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"

using namespace ServerFramework;

CDownloadServer g_DownloadServer;

DWORD g_dwDownloadUserLimit;

const char* DownloadServer_FindUserLimitConfig()
{
    std::string strKey("USER_LIMIT");
    std::map<std::string, std::string>::iterator it = g_ServerGlobals.mapExtra.find(strKey);
    if (it == g_ServerGlobals.mapExtra.end())
        return NULL;
    return it->second.c_str();
}

// ---------------------------------------------------------------------------------------------
// CDownloadServer

CDownloadServer::CDownloadServer()
{
    m_strFileTransDir.assign("download_files", 14);
    g_dwDownloadUserLimit = (DWORD)-1;
}

CDownloadServer::~CDownloadServer()
{
}

void CDownloadServer::SetDefaultEngineConfig()
{
    m_EngineParam.aSocket[0].nSendQueueLimit = -1;
    m_EngineParam.aSocket[1].nSendQueueLimit = -1;
    m_EngineParam.dwFlags = 0x33;
    m_EngineParam.nAcceptCount = 100;
    m_EngineParam.dwMaxConnectTime = 60;
    MEMORYSTATUS status;
    GlobalMemoryStatus(&status);
    m_EngineParam.dwFileCacheLimit = (DWORD)(status.dwTotalPhys >> 2);
    m_EngineParam.dw8C = 0x112;
    m_EngineParam.dw90 = 0x201;
    m_EngineParam.aSocket[1].dwSendMode = 1;
    m_EngineParam.aSocket[0].dwSendMode = 0;
    m_EngineParam.nSendFileType = 2;
    m_EngineParam.nRecvFileType = 1;
    m_EngineParam.bSecurityMode = 3;
}

void CDownloadServer::OnEngineConfig()
{
    m_EngineParam.Maintainer.aJobs[0].dwPeriod = 300000;
    m_EngineParam.Maintainer.aJobs[0].dwParam = 300000;
}

bool CDownloadServer::RegisterMainProcessTask()
{
    BSInstallSETranslator();
    return g_pBSNet->RegisterProactor(SERVER_TASK_MAIN, NULL,
                                      &CDownloadServerMainProcess::classCDownloadServerMainProcess,
                                      NULL)
        && g_pBSNet->StartProactorWorkers(SERVER_TASK_MAIN, 1, 0, 0, 0, NULL) != FALSE;
}

bool CDownloadServer::OnLocalDataInitialized()
{
    LogPrintf(0, "Initializing Local Data...");
    SetServerState(5);
    const char* pszValue = DownloadServer_FindUserLimitConfig();
    if (pszValue)
    {
        g_dwDownloadUserLimit = atol(pszValue);
        LogPrintf(0x2000000, "Max Concurrent User Count Overrided: %d", g_dwDownloadUserLimit);
        return true;
    }
    // the original passes the limit to this line too
    LogPrintf(0x2000000, "Max Concurrent User Count: Unlimited", g_dwDownloadUserLimit);
    return true;
}

bool CDownloadServer::InitMonitorCounters()
{
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_SESSION);
    return true;
}
