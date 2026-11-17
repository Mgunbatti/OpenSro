#include "ServerProcessMain.h"

#include <stdlib.h>

#include "BSNet/security/Security.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/FileTransMgr.h"
#include "ServerFramework/ServerApp.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"

#include <strsafe.h>

namespace ServerFramework
{

BS_IMPLEMENT_DYNCREATE(CServerProcessMain, &CServerProcessBase::classCServerProcessBase)

const char* const g_apszNetEngineError[22] =
{
    "GOOD", "TIMEOUT", "UNKNOWN", "SESSION_LOST", "BUFFER_FULL", "TOO_MANY_OUTSTANDING",
    "NODATA_TO_SEND", "NO_MSGTARGET", "INVALID_SOCKET", "INVALID_SESSION",
    "FAILED_TO_TRANSFILE", "INVALID_SEQUENCE", "INVALID_CHECKSUM", "INVALID_MSGSIZE",
    "FAILED_TO_ACCEPT", "INVALID_TARGET_FILE", "NET_ACCESSED_BEFORE_HANDSHAKE",
    "INVALID_MSG_HEADER", "COMPLETION_TARGET_MISMATCH", "TARGET_TASK_LOST",
    "FAILED_TO_CREATE_SESSION", "NERR_PEER_DOESNT_RESPONSE",
};

static SERVER_CORD* FindSessionCord(DWORD dwSessionID)
{
    std::map<DWORD, SERVER_CORD*>::iterator it = g_mapSessionCord.find(dwSessionID);
    if (it == g_mapSessionCord.end())
        return NULL;
    return it->second;
}

static CERT_SHARD* FindCertShard(WORD wShardID)
{
    std::map<WORD, CERT_SHARD*>::iterator it = g_mapCertShard.find(wShardID);
    if (it == g_mapCertShard.end())
        return NULL;
    return it->second;
}

static MODULE_VERSION_FILE* FindModuleVersionFile(DWORD dwFileID)
{
    stdext::hash_map<DWORD, MODULE_VERSION_FILE*>::iterator it =
        g_hmapModuleVersionFile.find(dwFileID);
    if (it == g_hmapModuleVersionFile.end())
        return NULL;
    return it->second;
}

static void SendByte(DWORD dwSessionID, WORD wOpcode, BYTE b)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(wOpcode);
    pMsg->WriteBytes(&b, 1);
    g_pBSNet->SendMsg(dwSessionID, pMsg);
    g_pBSNet->ReleaseMsg(pMsg);
}

// ---------------------------------------------------------------------------------------------

CServerProcessMain::CServerProcessMain()
{
    m_bUseTimer = TRUE;
    m_bUseOverlapJob = TRUE;
    CClassTrigger<CServerProcessMain>* pTrigger = new CClassTrigger<CServerProcessMain>(this);
    if (pTrigger)
        pTrigger->m_pfnTrigger = &CServerProcessMain::ForwardBodyStateChanged;
    m_pTrigger = pTrigger;
    g_pServerBodyStateObserver = pTrigger;
}

// BUG(FWAPP-009): g_pServerBodyStateObserver is left pointing to the deleted trigger.
CServerProcessMain::~CServerProcessMain()
{
    if (m_pTrigger)
        delete m_pTrigger;
}

