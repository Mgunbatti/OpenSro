#include "GlobalManager.h"

#include <stdlib.h>
#include <string.h>
#include <string>
#include <strsafe.h>

#include "ContentUser.h"
#include "GlobalManagerDB.h"
#include "GlobalManagerJobs.h"
#include "GlobalManagerMainProcess.h"
#include "GlobalManagerSlave.h"
#include "PunishmentManager.h"
#include "UserValidationHistory.h"
#include "BSLib/BSDebug.h"
#include "BSLib/DB/Database.h"
#include "BSLib/DB/DBQuery.h"
#include "ServerFramework/CertData.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerFrameWindow.h"
#include "ServerFramework/ServerProcess.h"
#include "ServerFramework/Trigger.h"

using namespace ServerFramework;

// The account database password the original inserts after "PWD=" of a division 2
// "AccountDBConfig" (a fixed value compiled into the binary, as in GatewayServer). Supplied at
// build time.
#ifndef GLOBAL_ACCOUNT_DB_PASSWORD
#define GLOBAL_ACCOUNT_DB_PASSWORD ""
#endif

CGlobalManager g_GlobalManager;

// ---------------------------------------------------------------------------------------------
// Monitor graphs

bool __cdecl GlobalManager_ReadContentUserCounter(MONITOR_COUNTER* pCounter)
{
    switch (pCounter->dwID)
    {
    case 0:
        pCounter->fValue = (float)g_ContentUserManager.m_nUserCount;
        break;
    case 1:
        pCounter->fValue = (float)(int)ContentUser_TakeRegisteredCount();
        break;
    case 2:
        pCounter->fValue = (float)(int)ContentUser_TakeUnregisteredCount();
        break;
    case 3:
        pCounter->fValue = (float)g_ContentUserManager.m_nDailyVisited;
        break;
    case 4:
        pCounter->fValue = (float)g_ContentUserManager.m_nTotalVisited;
        break;
    case 5:
        pCounter->fValue = (float)g_ContentUserManager.m_nOccupiedIPCount;
        break;
    }
    return true;
}

static MONITOR_COUNTER* NewCounter(DWORD dwSelector)
{
    MONITOR_COUNTER* pCounter = new MONITOR_COUNTER;
    pCounter->dwMaxSamples = 0;
    pCounter->dw4 = 0;
    pCounter->fValue = 0;
    pCounter->dwSampleCount = 0;
    pCounter->dw14 = 0;
    pCounter->pfnUpdate = GlobalManager_ReadContentUserCounter;
    pCounter->dwID = dwSelector;
    return pCounter;
}

// ---------------------------------------------------------------------------------------------
// Start-up steps

void GlobalManager_LoadPunishments()
{
    CGlobalManagerMainProcess* pMain = g_pGlobalManagerProcess;
    if (!g_pPunishmentManager)
        BSAssertFail();
    if (!g_pPunishmentManager->Load((CTask*)pMain->GetOwner()))
    {
        LogPrintf(0x2000000, "Cannot get punishment information");
        return;
    }
    pMain->RegisterTimer(GM_TIMER_PUNISHMENT, 60000,
                         static_cast<PFN_PROCESS_TIMER>(
                             &CGlobalManagerMainProcess::OnPunishmentTimer));
}

