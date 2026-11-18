#include "ServiceManagerProcess.h"

#include <string.h>
#include <strsafe.h>

#include "GlobalManagerSvcFwd.h"
#include "BSLib/BSDebug.h"
#include "BSNet/IBSNet.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerProcess.h"

using namespace ServerFramework;

CChunkAllocatorST<sServiceManagerUser> g_ServiceManagerUserPool;
CChunkAllocatorST<sSMDefault_RelayMsg_Param> g_SMRelayParamPool;

// ---------------------------------------------------------------------------------------------
// Pools

void __stdcall sServiceManagerUser::Release()
{
    if (bInUse)
    {
        bInUse = 0;
        g_ServiceManagerUserPool.Free(this);
    }
}

void __stdcall sSMDefault_RelayMsg_Param::Release()
{
    if (bInUse)
    {
        bInUse = 0;
        g_SMRelayParamPool.Free(this);
    }
}

sServiceManagerUser* ServiceManagerUserPool_Alloc()
{
    sServiceManagerUser* pUser = g_ServiceManagerUserPool.Alloc();
    pUser->bInUse = 1;
    return pUser;
}

sSMDefault_RelayMsg_Param* SMRelayParamPool_Alloc()
{
    sSMDefault_RelayMsg_Param* pParam = g_SMRelayParamPool.Alloc();
    pParam->bInUse = 1;
    return pParam;
}

void SM_FormatDateTime(std::string* pstrOut, const TIMESTAMP_STRUCT* pTime)
{
    char szText[MAX_PATH];
    if (FAILED(StringCchPrintfA(szText, MAX_PATH, "%d-%02d-%02d %02d:%02d:%02d", pTime->year,
                                pTime->month, pTime->day, pTime->hour, pTime->minute,
                                pTime->second)))
    {
        BSAssertFail();
    }
    pstrOut->assign(szText, strlen(szText));
}

// ---------------------------------------------------------------------------------------------
// CServiceManagerProcess

