#include "AgentMainProcess.h"

#include <string.h>

#include "Token.h"
#include "User.h"
#include "XTrap.h"
#include "BSLib/thread/Task.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/ServerApp.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerFramework.h"

using namespace ServerFramework;

BS_IMPLEMENT_DYNCREATE(CAgentMainProcess, &CServerProcessMain::classCServerProcessMain)

DWORD g_dwAgentMsgSentCount;
DWORD g_dwAgentMsgSentBytes;

#pragma pack(push, 1)
// The data of an XTrap answer (0x2113): BYTE step, then the step data.
struct AGENT_XTRAP_PACKET
{
    BYTE    abData[1 + XTRAP_PACKET_SIZE];
};
#pragma pack(pop)

C_ASSERT(sizeof(AGENT_XTRAP_PACKET) == 1025);

static void WriteByte(CMsg* pMsg, BYTE b)
{
    pMsg->WriteBytes(&b, 1);
}

// The GlobalManager body of this server's division: target of the logins and logouts.
static WORD GlobalManagerID()
{
    return g_pSelfBody->pDivision->wGlobalManagerID;
}

// The logout notice to the GlobalManager: 0x6202 {WORD 1, DWORD JID}.
static void SendLogoutToGlobalManager(CAgentMainProcess* pProcess, sUser* pUser,
                                      bool bLogFailure)
{
    CMassiveMsg* pLogout = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pLogout->SetOpcode(AGENT_MSG_USER_LOGOUT);
    DWORD dwCount = 1;
    pLogout->Write(&dwCount, 2);
    pLogout->Write(&pUser->dwJID, 4);
    if (!RelayMassiveMsgToBody_6008(pLogout, pProcess, GlobalManagerID(), 0, 0) && bLogFailure)
    {
        // "Big trouble.." (Korean, code page 949)
        LogPrintf(0x1000000, "\xc5\xab\xc0\xcf\xb3\xb5\xb3\xd7..");
    }
}