bool GlobalManager_InitValidationHistory()
{
    CGlobalManagerMainProcess* pMain = g_pGlobalManagerProcess;
    const char* pszPunisher = ServerConfig_FindExtraValue("AutomatedPunisher");
    const char* pszLoginTolerance = ServerConfig_FindExtraValue("LoginFailureTolerance");
    const char* pszLoginBlock = ServerConfig_FindExtraValue("LoginFailureBlockTimeMin");
    const char* pszLoginGuide = ServerConfig_FindExtraValue("LoginPunishmentGuide");
    const char* pszLoginDesc = ServerConfig_FindExtraValue("LoginPunishmentDescription");
    const char* pszIBUVTolerance = ServerConfig_FindExtraValue("IBUVFailureTolerance");
    const char* pszIBUVBlock = ServerConfig_FindExtraValue("IBUVFailureBlockTimeMin");
    const char* pszIBUVGuide = ServerConfig_FindExtraValue("IBUVPunishmentGuide");
    const char* pszIBUVDesc = ServerConfig_FindExtraValue("IBUVPunishmentDescription");
    const char* pszMOTPTolerance = ServerConfig_FindExtraValue("MOTPFailureTolerance");
    const char* pszMOTPBlock = ServerConfig_FindExtraValue("MOTPFailureBlockTimeMin");
    const char* pszMOTPGuide = ServerConfig_FindExtraValue("MOTPPunishmentGuide");
    const char* pszMOTPDesc = ServerConfig_FindExtraValue("MOTPPunishmentDescription");

    if (!pszPunisher)
        pszPunisher = "AutomatedPunisher";
    if (!g_pUserValidationHistoryMgr)
        BSAssertFail();
    g_pUserValidationHistoryMgr->SetPunisher(&pMain->m_SM, pszPunisher);

    const char* pszDesc = pszLoginDesc ? pszLoginDesc : "LoginPunishmentDescription";
    const char* pszGuide = pszLoginGuide ? pszLoginGuide : "LoginPunishmentGuide";
    DWORD dwBlock = pszLoginBlock ? atol(pszLoginBlock) : 10;
    DWORD dwTolerance = pszLoginTolerance ? atol(pszLoginTolerance) : 3;
    if (!g_pUserValidationHistoryMgr)
        BSAssertFail();
    g_pUserValidationHistoryMgr->SetConfig(0, pszGuide, pszDesc, dwTolerance, dwBlock);

    pszDesc = pszIBUVDesc ? pszIBUVDesc : "IBUVPunishmentDescription";
    pszGuide = pszIBUVGuide ? pszIBUVGuide : "IBUVPunishmentGuide";
    dwBlock = pszIBUVBlock ? atol(pszIBUVBlock) : 10;
    dwTolerance = pszIBUVTolerance ? atol(pszIBUVTolerance) : 3;
    if (!g_pUserValidationHistoryMgr)
        BSAssertFail();
    g_pUserValidationHistoryMgr->SetConfig(1, pszGuide, pszDesc, dwTolerance, dwBlock);

    // The default description is spelled like this in the original.
    pszDesc = pszMOTPDesc ? pszMOTPDesc : "MOTPPunishmenDescription";
    pszGuide = pszMOTPGuide ? pszMOTPGuide : "MOTPPunishmentGuide";
    dwBlock = pszMOTPBlock ? atol(pszMOTPBlock) : 10;
    dwTolerance = pszMOTPTolerance ? atol(pszMOTPTolerance) : 3;
    if (!g_pUserValidationHistoryMgr)
        BSAssertFail();
    g_pUserValidationHistoryMgr->SetConfig(2, pszGuide, pszDesc, dwTolerance, dwBlock);
    return true;
}

bool GlobalManager_LoadLastPlayTimeReset()
{
    COverlapJob_ResetPlayTimeOfGlobalServer* pJob = OverlapJob_AllocResetPlayTime();
    if (pJob->Setup(0, 0) && pJob->Execute() && pJob->OnComplete())
    {
        pJob->ReturnToPool();
        return true;
    }
    pJob->ReturnToPool();
    return false;
}

// ---------------------------------------------------------------------------------------------
// CGlobalManager

void CGlobalManager::SetDefaultEngineConfig()
{
    CServerApp::SetDefaultEngineConfig();
    m_EngineParam.bSecurityMode = 3;
}

bool CGlobalManager::RegisterMainProcessTask()
{
    BSInstallSETranslator();
    return g_pBSNet->RegisterProactor(SERVER_TASK_MAIN, NULL,
                                      &CGlobalManagerMainProcess::classCGlobalManagerMainProcess,
                                      NULL)
        && g_pBSNet->StartProactorWorkers(SERVER_TASK_MAIN, 1, 0, 0, 0, NULL)
        && g_pBSNet->RegisterProactor(GM_TASK_SLAVE, NULL,
                                      &CGlobalManagerSlave::classCGlobalManagerSlave, NULL)
        && g_pBSNet->StartProactorWorkers(GM_TASK_SLAVE, 1, -1, 0, 0, NULL) != FALSE;
}

