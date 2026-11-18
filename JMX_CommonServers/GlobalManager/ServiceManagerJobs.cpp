// Overlap jobs of the service manager (COverlapJob_SM_*, the SMC queries, punishments, patches,
// articles, the SMC log) and COverlapJob_ModuleVersionData.

#include "ServiceManagerJobs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strsafe.h>

#include "GlobalManagerDB.h"
#include "GlobalManagerMainFwd.h"
#include "BSLib/BSDebug.h"
#include "BSLib/DB/DBQuery.h"
#include "BSNet/IBSNet.h"
#include "BSNet/Msg.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/CertData.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerProcess.h"
#include "ServerFramework/Trigger.h"
#include "ServiceManagerProcess.h"

using namespace ServerFramework;

CChunkAllocatorST<COverlapJob_ModuleVersionData> g_ModuleVersionDataJobPool(
    NULL, 0, 500, "COverlapJob_ModuleVersionData");
CChunkAllocatorST<COverlapJob_SM_ModuleVersion> g_SMModuleVersionJobPool(
    NULL, 0, 500, "COverlapJob_SM_ModuleVersion");
CChunkAllocatorST<COverlapJob_SM_ModuleVersionFile> g_SMModuleVersionFileJobPool(
    NULL, 0, 500, "COverlapJob_SM_ModuleVersionFile");
CChunkAllocatorST<COverlapJob_RecordPatchData> g_RecordPatchDataJobPool(
    NULL, 0, 500, "COverlapJob_RecordPatchData");
CChunkAllocatorST<COverlapJob_RefreshSecurityData_n_SendToSM> g_RefreshSecurityDataJobPool(
    NULL, 0, 500, "COverlapJob_RefreshSecurityData_n_SendToSM");
CChunkAllocatorST<COverlapJob_QueryUser> g_QueryUserJobPool(
    NULL, 0, 500, "COverlapJob_QueryUser");
CChunkAllocatorST<COverlapJob_SMLog> g_SMLogJobPool(NULL, 0, 500, "COverlapJob_SMLog");
CChunkAllocatorST<COverlapJob_QuerySMLog> g_QuerySMLogJobPool(
    NULL, 0, 500, "COverlapJob_QuerySMLog");
CChunkAllocatorST<COverlapJob_QueryContentUserAccount> g_QueryContentUserAccountJobPool(
    NULL, 0, 500, "COverlapJob_QueryContentUserAccount");
CChunkAllocatorST<COverlapJob_QueryPunishment> g_QueryPunishmentJobPool(
    NULL, 0, 500, "COverlapJob_QueryPunishment");
CChunkAllocatorST<COverlapJob_PunishmentJob> g_PunishmentJobPool(
    NULL, 0, 500, "COverlapJob_PunishmentJob");
CChunkAllocatorST<COverlapJob_AutomatedPunishmentJob> g_AutomatedPunishmentJobPool(
    NULL, 0, 500, "COverlapJob_AutomatedPunishmentJob");
CChunkAllocatorST<COverlapJob_QueryCharOwnedUser> g_QueryCharOwnedUserJobPool(
    NULL, 0, 500, "COverlapJob_QueryCharOwnedUser");
CChunkAllocatorST<COverlapJob_QueryUserByUserID> g_QueryUserByUserIDJobPool(
    NULL, 0, 500, "COverlapJob_QueryUserByUserID");
CChunkAllocatorST<COverlapJob_PreparePatch> g_PreparePatchJobPool(
    NULL, 0, 500, "COverlapJob_PreparePatch");
CChunkAllocatorST<COverlapJob_ArticleJob> g_ArticleJobPool(
    NULL, 0, 500, "COverlapJob_ArticleJob");
CChunkAllocatorST<COverlapJob_ConcurrentUserLogJob> g_ConcurrentUserLogJobPool(
    NULL, 0, 500, "COverlapJob_ConcurrentUserLogJob");

static CServerProcessBase* OwnerProcess(CTask* pOwner)
{
    return (CServerProcessBase*)pOwner->GetJob();
}

static void WriteByte(CMassiveMsg* pMsg, BYTE b)
{
    pMsg->Write(&b, 1);
}

static void WriteByte(CMsg* pMsg, BYTE b)
{
    pMsg->WriteBytes(&b, 1);
}

static CMassiveMsg* NewReply(WORD wOpcode)
{
    CMassiveMsg* pMsg = CMassiveMsg::Alloc(1);
    pMsg->SetOpcode(wOpcode);
    return pMsg;
}

// A new reply {2} to the session.
static void SendFailure(WORD wOpcode, DWORD dwSessionID)
{
    CMassiveMsg* pMsg = NewReply(wOpcode);
    WriteByte(pMsg, 2);
    pMsg->SendTo(dwSessionID, FALSE);
}

static void SendAndRelease(DWORD dwSessionID, CMsg* pMsg)
{
    g_pBSNet->SendMsg(dwSessionID, pMsg);
    g_pBSNet->ReleaseMsg(pMsg);
}

// The row triggers of the SMC queries: before a row the query's m_dwParam is the row buffer of
// nSize bytes (cleared); after it BYTE 1 and the row go to the reply (m_pUserData).
static int OnReplyRow(CDBQuery* pQuery, int nSize)
{
    if (pQuery->m_bRowFetched)
    {
        CMassiveMsg* pReply = (CMassiveMsg*)pQuery->m_pUserData;
        WriteByte(pReply, 1);
        pReply->Write(pQuery->m_pBuffer, nSize);
    }
    else
    {
        BYTE* pBuffer = (BYTE*)pQuery->m_dwParam;
        pQuery->m_pBuffer = pBuffer;
        pQuery->m_nRowSize = nSize;
        pQuery->m_nBufferSize = nSize;
        memset(pBuffer, 0, nSize);
    }
    return 1;
}

// The same with the reply and the row in the job.
static int OnJobRow(CDBQuery* pQuery, CMassiveMsg* pReply, void* pRow, int nSize)
{
    if (pQuery->m_bRowFetched)
    {
        WriteByte(pReply, 1);
        pReply->Write(pRow, nSize);
    }
    else
    {
        pQuery->m_pBuffer = (BYTE*)pRow;
        pQuery->m_nRowSize = nSize;
        pQuery->m_nBufferSize = nSize;
        memset(pRow, 0, nSize);
    }
    return 1;
}

// String_ReplaceQuotesWithSpace (0x40f520): the GatewayServer function of the same name
// (JMX_CommonServers\GatewayServer), linked into GlobalManager.exe as well.
// BUG(GW-005): the double-byte check tests the byte after the quote.
static void ReplaceQuotesWithSpace(std::string* pstr)
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

// ---------------------------------------------------------------------------------------------
// Allocation (pool and job ID) and setup

template <class T>
static T* AllocJob(CChunkAllocatorST<T>& pool)
{
    T* pJob = pool.Alloc();
    pJob->AcquireId();
    return pJob;
}

SM_JOB* OverlapJob_AllocModuleVersionData()
{
    return AllocJob(g_ModuleVersionDataJobPool);
}

void OverlapJob_SetupModuleVersionData(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID)
{
    COverlapJob_ModuleVersionData* p = (COverlapJob_ModuleVersionData*)pJob;
    p->Init(pTask);
    p->m_dwSessionID = dwSessionID;
    p->m_pReply = NewReply(0xA306);
}

SM_JOB* OverlapJob_AllocSM_ModuleVersion()
{
    return AllocJob(g_SMModuleVersionJobPool);
}

