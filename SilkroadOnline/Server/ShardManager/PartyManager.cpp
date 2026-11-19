#include "ShardManager/PartyManager.h"

#include <algorithm>

#include "BSNet/NetEngine.h"

CPartyManager* g_pPartyManager = NULL;

namespace
{

void SendAndRelease(DWORD dwSessionID, CMsg* pMsg)
{
    g_pNetEngine->SendMsg(dwSessionID, pMsg);
    g_pNetEngine->ReleaseMsg(pMsg);
}

// The party fields of a member (inlined sPartyMemberInfo reset of Party::Clear and
// Party::RemoveMemberAndNotify; the guild name is kept).
void PartyMemberInfo_Reset(sPartyMemberInfo* pInfo)
{
    pInfo->m_strName.clear();
    pInfo->m_dwJID = 0;
    pInfo->m_dwRefObjID = 0;
    pInfo->m_byLevel = 0;
    pInfo->m_by42 = 0;
    pInfo->m_by42Hi = 0;
    pInfo->m_wRegion = 0;
    pInfo->m_dwPosX = 0;
    pInfo->m_dwPosY = 0;
    pInfo->m_dwPosZ = 0;
    pInfo->m_dwPartyID = 0;
    pInfo->m_byPartyType = 0;
    pInfo->m_dw54 = 0;
    pInfo->m_dw58 = 0;
    pInfo->m_dw5C = 0;
}

// 0x3864 subtype 17 (result of a party matching join) to one user.
void SendMatchJoinResult(DWORD dwSessionID, DWORD dwJID, BYTE byKind, WORD wCode)
{
    CMsg* pMsg = CPartyManager::MakeNotify3864(17);
    if (!pMsg)
        BSAssertFail();
    *pMsg << dwJID << byKind << wCode;
    SendAndRelease(dwSessionID, pMsg);
}

}

// ---------------------------------------------------------------------------------------------
// helpers

BOOL Party_IsCompatibleType(BYTE byType, BYTE byOther)
{
    switch (byType)
    {
    case 1:
    case 3:
        return byOther == 3 || byOther == 1;
    case 2:
        return byOther == 2;
    case 4:
        return byOther == 4;
    default:
        return FALSE;
    }
}

BOOL PartyMatch_IsJoinAllowed(BYTE byType, BYTE byPurpose)
{
    switch (byType)
    {
    case 1:
    case 3:
        return byPurpose == 2;
    case 2:
        return byPurpose == 3;
    case 4:
        return byPurpose != 2 && byPurpose != 3;
    default:
        BSAssertFail();
        return FALSE;
    }
}

void PartyMemberInfo_Copy(sPartyMemberInfo* pDest, const sPartyMemberInfo* pSrc)
{
    pDest->m_dwJID = pSrc->m_dwJID;
    pDest->m_strName.assign(pSrc->m_strName, 0, std::string::npos);
    pDest->m_strGuildName.assign(pSrc->m_strGuildName, 0, std::string::npos);
    pDest->m_dwRefObjID = pSrc->m_dwRefObjID;
    pDest->m_byLevel = pSrc->m_byLevel;
    pDest->m_byPartyType = pSrc->m_byPartyType;
    pDest->m_by42 = pSrc->m_by42;
    pDest->m_by42Hi = pSrc->m_by42Hi;
    pDest->m_wRegion = pSrc->m_wRegion;
    pDest->m_dwPosX = pSrc->m_dwPosX;
    pDest->m_dwPosY = pSrc->m_dwPosY;
    pDest->m_dwPosZ = pSrc->m_dwPosZ;
    pDest->m_dw54 = pSrc->m_dw54;
    pDest->m_dw58 = pSrc->m_dw58;
    pDest->m_dw5C = pSrc->m_dw5C;
    pDest->m_dwPartyID = pSrc->m_dwPartyID;
}

// ---------------------------------------------------------------------------------------------
// Party

