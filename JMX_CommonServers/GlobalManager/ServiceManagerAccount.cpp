// CServiceManagerProcess: SMC login, the architecture, the content users, the security
// descriptions, the SMC accounts, articles, punishments and the other queries (0x7001..0x7004,
// 0x7200..0x7209, 0x7300..0x7307, 0x7400..0x7412, 0x4023, 0x6313).

#include "ServiceManagerProcess.h"

#include <string.h>
#include <strsafe.h>

#include "ContentUser.h"
#include "GlobalManagerDB.h"
#include "GlobalManagerSvcFwd.h"
#include "BSLib/BSDebug.h"
#include "BSLib/DB/DBQuery.h"
#include "BSNet/IBSNet.h"
#include "ServerFramework/CertData.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerProcess.h"
#include "ServerFramework/Trigger.h"

using namespace ServerFramework;

static CServerProcessBase* TaskProcess(CTask* pTask)
{
    return (CServerProcessBase*)pTask->GetJob();
}

static CMsg* NewMsg(WORD wOpcode)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(wOpcode);
    return pMsg;
}

static void SendAndRelease(DWORD dwSessionID, CMsg* pMsg)
{
    g_pBSNet->SendMsg(dwSessionID, pMsg);
    g_pBSNet->ReleaseMsg(pMsg);
}

static void WriteByte(CMsg* pMsg, BYTE b)
{
    pMsg->WriteBytes(&b, 1);
}

static void WriteByte(CMassiveMsg* pMsg, BYTE b)
{
    pMsg->Write(&b, 1);
}

// sub_411AF0: WORD length and text.
static void ReadMassiveString(CMassiveMsg* pMassive, std::string& str)
{
    WORD wLen = 0;
    pMassive->Read(&wLen, 2);
    if (wLen > 0)
    {
        str.resize(wLen);
        pMassive->Read(&str[0], wLen);
    }
    else
    {
        str.clear();
    }
}

// The query of the service manager handlers: cleared, the division DB, connection 1.
static void InitQuery(CDBQuery* pQuery)
{
    memset(pQuery, 0, sizeof(*pQuery));
    pQuery->m_pSession = g_pDefaultDBSession;
    pQuery->m_nSession = 1;
}

// ---------------------------------------------------------------------------------------------
// 0x7001: login {string ID, string password, string key, DWORD version}. Answer (massive)
// 0xB001 {1, security, division, certification data} or {2, error}: 1 wrong version, 2 refused,
// 3 query failed, 4 not allowed; {2} alone without version.

#pragma pack(push, 1)
struct SM_CERTIFY_ROW
{
    DWORD   dwID;               // +0   0: refused
    BYTE    bySecurity;         // +4
    BYTE    ab5[3];             // +5
};
#pragma pack(pop)

C_ASSERT(sizeof(SM_CERTIFY_ROW) == 8);

