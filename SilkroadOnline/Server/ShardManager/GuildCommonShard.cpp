// The shard manager's part of the shared guild classes (ServerCommon\GuildCommon.h): the
// functions that use the shard DB tables or the shard manager's vote manager.

#include "ServerCommon/GuildCommon.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/GuildExtern.h"
#include "ShardManager/Vote.h"

#include <strsafe.h>

// Connections of the guild statements: 7 for procedures, 6 for selects.
#define GUILD_DB_CONN           7
#define GUILD_DB_SELECT_CONN    6

static CDBTable* ShardTable(int nIndex)
{
    if (!g_pShardDB)
        BSAssertFail();
    return g_pShardDB->GetTable(nIndex);
}

// CDBObjectList_DetachAndClear (0x40a7e0): the records of a failed select go back to the table.
static void FreeRecordList(CDBRecordList& list, CDBTable* pTable)
{
    for (CDBRecordList::iterator it = list.begin(); it != list.end(); ++it)
        pTable->FreeRecord(*it);
    list.clear();
}

// ---------------------------------------------------------------------------------------------
// CGuildMember

void CGuildMember::ReleaseRecord()
{
    m_pUser = NULL;
    m_dwState = (DWORD)-1;
    if (!m_pRecord)
        return;
    if (g_pShardDB)
        g_pShardDB->GetTable(SHARD_DB_TABLE_GUILDMEMBER)->FreeRecord(m_pRecord);
    m_pRecord = NULL;
}

// ---------------------------------------------------------------------------------------------
// CGuildCommon

BOOL CGuildCommon::Initialize(CInstanceGuild* pRecord)
{
    if (m_pRecord || !pRecord->ID)
    {
        BSAssertFail();
        return FALSE;
    }
    m_pRecord = pRecord;

    CDBRecordList list;
    CDBTable* pMemberTable = ShardTable(SHARD_DB_TABLE_GUILDMEMBER);
    if (!pMemberTable)
    {
        BSAssertFail();
        return FALSE;
    }
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "WHERE GuildID = %d", m_pRecord->ID))
    {
        BSAssertFail();
        return FALSE;
    }
    if (!pMemberTable->SelectWhere(szSql, GUILD_DB_SELECT_CONN, &list, 0))
    {
        FreeRecordList(list, pMemberTable);
        return FALSE;
    }
    for (CDBRecordList::iterator it = list.begin(); it != list.end(); ++it)
    {
        CInstanceGuildMember* pMember = static_cast<CInstanceGuildMember*>(*it);
        AddMember(1, pMember, NULL);
        if (pMember->MemberClass == 0)
        {
            if (m_pMasterRecord)
                BSAssertFail();
            m_pMasterRecord = pMember;
        }
    }
    memset(szSql, 0, sizeof(szSql));
    list.clear();

    CDBTable* pHistoryTable = ShardTable(SHARD_DB_TABLE_GPHISTORY);
    if (!pHistoryTable)
    {
        BSAssertFail();
        return FALSE;
    }
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _DeleteGPHistory (%d)}", m_pRecord->ID))
    {
        BSAssertFail();
        return FALSE;
    }
    __int64 llValue = 0;
    int nExec = CDatabase::ExecDirectWithReturnValue(GetSingleton_CShardDB()->GetSession(),
                                                     GUILD_DB_CONN, szSql,
                                                     DBEXEC_RETURN_INT | 1, &llValue);
    if (nExec & ~1)
    {
        BSAssertFail();
        return FALSE;
    }
    // BUG(SM-202): the result is zero-extended from its low byte before it is compared with
    // -3 and -1, so neither failure is ever logged or stops the load.
    int nResult = (BYTE)llValue;
    if (nResult == -3)
    {
        LogPrintf(0x2000001, "Failed to delete gp history. Unknown DB Error[%d]. GuildID[%d]",
                  nExec, m_pRecord->ID);
        BSAssertFail();
        return FALSE;
    }
    if (nResult == -1)
    {
        LogPrintf(0x2000001, "Failed to delete gp history. Not found guild[%d]", m_pRecord->ID);
        BSAssertFail();
        return FALSE;
    }
    memset(szSql, 0, sizeof(szSql));
    if (StringCchPrintfA(szSql, 0xC00, "SELECT TOP 100 * FROM _GPHistory WHERE GuildID = %d AND "
                         "UsedTime >= ( GETDATE() - 30 ) ORDER BY ID DESC", m_pRecord->ID))
    {
        BSAssertFail();
        return FALSE;
    }
    if (!pHistoryTable->SelectCustom(std::string(szSql), GUILD_DB_SELECT_CONN, &list, 0))
    {
        FreeRecordList(list, pHistoryTable);
        BSAssertFail();
        return FALSE;
    }
    for (CDBRecordList::iterator it = list.begin(); it != list.end(); ++it)
    {
        CInstanceGPHistory* pHistory = dynamic_cast<CInstanceGPHistory*>(*it);
        if (!pHistory || !AddGPHistory(pHistory, 0))
        {
            // The records already added to the history are released as well.
            FreeRecordList(list, pHistoryTable);
            BSAssertFail();
            return FALSE;
        }
    }
    memset(szSql, 0, sizeof(szSql));
    list.clear();
    return TRUE;
}