void CServerProcessMain::InitMsgHandlers()
{
    CServerProcessBase::InitMsgHandlers();
    RegisterHandler(SESSION_EVENT_CONNECTED, &CServerProcessMain::OnSessionCreated);
    RegisterHandler(SESSION_EVENT_HANDSHAKED, &CServerProcessMain::OnSessionHandshaked);
    RegisterHandler(SESSION_EVENT_CLOSED, &CServerProcessMain::OnSessionLost);
    RegisterHandler(SESSION_EVENT_FILE_PROGRESS, &CServerProcessMain::OnMsg_1004);
    RegisterHandler(SESSION_EVENT_FILE_DONE, &CServerProcessMain::OnMsg_1005);
    RegisterHandler(SESSION_EVENT_FILE_FAILED, &CServerProcessMain::OnMsg_1006);
    RegisterHandler(0x2001, &CServerProcessMain::OnMsg_2001);
    RegisterHandler(0x2002, &CServerProcessMain::OnMsg_2002);
    RegisterHandler(0x6003, &CServerProcessMain::OnMsg_6003);
    RegisterHandler(0xA003, &CServerProcessMain::OnMsg_A003);
    RegisterHandler(FILETRANS_OPCODE_REQ, &CServerProcessMain::OnMsg_6004);
    RegisterHandler(FILETRANS_OPCODE_ACK, &CServerProcessMain::OnMsg_A004);
    RegisterHandler(SERVER_STATUS_OPCODE, &CServerProcessMain::OnMsg_2005);
    RegisterHandler(0x6005, &CServerProcessMain::OnMsg_6005);
    RegisterHandler(0x6006, &CServerProcessMain::OnMsg_6006);
    RegisterHandler(0xA006, &CServerProcessMain::OnMsg_A006);
    RegisterHandler(0x6300, &CServerProcessMain::OnMsg_6300);
    RegisterHandler(0x6303, &CServerProcessMain::OnMsg_6303);
    RegisterHandler(MASSIVE_MSG_OPCODE, &CServerProcessMain::OnMsg_600D);
    RegisterHandler(0x2311, &CServerProcessMain::OnMsg_2311);
#if SERVER_MAIN_PROCESS_631D
    RegisterHandler(0x631D, &CServerProcessMain::OnMsg_631D);
#endif
}

void CServerProcessMain::OnMsg_2001(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strModule;
    BYTE bNotCertified;
    *pMsg >> strModule;
    *pMsg >> bNotCertified;
    DWORD dwSessionID = pMsg->m_dwSessionID;

    if (!g_pSelfBody)
    {
        // Not certified yet: the peer is the certification server.
        g_pBSNet->ExcludeSessionFromCleanup(dwSessionID);
        CMsg* pReq = g_pBSNet->AllocMsg(FALSE);
        pReq->SetOpcode(0x6003);
        *pReq << g_ServerGlobals.szServiceName;
        *pReq << g_ServerGlobals.szPublicIP;
        g_pBSNet->SendMsg(dwSessionID, pReq);
        g_pBSNet->ReleaseMsg(pReq);
        return;
    }

    SERVER_CORD* pCord = ServerCord_OnEstablished(dwSessionID, strModule.c_str());
    if (!g_pBSNet->GetSessionSocketState(dwSessionID))
    {
        // Accepted session: answer the introduction.
        if (bNotCertified)
            g_pBSNet->ExcludeSessionFromCleanup(dwSessionID);
        CMsg* pAck = g_pBSNet->AllocMsg(FALSE);
        pAck->SetOpcode(0x2001);
        BYTE bZero = 0;
        *pAck << g_ServerGlobals.szServiceName;
        pAck->WriteBytes(&bZero, 1);
        g_pBSNet->SendMsg(dwSessionID, pAck);
        g_pBSNet->ReleaseMsg(pAck);
    }

    if (pCord)
    {
        ServerBody_UpdateStatusAndNotify(pCord->dwID, 2, 0);
        OnCordEstablished(pCord, ServerBody_FindSelf(pCord));
    }
    else
    {
        if (!CertModule_FindByName(strModule.c_str()))
        {
            g_pBSNet->DisconnectSession(dwSessionID, 1);
            LogPrintf(0x1000000, "disconnected unknown module : %s", strModule.c_str());
            return;
        }
        if (g_ServerGlobals.bReportConnectionLog)
            LogPrintf(0, "setup cord [%d]: %s", dwSessionID, strModule.c_str());
    }

    if (bNotCertified)
        return;

    // This server's state, then the query for the peer's state and its cords.
    BYTE b;
    CMassiveMsg* pStatus = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pStatus->SetOpcode(SERVER_STATUS_OPCODE);
    b = 1;
    pStatus->Write(&b, 1);
    b = 0;
    pStatus->Write(&b, 1);
    b = 1;
    pStatus->Write(&b, 1);
    pStatus->Write(&g_pSelfBody->wID, 2);
    pStatus->Write(&g_pSelfBody->nState, 4);
    b = 2;
    pStatus->Write(&b, 1);
    pStatus->SendTo(dwSessionID, FALSE);

    CMassiveMsg* pQuery = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pQuery->SetOpcode(0x6005);
    b = 3;
    pQuery->Write(&b, 1);
    b = 0;
    pQuery->Write(&b, 1);
#ifdef JMX_FRAMEWORK_2007
    // FarmManager 0x4029c0: every other body whose route is this cord, then the cords of those
    // bodies.
    // BUG(FM-002): without a cord (a known module that has no cord to this server) the bodies
    // without a route match, so the query lists every unreachable body.
    for (ServerBodyList::iterator it = g_pServerBodyList->begin();
         it != g_pServerBodyList->end(); ++it)
    {
        SERVER_BODY* pBody = *it;
        if (pBody == g_pSelfBody || ServerBody_Find(pBody->wID) != pCord)
            continue;
        b = 1;
        pQuery->Write(&b, 1);
        pQuery->Write(pBody, 2);
        if (!pBody->wID)
            BSAssertFail();
    }
    b = 2;
    pQuery->Write(&b, 1);
    b = 0;
    pQuery->Write(&b, 1);
    for (ServerBodyList::iterator it = g_pServerBodyList->begin();
         it != g_pServerBodyList->end(); ++it)
    {
        SERVER_BODY* pBody = *it;
        if (pBody == g_pSelfBody || ServerBody_Find(pBody->wID) != pCord)
            continue;
        ServerCordList lstCords;
        ServerCord_CollectForBody(pBody->wID, &lstCords);
        for (ServerCordList::iterator itCord = lstCords.begin(); itCord != lstCords.end();
             ++itCord)
        {
            b = 1;
            pQuery->Write(&b, 1);
            pQuery->Write(*itCord, 4);
        }
    }
#else
    DWORD dwPeer = 0;
    if (pCord)
    {
        if (pCord->wFromID == g_pSelfBody->wID)
            dwPeer = pCord->wToID;
        else
            dwPeer = pCord->wFromID;
        b = 1;
        pQuery->Write(&b, 1);
        pQuery->Write(&dwPeer, 2);
    }
    b = 2;
    pQuery->Write(&b, 1);
    b = 0;
    pQuery->Write(&b, 1);
    if (pCord)
    {
        ServerCordList lstCords;
        ServerCord_CollectForBody((WORD)dwPeer, &lstCords);
        for (ServerCordList::iterator it = lstCords.begin(); it != lstCords.end(); ++it)
        {
            SERVER_CORD* pPeerCord = *it;
            b = 1;
            pQuery->Write(&b, 1);
            pQuery->Write(pPeerCord, 4);
        }
    }
#endif
    b = 2;
    pQuery->Write(&b, 1);
    pQuery->SendTo(dwSessionID, FALSE);
}