void CServiceManagerProcess::OnMsg_7001(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strID;
    std::string strPassword;
    std::string strKey;
    *pMsg >> strID >> strPassword >> strKey;
    if (pMsg->GetUnreadSize() == 0)
    {
        CMassiveMsg* pReply = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
        pReply->SetOpcode(0xB001);
        WriteByte(pReply, 2);
        pReply->SendTo(pMsg->m_dwSessionID, FALSE);
        return;
    }
    DWORD dwVersion;
    *pMsg >> dwVersion;
    CMassiveMsg* pReply = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pReply->SetOpcode(0xB001);
    BYTE byError;
    if (dwVersion != 24)
    {
        byError = 1;
    }
    else
    {
        // BUG(GM-002): the row is not initialised; when the query finds no row the stale stack
        // decides the login.
        SM_CERTIFY_ROW row;
        CDBQuery query;
        query.m_pSession = g_pDefaultDBSession;
        query.m_pBuffer = (BYTE*)&row;
        query.m_nBufferSize = sizeof(row);
        query.m_nRowSize = sizeof(row);
        query.m_pTrigger = NULL;
        query.m_nSession = 1;
        int nResult;
        BYTE byDivision = g_pSelfBody->byDivisionID;
        if (byDivision == 20 || byDivision == 29)
        {
            nResult = query.ExecuteFormat("exec _CertifyUser_For_SMC '%s','%s','%s'",
                                          strID.c_str(), strPassword.c_str(), strKey.c_str());
        }
        else
        {
            nResult = query.ExecuteFormat("exec _CertifyUser '%s','%s','%s'", strID.c_str(),
                                          strPassword.c_str(), strKey.c_str());
        }
        if (nResult < 0)
        {
            byError = 3;
        }
        else if (!row.dwID)
        {
            byError = 2;
        }
        else if (!PrivIPMap_ContainsIP(row.bySecurity, CertData_GetSettingValue("SMC_Login")))
        {
            byError = 4;
        }
        else
        {
            sServiceManagerUser* pUser = ServiceManagerUserPool_Alloc();
            pUser->dwID = row.dwID;
            pUser->strID.assign(strID, 0, std::string::npos);
            pUser->strPassword.assign(strPassword, 0, std::string::npos);
            pUser->dwSessionID = pMsg->m_dwSessionID;
            pUser->bySecurity = row.bySecurity;
            m_mapUser.insert(SMUserMap::value_type(pMsg->m_dwSessionID, pUser));
            g_setNotifySession.insert(pUser->dwSessionID);
            WriteByte(pReply, 1);
            pReply->Write(&row.bySecurity, 1);
            pReply->Write(&g_pSelfBody->pDivision->byID, 1);
            CertData_SerializeRecordListsToMsg(pReply);
            pReply->SendTo(pMsg->m_dwSessionID, FALSE);
            WriteLog(pMsg->m_dwSessionID, 9, "Login : %s", strID.c_str());
            return;
        }
    }
    WriteByte(pReply, 2);
    pReply->Write(&byError, 1);
    pReply->SendTo(pMsg->m_dwSessionID, FALSE);
}

// ---------------------------------------------------------------------------------------------
// 0x7002: the architecture, (massive) 0xB002 with each record list as (BYTE 1, record)...,
// BYTE 0.

template <class TList>
static void WriteRecordList(CMassiveMsg* pReply, TList* pList, int nSize)
{
    for (typename TList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        WriteByte(pReply, 1);
        pReply->Write(*it, nSize);
    }
    WriteByte(pReply, 0);
}

void CServiceManagerProcess::OnMsg_7002(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    CMassiveMsg* pReply = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pReply->SetOpcode(0xB002);
    WriteRecordList(pReply, g_pCertModuleList, sizeof(CERT_MODULE));
    WriteRecordList(pReply, g_pCertContentList, sizeof(CERT_CONTENT));
    WriteRecordList(pReply, g_pCertDivisionList, sizeof(CERT_DIVISION));
    WriteRecordList(pReply, g_pCertFarmList, sizeof(CERT_FARM));
    WriteRecordList(pReply, g_pCertFarmContentList, sizeof(CERT_FARM_CONTENT));
    WriteRecordList(pReply, g_pCertShardList, sizeof(CERT_SHARD));
    WriteRecordList(pReply, g_pCertMachineList, sizeof(CERT_MACHINE));
    WriteRecordList(pReply, g_pServerBodyList, sizeof(SERVER_BODY));
    WriteRecordList(pReply, g_pServerCordList, sizeof(SERVER_CORD));
    pReply->SendTo(pMsg->m_dwSessionID, FALSE);
}

// ---------------------------------------------------------------------------------------------
// 0x7003 / 0x7004: module versions and their files (jobs).

void CServiceManagerProcess::OnMsg_7003(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    SM_JOB* pJob = OverlapJob_AllocSM_ModuleVersion();
    OverlapJob_SetupSM_ModuleVersion(m_pTask, pJob, pMsg->m_dwSessionID);
    PostJob(pJob);
}

void CServiceManagerProcess::OnMsg_7004(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE byModuleID;
    BYTE byParam;
    *pMsg >> byModuleID;
    *pMsg >> byParam;
    SM_JOB* pJob = OverlapJob_AllocSM_ModuleVersionFile();
    OverlapJob_SetupSM_ModuleVersionFile(m_pTask, pJob, pMsg->m_dwSessionID, byModuleID,
                                         byParam);
    PostJob(pJob);
}

// ---------------------------------------------------------------------------------------------
// 0x7200..0x7205, 0xA20D: content users.

void CServiceManagerProcess::OnMsg_7200(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strPattern;
    *pMsg >> strPattern;
    WriteLog(pMsg->m_dwSessionID, 15, "Online User Query REQ : %s", strPattern.c_str());
    CMassiveMsg* pReply = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pReply->SetOpcode(0xB200);
    WriteByte(pReply, 1);
    g_ContentUserManager.WriteUserQuery(pReply, strPattern.c_str());
    pReply->SendTo(pMsg->m_dwSessionID, FALSE);
}