Party::Party()
{
    m_byType = 0;
    m_dwMasterJID = 0;
    m_dwID = 0;
    m_vecMember.reserve(8);
}

Party::~Party()
{
    Clear();
}

void Party::Clear()
{
    m_byType = 0;
    m_dwMasterJID = 0;
    m_dwID = 0;
    for (DWORD i = 0; i < m_vecMember.size(); ++i)
        PartyMemberInfo_Reset(&m_vecMember[i]->GetPartyMemberInfo());
    m_vecMember.clear();
}

BOOL Party::HasMember(CUser* pUser)
{
    return std::find(m_vecMember.begin(), m_vecMember.end(), pUser) != m_vecMember.end();
}

BOOL Party::IsFull()
{
    if (m_byType & 1)
        return m_vecMember.size() == 8;
    return m_vecMember.size() == 4;
}

WORD Party::InitWithTwoMembers(CUser* pMaster, DWORD dwID, CUser* pInvitee, BYTE byType)
{
    m_dwID = dwID;
    m_dwMasterJID = pMaster->GetJID();
    m_byType = byType;
    WORD wResult = AddMember(pMaster);
    if (wResult == 1)
        wResult = AddMember(pInvitee);
    return wResult;
}

WORD Party::AddMember(CUser* pUser)
{
    if (HasMember(pUser) == 1)
        return PARTY_ERR_ALREADY_MEMBER;
    if (IsFull())
        return (m_byType & 1) ? PARTY_ERR_FULL_8 : PARTY_ERR_FULL_4;
    m_vecMember.push_back(pUser);
    pUser->GetPartyMemberInfo().m_dwPartyID = m_dwID;
    return 1;
}

int Party::RemoveMemberAndNotify(CUser* pUser)
{
    if (m_vecMember.size() <= 2)
        BSAssertFail();
    PartyMemberInfo_Reset(&pUser->GetPartyMemberInfo());

    std::vector<CUser*>::iterator it = std::find(m_vecMember.begin(), m_vecMember.end(), pUser);
    if (it == m_vecMember.end())
        return (int)m_vecMember.size();

    DWORD dwEntryID = pUser->GetMatchEntryID();
    m_vecMember.erase(it);
    CUser* pMaster = m_vecMember.front();
    m_dwMasterJID = pMaster->GetJID();
    if (dwEntryID)
    {
        PartyMatchEntry* pEntry = GetSingleton_CPartyManager()->FindMatchEntry(dwEntryID);
        if (!pEntry)
            BSAssertFail();
        pEntry->m_dwMasterJID = pMaster->GetJID();
        pMaster->SetMatchEntryID(dwEntryID);
        pUser->SetMatchEntryID(0);
        CMsg* pMsg = CPartyManager::MakeNotify3864(18);
        *pMsg << m_dwID << pEntry->m_dwMasterJID << dwEntryID;
        SendAndRelease(pMaster->GetSessionID(), pMsg);
    }
    return (int)m_vecMember.size();
}

void Party::WriteToMsg(CMsg* pMsg)
{
    *pMsg << m_dwID << m_dwMasterJID << m_byType << (BYTE)m_vecMember.size();
    for (DWORD i = 0; i < m_vecMember.size(); ++i)
    {
        CUser* pMember = m_vecMember.at(i);
        if (pMember)
            sPartyMemberInfo_Write(pMsg, &pMember->GetPartyMemberInfo(), 0xFF);
    }
}

CUser* Party::FindMemberByJID(DWORD dwJID)
{
    for (DWORD i = 0; i < m_vecMember.size(); ++i)
    {
        CUser* pMember = m_vecMember.at(i);
        if (!pMember)
            BSAssertFail();
        if (pMember->GetJID() == dwJID)
            return pMember;
    }
    return NULL;
}

// ---------------------------------------------------------------------------------------------
// PartyMatchEntry

