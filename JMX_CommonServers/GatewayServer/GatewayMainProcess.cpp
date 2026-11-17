#include "GatewayMainProcess.h"

#include "ArticleManager.h"
#include "CertifierFwd.h"
#include "GatewayServer.h"
#include "ShardConditionManager.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerFramework/ServerApp.h"
#include "ServerFramework/ServerDataExtern.h"

using namespace ServerFramework;

BS_IMPLEMENT_DYNCREATE(CGatewayMainProcess, &CServerProcessMain::classCServerProcessMain)

int GatewayMainProcess_IsClientRequestBlocked()
{
    return 0;
}

bool ClientVersion_IsBelowRequired(CERT_MODULE* pModule, DWORD dwVersion)
{
    std::map<std::string, DWORD>::iterator it =
        g_GatewayServer.m_mapLastFullVersion.find(std::string(pModule->szName));
    if (it == g_GatewayServer.m_mapLastFullVersion.end())
        return false;
    return dwVersion < it->second;
}

// BUG(GW-005): the check meant for double-byte characters tests the byte after the quote, so a
// quote followed by a byte >= 0x80 stays.
void String_ReplaceQuotesWithSpace(std::string* pstr)
{
    char* p = (char*)pstr->c_str();
    if (!p || !*p)
        return;
    do
    {
        if (!(*(WORD*)p & 0x8000) && (*p == '\'' || *p == '"'))
            *p = ' ';
        p = CharNextA(p);
    } while (*p);
}

// CMsg::Read_BYTE (forward only copy) (0x40b660): asserts before it faults on a short message.
static BYTE ReadByteForwardOnly(CMsg* pMsg)
{
    if (pMsg->m_wReadPos + 1 > pMsg->m_wWritePos)
    {
        BSAssertFail();
        BS_CRASH();
    }
    BYTE b = pMsg->m_pData[pMsg->m_wReadPos];
    ++pMsg->m_wReadPos;
    return b;
}

static void WriteResult(CMassiveMsg* pReply, BYTE byResult, BYTE byCode)
{
    pReply->Write(&byResult, 1);
    pReply->Write(&byCode, 1);
}