// Ban: 0x620D {JID} to the user's AgentServer.
void CServiceManagerProcess::OnMsg_7201(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strID;
    *pMsg >> strID;
    WriteLog(pMsg->m_dwSessionID, 14, "Online User Ban REQ : %s", strID.c_str());
    sContentUser* pUser = g_ContentUserManager.FindByID(strID.c_str());
    CMsg* pRelay = g_pBSNet->AllocMsg(FALSE);
    if (!pUser)
    {
        pRelay->SetOpcode(0xB201);
        WriteByte(pRelay, 2);
        WriteByte(pRelay, 1);
        SendAndRelease(pMsg->m_dwSessionID, pRelay);
        return;
    }
    pRelay->SetOpcode(0x620D);
    pRelay->WriteBytes(&pUser->dwJID, 4);
    sSMDefault_RelayMsg_Param* pParam = SMRelayParamPool_Alloc();
    pParam->dwSessionID = pMsg->m_dwSessionID;
    pParam->dw8 = pUser->dwJID;
    pParam->wBodyID = pUser->wAgentBodyID;
    if (!RelayMsgToBody_6008(TaskProcess(m_pTask), pRelay, pUser->wAgentBodyID, 0, 0,
                             (DWORD)pParam))
    {
        // BUG(GM-001): the context of the failed relay is not released.
        CMsg* pReply = NewMsg(0xB201);
        WriteByte(pReply, 2);
        WriteByte(pReply, 2);
        SendAndRelease(pMsg->m_dwSessionID, pReply);
    }
}

// The AgentServer's answer {BYTE result}: 2 means the user was logged out (unregistered here).
void CServiceManagerProcess::OnMsg_A20D(CMsg* pMsg, DWORD, DWORD dwContext, CMassiveMsg*)
{
    sSMDefault_RelayMsg_Param* pParam = (sSMDefault_RelayMsg_Param*)dwContext;
    CMsg* pReply = NewMsg(0xB201);
    BYTE byResult;
    *pMsg >> byResult;
    if (byResult == 2)
        g_ContentUserManager.Unregister(pParam->dw8, NULL);
    WriteByte(pReply, 1);
    SendAndRelease(pParam->dwSessionID, pReply);
    pParam->Release();
}

// 0xB204 {division, users, daily visitors, all visitors, users per shard}.
void CServiceManagerProcess::OnMsg_7204(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    CMsg* pReply = NewMsg(0xB204);
    DWORD dwTotal = g_ContentUserManager.m_nTotalVisited;
    DWORD dwUsers = g_ContentUserManager.m_nUserCount;
    DWORD dwDaily = g_ContentUserManager.m_nDailyVisited;
    pReply->WriteBytes(&g_pSelfBody->byDivisionID, 1);
    pReply->WriteBytes(&dwUsers, 4);
    pReply->WriteBytes(&dwDaily, 4);
    pReply->WriteBytes(&dwTotal, 4);
    g_ContentUserManager.WriteShardUserCount(pReply);
    SendAndRelease(pMsg->m_dwSessionID, pReply);
}

// Reloads the franchise addresses: 0xB205 {1, DWORD count} or {2}.
void CServiceManagerProcess::OnMsg_7205(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    WriteLog(pMsg->m_dwSessionID, 13, "Launch Server REQ");
    int nCount = ContentUser_LoadFranchiseIPs();
    CMsg* pReply = NewMsg(0xB205);
    if (nCount < 0)
    {
        WriteByte(pReply, 2);
    }
    else
    {
        WriteByte(pReply, 1);
        pReply->WriteBytes(&nCount, 4);
    }
    SendAndRelease(pMsg->m_dwSessionID, pReply);
}

// 0x6313 {DWORD JID, string} (relayed from an AgentServer): logged with the user's account;
// answered 0xA313 {1}.
void CServiceManagerProcess::OnMsg_6313(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    DWORD dwJID;
    std::string strText;
    *pMsg >> dwJID;
    *pMsg >> strText;
    sContentUser* pUser = g_ContentUserManager.Find(dwJID);
    if (pUser)
        WriteUserLog(&strText, &pUser->strID);
    CMsg* pReply = NewMsg(0xA313);
    WriteByte(pReply, 1);
    TaskProcess(m_pTask)->SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}

