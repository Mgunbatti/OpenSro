#include "ShardManager/Guild.h"

#include <strsafe.h>

#include "BSLib/Util/TimeSpan.h"
#include "BSNet/NetEngine.h"
#include "ServerCommon/ReferenceData.h"
#include "ServerFramework/ServerDataExtern.h"

// Connection index of the guild statements (the original passes 7 everywhere).
#define GUILD_DB_CONN   7

const int g_anGuildLevelUpGP[6] = { 50, 5400, 50400, 135000, 378000, 0 };

static CDBSessionMgr* ShardDBSession()
{
    return GetSingleton_CShardDB()->GetSession();
}

// The UPDATE of one record group on the guild connection: 0 when nothing could be built, -1 when
// the statement failed, 1 on success.
static int ExecRecordUpdate(CDBRecord* pRecord, int nGroup)
{
    std::string strSql;
    if (!pRecord->BuildUpdate(strSql, nGroup))
        return 0;
    if (CDatabase::ExecDirectWithReturnValue(ShardDBSession(), GUILD_DB_CONN, strSql.c_str(), 0,
                                             NULL) & ~1)
        return -1;
    return 1;
}

// A stored procedure with an int return value: FALSE when the statement failed.
static BOOL ExecProcedure(const char* pszSql, int* pnResult)
{
    __int64 llValue = 0;
    if (CDatabase::ExecDirectWithReturnValue(ShardDBSession(), GUILD_DB_CONN, pszSql,
                                             DBEXEC_RETURN_INT | 1, &llValue) & ~1)
        return FALSE;
    *pnResult = (int)llValue;
    return TRUE;
}

static CMsg* AllocNotify(BYTE bSubType)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(GUILD_MSG_NOTIFY);
    pMsg->WriteBytes(&bSubType, 1);
    return pMsg;
}

static int BroadcastToGameServers(CMsg* pMsg)
{
    if (!g_pMainProcess)
        BSAssertFail();
    return g_pMainProcess->BroadcastToGameServers(pMsg, 0);
}

// The ack goes to the session the request came from and is released.
static void SendAck(DWORD dwSessionID, CMsg* pAck)
{
    g_pNetEngine->SendMsg(dwSessionID, pAck);
    g_pNetEngine->ReleaseMsg(pAck);
}

static void SendAckResult(DWORD dwSessionID, DWORD dwRequestID, WORD wResult)
{
    CMsg* pAck = CGuildManager::AllocAck(dwRequestID);
    BYTE bResult;
    if (wResult == GUILD_OK)
    {
        bResult = 1;
        pAck->WriteBytes(&bResult, 1);
    }
    else
    {
        bResult = 2;
        pAck->WriteBytes(&bResult, 1);
        pAck->WriteBytes(&wResult, 2);
    }
    SendAck(dwSessionID, pAck);
}

// ---------------------------------------------------------------------------------------------
// Guild war record helpers

void GuildWarRecord_Update(CInstanceGuildWar* pWar, DWORD dwGuild1,
                           const GUILD_WAR_REQUEST& request, DWORD dwGuild2)
{
    DBRecord_SetColumn(pWar, CInstanceGuildWar::s_TableInfo, pWar->VictoryPointIndex,
                       request.bVictoryPointIndex, 2);
    DBRecord_SetColumn(pWar, CInstanceGuildWar::s_TableInfo, pWar->LodgedGold,
                       (int)(2 * request.dwLodgedGold), 2);
    CDateTime now = CDateTime::GetCurrent();
    TIMESTAMP_STRUCT& end = pWar->WarEndTime;
    if (request.dwDuration >= 0x7FFFFFFF)
    {
        end.second = 0;
        end.minute = 0;
        end.hour = 0;
        end.day = 0;
        end.month = 0;
        end.year = 0;
    }
    else
    {
        DWORD dw = request.dwDuration;
        CTimeSpan span((LONG)((dw >> 15) & 0x1F), (dw >> 10) & 0x1F, (dw >> 20) & 0x3F,
                       dw >> 26);
        CDateTime dtEnd = now + span;
        end.year = (SQLSMALLINT)dtEnd.GetYear();
        end.month = (SQLUSMALLINT)dtEnd.GetMonth();
        end.day = (SQLUSMALLINT)dtEnd.GetDay();
        end.hour = (SQLUSMALLINT)dtEnd.GetHour();
        end.minute = (SQLUSMALLINT)dtEnd.GetMinute();
        end.second = (SQLUSMALLINT)dtEnd.GetSecond();
    }
    const CDBTableInfo& info = CInstanceGuildWar::s_TableInfo;
    DBRecord_SetColumn(pWar, info, pWar->Guild[0], (int)dwGuild1, 3);
    DBRecord_SetColumn(pWar, info, pWar->Guild[1], (int)dwGuild2, 3);
    DBRecord_SetColumn(pWar, info, pWar->PointGain[0], 0, 3);
    DBRecord_SetColumn(pWar, info, pWar->PointGain[1], 0, 3);
    DBRecord_SetColumn(pWar, info, pWar->Data1, 0, 4);
    DBRecord_SetColumn(pWar, info, pWar->Data2, 0, 4);
}

void GuildWarRecord_SetPointGain(CInstanceGuildWar* pWar, DWORD nIndex, int nValue)
{
    if (nIndex >= 2)
        BSAssertFail();
    DBRecord_SetColumn(pWar, CInstanceGuildWar::s_TableInfo, pWar->PointGain[nIndex], nValue, 3);
}