void CServerProcessMain::OnMsg_2002(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    pMsg->SkipRemaining();
}

void CServerProcessMain::OnMsg_6003(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    CMassiveMsg* pReply = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pReply->SetOpcode(0xA003);
    CertArch_OnCertificationRequest(pMsg, pReply);
    pReply->SendTo(pMsg->m_dwSessionID, FALSE);
}

void CServerProcessMain::OnMsg_A003(CMsg*, DWORD, DWORD, CMassiveMsg* pMassive)
{
    BYTE bResult;
    pMassive->Read(&bResult, 1);
    if (bResult == 2)
    {
        ServerApp_PostQuit();
        return;
    }

    CServerProcessMain_ParseA003CertArchAndBuildTargets(pMassive);
    LogPrintf(0, "successfully server certificated");
    g_pServerApp->SetServerState(g_pServerApp->m_nPendingState);

    // The certification session becomes the cord to the parent body.
    SERVER_BODY* pParent = ServerBodyInfo_Find(g_pSelfBody->wParentID);
    if (pParent)
    {
        SERVER_CORD* pCord = ServerCord_OnEstablished(pMassive->m_dw36, pParent->pModule->szName);
        if (pCord)
        {
            if (!g_ServerGlobals.dw2B4)
            {
                g_pBSNet->AddKeepAliveTarget(g_ServerGlobals.szCertificationIP,
                                             (u_short)g_ServerGlobals.nCertificationPort, NULL,
                                             SERVER_TASK_MAIN, pMassive->m_dw36);
            }
            ServerBody_UpdateStatusAndNotify(pCord->dwID, 2, 0);
            OnCordEstablished(pCord, ServerBody_FindSelf(pCord));
        }
        else
        {
            g_pBSNet->DisconnectSession(pMassive->m_dw36, 1);
            LogPrintf(0x1000000, "ServerCord no found. Session Closed!");
        }
    }

    if (!g_pServerApp->CreateListener())
    {
        ServerApp_PostQuit();
        return;
    }
    ServerCord_ConnectOutgoingCords();

    if (g_pSelfBody->wID == g_pSelfBody->pDivision->wGlobalManagerID)
    {
        CMsg* pInit = g_pBSNet->AllocMsg(FALSE);
        pInit->SetOpcode(SERVER_CONTROL_INIT_LOCAL_DATA);
        g_pServerApp->OnControlMsg(pInit);
        return;
    }
    COverlapJob_InitializeLocalData* pJob = OverlapJob_AllocInitializeLocalData();
    pJob->Init((CTask*)m_pOwner);
    SendOverlapJob(pJob, NULL);
}