// ---------------------------------------------------------------------------------------------
// 0x7206..0x7209: articles (jobs).

void CServiceManagerProcess::OnMsg_7206(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE byParam;
    *pMsg >> byParam;
    SM_JOB* pJob = OverlapJob_AllocArticleJob();
    OverlapJob_SetupArticleJob(m_pTask, pJob, 0, NULL, pMsg->m_dwSessionID, byParam, 0, NULL);
    PostJob(pJob);
}

void CServiceManagerProcess::OnMsg_7207(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE byParam;
    std::string strTitle;
    std::string strText;
    *pMsg >> byParam;
    *pMsg >> strTitle >> strText;
    WriteLog(pMsg->m_dwSessionID, 11, "Add Article %s", strTitle.c_str());
    SM_JOB* pJob = OverlapJob_AllocArticleJob();
    OverlapJob_SetupArticleJob(m_pTask, pJob, 1, strTitle.c_str(), pMsg->m_dwSessionID,
                               byParam, 0, strText.c_str());
    PostJob(pJob);
}

void CServiceManagerProcess::OnMsg_7208(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwArticleID;
    *pMsg >> dwArticleID;
    WriteLog(pMsg->m_dwSessionID, 11, "Delete Article %d", dwArticleID);
    SM_JOB* pJob = OverlapJob_AllocArticleJob();
    OverlapJob_SetupArticleJob(m_pTask, pJob, 2, NULL, pMsg->m_dwSessionID, 0, dwArticleID,
                               NULL);
    PostJob(pJob);
}

void CServiceManagerProcess::OnMsg_7209(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwArticleID;
    std::string strTitle;
    std::string strText;
    *pMsg >> dwArticleID;
    *pMsg >> strTitle >> strText;
    WriteLog(pMsg->m_dwSessionID, 11, "Edit Article");
    SM_JOB* pJob = OverlapJob_AllocArticleJob();
    OverlapJob_SetupArticleJob(m_pTask, pJob, 3, strTitle.c_str(), pMsg->m_dwSessionID, 0,
                               dwArticleID, strText.c_str());
    PostJob(pJob);
}

// ---------------------------------------------------------------------------------------------
// 0x7300..0x7307: security descriptions (division DB). Answers {1} (and the refreshed data
// for the SMC) or {2, 0}.

static void AnswerSecurityQuery(CServiceManagerProcess* pThis, CMsg* pMsg, CMsg* pReply,
                                int nResult)
{
    if (nResult >= 0)
    {
        WriteByte(pReply, 1);
        pThis->RefreshSecurityData(pMsg->m_dwSessionID);
    }
    else
    {
        WriteByte(pReply, 2);
        WriteByte(pReply, 0);
    }
    SendAndRelease(pMsg->m_dwSessionID, pReply);
}

void CServiceManagerProcess::OnMsg_7300(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strName;
    std::string strDesc;
    *pMsg >> strName >> strDesc;
    WriteLog(pMsg->m_dwSessionID, 12, "Add Security Description REQ : Name[%s], Desc[%s]",
             strName.c_str(), strDesc.c_str());
    CDBQuery query;
    InitQuery(&query);
    CMsg* pReply = NewMsg(0xB300);
    int nResult = query.ExecuteFormat("insert _SecurityDescription values( '%s', '%s')",
                                      strName.c_str(), strDesc.c_str());
    AnswerSecurityQuery(this, pMsg, pReply, nResult);
}

void CServiceManagerProcess::OnMsg_7301(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwID;
    *pMsg >> dwID;
    CERT_DATA_SETTING* pSetting = CertData_FindSettingLocked(dwID);
    if (pSetting)
    {
        WriteLog(pMsg->m_dwSessionID, 12, "Delete Security Description REQ : ID[%d] Name[%s]",
                 dwID, pSetting->szName);
    }
    CDBQuery query;
    InitQuery(&query);
    CMsg* pReply = NewMsg(0xB301);
    int nResult = query.ExecuteFormat("delete _SecurityDescription where nID = %d", dwID);
    AnswerSecurityQuery(this, pMsg, pReply, nResult);
}