int CGlobalManager::OnSecurityDescriptionRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    DWORD dwTable = pQuery->m_dwParam;
    if (!pQuery->m_bRowFetched)
    {
        switch (dwTable)
        {
        case 0:
            pQuery->m_pBuffer = (BYTE*)operator new(sizeof(CERT_DATA_SETTING));
            pQuery->m_nRowSize = sizeof(CERT_DATA_SETTING);
            break;
        case 1:
            pQuery->m_pBuffer = (BYTE*)operator new(sizeof(CERT_DATA_GROUP));
            pQuery->m_nRowSize = sizeof(CERT_DATA_GROUP);
            break;
        case 2:
            pQuery->m_pBuffer = (BYTE*)operator new(sizeof(CERT_DATA_GROUP_IP));
            pQuery->m_nRowSize = sizeof(CERT_DATA_GROUP_IP);
            break;
        }
        memset(pQuery->m_pBuffer, 0, pQuery->m_nRowSize);
        return 1;
    }
    switch (dwTable)
    {
    case 0:
        ((CertDataSettingList*)pQuery->m_pUserData)->push_back(
            (CERT_DATA_SETTING*)pQuery->m_pBuffer);
        break;
    case 1:
        ((CertDataGroupList*)pQuery->m_pUserData)->push_back(
            (CERT_DATA_GROUP*)pQuery->m_pBuffer);
        break;
    case 2:
        ((CertDataGroupIPList*)pQuery->m_pUserData)->push_back(
            (CERT_DATA_GROUP_IP*)pQuery->m_pBuffer);
        break;
    }
    return 1;
}

// BUG(GM-026): the three lists (and the rows read so far) are leaked when a query fails.
bool CGlobalManager::LoadSecurityDescriptions()
{
    CDBSessionMgr* pSession = g_pDefaultDBSession;
    CertDataSettingList* pSettings = new CertDataSettingList;
    CertDataGroupList* pGroups = new CertDataGroupList;
    CertDataGroupIPList* pGroupIPs = new CertDataGroupIPList;
    CClassTrigger<CGlobalManager> trigger(this);
    trigger.m_pfnTrigger2 = &CGlobalManager::OnSecurityDescriptionRow;

    // The buffer and row size are left to the trigger.
    CDBQuery query;
    query.m_pUserData = pSettings;
    query.m_pTrigger = &trigger;
    query.m_pSession = pSession;
    query.m_dwParam = 0;
    query.m_nSession = 0;
    if (FAILED(StringCchCopyA(query.m_szQuery, sizeof(query.m_szQuery),
                              "select * from _SecurityDescription with (NOLOCK)")))
    {
        BSAssertFail();
        return false;
    }
    if (query.Execute() < 0)
        return false;
    query.m_dwParam = 1;
    query.m_pUserData = pGroups;
    if (FAILED(StringCchCopyA(query.m_szQuery, sizeof(query.m_szQuery),
                              "select * from _SecurityDescriptionGroup with (NOLOCK)")))
    {
        BSAssertFail();
        return false;
    }
    if (query.Execute() < 0)
        return false;
    query.m_pUserData = pGroupIPs;
    query.m_dwParam = 2;
    if (FAILED(StringCchCopyA(query.m_szQuery, sizeof(query.m_szQuery),
                              "select * from _SecurityDescriptionGroupAssign with (NOLOCK)")))
    {
        BSAssertFail();
        return false;
    }
    if (query.Execute() < 0)
        return false;
    CertData_BuildPrivilegedIPMaps(pGroups, pSettings, pGroupIPs);
    return true;
}

