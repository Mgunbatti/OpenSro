#include "ServerCommon/GuildCommon.h"

#include "BSLib/ChunkAllocator.h"
#include "BSLib/Util/TimeSpan.h"
#include "ServerFramework/ServerDataExtern.h"

const DWORD g_adwGuildMemberLimit[5] = { 15, 20, 25, 35, 50 };

// The guild member pool (its in-use count is dword_89A404).
static CChunkAllocatorST<CGuildMember> s_GuildMemberPool;

// ---------------------------------------------------------------------------------------------
// Record serialisation

void GuildMemberRecord_WriteToMsg(CMsg* pMsg, const CInstanceGuildMember* pRecord)
{
    pMsg->WriteBytes(&pRecord->CharID, 4);
    *pMsg << pRecord->CharName.c_str();
    pMsg->WriteBytes(&pRecord->MemberClass, 1);
    pMsg->WriteBytes(&pRecord->CharLevel, 1);
    pMsg->WriteBytes(&pRecord->GP_Donation, 4);
    pMsg->WriteBytes(&pRecord->Permission, 4);
    pMsg->WriteBytes(&pRecord->Contribution, 4);
    pMsg->WriteBytes(&pRecord->GuildWarKill, 4);
    pMsg->WriteBytes(&pRecord->GuildWarKilled, 4);
    *pMsg << pRecord->Nickname.c_str();
    pMsg->WriteBytes(&pRecord->RefObjID, 4);
    pMsg->WriteBytes(&pRecord->SiegeAuthority, 1);
}

void GuildMemberRecord_Serialize(const CInstanceGuildMember* pRecord, CByteStream* pStream)
{
    pStream->Write<DWORD>(pRecord->CharID);
    pStream->WriteString(pRecord->CharName.c_str());
    pStream->Write<BYTE>(pRecord->MemberClass);
    pStream->Write<BYTE>(pRecord->CharLevel);
    pStream->Write<DWORD>(pRecord->GP_Donation);
    pStream->Write<DWORD>(pRecord->Permission);
    pStream->Write<DWORD>(pRecord->Contribution);
    pStream->Write<DWORD>(pRecord->GuildWarKill);
    pStream->Write<DWORD>(pRecord->GuildWarKilled);
    pStream->WriteString(pRecord->Nickname.c_str());
    pStream->Write<DWORD>(pRecord->RefObjID);
    pStream->Write<BYTE>(pRecord->SiegeAuthority);
}

void GuildRecord_Serialize(CByteStream* pStream, const CInstanceGuild* pRecord, BYTE bFlag)
{
    if (bFlag & 0x01)
        pStream->Write<DWORD>(pRecord->ID);
    if (bFlag & 0x02)
        pStream->WriteString(pRecord->Name.c_str());
    if (bFlag & 0x04)
        pStream->Write<BYTE>(pRecord->Lvl);
    if (bFlag & 0x08)
        pStream->Write<DWORD>(pRecord->GatheredSP);
    if (bFlag & 0x10)
    {
        pStream->WriteString(pRecord->MasterCommentTitle.c_str());
        pStream->WriteString(pRecord->MasterComment.c_str());
    }
    if (bFlag & 0x20)
        pStream->Write<DWORD>(pRecord->CurCrestRev);
    if (bFlag & 0x40)
        pStream->Write<BYTE>(pRecord->MercenaryAttr);
}

void GuildRecord_WriteToMsg(CMsg* pMsg, const CInstanceGuild* pRecord, BYTE bFlag)
{
    if (bFlag & 0x01)
        pMsg->WriteBytes(&pRecord->ID, 4);
    if (bFlag & 0x02)
        *pMsg << pRecord->Name.c_str();
    if (bFlag & 0x04)
        pMsg->WriteBytes(&pRecord->Lvl, 1);
    if (bFlag & 0x08)
        pMsg->WriteBytes(&pRecord->GatheredSP, 4);
    if (bFlag & 0x10)
    {
        *pMsg << pRecord->MasterCommentTitle.c_str();
        *pMsg << pRecord->MasterComment.c_str();
    }
    if (bFlag & 0x20)
        pMsg->WriteBytes(&pRecord->CurCrestRev, 4);
    if (bFlag & 0x40)
        pMsg->WriteBytes(&pRecord->MercenaryAttr, 1);
}