void CGuildCommon::Clear()
{
    if (!g_pShardDB)
        return;
    for (GuildMemberMap::iterator it = m_mapMember.begin(); it != m_mapMember.end(); ++it)
    {
        CGuildMember* pMember = it->second;
        GuildMember_FreePtr(pMember);
    }
    m_mapMember.clear();
    for (std::vector<CVote*>::iterator it = m_vecCandidate.begin(); it != m_vecCandidate.end();
         ++it)
    {
        if (!g_pVoteManager)
            BSAssertFail();
        g_pVoteManager->RemoveVote(*it);
    }
    m_vecCandidate.clear();
    m_mapHostile.clear();
    for (std::list<CInstanceGPHistory*>::iterator it = m_listGPHistory.begin();
         it != m_listGPHistory.end(); ++it)
    {
        ShardTable(SHARD_DB_TABLE_GPHISTORY)->FreeRecord(*it);
    }
    m_listGPHistory.clear();
    if (m_pRecord)
    {
        ShardTable(SHARD_DB_TABLE_GUILD)->FreeRecord(m_pRecord);
        m_pRecord = NULL;
    }
}

CInstanceGuildMember* CGuildCommon::LoadMemberRecord(DWORD dwCharID, DWORD dwGuildID)
{
    if (!dwGuildID || !dwCharID)
        return NULL;
    CDBTable* pTable = ShardTable(SHARD_DB_TABLE_GUILDMEMBER);
    if (!pTable)
    {
        BSAssertFail();
        return NULL;
    }
    CDBRecordList list;
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "WHERE GuildID = %d AND CharID = %d", dwGuildID,
                         dwCharID))
    {
        BSAssertFail();
        return NULL;
    }
    if (!pTable->SelectWhere(szSql, GUILD_DB_SELECT_CONN, &list, 0))
    {
        FreeRecordList(list, pTable);
        BSAssertFail();
        return NULL;
    }
    if (list.size() > 1)
        BSAssertFail();
    return static_cast<CInstanceGuildMember*>(list.front());
}

BOOL CGuildCommon::HasMasterCandidate(BYTE bKey)
{
    for (std::vector<CVote*>::iterator it = m_vecCandidate.begin(); it != m_vecCandidate.end();
         ++it)
    {
        if ((*it)->GetKind() == bKey)
            return TRUE;
    }
    return FALSE;
}

void CGuildCommon::RemoveMasterCandidate(DWORD dwVoteID)
{
    for (std::vector<CVote*>::iterator it = m_vecCandidate.begin(); it != m_vecCandidate.end();
         ++it)
    {
        if ((*it)->m_dwID == dwVoteID)
        {
            m_vecCandidate.erase(it);
            return;
        }
    }
}

void CGuildCommon::PopGPHistory(BYTE bCount)
{
    if (!bCount)
    {
        BSAssertFail();
        return;
    }
    BYTE bSize = (BYTE)m_listGPHistory.size();
    if (!bSize || bCount > bSize)
    {
        BSAssertFail();
        return;
    }
    for (BYTE i = 0; i < bCount; ++i)
    {
        ShardTable(SHARD_DB_TABLE_GPHISTORY)->FreeRecord(m_listGPHistory.back());
        m_listGPHistory.pop_back();
    }
}

// ---------------------------------------------------------------------------------------------
// CGuildMgrCommon