void CServiceManagerProcess::OnMsg_7302(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strName;
    std::string strDesc;
    *pMsg >> strName >> strDesc;
    WriteLog(pMsg->m_dwSessionID, 12,
             "Add Security Description Group REQ : Name[%s] Description[%s]", strName.c_str(),
             strDesc.c_str());
    CDBQuery query;
    InitQuery(&query);
    CMsg* pReply = NewMsg(0xB302);
    int nResult = query.ExecuteFormat("insert _SecurityDescriptionGroup values( '%s', '%s')",
                                      strName.c_str(), strDesc.c_str());
    AnswerSecurityQuery(this, pMsg, pReply, nResult);
}

void CServiceManagerProcess::OnMsg_7303(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE byID;
    *pMsg >> byID;
    CERT_DATA_GROUP* pGroup = CertData_FindGroupLocked(byID);
    if (pGroup)
    {
        // The name is passed for "%d" (the log shows its address).
        WriteLog(pMsg->m_dwSessionID, 12,
                 "Delete Security Description Group REQ : ID[%d] Name[%d]", byID, pGroup->ab1);
    }
    CDBQuery query;
    InitQuery(&query);
    CMsg* pReply = NewMsg(0xB303);
    int nResult = query.ExecuteFormat("delete _SecurityDescriptionGroup where nID = '%d'", byID);
    AnswerSecurityQuery(this, pMsg, pReply, nResult);
}

void CServiceManagerProcess::OnMsg_7304(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE byGroupID;
    DWORD dwDescID;
    *pMsg >> byGroupID;
    *pMsg >> dwDescID;
    WriteLog(pMsg->m_dwSessionID, 12,
             "Add Security Descrition Group Assign : Group_ID[%d] Description_ID[%d]",
             byGroupID, dwDescID);
    CDBQuery query;
    InitQuery(&query);
    CMsg* pReply = NewMsg(0xB304);
    int nResult = query.ExecuteFormat(
        "insert _SecurityDescriptionGroupAssign values( '%d', '%d')", byGroupID, dwDescID);
    AnswerSecurityQuery(this, pMsg, pReply, nResult);
}

void CServiceManagerProcess::OnMsg_7305(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE byGroupID;
    DWORD dwDescID;
    *pMsg >> byGroupID;
    *pMsg >> dwDescID;
    WriteLog(pMsg->m_dwSessionID, 12,
             "Delete Security Description Group Assign : Group_ID[%d] Description_ID[%d]",
             byGroupID, dwDescID);
    CDBQuery query;
    InitQuery(&query);
    CMsg* pReply = NewMsg(0xB305);
    int nResult = query.ExecuteFormat(
        "delete _SecurityDescriptionGroupAssign where nGroupID = %d and nDescriptionID = %d",
        byGroupID, dwDescID);
    AnswerSecurityQuery(this, pMsg, pReply, nResult);
}

void CServiceManagerProcess::OnMsg_7307(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwID;
    std::string strDesc;
    *pMsg >> dwID;
    *pMsg >> strDesc;
    WriteLog(pMsg->m_dwSessionID, 12, "Modify Security Description : ID[%d] Description[%s]",
             dwID, strDesc.c_str());
    CDBQuery query;
    InitQuery(&query);
    CMsg* pReply = NewMsg(0xB307);
    int nResult = query.ExecuteFormat(
        "update _SecurityDescription set szDesc = '%s' where nID = %d", strDesc.c_str(), dwID);
    AnswerSecurityQuery(this, pMsg, pReply, nResult);
}

// ---------------------------------------------------------------------------------------------
// 0x7400..0x7412: SMC accounts and queries.

void CServiceManagerProcess::OnMsg_7400(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strID;
    *pMsg >> strID;
    WriteLog(pMsg->m_dwSessionID, 12, "Query User REQ : %s", strID.c_str());
    SM_JOB* pJob = OverlapJob_AllocQueryUser();
    OverlapJob_SetupQueryUser(m_pTask, pJob, strID.c_str(), pMsg->m_dwSessionID);
    PostJob(pJob);
}

// The 32-character fields are not terminated in the message.
struct SM_USER_FIELDS
{
    char    szPassword[33];
    char    szA[33];
    char    szB[33];
};

static void CopyUserFields(SM_USER_FIELDS* pFields, const SM_USER_RECORD* pRecord)
{
    memcpy(pFields->szPassword, pRecord->szPassword, 32);
    pFields->szPassword[32] = 0;
    memcpy(pFields->szA, pRecord->szA, 32);
    pFields->szA[32] = 0;
    memcpy(pFields->szB, pRecord->szB, 32);
    pFields->szB[32] = 0;
}