void CServerProcessMain::OnMsg_6004(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwFileID;
    DWORD dwOffset;
    *pMsg >> dwFileID >> dwOffset;
    MODULE_VERSION_FILE* pFile = FindModuleVersionFile(dwFileID);
    if (!pFile)
    {
        LogPrintf(0x2000000, "client requested to transfer abnormal file : %d", dwFileID);
        SendByte(pMsg->m_dwSessionID, FILETRANS_OPCODE_ACK, 0);
        return;
    }
    char szSubDir[MAX_PATH];
    if (StringCchPrintfA(szSubDir, MAX_PATH, "%d_%d", pFile->byModuleID, pFile->ab8[1]))
        BSAssertFail();
    if (!ServerProcess_SendFileTo(szSubDir, pFile->szName, pMsg->m_dwSessionID, pFile->szPath,
                                  dwOffset))
    {
        SendByte(pMsg->m_dwSessionID, FILETRANS_OPCODE_ACK, 0);
    }
}

void CServerProcessMain::OnMsg_A004(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE bResult;
    *pMsg >> bResult;
    if (bResult == 1)
    {
        // ServerProcessMain_ForwardA004IfReady (0x45a9d0)
        if (g_FileTransMgr.m_RecvQueue.size())
            g_FileTransMgr.SendFrontQueuedFile();
        return;
    }
    LogPrintf(0x2000000, "file transfer failed : transfer server cannot send file");
}

void CServerProcessMain::OnMsg_2005(CMsg*, DWORD, DWORD, CMassiveMsg* pMassive)
{
    if (!g_pSelfBody)
        return;
    FindSessionCord(pMassive->m_dw36);

    // The flag byte lands in the high byte of the word.
    WORD wFlags;
    BYTE bMark;
    pMassive->Read((BYTE*)&wFlags + 1, 1);
    if (wFlags & 0x100)
    {
        pMassive->Read(&bMark, 1);
        pMassive->Read(&bMark, 1);
        while (bMark != 2)
        {
            WORD wBodyID;
            int nState;
            pMassive->Read(&wBodyID, 2);
            pMassive->Read(&nState, 4);
            pMassive->Read(&bMark, 1);
            if (wBodyID != g_pSelfBody->wID)
                ServerBody_SetStateAndBroadcast2005(nState, wBodyID, pMassive->m_dw36);
        }
    }
    if (wFlags & 0x200)
    {
        pMassive->Read(&bMark, 1);
        pMassive->Read(&bMark, 1);
        while (bMark != 2)
        {
            DWORD dwCordID;
            DWORD dwStatus;
            pMassive->Read(&dwCordID, 4);
            pMassive->Read(&dwStatus, 4);
            pMassive->Read(&bMark, 1);
            SERVER_CORD* pCord = ServerCordMap_FindById(dwCordID);
            if (pCord && pCord->wToID != g_pSelfBody->wID && pCord->wFromID != g_pSelfBody->wID)
                ServerBody_UpdateStatusAndNotify(dwCordID, dwStatus, pMassive->m_dw36);
        }
    }
}