void GuildWarRecord_WriteToMsg(const CInstanceGuildWar* pWar, CMsg* pMsg)
{
    pMsg->WriteBytes(&pWar->ID, 4);
    const TIMESTAMP_STRUCT& end = pWar->WarEndTime;
    DWORD dwEnd = 0;
    if (end.year || end.month || end.day || end.hour || end.minute || end.second)
        RefObj_PackTypeID(dwEnd, (const WORD*)&end);
    else
        dwEnd = 0;
    pMsg->WriteBytes(&dwEnd, 4);
    pMsg->WriteBytes(&pWar->VictoryPointIndex, 1);
    pMsg->WriteBytes(&pWar->LodgedGold, 4);
    pMsg->WriteBytes(&pWar->Guild[0], 4);
    pMsg->WriteBytes(&pWar->Guild[1], 4);
    pMsg->WriteBytes(&pWar->PointGain[0], 4);
    pMsg->WriteBytes(&pWar->PointGain[1], 4);
}

DWORD CCharacter_GetTrainingCampID(CCharacter* pCharacter)
{
    DWORD dwCharID = pCharacter->GetPCRecord()->CharID;
    if (!g_pTrainingCampManager)
        BSAssertFail();
    CTrainingCampCommon* pCamp = TrainingCampMgr_FindCampByChar(dwCharID,
                                                                g_pTrainingCampManager);
    if (!pCamp)
        return 0;
    return pCamp->GetID();
}

// ---------------------------------------------------------------------------------------------
// CAlliance (DB side)

void CAlliance::ReleaseRecord()
{
    if (!m_pRecord)
        return;
    if (!g_pShardDB)
        BSAssertFail();
    CDBTable* pTable = g_pShardDB->GetTable(SHARD_DB_TABLE_ALLIEDCLANS);
    pTable->FreeRecord(m_pRecord);
    m_pRecord = NULL;
}

WORD CAlliance::RemoveGuildSlot(DWORD dwGuildID)
{
    CInstanceAlliedClan* pRecord = m_pRecord;
    BYTE i = 0;
    while (pRecord->Ally[i] != (int)dwGuildID)
    {
        if (++i >= 8)
            return GUILD_ERR_NOT_IN_UNION;
    }
    if (i >= 8)
        BSAssertFail();
    DBRecord_SetColumn(pRecord, CInstanceAlliedClan::s_TableInfo, pRecord->Ally[i], 0, 2);
    if (ExecRecordUpdate(m_pRecord, 2) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CAlliance::DBRemove()
{
    if (!m_pRecord)
        return GUILD_ERR_NO_RECORD;
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _AllianceRemove (%d)}", m_pRecord->ID))
    {
        BSAssertFail();
        return GUILD_ERR_NO_RECORD;
    }
    int nResult;
    if (!ExecProcedure(szSql, &nResult) || nResult <= 0)
        return GUILD_ERR_FAILED;
    for (int i = 0; i < 8; ++i)
    {
        DWORD dwGuildID = m_pRecord->Ally[i];
        if (dwGuildID)
        {
            CGuild* pGuild = GetSingleton_CGuildManager()->FindGuildT(dwGuildID);
            if (pGuild)
                pGuild->ClearAllianceID();
        }
    }
    ReleaseRecord();
    return GUILD_OK;
}

WORD CAlliance::SetCrestRev(int nCrestRev)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    m_pRecord->SetColumn(&InstanceAlliedClanData::CurCrestRev, nCrestRev);
    if (nCrestRev)
        m_pRecord->SetColumn(&InstanceAlliedClanData::LastCrestRev, nCrestRev);
    if (ExecRecordUpdate(m_pRecord, 3) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

// ---------------------------------------------------------------------------------------------
// CGuild

void CGuild::OnMemberMsgRelay(CMsg* pMsg)
{
    if (!m_pRecord)
    {
        BSAssertFail();
        return;
    }
    std::string strFrom;
    std::string strText;
    BYTE bType;
    *pMsg >> bType;
    *pMsg >> strFrom;
    *pMsg >> strText;
    CMsg* pNotify = AllocNotify(7);
    pNotify->WriteBytes(&m_pRecord->ID, 4);
    pNotify->WriteBytes(&bType, 1);
    *pNotify << strFrom.c_str();
    *pNotify << strText;
    BroadcastToGameServers(pNotify);
}

int CGuild::RefreshMemberStatus(DWORD dwCharID, BYTE bType, DWORD dwValue, CUser* pUser)
{
    if (bType == 1)
    {
        CGuildMember* pMember = FindMember(dwCharID);
        if (pMember)
        {
            char szSql[0xC00];
            if (StringCchPrintfA(szSql, 0xC00, "{CALL _GUILD_REFRESHMEMBERSTATUS (%d, %d, %d, %d)}",
                                 GetID(), dwCharID, 1, dwValue))
            {
                BSAssertFail();
            }
            if (!g_pShardDB)
                BSAssertFail();
            CDatabase::ExecDirectWithReturnValue(g_pShardDB->GetSession(), GUILD_DB_CONN, szSql,
                                                 1, NULL);
            pMember->m_pRecord->SetColumn(&InstanceGuildMemberData::CharLevel, (BYTE)dwValue);
        }
    }
    else if (bType == 2)
    {
        CGuildMember* pMember = FindMember(dwCharID);
        if (pMember)
        {
            if (dwValue == 0)
            {
                if (!pUser)
                    BSAssertFail();
                pMember->m_pUser = pUser;
            }
            else if (dwValue == 1)
            {
                if (m_pLockMember == pMember)
                {
                    ResetLockMember();
                    GetSingleton_CGuildManager();
                    CGuildManager::SendMemberOnlineState(GetID(), dwCharID, 2);
                }
                pMember->m_pUser = NULL;
            }
        }
    }
    CMsg* pNotify = AllocNotify(6);
    DWORD dwGuildID = GetID();
    pNotify->WriteBytes(&dwGuildID, 4);
    pNotify->WriteBytes(&dwCharID, 4);
    pNotify->WriteBytes(&bType, 1);
    pNotify->WriteBytes(&dwValue, 4);
    return BroadcastToGameServers(pNotify);
}

WORD CGuild::DBDeleteMember(DWORD dwGuildID, DWORD dwCharID, BYTE bReason)
{
    if (!dwGuildID || !dwCharID)
        return GUILD_ERR_ARGUMENT;
    CGuildMember* pMember = FindMember(dwCharID);
    if (!pMember)
        return GUILD_ERR_MEMBER_NOT_FOUND;
    if (pMember->IsMaster())
        return GUILD_ERR_MASTER_CANNOT_LEAVE;
    if (bReason != 3)
    {
        char szSql[0xC00];
        if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _GUILD_DELMEMBER (%d, %d)}", dwGuildID,
                             dwCharID))
        {
            BSAssertFail();
            return GUILD_ERR_ARGUMENT;
        }
        int nResult;
        if (!ExecProcedure(szSql, &nResult))
            return GUILD_ERR_FAILED;
        if (nResult <= 0)
        {
            if (nResult == -1 || nResult == -1001)
                return GUILD_ERR_MEMBER_NOT_FOUND;
            if (nResult == -1000)
                return GUILD_ERR_GUILD_NOT_FOUND;
            return GUILD_ERR_FAILED;
        }
    }
    if (pMember->m_pUser)
        pMember->m_pUser->SetGuildID(0);
    m_mapMember.erase(m_mapMember.find(dwCharID));
    GuildMember_FreePtr(pMember);
    return GUILD_OK;
}

