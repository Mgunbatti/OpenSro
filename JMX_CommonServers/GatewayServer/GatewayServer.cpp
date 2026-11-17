#include "GatewayServer.h"

#include <stdlib.h>
#include <string.h>

#include "ArticleManager.h"
#include "CertifierFwd.h"
#include "BSLib/DB/DBQuery.h"
#include "GatewayMainProcess.h"
#include "ShardConditionManager.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/Trigger.h"

#include <strsafe.h>

using namespace ServerFramework;

// The account database password the original inserts after "PWD=" of a division 2
// "AccountDBConfig" (a fixed value compiled into the binary). Supplied at build time.
#ifndef GATEWAY_ACCOUNT_DB_PASSWORD
#define GATEWAY_ACCOUNT_DB_PASSWORD ""
#endif

CGatewayServer g_GatewayServer;

CDBSessionMgr* g_pGatewayDBSession;
CDBSessionMgr* g_pAccountDBSession;
SYSTEMTIME g_stGatewayLocalDataTime;
DWORD g_dwGatewaySessionCreated;
DWORD g_dwGatewaySessionLost;

CERTIFIER_PROCESS_POOL g_CertifierProcessPool;
DOWNLOAD_SERVER_LIST g_DownloadServerList;
std::list<FARM_MANAGER_ADDR> g_listFarmManagerAddr;

// CSingletonT<CIBUVProcess>::GetInstance with a check (AssertFail_wrap 0x407970,
// AssertFail_wrap_2 0x407990).
template <class T>
static T* GetSingletonChecked()
{
    T* p = CSingletonT<T>::GetInstance();
    if (!p)
        BSAssertFail();
    return p;
}

// ---------------------------------------------------------------------------------------------
// Helpers

const char* ServerConfig_FindExtraValue(const char* pszKey)
{
    std::string strKey(pszKey);
    std::map<std::string, std::string>::iterator it = g_ServerGlobals.mapExtra.find(strKey);
    if (it == g_ServerGlobals.mapExtra.end())
        return NULL;
    return it->second.c_str();
}

void IPv4ToString(std::string* pstrOut, DWORD dwIP)
{
    char szIP[256];
    if (FAILED(StringCchPrintfA(szIP, 256, "%d.%d.%d.%d", (BYTE)dwIP, (BYTE)(dwIP >> 8),
                                (BYTE)(dwIP >> 16), (BYTE)(dwIP >> 24))))
    {
        BSAssertFail();
    }
    pstrOut->assign(szIP, strlen(szIP));
}

// ---------------------------------------------------------------------------------------------
// Statistics

DWORD GatewayStat_TakeAverage()
{
    if (!g_CertifierProcessPool.dwElapsedCount)
        return 0;
    CSingleLockBS lock(&g_CertifierProcessPool.csStat, TRUE);
    DWORD dwAverage = g_CertifierProcessPool.dwElapsedSum / g_CertifierProcessPool.dwElapsedCount;
    g_CertifierProcessPool.dwElapsedSum = 0;
    g_CertifierProcessPool.dwElapsedCount = 0;
    lock.Unlock();
    return dwAverage;
}

bool __cdecl GatewayServer_ReadStatCounter(MONITOR_COUNTER* pCounter)
{
    switch (pCounter->dwID)
    {
    case 0:
        pCounter->fValue = (float)g_dwGatewaySessionCreated;
        g_dwGatewaySessionCreated = 0;
        break;
    case 1:
        pCounter->fValue = (float)g_dwGatewaySessionLost;
        g_dwGatewaySessionLost = 0;
        break;
    case 2:
        pCounter->fValue = (float)GatewayStat_TakeAverage();
        break;
    }
    return true;
}

static MONITOR_COUNTER* NewStatCounter(DWORD dwSelector)
{
    MONITOR_COUNTER* pCounter = new MONITOR_COUNTER;
    pCounter->dwMaxSamples = 0;
    pCounter->dw4 = 0;
    pCounter->fValue = 0;
    pCounter->dwSampleCount = 0;
    pCounter->dw14 = 0;
    pCounter->pfnUpdate = GatewayServer_ReadStatCounter;
    pCounter->dwID = dwSelector;
    return pCounter;
}

// ---------------------------------------------------------------------------------------------
// Download servers and FarmManagers

void DownloadServerList_Build()
{
    ServerBodyList* pBodies = g_pServerBodyList;
    CERT_MODULE* pModule = CertModule_FindByName("DownloadServer");
    for (ServerBodyList::iterator it = pBodies->begin(); it != pBodies->end(); ++it)
    {
        SERVER_BODY* pBody = *it;
        if (pBody->pModule == pModule)
            g_DownloadServerList.list.push_back(pBody);
    }
    g_DownloadServerList.itNext = g_DownloadServerList.list.begin();
}

