#include "ShardManager/BattleArena.h"

#include <string>

#include "BSLib/BSDebug.h"
#include "BSNet/IBSNet.h"
#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerCommon/ReferenceData.h"
#include "ShardManager/GameWorld.h"
#include "ShardManager/ShardScheduleJob.h"
#include "ShardManager/ShardWorldExtern.h"

enum
{
    OP_BATTLEARENA_NOTICE       = 0x34D2,   // to the game servers
    OP_BATTLEARENA_REGISTER_ACK = 0xB4D0
};

// The first byte of 0x34D2.
enum
{
    BATTLEARENA_PARTICIPATION_BEGIN = 2,    // kind, begin time, active match flags
    BATTLEARENA_PARTICIPATION_END   = 3,    // kind
    BATTLEARENA_GAME_END            = 5,    // kind
    BATTLEARENA_MEMBER_ADDED        = 7,    // world, layer, JID
    BATTLEARENA_PARTICIPATION_TIME  = 8,    // JID, ms
    BATTLEARENA_END_PLAY            = 9,    // kind
    BATTLEARENA_MEMBER_REMOVED      = 11,   // world, layer, JID
    BATTLEARENA_5MIN_LEFT           = 13,   // kind
    BATTLEARENA_1MIN_LEFT           = 14,   // kind
    BATTLEARENA_LETS_PARTICIPATE    = 15,   // kind
    BATTLEARENA_STOP_RESURRECT      = 18,   // kind
    BATTLEARENA_KICK                = 0xFF  // JID, reason
};

// Errors of 0xB4D0.
enum
{
    BATTLEARENA_ERR_INVALID         = 1,
    BATTLEARENA_ERR_REGISTERED      = 2,
    BATTLEARENA_ERR_NOT_REGISTERED  = 3,
    BATTLEARENA_ERR_NOT_OPEN        = 4,
    BATTLEARENA_ERR_PARTY_SIZE      = 14,
    BATTLEARENA_ERR_GUILD_LIST      = 15
};

static CMsg* AllocNotice()
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(OP_BATTLEARENA_NOTICE);
    return pMsg;
}

static void BroadcastToGameServers(CMsg* pMsg)
{
    if (!g_pMainProcess)
        BSAssertFail();
    CMainProcess::BroadcastToGameServers(pMsg, NULL);
}

static void WriteTypeAndKind(CMsg* pMsg, BYTE byType, BYTE byKind)
{
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&byKind, 1);
}

// ---------------------------------------------------------------------------------------------
// sBattleArenaTeam

sBattleArenaTeam::sBattleArenaTeam()
{
    m_wWorldID = 0;
    m_wLayerID = 0;
    m_byCount = 0;
    m_by1 = 0;
    m_dw20 = 0;
}

void sBattleArenaTeam::AddMember(DWORD dwJID)
{
    m_listMember.push_back(dwJID);
    ++m_byCount;
    CMsg* pMsg = AllocNotice();
    BYTE byType = BATTLEARENA_MEMBER_ADDED;
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&m_wWorldID, 2);
    pMsg->WriteBytes(&m_wLayerID, 2);
    pMsg->WriteBytes(&dwJID, 4);
    BroadcastToGameServers(pMsg);
}

DWORD sBattleArenaTeam::PopMember()
{
    DWORD dwJID = m_listMember.front();
    m_listMember.pop_front();
    --m_byCount;
    CMsg* pMsg = AllocNotice();
    BYTE byType = BATTLEARENA_MEMBER_REMOVED;
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&m_wWorldID, 2);
    pMsg->WriteBytes(&m_wLayerID, 2);
    pMsg->WriteBytes(&dwJID, 4);
    BroadcastToGameServers(pMsg);
    return dwJID;
}

// ---------------------------------------------------------------------------------------------
// CBattleArenaMgr

CBattleArenaMgr* g_pBattleArenaMgr = NULL;
CBattleArenaMgr g_BattleArenaMgr;

CBattleArenaMgr::CBattleArenaMgr()
{
    if (g_pBattleArenaMgr)
        BSAssertFail();
    g_pBattleArenaMgr = this;
    Reset();
}