void CServerProcessMain::OnMsg_6005(CMsg*, DWORD, DWORD, CMassiveMsg* pMassive)
{
    CMassiveMsg* pReply = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pReply->SetOpcode(SERVER_STATUS_OPCODE);
    BYTE bFlags;
    BYTE bMark;
    BYTE b;
    pMassive->Read(&bFlags, 1);
    pReply->Write(&bFlags, 1);
    if (bFlags & 1)
    {
        pMassive->Read(&bMark, 1);
        pMassive->Read(&bMark, 1);
        b = 0;
        pReply->Write(&b, 1);
        while (bMark != 2)
        {
            DWORD dwBodyID;
            pMassive->Read(&dwBodyID, 2);
            pMassive->Read(&bMark, 1);
            SERVER_BODY* pBody = ServerBodyInfo_Find((WORD)dwBodyID);
            b = 1;
            if (pBody)
            {
                pReply->Write(&b, 1);
                pReply->Write(&dwBodyID, 2);
                pReply->Write(&pBody->nState, 4);
            }
            else
            {
                if (!(WORD)dwBodyID)
                    BSAssertFail();
                int nState = 0;
                pReply->Write(&b, 1);
                pReply->Write(&dwBodyID, 2);
                pReply->Write(&nState, 4);
            }
        }
        b = 2;
        pReply->Write(&b, 1);
    }
    if (bFlags & 2)
    {
        pMassive->Read(&bMark, 1);
        pMassive->Read(&bMark, 1);
        b = 0;
        pReply->Write(&b, 1);
        while (bMark != 2)
        {
            DWORD dwCordID;
            pMassive->Read(&dwCordID, 4);
            pMassive->Read(&bMark, 1);
            SERVER_CORD* pCord = ServerCordMap_FindById(dwCordID);
            b = 1;
            if (pCord)
            {
                pReply->Write(&b, 1);
                pReply->Write(&dwCordID, 4);
                pReply->Write(&pCord->dwStatus, 4);
            }
            else
            {
                if (!dwCordID)
                    BSAssertFail();
                DWORD dwStatus = 0;
                pReply->Write(&b, 1);
                pReply->Write(&dwCordID, 4);
                pReply->Write(&dwStatus, 4);
            }
        }
        b = 2;
        pReply->Write(&b, 1);
    }
    pReply->SendTo(pMassive->m_dw36, FALSE);
}

void CServerProcessMain::OnMsg_6006(CMsg*, DWORD, DWORD, CMassiveMsg*)
{
}

void CServerProcessMain::OnMsg_A006(CMsg*, DWORD, DWORD, CMassiveMsg*)
{
}

void CServerProcessMain::OnSessionCreated(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwSessionID;
    DWORD dwRole;
    *pMsg >> dwSessionID >> dwRole;
    if (dwRole)
        return;

    if (!g_pSelfBody)
    {
        LogPrintf(0x1000000, "g_pServerBodyOfMyself IS NULL. Session Closed!");
        g_pBSNet->DisconnectSession(dwSessionID, 1);
        return;
    }

    SECURITY_HANDSHAKE_PARAM param;
    memset(&param.dwCountSeed, 0, 16);
    param.bOptions = 6;
    param.dwCountSeed = rand() % 255 + 1;
    param.dwCrcSeed = rand() % 255 + 1;
    for (int i = 0; i < 8; ++i)
        param.BlowFishKey[i] = (BYTE)(rand() % 255 + 1);
    g_pBSNet->StartHandshake(dwSessionID, &param);
    if (g_ServerGlobals.bReportConnectionLog)
        LogPrintf(0, "session created [%d] and set session encoding context", dwSessionID);
}