// {1}, or {2, 1} when the query failed.
static void AnswerUserQuery(CMsg* pMsg, CMsg* pReply, int nResult)
{
    if (nResult < 0)
        WriteByte(pReply, 2);
    WriteByte(pReply, 1);
    SendAndRelease(pMsg->m_dwSessionID, pReply);
}

void CServiceManagerProcess::OnMsg_7401(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    SM_USER_RECORD rec;
    *pMsg >> rec;
    WriteLog(pMsg->m_dwSessionID, 12, "Add User REQ : %s", rec.szUserID);
    CDBQuery query;
    InitQuery(&query);
    SM_USER_FIELDS fields;
    CopyUserFields(&fields, &rec);
    CMsg* pReply = NewMsg(0xB401);
    int nResult = query.ExecuteFormat(
        "exec _AddUser '%s','%s','%s','%s','%s','%s','%s',%d,%d,'%s'", rec.szUserID,
        fields.szPassword, rec.szName, fields.szA, fields.szB, rec.szC, rec.szD, rec.by1F4,
        rec.by1F5, rec.sz1F6);
    AnswerUserQuery(pMsg, pReply, nResult);
}

void CServiceManagerProcess::OnMsg_7402(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwID;
    *pMsg >> dwID;
    WriteLog(pMsg->m_dwSessionID, 12, "Delete User REQ : ID[%d]", dwID);
    CDBQuery query;
    InitQuery(&query);
    CMsg* pReply = NewMsg(0xB402);
    int nResult = query.ExecuteFormat("exec _DeleteUser %d", dwID);
    AnswerUserQuery(pMsg, pReply, nResult);
}

void CServiceManagerProcess::OnMsg_7403(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    SM_USER_RECORD rec;
    *pMsg >> rec;
    // The account name is passed for "%d" (the log shows its address).
    WriteLog(pMsg->m_dwSessionID, 12, "Modify User REQ : ID[%d] User_ID[%d]", rec.dwID,
             rec.szUserID);
    CDBQuery query;
    InitQuery(&query);
    SM_USER_FIELDS fields;
    CopyUserFields(&fields, &rec);
    CMsg* pReply = NewMsg(0xB403);
    int nResult = query.ExecuteFormat(
        "exec _EditUser %d,'%s','%s','%s','%s','%s','%s',%d,%d,'%s'", rec.dwID,
        fields.szPassword, rec.szName, fields.szA, fields.szB, rec.szC, rec.szD, rec.by1F4,
        rec.by1F5, rec.sz1F6);
    AnswerUserQuery(pMsg, pReply, nResult);
}

// {string account ("-": all), BYTE type, string begin, string end}
void CServiceManagerProcess::OnMsg_7404(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strID;
    std::string strBegin;
    std::string strEnd;
    BYTE byType;
    *pMsg >> strID;
    *pMsg >> byType;
    *pMsg >> strBegin >> strEnd;
    if (strID.compare("-") == 0)
        strID.assign("", (size_t)0);
    WriteLog(pMsg->m_dwSessionID, 23, "Query SMLog REQ : User_ID[%s] Begin[%s] End[%s]",
             strID.c_str(), strBegin.c_str(), strEnd.c_str());
    SM_JOB* pJob = OverlapJob_AllocQuerySMLog();
    OverlapJob_SetupQuerySMLog(m_pTask, pJob, strBegin.c_str(), strEnd.c_str(),
                               pMsg->m_dwSessionID, &strID, byType);
    PostJob(pJob);
}

// {BYTE type, string query when type 1}
void CServiceManagerProcess::OnMsg_7405(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strQuery;
    BYTE byType;
    *pMsg >> byType;
    if (byType == 1)
    {
        *pMsg >> strQuery;
        WriteLog(pMsg->m_dwSessionID, 24, "Query Content User Account REQ : Query String [%s]",
                 strQuery.c_str());
    }
    else
    {
        WriteLog(pMsg->m_dwSessionID, 24,
                 "Query Content User Account REQ : Query Blocked User");
    }
    SM_JOB* pJob = OverlapJob_AllocQueryContentUserAccount();
    OverlapJob_SetupQueryContentUserAccount(m_pTask, pJob, strQuery.c_str(), pMsg->m_dwSessionID,
                                            byType);
    PostJob(pJob);
}

