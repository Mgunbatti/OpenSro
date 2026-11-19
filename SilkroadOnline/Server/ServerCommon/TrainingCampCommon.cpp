#include "ServerCommon/TrainingCampCommon.h"

#include <strsafe.h>

#include "ShardManager/SocialExtern.h"

CChunkAllocatorST<TrainingCampMemberBuffData> TrainingCampMemberBuffData::s_Pool(
    NULL, 0, 500, "TrainingCampMemberBuffData");
CChunkAllocatorST<TrainingCampMember> TrainingCampMember::s_Pool(NULL, 0, 500,
                                                                 "TrainingCampMember");

namespace
{

// The shard table of a slot after the inlined size check of the instance table vector: an
// assert and NULL when the vector is too short.
CDBTable* ShardTable(DWORD nSlot)
{
    if (!g_pShardDB)
        BSAssertFail();
    std::vector<CDBTable*>& vecTables = g_pShardDB->GetInstanceTables();
    if (vecTables.size() <= nSlot)
    {
        BSAssertFail();
        return NULL;
    }
    return vecTables[nSlot];
}

}

// ---------------------------------------------------------------------------------------------
// Record output

void TrainingCampMemberRecord_WriteToMsg(CMsg* pMsg, const CInstanceTrainingCampMember* p)
{
    *pMsg << p->CampID << p->CharID << p->RefObjID << p->CharName.c_str() << p->MemberClass
          << p->CharJoinedLevel;
    pMsg->WriteBytes(&p->JoinDate, sizeof(TIMESTAMP_STRUCT));
    *pMsg << p->CharCurLevel << p->CharMaxLevel << p->HonorPoint;
}

void TrainingCampRecord_WriteToMsg(BYTE byMask, const CInstanceTrainingCamp* p, CMsg* pMsg)
{
    if (byMask & 1)
        *pMsg << p->ID;
    if (byMask & 2)
        pMsg->WriteBytes(&p->CreationDate, sizeof(TIMESTAMP_STRUCT));
    if (byMask & 4)
        *pMsg << p->Rank;
    if (byMask & 8)
        *pMsg << p->CommentTitle.c_str() << p->Comment.c_str();
}

// ---------------------------------------------------------------------------------------------
// TrainingCampMemberBuffData

void TrainingCampMemberBuffData::Release()
{
    PoolObject_Release(this, "TrainingCampMemberBuffData");
}

void TrainingCampMemberBuffData::DetachDBObject()
{
    if (m_pRecord)
    {
        if (g_pShardDB)
            g_pShardDB->GetTable(SHARD_TABLE_TRAINING_CAMP_BUFF)->FreeRecord(m_pRecord);
        m_pRecord = NULL;
    }
}

// ---------------------------------------------------------------------------------------------
// TrainingCampMember

TrainingCampMember::TrainingCampMember()
{
    m_dwStatus = (DWORD)-1;
    m_dw08 = 0;
    m_aw0C[0] = 0;
    m_aw0C[1] = 0;
    m_aw0C[2] = 0;
    m_aw0C[3] = 0;
    m_pRecord = NULL;
    m_pUser = NULL;
}

TrainingCampMember::~TrainingCampMember()
{
    Cleanup();
}

void TrainingCampMember::Release()
{
    PoolObject_Release(this, "TrainingCampMember");
}

void TrainingCampMember::Init(DWORD dwStatus, CUser* pUser, CInstanceTrainingCampMember* pRecord,
                              CDBRecordList* pBuffs)
{
    m_dwStatus = dwStatus;
    m_pRecord = pRecord;
    m_pUser = pUser;
    m_vecBuff.resize(GetBuffSlotCount());
    for (DWORD i = 0; i < m_vecBuff.size(); ++i)
        m_vecBuff[i] = PoolObject_Alloc<TrainingCampMemberBuffData>();
    m_nBuffType4 = 0;
    m_dwBuffType12 = 0;
    LoadBuffs(pBuffs);
}

void TrainingCampMember::Cleanup()
{
    m_pUser = NULL;
    m_dwStatus = (DWORD)-1;
    if (m_pRecord)
    {
        if (g_pShardDB)
        {
            g_pShardDB->GetTable(SHARD_TABLE_TRAINING_CAMP_MEMBER)->FreeRecord(m_pRecord);
            m_pRecord = NULL;
            ClearBuffs();
        }
        else
        {
            m_pRecord = NULL;
        }
    }
}

