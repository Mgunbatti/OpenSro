#include "GlobalManagerMainProcess.h"

#include <string.h>
#include <string>
#include <strsafe.h>

#include "ContentUser.h"
#include "GlobalManagerJobs.h"
#include "GlobalManagerMainFwd.h"
#include "GlobalManagerSlave.h"
#include "GlobalManagerSvcFwd.h"
#include "PunishmentManager.h"
#include "UserValidationHistory.h"
#include "Billing/StandardBillingMgr.h"
#include "BSLib/BSDebug.h"
#include "BSLib/thread/Task.h"
#include "BSLib/Util/TimeSpan.h"
#include "BSNet/IBSNet.h"
#include "BSNet/Msg.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerFramework.h"

using namespace ServerFramework;

BS_IMPLEMENT_DYNCREATE(CGlobalManagerMainProcess, &CServerProcessMain::classCServerProcessMain)

// ---------------------------------------------------------------------------------------------
// Helpers

static CMsg* NewMsg(WORD wOpcode)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(wOpcode);
    return pMsg;
}

static void WriteByte(CMsg* pMsg, BYTE b)
{
    pMsg->WriteBytes(&b, 1);
}

// sub_411C40: "XX-XX-XX-XX-XX-XX" of the 6 bytes (asserts when it does not fit).
static void FormatMAC(std::string* pstrOut, const BYTE* pbyMAC)
{
    char sz[32];
    if (FAILED(StringCchPrintfA(sz, 32, "%02X-%02X-%02X-%02X-%02X-%02X", pbyMAC[0], pbyMAC[1],
                                pbyMAC[2], pbyMAC[3], pbyMAC[4], pbyMAC[5])))
    {
        BSAssertFail();
    }
    pstrOut->assign(sz, strlen(sz));
}

// sub_411AF0: WORD length and the characters of a massive message.
static void ReadMassiveString(CMassiveMsg* pMassive, std::string* pstr)
{
    WORD wLen = 0;
    pMassive->Read(&wLen, 2);
    if (!wLen)
    {
        pstr->clear();
        return;
    }
    pstr->resize(wLen);
    pMassive->Read(&(*pstr)[0], wLen);
}

#pragma pack(push, 1)
// The fixed-size blocks the handlers read like any value (front or back of the message).
struct GM_MAC_ADDRESS
{
    BYTE    ab[6];
};
struct GM_SILK_HISTORY_REQ
{
    BYTE    ab[14];
};
#pragma pack(pop)

// The DWORD reader of 0x2320 (inlined): asserts before it faults on a short message.
static DWORD ReadDwordForwardOnly(CMsg* pMsg)
{
    if (pMsg->m_wReadPos + 4 > pMsg->m_wWritePos)
    {
        BSAssertFail();
        BS_CRASH();
    }
    DWORD dw = *(DWORD*)(pMsg->m_pData + pMsg->m_wReadPos);
    pMsg->m_wReadPos += 4;
    return dw;
}

void ContentUser_SetRequest(DWORD dwJID, DWORD dwSessionID, DWORD dwRelayID)
{
    sContentUser* pUser = g_ContentUserManager.Find(dwJID);
    if (pUser)
    {
        pUser->dwSessionID = dwSessionID;
        pUser->dwRelayID = dwRelayID;
    }
}

void __stdcall GlobalManager_PostToSlave(CMsg* pMsg)
{
    if (!g_pGlobalManagerSlave)
        BSAssertFail();
    CTask* pTask = (CTask*)g_pGlobalManagerSlave->GetOwner();
    if (pTask)
        pTask->m_pQueue->Push(pMsg);
}

// sub_4129C0 (not referenced; COverlapJob_ResetPlayTimeOfGlobalServer::OnComplete inlines it).
void GlobalManagerMain_SetLastPlayTimeReset(DWORD dwTime)
{
    g_pGlobalManagerProcess->m_dwLastPlayTimeReset = dwTime;
}

// ---------------------------------------------------------------------------------------------
// Process

CGlobalManagerMainProcess::CGlobalManagerMainProcess()
{
}

CGlobalManagerMainProcess::~CGlobalManagerMainProcess()
{
}

BOOL CGlobalManagerMainProcess::Initialize(void* pOwner, void* pParam)
{
    if (!CServerProcessMain::Initialize(pOwner, pParam))
        return FALSE;
    m_pBillingMgr = NULL;
    g_pGlobalManagerProcess = this;
    CreateBillingMgr();
    if (!m_pBillingMgr->Start())
    {
        LogPrintf(0x2000000, "BillingMgr- Failed to Start Service!");
        return FALSE;
    }
    LogPrintf(0, "BillingMgr- Service Initialized!");
    return TRUE;
}