void GuildGPHistory_WriteToMsg(const CInstanceGPHistory* pRecord, CMsg* pMsg)
{
    pMsg->WriteBytes(&pRecord->ID, 4);
    pMsg->WriteBytes(&pRecord->GuildID, 4);
    const TIMESTAMP_STRUCT& t = pRecord->UsedTime;
    CDateTime dt(t.year, t.month, t.day, t.hour, t.minute, t.second);
    DWORD dwTime = (DWORD)dt.GetTime();
    pMsg->WriteBytes(&dwTime, 4);
    *pMsg << pRecord->CharName.c_str();
    pMsg->WriteBytes(&pRecord->UsedGP, 4);
    pMsg->WriteBytes(&pRecord->Reason, 1);
}

// ---------------------------------------------------------------------------------------------
// CGuildMember

void CGuildMember::Release()
{
    if (m_bAlive)
    {
        m_bAlive = 0;
        ReleaseRecord();
        s_GuildMemberPool.Free(this);
    }
    else
    {
        LogPrintf(0x2000000, "why do you about to delete already deleted !!! [%s]", "GuildMember");
        BSAssertFail();
    }
}

CGuildMember* CGuildMember::Alloc()
{
    CGuildMember* pMember = s_GuildMemberPool.Alloc();
    pMember->m_bAlive = 1;
    return pMember;
}

void GuildMember_FreePtr(CGuildMember*& pMember)
{
    if (!pMember)
        return;
    if (pMember->m_bAlive)
    {
        pMember->m_bAlive = 0;
        pMember->ReleaseRecord();
        s_GuildMemberPool.Free(pMember);
    }
    pMember = NULL;
}

void CGuildMember::SetNickname(const std::string& strNickname)
{
    if (m_pRecord)
        m_pRecord->SetColumn(&InstanceGuildMemberData::Nickname, std::string(strNickname));
    else
        BSAssertFail();
}

void CGuildMember::WriteToMsg(CMsg* pMsg) const
{
    GuildMemberRecord_WriteToMsg(pMsg, m_pRecord);
    BYTE bState = (BYTE)m_dwState;
    pMsg->WriteBytes(&bState, 1);
}

void CGuildMember::Serialize(CByteStream* pStream) const
{
    GuildMemberRecord_Serialize(m_pRecord, pStream);
    pStream->Write<BYTE>((BYTE)m_dwState);
}

// ---------------------------------------------------------------------------------------------
// CGuildCommon

CGuildCommon::CGuildCommon()
{
    m_pRecord = NULL;
    m_pMasterRecord = NULL;
    m_pLockMember = NULL;
}

CGuildCommon::~CGuildCommon()
{
    CGuildCommon::Clear();
    m_pRecord = NULL;
    m_pMasterRecord = NULL;
    m_pLockMember = NULL;
}

DWORD CGuildCommon::AddMember(DWORD dwState, CInstanceGuildMember* pRecord, CUser* pUser)
{
    CGuildMember* pMember = CGuildMember::Alloc();
    pMember->m_dwState = dwState;
    pMember->m_pRecord = pRecord;
    pMember->m_pUser = pUser;
    if (m_mapMember.find(pRecord->CharID) != m_mapMember.end())
        BSAssertFail();
    m_mapMember.insert(GuildMemberMap::value_type(pRecord->CharID, pMember));
    return CountMembers();
}

void CGuildCommon::WriteToMsg(CMsg* pMsg, BYTE bFlag)
{
    GuildRecord_WriteToMsg(pMsg, m_pRecord, bFlag);
    BYTE bCount = (BYTE)m_mapMember.size();
    pMsg->WriteBytes(&bCount, 1);
    for (GuildMemberMap::iterator it = m_mapMember.begin(); it != m_mapMember.end(); ++it)
        it->second->WriteToMsg(pMsg);
}

