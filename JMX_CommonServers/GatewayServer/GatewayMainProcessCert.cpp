// The members of CGatewayMainProcess (GatewayMainProcess.h) and CGatewayServer (GatewayServer.h)
// that belong to the certification part: the login request, the login answer of the
// GlobalManager, the captcha messages and the row triggers of the start-up queries.

#include "GatewayMainProcess.h"

#include <string.h>

#include "BSLib/DB/DBQuery.h"
#include "BSNet/IBSNet.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerDataExtern.h"
#include "GatewayServer.h"
#include "IBUVProcess.h"
#include "PrivilegedIPManager.h"
#include "ShardConditionManager.h"
#include "UserCertifier.h"

using namespace ServerFramework;

static void WriteByte(CMsg* pMsg, BYTE b)
{
    pMsg->WriteBytes(&b, 1);
}

// 0xA102 {byResult, byCode} to the session and the message back to the engine.
static void SendLoginAck(DWORD dwSessionID, BOOL bEncrypted, BYTE byResult, BYTE byCode)
{
    CMsg* pReply = g_pBSNet->AllocMsg(bEncrypted);
    pReply->SetOpcode(GATEWAY_MSG_LOGIN_ACK);
    WriteByte(pReply, byResult);
    WriteByte(pReply, byCode);
    g_pBSNet->SendMsg(dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
}

// ---------------------------------------------------------------------------------------------
// Login

// {BYTE content, string ID, string password, WORD shard}: the login is certified by a certifier
// process (no answer here) or refused with 0xA102: {2, 7} unknown shard or content, or no client
// address; {2, 6} address blocked for failed captchas or the job could not be started;
// {2, 2, 2} the shard is not in service and the client is not privileged.
void CGatewayMainProcess::OnMsg_6102(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    if (GatewayMainProcess_IsClientRequestBlocked())
    {
        pMsg->SkipRemaining();
        g_pBSNet->DisconnectSession(pMsg->m_dwSessionID, 1);
        return;
    }

    BYTE byContentID = 0;
    std::string strID;
    std::string strPassword;
    WORD wShardID = 0;
    *pMsg >> byContentID;
    *pMsg >> strID;
    *pMsg >> strPassword;
    *pMsg >> wShardID;
    String_ReplaceQuotesWithSpace(&strID);
    if (_strlwr_s((char*)strID.c_str(), strID.size() + 1))
    {
        BSAssertFail();
        return;
    }

    std::map<WORD, CERT_SHARD*>::iterator itShard = g_mapCertShard.find(wShardID);
    CERT_SHARD* pShard = itShard != g_mapCertShard.end() ? itShard->second : NULL;
    SHARD_CONDITION* pCondition = ShardMap_Find(wShardID);
    ULONG ulIP = 0;
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(GATEWAY_MSG_LOGIN_ACK);
    if (!pShard || pShard->ab3[0] != byContentID || !pCondition
        || !g_pBSNet->GetSessionPeerAddress(pMsg->m_dwSessionID, NULL, &ulIP, NULL))
    {
        WriteByte(pReply, 2);
        WriteByte(pReply, 7);
    }
    else
    {
        CIBUVIPBlockMgr* pBlockMgr = CSingletonT<CIBUVIPBlockMgr>::GetInstance();
        if (!pBlockMgr)
            BSAssertFail();
        if (pBlockMgr->IsBlocked(ulIP))
        {
            WriteByte(pReply, 2);
            WriteByte(pReply, 6);
        }
        else if (!IsPrivilegedIP(ulIP) && pCondition->byServiceType)
        {
            WriteByte(pReply, 2);
            WriteByte(pReply, 2);
            WriteByte(pReply, 2);
        }
        else
        {
            if (!UserCertifier_StartJob((CTask*)m_pOwner, pMsg->m_dwSessionID, strID.c_str(),
                                        strPassword.c_str(), byContentID, wShardID))
            {
                g_pBSNet->ReleaseMsg(pReply);
                return;
            }
            WriteByte(pReply, 2);
            WriteByte(pReply, 6);
        }
    }
    g_pBSNet->SendMsg(pMsg->m_dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
}

// Answer of the login hand-over (context: the record). {BYTE 1, DWORD token, WORD AgentServer
// body}: 0xA102 {1, token, address of the AgentServer (the private one for clients in
// 192.168.0.x), WORD port}; otherwise 0xA102 {2, 6} and the session is closed.
void CGatewayMainProcess::OnMsg_A203(CMsg* pMsg, DWORD, DWORD dwContext, CMassiveMsg*)
{
    BYTE byResult;
    *pMsg >> byResult;
    sCertifyParam* pParam = (sCertifyParam*)dwContext;
    if (!pParam)
        return;
    CMsg* pReply = g_pBSNet->AllocMsg(TRUE);
    pReply->SetOpcode(GATEWAY_MSG_LOGIN_ACK);
    if (byResult == 1)
    {
        DWORD dwToken;
        WORD wBodyID;
        *pMsg >> dwToken;
        *pMsg >> wBodyID;
        SERVER_BODY* pBody = ServerBodyInfo_Find(wBodyID);
        if (!pBody)
            BSAssertFail();
        byResult = 1;
        const char* pszIP;
        if ((pParam->dw258 & 0xFFFFFF) == 0xA8C0)
        {
            pReply->WriteBytes(&byResult, 1);
            pReply->WriteBytes(&dwToken, 4);
            pszIP = pBody->pMachine->szPrivateIP;
        }
        else
        {
            pReply->WriteBytes(&byResult, 1);
            pReply->WriteBytes(&dwToken, 4);
            pszIP = pBody->pMachine->szPublicIP;
        }
        *pReply << pszIP;
        pReply->WriteBytes(&pBody->wPort, 2);
        g_pBSNet->SendMsg(pParam->dwSessionID, pReply);
        g_pBSNet->ReleaseMsg(pReply);
        CertParam_Release(&pParam);
        return;
    }
    WriteByte(pReply, 2);
    WriteByte(pReply, 6);
    g_pBSNet->SendMsg(pParam->dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
    g_pBSNet->DisconnectSession(pParam->dwSessionID, 1);
    CertParam_Release(&pParam);
}

// ---------------------------------------------------------------------------------------------
// Captcha

void RequestIBUVImage(sCertifyParam* pParam)
{
    if (!pParam)
        BSAssertFail();
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(IBUV_MSG_IMAGE_REQ);
    pMsg->WriteBytes(&pParam, 4);
    CIBUVProcess* pProcess = CSingletonT<CIBUVProcess>::GetInstance();
    if (!pProcess)
        BSAssertFail();
    CTask* pTask = (CTask*)CSingletonT<CIBUVProcess>::GetInstance()->GetOwner();
    pTask->m_pQueue->Push(pMsg);
}

// Image of the IBUV process {DWORD record, BYTE result, (1:) BYTE, wstring code, WORD size,
// image}: the code waits for the answer and the client gets 0x2322 {BYTE, WORD size, image}.
// Otherwise 0xA102 {2, 6}, the session is closed and the record released.
void CGatewayMainProcess::OnMsg_A321(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwParam;
    BYTE byResult;
    *pMsg >> dwParam;
    *pMsg >> byResult;
    sCertifyParam* pParam = (sCertifyParam*)dwParam;
    if (!pParam)
    {
        LogPrintf(0x2000001, "Invalid CertParam Pointer in OnFRAMEWORKMSG_IBUV_GENERATE_ACK");
        return;
    }
    if (byResult == 1)
    {
        BYTE b;
        WORD wSize;
        BYTE abImage[IBUV_IMAGE_SIZE];
        *pMsg >> b;
        *pMsg >> pParam->wstrIBUVCode;
        *pMsg >> wSize;
        pMsg->ReadBytes(abImage, wSize);
        CIBUVTemporalCertParamMgr* pMgr = CSingletonT<CIBUVTemporalCertParamMgr>::GetInstance();
        if (!pMgr)
            BSAssertFail();
        if (CSingletonT<CIBUVTemporalCertParamMgr>::GetInstance()->Register(pParam->dwSessionID,
                                                                             pParam))
        {
            CMsg* pImage = g_pBSNet->AllocMsg(FALSE);
            pImage->SetOpcode(IBUV_MSG_IMAGE_TO_USER);
            pImage->WriteBytes(&b, 1);
            pImage->WriteBytes(&wSize, 2);
            pImage->WriteBytes(abImage, wSize);
            g_pBSNet->SendMsg(pParam->dwSessionID, pImage);
            g_pBSNet->ReleaseMsg(pImage);
            return;
        }
        SendLoginAck(pParam->dwSessionID, TRUE, 2, 6);
        LogPrintf(0x2000001,
                  "Duplicated IBUV CertParam Registration Request!, User Info (Session: %d, "
                  "Account: %s)",
                  pParam->dwSessionID, pParam->strID.c_str());
    }
    else
    {
        SendLoginAck(pParam->dwSessionID, TRUE, 2, 6);
        LogPrintf(0x2000001, "IBUV GENERATE Failed, User Info (%d, %s)", pParam->dw0,
                  pParam->strID.c_str());
    }
    g_pBSNet->DisconnectSession(pParam->dwSessionID, 1);
    CertParam_Release(&pParam);
}

// The client's captcha answer {string}: right, 0xA323 {1} and 0x6112 {ID} to the GlobalManager;
// wrong, one more failure for the address and 0x6111 {ID, 1}. The GlobalManager answers with
// 0xA112 / 0xA111. A failed relay gives 0xA102 {2, 6}.
// BUG(GWCERT-002): after a failed relay of a wrong answer the record is never released and the
// session stays open.
void CGatewayMainProcess::OnMsg_6323(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strAnswer;
    *pMsg >> strAnswer;
    CIBUVTemporalCertParamMgr* pMgr = CSingletonT<CIBUVTemporalCertParamMgr>::GetInstance();
    if (!pMgr)
        BSAssertFail();
    DWORD dwSessionID = pMsg->m_dwSessionID;
    sCertifyParam* pParam =
        CSingletonT<CIBUVTemporalCertParamMgr>::GetInstance()->FindBySession(dwSessionID);
    if (!pParam)
    {
        SendLoginAck(pMsg->m_dwSessionID, TRUE, 2, 6);
        LogPrintf(0x2000001,
                  "FRAMEWORKMSG_IMAGE_BASED_USER_VALIDATE_REQ Failed, No Registered CertParam!!");
        g_pBSNet->DisconnectSession(pMsg->m_dwSessionID, 1);
        return;
    }

    if (!CSingletonT<CIBUVTemporalCertParamMgr>::GetInstance())
        BSAssertFail();
    CSingletonT<CIBUVTemporalCertParamMgr>::GetInstance()->Remove(pParam->dwSessionID);
    std::wstring wstrCode(pParam->wstrIBUVCode, 0, std::wstring::npos);
    pParam->wstrIBUVCode.clear();
    WCHAR szAnswer[255];
    memset(szAnswer, 0, sizeof(szAnswer));
    CIBUVProcess* pProcess = CSingletonT<CIBUVProcess>::GetInstance();
    if (!pProcess)
        BSAssertFail();
    MultiByteToWideChar(CSingletonT<CIBUVProcess>::GetInstance()->m_dwCodePage, 0,
                        strAnswer.c_str(), (int)strAnswer.size(), szAnswer, 255);
    if (wstrCode.compare(0, wstrCode.size(), szAnswer, wcslen(szAnswer)) == 0)
    {
        CMsg* pAck = g_pBSNet->AllocMsg(FALSE);
        pAck->SetOpcode(GATEWAY_MSG_IBUV_ANSWER_ACK);
        WriteByte(pAck, 1);
        g_pBSNet->SendMsg(pMsg->m_dwSessionID, pAck);
        g_pBSNet->ReleaseMsg(pAck);

        CMsg* pRelay = g_pBSNet->AllocMsg(FALSE);
        pRelay->SetOpcode(GATEWAY_MSG_VALIDATION_OK_REQ);
        *pRelay << pParam->strID.c_str();
        if (!RelayMsgToBody_6008(this, pRelay, g_pSelfBody->pDivision->wGlobalManagerID, 0, 0,
                                 (DWORD)pParam))
        {
            LogPrintf(0x2000000, "[OnFRAMEWORKMSG_IMAGE_BASED_USER_VALIDATE_REQ] RelayMsg Failed");
            SendLoginAck(pParam->dwSessionID, FALSE, 2, 6);
            g_pBSNet->DisconnectSession(pParam->dwSessionID, 1);
            CertParam_Release(&pParam);
        }
    }
    else
    {
        CIBUVIPBlockMgr* pBlockMgr = CSingletonT<CIBUVIPBlockMgr>::GetInstance();
        if (!pBlockMgr)
            BSAssertFail();
        CSingletonT<CIBUVIPBlockMgr>::GetInstance()->AddFailure(pParam->dw258);
        CMsg* pRelay = g_pBSNet->AllocMsg(FALSE);
        pRelay->SetOpcode(GATEWAY_MSG_VALIDATION_FAIL_REQ);
        *pRelay << pParam->strID.c_str();
        WriteByte(pRelay, 1);
        if (!RelayMsgToBody_6008(this, pRelay, g_pSelfBody->pDivision->wGlobalManagerID, 0, 0,
                                 (DWORD)pParam))
        {
            LogPrintf(0x2000000, "[OnFRAMEWORKMSG_IMAGE_BASED_USER_VALIDATE_REQ] RelayMsg Failed");
            SendLoginAck(pParam->dwSessionID, FALSE, 2, 6);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Row triggers of CGatewayServer::OnLocalDataInitialized

// _ModuleVersion (m_dwParam 0) / _ModuleVersionFile (1) rows: a zeroed row of the record size
// before each row, the row into the list of m_pUserData after it.
int CGatewayServer::OnModuleVersionRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (!pQuery->m_bRowFetched)
    {
        if (!pQuery->m_dwParam)
        {
            pQuery->m_pBuffer = (BYTE*)operator new(sizeof(MODULE_VERSION));
            pQuery->m_nRowSize = sizeof(MODULE_VERSION);
            memset(pQuery->m_pBuffer, 0, sizeof(MODULE_VERSION));
        }
        else
        {
            pQuery->m_pBuffer = (BYTE*)operator new(sizeof(MODULE_VERSION_FILE));
            pQuery->m_nRowSize = sizeof(MODULE_VERSION_FILE);
            memset(pQuery->m_pBuffer, 0, sizeof(MODULE_VERSION_FILE));
        }
        return 1;
    }
    if (!pQuery->m_dwParam)
    {
        ((ModuleVersionList*)pQuery->m_pUserData)
            ->push_back((MODULE_VERSION*)pQuery->m_pBuffer);
    }
    else
    {
        ((ModuleVersionFileList*)pQuery->m_pUserData)
            ->push_back((MODULE_VERSION_FILE*)pQuery->m_pBuffer);
    }
    return 1;
}

// _PrivilegedIP rows {DWORD first, DWORD last}: the range into g_PrivilegedIPManager.
int CGatewayServer::OnPrivilegedIPRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (!pQuery->m_bRowFetched)
    {
        DWORD* pRow = (DWORD*)pQuery->m_pBuffer;
        pRow[0] = 0;
        pRow[1] = 0;
        return 1;
    }
    g_PrivilegedIPManager.AddRange((DWORD*)pQuery->m_pBuffer);
    return 1;
}

// The same for g_PrivilegedIPManager2.
int CGatewayServer::OnPrivilegedIPRow2(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (!pQuery->m_bRowFetched)
    {
        DWORD* pRow = (DWORD*)pQuery->m_pBuffer;
        pRow[0] = 0;
        pRow[1] = 0;
        return 1;
    }
    g_PrivilegedIPManager2.AddRange((DWORD*)pQuery->m_pBuffer);
    return 1;
}