void OverlapJob_SetupSM_ModuleVersion(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID)
{
    COverlapJob_SM_ModuleVersion* p = (COverlapJob_SM_ModuleVersion*)pJob;
    p->Init(pTask);
    p->m_dwSessionID = dwSessionID;
    p->m_pReply = NewReply(0xB003);
}

SM_JOB* OverlapJob_AllocSM_ModuleVersionFile()
{
    return AllocJob(g_SMModuleVersionFileJobPool);
}

void OverlapJob_SetupSM_ModuleVersionFile(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID,
                                          BYTE byModuleID, BYTE byParam)
{
    COverlapJob_SM_ModuleVersionFile* p = (COverlapJob_SM_ModuleVersionFile*)pJob;
    p->Init(pTask);
    p->m_dwSessionID = dwSessionID;
    p->m_byModuleID = byModuleID;
    p->m_byContentID = byParam;
    p->m_pReply = NewReply(0xB004);
}

SM_JOB* OverlapJob_AllocRecordPatchData()
{
    return AllocJob(g_RecordPatchDataJobPool);
}

void OverlapJob_SetupRecordPatchData(CTask* pTask, SM_JOB* pJob, const MODULE_VERSION* pModule,
                                     ModuleVersionFileList* pFiles, DWORD dwSessionID)
{
    COverlapJob_RecordPatchData* p = (COverlapJob_RecordPatchData*)pJob;
    p->Init(pTask);
    p->m_pModule = pModule;
    p->m_pFiles = pFiles;
    p->m_dwSessionID = dwSessionID;
}

SM_JOB* OverlapJob_AllocRefreshSecurityData()
{
    return AllocJob(g_RefreshSecurityDataJobPool);
}

void OverlapJob_SetupRefreshSecurityData(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID)
{
    COverlapJob_RefreshSecurityData_n_SendToSM* p =
        (COverlapJob_RefreshSecurityData_n_SendToSM*)pJob;
    p->Init(pTask);
    p->m_dwSessionID = dwSessionID;
}

SM_JOB* OverlapJob_AllocQueryUser()
{
    return AllocJob(g_QueryUserJobPool);
}

void OverlapJob_SetupQueryUser(CTask* pTask, SM_JOB* pJob, const char* pszUserID,
                               DWORD dwSessionID)
{
    COverlapJob_QueryUser* p = (COverlapJob_QueryUser*)pJob;
    p->Init(pTask);
    p->m_dwSessionID = dwSessionID;
    p->m_strUserID.assign(pszUserID, strlen(pszUserID));
    p->m_pReply = NewReply(0xB400);
    WriteByte(p->m_pReply, 1);
}

SM_JOB* OverlapJob_AllocSMLog()
{
    return AllocJob(g_SMLogJobPool);
}

void OverlapJob_SetupSMLog(CTask* pTask, SM_JOB* pJob, std::string* pstrUserID, BYTE byType,
                           const char* pszText)
{
    COverlapJob_SMLog* p = (COverlapJob_SMLog*)pJob;
    p->Init(pTask);
    p->m_strUserID.assign(*pstrUserID, 0, std::string::npos);
    p->m_byType = byType;
    p->m_strText.assign(pszText, strlen(pszText));
}

SM_JOB* OverlapJob_AllocQuerySMLog()
{
    return AllocJob(g_QuerySMLogJobPool);
}

void OverlapJob_SetupQuerySMLog(CTask* pTask, SM_JOB* pJob, const char* pszBegin,
                                const char* pszEnd, DWORD dwSessionID,
                                std::string* pstrUserID, BYTE byType)
{
    COverlapJob_QuerySMLog* p = (COverlapJob_QuerySMLog*)pJob;
    p->Init(pTask);
    p->m_dwSessionID = dwSessionID;
    p->m_strUserID.assign(*pstrUserID, 0, std::string::npos);
    p->m_byType = byType;
    p->m_strBegin.assign(pszBegin, strlen(pszBegin));
    p->m_strEnd.assign(pszEnd, strlen(pszEnd));
    p->m_pReply = NewReply(0xB404);
    WriteByte(p->m_pReply, 1);
}

SM_JOB* OverlapJob_AllocQueryContentUserAccount()
{
    return AllocJob(g_QueryContentUserAccountJobPool);
}

void OverlapJob_SetupQueryContentUserAccount(CTask* pTask, SM_JOB* pJob, const char* pszQuery,
                                             DWORD dwSessionID, BYTE byType)
{
    COverlapJob_QueryContentUserAccount* p = (COverlapJob_QueryContentUserAccount*)pJob;
    p->Init(pTask);
    p->m_dwSessionID = dwSessionID;
    p->m_strQuery.assign(pszQuery, strlen(pszQuery));
    p->m_byType = byType;
    p->m_pReply = NewReply(0xB406);
    WriteByte(p->m_pReply, 1);
}

SM_JOB* OverlapJob_AllocQueryPunishment()
{
    return AllocJob(g_QueryPunishmentJobPool);
}

void OverlapJob_SetupQueryPunishment(CTask* pTask, SM_JOB* pJob, const char* pszUserID,
                                     DWORD dwSessionID)
{
    COverlapJob_QueryPunishment* p = (COverlapJob_QueryPunishment*)pJob;
    p->Init(pTask);
    p->m_dwSessionID = dwSessionID;
    p->m_strUserID.assign(pszUserID, strlen(pszUserID));
    p->m_pReply = NewReply(0xB407);
    WriteByte(p->m_pReply, 1);
}

SM_JOB* OverlapJob_AllocPunishmentJob()
{
    return AllocJob(g_PunishmentJobPool);
}

void OverlapJob_SetupPunishmentJob(CTask* pTask, SM_JOB* pJob, const char* psz1,
                                   const char* psz2, DWORD dwSessionID, BYTE byJob,
                                   DWORD dwSerial, DWORD dwJID, BYTE byType, WORD wShardID,
                                   const char* psz3, const char* psz4, const char* psz5,
                                   const char* psz6, SYSTEMTIME t1, SYSTEMTIME t2,
                                   SYSTEMTIME t3, SYSTEMTIME t4)
{
    COverlapJob_PunishmentJob* p = (COverlapJob_PunishmentJob*)pJob;
    p->Init(pTask);
    p->m_byJob = byJob;
    p->m_dwSessionID = dwSessionID;
    p->m_dwSerial = dwSerial;
    p->m_dwJID = dwJID;
    p->m_byType = byType;
    p->m_str1.assign(psz1, strlen(psz1));
    p->m_wShardID = wShardID;
    p->m_str2.assign(psz2, strlen(psz2));
    p->m_str3.assign(psz3, strlen(psz3));
    p->m_str4.assign(psz4, strlen(psz4));
    p->m_str5.assign(psz5, strlen(psz5));
    p->m_str6.assign(psz6, strlen(psz6));
    p->m_t1 = t1;
    p->m_t2 = t2;
    p->m_t3 = t3;
    p->m_t4 = t4;
}

SM_JOB* OverlapJob_AllocAutomatedPunishmentJob()
{
    return AllocJob(g_AutomatedPunishmentJobPool);
}

void OverlapJob_SetupAutomatedPunishmentJob(CTask* pTask, SM_JOB* pJob,
                                            std::string* pstrUserID, std::string* pstr2,
                                            std::string* pstr3, std::string* pstr4,
                                            DWORD dwParam)
{
    COverlapJob_AutomatedPunishmentJob* p = (COverlapJob_AutomatedPunishmentJob*)pJob;
    p->Init(pTask);
    p->m_nResult = 0;
    p->m_strUserID.assign(*pstrUserID, 0, std::string::npos);
    p->m_str2.assign(*pstr2, 0, std::string::npos);
    p->m_str3.assign(*pstr3, 0, std::string::npos);
    p->m_str4.assign(*pstr4, 0, std::string::npos);
    p->m_dwParam = dwParam;
}

