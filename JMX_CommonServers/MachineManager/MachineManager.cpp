#include "MachineManager.h"

#include <map>
#include <string>

#include "BSLib/BSDebug.h"
#include "MachineManagerMainProcess.h"
#include "PerfCounter.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"

using namespace ServerFramework;

CMachineManager g_MachineManager;

const char* MachineManager_FindShutdownProcessTimeoutConfig()
{
    std::string strKey("ShutdownProcessTimeout");
    std::map<std::string, std::string>::iterator it = g_ServerGlobals.mapExtra.find(strKey);
    if (it == g_ServerGlobals.mapExtra.end())
        return NULL;
    return it->second.c_str();
}

// ---------------------------------------------------------------------------------------------
// CMachineManager

CMachineManager::CMachineManager()
{
}

CMachineManager::~CMachineManager()
{
}

void CMachineManager::SetDefaultEngineConfig()
{
    CServerApp::SetDefaultEngineConfig();
    m_EngineParam.bSecurityMode = 3;
}

bool CMachineManager::RegisterMainProcessTask()
{
    BSInstallSETranslator();
    return g_pBSNet->RegisterProactor(SERVER_TASK_MAIN, NULL,
                                      &CMachineManagerMainProcess::classCMachineManagerMainProcess,
                                      NULL)
        && g_pBSNet->StartProactorWorkers(SERVER_TASK_MAIN, 1, 0, 0, 0, NULL) != FALSE;
}

bool CMachineManager::OnLocalDataInitialized()
{
    SetServerState(5);
    PerfCounter_OpenQuery();
    PerfCounter_Add(PERF_COUNTER_PROCESSOR);
    PerfCounter_Add(PERF_COUNTER_MEMORY);
    return true;
}

bool CMachineManager::OnShutdown()
{
    PerfCounter_CloseAll();
    return true;
}