void CGuildCommon::Serialize(CByteStream* pStream, BYTE bFlag)
{
    GuildRecord_Serialize(pStream, m_pRecord, bFlag);
    pStream->Write<BYTE>((BYTE)m_mapMember.size());
    for (GuildMemberMap::iterator it = m_mapMember.begin(); it != m_mapMember.end(); ++it)
        it->second->Serialize(pStream);
}

void CGuildCommon::WriteInfo(CMsg* pMsg, BYTE bMask)
{
    pMsg->WriteBytes(&m_pRecord->ID, 4);
    if (bMask & 1)
        *pMsg << m_pRecord->Name.c_str();
    if (bMask & 2)
        pMsg->WriteBytes(&m_pRecord->Lvl, 1);
    if (bMask & 4)
    {
        if (m_pMasterRecord)
        {
            *pMsg << m_pMasterRecord->CharName.c_str();
            pMsg->WriteBytes(&m_pMasterRecord->RefObjID, 4);
        }
        else
        {
            BSAssertFail();
            DWORD dwRefObjID = 0;
            *pMsg << "Unknown";
            pMsg->WriteBytes(&dwRefObjID, 4);
        }
    }
    if (bMask & 8)
    {
        BYTE bCount = (BYTE)m_mapMember.size();
        pMsg->WriteBytes(&bCount, 1);
    }
}

BOOL CGuildCommon::IsHostileGuild(DWORD dwGuildID)
{
    return FindHostileEntry(dwGuildID) != NULL;
}

CGuildMember* CGuildCommon::FindMember(DWORD dwCharID)
{
    if (!dwCharID)
        return NULL;
    GuildMemberMap::iterator it = m_mapMember.find(dwCharID);
    if (it == m_mapMember.end())
        return NULL;
    return it->second;
}

BOOL CGuildCommon::IsMemberMaster(DWORD dwCharID)
{
    CGuildMember* pMember = FindMember(dwCharID);
    if (!pMember)
        return FALSE;
    if (!pMember->m_pRecord)
        BSAssertFail();
    return pMember->IsMaster();
}

BOOL CGuildCommon::MemberHasPermission8(DWORD dwCharID)
{
    CGuildMember* pMember = FindMember(dwCharID);
    if (!pMember)
        return FALSE;
    CInstanceGuildMember* pRecord = pMember->m_pRecord;
    if (!pRecord)
        BSAssertFail();
    return (pRecord->Permission & 8) == 8;
}

void CGuildCommon::SetLockMember(DWORD dwCharID)
{
    CGuildMember* pMember = FindMember(dwCharID);
    if (pMember)
    {
        m_pLockMember = pMember;
        m_dwLockCharID = dwCharID;
    }
}

GUILD_HOSTILE_ENTRY* CGuildCommon::FindHostileEntry(DWORD dwGuildID)
{
    GuildHostileMap::iterator it = m_mapHostile.find(dwGuildID);
    if (it == m_mapHostile.end())
        return NULL;
    return &it->second;
}

DWORD CGuildCommon::CountMembers()
{
    DWORD dwSum = 0;
    DWORD dwMax = 0;
    for (GuildMemberMap::iterator it = m_mapMember.begin(); it != m_mapMember.end(); ++it)
    {
        DWORD dwLevel = it->second->m_pRecord->CharLevel;
        dwSum += dwLevel;
        if (dwLevel > dwMax)
            dwMax = it->second->m_pRecord->CharLevel;
    }
    DWORD dwAvg = dwSum / m_mapMember.size();
    m_dwMaxLevel = dwMax;
    m_dwAvgLevel = dwAvg;
    return dwAvg;
}

BOOL CGuildCommon::IsMemberCountFull()
{
    BYTE bLevel = m_pRecord->Lvl;
    if (!bLevel)
    {
        BSAssertFail();
        bLevel = 1;
    }
    BYTE bIndex = (BYTE)(bLevel - 1);
    if (bIndex >= 5)
        BSAssertFail();
    return m_mapMember.size() >= g_adwGuildMemberLimit[bIndex];
}