int CGlobalManagerMainProcess::SignalStop()
{
    if (m_pBillingMgr)
    {
        m_pBillingMgr->Stop();
        LogPrintf(0, "BillingMgr- Service Terminated!");
    }
    DestroyBillingMgr();
    return CServerProcessMain::SignalStop();
}

bool CGlobalManagerMainProcess::CreateBillingMgr()
{
    if (m_pBillingMgr)
        BSAssertFail();
    UINT nType = GetPrivateProfileIntA("billing", "type", 0, "./billing.ini");
    WORD wPort = (WORD)GetPrivateProfileIntA("billing", "port", 18810, "./billing.ini");
    WORD wPoolSize = (WORD)GetPrivateProfileIntA("billing", "poolsize", 0x2000, "./billing.ini");
    char szAddress[16];
    GetPrivateProfileStringA("billing", "ip", "127.0.0.1", szAddress, 16, "./billing.ini");
    if (nType)
        LogPrintf(0, "BILLING SERVER[type: %d, ip: %s, port: %d]", nType, szAddress, wPort);
    else
        LogPrintf(0, "BILLING SERVER[dummy]");
    if (nType == 1)
        m_pBillingMgr = static_cast<IBillingMgr*>(new CStandardBillingMgr(this));
    else
        m_pBillingMgr = new CDummyBillingMgr;
    if (!m_pBillingMgr)
        BSAssertFail();
    if (!m_pBillingMgr->Initialize(szAddress, wPort, wPoolSize))
    {
        LogPrintf(0x2000000, "BillingMgr- Initialization Failure!");
        return false;
    }
    LogPrintf(0, "BillingMgr- Created!");
    return true;
}

void CGlobalManagerMainProcess::DestroyBillingMgr()
{
    if (!m_pBillingMgr)
        return;
    m_pBillingMgr->Stop();
    m_pBillingMgr->Cleanup();
    m_pBillingMgr->Release();
    m_pBillingMgr = NULL;
    LogPrintf(0, "BillingMgr- Destroyed!");
}

void CGlobalManagerMainProcess::InitMsgHandlers()
{
    CServerProcessMain::InitMsgHandlers();
    RegisterHandler(GM_MSG_USER_LOGIN_REQ, &CGlobalManagerMainProcess::OnMsg_6201);
    RegisterHandler(GM_MSG_USER_CHECK_REQ, &CGlobalManagerMainProcess::OnMsg_6200);
    RegisterHandler(GM_MSG_USER_LOGOUT_REQ, &CGlobalManagerMainProcess::OnMsg_6202);
    RegisterHandler(GM_MSG_CHECK_CHAR_NAME_REQ, &CGlobalManagerMainProcess::OnMsg_620B);
    RegisterHandler(GM_MSG_MODULE_VERSION_REQ, &CGlobalManagerMainProcess::OnMsg_6306);
    RegisterHandler(GM_MSG_SHARD_USERS_REQ, &CGlobalManagerMainProcess::OnMsg_620C);
    RegisterHandler(0xAA00, &CGlobalManagerMainProcess::OnMsg_AA00);
    RegisterHandler(GM_MSG_CHAR_CREATE_REQ, &CGlobalManagerMainProcess::OnMsg_620E);
    RegisterHandler(GM_MSG_CHAR_DELETE_REQ, &CGlobalManagerMainProcess::OnMsg_620F);
    RegisterHandler(GM_MSG_CHAR_RENAME_REQ, &CGlobalManagerMainProcess::OnMsg_6210);
    RegisterHandler(GM_MSG_BILLING_6900_REQ, &CGlobalManagerMainProcess::OnMsg_6900);
    RegisterHandler(0x6901, &CGlobalManagerMainProcess::OnMsg_6901);
    RegisterHandler(0xA902, &CGlobalManagerMainProcess::OnMsg_A902);
    RegisterHandler(0xA903, &CGlobalManagerMainProcess::OnMsg_A903);
    RegisterHandler(GM_MSG_BILLING_6904_REQ, &CGlobalManagerMainProcess::OnMsg_6904);
    RegisterHandler(0xA905, &CGlobalManagerMainProcess::OnMsg_A905);
    RegisterHandler(GM_MSG_PUNISHMENT_REQ, &CGlobalManagerMainProcess::OnMsg_6318);
    RegisterHandler(0xA317, &CGlobalManagerMainProcess::OnMsg_A317);
    RegisterHandler(GM_MSG_VALIDATION_FAILED_REQ, &CGlobalManagerMainProcess::OnMsg_6111);
    RegisterHandler(GM_MSG_VALIDATION_PASSED_REQ, &CGlobalManagerMainProcess::OnMsg_6112);
    RegisterHandler(GM_MSG_SILK_HISTORY_REQ, &CGlobalManagerMainProcess::OnMsg_631A);
    RegisterHandler(GM_MSG_SILK_HISTORY_PARAM, &CGlobalManagerMainProcess::OnMsg_A31C);
    RegisterHandler(GM_MSG_PLAYTIME_RESTART, &CGlobalManagerMainProcess::OnMsg_2320);
    RegisterHandler(GM_MSG_BROADCAST_TO_SHARDS, &CGlobalManagerMainProcess::OnMsg_2327);
    m_SM.Init((CTask*)GetOwner());
    RegisterTimer(GM_TIMER_SHARD_USER_COUNT, 1000,
                  static_cast<PFN_PROCESS_TIMER>(&CGlobalManagerMainProcess::OnShardUserCountTimer));
    RegisterTimer(GM_TIMER_CASH, 5000,
                  static_cast<PFN_PROCESS_TIMER>(&CGlobalManagerMainProcess::OnCashTimer));
    RegisterTimer(GM_TIMER_SHARD_STATUS, 10000,
                  static_cast<PFN_PROCESS_TIMER>(&CGlobalManagerMainProcess::OnShardStatusTimer));
    RegisterTimer(GM_TIMER_SHARD_USER, 300000,
                  static_cast<PFN_PROCESS_TIMER>(&CGlobalManagerMainProcess::OnShardUserTimer));
    SchedulePlayTimeReset(FALSE);
    RegisterTimer(GM_TIMER_VALIDATION_DAY, 1000,
                  static_cast<PFN_PROCESS_TIMER>(&CGlobalManagerMainProcess::OnValidationDayTimer));
    m_dwCashTick = GetTickCount();
    m_dwShardStatusTick = GetTickCount();
}