WORD CGuild::DBDeleteGuild(DWORD dwCharID)
{
    if (!m_pRecord)
        return GUILD_ERR_NO_RECORD;
    if (!m_pRecord->ID)
        return GUILD_ERR_FAILED;
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _GUILD_DELETEGUILD (%d, %d)}", m_pRecord->ID,
                         dwCharID))
    {
        BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    int nResult;
    if (!ExecProcedure(szSql, &nResult))
        return GUILD_ERR_FAILED;
    if (nResult <= 0)
    {
        if (nResult == -2000)
            return GUILD_ERR_NOT_MASTER;
        return nResult != -1000 ? GUILD_ERR_FAILED : GUILD_ERR_GUILD_NOT_FOUND;
    }
    for (GuildMemberMap::iterator it = m_mapMember.begin(); it != m_mapMember.end(); ++it)
    {
        CGuildMember* pMember = it->second;
        if (pMember->m_pUser)
            pMember->m_pUser->SetGuildID(0);
        GuildMember_FreePtr(pMember);
    }
    m_mapMember.clear();
    return GUILD_OK;
}

WORD CGuild::DonateGP(int nAmount, DWORD dwCharID)
{
    if (nAmount <= 0)
        return GUILD_ERR_FAILED;
    if (!m_pRecord)
        return GUILD_ERR_NO_RECORD;
    if (!m_pRecord->ID)
        return GUILD_ERR_FAILED;
    CGuildMember* pMember = FindMember(dwCharID);
    if (!pMember)
        return GUILD_ERR_MEMBER_NOT_FOUND;
    if (m_pRecord->Lvl > 5)
        return GUILD_ERR_FAILED;
    m_pRecord->SetColumn(&InstanceGuildData::GatheredSP, m_pRecord->GatheredSP + nAmount);
    pMember->m_pRecord->SetColumn(&InstanceGuildMemberData::GP_Donation,
                                  pMember->m_pRecord->GP_Donation + nAmount);
    if (ExecRecordUpdate(m_pRecord, 3) != 1 || ExecRecordUpdate(pMember->m_pRecord, 2) != 1)
    {
        BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    return GUILD_OK;
}

WORD CGuild::LevelUp()
{
    if (!m_pRecord)
        return GUILD_ERR_NO_RECORD;
    BYTE bLevel = m_pRecord->Lvl;
    if (bLevel == 5)
        return GUILD_ERR_MAX_LEVEL;
    if (bLevel < 1)
        return GUILD_ERR_FAILED;
    int nCost = g_anGuildLevelUpGP[bLevel];
    if (m_pRecord->GatheredSP < nCost)
        return GUILD_ERR_NOT_ENOUGH_GP;
    m_pRecord->SetColumn(&InstanceGuildData::Lvl, (BYTE)(bLevel + 1));
    m_pRecord->SetColumn(&InstanceGuildData::GatheredSP, m_pRecord->GatheredSP - nCost);
    if (ExecRecordUpdate(m_pRecord, -1) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CGuild::UpdateNotice(const std::string& strTitle, const std::string& strNotice)
{
    if (!m_pRecord)
        return GUILD_ERR_NO_RECORD;
    if (!strTitle.size() || strTitle.size() > 0x80)
        return GUILD_ERR_NOTICE_TITLE;
    if (!strNotice.size() || strNotice.size() > 0x800)
        return GUILD_ERR_NOTICE_BODY;
    m_pRecord->SetColumn(&InstanceGuildData::MasterCommentTitle, strTitle);
    m_pRecord->SetColumn(&InstanceGuildData::MasterComment, strNotice);
    if (ExecRecordUpdate(m_pRecord, 5) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CGuild::SetAllianceID(CAlliance* pAlliance, BYTE bSave)
{
    m_pRecord->SetColumn(&InstanceGuildData::Alliance, pAlliance->m_pRecord->ID);
    if (bSave != 1)
        return GUILD_OK;
    if (ExecRecordUpdate(m_pRecord, 4) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CGuild::LeaveUnion(BYTE* pbResult)
{
    if (!m_pRecord)
        return GUILD_ERR_NO_RECORD;
    DWORD dwAllianceID = m_pRecord->Alliance;
    CAlliance* pAlliance = GetSingleton_CGuildManager()->FindAlliance(dwAllianceID);
    if (!pAlliance)
        return GUILD_ERR_NOT_IN_UNION;
    DWORD dwMasterGuildID = pAlliance->GetMasterGuildID();
    WORD wResult = pAlliance->RemoveGuildSlot(GetID());
    if (wResult != GUILD_OK)
        return wResult;
    m_pRecord->SetColumn(&InstanceGuildData::Alliance, 0);
    if (dwMasterGuildID == GetID() || pAlliance->CountGuilds() <= 1)
    {
        *pbResult = 3;
        return GUILD_OK;
    }
    if (ExecRecordUpdate(m_pRecord, 4) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CGuild::ClearAllianceID()
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    m_pRecord->SetColumn(&InstanceGuildData::Alliance, 0);
    return GUILD_OK;
}

WORD CGuild::DelegateMaster(CInstanceGuildMember* pNewMaster,
                            CInstanceGuildMember* pOldMaster)
{
    if (!m_pRecord || !pOldMaster || !pNewMaster)
        return GUILD_ERR_FAILED;
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00,
                         "{?=CALL _Guild_Delegate_Master (%d, %d, %d, %d, %d, %d, %d)}",
                         GetID(), pOldMaster->CharID, pNewMaster->CharID, 0, 10, 1, 0))
    {
        BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    int nResult;
    if (!ExecProcedure(szSql, &nResult))
        return GUILD_ERR_FAILED;
    if (nResult <= 0)
    {
        if (nResult != -2)
            BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    if (pOldMaster->MemberClass)
        BSAssertFail();
    if (!pNewMaster->MemberClass)
        BSAssertFail();
    pOldMaster->SetColumn(&InstanceGuildMemberData::MemberClass, (BYTE)10);
    pNewMaster->SetColumn(&InstanceGuildMemberData::MemberClass, (BYTE)0);
    pOldMaster->SetColumn(&InstanceGuildMemberData::SiegeAuthority, (BYTE)0);
    pNewMaster->SetColumn(&InstanceGuildMemberData::SiegeAuthority, (BYTE)1);
    pOldMaster->SetColumn(&InstanceGuildMemberData::Permission, 0);
    pNewMaster->SetColumn(&InstanceGuildMemberData::Permission, -1);
    m_pMasterRecord = pNewMaster;
    return GUILD_OK;
}

WORD CGuild::UpdateMemberPermissions(const GuildPermissionVector& vecPermission)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    for (GuildPermissionVector::const_iterator it = vecPermission.begin();
         it != vecPermission.end(); ++it)
    {
        CGuildMember* pMember = FindMember(it->first);
        if (!pMember)
        {
            BSAssertFail();
            continue;
        }
        pMember->m_pRecord->SetColumn(&InstanceGuildMemberData::Permission, (int)it->second);
        if (ExecRecordUpdate(pMember->m_pRecord, 5) != 1)
            BSAssertFail();
    }
    return GUILD_OK;
}

WORD CGuild::DBIsMasterCandidates(CVote* pVote)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    if (HasMasterCandidate(pVote->GetKind()) == 1)
        return GUILD_ERR_VOTE_RUNNING;
    if (!g_pShardDB)
        BSAssertFail();
    for (GuildMemberMap::iterator it = m_mapMember.begin(); it != m_mapMember.end(); ++it)
    {
        CGuildMember* pMember = it->second;
        DWORD dwCharID = pMember->GetCharID();
        if (pMember->m_pUser)
        {
            pVote->AddChoice(dwCharID);
            continue;
        }
        char szSql[0xC00];
        if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _Guild_Is_Master_Candidators(%d)}",
                             dwCharID))
        {
            BSAssertFail();
            return GUILD_ERR_FAILED;
        }
        int nResult;
        if (!ExecProcedure(szSql, &nResult) || nResult == -1000)
            return GUILD_ERR_FAILED;
        if (nResult == 0)
            continue;
        if (nResult != 1)
            return GUILD_ERR_FAILED;
        if (m_pMasterRecord->CharID != (int)dwCharID)
            pVote->AddChoice(dwCharID);
    }
    PushCandidate(pVote);
    return GUILD_OK;
}

void CGuild::AddBooty(int nGold)
{
    m_pRecord->SetColumn(&InstanceGuildData::Booty, m_pRecord->Booty + nGold);
}

WORD CGuild::DBClearBooty()
{
    if (!g_pShardDB)
        BSAssertFail();
    if (!g_pShardDB->GetTable(SHARD_DB_TABLE_GUILDWAR))
        return GUILD_ERR_FAILED;
    m_pRecord->SetColumn(&InstanceGuildData::Booty, 0);
    if (ExecRecordUpdate(m_pRecord, 6) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CGuild::Rename(const std::string& strName)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    m_pRecord->SetColumn(&InstanceGuildData::Name, std::string(strName));
    m_pRecord->ClearAllDirty();
    if (!g_pGuildManager)
        BSAssertFail();
    CGuildManager::SendGuildRenamed(strName, GetID());
    return GUILD_OK;
}

WORD CGuild::MemberRename(DWORD dwCharID, const std::string& strName)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    GuildMemberMap::iterator it = m_mapMember.find(dwCharID);
    if (it == m_mapMember.end())
        return GUILD_ERR_MEMBER_NOT_FOUND;
    CGuildMember* pMember = it->second;
    pMember->m_pRecord->SetColumn(&InstanceGuildMemberData::CharName,
                                  std::string(strName.c_str()));
    pMember->m_pRecord->ClearAllDirty();
    if (!g_pGuildManager)
        BSAssertFail();
    CGuildManager::SendMemberRenamed(strName, GetID(), dwCharID);
    return GUILD_OK;
}

WORD CGuild::SetMemberOnline(DWORD dwCharID, BYTE bType)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    if (m_mapMember.find(dwCharID) == m_mapMember.end())
        return GUILD_ERR_MEMBER_NOT_FOUND;
    if (GetLevel() < 2)
        return GUILD_ERR_LEVEL_TOO_LOW;
    if (bType == 1)
    {
        if (m_pLockMember)
        {
            return (WORD)((m_pLockMember->GetCharID() != dwCharID) +
                          GUILD_ERR_LOCK_HELD_BY_SELF);
        }
        if (!MemberHasPermission8(dwCharID))
            return GUILD_ERR_NOT_MASTER;
        SetLockMember(dwCharID);
        return GUILD_OK;
    }
    if (bType == 2)
    {
        if (m_pLockMember && m_pLockMember->GetCharID() == dwCharID)
        {
            ResetLockMember();
            return GUILD_OK;
        }
        return GUILD_ERR_LOCK_NOT_HELD;
    }
    return GUILD_ERR_ARGUMENT;
}

WORD CGuild::DBUpdateMember(DWORD dwCharID, const std::string& strNickname)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    GuildMemberMap::iterator it = m_mapMember.find(dwCharID);
    if (it == m_mapMember.end())
        return GUILD_ERR_MEMBER_NOT_FOUND;
    if (!g_pShardDB)
        BSAssertFail();
    if (!g_pShardDB->GetTable(SHARD_DB_TABLE_GUILDMEMBER))
        return GUILD_ERR_FAILED;
    CGuildMember* pMember = it->second;
    // GuildMember_SetNickname (0x43fca0, second guild group)
    if (pMember->m_pRecord)
        pMember->m_pRecord->SetColumn(&InstanceGuildMemberData::Nickname, strNickname);
    else
        BSAssertFail();
    if (ExecRecordUpdate(pMember->m_pRecord, 9) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CGuild::SetCrestRev(int nCrestRev)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    m_pRecord->SetColumn(&InstanceGuildData::CurCrestRev, nCrestRev);
    if (nCrestRev)
        m_pRecord->SetColumn(&InstanceGuildData::LastCrestRev, nCrestRev);
    if (ExecRecordUpdate(m_pRecord, 7) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CGuild::OffsetGP(int nOffset, BOOL bSave)
{
    if (nOffset + m_pRecord->GatheredSP < 0)
    {
        LogPrintf(0x2000001, "Offset GuildGP tryng to go minus!! GUILDID[%d] CurGP[%d] Offset[%d]",
                  m_pRecord->ID, m_pRecord->GatheredSP, nOffset);
        return GUILD_ERR_FAILED;
    }
    m_pRecord->SetColumn(&InstanceGuildData::GatheredSP, nOffset + m_pRecord->GatheredSP);
    if (bSave)
    {
        if (ExecRecordUpdate(m_pRecord, 3) != 1)
            return GUILD_ERR_FAILED;
    }
    return GUILD_OK;
}

WORD CGuild::SetMercenaryAttr(BYTE bAbility, DWORD* pdwGold, DWORD* pdwGP)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    GetMercenaryAbilityCost(pdwGP, bAbility, pdwGold);
    if ((DWORD)m_pRecord->GatheredSP < *pdwGP)
        return GUILD_ERR_NOT_ENOUGH_GP;
    BYTE bAttr;
    if (bAbility)
    {
        if (bAbility & m_pRecord->MercenaryAttr)
            return GUILD_ERR_ABILITY_SET;
        bAttr = bAbility | m_pRecord->MercenaryAttr;
    }
    else
    {
        bAttr = 0;
    }
    m_pRecord->SetColumn(&InstanceGuildData::MercenaryAttr, bAttr);
    m_pRecord->SetColumn(&InstanceGuildData::GatheredSP, (int)(m_pRecord->GatheredSP - *pdwGP));
    if (ExecRecordUpdate(m_pRecord, 3) != 1 || ExecRecordUpdate(m_pRecord, 8) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

// Members whose record says guild master (GuildMember_IsOffline) are left out.
void CGuild::BroadcastToOnlineMembers(transaction* pTransaction)
{
    for (GuildMemberMap::iterator it = m_mapMember.begin(); it != m_mapMember.end(); ++it)
    {
        CGuildMember* pMember = it->second;
        if (pMember->m_pRecord && !pMember->IsMaster())
        {
            if (!g_pClientManager)
                BSAssertFail();
            CUser* pUser = g_pClientManager->FindUserByCharName(pMember->m_pRecord->CharName);
            if (pUser)
                pTransaction->AddParticipantJID(pUser->GetJID());
        }
    }
}

// ---------------------------------------------------------------------------------------------
// CGuildManager

// The static instance (0x89A398) and its pointer.
CGuildManager* g_pGuildManager = NULL;

CGuildManager::CGuildManager()
{
    if (g_pGuildManager)
        BSAssertFail();
    g_pGuildManager = this;
}

CGuildManager::~CGuildManager()
{
    g_pGuildManager = NULL;
}

BOOL CGuildManager::Load()
{
    return CGuildMgrCommon::Load();
}

void CGuildManager::Reset()
{
    // MgrCommon_Reset_441900 (shared with the training camp manager)
    Clear();
    if (!Load())
        exit(0);
    CMsg* pMsg = AllocNotify(32);
    if (!g_pMainProcess)
        BSAssertFail();
    g_pMainProcess->BroadcastToAllGameServers(pMsg);
}

CGuildCommon* CGuildManager::CreateGuild(CInstanceGuild* pRecord)
{
    if (!pRecord)
        return NULL;
    CGuild* pGuild = new CGuild;
    if (!pGuild->Initialize(pRecord))
    {
        delete pGuild;
        return NULL;
    }
    if (m_mapGuild.find(pRecord->ID) != m_mapGuild.end())
        BSAssertFail();
    m_mapGuild.insert(GuildMap::value_type(pRecord->ID, pGuild));
    return pGuild;
}

CMsg* CGuildManager::AllocAck(DWORD dwRequestID)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(GUILD_MSG_ACK);
    pMsg->WriteBytes(&dwRequestID, 4);
    return pMsg;
}

WORD CGuildManager::DeleteGuild(DWORD dwGuildID, DWORD dwCharID)
{
    GuildMap::iterator it = m_mapGuild.find(dwGuildID);
    if (it == m_mapGuild.end())
        return GUILD_ERR_GUILD_NOT_FOUND;
    WORD wResult = static_cast<CGuild*>(it->second)->DBDeleteGuild(dwCharID);
    if (wResult == GUILD_OK)
    {
        if (it->second)
            delete it->second;
        m_mapGuild.erase(it);
    }
    return wResult;
}

namespace
{
struct GUILD_REQUEST_HANDLER
{
    BYTE bSubType;
    void (CGuildManager::*pfnHandler)(CMsg* pMsg);
};

const GUILD_REQUEST_HANDLER s_aGuildRequestHandler[] =
{
    { 0, &CGuildManager::OnCreateGuild },
    { 1, &CGuildManager::OnDisbandGuild },
    { 2, &CGuildManager::OnJoinGuild },
    { 3, &CGuildManager::OnLeaveGuild },
    { 8, &CGuildManager::OnDonateSP },
    { 9, &CGuildManager::OnLevelUpGuild },
    { 10, &CGuildManager::OnUpdateNotice },
    { 11, &CGuildManager::OnUnionInvite },
    { 16, &CGuildManager::OnUnionLeave },
    { 17, &CGuildManager::OnUnionKick },
    { 19, &CGuildManager::OnDelegateMaster },
    { 20, &CGuildManager::OnUpdatePermissions },
    { 21, &CGuildManager::OnMasterVoteStart },
    { 30, &CGuildManager::OnGuildWarRecordUpdate },
    { 36, &CGuildManager::OnMemberOnlineState },
    { 37, &CGuildManager::OnMemberInfoUpdate },
    { 38, &CGuildManager::OnGuildCrestRequest38 },
    { 39, &CGuildManager::OnGuildCrestUpdate },
    { 40, &CGuildManager::OnUnionCrestRequest40 },
    { 41, &CGuildManager::OnUnionCrestUpdate },
    { 42, &CGuildManager::OnDonateSP42 },
    { 43, &CGuildManager::OnGiveMercenaryAbility },
    { 44, &CGuildManager::OnUpdateGuildSP },
    { 45, &CGuildManager::OnOffsetGP },
    { 46, &CGuildManager::OnSetSiegeAuthority },
    { 50, &CGuildManager::OnGuildWarBegin },
    { 53, &CGuildManager::OnOffsetGPAndLevel },
};
}

void CGuildManager::OnGuildRequest(CMsg* pMsg)
{
    BYTE bSubType = pMsg->ReadForwardOnly<BYTE>();
    for (int i = 0; i < _countof(s_aGuildRequestHandler); ++i)
    {
        if (s_aGuildRequestHandler[i].bSubType == bSubType)
        {
            (this->*s_aGuildRequestHandler[i].pfnHandler)(pMsg);
            return;
        }
    }
    BSAssertFail();
}

void CGuildManager::CheckGuildWarTimeouts()
{
    static DWORD s_dwNow = 0;
    static DWORD s_dwEnd = 0;
    PackedDateTime_GetLocal(&s_dwNow);
    GuildWarMap::iterator it = m_mapGuildWar.begin();
    while (it != m_mapGuildWar.end())
    {
        CInstanceGuildWar* pWar = it->second;
        const TIMESTAMP_STRUCT& end = pWar->WarEndTime;
        if ((end.year || end.month || end.day || end.hour || end.minute || end.second) &&
            pWar->PointGain[0] != pWar->PointGain[1] &&
            (RefObj_PackTypeID(s_dwEnd, (const WORD*)&end),
             PackedDateTime_Compare(&s_dwEnd, &s_dwNow) >= 0))
        {
            ++it;
            DWORD dwWarID = pWar->ID;
            DWORD dwWinner;
            if (pWar->PointGain[0] <= pWar->PointGain[1])
                dwWinner = pWar->Guild[1];
            else
                dwWinner = pWar->Guild[0];
            if (DBEndGuildWar(dwWinner, dwWarID) == GUILD_OK)
                SendGuildWarEnded(dwWarID, dwWinner);
            else
                BSAssertFail();
        }
        else
        {
            ++it;
        }
    }
}

int CGuildManager::BroadcastGuildWarState()
{
    DWORD dwNow = 0;
    PackedDateTime_GetLocal(&dwNow);
    CMsg* pMsg = AllocNotify(31);
    pMsg->WriteBytes(&dwNow, 4);
    return BroadcastToGameServers(pMsg);
}

void CGuildManager::OnGuildNotify(CMsg* pMsg)
{
    BYTE bSubType = pMsg->ReadForwardOnly<BYTE>();
    if (bSubType == 6)
        OnRefreshMemberStatus(pMsg);
    else if (bSubType == 7)
        OnMemberRelay(pMsg);
    else if (bSubType == 29)
        OnGuildWarCombatResult(pMsg);
    else
        BSAssertFail();
}

void CGuildManager::OnRefreshMemberStatus(CMsg* pMsg)
{
    DWORD dwGuildID = pMsg->ReadForwardOnly<DWORD>();
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
        return;
    DWORD dwCharID;
    BYTE bType;
    DWORD dwValue;
    *pMsg >> dwCharID;
    *pMsg >> bType;
    *pMsg >> dwValue;
    pGuild->RefreshMemberStatus(dwCharID, bType, dwValue, NULL);
}

void CGuildManager::OnMemberRelay(CMsg* pMsg)
{
    DWORD dwGuildID = pMsg->ReadForwardOnly<DWORD>();
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (pGuild)
        pGuild->OnMemberMsgRelay(pMsg);
}

void CGuildManager::OnDisbandGuild(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID, dwCharID;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> dwCharID;
    WORD wResult;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    }
    else if (!pGuild->FindMember(dwCharID))
    {
        wResult = GUILD_ERR_MEMBER_NOT_FOUND;
    }
    else if (!pGuild->FindMember(dwCharID)->IsMaster())
    {
        wResult = GUILD_ERR_NOT_MASTER;
    }
    else
    {
        wResult = GUILD_OK;
        if (pGuild->GetAllianceID())
        {
            DWORD dwAllianceID = pGuild->GetAllianceID();
            if (!GetSingleton_CGuildManager()->FindAlliance(dwAllianceID))
            {
                wResult = GUILD_ERR_NOT_IN_UNION;
            }
            else
            {
                BYTE bLeave = 1;
                wResult = pGuild->LeaveUnion(&bLeave);
                if (wResult == GUILD_OK)
                {
                    SendUnionGuildRemoved(dwAllianceID, dwGuildID, 1);
                    if (bLeave == 3)
                    {
                        GetSingleton_CGuildManager()->DisbandUnion(dwAllianceID);
                        SendUnionDisbanded(dwAllianceID);
                    }
                }
            }
        }
        if (wResult == GUILD_OK)
        {
            wResult = DeleteGuild(dwGuildID, dwCharID);
            if (wResult == GUILD_OK)
                SendDisbandResult(dwGuildID);
        }
    }
    SendAckResult(dwSessionID, dwRequestID, wResult);
}

void CGuildManager::OnLeaveGuild(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID, dwCharID;
    BYTE bReason;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> dwCharID;
    *pMsg >> bReason;
    WORD wResult = RemoveMember(dwGuildID, dwCharID, bReason);
    SendAckResult(dwSessionID, dwRequestID, wResult);
}

void CGuildManager::OnDonateSP(CMsg* pMsg)
{
    DWORD dwGuildID, dwCharID;
    int nAmount;
    *pMsg >> dwGuildID;
    *pMsg >> dwCharID;
    *pMsg >> nAmount;
    WORD wResult;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    }
    else
    {
        wResult = pGuild->DonateGP(nAmount, dwCharID);
        if (wResult == GUILD_OK)
        {
            SendDonateSPResult(dwGuildID, dwCharID, nAmount);
            return;
        }
        if (wResult == GUILD_ERR_DONATE_SILENT)
            return;
    }
    LogPrintf(0x2000001, "SP DONATION FAILED!! GUILDID[%d] CHAR[%d] STUFF[%d] ERROR[%d]",
              dwGuildID, dwCharID, nAmount, wResult);
}

void CGuildManager::OnLevelUpGuild(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        SendAckResult(dwSessionID, dwRequestID, GUILD_ERR_GUILD_NOT_FOUND);
        return;
    }
    int nCost = g_anGuildLevelUpGP[pGuild->GetLevel()];
    WORD wResult = pGuild->LevelUp();
    if (wResult != GUILD_OK)
    {
        SendAckResult(dwSessionID, dwRequestID, wResult);
        return;
    }
    SendLevelUpResult(dwGuildID, pGuild->GetLevel(), pGuild->m_pRecord->GatheredSP);
    // BUG(SM-200): without a master record or with the master offline the guild has levelled
    // up (and was told so), but the GP history is skipped and the requester gets error 1.
    CInstanceGuildMember* pMaster = pGuild->m_pMasterRecord;
    CUser* pUser = NULL;
    if (pMaster)
    {
        if (!g_pClientManager)
            BSAssertFail();
        pUser = g_pClientManager->FindUserByCharName(pMaster->CharName);
    }
    if (!pUser)
    {
        BSAssertFail();
        CMsg* pAck = AllocAck(dwRequestID);
        BYTE bResult = 2;
        pAck->WriteBytes(&bResult, 1);
        pAck->WriteBytes(&wResult, 2);
        SendAck(dwSessionID, pAck);
        return;
    }
    if (DBAddGPHistory(dwGuildID, pUser->GetJID(), nCost, 5) == GUILD_OK)
        SendGuildInfo53(dwGuildID);
    else
        BSAssertFail();
    if ((BYTE)pGuild->m_listGPHistory.size() > 100)
    {
        BYTE bCount = DBDeleteGPHistory(pGuild);
        if (bCount)
            SendGPHistory54(dwGuildID, bCount);
    }
    SendAckResult(dwSessionID, dwRequestID, GUILD_OK);
}

void CGuildManager::OnUpdateNotice(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    std::string strTitle;
    std::string strNotice;
    DWORD dwRequestID, dwGuildID;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> strTitle;
    *pMsg >> strNotice;
    WORD wResult;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    }
    else
    {
        wResult = pGuild->UpdateNotice(strTitle, strNotice);
        if (wResult == GUILD_OK)
            SendNoticeUpdated(dwGuildID, strTitle, strNotice);
    }
    SendAckResult(dwSessionID, dwRequestID, wResult);
}