BOOL CGuildCommon::AddHostileGuild(CInstanceGuildWar* pWar)
{
    DWORD dwOther = pWar->Guild[0];
    if (dwOther == GetID())
        dwOther = pWar->Guild[1];
    if (m_mapHostile.find(dwOther) != m_mapHostile.end())
    {
        BSAssertFail();
        return FALSE;
    }
    GUILD_HOSTILE_ENTRY entry;
    entry.dwSide = (DWORD)pWar->Guild[0] != GetID();
    entry.pWar = pWar;
    m_mapHostile.insert(GuildHostileMap::value_type(dwOther, entry));
    return TRUE;
}

void CGuildCommon::RemoveHostileGuild(CInstanceGuildWar* pWar)
{
    DWORD dwOther = pWar->Guild[0];
    if (dwOther == (DWORD)m_pRecord->ID)
        dwOther = pWar->Guild[1];
    GuildHostileMap::iterator it = m_mapHostile.find(dwOther);
    if (it != m_mapHostile.end())
        m_mapHostile.erase(it);
}

BOOL CGuildCommon::AddGPHistory(CInstanceGPHistory* pRecord, BYTE bFront)
{
    if (!pRecord)
    {
        BSAssertFail();
        return FALSE;
    }
    if (bFront == 1)
        m_listGPHistory.push_front(pRecord);
    else
        m_listGPHistory.push_back(pRecord);
    return TRUE;
}

void CGuildCommon::WriteLastGPHistory(CMsg* pMsg)
{
    if (m_listGPHistory.empty())
        return;
    CInstanceGPHistory* pRecord = m_listGPHistory.front();
    if (pRecord)
        GuildGPHistory_WriteToMsg(pRecord, pMsg);
}

// ---------------------------------------------------------------------------------------------
// CGuildMgrCommon

CGuildCommon* CGuildMgrCommon::FindGuild(DWORD dwGuildID)
{
    if (!dwGuildID)
        return NULL;
    GuildMap::iterator it = m_mapGuild.find(dwGuildID);
    if (it == m_mapGuild.end())
        return NULL;
    return it->second;
}

CAlliance* CGuildMgrCommon::AddAlliance(CInstanceAlliedClan* pRecord)
{
    DWORD dwAllianceID = pRecord->ID;
    AllianceMap::iterator it = m_mapAlliance.find(dwAllianceID);
    if (it == m_mapAlliance.end())
    {
        // CAlliance_MakeMapValue (0x4432a0) / CAlliance::CopyToMapValue (0x445780): the value
        // is built from a temporary alliance and copied into the node.
        CAlliance alliance;
        m_mapAlliance.insert(AllianceMap::value_type(dwAllianceID, alliance));
        it = m_mapAlliance.find(dwAllianceID);
    }
    it->second.m_pRecord = pRecord;
    return &it->second;
}

CAlliance* CGuildMgrCommon::FindAlliance(DWORD dwAllianceID)
{
    if (!dwAllianceID)
        return NULL;
    AllianceMap::iterator it = m_mapAlliance.find(dwAllianceID);
    if (it == m_mapAlliance.end())
        return NULL;
    return &it->second;
}

BOOL CGuildMgrCommon::AllianceHasGuild(DWORD dwAllianceID, DWORD dwGuildID)
{
    AllianceMap::iterator it = m_mapAlliance.find(dwAllianceID);
    if (it == m_mapAlliance.end())
        return FALSE;
    return it->second.HasGuild(dwGuildID);
}

void CGuildMgrCommon::LinkGuildWars()
{
    for (GuildWarMap::iterator it = m_mapGuildWar.begin(); it != m_mapGuildWar.end(); ++it)
        AddHostileGuilds(it->second);
}

BOOL CGuildMgrCommon::AddGuildWar(CInstanceGuildWar* pWar)
{
    if (m_mapGuildWar.find(pWar->ID) != m_mapGuildWar.end())
        return FALSE;
    AddHostileGuilds(pWar);
    m_mapGuildWar.insert(GuildWarMap::value_type(pWar->ID, pWar));
    return TRUE;
}

CInstanceGuildWar* CGuildMgrCommon::FindGuildWar(DWORD dwWarID)
{
    GuildWarMap::iterator it = m_mapGuildWar.find(dwWarID);
    if (it == m_mapGuildWar.end())
        return NULL;
    return it->second;
}