void CGlobalManagerMainProcess::OnUnhandledMsg(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                               CMassiveMsg* pMassive)
{
    if (!m_SM.Dispatch(pMsg, pMassive, dwRelayID, dwContext))
        CServerProcessBase::OnUnhandledMsg(pMsg, dwRelayID, dwContext, pMassive);
}

void CGlobalManagerMainProcess::OnRelayFailed(WORD wOpcode, DWORD dwContext)
{
    switch (wOpcode)
    {
    case 0x6318:
    case 0x6902:
    case 0x6903:
    case 0x6905:
    case 0x6A00:
    case 0xA317:
    case 0xA902:
        return;
    case 0xA111:
    case 0xA112:
        LogPrintf(0x1000000, "IBUV Ack[%d] Relay failed!", wOpcode);
        return;
    }
    CServiceManagerProcess::OnRelayFailed(wOpcode, (sSMDefault_RelayMsg_Param*)dwContext);
}

void CGlobalManagerMainProcess::OnCordEstablished(SERVER_CORD*, SERVER_BODY* pPeer)
{
    if (!g_pSelfBody)
        return;
    ServerCordList listCord;
    ServerCord_CollectForBody(g_pSelfBody->wID, &listCord);
    bool bAllUp = true;
    for (ServerCordList::iterator it = listCord.begin(); it != listCord.end(); ++it)
    {
        DWORD dwStatus = (*it)->dwStatus;
        if (dwStatus != 2 && dwStatus != 3)
        {
            bAllUp = false;
            break;
        }
        // Checked for every connected cord before the first one that is not.
        if (pPeer->pModule == CertModule_FindByName("FarmManager") && IsPlayTimeResetDue())
            g_pGlobalManagerProcess->OnPlayTimeResetTimer(0x21);
    }
    ServerBody_SetStateAndBroadcast2005(bAllUp ? 5 : 4, g_pSelfBody->wID, 0);
}

void CGlobalManagerMainProcess::OnCordClosed(SERVER_CORD*, SERVER_BODY*)
{
    if (!g_pSelfBody)
        return;
    ServerCordList listCord;
    ServerCord_CollectForBody(g_pSelfBody->wID, &listCord);
    bool bAllUp = true;
    for (ServerCordList::iterator it = listCord.begin(); it != listCord.end(); ++it)
    {
        DWORD dwStatus = (*it)->dwStatus;
        if (dwStatus != 2 && dwStatus != 3)
        {
            bAllUp = false;
            break;
        }
    }
    ServerBody_SetStateAndBroadcast2005(bAllUp ? 5 : 4, g_pSelfBody->wID, 0);
    m_SM.OnCordClosed();
}

void CGlobalManagerMainProcess::OnServerBodyStateChanged(SERVER_BODY* pBody, int nOldState)
{
    if (pBody->pModule != CertModule_FindByName("AgentServer"))
        return;
    if (nOldState == 5 && pBody->nState != 5)
    {
        int nUsers = g_ContentUserManager.UnregisterByAgent(pBody->wID);
        LogPrintf(0x2000001, "agent server is broken !!. all associated user is unregistered : %d",
                  nUsers);
    }
}

