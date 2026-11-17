#include "FarmManager.h"

#include "BSLib/BSDebug.h"
#include "FarmManagerMainProcess.h"
#include "ServerFramework/ServerDataExtern.h"

using namespace ServerFramework;

CFarmManager g_FarmManager;

CFarmManager::CFarmManager()
{
}

CFarmManager::~CFarmManager()
{
}

void CFarmManager::SetDefaultEngineConfig()
{
    CServerApp::SetDefaultEngineConfig();
    m_EngineParam.bSecurityMode = 3;
}

bool CFarmManager::RegisterMainProcessTask()
{
    BSInstallSETranslator();
    return g_pBSNet->RegisterProactor(SERVER_TASK_MAIN, NULL,
                                      &CFarmManagerMainProcess::classCFarmManagerMainProcess,
                                      NULL)
        && g_pBSNet->StartProactorWorkers(SERVER_TASK_MAIN, 1, 0, 0, 0, NULL) != FALSE;
}

bool CFarmManager::OnLocalDataInitialized()
{
    SetServerState(5);
    return true;
}