bool CGlobalManager::OnLocalDataInitialized()
{
    if (!CDatabase::InitEnvironment((const char*)g_pSelfBody->pDivision->ab1 + 32))
    {
        LogPrintf(0x2000000, "Failed to initialize db");
        return false;
    }
    LogPrintf(0, "Successfully DB Initialized!");
    g_pDefaultDBSession = CDatabase::CreateSession(NULL);
    if (!g_pDefaultDBSession)
    {
        LogPrintf(0x2000000, "Can't connect to account DB");
        return false;
    }
    const char* pszValue = ServerConfig_FindExtraValue("MaxUserForNonePCBangIP");
    if (pszValue)
    {
        g_nMaxUserPerIP = atol(pszValue);
        LogPrintf(0x2000000, "Max User Count Restriction Per IP: overrided to %d", g_nMaxUserPerIP);
    }
    else
    {
        LogPrintf(0x2000000, "Max User Count Restriction Per IP: system default (infinite)");
    }
    LogPrintf(0, "Account DB Successfully Connected!");

    const char* pszConfig = ServerConfig_FindExtraValue("AccountDBConfig");
    if (pszConfig)
    {
        std::string strConnect(pszConfig);
        if (g_pSelfBody->pDivision->byID == 2)
        {
            std::string strKey("PWD=;");
            std::string strPassword(GLOBAL_ACCOUNT_DB_PASSWORD);
            std::string strConfig(pszConfig);
            std::string::size_type nPos = strConfig.find(strKey.c_str(), 0, strKey.size());
            if (nPos == std::string::npos)
            {
                LogPrintf(0x2000000, "invalid account db connection string !!!");
                return false;
            }
            std::string strHead(strConfig, 0, nPos + 4);
            std::string strTail(strConfig, nPos + 4, strConfig.size());
            strConnect.assign(strHead, 0, std::string::npos);
            strConnect.append(strPassword, 0, std::string::npos);
            strConnect.append(strTail, 0, std::string::npos);
        }
        g_pAccountDBSession = CDatabase::CreateSession(strConnect.c_str());
        if (!g_pAccountDBSession)
        {
            LogPrintf(0x2000000, "can't connect to global db");
            return false;
        }
        LogPrintf(0, "account db connected successfully");
    }
    else
    {
        g_pAccountDBSession = g_pDefaultDBSession;
    }

    pszConfig = ServerConfig_FindExtraValue("SilkDBConfig");
    if (pszConfig)
    {
        g_pSilkDBSession = CDatabase::CreateSession(pszConfig);
        if (!g_pSilkDBSession)
        {
            LogPrintf(0x2000000, "Can't connect to Silk DB!!!");
            return false;
        }
        LogPrintf(0, "Silk DB Connected Successfully!!");
    }
    else
    {
        g_pSilkDBSession = g_pAccountDBSession;
        LogPrintf(0, "Silk DB is assigned to Account DB!!");
    }

    if (!LoadSecurityDescriptions())
    {
        LogPrintf(0x2000000, "security description data loading failed....");
        return false;
    }
    if (g_pAccountDBSession)
        ContentUser_LoadFranchiseIPs();
    GlobalManager_LoadPunishments();
    if (!g_pGlobalManagerSlave)
        BSAssertFail();
    g_pGlobalManagerSlave->m_pMainProcess = g_pGlobalManagerProcess;
    if (!GlobalManager_InitValidationHistory())
    {
        LogPrintf(0x2000000, "User Validation History Mgr Initialize Failed!!!");
        return false;
    }
    if (g_pServerFrameWindow)
        g_pServerFrameWindow->m_dwDumpFlags |= SERVER_DUMP_MSG_USAGE;
    SetServerState(4);
    g_setNotifyBody.erase(g_pSelfBody->wParentID);
    MonitorGraph_AddCounterLocked(GM_COUNTER_CONTENT_USER, "ContentUser", NewCounter(0));
    MonitorGraph_AddCounterLocked(GM_COUNTER_REGISTERED, "Registered UserCount", NewCounter(1));
    MonitorGraph_AddCounterLocked(GM_COUNTER_UNREGISTERED, "Unregistered UserCount",
                                  NewCounter(2));
    MonitorGraph_AddCounterLocked(GM_COUNTER_DAILY_VISITED, "Daily Visited UserCount",
                                  NewCounter(3));
    MonitorGraph_AddCounterLocked(GM_COUNTER_TOTAL_VISITED, "Total Visited UserCount",
                                  NewCounter(4));
    MonitorGraph_AddCounterLocked(GM_COUNTER_OCCUPIED_IP, "Occupied IP Count", NewCounter(5));

    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(GM_MSG_DIVISION_NOTIFY);
    pMsg->WriteBytes(&g_pSelfBody->byDivisionID, 1);
    if (!RelayMsgToBody_6008(g_pGlobalManagerProcess, pMsg, g_pSelfBody->wParentID, 0, 0, 0))
        LogPrintf(0x2000001, "It was Failed to Initialize the Silk History Loadbalancer...");
    if (!GlobalManager_LoadLastPlayTimeReset())
    {
        LogPrintf(0x2000000, "MeterRateSytem data loading failed....");
        return false;
    }
    return true;
}

bool CGlobalManager::OnShutdown()
{
    CDatabase::DestroySession(g_pDefaultDBSession);
    g_ContentUserManager.RemoveAll();
    return true;
}