CBattleArenaMgr::~CBattleArenaMgr()
{
    Clear();
    g_pBattleArenaMgr = NULL;
}

void CBattleArenaMgr::Reset()
{
    for (int i = 0; i < BATTLEARENA_KIND_COUNT; ++i)
    {
        m_adwParticipation[i] = 0;
        m_abClosed[i] = 1;
    }
    m_adwMatchActive[0] = 0;
    m_adwMatchActive[1] = 0;
    m_adwMatchActive[2] = 0;
}

void CBattleArenaMgr::Clear()
{
    for (int i = 0; i < BATTLEARENA_KIND_COUNT; ++i)
        m_adwParticipation[i] = 0;
    m_adwMatchActive[0] = 0;
    m_adwMatchActive[1] = 0;
    m_adwMatchActive[2] = 0;
}

WORD CBattleArenaMgr::GetActiveMatchFlags() const
{
    WORD wFlags = 0;
    if (m_adwMatchActive[0])
        wFlags = 0x10;
    if (m_adwMatchActive[1])
        wFlags |= 0x20;
    if (m_adwMatchActive[2])
        wFlags |= 0x40;
    return wFlags;
}

void CBattleArenaMgr::OnMsg_74D0(CMsg* pMsg)
{
    BYTE byType;
    *pMsg >> byType;
    switch (byType)
    {
    case 1:
    case 2:
        OnRegisterReq(pMsg, byType);
        break;

    case 3:     // GM commands
        {
            BYTE byEvent;
            *pMsg >> byEvent;
            if (byEvent == 6)
            {
                OnRegisterReq(pMsg, 1);
            }
            else if (byEvent == 7)
            {
                OnRegisterReq(pMsg, 2);
            }
            else
            {
                BYTE byKind = pMsg->ReadForwardOnly<BYTE>();
                OnScheduleEvent(byKind, byEvent);
            }
        }
        break;

    case 8:     // the participation time
        {
            DWORD dwJID;
            *pMsg >> dwJID;
            CMsg* pReply = AllocNotice();
            BYTE byReply = BATTLEARENA_PARTICIPATION_TIME;
            pReply->WriteBytes(&byReply, 1);
            pReply->WriteBytes(&dwJID, 4);
            pReply->WriteBytes(&m_dwParticipationTime, 4);
            BroadcastToGameServers(pReply);
        }
        break;

    case 9:
        {
            BYTE byKind;
            *pMsg >> byKind;
            SendEndPlay(byKind);
        }
        break;

    case 0x11:
        {
            BYTE byKind;
            *pMsg >> byKind;
            CloseArenaOnce(byKind);
        }
        break;
    }
}