void CGlobalManagerMainProcess::OnSessionClosed(DWORD dwSessionID)
{
    m_SM.OnSessionClosed(dwSessionID);
}

void CGlobalManagerMainProcess::OnFileDone(DWORD dwSessionID, DWORD)
{
    m_SM.OnFileDone(dwSessionID);
}

void CGlobalManagerMainProcess::OnFileFailed(DWORD)
{
    m_SM.OnFileFailed();
}

// ---------------------------------------------------------------------------------------------
// Content users

void CGlobalManagerMainProcess::OnMsg_2320(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwJID = ReadDwordForwardOnly(pMsg);
    sContentUser* pUser = g_ContentUserManager.Find(dwJID);
    if (pUser)
        pUser->PlayTime.dwTime = CDateTime::GetCurrent().GetTime();
}

void CGlobalManagerMainProcess::OnMsg_6201(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    DWORD dwIP = 0;
    DWORD dwJID = 0;
    std::string strID;
    std::string strMAC;
    GM_MAC_ADDRESS mac;
    memset(&mac, 0, sizeof(mac));
    BYTE byContentID = 0;
    WORD wShardID = 0;
    WORD wAgentBodyID = 0;
    *pMsg >> dwIP;
    *pMsg >> dwJID;
    *pMsg >> strID;
    *pMsg >> byContentID;
    *pMsg >> wShardID;
    *pMsg >> wAgentBodyID;
    *pMsg >> mac;
    FormatMAC(&strMAC, mac.ab);
    if (!m_pBillingMgr)
        BSAssertFail();

    sContentUser user;
    user.dwIP = dwIP;
    user.dwJID = dwJID;
    user.strID.assign(strID.c_str(), strlen(strID.c_str()));
    user.byContentID = byContentID;
    user.wShardID = wShardID;
    user.wAgentBodyID = wAgentBodyID;
    user.strMAC.assign(strMAC.c_str(), strlen(strMAC.c_str()));
    user.byIPType = 0;
    WORD wPlayTime;
    DWORD dwTime;
    *pMsg >> wPlayTime;
    *pMsg >> dwTime;
    // Only the play time of today is kept; the start time stays 0 then.
    if (ContentUser_IsNotToday(dwTime))
        user.PlayTime.SetNow();
    else
        user.PlayTime.wPlayTime = wPlayTime;

    BYTE byError = g_ContentUserManager.Register(&user);
    if (byError)
    {
        CMsg* pReply = NewMsg(GM_MSG_USER_LOGIN_ACK);
        pReply->WriteBytes(&dwJID, 4);
        WriteByte(pReply, 2);
        WriteByte(pReply, byError);
        SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
        return;
    }

    CMsg* pBilling = NewMsg(GM_MSG_USER_LOGIN_REQ);
    pBilling->WriteBytes(&dwIP, 4);
    pBilling->WriteBytes(&dwJID, 4);
    *pBilling << strID.c_str();
    pBilling->WriteBytes(&byContentID, 1);
    pBilling->WriteBytes(&wShardID, 2);
    pBilling->WriteBytes(&wAgentBodyID, 2);
    *pBilling << strMAC.c_str();
    ContentUser_SetRequest(dwJID, pMsg->m_dwSessionID, dwRelayID);
    int nResult = m_pBillingMgr->ProcessMsg(pBilling, pMsg->m_dwSessionID, dwRelayID);
    if (nResult != 2)
    {
        CMsg* pReply = NewMsg(GM_MSG_USER_LOGIN_ACK);
        pReply->WriteBytes(&dwJID, 4);
        if (nResult == 0)
        {
            WriteByte(pReply, 2);
            WriteByte(pReply, 6);
        }
        else
        {
            WriteByte(pReply, 1);
            pReply->WriteBytes(&user.byIPType, 1);
            pReply->WriteBytes(&user.PlayTime.wPlayTime, 2);
        }
        SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
    }
    g_pBSNet->ReleaseMsg(pBilling);
}

void CGlobalManagerMainProcess::OnMsg_6200(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    DWORD dwIP;
    DWORD dwJID;
    BYTE by;
    *pMsg >> dwIP;
    *pMsg >> dwJID;
    *pMsg >> by;
    CMsg* pReply = NewMsg(GM_MSG_USER_CHECK_ACK);
    if (!m_pBillingMgr)
        BSAssertFail();
    if (!m_pBillingMgr->IsReady())
    {
        pReply->WriteBytes(&dwJID, 4);
        WriteByte(pReply, 2);
        WriteByte(pReply, 6);
    }
    else
    {
        BYTE byError = g_ContentUserManager.CheckLogin(dwJID, dwIP);
        pReply->WriteBytes(&dwJID, 4);
        if (byError)
        {
            WriteByte(pReply, 2);
            WriteByte(pReply, byError);
        }
        else
        {
            WriteByte(pReply, 1);
        }
    }
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}