void TrainingCampMember::WriteToMsg(CMsg* pMsg)
{
    TrainingCampMemberRecord_WriteToMsg(pMsg, m_pRecord);
    *pMsg << (BYTE)m_dwStatus << m_dw08 << m_aw0C[0] << m_aw0C[1] << m_aw0C[2] << m_aw0C[3];
}

int TrainingCampMember::GetGraduateHonor() const
{
    if (IsTrainee() && m_pRecord->CharJoinedLevel <= 30)
        return 40 - m_pRecord->CharJoinedLevel;
    return 0;
}

void TrainingCampMember::ClearBuffs()
{
    // Only an assert: the buff table must exist.
    ShardTable(SHARD_TABLE_TRAINING_CAMP_BUFF);
    for (DWORD i = 0; i < m_vecBuff.size(); ++i)
        PoolObject_FreePtr(m_vecBuff[i]);
    m_vecBuff.clear();
}

int TrainingCampMember::GetBuffSlotCount() const
{
    switch (m_pRecord->MemberClass)
    {
    case TRAINING_CAMP_CLASS_LEADER:
        return 6;
    case TRAINING_CAMP_CLASS_TRAINEE:
        return 2;
    default:
        return 0;
    }
}

void TrainingCampMember::LoadBuffs(CDBRecordList* pBuffs)
{
    DWORD i = 0;
    for (CDBRecordList::iterator it = pBuffs->begin(); it != pBuffs->end(); ++it, ++i)
    {
        CInstanceTrainingCampBuffStatus* pNew = (CInstanceTrainingCampBuffStatus*)*it;
        CInstanceTrainingCampBuffStatus* pOld = m_vecBuff.at(i)->m_pRecord;
        m_vecBuff.at(i)->m_pRecord = pNew;
        switch (pNew->BuffType)
        {
        case 0:
            m_dwBuffType12 = 0;
            break;
        case 1:
        case 2:
            m_dwBuffType12 = 1;
            break;
        case 3:
            break;
        case 4:
            ++m_nBuffType4;
            break;
        default:
            BSAssertFail();
            break;
        }
        // A slot that already had a record takes over the new values (the slots are new here,
        // so this does not happen).
        if (pOld)
        {
            const CDBTableInfo& info = CInstanceTrainingCampBuffStatus::s_TableInfo;
            DBRecord_SetColumn(pOld, info, pOld->DonorCharID, pNew->DonorCharID, 2);
            // TrainingCampMemberBuffData::SetTime (0x45d830)
            DBRecord_SetColumn(pOld, info, pOld->StartingTime, pNew->StartingTime, 3);
            DBRecord_SetColumn(pOld, info, pOld->RemainBuffPoint, pNew->RemainBuffPoint, 4);
            DBRecord_SetColumn(pOld, info, pOld->BuffType, pNew->BuffType, 3);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// CTrainingCampCommon

CTrainingCampCommon::CTrainingCampCommon()
{
    m_pCamp = NULL;
    m_pLeader = NULL;
}

CTrainingCampCommon::~CTrainingCampCommon()
{
    CTrainingCampCommon::Clear();
    m_pCamp = NULL;
    m_pLeader = NULL;
}

BOOL CTrainingCampCommon::Load(CInstanceTrainingCamp* pCamp)
{
    if (m_pCamp)
        BSAssertFail();
    if (!pCamp->ID)
        BSAssertFail();
    m_pCamp = pCamp;

    if (!g_pShardDB)
        BSAssertFail();
    std::vector<CDBTable*>& vecTables = g_pShardDB->GetInstanceTables();
    if (vecTables.size() <= SHARD_TABLE_TRAINING_CAMP_MEMBER)
    {
        BSAssertFail();
        BSAssertFail();
        return FALSE;
    }
    CDBTable* pTable = vecTables[SHARD_TABLE_TRAINING_CAMP_MEMBER];
    char szClause[0xC00];
    if (!pTable || StringCchPrintfA(szClause, 0xC00, "WHERE CampID = %d", m_pCamp->ID) != 0)
    {
        BSAssertFail();
        return FALSE;
    }

    CDBRecordList listRows;
    if (!pTable->SelectWhere(szClause, 6, &listRows, 0))
    {
        CDBObjectList_DetachAndClear(&listRows, pTable);
        return FALSE;
    }
    for (CDBRecordList::iterator it = listRows.begin(); it != listRows.end(); ++it)
    {
        CDBRecordList listBuffs;
        CInstanceTrainingCampMember* pMember = (CInstanceTrainingCampMember*)*it;
        if (pMember->MemberClass != TRAINING_CAMP_CLASS_MENTOR)
            TrainingCamp_DBLoadMemberBuffs(m_pCamp->ID, &listBuffs, pMember->CharID);
        AddMember(1, pMember, NULL, &listBuffs);
        if (pMember->MemberClass == TRAINING_CAMP_CLASS_LEADER)
        {
            if (m_pLeader)
                BSAssertFail();
            m_pLeader = pMember;
        }
    }
    return TRUE;
}

void CTrainingCampCommon::Clear()
{
    if (!g_pShardDB)
        return;
    for (TrainingCampMemberMap::iterator it = m_mapMember.begin(); it != m_mapMember.end(); ++it)
    {
        TrainingCampMember* pMember = it->second;
        PoolObject_FreePtr(pMember);
    }
    m_mapMember.clear();
    if (m_pCamp)
    {
        CDBTable* pTable = ShardTable(SHARD_TABLE_TRAINING_CAMP);
        pTable->FreeRecord(m_pCamp);
        m_pCamp = NULL;
    }
}

void CTrainingCampCommon::AddMember(DWORD dwStatus, CInstanceTrainingCampMember* pRecord,
                                    CUser* pUser, CDBRecordList* pBuffs)
{
    TrainingCampMember* pMember = PoolObject_Alloc<TrainingCampMember>();
    pMember->Init(dwStatus, pUser, pRecord, pBuffs);
    if (m_mapMember.find(pRecord->CharID) != m_mapMember.end())
        BSAssertFail();
    m_mapMember.insert(TrainingCampMemberMap::value_type(pRecord->CharID, pMember));
}

void CTrainingCampCommon::WriteToMsg(CMsg* pMsg, BYTE byMask)
{
    TrainingCampRecord_WriteToMsg(byMask, m_pCamp, pMsg);
    WriteMembersToMsg(pMsg);
}

void CTrainingCampCommon::WriteMembersToMsg(CMsg* pMsg)
{
    *pMsg << (BYTE)m_mapMember.size();
    for (TrainingCampMemberMap::iterator it = m_mapMember.begin(); it != m_mapMember.end(); ++it)
        it->second->WriteToMsg(pMsg);
}

TrainingCampMember* CTrainingCampCommon::FindMember(DWORD dwCharID)
{
    if (!dwCharID)
        return NULL;
    TrainingCampMemberMap::iterator it = m_mapMember.find(dwCharID);
    if (it == m_mapMember.end())
        return NULL;
    return it->second;
}

BOOL CTrainingCampCommon::IsMemberLeader(DWORD dwCharID)
{
    TrainingCampMember* pMember = FindMember(dwCharID);
    if (!pMember)
        return FALSE;
    if (!pMember->m_pRecord)
    {
        BSAssertFail();
        return FALSE;
    }
    return pMember->IsLeader();
}

DWORD CTrainingCampCommon::CountMentors()
{
    DWORD dwCount = 0;
    for (TrainingCampMemberMap::iterator it = m_mapMember.begin(); it != m_mapMember.end(); ++it)
    {
        if (it->second->IsMentor())
            ++dwCount;
    }
    return dwCount;
}

DWORD CTrainingCampCommon::CountTrainees()
{
    DWORD dwCount = 0;
    for (TrainingCampMemberMap::iterator it = m_mapMember.begin(); it != m_mapMember.end(); ++it)
    {
        if (it->second->IsTrainee())
            ++dwCount;
    }
    return dwCount;
}

// ---------------------------------------------------------------------------------------------
// CTrainingCampMgrCommon

BOOL CTrainingCampMgrCommon::Load()
{
    if (!g_pShardDB)
        BSAssertFail();
    std::vector<CDBTable*>& vecTables = g_pShardDB->GetInstanceTables();
    if (vecTables.size() <= SHARD_TABLE_TRAINING_CAMP)
    {
        BSAssertFail();
        BSAssertFail();
        return FALSE;
    }
    CDBTable* pTable = vecTables[SHARD_TABLE_TRAINING_CAMP];
    if (!pTable)
    {
        BSAssertFail();
        return FALSE;
    }

    CDBRecordList listRows;
    char szClause[0xC00];
    if (StringCchPrintfA(szClause, 0xC00, "WHERE ID <> 0") != 0)
    {
        BSAssertFail();
        return FALSE;
    }
    if (!pTable->SelectWhere(szClause, 6, &listRows, 0))
    {
        CDBObjectList_DetachAndClear(&listRows, pTable);
        BSAssertFail();
        return FALSE;
    }
    for (CDBRecordList::iterator it = listRows.begin(); it != listRows.end(); ++it)
        AddCamp((CInstanceTrainingCamp*)*it);
    return TRUE;
}

void CTrainingCampMgrCommon::Clear()
{
    for (TrainingCampMap::iterator it = m_mapCamp.begin(); it != m_mapCamp.end(); ++it)
    {
        CTrainingCampCommon* pCamp = it->second;
        pCamp->Clear();
        delete pCamp;
    }
    m_mapCamp.clear();
}

// ---------------------------------------------------------------------------------------------
// DB loads

CInstanceTrainingCampMember* TrainingCamp_DBLoadMember(DWORD dwCharID, DWORD dwCampID)
{
    if (!dwCampID || !dwCharID)
        return NULL;
    if (!g_pShardDB)
        BSAssertFail();
    CDBTable* pTable = g_pShardDB->GetTable(SHARD_TABLE_TRAINING_CAMP_MEMBER);
    if (!pTable)
    {
        BSAssertFail();
        return NULL;
    }

    CDBRecordList listRows;
    char szClause[0xC00];
    if (StringCchPrintfA(szClause, 0xC00, "WHERE CampID = %d AND CharID = %d", dwCampID,
                         dwCharID) != 0)
    {
        BSAssertFail();
        return NULL;
    }
    if (!pTable->SelectWhere(szClause, 6, &listRows, 0))
    {
        CDBObjectList_DetachAndClear(&listRows, pTable);
        BSAssertFail();
        return NULL;
    }
    if (listRows.size() > 1)
        BSAssertFail();
    return (CInstanceTrainingCampMember*)listRows.front();
}

BOOL TrainingCamp_DBLoadMemberBuffs(DWORD dwCampID, CDBRecordList* pList, DWORD dwCharID)
{
    if (!dwCampID || !dwCharID)
        return FALSE;
    if (!g_pShardDB)
        BSAssertFail();
    CDBTable* pTable = g_pShardDB->GetTable(SHARD_TABLE_TRAINING_CAMP_BUFF);
    char szClause[0xC00];
    if (!pTable ||
        StringCchPrintfA(szClause, 0xC00,
                         "WHERE CampID = %d AND RecipientCharID = %d ORDER BY BuffSlotIdx ASC",
                         dwCampID, dwCharID) != 0)
    {
        BSAssertFail();
        return FALSE;
    }
    if (!pTable->SelectWhere(szClause, 6, pList, 0))
    {
        CDBObjectList_DetachAndClear(pList, pTable);
        BSAssertFail();
        return FALSE;
    }
    return pList->size() != 0;
}

CInstanceTrainingCamp* TrainingCamp_DBLoadCamp(DWORD dwCampID)
{
    CDBRecordList listRows;
    if (!g_pShardDB)
        BSAssertFail();
    std::vector<CDBTable*>& vecTables = g_pShardDB->GetInstanceTables();
    if (vecTables.size() <= SHARD_TABLE_TRAINING_CAMP)
    {
        BSAssertFail();
        BSAssertFail();
        return NULL;
    }
    CDBTable* pTable = vecTables[SHARD_TABLE_TRAINING_CAMP];
    char szClause[0xC00];
    if (!pTable)
    {
        BSAssertFail();
        return NULL;
    }
    if (StringCchPrintfA(szClause, 0xC00, "WHERE ID = %d", dwCampID) != 0)
    {
        BSAssertFail();
        return NULL;
    }
    if (!pTable->SelectWhere(szClause, 6, &listRows, 0))
    {
        CDBObjectList_DetachAndClear(&listRows, pTable);
        BSAssertFail();
        return NULL;
    }
    if (listRows.size() > 1)
        BSAssertFail();
    return (CInstanceTrainingCamp*)listRows.front();
}