BOOL CGuildMgrCommon::Load()
{
    CDBTable* pGuildTable = ShardTable(SHARD_DB_TABLE_GUILD);
    if (!pGuildTable)
    {
        BSAssertFail();
        return FALSE;
    }
    CDBRecordList listGuild;
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "WHERE ID <> 0"))
    {
        BSAssertFail();
        return FALSE;
    }
    if (!pGuildTable->SelectWhere(szSql, GUILD_DB_SELECT_CONN, &listGuild, 0))
    {
        FreeRecordList(listGuild, pGuildTable);
        BSAssertFail();
        return FALSE;
    }
    for (CDBRecordList::iterator it = listGuild.begin(); it != listGuild.end(); ++it)
        CreateGuild(static_cast<CInstanceGuild*>(*it));

    CDBTable* pAllianceTable = ShardTable(SHARD_DB_TABLE_ALLIEDCLANS);
    if (!pAllianceTable)
    {
        BSAssertFail();
        return FALSE;
    }
    CDBRecordList listAlliance;
    if (!pAllianceTable->SelectAll(GUILD_DB_SELECT_CONN, &listAlliance))
    {
        FreeRecordList(listAlliance, pAllianceTable);
        return FALSE;
    }
    for (CDBRecordList::iterator it = listAlliance.begin(); it != listAlliance.end(); ++it)
    {
        CInstanceAlliedClan* pAlliance = static_cast<CInstanceAlliedClan*>(*it);
        if (pAlliance->ID)
            AddAlliance(pAlliance);
    }

    CDBTable* pWarTable = ShardTable(SHARD_DB_TABLE_GUILDWAR);
    if (!pWarTable)
    {
        BSAssertFail();
        return FALSE;
    }
    CDBRecordList listWar;
    if (!pWarTable->SelectAll(GUILD_DB_SELECT_CONN, &listWar))
    {
        FreeRecordList(listWar, pWarTable);
        BSAssertFail();
        return FALSE;
    }
    for (CDBRecordList::iterator it = listWar.begin(); it != listWar.end(); ++it)
    {
        CInstanceGuildWar* pWar = static_cast<CInstanceGuildWar*>(*it);
        m_mapGuildWar.insert(GuildWarMap::value_type(pWar->ID, pWar));
    }
    LinkGuildWars();
    return TRUE;
}

void CGuildMgrCommon::Clear()
{
    for (GuildMap::iterator it = m_mapGuild.begin(); it != m_mapGuild.end(); ++it)
    {
        CGuildCommon* pGuild = it->second;
        pGuild->Clear();
        delete pGuild;
    }
    m_mapGuild.clear();
    for (AllianceMap::iterator it = m_mapAlliance.begin(); it != m_mapAlliance.end(); ++it)
        it->second.ReleaseRecord();
    m_mapAlliance.clear();
    if (g_pShardDB)
    {
        CDBTable* pWarTable = g_pShardDB->GetTable(SHARD_DB_TABLE_GUILDWAR);
        for (GuildWarMap::iterator it = m_mapGuildWar.begin(); it != m_mapGuildWar.end(); ++it)
            pWarTable->FreeRecord(it->second);
    }
    m_mapGuildWar.clear();
}

void CGuildMgrCommon::RemoveAlliance(DWORD dwAllianceID)
{
    AllianceMap::iterator it = m_mapAlliance.find(dwAllianceID);
    if (it == m_mapAlliance.end())
        return;
    it->second.ReleaseRecord();
    m_mapAlliance.erase(it);
}

BOOL CGuildMgrCommon::RemoveGuildWar(CInstanceGuildWar* pWar)
{
    GuildWarMap::iterator it = m_mapGuildWar.find(pWar->ID);
    if (it == m_mapGuildWar.end())
        return FALSE;
    RemoveHostileGuilds(pWar);
    m_mapGuildWar.erase(it);
    CDBTable* pTable = ShardTable(SHARD_DB_TABLE_GUILDWAR);
    if (!pTable)
    {
        BSAssertFail();
        return FALSE;
    }
    pTable->FreeRecord(pWar);
    return TRUE;
}

// LoadGuildById / LoadAllianceById: one record by ID. The guild asserts more than one row, the
// alliance (bExactlyOne) any count but one.
static CDBRecord* LoadRecordById(int nTable, DWORD dwID, BOOL bExactlyOne)
{
    CDBTable* pTable = ShardTable(nTable);
    if (!pTable)
    {
        BSAssertFail();
        return NULL;
    }
    CDBRecordList list;
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "WHERE ID = %d", dwID))
    {
        BSAssertFail();
        return NULL;
    }
    if (!pTable->SelectWhere(szSql, GUILD_DB_SELECT_CONN, &list, 0))
    {
        FreeRecordList(list, pTable);
        BSAssertFail();
        return NULL;
    }
    if (bExactlyOne ? list.size() != 1 : list.size() > 1)
        BSAssertFail();
    return list.front();
}

CInstanceGuild* CGuildMgrCommon::LoadGuildById(DWORD dwGuildID)
{
    return static_cast<CInstanceGuild*>(LoadRecordById(SHARD_DB_TABLE_GUILD, dwGuildID, FALSE));
}

CInstanceAlliedClan* CGuildMgrCommon::LoadAllianceById(DWORD dwAllianceID)
{
    return static_cast<CInstanceAlliedClan*>(
        LoadRecordById(SHARD_DB_TABLE_ALLIEDCLANS, dwAllianceID, TRUE));
}
