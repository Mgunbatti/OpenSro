#include "UserCertifier.h"

#include <string.h>

#include "BSNet/IBSNet.h"
#include "ServerFramework/CertData.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerProcess.h"
#include "CertifyDB.h"
#include "GatewayServer.h"
#include "GatewayMainProcess.h"
#include "PrivilegedIPManager.h"
#include "ShardConditionManager.h"

using namespace ServerFramework;

CChunkAllocatorST<CUserCertifier_OverlapJob> g_UserCertifierJobPool;
CChunkAllocatorST<sCertifyParam> g_CertifyParamPool;

// 0xA102 {byResult, byCode} to the session.
static void SendLoginAck(DWORD dwSessionID, BYTE byResult, BYTE byCode)
{
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(GATEWAY_MSG_LOGIN_ACK);
    pReply->WriteBytes(&byResult, 1);
    pReply->WriteBytes(&byCode, 1);
    g_pBSNet->SendMsg(dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
}

// The GlobalManager of this server's division.
static WORD GlobalManagerBodyID()
{
    return g_pSelfBody->pDivision->wGlobalManagerID;
}

// ---------------------------------------------------------------------------------------------
// Records

void CertParam_Clear(sCertifyParam* pParam)
{
    pParam->dwType = 0;
    pParam->dw0 = 0;
    pParam->strID.clear();
    pParam->str24.clear();
    pParam->dwSessionID = 0;
    pParam->pAgentBody = NULL;
    pParam->pFreeParam1 = NULL;
    pParam->dw268 = 0;
    pParam->w26C = 0;
    pParam->dw270 = 0;
    pParam->wstrIBUVCode.clear();
}

sCertifyParam* CertParam_Alloc()
{
    sCertifyParam* pParam = g_CertifyParamPool.Alloc();
    if (pParam)
        CertParam_Clear(pParam);
    return pParam;
}

void CertParam_Release(sCertifyParam** ppParam)
{
    sCertifyParam* pParam = *ppParam;
    if (!pParam)
        return;
    if (pParam->dwType == 2 || pParam->dwType == 24)
    {
        if (pParam->pFreeParam1)
        {
            delete pParam->pFreeParam1;
            (*ppParam)->pFreeParam1 = NULL;
        }
    }
    g_CertifyParamPool.Free(*ppParam);
    *ppParam = NULL;
}

// ---------------------------------------------------------------------------------------------
// CUserCertifier_OverlapJob

void CUserCertifier_OverlapJob::ReturnToPool()
{
    if (m_dwID)
    {
        ReleaseId();
        g_UserCertifierJobPool.Free(this);
    }
}

bool CUserCertifier_OverlapJob::Setup(CTask* pOwner, DWORD dwProcessIndex, DWORD dwSessionID,
                                      const char* pszID, const char* pszPassword,
                                      BYTE byContentID, WORD wShardID)
{
    Init(pOwner);
    ULONG ulIP = 0;
    if (!g_pBSNet->GetSessionPeerAddress(dwSessionID, NULL, &ulIP, NULL))
        return false;

    m_dwProcessIndex = dwProcessIndex;
    m_wShardID = wShardID;
    CertParam_Release(&m_pCertParam);
    m_pCertParam = NULL;
    m_pCertParam = CertParam_Alloc();
    CertParam_Clear(m_pCertParam);
    DWORD dwType = byContentID;
    m_pCertParam->dwType = dwType;
    m_pCertParam->strID.assign(pszID, strlen(pszID));
    m_pCertParam->str24.assign(pszPassword, strlen(pszPassword));
    m_pCertParam->dwSessionID = dwSessionID;
    m_pCertParam->dw258 = ulIP;
    m_dwCertifyStart = GetTickCount();
    m_dwCertifyElapsed = 0;
    if (dwType == 2 || dwType == 24)
        m_pCertParam->pFreeParam1 = new sCertifyFreeParam;
    return true;
}

bool CUserCertifier_OverlapJob::IsSessionStillValid()
{
    if (!m_pCertParam)
        return false;
    if (g_pBSNet->GetSessionSocketState(m_pCertParam->dwSessionID) == -1)
        return false;
    ULONG ulIP = 0;
    if (!g_pBSNet->GetSessionPeerAddress(m_pCertParam->dwSessionID, NULL, &ulIP, NULL))
        return false;
    if (ulIP != m_pCertParam->dw258)
    {
        g_pBSNet->DisconnectSession(m_pCertParam->dwSessionID, 1);
        return false;
    }
    return true;
}

bool CUserCertifier_OverlapJob::Prepare()
{
    return IsSessionStillValid();
}

bool CUserCertifier_OverlapJob::Execute()
{
    if (!m_pCertParam)
        return false;
    m_nResult = UserCertifier_GetHandler(m_dwProcessIndex, m_pCertParam);
    return m_nResult == CERTIFY_RESULT_OK;
}

// The account was certified: the login goes to an AgentServer of the shard through the
// GlobalManager, 0x6200 {DWORD address, DWORD JID, BYTE content of the shard}, or the client gets
// 0xA102 {2, code}: 4 no AgentServer, 6 no age check record, 10 / 11 too young for the shard
// (11: test shard, name with "#$T"), 5 shard full, 13 GM account without privileged address.
// BUG(GWCERT-002): when the relay fails the record is neither handed on nor released.
bool CUserCertifier_OverlapJob::OnComplete()
{
    m_dwCertifyElapsed = GetTickCount() - m_dwCertifyStart;
    UserCertifier_AddTimingStat(m_dwCertifyElapsed);
    if (IsSessionStillValid())
    {
        m_pCertParam->pAgentBody = ShardService_PickAgentServerRR(m_wShardID);
        const BYTE* pbyContent = NULL;
        BYTE byCode = CheckAgentLogin(&pbyContent);
        if (!byCode)
        {
            g_pBSNet->GetSessionPeerAddress(m_pCertParam->dwSessionID, NULL,
                                            &m_pCertParam->dw258, &m_pCertParam->wPort);
            CMsg* pLogin = g_pBSNet->AllocMsg(FALSE);
            pLogin->SetOpcode(GATEWAY_MSG_LOGIN_GRANT_REQ);
            pLogin->WriteBytes(&m_pCertParam->dw258, 4);
            pLogin->WriteBytes(&m_pCertParam->dw0, 4);
            pLogin->WriteBytes(pbyContent, 1);
            CServerProcessBase* pProcess = (CServerProcessBase*)m_pOwner->GetJob();
            if (!RelayMsgToBody_6008(pProcess, pLogin, GlobalManagerBodyID(), 0, 0,
                                     (DWORD)m_pCertParam))
            {
                LogPrintf(0x2000000, "[MultiprocessUserCertifier] RelayMsg Failed");
                SendLoginAck(m_pCertParam->dwSessionID, 2, 6);
            }
            m_pCertParam = NULL;
            return true;
        }
        SendLoginAck(m_pCertParam->dwSessionID, 2, byCode);
    }
    CertParam_Release(&m_pCertParam);
    m_pCertParam = NULL;
    return false;
}

// The checks of ForwardLoginToAgentServer before the hand-over; 0 when the login may go on, then
// *ppbyContent points to the content ID of the AgentServer's shard.
BYTE CUserCertifier_OverlapJob::CheckAgentLogin(const BYTE** ppbyContent)
{
    SERVER_BODY* pAgent = m_pCertParam->pAgentBody;
    CERT_SHARD* pShard;
    if (!pAgent || !(pShard = pAgent->pShard))
        return 4;
    const BYTE* pbyContent = &pShard->ab3[0];
    *ppbyContent = pbyContent;
    if (*pbyContent == 2 || *pbyContent == 24)
    {
        if (!strstr(m_pCertParam->strID.c_str(), "#$"))
        {
            if (!m_pCertParam || !m_pCertParam->pFreeParam1)
            {
                LogPrintf(0x2000000, "[MultiprocessUserCertifier] m_pCertParam == NULL || "
                                     "m_pCertParam->dwFreeParam1 == NULL");
                return 6;
            }
            BOOL bTestShard = strstr((const char*)&pShard->ab3[1], "#$T") != NULL;
            int nAge = m_pCertParam->pFreeParam1->nAge;
            if (m_pCertParam->dwType != 24)
            {
                if (nAge < 15)
                    return (BYTE)(bTestShard + 10);
                if (nAge < 18 && !bTestShard)
                    return 10;
            }
        }
    }
    if (pShard->w557 >= pShard->w548)
    {
        // The original passes the content security group and the setting value in this order.
        DWORD dwIgnore = CertData_GetIgnoreShardMaxUserValue();
        if (!PrivIPMap_ContainsIP(m_pCertParam->b41, dwIgnore))
            return 5;
    }
    if ((m_pCertParam->b41 != 3 || m_pCertParam->b40 != 3)
        && !IsPrivilegedIP(m_pCertParam->dw258))
    {
        std::string strIP;
        IPv4ToString(&strIP, m_pCertParam->dw258);
        LogPrintf(0x2000000,
                  "[%s] login refused (GM cannot login from the PC that has not privileged ip) - "
                  "%s",
                  m_pCertParam->strID.c_str(), strIP.c_str());
        return 13;
    }
    return 0;
}

// The handler failed: a wrong password goes to the GlobalManager (0x6111 {ID, 0}), which counts
// the failures and answers with 0xA111; everything else is answered here.
// BUG(GWCERT-002): the record is dropped without CertParam_Release.
bool CUserCertifier_OverlapJob::OnFail()
{
    m_dwCertifyElapsed = GetTickCount() - m_dwCertifyStart;
    UserCertifier_AddTimingStat(m_dwCertifyElapsed);
    if (!IsSessionStillValid())
        return false;

    if (m_nResult == CERTIFY_RESULT_WRONG_PASSWORD)
    {
        CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
        pMsg->SetOpcode(GATEWAY_MSG_VALIDATION_FAIL_REQ);
        BYTE byKind = 0;
        *pMsg << m_pCertParam->strID.c_str();
        pMsg->WriteBytes(&byKind, 1);
        CServerProcessBase* pProcess = (CServerProcessBase*)m_pOwner->GetJob();
        if (!RelayMsgToBody_6008(pProcess, pMsg, GlobalManagerBodyID(), 0, 0,
                                 (DWORD)m_pCertParam))
        {
            LogPrintf(0x2000000, "[MultiprocessUserCertifier] RelayMsg Failed");
            SendLoginAck(m_pCertParam->dwSessionID, 2, 6);
            m_pCertParam = NULL;
            return true;
        }
    }
    else
    {
        SendLoginFailure();
    }
    m_pCertParam = NULL;
    return true;
}

void CUserCertifier_OverlapJob::SendLoginFailure()
{
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(GATEWAY_MSG_LOGIN_ACK);
    BYTE byResult = 2;
    BYTE byCode;
    BYTE byDetail;
    switch (m_nResult)
    {
    case CERTIFY_RESULT_2:
        byCode = 2;
        byDetail = 2;
        pReply->WriteBytes(&byResult, 1);
        pReply->WriteBytes(&byCode, 1);
        pReply->WriteBytes(&byDetail, 1);
        break;
    case CERTIFY_RESULT_NO_ACCOUNT:
        byCode = 2;
        byDetail = 3;
        pReply->WriteBytes(&byResult, 1);
        pReply->WriteBytes(&byCode, 1);
        pReply->WriteBytes(&byDetail, 1);
        LogPrintf(0x1000000, "there is no account info [%s]", m_pCertParam->strID.c_str());
        break;
    case CERTIFY_RESULT_BLOCKED:
        byCode = 2;
        byDetail = 1;
        pReply->WriteBytes(&byResult, 1);
        pReply->WriteBytes(&byCode, 1);
        pReply->WriteBytes(&byDetail, 1);
        *pReply << m_pCertParam->szBlockReason;
        pReply->WriteBytes(m_pCertParam->abBlockEnd, 16);
        LogPrintf(0x1000000, "user blocked [%s]", m_pCertParam->strID.c_str());
        break;
    default:
        LogPrintf(0x2000000, "cannot certify user : unstable server condition (%d)", m_nResult);
        byCode = 6;
        pReply->WriteBytes(&byResult, 1);
        pReply->WriteBytes(&byCode, 1);
        break;
    }
    g_pBSNet->SendMsg(m_pCertParam->dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
}

// ---------------------------------------------------------------------------------------------
// Certifier processes

CUserCertifier_OverlapJob* UserCertifier_AllocJob()
{
    CUserCertifier_OverlapJob* pJob = g_UserCertifierJobPool.Alloc();
    pJob->AcquireId();
    return pJob;
}

bool UserCertifier_InitProcessOverlapPool()
{
    for (int i = 0; i < 30; ++i)
    {
        DWORD dwTaskID = GATEWAY_TASK_CERTIFIER_FIRST + i;
        if (!g_pBSNet->RegisterProactor(dwTaskID, NULL,
                                        &CServerProcessOverlap::classCServerProcessOverlap, NULL)
            || !g_pBSNet->StartProactorWorkers(dwTaskID, 1, 0, 0, 0, NULL))
        {
            return false;
        }
        std::pair<DWORD, CTask*> entry(dwTaskID, g_pBSNet->GetProactor(dwTaskID));
        g_CertifierProcessPool.mapTask.insert(entry);
    }
    g_CertifierProcessPool.itNext = g_CertifierProcessPool.mapTask.begin();
    g_CertifierProcessPool.dwElapsedSum = 0;
    g_CertifierProcessPool.dwElapsedCount = 0;
    return true;
}

CTask* UserCertifier_NextProcess(DWORD* pdwTaskID)
{
    ++g_CertifierProcessPool.itNext;
    if (g_CertifierProcessPool.itNext == g_CertifierProcessPool.mapTask.end())
        g_CertifierProcessPool.itNext = g_CertifierProcessPool.mapTask.begin();
    *pdwTaskID = g_CertifierProcessPool.itNext->first;
    return g_CertifierProcessPool.itNext->second;
}

int UserCertifier_StartJob(CTask* pOwnerTask, DWORD dwSessionID, const char* pszID,
                           const char* pszPassword, BYTE byContentID, WORD wShardID)
{
    DWORD dwTaskID;
    CTask* pTask = UserCertifier_NextProcess(&dwTaskID);
    CUserCertifier_OverlapJob* pJob = UserCertifier_AllocJob();
    if (!pJob->Setup(pOwnerTask, dwTaskID - GATEWAY_TASK_CERTIFIER_FIRST, dwSessionID, pszID,
                     pszPassword, byContentID, wShardID))
    {
        pJob->ReturnToPool();
        return CERTIFY_RESULT_ERROR;
    }
    CServerProcessBase* pProcess = (CServerProcessBase*)pOwnerTask->GetJob();
    if (!pProcess->SendOverlapJob(pJob, pTask))
    {
        pJob->ReturnToPool();
        return CERTIFY_RESULT_ERROR;
    }
    return 0;
}

void UserCertifier_AddTimingStat(DWORD dwElapsed)
{
    CSingleLockBS lock(&g_CertifierProcessPool.csStat, TRUE);
    g_CertifierProcessPool.dwElapsedSum += dwElapsed;
    ++g_CertifierProcessPool.dwElapsedCount;
}