void CGuildManager::OnUnionInvite(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID, dwTargetGuildID, dwTargetCharID;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> dwTargetGuildID;
    *pMsg >> dwTargetCharID;
    WORD wResult;
    CGuild* pGuild = FindGuildT(dwGuildID);
    CGuild* pTarget = NULL;
    if (!pGuild)
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    else if (!(pTarget = FindGuildT(dwTargetGuildID)))
        wResult = GUILD_ERR_TARGET_NOT_FOUND;
    else if (pTarget->GetAllianceID())
        wResult = GUILD_ERR_ALREADY_IN_UNION;
    else if (!pTarget->IsMemberMaster(dwTargetCharID))
        wResult = GUILD_ERR_TARGET_NOT_MASTER;
    else
    {
        DWORD dwAllianceID = pGuild->GetAllianceID();
        if (!dwAllianceID)
        {
            wResult = DBCreateUnion(pGuild, pTarget, &dwAllianceID);
            if (wResult == GUILD_OK)
                SendUnionCreated(dwAllianceID, pGuild->GetID(), pTarget->GetID());
        }
        else
        {
            CAlliance* pAlliance = GetSingleton_CGuildManager()->FindAlliance(dwAllianceID);
            if (!pAlliance)
            {
                wResult = GUILD_ERR_UNION_NOT_FOUND;
            }
            else
            {
                BYTE bSlot = pAlliance->FindEmptySlot();
                if (!bSlot)
                {
                    wResult = GUILD_ERR_UNION_FULL;
                }
                else
                {
                    wResult = pAlliance->AddGuild(pTarget, bSlot);
                    if (wResult == GUILD_OK)
                        SendUnionGuildAdded(dwAllianceID, pTarget->GetID(), bSlot);
                }
            }
        }
    }
    SendAckResult(dwSessionID, dwRequestID, wResult);
}