void CBattleArenaMgr::OnRegisterReq(CMsg* pMsg, BYTE byAction)
{
    DWORD dwJID;
    BYTE byKind;
    BYTE byMatch;
    *pMsg >> dwJID;
    *pMsg >> byKind;
    *pMsg >> byMatch;

    BYTE byError = 0;
    if (byKind >= BATTLEARENA_KIND_COUNT || byMatch >= BATTLEARENA_MATCH_COUNT)
        byError = BATTLEARENA_ERR_INVALID;
    else if (!m_adwParticipation[byKind] || !m_adwMatchActive[byMatch])
        byError = BATTLEARENA_ERR_NOT_OPEN;
    else
    {
        if (!g_pMainProcess)
            BSAssertFail();
        if (byMatch <= 2 && !MainProcess_IsArenaMatchOpen(byMatch))
            byError = BATTLEARENA_ERR_INVALID;
    }
    if (byError)
    {
        pMsg->SkipRemaining();
        SendRegisterAck_Error(byAction, dwJID, byError);
        return;
    }

    if (byAction == 2)
    {
        OnUnregisterReq(byKind, dwJID, byMatch);
        return;
    }
    if (byAction != 1)
        return;

    switch (byKind)
    {
    case 0:     // random: value
        {
            BYTE byValue = 0;
            byValue = pMsg->ReadForwardOnly<BYTE>();
            if (IsUserRegistered(byMatch, byKind, dwJID))
                break;
            m_amapRandom[byMatch].insert(ByteMap::value_type(dwJID, byValue));
            SendRegisterAck_OK(1, dwJID, byKind, byMatch);
        }
        break;

    case 1:     // party: party ID
        {
            DWORD dwPartyID = pMsg->ReadForwardOnly<DWORD>();
            if (IsUserRegistered(byMatch, byKind, dwJID))
                break;
            void* pParty = PartyManager_FindParty(dwPartyID);
            if (!pParty)
            {
                SendRegisterAck_Error(1, dwJID, BATTLEARENA_ERR_INVALID);
                break;
            }
            if (Party_GetMemberCount(pParty) < 5)
            {
                SendRegisterAck_Error(1, dwJID, BATTLEARENA_ERR_PARTY_SIZE);
                break;
            }
            m_amapParty[byMatch].insert(DwordMap::value_type(dwJID, dwPartyID));
            SendRegisterAck_OK(1, dwJID, byKind, byMatch);
        }
        break;

    case 2:     // guild: guild ID and the JIDs of the members (up to 16)
        {
            DWORD dwGuildID = pMsg->ReadForwardOnly<DWORD>();
            if (IsUserRegistered(byMatch, byKind, dwJID))
                break;
            if (pMsg->GetUnreadSize() < 40)
            {
                pMsg->SkipRemaining();
                SendRegisterAck_Error(1, dwJID, BATTLEARENA_ERR_GUILD_LIST);
                break;
            }
            m_amapGuild[byMatch].insert(DwordMap::value_type(dwJID, dwGuildID));
            SendRegisterAck_OK(1, dwJID, byKind, byMatch);

            std::list<DWORD> listMember;
            BYTE byCount = 0;
            if (pMsg->GetUnreadSize() >= 4)
            {
                for (;;)
                {
                    DWORD dwMember = pMsg->ReadForwardOnly<DWORD>();
                    ++byCount;
                    listMember.push_back(dwMember);
                    if (byCount >= 16)
                    {
                        pMsg->SkipRemaining();
                        break;
                    }
                    if (pMsg->GetUnreadSize() < 4)
                        break;
                }
            }
            m_amapGuildMemberList[byMatch].insert(
                MemberListMap::value_type(dwGuildID, listMember));
        }
        break;

    case 3:     // job: value, job type
        {
            BYTE byValue = pMsg->ReadForwardOnly<BYTE>();
            BYTE byJob = pMsg->ReadForwardOnly<BYTE>();
            if (IsUserRegistered(byMatch, byKind, dwJID))
                break;
            if (byJob == 1 || byJob == 3)
                m_amapJobB[byMatch].insert(ByteMap::value_type(dwJID, byValue));
            else
                m_amapJobA[byMatch].insert(ByteMap::value_type(dwJID, byValue));
            SendRegisterAck_OK(1, dwJID, byKind, byMatch);
        }
        break;
    }
}

BOOL CBattleArenaMgr::IsUserRegistered(BYTE byMatch, BYTE byKind, DWORD dwJID)
{
    bool bFound;
    switch (byKind)
    {
    case 0:
        bFound = m_amapRandom[byMatch].find(dwJID) != m_amapRandom[byMatch].end();
        break;
    case 1:
        bFound = m_amapParty[byMatch].find(dwJID) != m_amapParty[byMatch].end();
        break;
    case 2:
        bFound = m_amapGuild[byMatch].find(dwJID) != m_amapGuild[byMatch].end();
        break;
    case 3:
        bFound = m_amapJobB[byMatch].find(dwJID) != m_amapJobB[byMatch].end()
                 || m_amapJobA[byMatch].find(dwJID) != m_amapJobA[byMatch].end();
        break;
    default:
        return FALSE;
    }
    if (!bFound)
        return FALSE;
    SendRegisterAck_Error(1, dwJID, BATTLEARENA_ERR_REGISTERED);
    return TRUE;
}