void PartyMatchEntry::Reset()
{
    m_pParty = NULL;
    m_dwMasterJID = 0;
    m_dw30 = 0;
    m_bJoinPending = 0;
    m_strTitle.clear();
    m_byMaxLevel = 0;
    m_byMinLevel = 0;
    m_byPurpose = 0;
    m_byPartyType = 0;
    m_dwPartyID = 0;
    m_dwID = 0;
}

BOOL PartyMatchEntry::WriteToMsg(CMsg* pMsg)
{
    if (!this)
        BSAssertFail();
    CUser* pMaster = GetSingleton_CClientManager()->FindUserByJID(m_dwMasterJID);
    if (!pMaster)
        return FALSE;
    *pMsg << m_dwID << m_dw30;

    const char* pszName;
    if (m_byPurpose == 2 || m_byPurpose == 3)
    {
        if (m_pParty)
        {
            CUser* pPartyMaster = m_pParty->FindMemberByJID(m_pParty->m_dwMasterJID);
            if (!pPartyMaster)
                return FALSE;
            const std::string& strName = pPartyMaster->GetPartyMemberInfo().m_strName;
            if (strName.empty())
                BSAssertFail();
            pszName = strName.c_str();
        }
        else
        {
            const std::string& strName = pMaster->GetString258();
            if (strName.empty())
                BSAssertFail();
            pszName = strName.c_str();
        }
    }
    else
    {
        pszName = pMaster->GetCharName();
    }
    *pMsg << pszName;

    BYTE byCount = m_pParty ? (BYTE)m_pParty->m_vecMember.size() : 1;
    *pMsg << pMaster->GetByte250() << byCount << m_byPartyType << m_byPurpose << m_byMinLevel
          << m_byMaxLevel << m_strTitle.c_str();
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// CPartyManager

CPartyManager::CPartyManager()
{
    if (g_pPartyManager)
        BSAssertFail();
    g_pPartyManager = this;
    m_dwPartyIDCounter = 0;
}

CPartyManager::~CPartyManager()
{
    m_hashParty.clear();
    g_pPartyManager = NULL;
}

PartyMatchEntry* CPartyManager::FindMatchEntry(DWORD dwEntryID)
{
    PartyMatchMap::iterator it = m_mapMatchEntry.find(dwEntryID);
    if (it == m_mapMatchEntry.end())
        return NULL;
    return it->second;
}

void CPartyManager::SendToShardAndFree(CMsg* pMsg, DWORD dwParam)
{
    if (!g_pMainProcess)
        BSAssertFail();
    g_pMainProcess->BroadcastToGameServers(pMsg, dwParam);
    g_pNetEngine->ReleaseMsg(pMsg);
}

void CPartyManager::SendPartyInfo(CMsg* pMsg, Party* pParty)
{
    DWORD dwJID;
    *pMsg >> dwJID;
    CUser* pMember = pParty->FindMemberByJID(dwJID);
    if (!pMember)
        return;
    sPartyMemberInfo* pInfo = &pMember->GetPartyMemberInfo();
    BYTE byMask = sPartyMemberInfo_Read(pInfo, pMsg);
    CMsg* pNotify = MakeNotify3864(6);
    *pNotify << pParty->m_dwID << dwJID;
    sPartyMemberInfo_Write(pNotify, pInfo, byMask);
    SendToShardAndFree(pNotify, 0);
}

CMsg* CPartyManager::MakeNotify3864(BYTE bySubType)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(MSG_PARTY_NOTIFY);
    *pMsg << bySubType;
    return pMsg;
}

CMsg* CPartyManager::MakeReplyB866(DWORD dwRequestID)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(MSG_PARTY_ACK);
    *pMsg << dwRequestID;
    return pMsg;
}

void CPartyManager::OnPartyNotifyRelay(CMsg* pMsg)
{
    std::string strFirst;
    std::string strSecond;
    DWORD dwPartyID;
    *pMsg >> dwPartyID;
    if (FindParty(dwPartyID))
    {
        *pMsg >> strFirst >> strSecond;
        CMsg* pNotify = MakeNotify3864(7);
        *pNotify << dwPartyID << strFirst.c_str() << strSecond.c_str();
        SendToShardAndFree(pNotify, 0);
    }
}