void DownloadServerList_Clear()
{
    g_DownloadServerList.itNext = g_DownloadServerList.list.end();
    g_DownloadServerList.list.clear();
}

SERVER_BODY* SelectPatchServer_RoundRobin()
{
    ServerBodyList& list = g_DownloadServerList.list;
    if (!list.size())
        return NULL;
    ServerBodyList::iterator itFound = list.end();
    for (int i = 0; i < (int)list.size(); ++i)
    {
        ++g_DownloadServerList.itNext;
        if (g_DownloadServerList.itNext == list.end())
            g_DownloadServerList.itNext = list.begin();
        if ((*g_DownloadServerList.itNext)->nState == 5)
        {
            itFound = g_DownloadServerList.itNext;
            break;
        }
    }
    if (itFound == list.end())
        return NULL;
    return *itFound;
}

size_t GatewayServer_CollectFarmManagerAddrs()
{
    ServerBodyList* pBodies = g_pServerBodyList;
    for (ServerBodyList::iterator it = pBodies->begin(); it != pBodies->end(); ++it)
    {
        SERVER_BODY* pBody = *it;
        if (pBody->pModule == CertModule_FindByName("FarmManager")
            && lstrlenA(pBody->pMachine->szPublicIP))
        {
            FARM_MANAGER_ADDR addr;
            addr.byFarmID = pBody->byFarmID;
            addr.dwIP = inet_addr(pBody->pMachine->szPublicIP);
            g_listFarmManagerAddr.push_back(addr);
        }
    }
    return g_listFarmManagerAddr.size();
}

// ---------------------------------------------------------------------------------------------
// CGatewayServer

CGatewayServer::CGatewayServer()
{
    m_dw24 = 100 * 1024 * 1024;
}

CGatewayServer::~CGatewayServer()
{
}

void CGatewayServer::SetDefaultEngineConfig()
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
    m_EngineParam.bSecurityMode = 3;
}

bool CGatewayServer::RegisterMainProcessTask()
{
    BSInstallSETranslator();
    return g_pBSNet->RegisterProactor(SERVER_TASK_MAIN, NULL,
                                      &CGatewayMainProcess::classCGatewayMainProcess, NULL)
        && g_pBSNet->StartProactorWorkers(SERVER_TASK_MAIN, 1, 0, 0, 0, NULL)
        && UserCertifier_InitProcessOverlapPool()
        && g_pBSNet->RegisterProactor(GATEWAY_TASK_IBUV, NULL, IBUVProcess_GetRuntimeClass(), NULL)
        && g_pBSNet->StartProactorWorkers(GATEWAY_TASK_IBUV, 1, 0, 0, 0, NULL) != FALSE;
}

// The failure paths after the module version lists were created log this and free the lists.
static bool FailLoadModuleVersions(ModuleVersionList* pModules, ModuleVersionFileList* pFiles)
{
    LogPrintf(0x2000000, "cannot load module version data from DB");
    if (pModules)
        delete pModules;
    if (pFiles)
        delete pFiles;
    return false;
}