BOOL CBattleArenaMgr::OnUnregisterReq(BYTE byKind, DWORD dwJID, BYTE byMatch)
{
    switch (byKind)
    {
    case 0:
        {
            ByteMap::iterator it = m_amapRandom[byMatch].find(dwJID);
            if (it == m_amapRandom[byMatch].end())
                break;
            m_amapRandom[byMatch].erase(it);
            SendRegisterAck_OK(2, dwJID, 0, byMatch);
            return TRUE;
        }

    case 1:
        {
            DwordMap::iterator it = m_amapParty[byMatch].find(dwJID);
            if (it == m_amapParty[byMatch].end())
                break;
            m_amapParty[byMatch].erase(it);
            SendRegisterAck_OK(2, dwJID, 1, byMatch);
            return TRUE;
        }

    case 2:
        {
            DwordMap::iterator it = m_amapGuild[byMatch].find(dwJID);
            if (it == m_amapGuild[byMatch].end())
                break;
            DWORD dwGuildID = it->second;
            m_amapGuild[byMatch].erase(it);
            SendRegisterAck_OK(2, dwJID, 2, byMatch);
            MemberListMap::iterator itList = m_amapGuildMemberList[byMatch].find(dwGuildID);
            if (itList != m_amapGuildMemberList[byMatch].end())
            {
                itList->second.clear();
                m_amapGuildMemberList[byMatch].erase(itList);
            }
            else
            {
                BSAssertFail();
            }
            return TRUE;
        }

    case 3:
        {
            ByteMap::iterator it = m_amapJobB[byMatch].find(dwJID);
            if (it != m_amapJobB[byMatch].end())
            {
                m_amapJobB[byMatch].erase(it);
            }
            else
            {
                it = m_amapJobA[byMatch].find(dwJID);
                if (it == m_amapJobA[byMatch].end())
                    break;
                m_amapJobA[byMatch].erase(it);
            }
            SendRegisterAck_OK(2, dwJID, 3, byMatch);
            return TRUE;
        }
    }
    SendRegisterAck_Error(2, dwJID, BATTLEARENA_ERR_NOT_REGISTERED);
    return FALSE;
}

void CBattleArenaMgr::OnScheduleEvent(BYTE byKind, BYTE byEvent)
{
    switch (byEvent)
    {
    case 1:
        m_dwParticipationTime = 1200000;
        SendParticipationStarted(byKind);
        break;
    case 2:
        SendParticipationEnded(byKind);
        break;
    case 3:
        CheckAndGo(byKind);
        break;
    case 4:
        SendEndPlay(byKind);
        break;
    case 5:
        OnGameEnded(byKind);
        break;
    }
}

void CBattleArenaMgr::SendRegisterAck_OK(BYTE byAction, DWORD dwJID, BYTE byKind, BYTE byMatch)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(OP_BATTLEARENA_REGISTER_ACK);
    BYTE byResult = 0;
    pMsg->WriteBytes(&byAction, 1);
    pMsg->WriteBytes(&dwJID, 4);
    pMsg->WriteBytes(&byResult, 1);
    pMsg->WriteBytes(&byKind, 1);
    pMsg->WriteBytes(&byMatch, 1);
    BroadcastToGameServers(pMsg);
}

void CBattleArenaMgr::SendRegisterAck_Error(BYTE byAction, DWORD dwJID, BYTE byError)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(OP_BATTLEARENA_REGISTER_ACK);
    pMsg->WriteBytes(&byAction, 1);
    pMsg->WriteBytes(&dwJID, 4);
    pMsg->WriteBytes(&byError, 1);
    BroadcastToGameServers(pMsg);
}

void CBattleArenaMgr::SendKickUser(DWORD dwJID, BYTE byReason)
{
    CMsg* pMsg = AllocNotice();
    BYTE byType = BATTLEARENA_KICK;
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&dwJID, 4);
    pMsg->WriteBytes(&byReason, 1);
    BroadcastToGameServers(pMsg);
}