// BUG(FWAPP-008): the error name is read with HIWORD(reason) & 0x7FFF from a 22-entry table.
void CServerProcessMain::OnSessionLost(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwSessionID;
    DWORD dwReason;
    *pMsg >> dwSessionID >> dwReason;
    if (g_ServerGlobals.bReportConnectionLog)
    {
#ifdef JMX_FRAMEWORK_2011
        // AgentServer 0x42c690: every line also goes to the fatal log.
        switch ((WORD)dwReason)
        {
        case 0:
            LogPrintf(0x2000001, "Session Lost [%d]: NETENGINE_ERROR-%s", dwSessionID,
                      g_apszNetEngineError[HIWORD(dwReason) & 0x7FFF]);
            break;
        case 1:
            LogPrintf(0x2000001, "Session Lost [%d]: INTENTIONAL_KILL", dwSessionID);
            break;
        case 2:
            LogPrintf(0x2000001, "Session Lost [%d]: STALE_SESSION", dwSessionID);
            break;
        case 3:
            LogPrintf(0x2000001, "Session Lost [%d]: CLOSED_BY_PEER", dwSessionID);
            break;
        case 4:
            LogPrintf(0x2000001, "Session Lost [%d]: ENGINE_SHUTDOWN", dwSessionID);
            break;
        }
#else
        switch ((WORD)dwReason)
        {
        case 0:
            LogPrintf(0x1000000, "Session Lost [%d]: NETENGINE_ERROR-%s", dwSessionID,
                      g_apszNetEngineError[HIWORD(dwReason) & 0x7FFF]);
            break;
        case 1:
            LogPrintf(0, "Session Lost [%d]: INTENTIONAL_KILL", dwSessionID);
            break;
        case 2:
            LogPrintf(0x1000000, "Session Lost [%d]: STALE_SESSION", dwSessionID);
            break;
        case 3:
            LogPrintf(0x1000000, "Session Lost [%d]: CLOSED_BY_PEER", dwSessionID);
            break;
        case 4:
            LogPrintf(0x1000000, "Session Lost [%d]: ENGINE_SHUTDOWN", dwSessionID);
            break;
        }
#endif
    }

    SERVER_CORD* pCord = FindSessionCord(dwSessionID);
    if (!pCord)
    {
        OnSessionClosed(dwSessionID);
        return;
    }

    // BUG(FWAPP-005): the job map is walked without the job manager's lock.
    OverlapJobMap& mapJob = m_OverlapJobManager.m_mapJob;
    for (OverlapJobMap::iterator it = mapJob.begin(); it != mapJob.end();)
    {
        COverlapJob* pJob = it->second;
        if (!pJob->IsRelayJob())
        {
            ++it;
            continue;
        }
        COverlapJob_RelayMsg* pRelay = static_cast<COverlapJob_RelayMsg*>(pJob);
        if (ServerBody_Find(pRelay->m_wTargetBody) != pCord)
        {
            ++it;
            continue;
        }
        OverlapJobMap::iterator itErase = it++;
        WORD wSource = pRelay->m_wSourceBody;
        if (wSource == g_pSelfBody->wID || !wSource)
        {
            OnRelayFailed(pRelay->m_wOpcode, pRelay->m_dwContext);
        }
        else
        {
            CMsg* pFail = g_pBSNet->AllocMsg(FALSE);
            pFail->SetOpcode(SERVER_MSG_RELAY_ACK);
            DWORD dwSourceJobID = pRelay->m_dwSourceJobID;
            DWORD dwOpcode = pRelay->m_wOpcode;
            BYTE bResult = SERVER_RELAY_RESULT_FAILED;
            pFail->WriteBytes(&bResult, 1);
            pFail->WriteBytes(&dwSourceJobID, 4);
            pFail->WriteBytes(&dwOpcode, 2);
            if (!ServerBody_SendMsg(pRelay->m_wSourceBody, pFail))
                g_pBSNet->ReleaseMsg(pFail);
        }
        mapJob.erase(itErase);
        pJob->ReturnToPool();
    }

    for (MassiveMsgSessionMap::iterator it = m_mapMassive.begin(); it != m_mapMassive.end();)
    {
        CMassiveMsg* pMassive = it->second;
        if (dwSessionID == it->first)
        {
            m_mapMassive.erase(it++);
            pMassive->Free();
        }
        else
        {
            ++it;
        }
    }

    OnCordClosed(pCord, ServerBody_FindSelf(pCord));
    ServerCord_OnClosed(dwSessionID);
}