// 0xA103 {2, byError} to a client session.
static void SendLoginFailure(DWORD dwSessionID, BYTE byError)
{
    CMsg* pReply = g_pBSNet->AllocMsg(TRUE);
    pReply->SetOpcode(AGENT_MSG_LOGIN_ACK);
    WriteByte(pReply, 2);
    WriteByte(pReply, byError);
    g_pBSNet->SendMsg(dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
}

// 0xA207 {DWORD JID, 2, byError}, answer to a move request.
static CMsg* NewMoveFailure(DWORD dwJID, BYTE byError)
{
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(AGENT_MSG_MOVE_USER_ACK);
    pReply->WriteBytes(&dwJID, 4);
    WriteByte(pReply, 2);
    WriteByte(pReply, byError);
    return pReply;
}

// ---------------------------------------------------------------------------------------------
// Reports

// The user's AgentServer.exe has the four LogPrintf calls of this function replaced by NOPs (the
// argument pushes and the stack cleanup are still there); AGENT_XTRAP_DETECTION_OFF builds that
// variant: the lookups run, nothing is logged.
// RECONSTRUCTED: the NOPed instructions are 5-byte calls (0x401ea2, 0x401ed2, 0x401f00, 0x401f19)
// after pushes of the flags 0x2000001 and a format string, followed by "add esp, 0x18 / 0x10 /
// 0x0c" = 6 / 4 / 3 cdecl arguments: LogPrintf (0x434400) as everywhere else in the module.
void AgentMainProcess_ReportSuspect(const WORD* pwOpcode, const DWORD* pdwSessionID)
{
    std::string strIP;
    sUser* pUser = g_UserManager.Find(*pdwSessionID);
    BOOL bAddress = g_pBSNet->GetSessionPeerAddress(*pdwSessionID, &strIP, NULL, NULL);
#ifdef AGENT_XTRAP_DETECTION_OFF
    (void)pUser;
    (void)pwOpcode;
    (void)bAddress;
#else
    if (pUser)
    {
        if (bAddress == 1)
        {
            LogPrintf(0x2000001,
                      "WARNING!! A SUSPECT DETECTED!!! JID[%d] IP[%s] UserID[%s] MsgID[0x%x]",
                      pUser->dwJID, strIP.c_str(), pUser->strID.c_str(), *pwOpcode);
        }
        else
        {
            LogPrintf(0x2000001,
                      "WARNING!! A SUSPECT DETECTED!!! JID[%d] IP[%d] UserID[%s] MsgID[0x%x]",
                      pUser->dwJID, pUser->dwIP, pUser->strID.c_str(), *pwOpcode);
        }
    }
    else if (bAddress == 1)
    {
        LogPrintf(0x2000001, "WARNING!! A SUSPECT DETECTED!!! MsgID[0x%x] IP[%s]", *pwOpcode,
                  strIP.c_str());
    }
    else
    {
        LogPrintf(0x2000001, "WARNING!! A SUSPECT DETECTED!!! MsgID[0x%x]", *pwOpcode);
    }
#endif
}

void AgentMainProcess_LogRegisterFailure(WORD wOpcode, BYTE byError)
{
    std::string strMsg;
    std::string strError;
    if (wOpcode == AGENT_MSG_USER_LOGIN_ACK)
    {
        strMsg.assign("FRAMEWORKMSG_REGISTER_CONTENT_USER_ACK", 38);
        switch (byError)
        {
        case 1:
            strError = "User Duplicated";
            break;
        case 2:
            strError = "Insufficient IP";
            break;
        case 3:
            strError = "Not Service Time";
            break;
        case 4:
            strError = "Block Gratis User";
            break;
        case 5:
            strError = "Unknown";
            break;
        case 6:
            strError = "Billing Failed";
            break;
        default:
            strError.assign("Unknown ErrorCode!", 18);
            break;
        }
    }
    else if (wOpcode == AGENT_MSG_SHARD_LOGIN_ACK)
    {
        strMsg.assign("FRAMEWORKMSG_REGISTER_SHARD_USER_ACK", 36);
        switch (byError)
        {
        case 1:
            strError.assign("Create User", 11);
            break;
        case 2:
            strError.assign("Duplicated", 10);
            break;
        case 3:
            strError.assign("MaxUser Exceeded", 16);
            break;
        default:
            strError.assign("Unknown ErrorCode!", 18);
            break;
        }
    }
    else
    {
        strError.assign("ErrorCode Undefined", 19);
    }
    LogPrintf(0x2000001, "Msg[%s] Failed - %s", strMsg.c_str(), strError.c_str());
}

// ---------------------------------------------------------------------------------------------
// CAgentMainProcess: process

CAgentMainProcess::CAgentMainProcess()
{
}

CAgentMainProcess::~CAgentMainProcess()
{
}

BOOL CAgentMainProcess::Initialize(void* pOwner, void* pParam)
{
    if (!CServerProcessMain::Initialize(pOwner, pParam))
        return FALSE;
    RegisterTimer(AGENT_TOKEN_TIMER_ID, AGENT_TOKEN_TIMER_PERIOD,
                  static_cast<PFN_PROCESS_TIMER>(&CAgentMainProcess::OnTimer));
    return TRUE;
}

// The structured exception handler of the base loop is a catch-all here.
static void DispatchProtected(CAgentMainProcess* pProcess, CMsg* pMsg, int nIndex)
{
    try
    {
        DWORD dwTick = GetTickCount();
        if (dwTick - g_dwXTrapLastBroadcastTick > XTRAP_BROADCAST_PERIOD)
        {
            g_dwXTrapLastBroadcastTick = dwTick;
            XTrap_BroadcastStep1();
        }
        if (!nIndex)
            BSAssertFail();
        pMsg->m_pOwner = (void*)(INT_PTR)nIndex;
        pProcess->Dispatch(pMsg, 0, 0, NULL);
        CheckMsgFullyConsumed(pMsg);
        g_pBSNet->ReleaseMsg(pMsg);
    }
    catch (...)
    {
        WORD wOpcode = pMsg->GetOpcode();
        DWORD dwSessionID = pMsg->m_dwSessionID;
        AgentMainProcess_ReportSuspect(&wOpcode, &dwSessionID);
        if (g_pBSNet && pMsg->m_dwSessionID)
            g_pBSNet->DisconnectSession(pMsg->m_dwSessionID, 1);
        pMsg->SkipRemaining();
        g_pBSNet->ReleaseMsg(pMsg);
    }
}

DWORD CAgentMainProcess::Run(volatile LONG* pbStop, int nIndex)
{
    if (*pbStop)
        return 0;
    while (!*pbStop || m_bRunAfterStop == 1)
    {
        CMsg* pMsg = NULL;
        if (!((CTask*)m_pOwner)->m_pQueue->Pop(&pMsg, INFINITE, FALSE))
            break;
        if (pMsg)
            DispatchProtected(this, pMsg, nIndex);
    }
    BS_THREAD_TRACE(GetRuntimeClass()->m_lpszClassName);
    return 0;
}

int CAgentMainProcess::SignalStop()
{
    for (AgentBroadcastMap::iterator it = m_mapBroadcast.begin(); it != m_mapBroadcast.end();
         ++it)
    {
        AGENT_BROADCAST* pBroadcast = it->second;
        if (pBroadcast)
            delete pBroadcast;
    }
    m_mapBroadcast.clear();
    return CServerProcessBase::SignalStop();
}

void CAgentMainProcess::InitMsgHandlers()
{
    CServerProcessMain::InitMsgHandlers();
    RegisterHandler(AGENT_MSG_TOKEN_REQ, &CAgentMainProcess::OnMsg_6203);
    RegisterHandler(AGENT_MSG_LOGIN_REQ, &CAgentMainProcess::OnMsg_6103);
    RegisterHandler(AGENT_MSG_USER_LOGIN_ACK, &CAgentMainProcess::OnMsg_A201);
    RegisterHandler(AGENT_MSG_SHARD_LOGIN_ACK, &CAgentMainProcess::OnMsg_A204);
    RegisterHandler(AGENT_MSG_USER_LOGOUT_ACK, &CAgentMainProcess::OnMsg_A202);
    RegisterHandler(AGENT_MSG_SHARD_LOGOUT_ACK, &CAgentMainProcess::OnMsg_A205);
    RegisterHandler(AGENT_MSG_KICK_USERS, &CAgentMainProcess::OnMsg_2206);
    RegisterHandler(AGENT_MSG_MOVE_USER_REQ, &CAgentMainProcess::OnMsg_6207);
    RegisterHandler(AGENT_MSG_MOVE_TARGET_ACK, &CAgentMainProcess::OnMsg_A208);
    RegisterHandler(AGENT_MSG_RELAY_TO_CLIENT, &CAgentMainProcess::OnMsg_2209);
    RegisterHandler(AGENT_MSG_RELAY_TO_SESSIONS, &CAgentMainProcess::OnMsg_220A);
    RegisterHandler(AGENT_MSG_RELAY_MULTI_DATA, &CAgentMainProcess::OnMsg_220E);
    RegisterHandler(AGENT_MSG_RELAY_MULTI_LIST, &CAgentMainProcess::OnMsg_220F);
    RegisterHandler(AGENT_MSG_BAN_USER_REQ, &CAgentMainProcess::OnMsg_620D);
    RegisterHandler(AGENT_MSG_BILLING_NOTICE_REQ, &CAgentMainProcess::OnMsg_6902);
    RegisterHandler(AGENT_MSG_BILLING_BAN_REQ, &CAgentMainProcess::OnMsg_6905);
    RegisterHandler(AGENT_MSG_XTRAP, &CAgentMainProcess::OnMsg_2113);
}

bool CAgentMainProcess::IsClientMsgAllowed(CMsg* pMsg, CMassiveMsg* pMassive)
{
    WORD wOpcode = 0;
    if (pMsg)
    {
        if (pMassive)
            BSAssertFail();
        wOpcode = pMsg->GetOpcode();
        if (wOpcode == MASSIVE_MSG_OPCODE)
        {
            CMassiveMsg* pPending = NULL;
            MassiveMsgSessionMap::iterator it = m_mapMassive.find(pMsg->m_dwSessionID);
            if (it != m_mapMassive.end())
                pPending = it->second;
            wOpcode = GetMassiveOpcode(pMsg, pPending);
        }
    }
    else if (pMassive)
    {
        wOpcode = pMassive->m_wOpcode;
    }
    switch (wOpcode)
    {
    case 0x2001:
    case 0x2002:
    case 0x2110:
    case 0x2113:
    case 0x6103:
    case 0x6110:
    case 0x6314:
    case 0x6316:
        return true;
    }
    return false;
}

// BUG(AS-001): a refused complete massive message is freed and erased here, and OnMsg_600D frees
// and erases it again when Dispatch returns.
void CAgentMainProcess::Dispatch(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                 CMassiveMsg* pMassive)
{
    WORD wOpcode;
    DWORD dwSessionID;
    if (pMassive)
    {
        wOpcode = pMassive->m_wOpcode;
        pMsg = NULL;
        dwSessionID = pMassive->m_dw36;
    }
    else
    {
        wOpcode = pMsg->GetOpcode();
        pMassive = NULL;
        dwSessionID = pMsg->m_dwSessionID;
    }
    if (dwSessionID && g_pSelfBody && g_pSelfBody->nState == 5)
    {
        bool bCheckCord;
        if (!(wOpcode & 0x1000) && !IsClientMsgAllowed(pMsg, pMassive))
            bCheckCord = true;
        else
            bCheckCord = (wOpcode & 0x8800) != 0;
        if (bCheckCord && !ServerCordMap_FindBySession(dwSessionID))
        {
            if (pMsg)
                pMsg->SkipRemaining();
            if (pMassive)
            {
                MassiveMsgSessionMap::iterator it = m_mapMassive.find(dwSessionID);
                if (it != m_mapMassive.end())
                {
                    it->second->Free();
                    m_mapMassive.erase(it);
                }
            }
            AgentMainProcess_ReportSuspect(&wOpcode, &dwSessionID);
            g_pBSNet->DisconnectSession(dwSessionID, 1);
            return;
        }
    }
    (this->*m_aHandler[wOpcode])(pMsg, dwRelayID, dwContext, pMassive);
}

void CAgentMainProcess::OnUnhandledMsg(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                       CMassiveMsg* pMassive)
{
    DWORD dwSessionID;
    if (pMassive)
    {
        dwSessionID = pMassive->m_dw36;
        pMsg = NULL;
        WORD wOpcode = pMassive->m_wOpcode;
        if (wOpcode >= 0x6314 && (wOpcode <= 0x6316 || wOpcode == 0xA314))
            return;
    }
    else
    {
        dwSessionID = pMsg->m_dwSessionID;
        pMassive = NULL;
    }
    sUser* pUser = g_UserManager.Find(dwSessionID);
    if (!pUser)
    {
        CServerProcessBase::OnUnhandledMsg(pMsg, dwRelayID, dwContext, pMassive);
        return;
    }
    if (!g_UserManager.CheckTraffic(pUser))
    {
        LogPrintf(0x2000001, "[TrafficFilter] Heavy traffic is occured by [%s]. disconnected.",
                  pUser->strID.c_str());
        if (pMsg)
            pMsg->SkipRemaining();
        g_pBSNet->DisconnectSession(pUser->dwSessionID, 1);
        return;
    }
    // The client's packet goes on to the game server with the JID at the end (read back from
    // there); massive messages of users are dropped.
    // BUG(AS-006): ServerBody_SendMsg releases the message the message loop releases again.
    if (pMsg)
    {
        pMsg->WriteBytes(&pUser->dwJID, 4);
        ServerBody_SendMsg(pUser->wTargetBodyID, pMsg);
    }
}

void CAgentMainProcess::OnRelayFailed(WORD wOpcode, DWORD dwContext)
{
    switch (wOpcode)
    {
    case AGENT_MSG_USER_LOGIN_REQ:
        {
            sUser* pUser = g_UserManager.Find(dwContext);
            if (!pUser)
                return;
            SendLoginFailure(pUser->dwSessionID, 3);
            g_UserManager.Unregister(pUser->dwSessionID);
        }
        return;
    case AGENT_MSG_USER_LOGOUT:
    case AGENT_MSG_SHARD_LOGOUT:
        return;
    case AGENT_MSG_SHARD_LOGIN_REQ:
        {
            sUser* pUser = g_UserManager.Find(dwContext);
            if (!pUser)
                return;
            SendLoginFailure(pUser->dwSessionID, 3);
            SendLogoutToGlobalManager(this, pUser, false);
            g_UserManager.Unregister(pUser->dwSessionID);
        }
        return;
    case AGENT_MSG_MOVE_TARGET_REQ:
        {
            AGENT_MOVE_REQUEST* pRequest = (AGENT_MOVE_REQUEST*)dwContext;
            CMsg* pReply = NewMoveFailure(pRequest->dwJID, 2);
            SendRelayReply(pRequest->dwSessionID, pRequest->dwRelayID, pReply);
            delete pRequest;
        }
        return;
    }
    LogPrintf(0x1000000, "unhandled relay msg error : wRealMsgID : 0x%X : dwParam : 0x%X",
              wOpcode, dwContext);
}

void CAgentMainProcess::OnCordClosed(SERVER_CORD* pCord, SERVER_BODY* pPeer)
{
    if (pPeer->pModule == CertModule_FindByName("FarmManager"))
        return;
    AgentUserMap& mapUser = g_UserManager.m_mapUser;
    DWORD dwCount = 0;
    for (AgentUserMap::iterator it = mapUser.begin(); it != mapUser.end(); ++it)
    {
        sUser* pUser = it->second;
        if (pUser->wHomeBodyID == pPeer->wID || pUser->wTargetBodyID == pPeer->wID)
            ++dwCount;
    }
    CMassiveMsg* pLogout = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pLogout->SetOpcode(AGENT_MSG_USER_LOGOUT);
    pLogout->Write(&dwCount, 2);
    dwCount = 0;
    AgentUserMap::iterator it = mapUser.begin();
    while (it != mapUser.end())
    {
        sUser* pUser = it->second;
        if (pUser->wHomeBodyID != pPeer->wID && pUser->wTargetBodyID != pPeer->wID)
        {
            ++it;
            continue;
        }
        mapUser.erase(it++);
        g_pBSNet->DisconnectSession(pUser->dwSessionID, 1);
        pLogout->Write(&pUser->dwJID, 4);
        pUser->Release();
        ++dwCount;
    }
    AgentBroadcastMap::iterator itBroadcast = m_mapBroadcast.find(pCord->dwSessionID);
    if (itBroadcast != m_mapBroadcast.end())
    {
        AGENT_BROADCAST* pBroadcast = itBroadcast->second;
        pBroadcast->wRealMsgID = 0;
        pBroadcast->vecTarget.clear();
    }
    RelayMassiveMsgToBody_6008(pLogout, this, GlobalManagerID(), 0, 0);
    LogPrintf(0x2000001, "%d:%s is blind : %d users are banned", pPeer->wID,
              pPeer->pModule->szName, (WORD)dwCount);
    g_pServerApp->SetServerState(4);
}

void CAgentMainProcess::OnSessionClosed(DWORD dwSessionID)
{
    sUser* pUser = g_UserManager.Find(dwSessionID);
    if (!pUser)
        return;
    SendLogoutToGlobalManager(this, pUser, false);
    CMsg* pLogout = g_pBSNet->AllocMsg(FALSE);
    pLogout->SetOpcode(AGENT_MSG_SHARD_LOGOUT);
    pLogout->WriteBytes(&pUser->dwJID, 4);
    RelayMsgToBody_6008(this, pLogout, pUser->wHomeBodyID, 0, 0, pUser->dwSessionID);
    MassiveMsgSessionMap::iterator it = m_mapMassive.find(dwSessionID);
    if (it != m_mapMassive.end())
    {
        it->second->Free();
        m_mapMassive.erase(it);
    }
    g_UserManager.Unregister(pUser->dwSessionID);
}

bool CAgentMainProcess::OnTimer(BYTE byTimerID)
{
    if (byTimerID == AGENT_TOKEN_TIMER_ID)
        g_TokenManager.CheckTimeout();
    return true;
}

// ---------------------------------------------------------------------------------------------
// Login

// 0x6203 (relayed from the GatewayServer) {DWORD client address, string ID, string password,
// DWORD JID, BYTE, BYTE security groups, WORD, DWORD}: answers 0xA203 {1, DWORD token ID,
// WORD this body} or {2}.
void CAgentMainProcess::OnMsg_6203(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    if (!dwRelayID)
        BSAssertFail();
    CToken* pToken = new CToken;
    *pMsg >> pToken->m_dwClientIP >> pToken->m_strID >> pToken->m_strPassword;
    *pMsg >> pToken->m_dwJID >> pToken->m_bySecPrimary >> pToken->m_bySecContent;
    *pMsg >> pToken->m_w50 >> pToken->m_dw54;
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(AGENT_MSG_TOKEN_ACK);
    if (g_TokenManager.Add(pToken))
    {
        WriteByte(pReply, 1);
        pReply->WriteBytes(&pToken->m_dwTokenID, 4);
        pReply->WriteBytes(&g_pSelfBody->wID, 2);
    }
    else
    {
        // the token is not deleted (its ID is unique while it lives, so this does not happen)
        WriteByte(pReply, 2);
    }
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}

// 0x6103 {DWORD token ID, string ID, string password, BYTE content, 6 bytes MAC} from a client.
// A wrong content closes the session without answer. Failures answer 0xA103 {2, error} and close
// the session: 3 not in service or unknown token, 1 token check failed, 2 the session is a user
// already, 3 no route to the GlobalManager. Otherwise the user is registered and the login goes
// to the GlobalManager (0x6201); its answer (0xA201) continues it.
void CAgentMainProcess::OnMsg_6103(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    std::string strID;
    std::string strPassword;
    DWORD dwTokenID;
    BYTE byContent;
    AGENT_MAC_ADDRESS mac;
    mac.dwLow = 0;
    mac.wHigh = 0;
    DWORD dwJID = 0;
    *pMsg >> dwTokenID >> strID >> strPassword >> byContent >> mac;
    if (byContent != g_pSelfBody->pShard->ab3[0])
    {
        LogPrintf(0x2000001,
                  "Someone tried to login using strange client that has another content id");
        g_pBSNet->DisconnectSession(pMsg->m_dwSessionID, 1);
        return;
    }
    // BUG(AS-005): the result is not checked; without the peer address the token is compared
    // with an uninitialised value.
    DWORD dwIP;
    u_short wPort;
    g_pBSNet->GetSessionPeerAddress(pMsg->m_dwSessionID, NULL, &dwIP, &wPort);
    CMsg* pReply = g_pBSNet->AllocMsg(TRUE);
    pReply->SetOpcode(AGENT_MSG_LOGIN_ACK);
    WORD w50;
    DWORD dw54;
    BYTE bySecPrimary;
    BYTE bySecContent;
    if (!g_pSelfBody || g_pSelfBody->nState != 5
        || !g_TokenManager.GetExtra(dwTokenID, &w50, &dw54))
    {
        WriteByte(pReply, 2);
        WriteByte(pReply, 3);
    }
    else if (!g_TokenManager.Consume(dwTokenID, dwIP, strID.c_str(), strPassword.c_str(),
                                     &dwJID, &bySecPrimary, &bySecContent))
    {
        WriteByte(pReply, 2);
        WriteByte(pReply, 1);
    }
    else
    {
        sUser* pUser = UserPool_Alloc();
        pUser->dwSessionID = pMsg->m_dwSessionID;
        pUser->dwJID = dwJID;
        pUser->strID.assign(strID, 0, std::string::npos);
        pUser->bySecPrimary = bySecPrimary;
        pUser->bySecContent = bySecContent;
        pUser->wHomeBodyID = g_pSelfBody->pShard->wManagerID;
        pUser->wTargetBodyID = g_pSelfBody->pShard->wManagerID;
        pUser->dwMacLow = mac.dwLow;
        pUser->dwIP = dwIP;
        pUser->byLoginResult = 0;
        pUser->wMacHigh = mac.wHigh;
        pUser->ResetTrafficCounter();
        if (!g_pfnXTrap_S_SessionInit(XTRAP_TIMEOUT, XTRAP_MAP_COUNT, g_abXTrapMap,
                                      pUser->abXTrapSession))
        {
            pUser->dwXTrapTick = GetTickCount();
        }
        else
        {
            // "[For Xtrap] SessionInit failed user : %s " (Korean, code page 949)
            LogPrintf(0x2000000, "[For Xtrap] SessionInit \xbd\xc7\xc6\xd0 \xc0\xaf\xc0\xfa : %s ",
                      pUser->strID.c_str());
            LogPrintf(0x2000001, "[For Xtrap] SessionInit \xbd\xc7\xc6\xd0 \xc0\xaf\xc0\xfa : %s ",
                      pUser->strID.c_str());
        }
        if (!g_UserManager.Register(pUser))
        {
            pUser->Release();
            WriteByte(pReply, 2);
            WriteByte(pReply, 2);
        }
        else
        {
            CMsg* pLogin = g_pBSNet->AllocMsg(FALSE);
            pLogin->SetOpcode(AGENT_MSG_USER_LOGIN_REQ);
            pLogin->WriteBytes(&dwIP, 4);
            pLogin->WriteBytes(&dwJID, 4);
            *pLogin << strID.c_str();
            pLogin->WriteBytes(&byContent, 1);
            pLogin->WriteBytes(&g_pSelfBody->pShard->wID, 2);
            pLogin->WriteBytes(&g_pSelfBody->wID, 2);
            pLogin->WriteBytes(&mac, 6);
            pLogin->WriteBytes(&w50, 2);
            pLogin->WriteBytes(&dw54, 4);
            if (RelayMsgToBody_6008(this, pLogin, GlobalManagerID(), 0, 0, pUser->dwSessionID))
            {
                g_pBSNet->ReleaseMsg(pReply);
                return;
            }
            g_UserManager.Unregister(pUser->dwSessionID);
            WriteByte(pReply, 2);
            WriteByte(pReply, 3);
            LogPrintf(0x2000000, "You check the 'Cert', this problem is Cert's input sequence is "
                                 "wrong, check the Agent & GameServer Sequence");
        }
    }
    g_pBSNet->SendMsg(pMsg->m_dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
    g_pBSNet->DisconnectSession(pMsg->m_dwSessionID, 1);
}

// 0xA201 {DWORD, BYTE result, ...} of the GlobalManager to 0x6201 (context = session):
// result 2 {BYTE error}: 0xA103 {2, 6 / 5 for error 2 / 3}, the user is dropped. Otherwise
// {BYTE, WORD}: the login goes to the game server (0x6204); without route 0xA103 {2, 3}, the
// logout to the GlobalManager and the user is dropped.
void CAgentMainProcess::OnMsg_A201(CMsg* pMsg, DWORD, DWORD dwContext, CMassiveMsg*)
{
    DWORD dwJID;
    BYTE byResult;
    *pMsg >> dwJID >> byResult;
    sUser* pUser = g_UserManager.Find(dwContext);
    if (!pUser)
        return;
    if (byResult == 2)
    {
        BYTE byError;
        *pMsg >> byError;
        if (byError != 6)
            byError = byError == 2 ? 5 : 3;
        SendLoginFailure(pUser->dwSessionID, byError);
        g_UserManager.Unregister(pUser->dwSessionID);
        return;
    }
    *pMsg >> byResult;
    pUser->byLoginResult = byResult;
    CMsg* pLogin = g_pBSNet->AllocMsg(FALSE);
    pLogin->SetOpcode(AGENT_MSG_SHARD_LOGIN_REQ);
    pLogin->WriteBytes(&pUser->dwJID, 4);
    pLogin->WriteBytes(&pUser->dwSessionID, 4);
    pLogin->WriteBytes(&pUser->bySecPrimary, 1);
    pLogin->WriteBytes(&pUser->bySecContent, 1);
    *pLogin << pUser->strID.c_str();
    pLogin->WriteBytes(&pUser->dwIP, 4);
    pLogin->WriteBytes(&pUser->byLoginResult, 1);
    pLogin->WriteBytes(&pUser->dwMacLow, 6);
    WORD w;
    *pMsg >> w;
    pLogin->WriteBytes(&w, 2);
    if (RelayMsgToBody_6008(this, pLogin, pUser->wHomeBodyID, 0, 0, pUser->dwSessionID))
        return;
    if (!pUser->wHomeBodyID)
    {
        LogPrintf(0x2000000, "there is no assigned startup server! This may occur C10 error code! "
                             "plz contact to Joymax server license manager!");
    }
    SendLoginFailure(pUser->dwSessionID, 3);
    SendLogoutToGlobalManager(this, pUser, true);
    g_UserManager.Unregister(pUser->dwSessionID);
}

// 0xA204 {BYTE result} of the game server to 0x6204 (context = session): 0xA103 {1}, or for
// result 2 {BYTE error}: 0xA103 {2, 2 / 4 / 3 for error 2 / 3 / other}, the logout to the
// GlobalManager, the session closed and the user dropped.
void CAgentMainProcess::OnMsg_A204(CMsg* pMsg, DWORD, DWORD dwContext, CMassiveMsg*)
{
    BYTE byResult;
    *pMsg >> byResult;
    sUser* pUser = g_UserManager.Find(dwContext);
    if (!pUser)
        return;
    if (byResult != 2)
    {
        CMsg* pReply = g_pBSNet->AllocMsg(TRUE);
        pReply->SetOpcode(AGENT_MSG_LOGIN_ACK);
        WriteByte(pReply, 1);
        g_pBSNet->SendMsg(pUser->dwSessionID, pReply);
        g_pBSNet->ReleaseMsg(pReply);
        return;
    }
    *pMsg >> byResult;
    BYTE byError;
    if (byResult == 2)
        byError = 2;
    else if (byResult == 3)
        byError = 4;
    else
        byError = 3;
    SendLoginFailure(pUser->dwSessionID, byError);
    SendLogoutToGlobalManager(this, pUser, false);
    g_pBSNet->DisconnectSession(pUser->dwSessionID, 1);
    g_UserManager.Unregister(pUser->dwSessionID);
}

// 0xA202 (massive) {WORD}: the GlobalManager's answer to a logout, read and dropped.
void CAgentMainProcess::OnMsg_A202(CMsg*, DWORD, DWORD, CMassiveMsg* pMassive)
{
    WORD w;
    pMassive->Read(&w, 2);
}

// 0xA205 (nullsub_14): the game server's answer to 0x6205, ignored.
void CAgentMainProcess::OnMsg_A205(CMsg*, DWORD, DWORD, CMassiveMsg*)
{
}

// 0x6103 of a session that is gone: the logout goes to the GlobalManager and the game server.
// (OnSessionClosed above.)

// ---------------------------------------------------------------------------------------------
// GlobalManager requests

// 0x2206 {WORD n, n * DWORD session}: the sessions are closed; the logout of their JIDs (0 for
// unknown sessions) goes to the GlobalManager.
void CAgentMainProcess::OnMsg_2206(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    WORD wCount;
    *pMsg >> wCount;
    CMassiveMsg* pLogout = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pLogout->SetOpcode(AGENT_MSG_USER_LOGOUT);
    pLogout->Write(&wCount, 2);
    for (int i = 0; i < (int)wCount; ++i)
    {
        DWORD dwSessionID;
        *pMsg >> dwSessionID;
        sUser* pUser = g_UserManager.Find(dwSessionID);
        if (!pUser)
        {
            DWORD dwZero = 0;
            pLogout->Write(&dwZero, 4);
            continue;
        }
        pLogout->Write(&pUser->dwJID, 4);
        g_pBSNet->DisconnectSession(dwSessionID, 1);
        g_UserManager.Unregister(dwSessionID);
    }
    RelayMassiveMsgToBody_6008(pLogout, this, GlobalManagerID(), 0, 0);
}

// 0x6207 {DWORD session, DWORD JID, WORD body} (relayed): moves the user's packets to another
// body after that body agreed (0x6208 {JID, session}, answer 0xA208). 0xA207 {JID, 2, error}:
// 1 no user, 5 other JID, 3 the body is the current one, 2 no route to the body.
void CAgentMainProcess::OnMsg_6207(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    DWORD dwUserSession;
    DWORD dwJID;
    WORD wBodyID;
    *pMsg >> dwUserSession >> dwJID >> wBodyID;
    sUser* pUser = g_UserManager.Find(dwUserSession);
    if (!pUser)
    {
        SendRelayReply(pMsg->m_dwSessionID, dwRelayID, NewMoveFailure(dwJID, 1));
        return;
    }
    if (pUser->dwJID != dwJID)
    {
        SendRelayReply(pMsg->m_dwSessionID, dwRelayID, NewMoveFailure(dwJID, 5));
        return;
    }
    if (pUser->wTargetBodyID == wBodyID)
    {
        SendRelayReply(pMsg->m_dwSessionID, dwRelayID, NewMoveFailure(dwJID, 3));
        return;
    }
    CMsg* pMove = g_pBSNet->AllocMsg(FALSE);
    pMove->SetOpcode(AGENT_MSG_MOVE_TARGET_REQ);
    pMove->WriteBytes(&pUser->dwJID, 4);
    pMove->WriteBytes(&pUser->dwSessionID, 4);
    AGENT_MOVE_REQUEST* pRequest = new AGENT_MOVE_REQUEST;
    pRequest->dwSessionID = pMsg->m_dwSessionID;
    pRequest->dwUserSession = dwUserSession;
    pRequest->wBodyID = wBodyID;
    pRequest->dwJID = dwJID;
    pRequest->dwRelayID = dwRelayID;
    if (RelayMsgToBody_6008(this, pMove, wBodyID, 0, 0, (DWORD)pRequest))
        return;
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, NewMoveFailure(dwJID, 2));
    delete pRequest;
}

// 0xA208 {BYTE result} of the new body (context = AGENT_MOVE_REQUEST): 0xA207 {JID, 1} and the
// user's packets go to the new body, or {JID, 2, 1} without user / {JID, 2, 4} when refused.
void CAgentMainProcess::OnMsg_A208(CMsg* pMsg, DWORD, DWORD dwContext, CMassiveMsg*)
{
    BYTE byResult;
    *pMsg >> byResult;
    AGENT_MOVE_REQUEST* pRequest = (AGENT_MOVE_REQUEST*)dwContext;
    sUser* pUser = g_UserManager.Find(pRequest->dwUserSession);
    CMsg* pReply;
    if (!pUser)
    {
        pReply = NewMoveFailure(pRequest->dwJID, 1);
    }
    else if (byResult != 1)
    {
        pReply = NewMoveFailure(pRequest->dwJID, 4);
    }
    else
    {
        pUser->wTargetBodyID = pRequest->wBodyID;
        pReply = g_pBSNet->AllocMsg(FALSE);
        pReply->SetOpcode(AGENT_MSG_MOVE_USER_ACK);
        pReply->WriteBytes(&pRequest->dwJID, 4);
        WriteByte(pReply, 1);
    }
    SendRelayReply(pRequest->dwSessionID, pRequest->dwRelayID, pReply);
    delete pRequest;
}

// 0x620D {DWORD JID} (relayed): closes the user's session; 0xA20D {1}, or {2, DWORD JID}
// without user.
void CAgentMainProcess::OnMsg_620D(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    DWORD dwJID;
    *pMsg >> dwJID;
    sUser* pUser = g_UserManager.FindByJID(dwJID);
    CMsg* pReply;
    if (!pUser)
    {
        pReply = g_pBSNet->AllocMsg(FALSE);
        pReply->SetOpcode(AGENT_MSG_BAN_USER_ACK);
        WriteByte(pReply, 2);
        pReply->WriteBytes(&dwJID, 4);
    }
    else
    {
        g_pBSNet->DisconnectSession(pUser->dwSessionID, 1);
        LogPrintf(0, "ban user from division manager : %s", pUser->strID.c_str());
        pReply = g_pBSNet->AllocMsg(FALSE);
        pReply->SetOpcode(AGENT_MSG_BAN_USER_ACK);
        WriteByte(pReply, 1);
    }
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}

// 0x6902 {DWORD JID, BYTE, BYTE, DWORD} (relayed): passed to the user's client as 0x6902
// {BYTE, BYTE, DWORD}; 0xA902 {1} in any case.
void CAgentMainProcess::OnMsg_6902(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    DWORD dwJID;
    BYTE b1;
    BYTE b2;
    DWORD dw;
    *pMsg >> dwJID >> b1 >> b2 >> dw;
    sUser* pUser = g_UserManager.FindByJID(dwJID);
    if (pUser)
    {
        CMsg* pNotice = g_pBSNet->AllocMsg(TRUE);
        pNotice->SetOpcode(AGENT_MSG_BILLING_NOTICE);
        pNotice->WriteBytes(&b1, 1);
        pNotice->WriteBytes(&b2, 1);
        pNotice->WriteBytes(&dw, 4);
        g_pBSNet->SendMsg(pUser->dwSessionID, pNotice);
        g_pBSNet->ReleaseMsg(pNotice);
    }
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(AGENT_MSG_BILLING_NOTICE_ACK);
    WriteByte(pReply, 1);
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}

// 0x6905 {DWORD JID} (relayed): like 0x620D for the billing; 0xA905 {1} or {2}.
void CAgentMainProcess::OnMsg_6905(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    DWORD dwJID;
    *pMsg >> dwJID;
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(AGENT_MSG_BILLING_BAN_ACK);
    sUser* pUser = g_UserManager.FindByJID(dwJID);
    if (!pUser)
    {
        WriteByte(pReply, 2);
    }
    else
    {
        g_pBSNet->DisconnectSession(pUser->dwSessionID, 1);
        LogPrintf(0, "ban user from division manager<billing> : %s", pUser->strID.c_str());
        WriteByte(pReply, 1);
    }
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}

// ---------------------------------------------------------------------------------------------
// Relays to clients

// 0x2209 {DWORD session, WORD opcode, data}: the data goes to the session with the opcode. A
// short packet is skipped.
// BUG(AS-003): the byte count is read from the relay message after it was released.
void CAgentMainProcess::OnMsg_2209(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwSessionID;
    WORD wRealMsgID;
    try
    {
        *pMsg >> dwSessionID >> wRealMsgID;
    }
    catch (...)
    {
        pMsg->SkipRemaining();
        return;
    }
    CMsg* pRelay = g_pBSNet->AllocMsg(FALSE);
    pRelay->SetOpcode(wRealMsgID);
    pRelay->WriteBytes(pMsg->m_pData + pMsg->m_wReadPos,
                       (WORD)(pMsg->m_wWritePos - pMsg->m_wReadPos));
    g_pBSNet->SendMsg(dwSessionID, pRelay);
    g_pBSNet->ReleaseMsg(pRelay);
    pMsg->SkipRemaining();
    g_dwAgentMsgSentBytes += (WORD)((*pRelay->m_pSize & MSG_SIZE_MASK) + MSG_HEADER_SIZE);
    ++g_dwAgentMsgSentCount;
}

// 0x220A {data, n * DWORD session, WORD n, WORD opcode}: the message itself, cut to the data and
// given the opcode, goes to each session (the list is walked from its end).
// BUG(AS-002): a session ID 0 does not advance the list: every following entry is that 0 again
// (logged), the remaining sessions get nothing.
void CAgentMainProcess::OnMsg_220A(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    WORD wRealMsgID =
        *(WORD*)pMsg->PtrAt((WORD)((*pMsg->m_pSize & MSG_SIZE_MASK) + MSG_HEADER_SIZE) - 2);
    WORD wCount =
        *(WORD*)pMsg->PtrAt((WORD)((*pMsg->m_pSize & MSG_SIZE_MASK) + MSG_HEADER_SIZE) - 4);
    DWORD* pdwSession =
        (DWORD*)pMsg->PtrAt((WORD)((*pMsg->m_pSize & MSG_SIZE_MASK) + MSG_HEADER_SIZE) - 8);
    WORD wSize = (WORD)((*pMsg->m_pSize & MSG_SIZE_MASK) - 4 * wCount - 4);
    pMsg->SetOpcode(wRealMsgID);
    pMsg->SetPayloadSize(wSize);
    if (wSize + MSG_HEADER_SIZE <= pMsg->m_nCapacity)
        pMsg->m_wWritePos = (WORD)(wSize + MSG_HEADER_SIZE);
    pMsg->m_wReadPos = pMsg->m_wWritePos;
    for (int n = wCount; n > 0; --n)
    {
        if (!*pdwSession)
        {
            LogPrintf(0x2000000,
                      "there is invalid session id at relay msg (multi list) : real_msg_id[0x%X]",
                      wRealMsgID);
            continue;
        }
        g_pBSNet->SendMsg(*pdwSession, pMsg);
        ++g_dwAgentMsgSentCount;
        g_dwAgentMsgSentBytes += (WORD)((*pMsg->m_pSize & MSG_SIZE_MASK) + MSG_HEADER_SIZE);
        --pdwSession;
    }
}

// 0x220E {data}: the data goes, with the opcode collected by 0x220F, to the sessions collected
// for this server session; then the collection is reset.
void CAgentMainProcess::OnMsg_220E(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    AgentBroadcastMap::iterator it = m_mapBroadcast.find(pMsg->m_dwSessionID);
    if (it == m_mapBroadcast.end())
    {
        LogPrintf(0x2000000, "RELAY_MSG_TO_CLIENT_MULTI_DATA : there is no broadcast object");
        pMsg->SkipRemaining();
        return;
    }
    AGENT_BROADCAST* pBroadcast = it->second;
    if (!pBroadcast->wRealMsgID || !pBroadcast->vecTarget.size())
    {
        LogPrintf(0x2000000,
                  "RELAY_MSG_TO_CLIENT_MULTI_DATA : realmsgid is null or target is empty");
        pMsg->SkipRemaining();
        return;
    }
    pMsg->SetOpcode(pBroadcast->wRealMsgID);
    for (int i = 0; i < (int)pBroadcast->vecTarget.size(); ++i)
    {
        g_pBSNet->SendMsg(pBroadcast->vecTarget[i], pMsg);
        ++g_dwAgentMsgSentCount;
        g_dwAgentMsgSentBytes += (WORD)((*pMsg->m_pSize & MSG_SIZE_MASK) + MSG_HEADER_SIZE);
    }
    pBroadcast->wRealMsgID = 0;
    pBroadcast->vecTarget.clear();
}

// 0x220F {WORD opcode, WORD n, n * DWORD session}: collects targets for the next 0x220E of this
// server session; refused (logged) while targets of another opcode are collected.
void CAgentMainProcess::OnMsg_220F(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    WORD wRealMsgID;
    *pMsg >> wRealMsgID;
    AGENT_BROADCAST* pBroadcast;
    AgentBroadcastMap::iterator it = m_mapBroadcast.find(pMsg->m_dwSessionID);
    if (it == m_mapBroadcast.end())
    {
        pBroadcast = new AGENT_BROADCAST;
        pBroadcast->wRealMsgID = 0;
        m_mapBroadcast.insert(AgentBroadcastMap::value_type(pMsg->m_dwSessionID, pBroadcast));
    }
    else
    {
        pBroadcast = it->second;
    }
    WORD wCurrent = pBroadcast->wRealMsgID;
    if (wCurrent && wCurrent != wRealMsgID)
    {
        LogPrintf(0x2000000,
                  "RELAY_MSG_TO_CLIENT_MULTI_LIST failed : realmsgid is different [0x%X] - "
                  "[0x%X]",
                  wRealMsgID, wCurrent);
        return;
    }
    pBroadcast->wRealMsgID = wRealMsgID;
    WORD wCount;
    *pMsg >> wCount;
    for (int i = 0; i < (int)wCount; ++i)
    {
        DWORD dwSessionID;
        *pMsg >> dwSessionID;
        pBroadcast->vecTarget.push_back(dwSessionID);
    }
}

// ---------------------------------------------------------------------------------------------
// XTrap

// 0x2113 {BYTE type, ...} from a user: type 2 {BYTE step, 1024 bytes}: step 2 goes to
// XTrap_CS_Step3. Other types are logged ("wrong packet sent!!").
void CAgentMainProcess::OnMsg_2113(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    sUser* pUser = g_UserManager.Find(pMsg->m_dwSessionID);
    AGENT_XTRAP_PACKET answer;
    answer.abData[0] = 0;
    memset(answer.abData + 1, 0, XTRAP_PACKET_SIZE);
    if (!pUser)
        return;
    BYTE byType = 0;
    *pMsg >> byType;
    if (byType != 2)
    {
        // "[For Xtrap] wrong packet sent!!" (Korean, code page 949)
        LogPrintf(0x2000000, "[For Xtrap] \xc0\xdf\xb8\xf8\xb5\xc8 \xc6\xd0\xc5\xb6 "
                             "\xc0\xfc\xbc\xdb!!");
        return;
    }
    *pMsg >> answer;
    if (answer.abData[0] == 2)
        g_pfnXTrap_CS_Step3(pUser->abXTrapSession, answer.abData + 1);
}