void CBattleArenaMgr::SendParticipationStarted(BYTE byKind)
{
    if (m_adwParticipation[byKind])
        return;
    m_dtParticipationBegin = CDateTime::GetCurrent();
    LogPrintf(0, "BattleArena: Participation started : %d", byKind);
    m_adwParticipation[byKind] = 1;
    CMsg* pMsg = AllocNotice();
    WORD wFlags = GetActiveMatchFlags();
    DWORD dwTime = (DWORD)m_dtParticipationBegin.GetTime();
    WriteTypeAndKind(pMsg, BATTLEARENA_PARTICIPATION_BEGIN, byKind);
    pMsg->WriteBytes(&dwTime, 4);
    pMsg->WriteBytes(&wFlags, 2);
    BroadcastToGameServers(pMsg);
}

void CBattleArenaMgr::SendParticipationEnded(BYTE byKind)
{
    if (m_adwParticipation[byKind] != 1)
        return;
    LogPrintf(0, "BattleArena: Participation ended : %d", byKind);
    m_adwParticipation[byKind] = 0;
    CMsg* pMsg = AllocNotice();
    WriteTypeAndKind(pMsg, BATTLEARENA_PARTICIPATION_END, byKind);
    BroadcastToGameServers(pMsg);
}

void CBattleArenaMgr::SendEndPlay(BYTE byKind)
{
    LogPrintf(0, "BattleArena: End Play %d", byKind);
    CMsg* pMsg = AllocNotice();
    WriteTypeAndKind(pMsg, BATTLEARENA_END_PLAY, byKind);
    BroadcastToGameServers(pMsg);
}

void CBattleArenaMgr::WriteParticipationStarted(CMsg* pMsg, BYTE byKind)
{
    if (m_adwParticipation[byKind])
        return;
    m_dtParticipationBegin = CDateTime::GetCurrent();
    m_adwParticipation[byKind] = 1;
    WORD wFlags = GetActiveMatchFlags();
    DWORD dwTime = (DWORD)m_dtParticipationBegin.GetTime();
    WriteTypeAndKind(pMsg, BATTLEARENA_PARTICIPATION_BEGIN, byKind);
    pMsg->WriteBytes(&dwTime, 4);
    pMsg->WriteBytes(&wFlags, 2);
    LogPrintf(0, "BattleArena: Participation started : %d", byKind);
}

void CBattleArenaMgr::WriteParticipationEnded(CMsg* pMsg, BYTE byKind)
{
    if (m_adwParticipation[byKind] != 1)
        return;
    m_adwParticipation[byKind] = 0;
    WriteTypeAndKind(pMsg, BATTLEARENA_PARTICIPATION_END, byKind);
    LogPrintf(0, "BattleArena: Participation ended : %d", byKind);
}

void CBattleArenaMgr::WriteLetsParticipate(CMsg* pMsg, BYTE byKind)
{
    WriteTypeAndKind(pMsg, BATTLEARENA_LETS_PARTICIPATE, byKind);
    LogPrintf(0, "BattleArena: Let's Participate Battle Arena %d!", byKind);
}

void CBattleArenaMgr::Write5MinRemain(CMsg* pMsg, BYTE byKind)
{
    WriteTypeAndKind(pMsg, BATTLEARENA_5MIN_LEFT, byKind);
    LogPrintf(0, "BattleArena: 5 Minute remains to begin game : %d", byKind);
}

void CBattleArenaMgr::Write1MinRemain(CMsg* pMsg, BYTE byKind)
{
    WriteTypeAndKind(pMsg, BATTLEARENA_1MIN_LEFT, byKind);
    LogPrintf(0, "BattleArena: 1 Minute remains to begin game : %d", byKind);
}

void CBattleArenaMgr::WriteEndPlay(CMsg* pMsg, BYTE byKind)
{
    WriteTypeAndKind(pMsg, BATTLEARENA_END_PLAY, byKind);
    LogPrintf(0, "BattleArena: End Play %d", byKind);
}

void CBattleArenaMgr::WriteStopResurrect(CMsg* pMsg, BYTE byKind)
{
    WriteTypeAndKind(pMsg, BATTLEARENA_STOP_RESURRECT, byKind);
    LogPrintf(0, "BattleArena: Stop Resurrect %d", byKind);
}