void CGlobalManagerMainProcess::OnMsg_6202(CMsg*, DWORD dwRelayID, DWORD, CMassiveMsg* pMassive)
{
    WORD wCount;
    pMassive->Read(&wCount, 2);
    CMassiveMsg* pReply = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pReply->SetOpcode(GM_MSG_USER_LOGOUT_ACK);
    pReply->Write(&wCount, 2);
    if (wCount == 1)
    {
        DWORD dwJID;
        pMassive->Read(&dwJID, 4);
        pReply->Write(&dwJID, 4);
        CMsg* pBilling = NewMsg(GM_MSG_USER_LOGOUT_REQ);
        WriteByte(pBilling, 1);
        pBilling->WriteBytes(&dwJID, 4);
        DWORD dwIP;
        if (g_ContentUserManager.Unregister(dwJID, &dwIP))
            m_pBillingMgr->ProcessMsg(pBilling, 0, 0);
        g_pBSNet->ReleaseMsg(pBilling);
    }
    else
    {
        // In parts of up to 1000 JIDs (one empty part for a count of 0).
        WORD wLeft = wCount;
        do
        {
            WORD wPart = wLeft >= 1000 ? 1000 : wLeft;
            CMsg* pBilling = NewMsg(GM_MSG_USER_LOGOUT_REQ);
            WriteByte(pBilling, 2);
            pBilling->WriteBytes(&wPart, 2);
            for (int i = wPart; i > 0; --i)
            {
                DWORD dwJID;
                pMassive->Read(&dwJID, 4);
                pReply->Write(&dwJID, 4);
                pBilling->WriteBytes(&dwJID, 4);
                DWORD dwIP;
                g_ContentUserManager.Unregister(dwJID, &dwIP);
            }
            m_pBillingMgr->ProcessMsg(pBilling, 0, 0);
            g_pBSNet->ReleaseMsg(pBilling);
            wLeft = (WORD)(wLeft - wPart);
        } while (wLeft);
    }
    SendRelayReplyMassive(pMassive->m_dw36, dwRelayID, pReply);
}

void CGlobalManagerMainProcess::OnMsg_620C(CMsg*, DWORD dwRelayID, DWORD, CMassiveMsg* pMassive)
{
    if (!pMassive)
    {
        BSAssertFail();
        return;
    }
    CMsg* pBilling = NewMsg(GM_MSG_SHARD_USERS_REQ);
    WORD wShardID;
    WORD wCount;
    pMassive->Read(&wShardID, 2);
    pMassive->Read(&wCount, 2);
    LogPrintf(0x2000001, "shard id(%d) count(%d)", wShardID, wCount);
    std::map<WORD, CERT_SHARD*>::iterator itShard = g_mapCertShard.find(wShardID);
    CERT_SHARD* pShard = itShard != g_mapCertShard.end() ? itShard->second : NULL;
    if (!pShard)
    {
        // BUG(GM-027): the billing message is not released.
        // "\xbe\xc6\xbb\xd4\xbd\xce" is Korean (code page 949).
        LogPrintf(0x2000000, "\xbe\xc6\xbb\xd4\xbd\xce %s(%d)",
                  "D:\\WORK2005\\Source\\JMX_CommonServers\\GlobalManager\\GlobalManager.cpp",
                  1597);
    }
    else
    {
        for (int i = wCount; i > 0; --i)
        {
            BYTE abyMAC[6] = { 0 };
            sContentUser user;
            pMassive->Read(&user.dwIP, 4);
            pMassive->Read(&user.dwJID, 4);
            ReadMassiveString(pMassive, &user.strID);
            pMassive->Read(&user.wAgentBodyID, 2);
            pMassive->Read(abyMAC, 6);
            WORD wPlayTime;
            pMassive->Read(&wPlayTime, 2);
            user.PlayTime.dwTime = CDateTime::GetCurrent().GetTime();
            user.wShardID = wShardID;
            user.PlayTime.wPlayTime = wPlayTime;
            user.byContentID = pShard->ab3[0];
            FormatMAC(&user.strMAC, abyMAC);
            g_ContentUserManager.Register(&user);
        }
        m_pBillingMgr->ProcessMsg(pBilling, 0, 0);
        g_pBSNet->ReleaseMsg(pBilling);
        LogPrintf(0x2000001, "concurrent shard user %d added : shard id %s", wCount,
                  (const char*)pShard + 4);
    }
    SendRelayReply(pMassive->m_dw36, dwRelayID, NewMsg(GM_MSG_SHARD_USERS_ACK));
}