Party* CPartyManager::CreateParty(CUser* pMaster, CUser* pInvitee, BYTE byType, WORD* pwResult)
{
    if (m_dwPartyIDCounter++ == 0xFFFFFFFF)
        m_dwPartyIDCounter = 1;
    DWORD dwID = m_dwPartyIDCounter;
    if (!dwID)
    {
        *pwResult = 2;
        return NULL;
    }
    if (m_hashParty.find(dwID) != m_hashParty.end())
    {
        *pwResult = 2;
        return NULL;
    }
    Party* pParty = m_PartyPool.Alloc();
    if (!pParty)
    {
        *pwResult = 2;
        return NULL;
    }
    WORD wResult = pParty->InitWithTwoMembers(pMaster, dwID, pInvitee, byType);
    *pwResult = wResult;
    if (wResult != 1)
    {
        pParty->Clear();
        m_PartyPool.Free(pParty);
        return NULL;
    }
    *pwResult = 1;
    m_hashParty.insert(PartyHash::value_type(dwID, pParty));
    return pParty;
}

Party* CPartyManager::FindParty(DWORD dwPartyID)
{
    PartyHash::iterator it = m_hashParty.find(dwPartyID);
    if (it == m_hashParty.end())
        return NULL;
    return it->second;
}

void CPartyManager::DisbandParty(DWORD dwPartyID)
{
    WORD wReason = PARTY_ERR_DISBANDED;
    PartyHash::iterator it = m_hashParty.find(dwPartyID);
    if (it == m_hashParty.end())
        return;
    Party* pParty = it->second;
    CMsg* pNotify = MakeNotify3864(1);
    *pNotify << pParty->m_dwID << wReason;
    SendToShardAndFree(pNotify, 0);

    CUser* pMaster = GetSingleton_CClientManager()->FindUserByJID(pParty->m_dwMasterJID);
    if (pMaster && pMaster->GetMatchEntryID())
    {
        RemoveMatchEntry(pMaster, pMaster->GetMatchEntryID());
        RebuildMatchListCache();
    }
    pParty->Clear();
    m_PartyPool.Free(pParty);
    m_hashParty.erase(it);
}

void CPartyManager::OnPartyRequest(CMsg* pMsg)
{
    BYTE bySubType;
    *pMsg >> bySubType;
    switch (bySubType)
    {
    case 0:
        OnCreateParty(pMsg);
        break;
    case 2:
        OnJoinParty(pMsg);
        break;
    case 10:
        OnRegisterPartyMatch(pMsg);
        break;
    case 11:
        OnModifyPartyMatch(pMsg);
        break;
    case 12:
        OnDeletePartyMatch(pMsg);
        break;
    case 13:
        OnPartyMatchList(pMsg);
        break;
    default:
        BSAssertFail();
        break;
    }
}