static const char s_szHandshakeLost[] =
    "HandShaking \xc1\xf7\xc8\xc4\xbf\xa1 Session\xc0\xcc \xb2\xf7\xb1\xe4\xb5\xed "
    "\xc7\xcf\xb4\xd9![SessionID: %d]";

void CServerProcessMain::OnSessionHandshaked(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwSessionID;
    DWORD dwParam;
    *pMsg >> dwSessionID >> dwParam;
    if (g_ServerGlobals.bReportConnectionLog)
        LogPrintf(0, "session handshaked [%d]", dwSessionID);

    int nState = g_pBSNet->GetSessionSocketState(dwSessionID);
    if (nState == -1)
    {
        if (g_ServerGlobals.bReportConnectionLog)
            LogPrintf(0x1000000, s_szHandshakeLost, dwSessionID);
    }
    else if (nState == 1)
    {
        BYTE bNotCertified = g_pSelfBody == NULL;
        CMsg* pHello = g_pBSNet->AllocMsg(FALSE);
        pHello->SetOpcode(0x2001);
        *pHello << g_ServerGlobals.szServiceName;
        pHello->WriteBytes(&bNotCertified, 1);
        g_pBSNet->SendMsg(dwSessionID, pHello);
        g_pBSNet->ReleaseMsg(pHello);
    }
}

void CServerProcessMain::OnMsg_1004(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwSessionID;
    DWORD dwBytes;
    *pMsg >> dwSessionID >> dwBytes;
    OnFileProgress(dwSessionID, dwBytes);
}

void CServerProcessMain::OnMsg_1005(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwSessionID;
    DWORD dwFile;
    *pMsg >> dwSessionID >> dwFile;
    g_FileTransMgr.OnFileDone((IFile*)(DWORD_PTR)dwFile, dwSessionID);
    OnFileDone(dwSessionID, 0);
}

void CServerProcessMain::OnMsg_1006(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwSessionID;
    DWORD dwParam;
    *pMsg >> dwSessionID >> dwParam;
    g_FileTransMgr.OnFileFailed(dwSessionID);
    OnFileFailed(dwSessionID);
}

void CServerProcessMain::OnMsg_6300(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(0xA300);
    BYTE bResult = 1;
    pReply->WriteBytes(&bResult, 1);
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}

void CServerProcessMain::OnMsg_6303(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    BYTE bResult = 2;
    BYTE byMode;
    *pMsg >> byMode;
    BYTE bCheck = CheckModeChange(byMode);
    if (!bCheck)
    {
        if (byMode == 1)
            g_pServerApp->SetServerState(5);
        else if (byMode == 2)
            g_pServerApp->SetServerState(4);
        bResult = 1;
    }
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(0xA303);
    pReply->WriteBytes(&bResult, 1);
    pReply->WriteBytes(&bCheck, 1);
    pReply->WriteBytes(&byMode, 1);
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}

void CServerProcessMain::OnMsg_600D(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE bHeader;
    *pMsg >> bHeader;
    DWORD dwSessionID = pMsg->m_dwSessionID;
    MassiveMsgSessionMap::iterator it = m_mapMassive.find(dwSessionID);
    if (bHeader)
    {
        WORD wPartCount;
        WORD wOpcode;
        *pMsg >> wPartCount >> wOpcode;
        if (it != m_mapMassive.end())
        {
            it->second->Free();
            m_mapMassive.erase(it);
        }
        CMassiveMsg* pMassive = CMassiveMsg::Alloc(MASSIVE_MODE_READ);
        pMassive->SetRecvHeader(wOpcode, wPartCount);
        m_mapMassive.insert(MassiveMsgSessionMap::value_type(pMsg->m_dwSessionID, pMassive));
        return;
    }
    if (it == m_mapMassive.end())
        return;
    CMassiveMsg* pMassive = it->second;
    if (pMassive->AddRecvPart(pMsg))
    {
        pMassive->m_dw36 = pMsg->m_dwSessionID;
        Dispatch(NULL, 0, 0, pMassive);
        pMassive->Free();
        m_mapMassive.erase(it);
    }
}

