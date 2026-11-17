#include "AgentServer.h"

#include "AgentMainProcess.h"
#include "Token.h"
#include "User.h"
#include "XTrap.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"

using namespace ServerFramework;

CAgentServer g_AgentServer;
CAgentMainProcess g_AgentMainProcessUnused;

// ---------------------------------------------------------------------------------------------
// Monitor graphs

bool __cdecl AgentServer_ReadUserCount(MONITOR_COUNTER* pCounter)
{
    pCounter->fValue = (float)(DWORD)g_UserManager.m_mapUser.size();
    return true;
}

bool __cdecl AgentServer_ReadTokenCount(MONITOR_COUNTER* pCounter)
{
    pCounter->fValue = (float)(DWORD)g_TokenManager.m_mapToken.size();
    return true;
}

bool __cdecl AgentServer_ReadMsgSentCounter(MONITOR_COUNTER* pCounter)
{
    switch (pCounter->dwID)
    {
    case 0:
        pCounter->fValue = (float)g_dwAgentMsgSentCount;
        g_dwAgentMsgSentCount = 0;
        break;
    case 1:
        pCounter->fValue = (float)g_dwAgentMsgSentBytes;
        g_dwAgentMsgSentBytes = 0;
        break;
    }
    return true;
}

static MONITOR_COUNTER* NewCounter(PFN_MONITOR_COUNTER_UPDATE pfnUpdate, DWORD dwSelector)
{
    MONITOR_COUNTER* pCounter = new MONITOR_COUNTER;
    pCounter->dwMaxSamples = 0;
    pCounter->dw4 = 0;
    pCounter->fValue = 0;
    pCounter->dwSampleCount = 0;
    pCounter->dwID = dwSelector;
    pCounter->dw14 = 0;
    pCounter->pfnUpdate = pfnUpdate;
    return pCounter;
}

// ---------------------------------------------------------------------------------------------
// CAgentServer

void CAgentServer::SetDefaultEngineConfig()
{
    m_EngineParam.aSocket[0].nSendQueueLimit = -1;
    m_EngineParam.aSocket[1].nSendQueueLimit = -1;
    m_EngineParam.dwFlags = 0x13;
    m_EngineParam.nAcceptCount = 100;
    m_EngineParam.dwMaxConnectTime = 60;
    m_EngineParam.dw8C = 0x112;
    m_EngineParam.dw90 = 0x201;
    m_EngineParam.aSocket[1].dwSendMode = 1;
    m_EngineParam.aSocket[0].dwSendMode = 0;
    m_EngineParam.dwFileCacheLimit = 0;
    m_EngineParam.nSendFileType = 1;
    m_EngineParam.nRecvFileType = 1;
    m_EngineParam.bSecurityMode = 2;
}

bool CAgentServer::RegisterMainProcessTask()
{
    BSInstallSETranslator();
    return g_pBSNet->RegisterProactor(SERVER_TASK_MAIN, NULL,
                                      &CAgentMainProcess::classCAgentMainProcess, NULL)
        && g_pBSNet->StartProactorWorkers(SERVER_TASK_MAIN, 1, 0, 0, 0, NULL) != FALSE;
}

bool CAgentServer::OnLocalDataInitialized()
{
    // the process ID of another AgentServer is looked up but not used
    FindOtherProcessByExeName("AgentServer");
    SetServerState(4);
    MonitorGraph_AddCounterLocked(AGENT_COUNTER_USER, "UserCount",
                                  NewCounter(AgentServer_ReadUserCount, 0));
    MonitorGraph_AddCounterLocked(AGENT_COUNTER_TOKEN, "TokenCount",
                                  NewCounter(AgentServer_ReadTokenCount, 0));
    MonitorGraph_AddCounterLocked(AGENT_COUNTER_MSG_SENT_COUNT, "MsgSentCount",
                                  NewCounter(AgentServer_ReadMsgSentCounter, 0));
    MonitorGraph_AddCounterLocked(AGENT_COUNTER_MSG_SENT_BYTES, "MsgSentBytes",
                                  NewCounter(AgentServer_ReadMsgSentCounter, 1));
    g_UserManager.LoadTrafficFilterConfig();
    if (XTrap_Initialize())
    {
        LogPrintf(0x2000000, "###   XTrap Initialized Deactivated   ###");
        LogPrintf(0x2000001, "###   XTrap Initialized Deactivated   ###");
        return true;
    }
    LogPrintf(0x2000000, "###   XTrap Initialized Sucess   ###");
    return true;
}

bool CAgentServer::OnShutdown()
{
    g_UserManager.RemoveAll();
    g_TokenManager.RemoveAll();
    return true;
}

bool CAgentServer::InitMonitorCounters()
{
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_MSG);
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_SESSION);
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_SOCK_TCP);
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_CLASS_LINK);
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_ACCEPT);
    return true;
}