SM_JOB* OverlapJob_AllocQueryCharOwnedUser()
{
    return AllocJob(g_QueryCharOwnedUserJobPool);
}

void OverlapJob_SetupQueryCharOwnedUser(CTask* pTask, SM_JOB* pJob, const char* pszUserID,
                                        DWORD dwSessionID)
{
    COverlapJob_QueryCharOwnedUser* p = (COverlapJob_QueryCharOwnedUser*)pJob;
    p->Init(pTask);
    p->m_dwSessionID = dwSessionID;
    p->m_strName.assign(pszUserID, strlen(pszUserID));
    p->m_pReply = NewReply(0xB409);
    WriteByte(p->m_pReply, 1);
}

SM_JOB* OverlapJob_AllocQueryUserByUserID()
{
    return AllocJob(g_QueryUserByUserIDJobPool);
}

void OverlapJob_SetupQueryUserByUserID(CTask* pTask, SM_JOB* pJob, const char* pszUserID,
                                       DWORD dwSessionID)
{
    COverlapJob_QueryUserByUserID* p = (COverlapJob_QueryUserByUserID*)pJob;
    p->Init(pTask);
    p->m_dwSessionID = dwSessionID;
    p->m_strUserID.assign(pszUserID, strlen(pszUserID));
    // BUG(GM-008): the answer of 0x7410 has the opcode of 0x7409's answer.
    p->m_pReply = NewReply(0xB409);
    WriteByte(p->m_pReply, 1);
}

SM_JOB* OverlapJob_AllocPreparePatch()
{
    return AllocJob(g_PreparePatchJobPool);
}

void OverlapJob_SetupPreparePatch(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID,
                                  BYTE byModuleID, BYTE byParam)
{
    COverlapJob_PreparePatch* p = (COverlapJob_PreparePatch*)pJob;
    p->Init(pTask);
    p->m_dwSessionID = dwSessionID;
    p->m_byModuleID = byModuleID;
    p->m_byContentID = byParam;
    p->m_pReply = NewReply(0xB104);
    WriteByte(p->m_pReply, 1);
    p->m_bFound = FALSE;
}

SM_JOB* OverlapJob_AllocArticleJob()
{
    return AllocJob(g_ArticleJobPool);
}

void OverlapJob_SetupArticleJob(CTask* pTask, SM_JOB* pJob, BYTE byType, const char* pszTitle,
                                DWORD dwSessionID, BYTE byParam, DWORD dwArticleID,
                                const char* pszText)
{
    COverlapJob_ArticleJob* p = (COverlapJob_ArticleJob*)pJob;
    p->Init(pTask);
    p->m_dwSessionID = dwSessionID;
    p->m_byContentID = byParam;
    p->m_byType = byType;
    p->m_dwArticleID = dwArticleID;
    // Missing texts leave the strings of the job's previous use.
    if (pszTitle)
        p->m_strTitle.assign(pszTitle, strlen(pszTitle));
    if (pszText)
        p->m_strText.assign(pszText, strlen(pszText));
    p->m_pReply = NULL;
    if (!byType)
    {
        p->m_pReply = NewReply(0xB206);
        WriteByte(p->m_pReply, 1);
    }
}

SM_JOB* OverlapJob_AllocConcurrentUserLogJob()
{
    return AllocJob(g_ConcurrentUserLogJobPool);
}

void OverlapJob_SetupConcurrentUserLogJob(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID,
                                          WORD wShardID, BYTE byType, std::string* pstrBegin,
                                          std::string* pstrEnd)
{
    COverlapJob_ConcurrentUserLogJob* p = (COverlapJob_ConcurrentUserLogJob*)pJob;
    p->Init(pTask);
    p->m_dwSessionID = dwSessionID;
    p->m_wShardID = wShardID;
    p->m_byType = byType;
    p->m_strBegin.assign(*pstrBegin, 0, std::string::npos);
    p->m_strEnd.assign(*pstrEnd, 0, std::string::npos);
    p->m_pReply = CMassiveMsg::Alloc(1);
}

// ---------------------------------------------------------------------------------------------
// ReturnToPool of every class (vf03): jobs with an ID go back to their pool.

#define JOB_RETURN_TO_POOL(Class, Pool)                                                         \
    void Class::ReturnToPool()                                                                 \
    {                                                                                          \
        if (m_dwID)                                                                            \
        {                                                                                      \
            ReleaseId();                                                                       \
            Pool.Free(this);                                                                   \
        }                                                                                      \
    }

JOB_RETURN_TO_POOL(COverlapJob_ModuleVersionData, g_ModuleVersionDataJobPool)
JOB_RETURN_TO_POOL(COverlapJob_SM_ModuleVersion, g_SMModuleVersionJobPool)
JOB_RETURN_TO_POOL(COverlapJob_SM_ModuleVersionFile, g_SMModuleVersionFileJobPool)
JOB_RETURN_TO_POOL(COverlapJob_RecordPatchData, g_RecordPatchDataJobPool)
JOB_RETURN_TO_POOL(COverlapJob_RefreshSecurityData_n_SendToSM, g_RefreshSecurityDataJobPool)
JOB_RETURN_TO_POOL(COverlapJob_QueryUser, g_QueryUserJobPool)
JOB_RETURN_TO_POOL(COverlapJob_SMLog, g_SMLogJobPool)
JOB_RETURN_TO_POOL(COverlapJob_QuerySMLog, g_QuerySMLogJobPool)
JOB_RETURN_TO_POOL(COverlapJob_QueryContentUserAccount, g_QueryContentUserAccountJobPool)
JOB_RETURN_TO_POOL(COverlapJob_QueryPunishment, g_QueryPunishmentJobPool)
JOB_RETURN_TO_POOL(COverlapJob_PunishmentJob, g_PunishmentJobPool)
JOB_RETURN_TO_POOL(COverlapJob_AutomatedPunishmentJob, g_AutomatedPunishmentJobPool)
JOB_RETURN_TO_POOL(COverlapJob_QueryCharOwnedUser, g_QueryCharOwnedUserJobPool)
JOB_RETURN_TO_POOL(COverlapJob_QueryUserByUserID, g_QueryUserByUserIDJobPool)
JOB_RETURN_TO_POOL(COverlapJob_PreparePatch, g_PreparePatchJobPool)
JOB_RETURN_TO_POOL(COverlapJob_ArticleJob, g_ArticleJobPool)
JOB_RETURN_TO_POOL(COverlapJob_ConcurrentUserLogJob, g_ConcurrentUserLogJobPool)

// ---------------------------------------------------------------------------------------------
// COverlapJob_ModuleVersionData

bool COverlapJob_ModuleVersionData::Prepare()
{
    if (g_pBSNet->GetSessionSocketState(m_dwSessionID) == -1)
    {
        m_pReply->Free();
        return false;
    }
    return true;
}

int COverlapJob_ModuleVersionData::OnModuleRow(DWORD dwQuery, DWORD)
{
    return OnJobRow((CDBQuery*)dwQuery, m_pReply, &m_Module, sizeof(m_Module));
}

int COverlapJob_ModuleVersionData::OnFileRow(DWORD dwQuery, DWORD)
{
    return OnJobRow((CDBQuery*)dwQuery, m_pReply, &m_File, sizeof(m_File));
}