void CPartyManager::OnCreateParty(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    Party* pParty = NULL;
    DWORD dwResult = 2;
    DWORD dwRequestID = 0;
    BYTE byMatchType;
    *pMsg >> dwRequestID >> byMatchType;
    CMsg* pReply = MakeReplyB866(dwRequestID);

    sPartyMemberInfo infoMaster;
    sPartyMemberInfo infoInvitee;
    sPartyMemberInfo_Read(&infoMaster, pMsg);
    sPartyMemberInfo_Read(&infoInvitee, pMsg);
    CUser* pMaster = GetSingleton_CClientManager()->FindUserByJID(infoMaster.m_dwJID);
    CUser* pInvitee = GetSingleton_CClientManager()->FindUserByJID(infoInvitee.m_dwJID);
    if (!pMaster || !pInvitee)
    {
        dwResult = PARTY_ERR_NO_USER;
    }
    else if (pMaster->GetPartyMemberInfo().m_dwPartyID)
    {
        dwResult = PARTY_ERR_ALREADY_IN_PARTY;
    }
    else if (pInvitee->GetPartyMemberInfo().m_dwPartyID)
    {
        dwResult = PARTY_ERR_TARGET_IN_PARTY;
    }
    else if (!Party_IsCompatibleType(infoMaster.m_byPartyType, infoInvitee.m_byPartyType))
    {
        dwResult = PARTY_ERR_TYPE;
    }
    else if (pMaster->GetMatchEntryID() &&
             FindMatchEntry(pMaster->GetMatchEntryID())->m_byPartyType != byMatchType)
    {
        dwResult = PARTY_ERR_MATCH_TYPE;
    }
    else
    {
        PartyMemberInfo_Copy(&pMaster->GetPartyMemberInfo(), &infoMaster);
        PartyMemberInfo_Copy(&pInvitee->GetPartyMemberInfo(), &infoInvitee);
        WORD wResult = (WORD)dwResult;
        pParty = CreateParty(pMaster, pInvitee, byMatchType, &wResult);
        dwResult = wResult;
        if (wResult != 1)
        {
            if (pParty)
                BSAssertFail();
        }
        else
        {
            if (!pParty)
                BSAssertFail();
            if (pMaster->GetMatchEntryID())
            {
                PartyMatchEntry* pEntry = FindMatchEntry(pMaster->GetMatchEntryID());
                if (!pEntry)
                    BSAssertFail();
                pEntry->m_pParty = pParty;
                pEntry->m_dwPartyID = pParty->m_dwID;
                RebuildMatchListCache();
            }
            *pReply << (BYTE)1 << pParty->m_dwID;
            SendAndRelease(dwSessionID, pReply);
            CMsg* pNotify = MakeNotify3864(8);
            pParty->WriteToMsg(pNotify);
            SendToShardAndFree(pNotify, dwSessionID);
            return;
        }
    }

    pMsg->SkipRemaining();
    if (pParty)
    {
        pParty->Clear();
        m_PartyPool.Free(pParty);
    }
    *pReply << (BYTE)2 << (WORD)dwResult;
    SendAndRelease(dwSessionID, pReply);
}

void CPartyManager::OnJoinParty(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    sPartyMemberInfo info;
    DWORD dwResult;
    DWORD dwPartyID;
    *pMsg >> dwResult >> dwPartyID;     // the request ID, then the result code in the same slot
    CMsg* pReply = MakeReplyB866(dwResult);

    Party* pParty = FindParty(dwPartyID);
    if (!pParty)
    {
        dwResult = PARTY_ERR_NO_PARTY;
    }
    else
    {
        sPartyMemberInfo_Read(&info, pMsg);
        CUser* pMaster = pParty->FindMemberByJID(pParty->m_dwMasterJID);
        CUser* pUser = NULL;
        if (!pMaster)
        {
            dwResult = PARTY_ERR_NO_PARTY;
        }
        else if (!Party_IsCompatibleType(pMaster->GetPartyMemberInfo().m_byPartyType,
                                         info.m_byPartyType))
        {
            dwResult = PARTY_ERR_TYPE;
        }
        else if (!(pUser = GetSingleton_CClientManager()->FindUserByJID(info.m_dwJID)))
        {
            dwResult = PARTY_ERR_NO_USER;
        }
        else if (pUser->GetMatchEntryID())
        {
            dwResult = PARTY_ERR_HAS_MATCH_ENTRY;
        }
        else
        {
            dwResult = pParty->AddMember(pUser);
            if ((WORD)dwResult == 1)
            {
                info.m_dwPartyID = pParty->m_dwID;
                PartyMemberInfo_Copy(&pUser->GetPartyMemberInfo(), &info);
                pMaster = pParty->FindMemberByJID(pParty->m_dwMasterJID);
                if (!pMaster)
                    BSAssertFail();
                if (pMaster->GetMatchEntryID())
                    RebuildMatchListCache();
                *pReply << (BYTE)1;
                SendAndRelease(dwSessionID, pReply);
                CMsg* pNotify = MakeNotify3864(2);
                *pNotify << dwPartyID;
                sPartyMemberInfo_Write(pNotify, &info, 0xFF);
                SendToShardAndFree(pNotify, 0);
                return;
            }
        }
    }

    pMsg->SkipRemaining();
    *pReply << (BYTE)2 << (WORD)dwResult;
    SendAndRelease(dwSessionID, pReply);
}