// 0xA102 {2, byCode} to the client of the login, then the login ends.
static void FailLogin(CERTIFY_PARAM* pParam, BYTE byCode)
{
    CMsg* pReply = g_pBSNet->AllocMsg(TRUE);
    pReply->SetOpcode(GATEWAY_MSG_LOGIN_ACK);
    BYTE byResult = 2;
    pReply->WriteBytes(&byResult, 1);
    pReply->WriteBytes(&byCode, 1);
    g_pBSNet->SendMsg(pParam->dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
    g_pBSNet->DisconnectSession(pParam->dwSessionID, 1);
    CertParam_Release(&pParam);
}

// ---------------------------------------------------------------------------------------------
// CGatewayMainProcess

CGatewayMainProcess::CGatewayMainProcess()
{
}

CGatewayMainProcess::~CGatewayMainProcess()
{
}

BOOL CGatewayMainProcess::Initialize(void* pOwner, void* pParam)
{
    if (CServerProcessBase::Initialize(pOwner, pParam) != TRUE)
        return FALSE;
    {
        std::string strHost("");
        m_UdpPing.Open(strHost);
    }
    if (m_UdpPing.Resume())
        LogPrintf(0, "UDP Ping Service was Started!");
    else
        LogPrintf(0x2000000, "Failed To Start UDP Ping Service!");
    return TRUE;
}

int CGatewayMainProcess::SignalStop()
{
    m_UdpPing.Suspend();
    m_UdpPing.Stop();
    return CServerProcessBase::SignalStop();
}

void CGatewayMainProcess::InitMsgHandlers()
{
    CServerProcessMain::InitMsgHandlers();
    RegisterHandler(0x1001, &CGatewayMainProcess::OnSessionCreated);
    RegisterHandler(0x1002, &CGatewayMainProcess::OnSessionLost);
    RegisterHandler(GATEWAY_MSG_VERSION_REQ, &CGatewayMainProcess::OnMsg_6100);
    RegisterHandler(GATEWAY_MSG_SHARD_LIST_REQ, &CGatewayMainProcess::OnMsg_6101);
    RegisterHandler(GATEWAY_MSG_LOGIN_REQ, &CGatewayMainProcess::OnMsg_6102);
    RegisterHandler(GATEWAY_MSG_FIND_USER_ACK, &CGatewayMainProcess::OnMsg_A200);
    RegisterHandler(GATEWAY_MSG_LOGIN_GRANTED_ACK, &CGatewayMainProcess::OnMsg_A203);
    RegisterHandler(GATEWAY_MSG_MODULE_VERSION_ACK, &CGatewayMainProcess::OnMsg_A306);
    RegisterHandler(GATEWAY_MSG_RELOAD_ARTICLE_REQ, &CGatewayMainProcess::OnMsg_6307);
    RegisterHandler(GATEWAY_MSG_NOTICE_REQ, &CGatewayMainProcess::OnMsg_6104);
    RegisterHandler(GATEWAY_MSG_SHARD_USERS, &CGatewayMainProcess::OnMsg_6105);
    RegisterHandler(GATEWAY_MSG_PATCH_DONE_REQ, &CGatewayMainProcess::OnMsg_6308);
    RegisterHandler(GATEWAY_MSG_SHARD_SERVICE_REQ, &CGatewayMainProcess::OnMsg_6312);
    RegisterHandler(GATEWAY_MSG_FARM_MANAGER_REQ, &CGatewayMainProcess::OnMsg_6106);
    RegisterHandler(GATEWAY_MSG_IBUV_ACK, &CGatewayMainProcess::OnMsg_A321);
    RegisterHandler(GATEWAY_MSG_IBUV_ANSWER_REQ, &CGatewayMainProcess::OnMsg_6323);
    RegisterHandler(GATEWAY_MSG_VALIDATION_FAIL_ACK, &CGatewayMainProcess::OnMsg_A111);
    RegisterHandler(GATEWAY_MSG_VALIDATION_OK_ACK, &CGatewayMainProcess::OnMsg_A112);
}

void CGatewayMainProcess::OnRelayFailed(WORD wOpcode, DWORD dwContext)
{
    if (wOpcode > GATEWAY_MSG_AGENT_LOGIN_REQ)
    {
        if (wOpcode != GATEWAY_MSG_FIND_USER_ACK)
        {
            LogPrintf(0x1000000, "unhandled relay msg error : wRealMsgID : 0x%X : dwParam : 0x%X",
                      wOpcode, dwContext);
            return;
        }
    }
    else if (wOpcode != GATEWAY_MSG_AGENT_LOGIN_REQ)
    {
        if (wOpcode == GATEWAY_MSG_VALIDATION_FAIL_REQ)
        {
            LogPrintf(0x1000000, "Unhandled Process User Validation Failure Req!");
        }
        else if (wOpcode == GATEWAY_MSG_VALIDATION_OK_REQ)
        {
            LogPrintf(0x1000000, "Unhandled Process User Validation Success Req!");
        }
        else
        {
            LogPrintf(0x1000000, "unhandled relay msg error : wRealMsgID : 0x%X : dwParam : 0x%X",
                      wOpcode, dwContext);
            return;
        }
    }
    FailLogin((CERTIFY_PARAM*)dwContext, 6);
}

void CGatewayMainProcess::OnSessionCreated(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                           CMassiveMsg* pMassive)
{
    CServerProcessMain::OnSessionCreated(pMsg, dwRelayID, dwContext, pMassive);
    ++g_dwGatewaySessionCreated;
}

void CGatewayMainProcess::OnSessionLost(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                        CMassiveMsg* pMassive)
{
    CServerProcessMain::OnSessionLost(pMsg, dwRelayID, dwContext, pMassive);
    ++g_dwGatewaySessionLost;
    DWORD dwSessionID = pMsg->m_dwSessionID;
    CIBUVTemporalCertParamMgr* pMgr = CSingletonT<CIBUVTemporalCertParamMgr>::GetInstance();
    if (!pMgr)
        BSAssertFail();
    IBUVTemporalCertParamMgr_Remove(CSingletonT<CIBUVTemporalCertParamMgr>::GetInstance(),
                                    dwSessionID);
}

void CGatewayMainProcess::OnServerBodyStateChanged(SERVER_BODY* pBody, int)
{
    if (pBody->pShard)
        ShardMap_RecomputeReadyFlags();
}

// ---------------------------------------------------------------------------------------------
// Client packets

// 0xA100: {1} the version is current; {2, 4} the gateway is not in service; {2, 1} unknown
// module or version newer than the latest; {2, 5} too old to be patched; {2, 3} no download
// server; {2, 2, string address, WORD port, DWORD latest version, patch files} patch needed.
void CGatewayMainProcess::OnMsg_6100(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strModule;
    std::string strIP;
    if (GatewayMainProcess_IsClientRequestBlocked())
    {
        pMsg->SkipRemaining();
        g_pBSNet->DisconnectSession(pMsg->m_dwSessionID, 1);
        return;
    }
    if (pMsg->GetUnreadSize() < 1)
    {
        LogPrintf(0x1000000, "certificate client module : invalid msg data (0)");
        pMsg->SkipRemaining();
        g_pBSNet->DisconnectSession(pMsg->m_dwSessionID, 1);
        return;
    }
    BYTE byContentID;
    *pMsg >> byContentID;
    if (pMsg->GetUnreadSize() < 3)
    {
        LogPrintf(0x1000000, "certificate client module : invalid msg data (1)");
        pMsg->SkipRemaining();
        g_pBSNet->DisconnectSession(pMsg->m_dwSessionID, 1);
        return;
    }
    *pMsg >> strModule;
    if (pMsg->GetUnreadSize() < 4)
    {
        LogPrintf(0x1000000, "certificate client module : invalid msg data (2)");
        pMsg->SkipRemaining();
        g_pBSNet->DisconnectSession(pMsg->m_dwSessionID, 1);
        return;
    }
    DWORD dwVersion;
    *pMsg >> dwVersion;

    CMassiveMsg* pReply = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pReply->SetOpcode(GATEWAY_MSG_VERSION_ACK);
    if (!g_pSelfBody)
    {
        WriteResult(pReply, 2, 4);
        pReply->SendTo(pMsg->m_dwSessionID, FALSE);
        return;
    }
    ULONG dwIP = 0;
    g_pBSNet->GetSessionPeerAddress(pMsg->m_dwSessionID, &strIP, &dwIP, NULL);
    if (IsPrivilegedIP(dwIP) == 1)
    {
        LogPrintf(0x2000001,
                  "[GatewaySvr] Official IP Detected, Official IP Pass the 'In Service' Check "
                  "(%d.%d.%d.%d)",
                  (BYTE)dwIP, (BYTE)(dwIP >> 8), (BYTE)(dwIP >> 16), (BYTE)(dwIP >> 24));
    }
    else if (g_pSelfBody->nState != 5)
    {
        WriteResult(pReply, 2, 4);
        pReply->SendTo(pMsg->m_dwSessionID, FALSE);
        return;
    }

    CERT_MODULE* pModule = CertModule_FindByName(strModule.c_str());
    if (!pModule || !dwVersion)
    {
        LogPrintf(0, "someone tried to login with unknown client: [content id: %d, module name: %s]",
                  byContentID, strModule.c_str());
        WriteResult(pReply, 2, 1);
        std::string strSource;
        IPv4ToString(&strSource, dwIP);
        LogPrintf(0x2000001, "source ip for trying to exploit : %s", strSource.c_str());
    }
    else
    {
        DWORD dwLatest = ModuleVersion_GetLatest(pModule->byID);
        if (ClientVersion_IsBelowRequired(pModule, dwVersion))
        {
            WriteResult(pReply, 2, 5);
        }
        else if (dwVersion > dwLatest)
        {
            LogPrintf(0,
                      "client requested higher version than the latest version known to server! "
                      "about to exploit?: [contents: %d, module: %s, requested ver: %d, latest "
                      "ver: %d",
                      byContentID, strModule.c_str(), dwVersion, dwLatest);
            WriteResult(pReply, 2, 1);
            LogPrintf(0x2000001, "source ip for trying to exploit : %s", strIP.c_str());
        }
        else if (dwVersion < dwLatest)
        {
            SERVER_BODY* pServer = SelectPatchServer_RoundRobin();
            if (!pServer)
            {
                LogPrintf(0, "can't find any available patch server!!!");
                WriteResult(pReply, 2, 3);
            }
            else
            {
                WriteResult(pReply, 2, 2);
                pReply->WriteString(pServer->pMachine->szPublicIP);
                pReply->Write(&pServer->wPort, 2);
                pReply->Write(&dwLatest, 4);
                ModuleVersion_WritePatchFiles(pReply, pModule->byID, dwVersion);
            }
        }
        else
        {
            BYTE byResult = 1;
            pReply->Write(&byResult, 1);
        }
    }
    pReply->SendTo(pMsg->m_dwSessionID, FALSE);
}

// 0xA101: farms (BYTE 1, BYTE ID, string name)..., BYTE 0, then the shards (ShardList_WriteA101).
void CGatewayMainProcess::OnMsg_6101(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(GATEWAY_MSG_SHARD_LIST_ACK);
    CertFarmList* pFarms = g_pCertFarmList;
    for (CertFarmList::iterator it = pFarms->begin(); it != pFarms->end(); ++it)
    {
        CERT_FARM* pFarm = *it;
        BYTE byMore = 1;
        pReply->WriteBytes(&byMore, 1);
        pReply->WriteBytes(&pFarm->byID, 1);
        *pReply << (const char*)pFarm->ab2;
    }
    BYTE byEnd = 0;
    pReply->WriteBytes(&byEnd, 1);
    ULONG dwIP = 0;
    g_pBSNet->GetSessionPeerAddress(pMsg->m_dwSessionID, NULL, &dwIP, NULL);
    ShardList_WriteA101(pReply, IsPrivilegedIP(dwIP));
    g_pBSNet->SendMsg(pMsg->m_dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
}

// 0xA104 (massive): the notices of the requested type; without one the type of the division
// (1 for division 1, 2 otherwise).
void CGatewayMainProcess::OnMsg_6104(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE byType;
    if (pMsg->GetUnreadSize() >= 1)
        *pMsg >> byType;
    else
        byType = (BYTE)((g_pSelfBody->byDivisionID != 1) + 1);
    CMassiveMsg* pReply = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pReply->SetOpcode(GATEWAY_MSG_NOTICE_ACK);
    g_ArticleManager.WriteArticles(pReply, byType);
    pReply->SendTo(pMsg->m_dwSessionID, FALSE);
}

// BUG(GW-001): an unknown shard ID writes through the NULL of the failed lookup.
void CGatewayMainProcess::OnMsg_6105(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE byCount;
    *pMsg >> byCount;
    for (int n = byCount; n > 0; --n)
    {
        WORD wShardID;
        WORD wUsers;
        *pMsg >> wShardID;
        *pMsg >> wUsers;
        std::map<WORD, CERT_SHARD*>::iterator it = g_mapCertShard.find(wShardID);
        CERT_SHARD* pShard = it == g_mapCertShard.end() ? NULL : it->second;
        pShard->w557 = wUsers;
    }
}

// 0xA106: BYTE count, then (BYTE farm, DWORD address) per FarmManager.
void CGatewayMainProcess::OnMsg_6106(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE byCount = (BYTE)g_listFarmManagerAddr.size();
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(GATEWAY_MSG_FARM_MANAGER_ACK);
    pReply->WriteBytes(&byCount, 1);
    for (std::list<FARM_MANAGER_ADDR>::iterator it = g_listFarmManagerAddr.begin();
         it != g_listFarmManagerAddr.end(); ++it)
    {
        pReply->WriteBytes(&it->byFarmID, 1);
        pReply->WriteBytes(&it->dwIP, 4);
    }
    g_pBSNet->SendMsg(pMsg->m_dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
}

// ---------------------------------------------------------------------------------------------
// Login results (context: the CERTIFY_PARAM of the login)

// {DWORD, BYTE result}: 1 the account needs a captcha; otherwise BYTE error, mapped to 0xA102.
void CGatewayMainProcess::OnMsg_A200(CMsg* pMsg, DWORD, DWORD dwContext, CMassiveMsg*)
{
    DWORD dwValue;
    BYTE byResult;
    *pMsg >> dwValue;
    *pMsg >> byResult;
    CERTIFY_PARAM* pParam = (CERTIFY_PARAM*)dwContext;
    if (!pParam)
        return;
    if (byResult == 1)
    {
        RequestIBUVImage(pParam);
        return;
    }

    BYTE byError = ReadByteForwardOnly(pMsg);
    CMsg* pReply = g_pBSNet->AllocMsg(TRUE);
    pReply->SetOpcode(GATEWAY_MSG_LOGIN_ACK);
    BYTE b = 2;
    pReply->WriteBytes(&b, 1);
    switch (byError)
    {
    case 1:
        b = 3;
        pReply->WriteBytes(&b, 1);
        break;
    case 2:
        b = 8;
        pReply->WriteBytes(&b, 1);
        break;
    case 3:
        b = 2;
        pReply->WriteBytes(&b, 1);
        pReply->WriteBytes(&b, 1);
        break;
    case 4:
        {
            BYTE byKind = 4;
            b = 2;
            pReply->WriteBytes(&b, 1);
            pReply->WriteBytes(&byKind, 1);
        }
        break;
    case 5:
        b = 6;
        pReply->WriteBytes(&b, 1);
        break;
    case 6:
        b = 9;
        pReply->WriteBytes(&b, 1);
        break;
    default:
        LogPrintf(0x1000000, "FIND_CONTENT_USER_ACK: Unknown ErrorCode : %d", byError);
        b = 6;
        pReply->WriteBytes(&b, 1);
        break;
    }
    g_pBSNet->SendMsg(pParam->dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
    g_pBSNet->DisconnectSession(pParam->dwSessionID, 1);
    CertParam_Release(&pParam);
}

// {string ID, BYTE kind, BYTE close, DWORD failures, DWORD maximum}: kind 0 wrong password
// (0xA102 {2, 1, failures, maximum}), kind 1 wrong captcha (0xA323 {2, failures, maximum} and a
// new image unless the session is closed).
void CGatewayMainProcess::OnMsg_A111(CMsg* pMsg, DWORD, DWORD dwContext, CMassiveMsg*)
{
    CERTIFY_PARAM* pParam = (CERTIFY_PARAM*)dwContext;
    std::string strID;
    BYTE byKind;
    BYTE byClose;
    DWORD dwFailures;
    DWORD dwMaximum;
    *pMsg >> strID;
    *pMsg >> byKind;
    *pMsg >> byClose;
    *pMsg >> dwFailures;
    *pMsg >> dwMaximum;
    if (byKind == 0)
    {
        CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
        pReply->SetOpcode(GATEWAY_MSG_LOGIN_ACK);
        BYTE byResult = 2;
        BYTE byError = 1;
        pReply->WriteBytes(&byResult, 1);
        pReply->WriteBytes(&byError, 1);
        pReply->WriteBytes(&dwFailures, 4);
        pReply->WriteBytes(&dwMaximum, 4);
        g_pBSNet->SendMsg(pParam->dwSessionID, pReply);
        g_pBSNet->ReleaseMsg(pReply);
        if (byClose == 1)
        {
            g_pBSNet->DisconnectSession(pParam->dwSessionID, 1);
            CertParam_Release(&pParam);
        }
    }
    else if (byKind == 1)
    {
        CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
        pReply->SetOpcode(GATEWAY_MSG_IBUV_ANSWER_ACK);
        BYTE byResult = 2;
        pReply->WriteBytes(&byResult, 1);
        pReply->WriteBytes(&dwFailures, 4);
        pReply->WriteBytes(&dwMaximum, 4);
        g_pBSNet->SendMsg(pParam->dwSessionID, pReply);
        g_pBSNet->ReleaseMsg(pReply);
        if (byClose == 1)
        {
            g_pBSNet->DisconnectSession(pParam->dwSessionID, 1);
            CertParam_Release(&pParam);
        }
        else
        {
            RequestIBUVImage(pParam);
        }
    }
    else
    {
        BSAssertFail();
    }
}

// {string ID}: the account is valid; the login goes to the AgentServer chosen for it (0x6203
// relayed), or fails with 0xA102 {2, 6} when there is no route.
void CGatewayMainProcess::OnMsg_A112(CMsg* pMsg, DWORD, DWORD dwContext, CMassiveMsg*)
{
    CERTIFY_PARAM* pParam = (CERTIFY_PARAM*)dwContext;
    std::string strID;
    *pMsg >> strID;
    if (!(pParam->strID == strID))
        BSAssertFail();
    CMsg* pLogin = g_pBSNet->AllocMsg(FALSE);
    pLogin->SetOpcode(GATEWAY_MSG_AGENT_LOGIN_REQ);
    pLogin->WriteBytes(&pParam->dw258, 4);
    *pLogin << pParam->strID.c_str() << pParam->str24.c_str();
    pLogin->WriteBytes(&pParam->dw0, 4);
    pLogin->WriteBytes(&pParam->b40, 1);
    pLogin->WriteBytes(&pParam->b41, 1);
    pLogin->WriteBytes(&pParam->w26C, 2);
    pLogin->WriteBytes(&pParam->dw270, 4);
    if (RelayMsgToBody_6008(this, pLogin, pParam->pAgentBody->wID, 0, 0, (DWORD)pParam))
        return;

    CMsg* pReply = g_pBSNet->AllocMsg(TRUE);
    pReply->SetOpcode(GATEWAY_MSG_LOGIN_ACK);
    BYTE byResult = 2;
    BYTE byError = 6;
    pReply->WriteBytes(&byResult, 1);
    pReply->WriteBytes(&byError, 1);
    g_pBSNet->SendMsg(pParam->dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
    LogPrintf(0x2000001,
              "OnFRAMEWORKMSG_PROCESS_USER_VALIDATION_SUCCESS_ACK RelayMsg Failed (AgentSvrBodyID "
              ": %d, User Info (%d, %s)",
              pParam->pAgentBody->wID, pParam->dw0, pParam->strID.c_str());
    g_pBSNet->DisconnectSession(pParam->dwSessionID, 1);
    CertParam_Release(&pParam);
}

// ---------------------------------------------------------------------------------------------
// Server packets

// 0xA306 (massive): BYTE result (2: failed), then the module versions and the patch files of the
// DivisionManager, each as (record, BYTE more)... after a first BYTE more.
void CGatewayMainProcess::OnMsg_A306(CMsg*, DWORD, DWORD, CMassiveMsg* pMassive)
{
    BYTE b;
    pMassive->Read(&b, 1);
    if (b == 2)
    {
        LogPrintf(0x2000000, "cannot retrieve module version data from divisionmanager");
        return;
    }
    ModuleVersionList* pModules = new ModuleVersionList;
    ModuleVersionFileList* pFiles = new ModuleVersionFileList;
    pMassive->Read(&b, 1);
    while (b)
    {
        MODULE_VERSION* pVersion = (MODULE_VERSION*)operator new(sizeof(MODULE_VERSION));
        pMassive->Read(pVersion, sizeof(MODULE_VERSION));
        pMassive->Read(&b, 1);
        pModules->push_back(pVersion);
    }
    pMassive->Read(&b, 1);
    while (b)
    {
        MODULE_VERSION_FILE* pFile =
            (MODULE_VERSION_FILE*)operator new(sizeof(MODULE_VERSION_FILE));
        pMassive->Read(pFile, sizeof(MODULE_VERSION_FILE));
        pMassive->Read(&b, 1);
        pFiles->push_back(pFile);
    }
    ModuleVersion_FreeAllData();
    ModuleVersion_StoreAndIndexLists(pFiles, pModules);
    LogPrintf(0, "retrieve module version data from divisionmanager");
}

// Relayed: reloads the notices, answers 0xA307.
void CGatewayMainProcess::OnMsg_6307(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    CArticleManager_ReloadFromDB();
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(GATEWAY_MSG_RELOAD_ARTICLE_ACK);
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}

// Relayed: the patch files are ready; closed sessions are cleaned up, state 4, 0xA309 {1}.
void CGatewayMainProcess::OnMsg_6308(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    g_pBSNet->CleanupDisconnectedSessions();
    g_pServerApp->SetServerState(4);
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(GATEWAY_MSG_PATCH_DONE_ACK);
    BYTE byResult = 1;
    pReply->WriteBytes(&byResult, 1);
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
    LogPrintf(0, "preparing patch is completed !!!");
}

// Relayed {WORD shard}: reloads the service type of the shard (0: all), answers 0xA312 {1}.
void CGatewayMainProcess::OnMsg_6312(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    WORD wShardID;
    *pMsg >> wShardID;
    DB_RefreshShardServiceType(wShardID);
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(GATEWAY_MSG_SHARD_SERVICE_ACK);
    BYTE byResult = 1;
    pReply->WriteBytes(&byResult, 1);
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}