void CBattleArenaMgr::OnScheduledGameEnd(CMsg* pMsg, BYTE byKind)
{
    SendParticipationEnded(byKind);
    int i;
    switch (byKind)
    {
    case 0:
        for (i = 0; i < BATTLEARENA_MATCH_COUNT; ++i)
            m_amapRandom[i].clear();
        break;
    case 1:
        for (i = 0; i < BATTLEARENA_MATCH_COUNT; ++i)
        {
            m_amapParty[i].clear();
            m_amapParty19C[i].clear();
            m_amapPartyLevel[i].clear();
            m_alistParty[i].clear();
            for (GroupMap::iterator it = m_amapPartyMember[i].begin();
                 it != m_amapPartyMember[i].end(); ++it)
                it->second.clear();
            m_amapPartyMember[i].clear();
        }
        break;
    case 2:
        for (i = 0; i < BATTLEARENA_MATCH_COUNT; ++i)
        {
            m_amapGuild[i].clear();
            m_amapGuild250[i].clear();
            m_amapGuildLevel[i].clear();
            m_alistGuild[i].clear();
            for (GroupMap::iterator it = m_amapGuildMember[i].begin();
                 it != m_amapGuildMember[i].end(); ++it)
                it->second.clear();
            m_amapGuildMember[i].clear();
            m_amapGuildMemberList[i].clear();
        }
        break;
    case 3:
        for (i = 0; i < BATTLEARENA_MATCH_COUNT; ++i)
        {
            m_amapJobB[i].clear();
            m_amapJobA[i].clear();
        }
        break;
    }
    WriteTypeAndKind(pMsg, BATTLEARENA_GAME_END, byKind);
    LogPrintf(0, "BattleArena: Game Ended : %d", byKind);
}

RefGameWorld* CBattleArenaMgr::GetRefGameWorld(BYTE byMatchType, BYTE byMatch)
{
    static const char* const s_apszWorld[3][3] =
    {
        { "INS_ARENA_OCCUPY_8", "INS_ARENA_OCCUPY_16", "INS_ARENA_OCCUPY_32" },
        { "INS_ARENA_SCORE_8", "INS_ARENA_SCORE_16", "INS_ARENA_SCORE_32" },
        { "INS_ARENA_FLAG_8", "INS_ARENA_FLAG_16", "INS_ARENA_FLAG_32" }
    };
    if (byMatchType > 2 || byMatch < 1 || byMatch > 3)
        return NULL;
    return CReferenceData::GetInstance()->FindGameWorldByCodeName(
        std::string(s_apszWorld[byMatchType][byMatch - 1]));
}

BOOL CBattleArenaMgr::OpenArenaLayer(sBattleArenaTeam* pTeam, BYTE byMatchType, BYTE byMatch)
{
    RefGameWorld* pRef = GetRefGameWorld(byMatchType, byMatch);
    if (!pRef)
    {
        LogPrintf(0x2000000, "BattleArena: RefGameWorld is NULL %d %d", byMatchType, byMatch);
        BSAssertFail();
        return FALSE;
    }

    sGameWorldCreateParam param;
    WORD wWorldID = (WORD)pRef->ID;
    BYTE byResult = 1;
    param.m_dw0 = 0;
    param.m_b4 = 7;
    param.m_dw8 = 0;
    param.m_b12 = 1;
    param.m_b13 = 0;
    param.m_dwDuration = 3600000;
    param.m_dwEmptyRemainTime = 120000;
    param.m_b24 = 0;
    CGameWorld* pWorld = CGameWorldMgr::GetInstance()->FindWorld(wWorldID);
    if (!pWorld)
    {
        LogPrintf(0x2000000, "Failed to find GameWorld!!");
        return FALSE;
    }
    GameWorldLayer* pLayer = CGameWorldMgr::GetInstance()->OpenLayer(&byResult, wWorldID, &param);
    if (!pLayer)
    {
        LogPrintf(0x2000000, "Failed to create new game layer (for BattleArena)!! error[%d]",
                  byResult);
        return FALSE;
    }
    WORD wOpened = (WORD)pWorld->m_pRef->ID;
    CGameWorldMgr::GetInstance();
    CGameWorldMgr::BroadcastOpened(wOpened, pLayer);
    pTeam->m_wWorldID = (WORD)pWorld->m_pRef->ID;
    pTeam->m_wLayerID = pLayer->m_wLayerID;
    return TRUE;
}

