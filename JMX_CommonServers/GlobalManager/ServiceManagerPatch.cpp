// CServiceManagerProcess: patches. The SMC prepares a patch (0x7104: every DownloadServer and
// every GatewayServer in service confirms 0x6308, then a PreparePatch job runs), uploads it
// (0x7100: the module version and its file list; GlobalManager then requests the files from the
// SMC through the framework file transfer) and GlobalManager passes it to the DownloadServers
// (0x6305 with the file list; they request the files). When every DownloadServer has all files
// the patch is recorded (RecordPatchData) and the DownloadServers reload the versions.
// Progress goes to the SMC as 0x3101 / 0x3102.

#include "ServiceManagerProcess.h"

#include <strsafe.h>

#include "GlobalManagerSvcFwd.h"
#include "BSLib/BSDebug.h"
#include "BSNet/IBSNet.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/FileTransMgr.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerProcess.h"

using namespace ServerFramework;

static void WriteByte(CMsg* pMsg, BYTE b)
{
    pMsg->WriteBytes(&b, 1);
}

static void WriteByte(CMassiveMsg* pMsg, BYTE b)
{
    pMsg->Write(&b, 1);
}

static void SendAndRelease(DWORD dwSessionID, CMsg* pMsg)
{
    g_pBSNet->SendMsg(dwSessionID, pMsg);
    g_pBSNet->ReleaseMsg(pMsg);
}

// The progress messages to the uploading SMC. The original passes the session as the encryption
// flag of the message.
static CMsg* NewProgressMsg(sServiceManagerUser* pUser, WORD wOpcode)
{
    CMsg* pMsg = g_pBSNet->AllocMsg((BOOL)pUser->dwSessionID);
    pMsg->SetOpcode(wOpcode);
    return pMsg;
}

void CServiceManagerProcess::SendPatchBusy(DWORD dwSessionID)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(0x3103);
    WriteByte(pMsg, 1);
    SendAndRelease(dwSessionID, pMsg);
}

// ---------------------------------------------------------------------------------------------
// 0x7100 (massive) {MODULE_VERSION, WORD count, count MODULE_VERSION_FILE}: the upload.

void CServiceManagerProcess::OnMsg_7100(CMsg*, DWORD, DWORD, CMassiveMsg* pMassive)
{
    DWORD dwSessionID = pMassive->m_dw36;
    if (m_bPatchUploading)
    {
        SendPatchBusy(dwSessionID);
        return;
    }
    if (!FindUser(dwSessionID))
        return;
    ModuleVersion_FreeAllData();
    pMassive->Read(&m_PatchModule, sizeof(m_PatchModule));
    WORD wCount;
    pMassive->Read(&wCount, 2);
    m_wPatchFileCount = wCount;
    ModuleVersionList* pModules = new ModuleVersionList;
    ModuleVersionFileList* pFiles = new ModuleVersionFileList;
    for (int i = wCount; i > 0; --i)
    {
        MODULE_VERSION_FILE* pFile = (MODULE_VERSION_FILE*)operator new(sizeof(*pFile));
        pMassive->Read(pFile, sizeof(*pFile));
        pFiles->push_back(pFile);
    }
    ModuleVersion_StoreAndIndexLists(pFiles, pModules);
    m_bPatchUploading = 1;
    m_pPatchUser = FindUser(pMassive->m_dw36);
    m_wPatchFilesLeft = wCount;
    m_nPatchStep = 0;
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(0xB100);
    WriteByte(pReply, 1);
    SendAndRelease(pMassive->m_dw36, pReply);
    char szSubDir[MAX_PATH];
    if (FAILED(StringCchPrintfA(szSubDir, MAX_PATH, "%d_%d", m_PatchModule.byModuleID,
                                m_PatchModule.ab0[5])))
    {
        BSAssertFail();
    }
    g_FileTransMgr.BuildRecvQueueAndStart(pMassive->m_dw36, szSubDir, pFiles);
}

// {BYTE result}: 2 cancels the upload.
void CServiceManagerProcess::OnMsg_A305(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    if (!m_bPatchUploading || !m_pPatchUser)
        BSAssertFail();
    BYTE byResult;
    *pMsg >> byResult;
    if (byResult == 2)
        CancelPatchUpload();
}

// ---------------------------------------------------------------------------------------------
// The file transfer events.

bool CServiceManagerProcess::IsPatchDelivered()
{
    for (SMPatchTargetMap::iterator it = m_mapPatchTarget.begin(); it != m_mapPatchTarget.end();
         ++it)
    {
        if (it->second.wFilesLeft > 0)
            return false;
    }
    return true;
}

