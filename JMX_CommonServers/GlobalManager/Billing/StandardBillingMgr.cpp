#include "StandardBillingMgr.h"

#include "GlobalManager/ContentUser.h"
#include "BSLib/BSDebug.h"
#include "BSNet/IBSNet.h"
#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerProcess.h"

using namespace ServerFramework;

static const char s_szStandardBillingFile[] =
    "D:\\WORK2005\\Source\\JMX_CommonServers\\GlobalManager\\Billing\\StandardBillingMgr.cpp";

// 0x52D1EC (CP949): the user of the billing answer is not logged in.
static const char s_szNoUser[] =
    "\xbe\xc6~~ \xc5\xab\xc0\xcf\xc0\xcc\xb4\xd9.. OverlapJob \xc1\xa4\xba\xb8\xb0\xa1"
    " \xbe\xf8\xb4\xd9.. \xa4\xd0\xa4\xd0";

static void WriteByte(CMsg* pMsg, BYTE b)
{
    pMsg->WriteBytes(&b, 1);
}

static CERT_SHARD* FindCertShard(WORD wShardID)
{
    std::map<WORD, CERT_SHARD*>::iterator it = g_mapCertShard.find(wShardID);
    if (it == g_mapCertShard.end())
        return NULL;
    return it->second;
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

// Main process messages are read again from the start of the data.
static void RewindMsg(CMsg* pMsg)
{
    if (pMsg->m_nCapacity >= 6)
        pMsg->m_wReadPos = 6;
}

// ---------------------------------------------------------------------------------------------
// Construction

CStandardBillingMgr::CStandardBillingMgr(CServerProcessBase* pOwner)
    : CBaseBillingMgr(pOwner)
{
    m_bNoActiveUserList = FALSE;
    CStandardBillingMgr::InitHandlers();
}

CStandardBillingMgr::~CStandardBillingMgr()
{
}

void CStandardBillingMgr::InitHandlers()
{
    CBaseBillingMgr::InitHandlers();
    SetHandler(0x4001, static_cast<PFN_BILLING_HANDLER>(&CStandardBillingMgr::OnCertifyAck));
    SetHandler(0x4002, static_cast<PFN_BILLING_HANDLER>(&CStandardBillingMgr::OnLoginAck));
    SetHandler(0x4003, static_cast<PFN_BILLING_HANDLER>(&CStandardBillingMgr::OnHeartBeatAck));
    SetHandler(0x8004, static_cast<PFN_BILLING_HANDLER>(&CStandardBillingMgr::On8004));
    SetHandler(0x8005, static_cast<PFN_BILLING_HANDLER>(&CStandardBillingMgr::On8005));
    SetHandler(0x4006, static_cast<PFN_BILLING_HANDLER>(&CStandardBillingMgr::On4006));
    SetHandler(0x8009, static_cast<PFN_BILLING_HANDLER>(&CStandardBillingMgr::On8009));
    SetHandler(0x200B,
               static_cast<PFN_BILLING_HANDLER>(&CStandardBillingMgr::OnOnlineStatusReq));
}

// ---------------------------------------------------------------------------------------------
// IBillingMgr

BOOL CStandardBillingMgr::Initialize(const char* pszAddress, WORD wPort, WORD wPoolSize)
{
    if (!CBaseBillingMgr::Initialize(pszAddress, wPort, wPoolSize))
    {
        BillingMgr_Log(0x2000000, "Failed To Initialize!");
        return FALSE;
    }
    m_dwTimerIndex[eHeartBeatTimer] = CreateTimer(30000);
    m_dwTimerIndex[eHeartBeatAckTimer] = CreateTimer(3000);
    m_dwTimerIndex[eReconnectTimer] = CreateTimer(10000);
    if (m_dwTimerIndex[eHeartBeatTimer] == (DWORD)-1)
        BSAssertFail();
    if (m_dwTimerIndex[eHeartBeatAckTimer] == (DWORD)-1)
        BSAssertFail();
    if (m_dwTimerIndex[eReconnectTimer] == (DWORD)-1)
        BSAssertFail();
    return TRUE;
}

void CStandardBillingMgr::Cleanup()
{
    CBaseBillingMgr::Cleanup();
}

BOOL CStandardBillingMgr::Start()
{
    if (!m_pEventNotifier)
        BSAssertFail();
    if (!m_pEventNotifier->ResumeAll())
    {
        BillingMgr_Log(0x2000000, "Failed m_pEventNotifier->ResumeTask()!");
        return FALSE;
    }
    if (!ActivateNetwork())
    {
        BillingMgr_Log(0x2000000, "Failed ActivateNetwork()!");
        return FALSE;
    }
    return TRUE;
}

void CStandardBillingMgr::Stop()
{
    InterlockedExchange(&m_nState, BILLING_STATE_STOPPING);
    DeactivateTimer(m_dwTimerIndex[eHeartBeatTimer]);
    DeactivateTimer(m_dwTimerIndex[eHeartBeatAckTimer]);
    DeactivateTimer(m_dwTimerIndex[eReconnectTimer]);
    CloseSession();
    if (!m_pEventNotifier)
        BSAssertFail();
    m_pEventNotifier->SuspendAll();
    InterlockedExchange(&m_nState, BILLING_STATE_IDLE);
}

void CStandardBillingMgr::Release()
{
    delete this;
}

int CStandardBillingMgr::ProcessMsg(CMsg* pMsg, DWORD dwSessionID, DWORD dwRelayID)
{
    switch (pMsg->GetOpcode())
    {
    case 0x620C:
        RefreshActiveUserList();
        return 2;
    case 0x6201:
        OnUserLogin(pMsg, dwSessionID, dwRelayID);
        return 2;
    case 0x6202:
        OnUserLogout(pMsg, dwSessionID, dwRelayID);
        return 2;
    case 0x6900:
        On6900(pMsg, dwSessionID, dwRelayID);
        return 2;
    case 0x6904:
        On6904(pMsg, dwSessionID, dwRelayID);
        return 2;
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Connection

void CStandardBillingMgr::OnTimer(DWORD dwIndex)
{
    if (dwIndex == eHeartBeatTimer)
    {
        SendHeartBeat();
    }
    else if (dwIndex == eHeartBeatAckTimer)
    {
        BillingMgr_Log(0x1000000, "Heartbeat Timeout!");
        DeactivateTimer(m_dwTimerIndex[eHeartBeatTimer]);
        DeactivateTimer(m_dwTimerIndex[eHeartBeatAckTimer]);
        // BUG(GM-017): CloseSession only disconnects while stopping, so the dead session stays
        // and the reconnect timer finds the manager still activated.
        CloseSession();
        if (m_nState != BILLING_STATE_STOPPING)
            ActivateTimer(m_dwTimerIndex[eReconnectTimer]);
    }
    else if (dwIndex == eReconnectTimer)
    {
        if (m_nState == BILLING_STATE_IDLE && !ActivateNetwork()
            && !BSAssertDialog(155, s_szStandardBillingFile, "ActivateNetwork()"))
        {
            DebugBreak();
        }
    }
}

void CStandardBillingMgr::OnConnected(DWORD, DWORD)
{
    DeactivateTimer(m_dwTimerIndex[eReconnectTimer]);
    InterlockedExchange(&m_nState, BILLING_STATE_CONNECTED);
    SendCertifyReq();
    BillingMgr_Log(0, "Session Established!");
}

void CStandardBillingMgr::OnDisconnected(DWORD, DWORD)
{
    if (m_nState == BILLING_STATE_ACTIVATED || m_nState == BILLING_STATE_CONNECTED)
        BillingMgr_Log(0x2000000, "Billing Server Session Closed!");
    else
        BillingMgr_Log(0x2000000, "Failed To Connect Billing Server!");
    if (m_nState != BILLING_STATE_STOPPING)
    {
        InterlockedExchange(&m_nState, BILLING_STATE_IDLE);
        ActivateTimer(m_dwTimerIndex[eReconnectTimer]);
        DeactivateTimer(m_dwTimerIndex[eHeartBeatTimer]);
    }
}

void CStandardBillingMgr::SendPacket(CBillingPacket* pPacket)
{
    if (!m_pINet)
        BSAssertFail();
    if (m_dwUnit == (DWORD)-1)
        BSAssertFail();
    if (m_dwSession == (DWORD)-1)
        BSAssertFail();
    if (m_nState != BILLING_STATE_ACTIVATED && m_nState != BILLING_STATE_CONNECTED)
        return;
    if (!m_pINet->Send(m_dwUnit, m_dwSession, pPacket->m_abData + 2, *pPacket->m_pwSize + 2))
        BillingMgr_Log(0x1000000, "SendBillingPacket Failed! Check Connection.");
}

void CStandardBillingMgr::SendCertifyReq()
{
    CBillingPacket packet;
    packet.SetOpcode(0x2001);
    DWORD dwVersion = 103;
    packet.Write(&dwVersion, 4);
    SendPacket(&packet);
    OutputDebugStringA("SBM: STD_BILLING_CERTIFY_BILLING_CLIENT_REQ Sent\n");
}

void CStandardBillingMgr::SendHeartBeat()
{
    CBillingPacket packet;
    packet.SetOpcode(0x2003);
    BYTE byZero = 0;
    packet.Write(&byZero, 1);
    SendPacket(&packet);
    if (!m_pITimer->ActivateTimer(m_dwTimerIndex[eHeartBeatAckTimer])
        && !BSAssertDialog(705, s_szStandardBillingFile,
                           "m_pITimer->ActivateTimer(m_dwTimerIndex[eHeartBeatAckTimer])"))
    {
        DebugBreak();
    }
}

// ---------------------------------------------------------------------------------------------
// Billing server packets

void CStandardBillingMgr::OnCertifyAck(CBillingPacket* pPacket)
{
    BYTE byResult;
    pPacket->Read(byResult);
    if (byResult)
    {
        pPacket->Skip(1);
        Stop();
        __debugbreak();
        return;
    }
    OutputDebugStringA("SBM: STD_BILLING_CERTIFY_BILLING_CLIENT_ACK Received");
    BYTE byNoList;
    pPacket->Read(byNoList);
    m_bNoActiveUserList = byNoList == 1;
    InterlockedExchange(&m_nState, BILLING_STATE_ACTIVATED);
    BillingMgr_Log(0, "Service Activated");
    RefreshActiveUserList();
    SendHeartBeat();
    if (!ActivateTimer(m_dwTimerIndex[eHeartBeatTimer])
        && !BSAssertDialog(283, s_szStandardBillingFile,
                           "ActivateTimer(m_dwTimerIndex[eHeartBeatTimer])"))
    {
        DebugBreak();
    }
}

void CStandardBillingMgr::OnLoginAck(CBillingPacket* pPacket)
{
    DWORD dwJID;
    BYTE byResult;
    pPacket->Read(dwJID);
    pPacket->Read(byResult);
    // BUG(GM-018): the answer is not released when the user is gone.
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(0xA201);
    sContentUser* pUser = g_ContentUserManager.Find(dwJID);
    if (!pUser)
    {
        BillingMgr_Log(0x2000000, s_szNoUser);
        return;
    }
    if (byResult)
    {
        pPacket->Skip(1);
        pMsg->WriteBytes(&dwJID, 4);
        WriteByte(pMsg, 2);
        WriteByte(pMsg, 6);
        // The answer below reads the session and relay of the released user.
        g_ContentUserManager.Unregister(dwJID, NULL);
    }
    else
    {
        pUser->b5D = 1;
        pMsg->WriteBytes(&dwJID, 4);
        WriteByte(pMsg, 1);
        pMsg->WriteBytes(&pUser->byIPType, 1);
        DWORD dwPlayTime = pUser->PlayTime.wPlayTime;
        pMsg->WriteBytes(&dwPlayTime, 2);
    }
    m_pOwner->SendRelayReply(pUser->dwSessionID, pUser->dwRelayID, pMsg);
}

void CStandardBillingMgr::OnHeartBeatAck(CBillingPacket*)
{
    m_pITimer->DeactivateTimer(m_dwTimerIndex[eHeartBeatAckTimer]);
}

void CStandardBillingMgr::On8004(CBillingPacket* pPacket)
{
    DWORD dwJID;
    BYTE by;
    DWORD dw;
    pPacket->Read(dwJID);
    pPacket->Read(by);
    pPacket->Read(dw);
    sContentUser* pUser = g_ContentUserManager.Find(dwJID);
    if (!pUser)
        return;
    pUser->b5C = by;
    pUser->dw60 = dw;
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(0x6902);
    pMsg->WriteBytes(&dwJID, 4);
    WriteByte(pMsg, 2);
    pMsg->WriteBytes(&by, 1);
    pMsg->WriteBytes(&dw, 4);
    RelayMsgToBody_6008(m_pOwner, pMsg, pUser->wAgentBodyID, 0, 0, 0);
}

void CStandardBillingMgr::On8005(CBillingPacket* pPacket)
{
    DWORD dwJID;
    BYTE by;
    DWORD dw;
    pPacket->Read(dwJID);
    pPacket->Read(by);
    pPacket->Read(dw);
    sContentUser* pUser = g_ContentUserManager.Find(dwJID);
    if (!pUser)
        return;
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(0x6902);
    pMsg->WriteBytes(&dwJID, 4);
    WriteByte(pMsg, 1);
    pMsg->WriteBytes(&by, 1);
    pMsg->WriteBytes(&dw, 4);
    RelayMsgToBody_6008(m_pOwner, pMsg, pUser->wAgentBodyID, 0, 0, 0);
}

void CStandardBillingMgr::On4006(CBillingPacket* pPacket)
{
    BYTE byType2 = 0;
    BYTE byType1 = 0;
    DWORD dwValue = 0;
    DWORD dwJID;
    BYTE byFlag;
    pPacket->Read(dwJID);
    pPacket->Read(byFlag);
    // BUG(GM-018): the answer is not released when the user is gone.
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(0xA900);
    pMsg->WriteBytes(&dwJID, 4);
    pMsg->WriteBytes(&byFlag, 1);
    sContentUser* pUser = g_ContentUserManager.Find(dwJID);
    if (!pUser)
    {
        BillingMgr_Log(0x2000000, s_szNoUser);
        return;
    }
    BYTE byExtra = 0;
    if (byFlag)
    {
        BYTE b;
        pPacket->Read(b);
        pMsg->WriteBytes(&b, 1);
    }
    else
    {
        pPacket->Read(byType1);
        pPacket->Read(byType2);
        pPacket->Read(dwValue);
        pPacket->Read(byExtra);
        DWORD dwExtra = byExtra;
        pMsg->WriteBytes(&byType1, 1);
        pMsg->WriteBytes(&byType2, 1);
        pMsg->WriteBytes(&dwValue, 4);
        pMsg->WriteBytes(&dwExtra, 4);
    }
    m_pOwner->SendRelayReply(pUser->dwSessionID, pUser->dwRelayID, pMsg);
    if (byFlag)
    {
        CMsg* pRelay = g_pBSNet->AllocMsg(FALSE);
        pRelay->SetOpcode(0x6905);
        pRelay->WriteBytes(&dwJID, 4);
        RelayMsgToBody_6008(m_pOwner, pRelay, pUser->wAgentBodyID, 0, 0, 0);
        return;
    }
    if (byType1 != 1)
    {
        CMsg* pRelay = g_pBSNet->AllocMsg(FALSE);
        pRelay->SetOpcode(0x6902);
        pRelay->WriteBytes(&dwJID, 4);
        WriteByte(pRelay, 0);
        pRelay->WriteBytes(&byType2, 1);
        pRelay->WriteBytes(&dwValue, 4);
        RelayMsgToBody_6008(m_pOwner, pRelay, pUser->wAgentBodyID, 0, 0, 0);
    }
    pUser->b5C = byType2;
    pUser->dw60 = dwValue;
    pUser->dwTick68 = GetTickCount();
    pUser->dw64 = byExtra;
    pUser->b5D = 2;
    if (!byExtra)
        return;
    CERT_SHARD* pShard = FindCertShard(pUser->wShardID);
    if (!pShard)
        return;
    CMsg* pRelay = g_pBSNet->AllocMsg(FALSE);
    pRelay->SetOpcode(0x6903);
    DWORD dwExtra = byExtra;
    pRelay->WriteBytes(&dwJID, 4);
    pRelay->WriteBytes(&dwExtra, 4);
    RelayMsgToBody_6008(m_pOwner, pRelay, pShard->wManagerID, 0, 0, 0);
}

void CStandardBillingMgr::On8009(CBillingPacket* pPacket)
{
    DWORD dwJID;
    pPacket->Read(dwJID);
    pPacket->Skip(1);
    sContentUser* pUser = g_ContentUserManager.Find(dwJID);
    if (!pUser)
        return;
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(0x6905);
    pMsg->WriteBytes(&dwJID, 4);
    RelayMsgToBody_6008(m_pOwner, pMsg, pUser->wAgentBodyID, 0, 0, 0);
}

void CStandardBillingMgr::OnOnlineStatusReq(CBillingPacket* pPacket)
{
    DWORD dwJID;
    pPacket->Read(dwJID);
    CBillingPacket answer;
    answer.SetOpcode(0x400B);
    answer.Write(&dwJID, 4);
    sContentUser* pUser = g_ContentUserManager.Find(dwJID);
    if (pUser)
    {
        DWORD dwContent = pUser->byContentID;
        DWORD dwShard = pUser->wShardID;
        BYTE byResult = 0;
        answer.Write(&byResult, 1);
        answer.Write(&pUser->dwIP, 4);
        answer.Write(&dwContent, 4);
        answer.Write(&dwShard, 4);
        answer.WriteString(pUser->strMAC);
    }
    else
    {
        BYTE byResult = 1;
        answer.Write(&byResult, 1);
    }
    SendPacket(&answer);
    OutputDebugStringA("SBM: STD_BILLING_ONLINE_STATUS_ACK Sent");
}

void CStandardBillingMgr::RefreshActiveUserList()
{
    if (m_bNoActiveUserList)
        return;
    CMassiveMsg* pList = CMassiveMsg::Alloc(1);
    g_ContentUserManager.WriteBillingUserList(pList);
    pList->BeginRead();
    WORD wLeft = 0;
    pList->Read(&wLeft, 2);
    if (wLeft)
    {
        CBillingPacket packet;
        std::string strMAC;
        do
        {
            WORD wCount = wLeft >= 90 ? 90 : wLeft;
            packet.Reset();
            packet.SetOpcode(0x8007);
            packet.Write(&wCount, 2);
            for (WORD i = 0; i < wCount; ++i)
            {
                DWORD dwIP, dwJID, dwContent, dwShard, dwValue, dwTime;
                BYTE by;
                pList->Read(&dwIP, 4);
                pList->Read(&dwJID, 4);
                pList->Read(&dwContent, 4);
                pList->Read(&dwShard, 4);
                pList->Read(&by, 1);
                pList->Read(&dwValue, 4);
                pList->Read(&dwTime, 4);
                ReadMassiveString(pList, strMAC);
                packet.Write(&dwJID, 4);
                packet.Write(&dwIP, 4);
                packet.Write(&dwContent, 4);
                packet.Write(&dwShard, 4);
                packet.Write(&by, 1);
                packet.Write(&dwValue, 4);
                packet.Write(&dwTime, 4);
                packet.WriteString(strMAC.c_str());
            }
            SendPacket(&packet);
            BillingMgr_Log(0, "RefreshActiveUserList Sent!! [Data Size: %dBytes]",
                           *packet.m_pwSize);
            wLeft = (WORD)(wLeft - wCount);
        }
        while (wLeft);
    }
    pList->Free();
    OutputDebugStringA("SBM: STD_BILLING_REFRESH_ACTIVE_USER_LIST Sent");
}

// ---------------------------------------------------------------------------------------------
// Main process messages

void CStandardBillingMgr::OnUserLogin(CMsg* pMsg, DWORD, DWORD)
{
    RewindMsg(pMsg);
    std::string strID;
    std::string strMAC;
    DWORD dwFirst, dwSecond;
    BYTE by;
    WORD w1, w2;
    *pMsg >> dwFirst;
    *pMsg >> dwSecond;
    *pMsg >> strID;
    *pMsg >> by;
    *pMsg >> w1;
    *pMsg >> w2;
    *pMsg >> strMAC;
    CBillingPacket packet;
    packet.SetOpcode(0x2002);
    DWORD dwWord = w1;
    DWORD dwByte = by;
    packet.Write(&dwSecond, 4);
    packet.Write(&dwFirst, 4);
    packet.Write(&dwByte, 4);
    packet.Write(&dwWord, 4);
    packet.WriteString(strMAC.c_str());
    SendPacket(&packet);
}

void CStandardBillingMgr::On6900(CMsg* pMsg, DWORD, DWORD)
{
    RewindMsg(pMsg);
    DWORD dwJID;
    *pMsg >> dwJID;
    if (!g_ContentUserManager.Find(dwJID))
        return;
    CBillingPacket packet;
    packet.SetOpcode(0x2006);
    packet.Write(&dwJID, 4);
    SendPacket(&packet);
}

void CStandardBillingMgr::On6904(CMsg* pMsg, DWORD, DWORD)
{
    DWORD dw1, dw2, dw3;
    *pMsg >> dw1;
    *pMsg >> dw2;
    *pMsg >> dw3;
    CBillingPacket packet;
    packet.SetOpcode(0x800A);
    BYTE by = (BYTE)dw2;
    packet.Write(&dw1, 4);
    packet.Write(&by, 1);
    packet.Write(&dw3, 4);
    SendPacket(&packet);
}

void CStandardBillingMgr::OnUserLogout(CMsg* pMsg, DWORD, DWORD)
{
    DWORD dwType = 0;
    *pMsg >> *(BYTE*)&dwType;
    CBillingPacket packet;
    packet.SetOpcode(0x8008);
    packet.Write(&dwType, 1);
    if ((BYTE)dwType == 2)
    {
        DWORD dwCount = 0;
        *pMsg >> *(WORD*)&dwCount;
        if ((WORD)dwCount > 1000)
            BSAssertFail();
        packet.Write(&dwCount, 2);
        WORD wCount = (WORD)dwCount;
        for (WORD i = 0; i < wCount; ++i)
        {
            DWORD dwJID;
            *pMsg >> dwJID;
            packet.Write(&dwJID, 4);
        }
    }
    else
    {
        DWORD dwJID;
        *pMsg >> dwJID;
        packet.Write(&dwJID, 4);
    }
    SendPacket(&packet);
}