void CBattleArenaMgr::CloseArenaLayers(BYTE byKind)
{
    for (int i = 0; i < BATTLEARENA_MATCH_COUNT; ++i)
    {
        TeamVector& vecTeam = m_avecTeam[byKind][i];
        for (TeamVector::iterator it = vecTeam.begin(); it != vecTeam.end(); ++it)
        {
            CGameWorld* pWorld = CGameWorldMgr::GetInstance()->FindWorld(it->m_wWorldID);
            if (pWorld && pWorld->LayerAt(it->m_wLayerID))
            {
                CGameWorldMgr::GetInstance();
                CGameWorldMgr::BroadcastClosed(it->m_wWorldID, it->m_wLayerID);
                it->m_wLayerID = 0;
                it->m_wWorldID = 0;
            }
        }
        vecTeam.erase(vecTeam.begin(), vecTeam.end());
    }
}

void CBattleArenaMgr::CloseArenaOnce(BYTE byKind)
{
    if (m_abClosed[byKind])
        return;
    CloseArenaLayers(byKind);
    m_abClosed[byKind] = 1;
}

void CBattleArenaMgr::CheckAndGo(BYTE byKind)
{
    LogPrintf(0, "BattleArena: CheckAndGo %d", byKind);
    switch (byKind)
    {
    case 0:
        CheckAndGoRandom();
        break;
    case 1:
        CheckAndGoParty();
        break;
    case 2:
        CheckAndGoGuild();
        break;
    case 3:
        CheckAndGoJob();
        break;
    }
}

// ---------------------------------------------------------------------------------------------
// The battle arena schedule jobs

static CBattleArenaMgr* GetBattleArenaMgr()
{
    if (!g_pBattleArenaMgr)
        BSAssertFail();
    return g_pBattleArenaMgr;
}

BOOL CShard_BattleArenaScheduleParticipation::OnBegin() // 0x677e50
{
    if (g_pShardScheduleExecutor->FindJobByName(m_Info.strDefine))
        return FALSE;
    m_dwRunning = 1;
    m_dwNoticeTick = GetTickCount();
    BeginNotify(NULL);
    GetBattleArenaMgr()->WriteParticipationStarted(m_pNotifyMsg, m_byKind);
    BroadcastNotify();
    LogSchedule("Scheduleing : BattleArena(%d) Request is Start ......!", m_byKind);
    return TRUE;
}

BOOL CShard_BattleArenaScheduleParticipation::OnTick(CDateTime& /*now*/)  // 0x678000
{
    if (!m_dwRunning)
        return TRUE;
    // The notices of the tick go out as state 2.
    NewSchedule::sScheduleInfo info = m_Info;
    info.bBegin = 2;
    DWORD dwTick = GetTickCount();
    if (dwTick - m_dwNoticeTick > 600000)
    {
        BeginNotify(&info);
        GetBattleArenaMgr();
        CBattleArenaMgr::WriteLetsParticipate(m_pNotifyMsg, m_byKind);
        BroadcastNotify();
        m_dwNoticeTick = dwTick;
    }
    return TRUE;
}

void CShard_BattleArenaScheduleParticipation::OnEnd(CDateTime& /*now*/)   // 0x6780e0
{
    m_dwRunning = 0;
    BeginNotify(NULL);
    GetBattleArenaMgr()->WriteParticipationEnded(m_pNotifyMsg, m_byKind);
    BroadcastNotify();
    LogSchedule("Scheduleing : BattleArena(%d) Request is End ......!", m_byKind);
}

BOOL CShard_BattleArenaSchedule::OnBegin()              // 0x6782c0
{
    if (g_pShardScheduleExecutor->FindJobByName(m_Info.strDefine))
        return FALSE;
    DWORD dwBegin = m_Info.dwBegin;
    m_dwBegin = dwBegin;
    m_dwOneMinuteLeft = dwBegin + 240;
    m_dwGameStart = dwBegin + 300;
    m_byStage = 0;
    BeginNotify(NULL);
    GetBattleArenaMgr();
    CBattleArenaMgr::Write5MinRemain(m_pNotifyMsg, m_byKind);
    BroadcastNotify();
    LogSchedule("Scheduleing : BattleArena(%d) is Start Before 5 Min......!", m_byKind);
    return TRUE;
}