bool COverlapJob_ModuleVersionData::Execute()
{
    WriteByte(m_pReply, 1);
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pDefaultDBSession;
    CClassTrigger<COverlapJob_ModuleVersionData>* pTrigger =
        new CClassTrigger<COverlapJob_ModuleVersionData>(this);
    if (pTrigger)
        pTrigger->m_pfnTrigger2 = &COverlapJob_ModuleVersionData::OnModuleRow;
    query.m_pTrigger = pTrigger;
    query.m_nSession = 0;
    if (query.ExecuteFormat("select * from _ModuleVersion with (NOLOCK) where nValid = 1") < 0)
    {
        delete query.m_pTrigger;
        return false;
    }
    delete query.m_pTrigger;
    WriteByte(m_pReply, 0);
    query.m_pSession = g_pDefaultDBSession;
    pTrigger = new CClassTrigger<COverlapJob_ModuleVersionData>(this);
    if (pTrigger)
        pTrigger->m_pfnTrigger2 = &COverlapJob_ModuleVersionData::OnFileRow;
    query.m_pTrigger = pTrigger;
    query.m_nSession = 0;
    if (query.ExecuteFormat("select * from _ModuleVersionFile with (NOLOCK) where nValid = 1")
        < 0)
    {
        delete query.m_pTrigger;
        return false;
    }
    delete query.m_pTrigger;
    WriteByte(m_pReply, 0);
    return true;
}