void CGuildManager::OnUnionLeave(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID;
    *pMsg >> dwRequestID;
    DWORD dwGuildID = pMsg->ReadForwardOnly<DWORD>();
    DWORD dwCharID = pMsg->ReadForwardOnly<DWORD>();
    CGuild* pGuild = GetSingleton_CGuildManager()->FindGuildT(dwGuildID);
    WORD wResult;
    DWORD dwAllianceID = 0;
    BYTE bLeave = 1;
    if (!pGuild)
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    else if (!pGuild->IsMemberMaster(dwCharID))
        wResult = GUILD_ERR_NOT_MASTER;
    else if (!(dwAllianceID = pGuild->GetAllianceID()))
        wResult = GUILD_ERR_NOT_IN_UNION;
    else
    {
        wResult = pGuild->LeaveUnion(&bLeave);
        if (wResult == GUILD_OK)
            SendUnionGuildRemoved(dwAllianceID, dwGuildID, 1);
    }
    SendAckResult(dwSessionID, dwRequestID, wResult);
    if (wResult == GUILD_OK && bLeave == 3)
    {
        GetSingleton_CGuildManager()->DisbandUnion(dwAllianceID);
        SendUnionDisbanded(dwAllianceID);
    }
}

void CGuildManager::OnUnionKick(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID;
    *pMsg >> dwRequestID;
    DWORD dwGuildID = pMsg->ReadForwardOnly<DWORD>();
    DWORD dwCharID = pMsg->ReadForwardOnly<DWORD>();
    DWORD dwTargetGuildID = pMsg->ReadForwardOnly<DWORD>();
    CGuild* pGuild = GetSingleton_CGuildManager()->FindGuildT(dwGuildID);
    CGuild* pTarget = NULL;
    WORD wResult;
    DWORD dwAllianceID = 0;
    BYTE bLeave = 2;
    if (!pGuild)
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    else if (!pGuild->IsMemberMaster(dwCharID))
        wResult = GUILD_ERR_NOT_MASTER;
    else if (!(pTarget = GetSingleton_CGuildManager()->FindGuildT(dwTargetGuildID)))
        wResult = GUILD_ERR_TARGET_NOT_FOUND;
    else
    {
        dwAllianceID = pTarget->GetAllianceID();
        if (pGuild->GetAllianceID() != dwAllianceID)
            BSAssertFail();
        if (!GetSingleton_CGuildManager()->FindAlliance(dwAllianceID))
        {
            wResult = GUILD_ERR_KICK_UNION_NOT_FOUND;
        }
        else
        {
            wResult = pTarget->LeaveUnion(&bLeave);
            if (wResult == GUILD_OK)
                SendUnionGuildRemoved(dwAllianceID, dwTargetGuildID, 2);
        }
    }
    SendAckResult(dwSessionID, dwRequestID, wResult);
    if (wResult == GUILD_OK && bLeave == 3)
    {
        GetSingleton_CGuildManager()->DisbandUnion(dwAllianceID);
        SendUnionDisbanded(dwAllianceID);
    }
}

void CGuildManager::OnDelegateMaster(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID, dwCharID;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> dwCharID;
    WORD wResult = GUILD_ERR_FAILED;
    CGuild* pGuild = GetSingleton_CGuildManager()->FindGuildT(dwGuildID);
    CInstanceGuildMember* pOldMaster = NULL;
    if (!pGuild)
    {
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    }
    else if ((pOldMaster = pGuild->m_pMasterRecord) != NULL)
    {
        CGuildMember* pMember = pGuild->FindMember(dwCharID);
        if (!pMember || pMember->m_pRecord == pOldMaster)
        {
            wResult = GUILD_ERR_NO_RECORD;
        }
        else
        {
            wResult = pGuild->DelegateMaster(pMember->m_pRecord, pOldMaster);
            if (wResult == GUILD_OK)
                SendMasterDelegated(dwGuildID, dwCharID, pOldMaster->CharID);
        }
    }
    SendAckResult(dwSessionID, dwRequestID, wResult);
}