// BUG(GW-003): a failed _PrivilegedIP load frees the module version lists that
// ModuleVersion_StoreAndIndexLists already owns.
// BUG(GW-004): a failed account database connection logs the connection string with the
// password.
bool CGatewayServer::OnLocalDataInitialized()
{
    GetLocalTime(&g_stGatewayLocalDataTime);
    CertModuleList* pModuleList = g_pCertModuleList;
    for (CertModuleList::iterator it = pModuleList->begin(); it != pModuleList->end(); ++it)
    {
        const char* pszName = (*it)->szName;
        char szKey[MAX_PATH];
        if (FAILED(StringCchPrintfA(szKey, MAX_PATH, "LastFullVersion_%s", pszName)))
        {
            BSAssertFail();
            return false;
        }
        const char* pszValue = ServerConfig_FindExtraValue(szKey);
        if (pszValue)
        {
            DWORD dwVersion = atol(pszValue);
            m_mapLastFullVersion.insert(std::make_pair(std::string(pszName), dwVersion));
            LogPrintf(0, "FullVersion Setup [%s] : Version [%d]", pszName, dwVersion);
        }
    }

    CClassTrigger<CGatewayServer> triggerModule(this);
    triggerModule.m_pfnTrigger2 = &CGatewayServer::OnModuleVersionRow;
    CClassTrigger<CGatewayServer> triggerIP(this);
    triggerIP.m_pfnTrigger2 = &CGatewayServer::OnPrivilegedIPRow;
    CClassTrigger<CGatewayServer> triggerIP2(this);
    triggerIP2.m_pfnTrigger2 = &CGatewayServer::OnPrivilegedIPRow2;

    ModuleVersionList* pModules = new ModuleVersionList;
    ModuleVersionFileList* pFiles = new ModuleVersionFileList;

    if (!CDatabase::InitEnvironment((const char*)g_pSelfBody->pDivision->ab1 + 32))
        return false;
    CDBSessionMgr* pConnection = CDatabase::CreateSession(NULL);
    if (!pConnection)
    {
        LogPrintf(0x2000000, "cannot connect DB");
        return FailLoadModuleVersions(pModules, pFiles);
    }
    LogPrintf(0, "DB Connected.");
    BYTE byDivision = g_pSelfBody->byDivisionID;
    g_pGatewayDBSession = pConnection;
    if (byDivision == 2)
    {
        const char* pszConfig = ServerConfig_FindExtraValue("AccountDBConfig");
        if (!pszConfig)
        {
            LogPrintf(0x2000000, "AccountDBConfig required !!!");
            return FailLoadModuleVersions(pModules, pFiles);
        }
        std::string strKey("PWD=;");
        std::string strPassword(GATEWAY_ACCOUNT_DB_PASSWORD);
        std::string strConfig(pszConfig);
        std::string::size_type nPos = strConfig.find(strKey.c_str(), 0, strKey.size());
        if (nPos == std::string::npos)
        {
            LogPrintf(0x2000000, "invalid account db connection string !!!");
            return false;
        }
        std::string strHead(strConfig, 0, nPos + 4);
        std::string strTail(strConfig, nPos + 4, strConfig.size());
        std::string strConnect(strHead);
        strConnect.append(strPassword);
        strConnect.append(strTail);
        CDBSessionMgr* pAccount = CDatabase::CreateSession(strConnect.c_str());
        if (!pAccount)
        {
            LogPrintf(0x2000000, "cannot connect to account DB : %s", strConnect.c_str());
            return FailLoadModuleVersions(pModules, pFiles);
        }
        g_pAccountDBSession = pAccount;
        LogPrintf(0, "Account DB Connected Successfully");
    }
    else if (byDivision == 1 || byDivision == 11)
    {
        const char* pszConfig = ServerConfig_FindExtraValue("AccountDBConfig");
        if (!pszConfig)
        {
            LogPrintf(0x2000000, "need AccountDBConfig !!!");
            return FailLoadModuleVersions(pModules, pFiles);
        }
        CDBSessionMgr* pAccount = CDatabase::CreateSession(pszConfig);
        if (!pAccount)
        {
            LogPrintf(0x2000000, "cannot connect to DB : %s", pszConfig);
            return FailLoadModuleVersions(pModules, pFiles);
        }
        g_pAccountDBSession = pAccount;
        LogPrintf(0, "successfully DB Connected : %s", pszConfig);
    }
    else
    {
        g_pAccountDBSession = pConnection;
    }

    CDBQuery query;
    query.m_pSession = g_pGatewayDBSession;
    query.m_pUserData = pModules;
    query.m_pTrigger = &triggerModule;
    query.m_dwParam = 0;
    query.m_nSession = 0;
    if (FAILED(StringCchCopyA(query.m_szQuery, sizeof(query.m_szQuery),
                              "select * from _ModuleVersion with (NOLOCK) where nValid = 1")))
    {
        BSAssertFail();
        return FailLoadModuleVersions(pModules, pFiles);
    }
    if (query.Execute() < 0)
    {
        LogPrintf(0x2000001, "cannot loading _ModuleVersion Table");
        return FailLoadModuleVersions(pModules, pFiles);
    }
    query.m_pUserData = pFiles;
    query.m_dwParam = 1;
    if (FAILED(StringCchCopyA(query.m_szQuery, sizeof(query.m_szQuery),
                              "select * from _ModuleVersionFile with (NOLOCK) where nValid = 1")))
    {
        BSAssertFail();
        return FailLoadModuleVersions(pModules, pFiles);
    }
    if (query.Execute() < 0)
    {
        LogPrintf(0x2000001, "cannot loading _ModuleVersionFile Table");
        return FailLoadModuleVersions(pModules, pFiles);
    }
    for (ModuleVersionList::iterator it = pModules->begin(); it != pModules->end(); ++it)
    {
        MODULE_VERSION* pVersion = *it;
        LogPrintf(0, "version : %d, %d, %s", pVersion->ab0[5], pVersion->dwVersion,
                  (const char*)pVersion->ab11);
    }
    ModuleVersion_StoreAndIndexLists(pFiles, pModules);
    GatewayServer_CollectFarmManagerAddrs();

    BYTE abIPRange[8];
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pGatewayDBSession;
    query.m_pTrigger = &triggerIP;
    query.m_dwParam = 2;
    query.m_pBuffer = abIPRange;
    query.m_nBufferSize = 8;
    query.m_nRowSize = 8;
    if (query.ExecuteFormat("select * from _PrivilegedIP") < 0)
    {
        LogPrintf(0x2000001, "cannot load _PrivilegedIP Table");
        return FailLoadModuleVersions(pModules, pFiles);
    }

    ShardService_BuildMap();
    DownloadServerList_Build();
    if (!CArticleManager_ReloadFromDB())
        return false;
    DB_RefreshShardServiceType(0);
    MonitorGraph_AddCounterLocked(GATEWAY_COUNTER_SESSION_CREATED, "Connection Established",
                                  NewStatCounter(0));
    MonitorGraph_AddCounterLocked(GATEWAY_COUNTER_SESSION_LOST, "Connection Lost",
                                  NewStatCounter(1));
    MonitorGraph_AddCounterLocked(GATEWAY_COUNTER_CERTIFY_TIME, "Certify Elapsed Time Avg",
                                  NewStatCounter(2));

    LogPrintf(0, "IBUV Initialization Start Now!");
    DWORD dwReserveCount;
    const char* pszValue = ServerConfig_FindExtraValue("IBUVQueueReserveCount");
    if (pszValue)
        dwReserveCount = atol(pszValue);
    else
        dwReserveCount = 20000;
    float fPrepareRatio;
    pszValue = ServerConfig_FindExtraValue("IBUVQueuePrepareRatio");
    if (pszValue)
        fPrepareRatio = (float)atof(pszValue);
    else
        fPrepareRatio = 0.05f;
    char szCharSet[1024] = { 0 };
    pszValue = ServerConfig_FindExtraValue("IBUVCharacterSet");
    if (!pszValue)
    {
        // "Huh! The image certification code was changed, check the settings!!!!" /
        // "image certification initialisation error" (Korean, code page 949)
        static const char s_szText[] =
            "\xc7\xea! \xc0\xcc\xb9\xcc\xc1\xf6 \xc0\xce\xc1\xf5 \xc4\xda\xb5\xe5 \xba\xaf"
            "\xb0\xe6\xb5\xc8 \xb3\xd1\xc0\xcc \xbf\xc3\xb6\xf3\xbf\xd4\xb4\xc2\xb5\xa5 \xbc"
            "\xbc\xc6\xc3\xc8\xae\xc0\xce\xc7\xd8\xba\xb8\xbb\xef!!!!";
        static const char s_szCaption[] =
            "\xc0\xcc\xb9\xcc\xc1\xf6 \xc0\xce\xc1\xf5 \xc3\xca\xb1\xe2\xc8\xad \xbf\xc0\xb7"
            "\xf9";
        LogPrintf(0x2000001, s_szText);
        MessageBoxA(NULL, s_szText, s_szCaption, MB_OK);
        BSAssertFail();
        return false;
    }
    strcpy_s(szCharSet, sizeof(szCharSet), pszValue);
    int nStringSize;
    pszValue = ServerConfig_FindExtraValue("IBUVStringSize");
    if (pszValue)
        nStringSize = atol(pszValue);
    else
        nStringSize = 6;
    LogPrintf(0x2000001, "IBUV: StringSize: %d, CharSet: %s", nStringSize, szCharSet);
    CTask* pMainTask = g_pBSNet->GetProactor(SERVER_TASK_MAIN);
    if (!pMainTask)
        BSAssertFail();
    if (!IBUVProcess_Initialize(GetSingletonChecked<CIBUVProcess>(), pMainTask, dwReserveCount,
                                fPrepareRatio, nStringSize, szCharSet))
    {
        LogPrintf(0x2000001, "Failed to initialize IBUV Process!!!");
        BSAssertFail();
        return false;
    }
    DWORD dwTolerance;
    pszValue = ServerConfig_FindExtraValue("IBUVFailureIPTolerance");
    if (pszValue)
        dwTolerance = atol(pszValue);
    else
        dwTolerance = 0;
    IBUVIPBlockMgr_SetFailureIPTolerance(GetSingletonChecked<CIBUVIPBlockMgr>(), dwTolerance);
    LogPrintf(0, "IBUV Process Initialization Successfully Finished!");
    SetServerState(4);
    return true;
}

bool CGatewayServer::OnShutdown()
{
    ShardServiceMap_DestroyAll();
    DownloadServerList_Clear();
    ModuleVersion_FreeAllData();
    CDatabase::DestroyAllSessions();
    return false;
}

bool CGatewayServer::InitMonitorCounters()
{
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_MSG);
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_SESSION);
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_SOCK_TCP);
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_CLASS_LINK);
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_ACCEPT);
    MonitorGraph_RegisterNetEngineCounter(MONITOR_COUNTER_OVERLAP);
    return true;
}