// ---------------------------------------------------------------------------------------------
// Shard requests

void CGlobalManagerMainProcess::OnMsg_620B(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    COverlapJob_CheckCharName* pJob = OverlapJob_AllocCheckCharName();
    pJob->Setup((CTask*)GetOwner(), pMsg, pMsg->m_dwSessionID, dwRelayID);
    SendOverlapJob(pJob, NULL);
}

void CGlobalManagerMainProcess::OnMsg_6306(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    if (!g_pSelfBody || g_pSelfBody->nState < 4)
        return;
    SM_JOB* pJob = OverlapJob_AllocModuleVersionData();
    OverlapJob_SetupModuleVersionData((CTask*)GetOwner(), pJob, pMsg->m_dwSessionID);
    SendOverlapJob(pJob, NULL);
}

void CGlobalManagerMainProcess::ManageShardCharacter(CMsg* pMsg, DWORD dwRelayID, BYTE byType,
                                                     WORD wAckOpcode)
{
    std::string strUnused;
    COverlapJob_ManageShardCharacterData* pJob = NULL;
    WORD wShardID;
    *pMsg >> wShardID;
    if (g_mapCertShard.find(wShardID) != g_mapCertShard.end())
    {
        pJob = OverlapJob_AllocManageShardCharacterData();
        if (pJob->Setup((CTask*)GetOwner(), pMsg, dwRelayID, byType, wShardID))
        {
            SendOverlapJob(pJob, NULL);
            return;
        }
    }
    CMsg* pReply = NewMsg(wAckOpcode);
    WriteByte(pReply, 2);
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
    if (pJob)
        pJob->ReturnToPool();
}

void CGlobalManagerMainProcess::OnMsg_620E(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    ManageShardCharacter(pMsg, dwRelayID, 0, GM_MSG_CHAR_CREATE_ACK);
}

void CGlobalManagerMainProcess::OnMsg_620F(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    ManageShardCharacter(pMsg, dwRelayID, 1, GM_MSG_CHAR_DELETE_ACK);
}

void CGlobalManagerMainProcess::OnMsg_6210(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    ManageShardCharacter(pMsg, dwRelayID, 2, GM_MSG_CHAR_RENAME_ACK);
}

void CGlobalManagerMainProcess::OnMsg_AA00(CMsg*, DWORD, DWORD, CMassiveMsg*)
{
}

void CGlobalManagerMainProcess::OnMsg_6111(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    DWORD dwCount = 0;
    DWORD dwTolerance = 0;
    std::string strUserID;
    BYTE byCheck;
    bool bPunished = false;
    *pMsg >> strUserID;
    *pMsg >> byCheck;
    switch (byCheck)
    {
    case 0:
    case 1:
    case 2:
        if (!g_pUserValidationHistoryMgr)
            BSAssertFail();
        bPunished = g_pUserValidationHistoryMgr->RecordFailure(&strUserID, byCheck, &dwCount);
        if (!g_pUserValidationHistoryMgr)
            BSAssertFail();
        dwTolerance = g_pUserValidationHistoryMgr->m_aConfig[byCheck].dwTolerance;
        break;
    default:
        BSAssertFail();
        break;
    }
    BYTE byPunished = bPunished;
    CMsg* pReply = NewMsg(GM_MSG_VALIDATION_FAILED_ACK);
    *pReply << strUserID.c_str();
    pReply->WriteBytes(&byCheck, 1);
    pReply->WriteBytes(&byPunished, 1);
    pReply->WriteBytes(&dwTolerance, 4);
    pReply->WriteBytes(&dwCount, 4);
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}

void CGlobalManagerMainProcess::OnMsg_6112(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    std::string strUserID;
    *pMsg >> strUserID;
    if (!g_pUserValidationHistoryMgr)
        BSAssertFail();
    g_pUserValidationHistoryMgr->Clear(&strUserID);
    CMsg* pReply = NewMsg(GM_MSG_VALIDATION_PASSED_ACK);
    *pReply << strUserID.c_str();
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}

// ---------------------------------------------------------------------------------------------
// Billing

void CGlobalManagerMainProcess::OnMsg_6900(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    if (!m_pBillingMgr)
        BSAssertFail();
    DWORD dwJID;
    *pMsg >> dwJID;
    CMsg* pBilling = NewMsg(GM_MSG_BILLING_6900_REQ);
    pBilling->WriteBytes(&dwJID, 4);
    ContentUser_SetRequest(dwJID, pMsg->m_dwSessionID, dwRelayID);
    int nResult = m_pBillingMgr->ProcessMsg(pBilling, pMsg->m_dwSessionID, dwRelayID);
    if (nResult != 2)
    {
        BYTE byResult = nResult == 1 ? 1 : 2;
        CMsg* pReply = NewMsg(GM_MSG_BILLING_6900_ACK);
        pReply->WriteBytes(&dwJID, 4);
        pReply->WriteBytes(&byResult, 1);
        SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
    }
    g_pBSNet->ReleaseMsg(pBilling);
}