void CServiceManagerProcess::OnMsg_7407(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strID;
    *pMsg >> strID;
    WriteLog(pMsg->m_dwSessionID, 24, "Query Punishment REQ : User ID[ %s]", strID.c_str());
    SM_JOB* pJob = OverlapJob_AllocQueryPunishment();
    OverlapJob_SetupQueryPunishment(m_pTask, pJob, strID.c_str(), pMsg->m_dwSessionID);
    PostJob(pJob);
}

// (massive) {BYTE job, DWORD serial, DWORD JID, BYTE type, string, WORD shard, 5 strings,
// 4 SYSTEMTIME}
void CServiceManagerProcess::OnMsg_7408(CMsg*, DWORD, DWORD, CMassiveMsg* pMassive)
{
    BYTE byJob;
    DWORD dwSerial;
    DWORD dwJID;
    BYTE byType;
    WORD wShardID;
    std::string str1;
    std::string str2;
    std::string str3;
    std::string str4;
    std::string str5;
    std::string str6;
    SYSTEMTIME t1;
    SYSTEMTIME t2;
    SYSTEMTIME t3;
    SYSTEMTIME t4;
    pMassive->Read(&byJob, 1);
    pMassive->Read(&dwSerial, 4);
    pMassive->Read(&dwJID, 4);
    pMassive->Read(&byType, 1);
    ReadMassiveString(pMassive, str1);
    pMassive->Read(&wShardID, 2);
    ReadMassiveString(pMassive, str2);
    ReadMassiveString(pMassive, str3);
    ReadMassiveString(pMassive, str4);
    ReadMassiveString(pMassive, str5);
    ReadMassiveString(pMassive, str6);
    pMassive->Read(&t1, 16);
    pMassive->Read(&t2, 16);
    pMassive->Read(&t3, 16);
    pMassive->Read(&t4, 16);
    WriteLog(pMassive->m_dw36, 24, "Punishment Job REQ : Job[ %d], Serial No[ %d], User ID[ %d]",
             byJob, dwSerial, dwJID);
    SM_JOB* pJob = OverlapJob_AllocPunishmentJob();
    OverlapJob_SetupPunishmentJob(m_pTask, pJob, str1.c_str(), str2.c_str(), pMassive->m_dw36,
                                  byJob, dwSerial, dwJID, byType, wShardID, str3.c_str(),
                                  str4.c_str(), str5.c_str(), str6.c_str(), t1, t2, t3, t4);
    PostJob(pJob);
}

void CServiceManagerProcess::OnMsg_7409(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strID;
    *pMsg >> strID;
    WriteQueryLog(pMsg->m_dwSessionID, "Query Char Owned By User [%s]", strID.c_str());
    SM_JOB* pJob = OverlapJob_AllocQueryCharOwnedUser();
    OverlapJob_SetupQueryCharOwnedUser(m_pTask, pJob, strID.c_str(), pMsg->m_dwSessionID);
    PostJob(pJob);
}

void CServiceManagerProcess::OnMsg_7410(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strID;
    *pMsg >> strID;
    WriteQueryLog(pMsg->m_dwSessionID, "Query User By User ID [%s]", strID.c_str());
    SM_JOB* pJob = OverlapJob_AllocQueryUserByUserID();
    OverlapJob_SetupQueryUserByUserID(m_pTask, pJob, strID.c_str(), pMsg->m_dwSessionID);
    PostJob(pJob);
}