void CPartyManager::OnPartyNotify(CMsg* pMsg)
{
    BYTE bySubType;
    *pMsg >> bySubType;
    switch (bySubType)
    {
    case 3:
        OnLeaveParty(pMsg);
        break;
    case 6:
        OnPartyInfoRequest(pMsg);
        break;
    case 7:
        OnPartyNotifyRelay(pMsg);
        break;
    default:
        BSAssertFail();
        break;
    }
    pMsg->SkipRemaining();
}

void CPartyManager::OnPartyInfoRequest(CMsg* pMsg)
{
    DWORD dwPartyID;
    *pMsg >> dwPartyID;
    Party* pParty = FindParty(dwPartyID);
    if (pParty)
        SendPartyInfo(pMsg, pParty);
    pMsg->SkipRemaining();
}

void CPartyManager::OnLeaveParty(CMsg* pMsg)
{
    DWORD dwPartyID, dwJID;
    BYTE byReason;
    *pMsg >> dwPartyID >> dwJID >> byReason;
    RemoveMember(dwPartyID, dwJID, byReason);
}

void CPartyManager::RemoveMember(DWORD dwPartyID, DWORD dwJID, BYTE byReason)
{
    Party* pParty = FindParty(dwPartyID);
    if (!pParty)
        return;
    CUser* pMember = pParty->FindMemberByJID(dwJID);
    if (!pMember)
        return;
    if (pParty->m_vecMember.size() == 2)
    {
        DisbandParty(dwPartyID);
        return;
    }
    CMsg* pNotify = MakeNotify3864(3);
    *pNotify << pParty->m_dwID << dwJID << byReason;
    SendToShardAndFree(pNotify, 0);
    pParty->RemoveMemberAndNotify(pMember);
    RebuildMatchListCache();
}