bool COverlapJob_ModuleVersionData::OnComplete()
{
    if (g_pBSNet->GetSessionSocketState(m_dwSessionID) == -1)
    {
        m_pReply->Free();
        return false;
    }
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

bool COverlapJob_ModuleVersionData::OnFail()
{
    m_pReply->Free();
    return true;
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_SM_ModuleVersion / COverlapJob_SM_ModuleVersionFile

bool COverlapJob_SM_ModuleVersion::Prepare()
{
    return true;
}

int COverlapJob_SM_ModuleVersion::OnModuleRow(DWORD dwQuery, DWORD)
{
    return OnJobRow((CDBQuery*)dwQuery, m_pReply, &m_Module, sizeof(m_Module));
}

bool COverlapJob_SM_ModuleVersion::Execute()
{
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pDefaultDBSession;
    CClassTrigger<COverlapJob_SM_ModuleVersion>* pTrigger =
        new CClassTrigger<COverlapJob_SM_ModuleVersion>(this);
    if (pTrigger)
        pTrigger->m_pfnTrigger2 = &COverlapJob_SM_ModuleVersion::OnModuleRow;
    query.m_pTrigger = pTrigger;
    query.m_nSession = 0;
    WriteByte(m_pReply, 1);
    if (query.ExecuteFormat("select * from _ModuleVersion with (NOLOCK) where nValid = 1") < 0)
    {
        delete query.m_pTrigger;
        return false;
    }
    WriteByte(m_pReply, 0);
    delete query.m_pTrigger;
    return true;
}

bool COverlapJob_SM_ModuleVersion::OnComplete()
{
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

bool COverlapJob_SM_ModuleVersion::OnFail()
{
    m_pReply->Free();
    m_pReply = NewReply(0xB003);
    WriteByte(m_pReply, 2);
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

bool COverlapJob_SM_ModuleVersionFile::Prepare()
{
    return true;
}

int COverlapJob_SM_ModuleVersionFile::OnFileRow(DWORD dwQuery, DWORD)
{
    return OnJobRow((CDBQuery*)dwQuery, m_pReply, &m_File, sizeof(m_File));
}

bool COverlapJob_SM_ModuleVersionFile::Execute()
{
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pDefaultDBSession;
    CClassTrigger<COverlapJob_SM_ModuleVersionFile>* pTrigger =
        new CClassTrigger<COverlapJob_SM_ModuleVersionFile>(this);
    if (pTrigger)
        pTrigger->m_pfnTrigger2 = &COverlapJob_SM_ModuleVersionFile::OnFileRow;
    query.m_pTrigger = pTrigger;
    query.m_nSession = 0;
    WriteByte(m_pReply, 1);
    if (query.ExecuteFormat("select * from _ModuleVersionFile with (NOLOCK) where nModuleID = %d "
                            "and nContentID = %d and nValid = 1", m_byModuleID,
                            m_byContentID) < 0)
    {
        delete query.m_pTrigger;
        return false;
    }
    WriteByte(m_pReply, 0);
    delete query.m_pTrigger;
    return true;
}

bool COverlapJob_SM_ModuleVersionFile::OnComplete()
{
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

bool COverlapJob_SM_ModuleVersionFile::OnFail()
{
    m_pReply->Free();
    m_pReply = NewReply(0xB004);
    WriteByte(m_pReply, 2);
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_RecordPatchData

static BYTE FieldB(const void* p, int nOffset)
{
    return *((const BYTE*)p + nOffset);
}

static WORD FieldW(const void* p, int nOffset)
{
    return *(const WORD*)((const BYTE*)p + nOffset);
}

static DWORD FieldD(const void* p, int nOffset)
{
    return *(const DWORD*)((const BYTE*)p + nOffset);
}

bool COverlapJob_RecordPatchData::Prepare()
{
    return true;
}

bool COverlapJob_RecordPatchData::Execute()
{
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pDefaultDBSession;
    query.m_nSession = 0;
    ModuleVersionFileList* pFiles = m_pFiles;
    int nCount = (int)pFiles->size();
    BYTE byPercent = 0;
    int nDone = 0;
    for (ModuleVersionFileList::iterator it = pFiles->begin(); it != pFiles->end(); ++it)
    {
        const MODULE_VERSION_FILE* pFile = *it;
        if (g_pBSNet->GetSessionSocketState(m_dwSessionID) == -1)
        {
            m_dwSessionID = 0;
            return false;
        }
        // The client root is stored as "".
        char szPath[MAX_PATH];
        if (FAILED(StringCchCopyA(szPath, MAX_PATH, pFile->szPath[0] ? pFile->szPath : ".")))
        {
            BSAssertFail();
            return false;
        }
        if (query.ExecuteFormat("update _ModuleVersionFile set nValid = 0 where nModuleID = %d "
                                "and nContentID = %d and szFilename = '%s' and szPath = '%s' "
                                "and nValid = 1", pFile->byModuleID, pFile->ab8[1],
                                pFile->szName, szPath) < 0)
        {
            return false;
        }
        if (query.ExecuteFormat("insert _ModuleVersionFile values( %d, %d, %d, %d, '%s', '%s', "
                                "%d, %d, %d, %d, '%04d-%02d-%02d %02d:%02d:%02d.000', 1)",
                                pFile->dwVersion, pFile->ab8[0], pFile->ab8[1],
                                pFile->byModuleID, pFile->szName, szPath, FieldD(pFile, 0x20B),
                                FieldB(pFile, 0x20F), FieldD(pFile, 0x210),
                                FieldB(pFile, 0x214), (short)FieldW(pFile, 0x215),
                                FieldW(pFile, 0x217), FieldW(pFile, 0x219),
                                FieldW(pFile, 0x21B), FieldW(pFile, 0x21D),
                                FieldW(pFile, 0x21F)) < 0)
        {
            return false;
        }
        nDone += 100;
        int nPercent = nDone / nCount;
        if (abs(nPercent - byPercent) > 1)
        {
            CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
            pMsg->SetOpcode(0x3101);
            WriteByte(pMsg, 2);
            WriteByte(pMsg, 2);
            // BUG(GM-005): the percentage sent is the one of the previous message.
            pMsg->WriteBytes(&byPercent, 1);
            SendAndRelease(m_dwSessionID, pMsg);
            byPercent = (BYTE)nPercent;
        }
    }
    if (g_pBSNet->GetSessionSocketState(m_dwSessionID) == -1)
    {
        m_dwSessionID = 0;
        return false;
    }
    const MODULE_VERSION* pModule = m_pModule;
    if (query.ExecuteFormat("update _ModuleVersion set nValid = 0 where nModuleID = %d and "
                            "nContentID = %d", pModule->byModuleID, pModule->ab0[5]) < 0)
    {
        return false;
    }
    return query.ExecuteFormat("insert _ModuleVersion values( %d, %d, %d, %d, '%s', '%s', 1)",
                               pModule->ab0[4], pModule->ab0[5], pModule->byModuleID,
                               pModule->dwVersion, (const char*)pModule + 11,
                               (const char*)pModule + 75) >= 0;
}

bool COverlapJob_RecordPatchData::OnComplete()
{
    ModuleVersion_FreeAllData();
    CServerProcessBase* pProcess = OwnerProcess(m_pOwner);
    ServerBodyList* pBodies = g_pServerBodyList;
    for (ServerBodyList::iterator it = pBodies->begin(); it != pBodies->end(); ++it)
    {
        SERVER_BODY* pBody = *it;
        if (pBody->pModule != CertModule_FindByName("GatewayServer")
            && pBody->pModule != CertModule_FindByName("DownloadServer"))
        {
            continue;
        }
        SERVER_CORD* pCord = ServerBody_Find(pBody->wID);
        if (pCord->dwSessionID)
        {
            SM_JOB* pJob = OverlapJob_AllocModuleVersionData();
            OverlapJob_SetupModuleVersionData(m_pOwner, pJob, pCord->dwSessionID);
            pProcess->SendOverlapJob(pJob, NULL);
        }
        else
        {
            LogPrintf(0x1000000, "cannot send latest module data to %d:%s", pBody->wID,
                      pBody->pModule->szName);
        }
    }
    if (m_dwSessionID)
    {
        CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
        pMsg->SetOpcode(0x3102);
        WriteByte(pMsg, 1);
        SendAndRelease(m_dwSessionID, pMsg);
    }
    return true;
}

bool COverlapJob_RecordPatchData::OnFail()
{
    ModuleVersion_FreeAllData();
    if (m_dwSessionID)
    {
        CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
        pMsg->SetOpcode(0x3102);
        WriteByte(pMsg, 2);
        WriteByte(pMsg, 4);
        SendAndRelease(m_dwSessionID, pMsg);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_RefreshSecurityData_n_SendToSM

bool COverlapJob_RefreshSecurityData_n_SendToSM::Prepare()
{
    return true;
}

int COverlapJob_RefreshSecurityData_n_SendToSM::OnSecurityRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    DWORD dwType = pQuery->m_dwParam;
    if (!pQuery->m_bRowFetched)
    {
        switch (dwType)
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
    switch (dwType)
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

// Copies the text into the query (StringCchCopy) and executes it; asserts when it does not fit.
static int ExecuteText(CDBQuery* pQuery, const char* pszText, bool* pbCopied)
{
    *pbCopied = SUCCEEDED(StringCchCopyA(pQuery->m_szQuery, sizeof(pQuery->m_szQuery),
                                         pszText));
    if (!*pbCopied)
    {
        BSAssertFail();
        return -1;
    }
    return pQuery->Execute();
}

bool COverlapJob_RefreshSecurityData_n_SendToSM::Execute()
{
    // BUG(GM-012): the new lists (and the records read) are not freed when a query fails.
    CertDataSettingList* pSettings = new CertDataSettingList;
    CertDataGroupList* pGroups = new CertDataGroupList;
    CertDataGroupIPList* pGroupIPs = new CertDataGroupIPList;
    CClassTrigger<COverlapJob_RefreshSecurityData_n_SendToSM> trigger(this);
    trigger.m_pfnTrigger2 = &COverlapJob_RefreshSecurityData_n_SendToSM::OnSecurityRow;
    // The query is not cleared: the trigger sets the buffer of every row.
    CDBQuery query;
    query.m_pUserData = pSettings;
    query.m_pTrigger = &trigger;
    query.m_pSession = g_pDefaultDBSession;
    query.m_dwParam = 0;
    query.m_nSession = 0;
    bool bCopied;
    if (ExecuteText(&query, "select * from _SecurityDescription with (NOLOCK)", &bCopied) < 0)
        return false;
    query.m_pUserData = pGroups;
    query.m_dwParam = 1;
    if (ExecuteText(&query, "select * from _SecurityDescriptionGroup with (NOLOCK)", &bCopied)
        < 0)
    {
        return false;
    }
    query.m_pUserData = pGroupIPs;
    query.m_dwParam = 2;
    if (ExecuteText(&query, "select * from _SecurityDescriptionGroupAssign with (NOLOCK)",
                    &bCopied) < 0)
    {
        return false;
    }
    CertData_FreeRecordListsAndIPMaps();
    CertData_BuildPrivilegedIPMaps(pGroups, pSettings, pGroupIPs);
    return true;
}

bool COverlapJob_RefreshSecurityData_n_SendToSM::OnComplete()
{
    CMassiveMsg* pMsg = NewReply(0x3306);
    WriteByte(pMsg, 1);
    CertData_SerializeRecordListsToMsg(pMsg);
    pMsg->SendTo(m_dwSessionID, FALSE);
    return true;
}

bool COverlapJob_RefreshSecurityData_n_SendToSM::OnFail()
{
    SendFailure(0x3306, m_dwSessionID);
    return true;
}


// ---------------------------------------------------------------------------------------------
// The SMC queries with one result set: the rows go to the reply through the trigger (the row
// buffer is the query's m_dwParam, the reply its m_pUserData); a failed query frees the reply.

#pragma pack(push, 1)
struct SM_LOG_ROW               { BYTE ab[401]; };
struct SM_CONTENT_ACCOUNT_ROW   { BYTE ab[165]; };
struct SM_PUNISHMENT_ROW        { BYTE ab[2124]; };
struct SM_CHAR_OWNER_ROW        { BYTE ab[194]; };
struct SM_USER_INFO_ROW         { BYTE ab[128]; };
struct SM_ARTICLE_ROW           { BYTE ab[1125]; };
struct SM_CONCURRENT_USER_ROW   { BYTE ab[38]; };
#pragma pack(pop)

// COverlapJob_QueryUser

bool COverlapJob_QueryUser::Prepare()
{
    return true;
}

int COverlapJob_QueryUser::OnRow(DWORD dwQuery, DWORD)
{
    return OnReplyRow((CDBQuery*)dwQuery, sizeof(SM_USER_RECORD));
}

bool COverlapJob_QueryUser::Execute()
{
    CClassTrigger<COverlapJob_QueryUser> trigger(this);
    trigger.m_pfnTrigger2 = &COverlapJob_QueryUser::OnRow;
    SM_USER_RECORD row;
    // The query is not cleared.
    CDBQuery query;
    query.m_pSession = g_pDefaultDBSession;
    query.m_pTrigger = &trigger;
    query.m_dwParam = (DWORD)&row;
    query.m_pUserData = m_pReply;
    query.m_nSession = 0;
    if (query.ExecuteFormat("exec _QueryUser '%s'", m_strUserID.c_str()) >= 0)
        return true;
    m_pReply->Free();
    m_pReply = NULL;
    return false;
}

bool COverlapJob_QueryUser::OnComplete()
{
    WriteByte(m_pReply, 0);
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

bool COverlapJob_QueryUser::OnFail()
{
    CMassiveMsg* pMsg = NewReply(0xB400);
    WriteByte(pMsg, 2);
    WriteByte(pMsg, 1);
    pMsg->SendTo(m_dwSessionID, FALSE);
    return true;
}

// COverlapJob_SMLog

bool COverlapJob_SMLog::Prepare()
{
    return true;
}

bool COverlapJob_SMLog::Execute()
{
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pDefaultDBSession;
    query.m_nSession = 0;
    query.ExecuteFormat("insert _SMCLog values ('%s', %d, '%s', default)", m_strUserID.c_str(),
                        m_byType, m_strText.c_str());
    return true;
}

bool COverlapJob_SMLog::OnComplete()
{
    return true;
}

bool COverlapJob_SMLog::OnFail()
{
    return true;
}

// COverlapJob_QuerySMLog

bool COverlapJob_QuerySMLog::Prepare()
{
    return true;
}

int COverlapJob_QuerySMLog::OnRow(DWORD dwQuery, DWORD)
{
    return OnReplyRow((CDBQuery*)dwQuery, sizeof(SM_LOG_ROW));
}

bool COverlapJob_QuerySMLog::Execute()
{
    CClassTrigger<COverlapJob_QuerySMLog> trigger(this);
    trigger.m_pfnTrigger2 = &COverlapJob_QuerySMLog::OnRow;
    SM_LOG_ROW row;
    // The query is not cleared.
    CDBQuery query;
    query.m_dwParam = (DWORD)&row;
    query.m_pSession = g_pDefaultDBSession;
    query.m_pTrigger = &trigger;
    query.m_pUserData = m_pReply;
    query.m_nSession = 0;
    if (query.ExecuteFormat("exec _GetSMLog %d, '%s', '%s', '%s'", m_byType,
                            m_strUserID.c_str(), m_strBegin.c_str(), m_strEnd.c_str()) >= 0)
    {
        return true;
    }
    m_pReply->Free();
    m_pReply = NULL;
    return false;
}

bool COverlapJob_QuerySMLog::OnComplete()
{
    WriteByte(m_pReply, 0);
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

bool COverlapJob_QuerySMLog::OnFail()
{
    SendFailure(0xB404, m_dwSessionID);
    return true;
}

// COverlapJob_QueryContentUserAccount

bool COverlapJob_QueryContentUserAccount::Prepare()
{
    return true;
}

int COverlapJob_QueryContentUserAccount::OnRow(DWORD dwQuery, DWORD)
{
    return OnReplyRow((CDBQuery*)dwQuery, sizeof(SM_CONTENT_ACCOUNT_ROW));
}

bool COverlapJob_QueryContentUserAccount::Execute()
{
    CClassTrigger<COverlapJob_QueryContentUserAccount> trigger(this);
    trigger.m_pfnTrigger2 = &COverlapJob_QueryContentUserAccount::OnRow;
    SM_CONTENT_ACCOUNT_ROW row;
    // The query is not cleared.
    CDBQuery query;
    query.m_pSession = g_pAccountDBSession;
    query.m_pTrigger = &trigger;
    query.m_dwParam = (DWORD)&row;
    query.m_pUserData = m_pReply;
    query.m_nSession = 0;
    if (query.ExecuteFormat("exec _ViewContentUser %d,'%s'", m_byType, m_strQuery.c_str()) >= 0)
        return true;
    m_pReply->Free();
    m_pReply = NULL;
    return false;
}

bool COverlapJob_QueryContentUserAccount::OnComplete()
{
    WriteByte(m_pReply, 0);
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

bool COverlapJob_QueryContentUserAccount::OnFail()
{
    SendFailure(0xB406, m_dwSessionID);
    return true;
}

// COverlapJob_QueryPunishment

bool COverlapJob_QueryPunishment::Prepare()
{
    return true;
}

int COverlapJob_QueryPunishment::OnRow(DWORD dwQuery, DWORD)
{
    return OnReplyRow((CDBQuery*)dwQuery, sizeof(SM_PUNISHMENT_ROW));
}

bool COverlapJob_QueryPunishment::Execute()
{
    CClassTrigger<COverlapJob_QueryPunishment> trigger(this);
    trigger.m_pfnTrigger2 = &COverlapJob_QueryPunishment::OnRow;
    // BUG(GM-007): the JID is not initialised; without a row the stale stack value is used.
    DWORD dwJID;
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pBuffer = (BYTE*)&dwJID;
    query.m_pSession = g_pAccountDBSession;
    query.m_nRowSize = 4;
    query.m_nBufferSize = 4;
    query.m_nSession = 0;
    if (query.ExecuteFormat("exec _GetUserJID '%s'", m_strUserID.c_str()) >= 0
        && dwJID != 0xFFFFFFFF)
    {
        SM_USER_INFO_ROW info;
        memset(&info, 0, sizeof(info));
        memset(&query, 0, sizeof(query));
        query.m_pSession = g_pAccountDBSession;
        query.m_pBuffer = info.ab;
        query.m_nRowSize = sizeof(info);
        query.m_nBufferSize = sizeof(info);
        query.m_nSession = 0;
        if (query.ExecuteFormat("exec _GetUserInfo %d", dwJID) >= 0)
        {
            m_pReply->Write(&dwJID, 4);
            m_pReply->WriteString((const char*)info.ab);
            // BUG(GM-006): the second text of the account is written twice.
            m_pReply->WriteString((const char*)info.ab + 0x14);
            m_pReply->WriteString((const char*)info.ab + 0x14);
            m_pReply->WriteString((const char*)info.ab + 0x2A);
            m_pReply->WriteString((const char*)info.ab + 0x3A);
            SM_PUNISHMENT_ROW row;
            memset(&query, 0, sizeof(query));
            query.m_pTrigger = &trigger;
            query.m_pSession = g_pAccountDBSession;
            query.m_dwParam = (DWORD)&row;
            query.m_pUserData = m_pReply;
            query.m_nSession = 0;
            if (query.ExecuteFormat("select * from _Punishment where UserJID = %d", dwJID) >= 0)
                return true;
        }
    }
    m_pReply->Free();
    m_pReply = NULL;
    return false;
}

bool COverlapJob_QueryPunishment::OnComplete()
{
    WriteByte(m_pReply, 0);
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

bool COverlapJob_QueryPunishment::OnFail()
{
    SendFailure(0xB407, m_dwSessionID);
    return true;
}

// COverlapJob_PunishmentJob

bool COverlapJob_PunishmentJob::Prepare()
{
    return true;
}

bool COverlapJob_PunishmentJob::Execute()
{
    // BUG(GM-007): the result is not initialised; without a row the stale stack value decides.
    int nResult;
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pAccountDBSession;
    query.m_pBuffer = (BYTE*)&nResult;
    query.m_nRowSize = 4;
    query.m_nBufferSize = 4;
    query.m_nSession = 0;
    switch (m_byJob)
    {
    case 0:
    {
        std::string strT4, strT3, strT2, strT1;
        SM_FormatDateTime(&strT4, (const TIMESTAMP_STRUCT*)&m_t4);
        SM_FormatDateTime(&strT3, (const TIMESTAMP_STRUCT*)&m_t3);
        SM_FormatDateTime(&strT2, (const TIMESTAMP_STRUCT*)&m_t2);
        SM_FormatDateTime(&strT1, (const TIMESTAMP_STRUCT*)&m_t1);
        bool bFailed = query.ExecuteFormat(
            "EXEC _RegisterPunishment %d,%d,'%s',%d,'%s','%s','%s','%s','%s','%s','%s','%s','%s'",
            m_dwJID, m_byType, m_str1.c_str(), m_wShardID, m_str2.c_str(), m_str3.c_str(),
            m_str4.c_str(), m_str5.c_str(), m_str6.c_str(), strT1.c_str(), strT2.c_str(),
            strT3.c_str(), strT4.c_str()) < 0;
        if (bFailed)
        {
            m_byError = 1;
            return false;
        }
        if (nResult < 0)
        {
            m_byError = (BYTE)((nResult == -1) + 1);
            return false;
        }
        return true;
    }
    case 1:
    {
        std::string strT4, strT3, strT2, strT1;
        SM_FormatDateTime(&strT4, (const TIMESTAMP_STRUCT*)&m_t4);
        SM_FormatDateTime(&strT3, (const TIMESTAMP_STRUCT*)&m_t3);
        SM_FormatDateTime(&strT2, (const TIMESTAMP_STRUCT*)&m_t2);
        SM_FormatDateTime(&strT1, (const TIMESTAMP_STRUCT*)&m_t1);
        bool bFailed = query.ExecuteFormat(
            "EXEC _UpdatePunishment %d,%d,%d,'%s', %d, '%s', '%s', '%s', '%s', '%s', '%s', '%s', "
            "'%s', '%s'",
            m_dwSerial, m_dwJID, m_byType, m_str1.c_str(), m_wShardID, m_str2.c_str(),
            m_str3.c_str(), m_str4.c_str(), m_str5.c_str(), m_str6.c_str(), strT1.c_str(),
            strT2.c_str(), strT3.c_str(), strT4.c_str()) < 0;
        if (bFailed)
        {
            m_byError = 1;
            return false;
        }
        if (nResult < 0)
        {
            m_byError = (BYTE)(2 * (nResult == -1) + 1);
            return false;
        }
        return true;
    }
    }
    m_byError = 4;
    return false;
}

bool COverlapJob_PunishmentJob::OnComplete()
{
    CMsg* pMsg = g_pBSNet->AllocMsg(TRUE);
    pMsg->SetOpcode(0xB408);
    WriteByte(pMsg, 1);
    pMsg->WriteBytes(&m_byJob, 1);
    SendAndRelease(m_dwSessionID, pMsg);
    if (!g_pPunishmentManager)
        BSAssertFail();
    PunishmentManager_Add(g_pPunishmentManager, m_dwJID, m_byType,
                          (const TIMESTAMP_STRUCT*)&m_t2, (const TIMESTAMP_STRUCT*)&m_t3);
    LogPrintf(0, "New Punishment inserted!");
    return true;
}

bool COverlapJob_PunishmentJob::OnFail()
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(0xB408);
    WriteByte(pMsg, 2);
    pMsg->WriteBytes(&m_byJob, 1);
    pMsg->WriteBytes(&m_byError, 1);
    SendAndRelease(m_dwSessionID, pMsg);
    return true;
}

// COverlapJob_AutomatedPunishmentJob

bool COverlapJob_AutomatedPunishmentJob::Prepare()
{
    return true;
}

bool COverlapJob_AutomatedPunishmentJob::Execute()
{
    // BUG(GM-007): the result is not initialised.
    int nResult;
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pAccountDBSession;
    query.m_pBuffer = (BYTE*)&nResult;
    query.m_nRowSize = 4;
    query.m_nBufferSize = 4;
    query.m_nSession = 0;
    char szQuery[0x2800];
    sprintf_s(szQuery, 0x2800, "EXEC _RegisterAutomatedPunishment '%s',%d,'%s','%s','%s',%d",
              m_strUserID.c_str(), 1, m_str2.c_str(), m_str3.c_str(), m_str4.c_str(),
              m_dwParam);
    // BUG(GM-003): the formatted text is the format of the query (no escaping either).
    if (query.ExecuteFormat(szQuery) < 0)
    {
        LogPrintf(0x1000000, "Query failed!: [%s]", szQuery);
        return false;
    }
    if (nResult < 0)
    {
        if (nResult != -1)
        {
            LogPrintf(0x1000000,
                      "_RegisterAutomatedPunishment Query failed!: [ret: %d, User: %s]",
                      nResult, m_strUserID.c_str());
        }
        return false;
    }
    m_nResult = nResult;
    return true;
}

bool COverlapJob_AutomatedPunishmentJob::OnComplete()
{
    return true;
}

bool COverlapJob_AutomatedPunishmentJob::OnFail()
{
    return true;
}

// COverlapJob_QueryCharOwnedUser / COverlapJob_QueryUserByUserID

bool COverlapJob_QueryCharOwnedUser::Prepare()
{
    return true;
}

int COverlapJob_QueryCharOwnedUser::OnRow(DWORD dwQuery, DWORD)
{
    return OnReplyRow((CDBQuery*)dwQuery, sizeof(SM_CHAR_OWNER_ROW));
}

bool COverlapJob_QueryCharOwnedUser::Execute()
{
    CClassTrigger<COverlapJob_QueryCharOwnedUser> trigger(this);
    trigger.m_pfnTrigger2 = &COverlapJob_QueryCharOwnedUser::OnRow;
    SM_CHAR_OWNER_ROW row;
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pAccountDBSession;
    query.m_pTrigger = &trigger;
    query.m_dwParam = (DWORD)&row;
    query.m_pUserData = m_pReply;
    query.m_nSession = 0;
    if (query.ExecuteFormat("exec _QueryCharOwnedUser '%s'", m_strName.c_str()) >= 0)
        return true;
    m_pReply->Free();
    m_pReply = NULL;
    return false;
}

bool COverlapJob_QueryCharOwnedUser::OnComplete()
{
    WriteByte(m_pReply, 0);
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

bool COverlapJob_QueryCharOwnedUser::OnFail()
{
    SendFailure(0xB409, m_dwSessionID);
    return true;
}

bool COverlapJob_QueryUserByUserID::Prepare()
{
    return true;
}

int COverlapJob_QueryUserByUserID::OnRow(DWORD dwQuery, DWORD)
{
    return OnReplyRow((CDBQuery*)dwQuery, sizeof(SM_CHAR_OWNER_ROW));
}

bool COverlapJob_QueryUserByUserID::Execute()
{
    CClassTrigger<COverlapJob_QueryUserByUserID> trigger(this);
    trigger.m_pfnTrigger2 = &COverlapJob_QueryUserByUserID::OnRow;
    SM_CHAR_OWNER_ROW row;
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pAccountDBSession;
    query.m_pTrigger = &trigger;
    query.m_dwParam = (DWORD)&row;
    query.m_pUserData = m_pReply;
    query.m_nSession = 0;
    if (query.ExecuteFormat("exec _QueryUserByUserID '%s'", m_strUserID.c_str()) >= 0)
        return true;
    m_pReply->Free();
    m_pReply = NULL;
    return false;
}

bool COverlapJob_QueryUserByUserID::OnComplete()
{
    WriteByte(m_pReply, 0);
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

bool COverlapJob_QueryUserByUserID::OnFail()
{
    SendFailure(0xB409, m_dwSessionID);
    return true;
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_PreparePatch

bool COverlapJob_PreparePatch::Prepare()
{
    return g_pBSNet->GetSessionSocketState(m_dwSessionID) != -1;
}

int COverlapJob_PreparePatch::OnPatchRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    int nSize = pQuery->m_pUserData ? (int)sizeof(MODULE_VERSION_FILE)
                                    : (int)sizeof(MODULE_VERSION);
    if (!pQuery->m_bRowFetched)
    {
        BYTE* pBuffer = (BYTE*)pQuery->m_dwParam;
        pQuery->m_pBuffer = pBuffer;
        pQuery->m_nRowSize = nSize;
        pQuery->m_nBufferSize = nSize;
        memset(pBuffer, 0, nSize);
        return 1;
    }
    WriteByte(m_pReply, 1);
    m_pReply->Write(pQuery->m_pBuffer, nSize);
    m_bFound = TRUE;
    return 1;
}

bool COverlapJob_PreparePatch::Execute()
{
    CClassTrigger<COverlapJob_PreparePatch> trigger(this);
    trigger.m_pfnTrigger2 = &COverlapJob_PreparePatch::OnPatchRow;
    MODULE_VERSION module;
    MODULE_VERSION_FILE file;
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pDefaultDBSession;
    query.m_pTrigger = &trigger;
    query.m_dwParam = (DWORD)&module;
    query.m_pUserData = NULL;
    query.m_nSession = 0;
    if (query.ExecuteFormat("select * from _ModuleVersion with(NOLOCK) where nModuleID = %d and "
                            "nContentID = %d and nValid = 1", m_byModuleID,
                            m_byContentID) < 0)
    {
        LogPrintf(0x2000000, "an error occured during select _ModuleVersion data from DB "
                             "[ContentID: %d, ModuleID: %d]", m_byContentID, m_byModuleID);
        m_pReply->Free();
        m_pReply = NULL;
        return false;
    }
    if (!m_bFound)
    {
        // BUG(GM-011): the reply is kept; OnFail sends a new one and this one is lost.
        LogPrintf(0x2000000, "can't find any version info from table _ModuleVersion "
                             "[ContentID: %d, ModuleID: %d]", m_byContentID, m_byModuleID);
        return false;
    }
    WriteByte(m_pReply, 0);
    m_bFound = FALSE;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pDefaultDBSession;
    query.m_pTrigger = &trigger;
    query.m_dwParam = (DWORD)&file;
    query.m_pUserData = (void*)1;
    query.m_nSession = 0;
    if (query.ExecuteFormat("select * from _ModuleVersionFile with(NOLOCK) where nModuleID = %d "
                            "and nContentID = %d and nValid = 1", m_byModuleID,
                            m_byContentID) < 0)
    {
        LogPrintf(0x2000000, "an error occured during select _ModuleVersionFile data from DB "
                             "[ContentID: %d, ModuleID: %d]", m_byContentID, m_byModuleID);
        m_pReply->Free();
        m_pReply = NULL;
        return false;
    }
    WriteByte(m_pReply, 0);
    return true;
}

bool COverlapJob_PreparePatch::OnComplete()
{
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

bool COverlapJob_PreparePatch::OnFail()
{
    m_pReply = NewReply(0xB104);
    WriteByte(m_pReply, 2);
    WriteByte(m_pReply, 3);
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_ArticleJob

bool COverlapJob_ArticleJob::Prepare()
{
    return true;
}

// Article row: DWORD ID, BYTE, title (80), text (1024), TIMESTAMP_STRUCT edit date.
int COverlapJob_ArticleJob::OnArticleRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (!pQuery->m_bRowFetched)
    {
        memset(pQuery->m_pBuffer, 0, sizeof(SM_ARTICLE_ROW));
        return 1;
    }
    const BYTE* pRow = pQuery->m_pBuffer;
    WriteByte(m_pReply, 1);
    m_pReply->Write(pRow, 4);
    m_pReply->Write(pRow + 4, 1);
    WORD wLen = (WORD)lstrlenA((LPCSTR)(pRow + 5));
    m_pReply->Write(&wLen, 2);
    if (wLen)
        m_pReply->Write(pRow + 5, wLen);
    wLen = (WORD)lstrlenA((LPCSTR)(pRow + 85));
    m_pReply->Write(&wLen, 2);
    if (wLen)
        m_pReply->Write(pRow + 85, wLen);
    m_pReply->Write(pRow + 1109, 16);
    return 1;
}

bool COverlapJob_ArticleJob::Execute()
{
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    CClassTrigger<COverlapJob_ArticleJob> trigger(this);
    trigger.m_pfnTrigger2 = &COverlapJob_ArticleJob::OnArticleRow;
    SM_ARTICLE_ROW row;
    query.m_pBuffer = row.ab;
    query.m_nRowSize = sizeof(row);
    query.m_nBufferSize = sizeof(row);
    query.m_pSession = g_pDefaultDBSession;
    ReplaceQuotesWithSpace(&m_strTitle);
    ReplaceQuotesWithSpace(&m_strText);
    int nResult;
    switch (m_byType)
    {
    case 0:
        query.m_pTrigger = &trigger;
        nResult = query.ExecuteFormat("select * from _Notice where ContentID = %d order by "
                                      "EditDate desc", m_byContentID);
        break;
    case 1:
        nResult = query.ExecuteFormat("exec _AddArticleData %d,'%s','%s'", m_byContentID,
                                      m_strTitle.c_str(), m_strText.c_str());
        break;
    case 2:
        nResult = query.ExecuteFormat("exec _DeleteArticleData %d", m_dwArticleID);
        break;
    case 3:
        nResult = query.ExecuteFormat("exec _UpdateArticleData %d,'%s','%s'", m_dwArticleID,
                                      m_strTitle.c_str(), m_strText.c_str());
        break;
    default:
        return true;
    }
    return nResult >= 0;
}

void COverlapJob_ArticleJob::Answer(BYTE byResult)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    // Other types keep the opcode of the new message.
    switch (m_byType)
    {
    case 1:
        pMsg->SetOpcode(0xB207);
        break;
    case 2:
        pMsg->SetOpcode(0xB208);
        break;
    case 3:
        pMsg->SetOpcode(0xB209);
        break;
    }
    WriteByte(pMsg, byResult);
    SendAndRelease(m_dwSessionID, pMsg);
}

bool COverlapJob_ArticleJob::OnComplete()
{
    if (m_byType)
    {
        Answer(1);
        return true;
    }
    WriteByte(m_pReply, 0);
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

bool COverlapJob_ArticleJob::OnFail()
{
    if (m_byType)
    {
        Answer(2);
        return true;
    }
    m_pReply->Free();
    m_pReply = NewReply(0xB206);
    WriteByte(m_pReply, 2);
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

// ---------------------------------------------------------------------------------------------
// COverlapJob_ConcurrentUserLogJob

bool COverlapJob_ConcurrentUserLogJob::Prepare()
{
    return true;
}

int COverlapJob_ConcurrentUserLogJob::OnLogRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (pQuery->m_bRowFetched)
    {
        WriteByte(m_pReply, 1);
        m_pReply->Write(pQuery->m_pBuffer, sizeof(SM_CONCURRENT_USER_ROW));
    }
    else
    {
        memset(pQuery->m_pBuffer, 0, sizeof(SM_CONCURRENT_USER_ROW));
    }
    return 1;
}

bool COverlapJob_ConcurrentUserLogJob::Execute()
{
    m_pReply->SetOpcode(0xB412);
    m_pReply->Write(&m_wShardID, 2);
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    CClassTrigger<COverlapJob_ConcurrentUserLogJob> trigger(this);
    trigger.m_pfnTrigger2 = &COverlapJob_ConcurrentUserLogJob::OnLogRow;
    SM_CONCURRENT_USER_ROW row;
    query.m_pBuffer = row.ab;
    query.m_nRowSize = sizeof(row);
    query.m_nBufferSize = sizeof(row);
    query.m_pSession = g_pDefaultDBSession;
    query.m_pTrigger = &trigger;
    if (query.ExecuteFormat("exec _GetConcurrentUserLog %d, %d, '%s', '%s' ", m_wShardID,
                            m_byType, m_strBegin.c_str(), m_strEnd.c_str()) < 0)
    {
        return false;
    }
    WriteByte(m_pReply, 0);
    return true;
}

bool COverlapJob_ConcurrentUserLogJob::OnComplete()
{
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}

bool COverlapJob_ConcurrentUserLogJob::OnFail()
{
    WriteByte(m_pReply, 0);
    m_pReply->SendTo(m_dwSessionID, FALSE);
    return true;
}