bool CServiceManagerProcess::SendPatchToDownloadServers()
{
    m_nPatchStep = 1;
    ServerBodyList* pList = g_pServerBodyList;
    for (ServerBodyList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        SERVER_BODY* pBody = *it;
        if (pBody->pModule != CertModule_FindByName("DownloadServer"))
            continue;
        if (pBody->nState != 5)
            return false;
        SERVER_CORD* pCord = ServerBody_Find(pBody->wID);
        if (!pCord || !pCord->dwSessionID)
            return false;
        SM_PATCH_TARGET target;
        target.pBody = pBody;
        target.dwSessionID = pCord->dwSessionID;
        target.wFilesLeft = m_wPatchFileCount;
        target.bSent = 0;
        target.bB = 0;
        m_mapPatchTarget.insert(SMPatchTargetMap::value_type(pCord->dwSessionID, target));
    }
    ModuleVersionFileList* pFiles = g_pModuleVersionFileList;
    for (SMPatchTargetMap::iterator it = m_mapPatchTarget.begin(); it != m_mapPatchTarget.end();
         ++it)
    {
        CMassiveMsg* pMsg = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
        pMsg->SetOpcode(0x6305);
        DWORD dwCount = (WORD)pFiles->size();
        pMsg->Write(&dwCount, 2);
        for (ModuleVersionFileList::iterator itFile = pFiles->begin(); itFile != pFiles->end();
             ++itFile)
        {
            pMsg->Write(*itFile, sizeof(MODULE_VERSION_FILE));
        }
        pMsg->SendTo(it->second.dwSessionID, FALSE);
        it->second.bSent = 1;
    }
    return true;
}

void CServiceManagerProcess::OnFileDone(DWORD dwSessionID)
{
    if (!m_pPatchUser)
        return;
    if (m_nPatchStep == 0)
    {
        // a file of the SMC was received
        if (m_wPatchFilesLeft == 0)
            BSAssertFail();
        if (--m_wPatchFilesLeft)
            return;
        CMsg* pMsg = NewProgressMsg(m_pPatchUser, 0x3101);
        WriteByte(pMsg, 1);
        WriteByte(pMsg, 1);
        SendAndRelease(m_pPatchUser->dwSessionID, pMsg);
        if (SendPatchToDownloadServers())
            return;
        pMsg = NewProgressMsg(m_pPatchUser, 0x3102);
        WriteByte(pMsg, 2);
        WriteByte(pMsg, 1);
        SendAndRelease(m_pPatchUser->dwSessionID, pMsg);
        CancelPatchUpload();
        return;
    }
    if (m_nPatchStep != 1)
        return;
    // a DownloadServer received a file
    SMPatchTargetMap::iterator it = m_mapPatchTarget.find(dwSessionID);
    if (it == m_mapPatchTarget.end())
        BSAssertFail();
    SM_PATCH_TARGET& target = it->second;
    if (target.wFilesLeft == 0)
        BSAssertFail();
    --target.wFilesLeft;
    if (!IsPatchDelivered())
    {
        CMsg* pMsg = NewProgressMsg(m_pPatchUser, 0x3101);
        WriteByte(pMsg, 1);
        WriteByte(pMsg, 2);
        pMsg->WriteBytes(&target.pBody->wID, 2);
        pMsg->WriteBytes(&target.wFilesLeft, 2);
        SendAndRelease(m_pPatchUser->dwSessionID, pMsg);
        return;
    }
    CMsg* pMsg = NewProgressMsg(m_pPatchUser, 0x3101);
    WriteByte(pMsg, 2);
    WriteByte(pMsg, 1);
    SendAndRelease(m_pPatchUser->dwSessionID, pMsg);
    SM_JOB* pJob = OverlapJob_AllocRecordPatchData();
    OverlapJob_SetupRecordPatchData(m_pTask, pJob, &m_PatchModule, g_pModuleVersionFileList,
                                    m_pPatchUser->dwSessionID);
    CServerProcessBase* pProcess = (CServerProcessBase*)m_pTask->GetJob();
    pProcess->SendOverlapJob(pJob, NULL);
    for (it = m_mapPatchTarget.begin(); it != m_mapPatchTarget.end(); ++it)
    {
        pJob = OverlapJob_AllocModuleVersionData();
        OverlapJob_SetupModuleVersionData(m_pTask, pJob, it->second.dwSessionID);
        pProcess->SendOverlapJob(pJob, NULL);
    }
    m_bPatchUploading = 0;
    m_nPatchStep = -1;
    m_pPatchUser = NULL;
    m_mapPatchTarget.clear();
}

void CServiceManagerProcess::OnFileFailed()
{
    if (!m_pPatchUser)
        return;
    CMsg* pMsg = NewProgressMsg(m_pPatchUser, 0x3102);
    WriteByte(pMsg, 2);
    WriteByte(pMsg, 2);
    SendAndRelease(m_pPatchUser->dwSessionID, pMsg);
    CancelPatchUpload();
}