WORD CPartyManager::OnPartyMatchJoinRequest(DWORD dwRequesterJID, DWORD dwOpcode, CMsg* pMsg)
{
    CUser* pRequester = GetSingleton_CClientManager()->FindUserByJID(dwRequesterJID);
    if (!pRequester)
        return 2;
    trs_party_job* pJob = trs_party_job_Alloc();
    WORD wResult;

    if ((WORD)dwOpcode == MSG_PARTY_MATCH_JOIN_REQ)
    {
        if (pRequester->GetPartyMemberInfo().m_dwPartyID)
        {
            wResult = PARTY_ERR_TARGET_IN_PARTY;
            goto release;
        }
        if (pRequester->GetMatchEntryID())
        {
            wResult = PARTY_ERR_HAS_MATCH_ENTRY;
            goto release;
        }
        sPartyInfo* pInfo = &pJob->m_Info;
        sPartyInfo_Reset(pInfo);
        PartyMatchJoinRequest_ReadFromMsg(pMsg, pInfo);
        PartyMatchEntry* pEntry = FindMatchEntry(pInfo->m_dwMatchEntryID);
        if (!pEntry)
        {
            wResult = PARTY_ERR_NO_MATCH_ENTRY;
            goto release;
        }
        Party* pParty = pEntry->m_pParty;
        if (pParty)
        {
            if (pParty->IsFull())
            {
                wResult = (pParty->m_byType & 1) ? PARTY_ERR_FULL_8 : PARTY_ERR_FULL_4;
                goto release;
            }
            CUser* pMaster = pParty->FindMemberByJID(pParty->m_dwMasterJID);
            if (!pMaster)
            {
                wResult = PARTY_ERR_NO_MATCH_ENTRY;
                goto release;
            }
            if (!Party_IsCompatibleType(pMaster->GetPartyMemberInfo().m_byPartyType,
                                        pInfo->m_byPartyType))
            {
                wResult = PARTY_ERR_TYPE;
                goto release;
            }
        }
        if (!PartyMatch_IsJoinAllowed(pInfo->m_byPartyType, pEntry->m_byPurpose))
        {
            wResult = PARTY_ERR_TYPE;
            goto release;
        }
        BYTE byLevel = pInfo->m_Member.m_byLevel;
        if (pEntry->m_byMinLevel > byLevel || pEntry->m_byMaxLevel < byLevel)
        {
            wResult = PARTY_ERR_LEVEL;
            goto release;
        }
        if (pEntry->m_bJoinPending)
        {
            wResult = PARTY_ERR_JOIN_PENDING;
            goto release;
        }
        CUser* pEntryMaster = GetSingleton_CClientManager()->FindUserByJID(pEntry->m_dwMasterJID);
        if (!pEntryMaster)
        {
            wResult = PARTY_ERR_NO_MASTER;
            goto release;
        }
        pEntry->m_bJoinPending = 1;
        pJob->Init(dwRequesterJID, dwOpcode, pEntry->m_dwMasterJID, 15.0f, 0, 0);
        pJob->m_dwTargetSessionID = pEntryMaster->GetSessionID();
    }
    else if ((WORD)dwOpcode == MSG_PARTY_MATCH_INVITE_REQ)
    {
        DWORD dw120;
        float fTimeout;
        *pMsg >> dw120 >> fTimeout;
        Party* pParty =
            GetSingleton_CPartyManager()->FindParty(pRequester->GetPartyMemberInfo().m_dwPartyID);
        if (!pParty)
        {
            wResult = PARTY_ERR_NO_PARTY;
            goto release;
        }
        if (dwRequesterJID != pParty->m_dwMasterJID)
        {
            wResult = PARTY_ERR_NO_MASTER;
            goto release;
        }
        pJob->Init(dwRequesterJID, dwOpcode, 0, fTimeout, 0, 0);
        pJob->m_dw120 = dw120;
        std::vector<CUser*> vecMember(pParty->m_vecMember);
        for (DWORD i = 0; i < vecMember.size(); ++i)
        {
            CUser* pMember = vecMember[i];
            if (!pMember)
            {
                wResult = PARTY_ERR_NO_USER;
                goto release;
            }
            if (dwRequesterJID != pMember->GetJID())
                pJob->AddParticipantJID(pMember->GetJID());
        }
    }
    else
    {
        BSAssertFail();
        wResult = 2;
        goto release;
    }

    if (GetSingleton_COverlappedJobMgr()->StartJob(pJob, pRequester, pMsg))
        return 1;
    pJob->Release();
    return 2;

release:
    pJob->Release();
    return wResult;
}