void CGlobalManagerMainProcess::OnMsg_6901(CMsg*, DWORD, DWORD, CMassiveMsg*)
{
}

void CGlobalManagerMainProcess::OnMsg_6904(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    if (!m_pBillingMgr)
        BSAssertFail();
    BYTE byResult = m_pBillingMgr->ProcessMsg(pMsg, pMsg->m_dwSessionID, dwRelayID) ? 1 : 2;
    CMsg* pReply = NewMsg(GM_MSG_BILLING_6904_ACK);
    pReply->WriteBytes(&byResult, 1);
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}

void CGlobalManagerMainProcess::OnMsg_A903(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    pMsg->SkipRemaining();
}

void CGlobalManagerMainProcess::OnMsg_A902(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    pMsg->SkipRemaining();
}

void CGlobalManagerMainProcess::OnMsg_A905(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE byResult;
    *pMsg >> byResult;
    if (byResult == 2)
    {
        DWORD dwJID;
        *pMsg >> dwJID;
        g_ContentUserManager.Unregister(dwJID, NULL);
    }
}

// ---------------------------------------------------------------------------------------------
// Punishments and the slave

void CGlobalManagerMainProcess::OnMsg_6318(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    DWORD dwJID;
    BYTE abyActive[PUNISHMENT_ACTIVE_BUFFER];
    *pMsg >> dwJID;
    if (!g_pPunishmentManager)
        BSAssertFail();
    g_pPunishmentManager->GetActive(dwJID, abyActive);
    CMsg* pReply = NewMsg(GM_MSG_PUNISHMENT_ACK);
    pReply->WriteBytes(&dwJID, 4);
    pReply->WriteBytes(abyActive, PUNISHMENT_ACTIVE_BUFFER);
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}

void CGlobalManagerMainProcess::OnMsg_A317(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    pMsg->SkipRemaining();
}

void CGlobalManagerMainProcess::OnMsg_631A(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwValue = 0;
    WORD wBodyID = 0;
    GM_SILK_HISTORY_REQ request;
    memset(&request, 0, sizeof(request));
    *pMsg >> dwValue;
    *pMsg >> wBodyID;
    *pMsg >> request;
    if (dwValue != 1)
        return;
    CMsg* pSlave = NewMsg(GM_MSG_SLAVE_JOB);
    WriteByte(pSlave, 1);
    pSlave->WriteBytes(&wBodyID, 2);
    pSlave->WriteBytes(&request, 14);
    GlobalManager_PostToSlave(pSlave);
}

void CGlobalManagerMainProcess::OnMsg_A31C(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strValue;
    *pMsg >> strValue;
    CMsg* pSlave = NewMsg(GM_MSG_SLAVE_JOB);
    WriteByte(pSlave, 2);
    WriteByte(pSlave, 0x10);
    *pSlave << strValue;
    GlobalManager_PostToSlave(pSlave);
}

void CGlobalManagerMainProcess::OnMsg_SkipByte(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE b;
    *pMsg >> b;
}

void CGlobalManagerMainProcess::OnMsg_2327(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    WORD wOpcode = 0;
    *pMsg >> wOpcode;
    CertShardList* pList = g_pCertShardList;
    for (CertShardList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        CERT_SHARD* pShard = *it;
        if (!pShard)
            continue;
        CMsg* pRelay = NewMsg(wOpcode);
        // The payload size minus the opcode WORD from the read position (the same data for
        // every shard).
        WORD wSize = (WORD)(*pMsg->m_pSize & MSG_SIZE_MASK);
        int nPos = pMsg->m_wReadPos;
        if (nPos >= pMsg->m_nCapacity)
            BSAssertFail();
        pRelay->WriteBytes(pMsg->m_pData + nPos, (WORD)(wSize - 2));
        RelayMsgToBody_6008(this, pRelay, pShard->wManagerID, 0, 0, 0);
    }
}

// ---------------------------------------------------------------------------------------------
// Timers

bool CGlobalManagerMainProcess::OnShardUserCountTimer(BYTE)
{
    if (g_pSelfBody)
        ContentUser_BroadcastShardUserCount();
    return true;
}