// The JID of a game account (account DB; the table depends on the division): 0xB411 {1, DWORD},
// {2, 1} without row, {2, 0} when the query failed.
void CServiceManagerProcess::OnMsg_7411(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strID;
    *pMsg >> strID;
    CMsg* pReply = NewMsg(0xB411);
    CClassTrigger<CServiceManagerProcess> trigger(this);
    trigger.m_pfnTrigger2 = &CServiceManagerProcess::OnUserJIDRow;
    DWORD dwJID = 0xFFFFFFFF;
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pAccountDBSession;
    query.m_pBuffer = (BYTE*)&dwJID;
    query.m_nBufferSize = 4;
    query.m_nRowSize = 4;
    query.m_nSession = 1;
    query.m_dwParam = (DWORD)pReply;
    query.m_pTrigger = &trigger;
    char szQuery[0xC00];
    switch (g_pSelfBody->pDivision->byID)
    {
    case 1:
    case 2:
        // The result of the search is not used.
        strID.find("#$", 0, 2);
        StringCchPrintfA(szQuery, 0xC00,
                         "select sr_code from sr_member1 with (nolock) where sr_id = '%s'",
                         strID.c_str());
        break;
    case 4:
        StringCchPrintfA(szQuery, 0xC00,
                         "select nID from sr_member1 with (nolock) where sr_id = '%s'",
                         strID.c_str());
        break;
    case 18:
        StringCchPrintfA(szQuery, 0xC00,
                         "select nID from _User with (nolock) where szUserID = '%s'",
                         strID.c_str());
        break;
    default:
        StringCchPrintfA(szQuery, 0xC00,
                         "select JID from TB_User with (nolock) where strUserID = '%s'",
                         strID.c_str());
        break;
    }
    // BUG(GM-003): the formatted query is used as the format string of the query.
    if (query.ExecuteFormat(szQuery) < 0)
    {
        WriteByte(pReply, 2);
        WriteByte(pReply, 0);
    }
    else if (dwJID == 0xFFFFFFFF)
    {
        WriteByte(pReply, 2);
        WriteByte(pReply, 1);
    }
    SendAndRelease(pMsg->m_dwSessionID, pReply);
}

int CServiceManagerProcess::OnUserJIDRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (pQuery->m_bRowFetched)
    {
        CMsg* pReply = (CMsg*)pQuery->m_dwParam;
        WriteByte(pReply, 1);
        pReply->WriteBytes(pQuery->m_pBuffer, 4);
    }
    return 1;
}

// {WORD shard, BYTE type, string begin, string end}
void CServiceManagerProcess::OnMsg_7412(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    WORD wShardID;
    BYTE byType;
    std::string strBegin;
    std::string strEnd;
    *pMsg >> wShardID;
    *pMsg >> byType;
    *pMsg >> strBegin >> strEnd;
    SM_JOB* pJob = OverlapJob_AllocConcurrentUserLogJob();
    OverlapJob_SetupConcurrentUserLogJob(m_pTask, pJob, pMsg->m_dwSessionID, wShardID, byType,
                                         &strBegin, &strEnd);
    PostJob(pJob);
}

// ---------------------------------------------------------------------------------------------
// 0x4023 (massive) {WORD body, (BYTE 1, 2443-byte record)..., BYTE 0}: relayed to the body;
// the answer 0x8023 goes back to the SMC.

void CServiceManagerProcess::OnMsg_4023(CMsg*, DWORD, DWORD, CMassiveMsg* pMassive)
{
    WORD wBodyID;
    pMassive->Read(&wBodyID, 2);
    CMassiveMsg* pRelay = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pRelay->SetOpcode(0x4023);
    BYTE byMore = 1;
    pMassive->Read(&byMore, 1);
    BYTE abRecord[2443];
    while (byMore == 1)
    {
        memset(abRecord, 0, sizeof(abRecord));
        pMassive->Read(abRecord, sizeof(abRecord));
        pRelay->Write(&byMore, 1);
        pRelay->Write(abRecord, sizeof(abRecord));
        pMassive->Read(&byMore, 1);
    }
    WriteByte(pRelay, 0);
    sSMDefault_RelayMsg_Param* pParam = SMRelayParamPool_Alloc();
    pParam->dwSessionID = pMassive->m_dw36;
    pParam->dw8 = 0x4023;
    pParam->wBodyID = wBodyID;
    if (!RelayMassiveMsgToBody_6008(pRelay, TaskProcess(m_pTask), wBodyID, 0, 0, (DWORD)pParam))
    {
        // BUG(GM-004): the answer is sent but not released.
        CMsg* pReply = NewMsg(0x8023);
        WriteByte(pReply, 0);
        g_pBSNet->SendMsg(pMassive->m_dw36, pReply);
        pParam->Release();
    }
}

void CServiceManagerProcess::OnMsg_8023(CMsg*, DWORD, DWORD dwContext, CMassiveMsg*)
{
    sSMDefault_RelayMsg_Param* pParam = (sSMDefault_RelayMsg_Param*)dwContext;
    CMsg* pReply = NewMsg(0x8023);
    WriteByte(pReply, 1);
    // Without context the original calls through a NULL object.
    // BUG(GM-004): the answer is sent but not released.
    g_pBSNet->SendMsg(pParam->dwSessionID, pReply);
    pParam->Release();
}