CServiceManagerProcess::CServiceManagerProcess()
{
    memset(m_aHandler, 0, sizeof(m_aHandler));
    m_aHandler[0x7001] = &CServiceManagerProcess::OnMsg_7001;
    m_aHandler[0x7002] = &CServiceManagerProcess::OnMsg_7002;
    m_aHandler[0x7003] = &CServiceManagerProcess::OnMsg_7003;
    m_aHandler[0x7004] = &CServiceManagerProcess::OnMsg_7004;
    m_aHandler[0x7005] = &CServiceManagerProcess::OnMsg_7005;
    m_aHandler[0x7006] = &CServiceManagerProcess::OnMsg_7006;
    m_aHandler[0x7007] = &CServiceManagerProcess::OnMsg_7007;
    m_aHandler[0xA300] = &CServiceManagerProcess::OnMsg_A300;
    m_aHandler[0xA301] = &CServiceManagerProcess::OnMsg_A301;
    m_aHandler[0xA302] = &CServiceManagerProcess::OnMsg_A302;
    m_aHandler[0x7308] = &CServiceManagerProcess::OnMsg_7308;
    m_aHandler[0xA31D] = &CServiceManagerProcess::OnMsg_A31D;
    m_aHandler[0x7008] = &CServiceManagerProcess::OnMsg_7008;
    m_aHandler[0xA303] = &CServiceManagerProcess::OnMsg_A303;
    m_aHandler[0x7009] = &CServiceManagerProcess::OnMsg_7009;
    m_aHandler[0xA304] = &CServiceManagerProcess::OnMsg_A304;
    m_aHandler[0x7100] = &CServiceManagerProcess::OnMsg_7100;
    m_aHandler[0xA305] = &CServiceManagerProcess::OnMsg_A305;
    m_aHandler[0x7104] = &CServiceManagerProcess::OnMsg_7104;
    m_aHandler[0xA308] = &CServiceManagerProcess::OnMsg_A308;
    m_aHandler[0x7200] = &CServiceManagerProcess::OnMsg_7200;
    m_aHandler[0x7201] = &CServiceManagerProcess::OnMsg_7201;
    m_aHandler[0x7202] = &CServiceManagerProcess::OnMsg_7202;
    m_aHandler[0xA307] = &CServiceManagerProcess::OnMsg_A307;
    m_aHandler[0x7203] = &CServiceManagerProcess::OnMsg_7203;
    m_aHandler[0x7204] = &CServiceManagerProcess::OnMsg_7204;
    m_aHandler[0xA20D] = &CServiceManagerProcess::OnMsg_A20D;
    m_aHandler[0x7300] = &CServiceManagerProcess::OnMsg_7300;
    m_aHandler[0x7301] = &CServiceManagerProcess::OnMsg_7301;
    m_aHandler[0x7302] = &CServiceManagerProcess::OnMsg_7302;
    m_aHandler[0x7303] = &CServiceManagerProcess::OnMsg_7303;
    m_aHandler[0x7304] = &CServiceManagerProcess::OnMsg_7304;
    m_aHandler[0x7305] = &CServiceManagerProcess::OnMsg_7305;
    m_aHandler[0x7307] = &CServiceManagerProcess::OnMsg_7307;
    m_aHandler[0x7400] = &CServiceManagerProcess::OnMsg_7400;
    m_aHandler[0x7401] = &CServiceManagerProcess::OnMsg_7401;
    m_aHandler[0x7402] = &CServiceManagerProcess::OnMsg_7402;
    m_aHandler[0x7403] = &CServiceManagerProcess::OnMsg_7403;
    m_aHandler[0x7404] = &CServiceManagerProcess::OnMsg_7404;
    m_aHandler[0x7405] = &CServiceManagerProcess::OnMsg_7405;
    m_aHandler[0x7407] = &CServiceManagerProcess::OnMsg_7407;
    m_aHandler[0x7408] = &CServiceManagerProcess::OnMsg_7408;
    m_aHandler[0x7409] = &CServiceManagerProcess::OnMsg_7409;
    m_aHandler[0x7410] = &CServiceManagerProcess::OnMsg_7410;
    m_aHandler[0x7205] = &CServiceManagerProcess::OnMsg_7205;
    m_aHandler[0x7010] = &CServiceManagerProcess::OnMsg_7010;
    m_aHandler[0xA310] = &CServiceManagerProcess::OnMsg_A310;
    m_aHandler[0x7206] = &CServiceManagerProcess::OnMsg_7206;
    m_aHandler[0x7207] = &CServiceManagerProcess::OnMsg_7207;
    m_aHandler[0x7208] = &CServiceManagerProcess::OnMsg_7208;
    m_aHandler[0x7209] = &CServiceManagerProcess::OnMsg_7209;
    m_aHandler[0x7011] = &CServiceManagerProcess::OnMsg_7011;
    m_aHandler[0xA312] = &CServiceManagerProcess::OnMsg_A312;
    m_aHandler[0x7012] = &CServiceManagerProcess::OnMsg_7012;
    m_aHandler[0x7411] = &CServiceManagerProcess::OnMsg_7411;
    m_aHandler[0x6313] = &CServiceManagerProcess::OnMsg_6313;
    m_aHandler[0x7412] = &CServiceManagerProcess::OnMsg_7412;
    m_aHandler[0x4023] = &CServiceManagerProcess::OnMsg_4023;
    m_aHandler[0x8023] = &CServiceManagerProcess::OnMsg_8023;
}

CServiceManagerProcess::~CServiceManagerProcess()
{
    for (SMUserMap::iterator it = m_mapUser.begin(); it != m_mapUser.end(); ++it)
        it->second->Release();
    m_mapUser.clear();
    m_setPrepareBody.clear();
    m_mapPatchTarget.clear();
}

void CServiceManagerProcess::Init(CTask* pTask)
{
    m_pTask = pTask;
    m_pPatchUser = NULL;
    m_bPatchUploading = 0;
    m_bPreparingPatch = 0;
    m_nPatchStep = -1;
    m_wPatchFilesLeft = 0;
    m_wPatchFileCount = 0;
}

bool CServiceManagerProcess::Dispatch(CMsg* pMsg, CMassiveMsg* pMassive, DWORD dwRelayID,
                                      DWORD dwContext)
{
    if (pMsg)
    {
        PFN_SM_HANDLER pfn = m_aHandler[pMsg->GetOpcode()];
        if (!pfn)
            return OnUnhandledAnswer(pMsg, pMassive, (sSMDefault_RelayMsg_Param*)dwContext);
        (this->*pfn)(pMsg, dwRelayID, dwContext, pMassive);
    }
    else if (pMassive)
    {
        PFN_SM_HANDLER pfn = m_aHandler[pMassive->m_wOpcode];
        if (!pfn)
            return OnUnhandledAnswer(NULL, pMassive, (sSMDefault_RelayMsg_Param*)dwContext);
        (this->*pfn)(NULL, dwRelayID, dwContext, pMassive);
    }
    return true;
}