bool CGlobalManagerMainProcess::OnCashTimer(BYTE)
{
    if (g_pSelfBody && GetTickCount() - m_dwCashTick >= 5000)
    {
        m_dwCashTick = GetTickCount();
        COverlapJob_RefreshCommercialUser_Cash* pJob = OverlapJob_AllocRefreshCommercialUserCash();
        pJob->Init((CTask*)GetOwner());
        SendOverlapJob(pJob, NULL);
    }
    return true;
}

bool CGlobalManagerMainProcess::OnShardStatusTimer(BYTE)
{
    if (g_pSelfBody && GetTickCount() - m_dwShardStatusTick >= 10000)
    {
        m_dwShardStatusTick = GetTickCount();
        COverlapJob_RefreshShardStatus* pJob = OverlapJob_AllocRefreshShardStatus();
        pJob->Init((CTask*)GetOwner());
        SendOverlapJob(pJob, NULL);
    }
    return true;
}

bool CGlobalManagerMainProcess::OnShardUserTimer(BYTE)
{
    if (g_pSelfBody)
    {
        COverlapJob_RefreshShardUser* pJob = OverlapJob_AllocRefreshShardUser();
        pJob->Init((CTask*)GetOwner());
        SendOverlapJob(pJob, NULL);
    }
    return true;
}

bool CGlobalManagerMainProcess::OnPunishmentTimer(BYTE)
{
    if (g_pSelfBody)
    {
        if (!g_pPunishmentManager)
            BSAssertFail();
        g_pPunishmentManager->Tick();
    }
    return true;
}

bool CGlobalManagerMainProcess::OnValidationDayTimer(BYTE)
{
    if (g_pSelfBody)
    {
        if (!g_pUserValidationHistoryMgr)
            BSAssertFail();
        g_pUserValidationHistoryMgr->CheckDay();
    }
    return true;
}

void CGlobalManagerMainProcess::SchedulePlayTimeReset(BOOL bUpdate)
{
    CDateTime dtNow = CDateTime::GetCurrent();
    CTimeSpan spanDay(0, 24, 0, 0);
    int nSecond = dtNow.GetSecond();
    int nMinute = dtNow.GetMinute();
    int nHour = dtNow.GetHour();
    CTimeSpan spanNow(0, nHour, nMinute, nSecond);
    DWORD dwPeriod = 1000 * (spanDay - spanNow).GetTotalSeconds();
    if (bUpdate)
        SetTimerPeriod(GM_TIMER_PLAYTIME_RESET, dwPeriod);
    else
        RegisterTimer(GM_TIMER_PLAYTIME_RESET, dwPeriod,
                      static_cast<PFN_PROCESS_TIMER>(
                          &CGlobalManagerMainProcess::OnPlayTimeResetTimer));
}

bool CGlobalManagerMainProcess::IsPlayTimeResetDue()
{
    CGlobalManagerMainProcess* pMain = g_pGlobalManagerProcess;
    if (!pMain->m_dwLastPlayTimeReset)
        BSAssertFail();
    CDateTime dtLast(pMain->m_dwLastPlayTimeReset);
    CDateTime dtNow = CDateTime::GetCurrent();
    int nDay = dtNow.GetDay();
    if (dtLast.GetDay() != nDay)
        return true;
    int nMonth = dtNow.GetMonth();
    if (dtLast.GetMonth() != nMonth)
        return true;
    int nYear = dtNow.GetYear();
    return dtLast.GetYear() != nYear;
}

bool CGlobalManagerMainProcess::OnPlayTimeResetTimer(BYTE)
{
    SchedulePlayTimeReset(TRUE);
    COverlapJob_ResetPlayTimeOfGlobalServer* pJob = OverlapJob_AllocResetPlayTime();
    if (!pJob)
    {
        BSAssertFail();
        return false;
    }
    CDateTime dtNow = CDateTime::GetCurrent();
    DWORD dwTime = dtNow.GetTime();
    m_dwLastPlayTimeReset = dwTime;
    if (!pJob->Setup(1, dwTime))
        return false;
    int nSecond = dtNow.GetSecond();
    int nMinute = dtNow.GetMinute();
    int nHour = dtNow.GetHour();
    DWORD dwElapsed = CTimeSpan(0, nHour, nMinute, nSecond).GetTotalSeconds();
    if (dwElapsed >= 0xFFFF)
        dwElapsed = 0xFFFF;
    SendOverlapJob(pJob, NULL);
    g_ContentUserManager.ResetPlayTimes();
    CertShardList* pList = g_pCertShardList;
    for (CertShardList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        CERT_SHARD* pShard = *it;
        CMsg* pRelay = NewMsg(GM_MSG_PLAYTIME_RESET);
        WORD wElapsed = (WORD)dwElapsed;
        pRelay->WriteBytes(&wElapsed, 2);
        RelayMsgToBody_6008(this, pRelay, pShard->wManagerID, 0, 0, 0);
    }
    return true;
}