void CPartyManager::OnPartyMatchJoinAnswer(trs_party_job* pJob, BYTE byAnswer)
{
    if (byAnswer != 1)
    {
        if (byAnswer != 0 && byAnswer != 2)
        {
            BSAssertFail();
            return;
        }
        CUser* pRequester = GetSingleton_CClientManager()->FindUserByJID(pJob->m_dwRequesterJID);
        if (pRequester)
        {
            SendMatchJoinResult(pRequester->GetSessionID(), pRequester->GetJID(), 1, byAnswer);
            PartyMatchEntry* pEntry = FindMatchEntry(pJob->m_Info.m_dwMatchEntryID);
            if (pEntry)
                pEntry->m_bJoinPending = 0;
        }
        return;
    }

    CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(pJob->m_dwRequesterJID);
    CUser* pMaster = GetSingleton_CClientManager()->FindUserByJID(pJob->m_dwTargetJID);
    if (!pUser && pMaster)
    {
        SendMatchJoinResult(pMaster->GetSessionID(), pMaster->GetJID(), 2, PARTY_ERR_NO_USER);
        return;
    }
    if (!pMaster)
    {
        // BUG(SM-243): with both users gone the requester (NULL) is dereferenced.
        SendMatchJoinResult(pUser->GetSessionID(), pUser->GetJID(), 2, PARTY_ERR_NO_USER);
        return;
    }

    // BUG(SM-242): the failures below leave the entry's join-pending flag set.
    PartyMatchEntry* pEntry = FindMatchEntry(pJob->m_Info.m_dwMatchEntryID);
    if (!pEntry)
    {
        SendMatchJoinResult(pUser->GetSessionID(), pUser->GetJID(), 2, PARTY_ERR_NO_MATCH_ENTRY);
        SendMatchJoinResult(pMaster->GetSessionID(), pMaster->GetJID(), 2,
                            PARTY_ERR_NO_MATCH_ENTRY);
        return;
    }

    if (pMaster->GetPartyMemberInfo().m_dwPartyID)
    {
        Party* pParty = FindParty(pMaster->GetPartyMemberInfo().m_dwPartyID);
        if (!pParty)
        {
            SendMatchJoinResult(pUser->GetSessionID(), pUser->GetJID(), 2, PARTY_ERR_NO_PARTY);
            SendMatchJoinResult(pMaster->GetSessionID(), pMaster->GetJID(), 2,
                                PARTY_ERR_NO_PARTY);
            return;
        }
        WORD wResult = pParty->AddMember(pUser);
        if (wResult != 1)
        {
            SendMatchJoinResult(pUser->GetSessionID(), pUser->GetJID(), 2, wResult);
            SendMatchJoinResult(pMaster->GetSessionID(), pMaster->GetJID(), 2, wResult);
            return;
        }
        pJob->m_Info.m_Member.m_dwPartyID = pParty->m_dwID;
        PartyMemberInfo_Copy(&pUser->GetPartyMemberInfo(), &pJob->m_Info.m_Member);
        pEntry->m_bJoinPending = 0;
        SendMatchJoinResult(pUser->GetSessionID(), pUser->GetJID(), 1, byAnswer);
        SendMatchJoinResult(pMaster->GetSessionID(), pMaster->GetJID(), 1, byAnswer);
        CMsg* pNotify = MakeNotify3864(16);
        *pNotify << pParty->m_dwID;
        sPartyMemberInfo_Write(pNotify, &pJob->m_Info.m_Member, 0xFF);
        SendToShardAndFree(pNotify, 0);
        RebuildMatchListCache();
        return;
    }

    PartyMemberInfo_Copy(&pUser->GetPartyMemberInfo(), &pJob->m_Info.m_Member);
    PartyMemberInfo_Copy(&pMaster->GetPartyMemberInfo(), &pJob->m_Master);
    WORD wResult = 1;
    Party* pParty = CreateParty(pMaster, pUser, pEntry->m_byPartyType, &wResult);
    if (wResult != 1)
    {
        SendMatchJoinResult(pUser->GetSessionID(), pUser->GetJID(), 2, wResult);
        SendMatchJoinResult(pMaster->GetSessionID(), pMaster->GetJID(), 2, wResult);
        return;
    }
    pEntry->m_pParty = pParty;
    pEntry->m_dwPartyID = pParty->m_dwID;
    pEntry->m_bJoinPending = 0;
    SendMatchJoinResult(pUser->GetSessionID(), pUser->GetJID(), 1, byAnswer);
    // BUG(SM-241): the master's notice carries the requester's JID, not the master's.
    SendMatchJoinResult(pMaster->GetSessionID(), pUser->GetJID(), 1, byAnswer);
    CMsg* pNotify = MakeNotify3864(15);
    pParty->WriteToMsg(pNotify);
    SendToShardAndFree(pNotify, 0);
    RebuildMatchListCache();
}