void CServerProcessMain::OnMsg_2311(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strName;
    WORD wShardID;
    BYTE bType;
    *pMsg >> wShardID >> bType;
    if (!bType)
    {
        *pMsg >> strName;
        CERT_SHARD* pShard = FindCertShard(wShardID);
        if (!pShard)
            return;
        char* pszShardName = (char*)pShard->ab3 + 1;
        LogPrintf(0, "shard [%d] name changed : %s -> %s", pShard->wID, pszShardName,
                  strName.c_str());
#ifdef JMX_FRAMEWORK_2011
        // AgentServer 0x42d630: at most 31 characters.
        if (StringCchCopyA(pszShardName, 32, strName.c_str()))
            BSAssertFail();
#else
        if (StringCchCopyA(pszShardName, 256, strName.c_str()))
            BSAssertFail();
#endif
        for (ServerBodyList::iterator it = g_pServerBodyList->begin();
             it != g_pServerBodyList->end(); ++it)
        {
            SERVER_BODY* pBody = *it;
            if (pBody == g_pSelfBody || pBody->wParentID != g_pSelfBody->wID)
                continue;
            CMsg* pFwd = g_pBSNet->AllocMsg(FALSE);
            pFwd->SetOpcode(0x2311);
            pFwd->WriteBytes(&wShardID, 2);
            pFwd->WriteBytes(&bType, 1);
            *pFwd << strName;
            if (!ServerBody_SendMsg(pBody->wID, pFwd))
                g_pBSNet->ReleaseMsg(pFwd);
        }
        return;
    }

    WORD wMaxUser;
    *pMsg >> wMaxUser;
    CERT_SHARD* pShard = FindCertShard(wShardID);
    if (!pShard)
        return;
    LogPrintf(0, "shard [%s] max user changed : %d -> %d", (char*)pShard->ab3 + 1,
              pShard->w548, wMaxUser);
    pShard->w548 = wMaxUser;
    for (ServerBodyList::iterator it = g_pServerBodyList->begin(); it != g_pServerBodyList->end();
         ++it)
    {
        SERVER_BODY* pBody = *it;
        if (pBody == g_pSelfBody || pBody->wParentID != g_pSelfBody->wID)
            continue;
        CMsg* pFwd = g_pBSNet->AllocMsg(FALSE);
        pFwd->SetOpcode(0x2311);
        pFwd->WriteBytes(&wShardID, 2);
        pFwd->WriteBytes(&bType, 1);
        pFwd->WriteBytes(&wMaxUser, 2);
        if (!ServerBody_SendMsg(pBody->wID, pFwd))
            g_pBSNet->ReleaseMsg(pFwd);
    }
}

#if SERVER_MAIN_PROCESS_631D
void CServerProcessMain::OnMsg_631D(CMsg*, DWORD, DWORD, CMassiveMsg*)
{
    g_pServerApp->SetServerState(7);
}
#endif

void CServerProcessMain::OnCordEstablished(SERVER_CORD*, SERVER_BODY*)
{
}

void CServerProcessMain::OnCordClosed(SERVER_CORD*, SERVER_BODY*)
{
}

void CServerProcessMain::OnServerBodyStateChanged(SERVER_BODY*, int)
{
}

void CServerProcessMain::OnSessionClosed(DWORD)
{
}

BYTE CServerProcessMain::CheckModeChange(BYTE byMode)
{
    if (byMode == 1)
    {
        if (g_pSelfBody->nState == 4)
        {
            DWORD nCords = 0;
            DWORD nConnected = 0;
            ServerCord_CountForBody(g_pSelfBody->wID, &nCords, &nConnected);
            if (nCords == nConnected)
                return 0;
        }
    }
    else if (byMode == 2 && g_pSelfBody->nState == 5)
    {
        return 0;
    }
    return 3;
}

void CServerProcessMain::OnFileProgress(DWORD, DWORD)
{
}

void CServerProcessMain::OnFileDone(DWORD, DWORD)
{
}

void CServerProcessMain::OnFileFailed(DWORD)
{
}

int CServerProcessMain::ForwardBodyStateChanged(DWORD dwBody, DWORD dwOldState)
{
    OnServerBodyStateChanged((SERVER_BODY*)(DWORD_PTR)dwBody, (int)dwOldState);
    return 0;
}

} // namespace ServerFramework