// Requests (0x2xxx, 0x3xxx) are left to the caller.
static bool IsRequestOpcode(WORD wOpcode)
{
    return (wOpcode & 0x2000) != 0 || (wOpcode & 0x3000) == 0x3000;
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

bool CServiceManagerProcess::OnUnhandledAnswer(CMsg* pMsg, CMassiveMsg* pMassive,
                                               sSMDefault_RelayMsg_Param* pParam)
{
    BYTE byResult;
    if (pMsg)
    {
        if (IsRequestOpcode(pMsg->GetOpcode()))
            return false;
        CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
        pReply->SetOpcode(0xB203);
        DWORD dwOpcode = pMsg->GetOpcode();
        byResult = 1;
        pReply->WriteBytes(&byResult, 1);
        pReply->WriteBytes(&dwOpcode, 2);
        // The leading string (the account the answer is about) is not passed on.
        std::string strText;
        if (pMsg->GetOpcode() != 0x8004)
            *pMsg >> strText;
        pReply->WriteBytes(pMsg->m_pData + pMsg->m_wReadPos,
                           (WORD)(pMsg->m_wWritePos - pMsg->m_wReadPos));
        *pMsg >> byResult;
        if (byResult != 2 && strText.size() > 0)
            WriteAnswerLog(&strText, pParam->dwSessionID, SMLog_GetAnswerType(pMsg->GetOpcode()));
        g_pBSNet->SendMsg(pParam->dwSessionID, pReply);
        g_pBSNet->ReleaseMsg(pReply);
        pMsg->SkipRemaining();
        pParam->Release();
        return true;
    }
    if (!pMassive)
    {
        BSAssertFail();
        return false;
    }
    if (IsRequestOpcode(pMassive->m_wOpcode))
        return false;
    CMassiveMsg* pReply = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pReply->SetOpcode(0xB203);
    DWORD dwOpcode = pMassive->m_wOpcode;
    byResult = 1;
    pReply->Write(&byResult, 1);
    pReply->Write(&dwOpcode, 2);
    std::string strText;
    ReadMassiveString(pMassive, strText);
    pReply->AppendFrom(pMassive, TRUE);
    pMassive->Read(&byResult, 1);
    if (byResult != 2)
        WriteAnswerLog(&strText, pParam->dwSessionID, SMLog_GetAnswerType(pMassive->m_wOpcode));
    pReply->SendTo(pParam->dwSessionID, FALSE);
    pParam->Release();
    return true;
}

// Sends the reply to the SMC of the relay and releases it.
static void SendAndRelease(DWORD dwSessionID, CMsg* pReply)
{
    g_pBSNet->SendMsg(dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
}

void CServiceManagerProcess::OnRelayFailed(WORD wOpcode, sSMDefault_RelayMsg_Param* pParam)
{
    BYTE byFail = 2;
    BYTE byReason;
    CMsg* pReply;
    switch (wOpcode)
    {
    case 0x620D:
        pReply = g_pBSNet->AllocMsg(FALSE);
        pReply->SetOpcode(0xB201);
        byReason = 2;
        pReply->WriteBytes(&byFail, 1);
        pReply->WriteBytes(&byReason, 1);
        break;
    case 0x6300:
        pReply = g_pBSNet->AllocMsg(FALSE);
        pReply->SetOpcode(0xB005);
        pReply->WriteBytes(&byFail, 1);
        pReply->WriteBytes(&pParam->wBodyID, 2);
        break;
    case 0x6301:
    case 0x6302:
    case 0x631D:
        pReply = g_pBSNet->AllocMsg(FALSE);
        pReply->SetOpcode(wOpcode == 0x6301 ? 0xB006 : wOpcode == 0x6302 ? 0xB007 : 0xB308);
        byReason = 3;
        pReply->WriteBytes(&byFail, 1);
        pReply->WriteBytes(&byReason, 1);
        pReply->WriteBytes(&pParam->wBodyID, 2);
        break;
    case 0x6303:
    {
        pReply = g_pBSNet->AllocMsg(FALSE);
        pReply->SetOpcode(0xB008);
        BYTE byOperation = (BYTE)pParam->dw8;
        byReason = 1;
        pReply->WriteBytes(&byFail, 1);
        pReply->WriteBytes(&byReason, 1);
        pReply->WriteBytes(&byOperation, 1);
        pReply->WriteBytes(&pParam->wBodyID, 2);
        break;
    }
    case 0x6304:
        pReply = g_pBSNet->AllocMsg(FALSE);
        pReply->SetOpcode(0xB009);
        byReason = 2;
        pReply->WriteBytes(&byFail, 1);
        pReply->WriteBytes(&byReason, 1);
        pReply->WriteBytes(&pParam->wBodyID, 2);
        break;
    case 0x6307:
        pReply = g_pBSNet->AllocMsg(FALSE);
        pReply->SetOpcode(0xB202);
        pReply->WriteBytes(&pParam->wBodyID, 2);
        pReply->WriteBytes(&byFail, 1);
        break;
    case 0x6308:
    case 0x6312:
        // The patch preparation and the shard service relays have no context.
        return;
    case 0x6310:
    {
        // The context of the shard setting relay is the SMC session itself.
        pReply = g_pBSNet->AllocMsg(FALSE);
        pReply->SetOpcode(0xB010);
        pReply->WriteBytes(&byFail, 1);
        SendAndRelease((DWORD)pParam, pReply);
        return;
    }
    default:
    {
        if (wOpcode & 0x2000)
            return;
        pReply = g_pBSNet->AllocMsg(FALSE);
        pReply->SetOpcode(0xB203);
        DWORD dwOpcode = (WORD)pParam->dw8;
        pReply->WriteBytes(&byFail, 1);
        pReply->WriteBytes(&pParam->wBodyID, 2);
        pReply->WriteBytes(&dwOpcode, 2);
        break;
    }
    }
    SendAndRelease(pParam->dwSessionID, pReply);
    pParam->Release();
}

void CServiceManagerProcess::OnSessionClosed(DWORD dwSessionID)
{
    SMUserMap::iterator it = m_mapUser.find(dwSessionID);
    if (it == m_mapUser.end())
        return;
    sServiceManagerUser* pUser = it->second;
    g_setNotifySession.erase(dwSessionID);
    LogPrintf(0, "sm user logout : %s", pUser->strID.c_str());
    if (pUser == m_pPatchUser && m_bPatchUploading)
        CancelPatchUpload();
    if (dwSessionID == m_dwPrepareSession && m_bPreparingPatch)
    {
        m_bPreparingPatch = 0;
        m_setPrepareBody.clear();
    }
    WriteLog(dwSessionID, 9, "Logout : %s", pUser->strID.c_str());
    m_mapUser.erase(it);
    pUser->Release();
}

sServiceManagerUser* CServiceManagerProcess::FindUser(DWORD dwSessionID)
{
    SMUserMap::iterator it = m_mapUser.find(dwSessionID);
    if (it == m_mapUser.end())
        return NULL;
    return it->second;
}

void CServiceManagerProcess::BroadcastToUsers(CMsg* pMsg)
{
    pMsg->SkipRemaining();
    for (SMUserMap::iterator it = m_mapUser.begin(); it != m_mapUser.end(); ++it)
        g_pBSNet->SendMsg(it->second->dwSessionID, pMsg);
    g_pBSNet->ReleaseMsg(pMsg);
}

bool CServiceManagerProcess::PostJob(COverlapJob_Local* pJob)
{
    CServerProcessBase* pProcess = (CServerProcessBase*)m_pTask->GetJob();
    return pProcess->SendOverlapJob(pJob, NULL);
}

void CServiceManagerProcess::RefreshSecurityData(DWORD dwSessionID)
{
    SM_JOB* pJob = OverlapJob_AllocRefreshSecurityData();
    OverlapJob_SetupRefreshSecurityData(m_pTask, pJob, dwSessionID);
    PostJob(pJob);
}

bool CServiceManagerProcess::RegisterAutomatedPunishment(std::string* pstrUserID,
                                                         std::string* pstr2, std::string* pstr3,
                                                         std::string* pstr4, DWORD dwParam)
{
    SM_JOB* pJob = OverlapJob_AllocAutomatedPunishmentJob();
    OverlapJob_SetupAutomatedPunishmentJob(m_pTask, pJob, pstrUserID, pstr2, pstr3, pstr4,
                                           dwParam);
    bool bPosted = PostJob(pJob);
    if (!bPosted)
    {
        return LogPrintf(0x1000000, "RegisterAutomatedServerPunishment Failed for User[%s]",
                         pstrUserID->c_str()) != 0;
    }
    return bPosted;
}

void CServiceManagerProcess::CancelPatchUpload()
{
    m_pPatchUser = NULL;
    m_bPatchUploading = 0;
    m_nPatchStep = -1;
    m_wPatchFilesLeft = 0;
    m_wPatchFileCount = 0;
    ModuleVersion_FreeAllData();
    m_mapPatchTarget.clear();
    LogPrintf(0x1000000, "patch upload is canceled");
}