void CServiceManagerProcess::OnCordClosed()
{
    if (!m_bPatchUploading || !m_pPatchUser || m_nPatchStep != 1)
        return;
    CMsg* pMsg = NewProgressMsg(m_pPatchUser, 0x3102);
    WriteByte(pMsg, 2);
    WriteByte(pMsg, 2);
    SendAndRelease(m_pPatchUser->dwSessionID, pMsg);
    CancelPatchUpload();
}

// ---------------------------------------------------------------------------------------------
// 0x7104 {BYTE module, BYTE}: prepare a patch; 0xA308 {BYTE result}: the answers of 0x6308.

static void SendPrepareResult(DWORD dwSessionID, BYTE byReason)
{
    CMassiveMsg* pMsg = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pMsg->SetOpcode(0xB104);
    WriteByte(pMsg, 2);
    WriteByte(pMsg, byReason);
    pMsg->SendTo(dwSessionID, FALSE);
}

void CServiceManagerProcess::OnMsg_7104(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    *pMsg >> m_byPrepareModule;
    *pMsg >> m_byPrepare181;
    if (m_bPreparingPatch || m_bPatchUploading)
    {
        LogPrintf(0x2000000, "another patch process is in progress...try later...");
        SendPrepareResult(pMsg->m_dwSessionID, 1);
        return;
    }
    m_bPreparingPatch = 1;
    m_setPrepareBody.clear();
    m_dwPrepareSession = pMsg->m_dwSessionID;
    ServerBodyList* pList = g_pServerBodyList;
    for (ServerBodyList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        SERVER_BODY* pBody = *it;
        if (pBody->pModule != CertModule_FindByName("DownloadServer")
            && pBody->pModule != CertModule_FindByName("GatewayServer"))
        {
            continue;
        }
        if (pBody->pModule == CertModule_FindByName("GatewayServer") && pBody->nState != 5
            && pBody->nState != 4)
        {
            continue;
        }
        m_setPrepareBody.insert(pBody->wID);
        CMsg* pRelay = g_pBSNet->AllocMsg(FALSE);
        pRelay->SetOpcode(0x6308);
        if (!RelayMsgToBody_6008((CServerProcessBase*)m_pTask->GetJob(), pRelay, pBody->wID, 0,
                                 0, pBody->wID))
        {
            m_bPreparingPatch = 0;
            m_setPrepareBody.clear();
            LogPrintf(0x2000000, "can't relay 'prepare_patch_req' to associated front "
                                 "servers!!! check if online or not");
            SendPrepareResult(pMsg->m_dwSessionID, 2);
            return;
        }
    }
}

// sub_469890: the other end of a cord of this server.
static WORD PeerBodyID(SERVER_CORD* pCord)
{
    if (pCord->wFromID != g_pSelfBody->wID && pCord->wToID != g_pSelfBody->wID)
        BSAssertFail();
    if (pCord->wFromID == g_pSelfBody->wID)
        return pCord->wToID;
    return pCord->wFromID;
}

void CServiceManagerProcess::OnMsg_A308(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE byResult;
    *pMsg >> byResult;
    SERVER_CORD* pCord = ServerCordMap_FindBySession(pMsg->m_dwSessionID);
    if (!pCord)
    {
        LogPrintf(0x2000000,
                  "'prepare_patch_ack' received but can't find server cord !!! what the hell -_-;");
        return;
    }
    if (!m_bPreparingPatch)
    {
        LogPrintf(0x2000000,
                  "invalid 'prepare_patch_ack', currently patch process is not working...");
        return;
    }
    if (byResult == 2)
    {
        m_bPreparingPatch = 0;
        m_setPrepareBody.clear();
        LogPrintf(0x2000000, "an associated front server has returned 'prepare_patch_ack' with "
                             "fail... check it out!");
        SendPrepareResult(m_dwPrepareSession, 2);
        return;
    }
    std::set<WORD>::iterator it = m_setPrepareBody.find(PeerBodyID(pCord));
    if (it == m_setPrepareBody.end())
    {
        LogPrintf(0x2000000, "'prepare_patch_req' faild! check configuration setting of Server "
                             "Cord from _Certification DB ... (contact to Joymax server team)");
        return;
    }
    m_setPrepareBody.erase(it);
    if (!m_setPrepareBody.empty())
        return;
    m_bPreparingPatch = 0;
    SM_JOB* pJob = OverlapJob_AllocPreparePatch();
    OverlapJob_SetupPreparePatch(m_pTask, pJob, m_dwPrepareSession, m_byPrepareModule,
                                 m_byPrepare181);
    PostJob(pJob);
}