BOOL CShard_BattleArenaSchedule::OnTick(CDateTime& now) // 0x678480
{
    // The notices of the tick go out as state 2.
    NewSchedule::sScheduleInfo info = m_Info;
    info.bBegin = 2;
    if ((DWORD)now.GetTime() > m_dwOneMinuteLeft && !m_byStage)
    {
        BeginNotify(&info);
        GetBattleArenaMgr();
        CBattleArenaMgr::Write1MinRemain(m_pNotifyMsg, m_byKind);
        BroadcastNotify();
        m_byStage = 1;
        LogPrintf(g_nScheduleLogMode != 1 ? 0 : 0x2000001,
                  "Scheduleing : BattleArena(%d) is Start Before 1 Min......!", m_byKind);
    }
    DWORD dwNow = (DWORD)now.GetTime();
    if (dwNow > m_dwGameStart && m_byStage == 1)
    {
        m_dwRunning = 1;
        m_bEndPlaySent = 0;
        m_dwCheckInterval = 60000;
        m_dwCheckTick = GetTickCount();
        // The play time: ms from the game start to the end of the period.
        GetBattleArenaMgr()->SetParticipationTime(1000 * (info.dwEnd - m_dwGameStart));
        GetBattleArenaMgr()->CheckAndGo(m_byKind);
        m_byStage = 2;
        LogPrintf(g_nScheduleLogMode != 1 ? 0 : 0x2000001,
                  "Scheduleing : BattleArena(%d) is Start ......!", m_byKind);
    }
    // The check tick is not renewed: once the interval has passed, every tick checks.
    if (m_dwRunning == 1 && m_byStage == 2 && !m_bEndPlaySent
        && GetTickCount() - m_dwCheckTick > m_dwCheckInterval)
    {
        DWORD dwRemain = GetRemainSeconds(now);
        if (dwRemain < 60)
            m_dwCheckInterval = 1000;
        if (!m_bStopResurrectSent && dwRemain < 25)
        {
            BeginNotify(&info);
            GetBattleArenaMgr();
            CBattleArenaMgr::WriteStopResurrect(m_pNotifyMsg, m_byKind);
            BroadcastNotify();
            m_bStopResurrectSent = 1;
        }
        if (dwRemain < 15)
        {
            BeginNotify(&info);
            GetBattleArenaMgr();
            CBattleArenaMgr::WriteEndPlay(m_pNotifyMsg, m_byKind);
            BroadcastNotify();
            m_bEndPlaySent = 1;
        }
    }
    return TRUE;
}

void CShard_BattleArenaSchedule::OnEnd(CDateTime& /*now*/)     // 0x678720
{
    m_dwRunning = 0;
    BeginNotify(NULL);
    GetBattleArenaMgr()->OnScheduledGameEnd(m_pNotifyMsg, m_byKind);
    BroadcastNotify();
    LogSchedule("Scheduleing : BattleArena(%d) is End ......!", m_byKind);
}

DWORD CShard_BattleArenaSchedule::GetRemainSeconds(CDateTime now)
{
    CDateTime end((__time32_t)m_Info.dwEnd);
    return (DWORD)end.GetTime() - (DWORD)now.GetTime();
}

BOOL CShard_BattleArenaMatchSchedule::OnBegin()         // 0x6788c0
{
    if (g_pShardScheduleExecutor->FindJobByName(m_Info.strDefine))
        return FALSE;
    GetBattleArenaMgr()->SetMatchActive(m_byMatchType, 1);
    LogSchedule(NULL);
    return TRUE;
}

void CShard_BattleArenaMatchSchedule::OnEnd(CDateTime& /*now*/)    // 0x678a20
{
    GetBattleArenaMgr()->SetMatchActive(m_byMatchType, 0);
    LogSchedule(NULL);
}
