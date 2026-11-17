#include "DownloadServerMainProcess.h"

#include "DownloadServer.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/FileTransMgr.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerFramework/ServerDataExtern.h"

#include <strsafe.h>

using namespace ServerFramework;

BS_IMPLEMENT_DYNCREATE(CDownloadServerMainProcess, &CServerProcessMain::classCServerProcessMain)

CDownloadServerMainProcess::CDownloadServerMainProcess()
{
}

CDownloadServerMainProcess::~CDownloadServerMainProcess()
{
}

void CDownloadServerMainProcess::InitMsgHandlers()
{
    CServerProcessMain::InitMsgHandlers();
    RegisterHandler(DOWNLOAD_MSG_MODULE_VERSION_ACK, &CDownloadServerMainProcess::OnMsg_A306);
    RegisterHandler(DOWNLOAD_MSG_PATCH_RELAY_REQ, &CDownloadServerMainProcess::OnMsg_6305);
    RegisterHandler(DOWNLOAD_MSG_PATCH_DONE_REQ, &CDownloadServerMainProcess::OnMsg_6308);
}

// 0x1001 {DWORD session, DWORD role}: the data is peeked (the read position is saved in
// CMsg +4160 and restored) and left to the base handler.
void CDownloadServerMainProcess::OnSessionCreated(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                                  CMassiveMsg* pMassive)
{
    if (pMsg->m_dw4160)
        BSAssertFail();
    pMsg->m_dw4160 = pMsg->m_wReadPos;
    DWORD dwSessionID;
    DWORD dwRole;
    *pMsg >> dwSessionID >> dwRole;
    pMsg->m_wReadPos = (WORD)pMsg->m_dw4160;
    pMsg->m_dw4160 = 0;
    if (g_dwDownloadUserLimit == (DWORD)-1)
    {
        CServerProcessMain::OnSessionCreated(pMsg, dwRelayID, dwContext, pMassive);
        return;
    }
    // BUG(DL-002): the limit counts every session of the engine (server cords included); a
    // refused client's 0x1001 stays unread.
    NETENGINE_STATISTICS stats;
    g_pBSNet->GetStatistics(BSNET_STAT_SESSIONS, &stats);
    if (dwRole || stats.dwSessionUsed <= g_dwDownloadUserLimit)
    {
        CServerProcessMain::OnSessionCreated(pMsg, dwRelayID, dwContext, pMassive);
        return;
    }
    g_pBSNet->DisconnectSession(dwSessionID, 1);
}

void CDownloadServerMainProcess::OnCordEstablished(SERVER_CORD* pCord, SERVER_BODY* pPeer)
{
    if (pPeer->pModule != CertModule_FindByName("GlobalManager"))
        return;
    ModuleVersion_FreeAllData();
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(DOWNLOAD_MSG_PATCH_RESET);
    g_pBSNet->SendMsg(pCord->dwSessionID, pMsg);
    g_pBSNet->ReleaseMsg(pMsg);
}

void CDownloadServerMainProcess::OnFileFailed(DWORD)
{
}

// 0xA306 (massive): BYTE result (2: failed), then the module versions and the patch files of the
// DivisionManager, each as (record, BYTE more)... after a first BYTE more. The same code as
// CGatewayMainProcess::OnMsg_A306.
void CDownloadServerMainProcess::OnMsg_A306(CMsg*, DWORD, DWORD, CMassiveMsg* pMassive)
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

// 0x6305 (massive) {WORD n, n * MODULE_VERSION_FILE}: the patch files replace the module
// version data, 0xA305 {1} answers, then the files are fetched from the sending session into
// the directory "<module ID>_<byte 9>" of the last record.
// BUG(DL-001): without records the directory name comes from uninitialised bytes (the fetch
// then asserts and does not start).
void CDownloadServerMainProcess::OnMsg_6305(CMsg*, DWORD, DWORD, CMassiveMsg* pMassive)
{
    ModuleVersion_FreeAllData();
    ModuleVersionList* pModules = new ModuleVersionList;
    ModuleVersionFileList* pFiles = new ModuleVersionFileList;
    WORD wCount;
    pMassive->Read(&wCount, 2);
    BYTE byModuleID;
    BYTE by9;
    for (int n = wCount; n > 0; --n)
    {
        MODULE_VERSION_FILE* pFile =
            (MODULE_VERSION_FILE*)operator new(sizeof(MODULE_VERSION_FILE));
        pMassive->Read(pFile, sizeof(MODULE_VERSION_FILE));
        byModuleID = pFile->byModuleID;
        by9 = pFile->ab8[1];
        pFiles->push_back(pFile);
    }
    ModuleVersion_StoreAndIndexLists(pFiles, pModules);

    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(DOWNLOAD_MSG_PATCH_RELAY_ACK);
    BYTE byResult = 1;
    pReply->WriteBytes(&byResult, 1);
    g_pBSNet->SendMsg(pMassive->m_dw36, pReply);
    g_pBSNet->ReleaseMsg(pReply);

    char szSubDir[MAX_PATH];
    if (FAILED(StringCchPrintfA(szSubDir, MAX_PATH, "%d_%d", byModuleID, by9)))
        BSAssertFail();
    g_FileTransMgr.BuildRecvQueueAndStart(pMassive->m_dw36, szSubDir, pFiles);
    m_wPatchFileCount = wCount;
    LogPrintf(0, "start patch relay : %d", (DWORD)wCount);
}

// Relayed: closes the client sessions and the open files, answers 0xA309 {1}.
void CDownloadServerMainProcess::OnMsg_6308(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    g_pBSNet->CleanupDisconnectedSessions();
    g_pBSNet->CloseAllFiles();
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(DOWNLOAD_MSG_PATCH_DONE_ACK);
    BYTE byResult = 1;
    pReply->WriteBytes(&byResult, 1);
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
    LogPrintf(0, "preparing patch is completed !!!");
}
