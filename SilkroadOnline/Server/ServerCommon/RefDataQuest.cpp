#include "ServerCommon/RefDataQuest.h"

#include <string.h>

#include "BSLib/BSDebug.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerCommon/RefDataMisc.h"
#include "ServerCommon/RefDataObj.h"
#include "ServerFramework/ServerDataExtern.h"

// The code name of a reward that is no rent item (0x783f08).
static const char s_szNoRentItem[] = "xxx";

// ---------------------------------------------------------------------------------------------
// Helpers

// The reward check of the item readers: "xxx" (case sensitive) or a known rent item.
static BOOL RefQuest_IsKnownRentCode(CReferenceData* pData, const std::string& strCodeName)
{
    const char* psz = strCodeName.c_str();
    if (CompareStringA(0x400, 0, psz, lstrlenA(psz), s_szNoRentItem, lstrlenA(s_szNoRentItem)) ==
        CSTR_EQUAL)
        return TRUE;
    return RefData_FindRentItem(pData, psz) != NULL;
}

// RefData_AddRow<Table> (0x4d1e40, 0x4d1f20, 0x4d2000, 0x4d2620, 0x4d2700): a row the reader
// rejects is freed; a key that is already in the map frees the row and fails.
template <class T, class TMap>
static int RefQuest_AddRowUnique(TMap& map, CDBRecord* pRecord)
{
    CRefRow row(static_cast<CDBRecStream*>(pRecord));
    T* pRow = new T;
    DWORD dwKey = (DWORD)RefRow_Read(row, *pRow);
    if (!dwKey)
    {
        delete pRow;
        return 1;
    }
    typename TMap::iterator it = map.find(dwKey);
    if (it != map.end() && it->second)
    {
        delete pRow;
        BSAssertFail();
        return 0;
    }
    map.insert(typename TMap::value_type(dwKey, pRow));
    return 1;
}

// RefData_BuildRef<Table> (0x4cf4f0, 0x4cf480, 0x4cee30, 0x4ceea0, 0x4cef10): FALSE for an empty
// table; the first row that fails asserts and stops the build.
template <class T, class TMap>
static int RefQuest_BuildUnique(TMap& map, RefTableEntry* pTable)
{
    if (pTable->m_listRows.size() == 0)
        return 0;
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        if (!RefQuest_AddRowUnique<T>(map, *it))
        {
            BSAssertFail();
            return 0;
        }
    }
    return 1;
}

// The builders of the tables with a loader per row (0x4d07f0, 0x4d0860, 0x4d08d0).
typedef int (*PFN_QUEST_LOAD)(CReferenceData* pData, CDBRecStream* pRecord);

static int RefQuest_BuildRows(CReferenceData* pData, RefTableEntry* pTable, PFN_QUEST_LOAD pfnLoad)
{
    if (pTable->m_listRows.size() == 0)
        return 0;
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        if (!pfnLoad(pData, static_cast<CDBRecStream*>(*it)))
        {
            BSAssertFail();
            return 0;
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------
// AI tables

int RefRow_Read(CRefRow& row, RefHiveRow& r)
{
    row >> r.HiveID >> r.KeepMonsterCountType >> r.OverwriteMaxTotalCount >> r.MonsterCountPerPC >>
        r.SpawnSpeed >> r.MaxIncrease >> r.Flag >> r.GameWorldID >> r.HatchObjType;
    return (int)r.HiveID;
}

int RefRow_Read(CRefRow& row, RefTacticsRow& r)
{
    row >> r.TacticsID >> r.dw04 >> r.b08 >> r.dw0C >> r.b10 >> r.dw14 >> r.b18 >> r.dw1C;
    for (int i = 0; i < 4; ++i)
        row >> r.ab20[i];
    row >> r.dw24 >> r.b28;
    for (int i = 0; i < 8; ++i)
        row >> r.adw2C[i];
    row >> r.b4C;
    for (int i = 0; i < 8; ++i)
        row >> r.adw50[i];
    row >> r.b70 >> r.dw74 >> r.b78 >> r.b79 >> r.dw7C >> r.b80 >> r.dw84 >> r.b88 >> r.b89 >>
        r.dw8C >> r.dw90 >> r.str94;

    CReferenceData::RefAISkillMap& mmapAISkill = CReferenceData::GetInstance()->m_mmapAISkill;
    std::pair<CReferenceData::RefAISkillMap::iterator, CReferenceData::RefAISkillMap::iterator>
        range = mmapAISkill.equal_range(r.TacticsID);
    for (CReferenceData::RefAISkillMap::iterator it = range.first; it != range.second; ++it)
    {
        RefAISkill* pAISkill = it->second;
        CReferenceData::RefSkillByCodeNameMap& mapSkill =
            CReferenceData::GetInstance()->m_mapSkillByCodeName;
        CReferenceData::RefSkillByCodeNameMap::iterator itSkill =
            mapSkill.find(pAISkill->SkillCodeName);
        if (itSkill == mapSkill.end() || !itSkill->second)
        {
            BSAssertFail();
            LogPrintf(0x2000001, "Invalid AIRefSkill is bound!! TacticsID[%d] SKILL[%s]",
                      r.TacticsID, pAISkill->SkillCodeName.c_str());
            return 0;
        }
        r.mapAISkill.insert(std::make_pair((DWORD)itSkill->second->ID, pAISkill));
    }
    return (int)r.TacticsID;
}

int RefRow_Read(CRefRow& row, RefNestRow& r)
{
    float fX, fY, fZ;
    row >> r.NestID >> r.HiveID >> r.TacticsID >> r.RegionDBID >> fX >> fY >> fZ;
    r.LocalPosX = fX;
    r.LocalPosY = fY;
    r.LocalPosZ = fZ;
    row >> r.InitialDir >> r.Radius >> r.GenerateRadius >> r.ChampionGenPercentage >>
        r.DelayTimeMin >> r.DelayTimeMax >> r.MaxTotalCount >> r.Flag >> r.Respawn >> r.Type;
    return (int)r.NestID;
}

int RefData_BuildHive(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    return RefQuest_BuildUnique<RefHiveRow>(pData->m_mapHive, pTable);
}

int RefData_BuildTactics(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    return RefQuest_BuildUnique<RefTacticsRow>(pData->m_mapTactics, pTable);
}

int RefData_LoadNest(CReferenceData* pData, RefTableEntry*, unsigned int)
{
    if (!RefQuest_BuildUnique<RefNestRow>(pData->m_mapNest, pData->m_pTables->at(0x3B)))
        return 0;
    for (CReferenceData::RefNestMap::iterator it = pData->m_mapNest.begin();
         it != pData->m_mapNest.end(); ++it)
    {
        RefNestRow* pNest = it->second;
        CReferenceData::RefHiveMap::iterator itHive = pData->m_mapHive.find(pNest->HiveID);
        RefHiveRow* pHive = itHive == pData->m_mapHive.end() ? NULL : itHive->second;
        if (!pHive)
            BSAssertFail();
        // BUG(SM-060): a nest of an unknown hive is appended through a NULL hive.
        pHive->vecNest.push_back(pNest);
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------
// Ranking tables

int RefRow_Read(CRefRow& row, RefRankingActivity& r)
{
    row >> r.Rank >> r.CharName >> r.b20 >> r.dw24 >> r.b28;
    return r.Rank;
}

int RefRow_Read(CRefRow& row, RefRankingContribution& r)
{
    row >> r.Rank >> r.CharName >> r.b20 >> r.dw24;
    return r.Rank;
}

// The packet writes of the ranking builders: an overflow asserts and throws.
class CRankingPacketWriter
{
public:
    CRankingPacketWriter(BYTE* pBuffer, int nCapacity)
        : m_pBuffer(pBuffer), m_nCapacity(nCapacity), m_nPos(0)
    {
    }

    template <class T>
    void Write(T value)
    {
        if (m_nPos + (int)sizeof(T) > m_nCapacity)
            BSAssertFailAndThrow();
        *(T*)(m_pBuffer + m_nPos) = value;
        m_nPos += sizeof(T);
    }

    void WriteName(const std::string& strName)
    {
        const char* psz = strName.c_str();
        int nLength = lstrlenA(psz);
        Write((WORD)nLength);
        if (nLength)
        {
            if (m_nPos + nLength > m_nCapacity)
                BSAssertFailAndThrow();
            memcpy(m_pBuffer + m_nPos, psz, nLength);
            m_nPos += nLength;
        }
    }

    int Pos() const
    {
        return m_nPos;
    }

private:
    BYTE*   m_pBuffer;
    int     m_nCapacity;
    int     m_nPos;
};

static void RefQuest_WriteRankingColumns(CRankingPacketWriter& writer,
                                         const RefRankingActivity& r)
{
    writer.Write(r.b20);
    writer.Write(r.dw24);
    writer.Write(r.b28);
}

static void RefQuest_WriteRankingColumns(CRankingPacketWriter& writer,
                                         const RefRankingContribution& r)
{
    writer.Write(r.b20);
    writer.Write(r.dw24);
}

template <class TMap>
static int RefQuest_BuildRankingTable(CStreamBuffer* pPacket, const TMap& mapRow)
{
    if (pPacket->m_nCapacity <= 0)
        BSAssertFail();
    int nCapacity = pPacket->m_nCapacity;
    int nReadPos = pPacket->m_nReadPos;
    pPacket->m_nWritePos = 0;
    if (nCapacity <= 0)
        BSAssertFail();
    BYTE* pBuffer = pPacket->m_pBuffer;
    size_t nCount = mapRow.size();
    if (!pBuffer)
        BSAssertFail();
    CRankingPacketWriter writer(pBuffer, nCapacity);
    writer.Write((BYTE)nCount);
    typename TMap::const_iterator it = mapRow.begin();
    for (size_t i = 0; i < nCount; ++i, ++it)
    {
        writer.Write(it->second->Rank);
        writer.WriteName(it->second->CharName);
        RefQuest_WriteRankingColumns(writer, *it->second);
    }
    if (nReadPos != pPacket->m_nReadPos)
        pPacket->m_nReadPos = nReadPos;
    if (writer.Pos() != pPacket->m_nWritePos)
    {
        // A packet filled to its last byte asserts as well.
        if (writer.Pos() < 0 || writer.Pos() >= nCapacity)
            BSAssertFail();
        pPacket->m_nWritePos = writer.Pos();
    }
    return 0;
}

int RefData_BuildRankingActivityTable(RefRankingPackets& packets, BYTE byType,
                                      const RefRankingActivityMap& mapRow)
{
    return RefQuest_BuildRankingTable(packets.m_vecActivity.at(byType - 1), mapRow);
}

int RefData_BuildRankingContributionTable(RefRankingPackets& packets, BYTE byType,
                                          const RefRankingContributionMap& mapRow)
{
    return RefQuest_BuildRankingTable(packets.m_vecContribution.at(byType - 1), mapRow);
}

// The packet is written even when the table fails; the rows stay allocated.
int RefData_LoadRankingActivity(CReferenceData* pData, RefTableEntry*, unsigned int nSlot)
{
    RefRankingActivityMap mapRow;
    int nResult = RefQuest_BuildUnique<RefRankingActivity>(mapRow, pData->m_pTables->at(nSlot));
    RefData_BuildRankingActivityTable(pData->m_RankingPackets, (BYTE)((nSlot - 0x40) / 2 + 1),
                                      mapRow);
    // BUG(SM-061): the map goes without freeing its rows.
    return nResult;
}

int RefData_LoadRankingContribution(CReferenceData* pData, RefTableEntry*, unsigned int nSlot)
{
    RefRankingContributionMap mapRow;
    int nResult =
        RefQuest_BuildUnique<RefRankingContribution>(mapRow, pData->m_pTables->at(nSlot));
    RefData_BuildRankingContributionTable(pData->m_RankingPackets,
                                          (BYTE)((nSlot - 0x41) / 2 + 1), mapRow);
    // BUG(SM-061): the map goes without freeing its rows.
    return nResult;
}

// ---------------------------------------------------------------------------------------------
// Quest and event reward tables

RefQuestReward* RefData_FindQuestReward(CReferenceData* pData, DWORD dwQuestID)
{
    CReferenceData::RefQuestRewardMap::iterator it = pData->m_mapQuestReward.find(dwQuestID);
    if (it == pData->m_mapQuestReward.end())
        return NULL;
    return it->second;
}

RefEventRewardRow* RefData_FindEventReward(CReferenceData* pData, DWORD dwID)
{
    CReferenceData::RefEventRewardMap::iterator it = pData->m_mapEventReward.find(dwID);
    if (it == pData->m_mapEventReward.end())
        return NULL;
    return it->second;
}

int RefRow_Read(CRefRow& row, RefQuestRewardItemsRow& r)
{
    BYTE bService;
    row >> bService;
    if (!bService)
        return 0;
    row >> r.QuestID;
    if (!RefData_FindQuestReward(CReferenceData::GetInstance(), r.QuestID))
        return 0;
    row.Skip();
    std::string strItemCodeName;
    row >> r.b04 >> strItemCodeName >> r.str0C >> r.dw28 >> r.dw2C >> r.str30 >> r.dw4C >>
        r.str50 >> r.dw6C >> r.str70;
    CReferenceData* pData = CReferenceData::GetInstance();
    RefObjCommonRow* pObj = pData->FindRefObjByCodeName(strItemCodeName);
    r.ItemObjID = pObj ? pObj->ID : 0;
    return RefQuest_IsKnownRentCode(CReferenceData::GetInstance(), r.str30);
}

int RefRow_Read(CRefRow& row, RefEventRewardRow& r)
{
    BYTE bService;
    row >> bService;
    if (!bService)
        return 0;
    row >> r.ID;
    if (r.ID < 10000)
        return 0;
    for (int i = 0; i < 8; ++i)
        row.Skip();
    row >> r.dw08 >> r.dw0C >> r.dw10 >> r.dw14 >> r.b18 >> r.b19 >> r.b04 >> r.b05 >> r.dw1C >>
        r.str20 >> r.dw3C >> r.str40 >> r.dw5C >> r.str60;
    return 1;
}

int RefRow_Read(CRefRow& row, RefEventRewardItemsRow& r)
{
    BYTE bService;
    row >> bService;
    if (!bService)
        return 0;
    row >> r.EventRewardID;
    if (!RefData_FindEventReward(CReferenceData::GetInstance(), r.EventRewardID))
        return 0;
    row.Skip();
    std::string strItemCodeName;
    row >> strItemCodeName >> r.dw08 >> r.f0C >> r.str10 >> r.dw2C >> r.str30 >> r.dw4C >>
        r.str50;
    RefObjCommonRow* pObj = CReferenceData::GetInstance()->FindRefObjByCodeName(strItemCodeName);
    if (!pObj)
        return 0;
    r.ItemObjID = pObj->ID;
    return RefQuest_IsKnownRentCode(CReferenceData::GetInstance(), r.str10);
}

// CRefInstanceGenerator::LoadQuestRewardItems (0x4a6f60): the items of a quest and group in the
// order of the table (keyed by their index).
static int RefQuest_LoadQuestRewardItems(CReferenceData* pData, CDBRecStream* pRecord)
{
    CRefRow row(pRecord);
    RefQuestRewardItemsRow* pRow = new RefQuestRewardItemsRow;
    if (!RefRow_Read(row, *pRow))
    {
        delete pRow;
        return 1;
    }
    CReferenceData::RefQuestRewardItemsMap& map = pData->m_mapQuestRewardItems;
    CReferenceData::RefQuestRewardItemsMap::iterator it = map.find(pRow->QuestID);
    if (it == map.end())
    {
        CReferenceData::RefQuestRewardItemList* pList = new CReferenceData::RefQuestRewardItemList;
        pList->insert(std::make_pair((DWORD)pList->size(), pRow));
        CReferenceData::RefQuestRewardItemGroupMap* pGroups =
            new CReferenceData::RefQuestRewardItemGroupMap;
        pGroups->insert(std::make_pair(pRow->b04, pList));
        map.insert(std::make_pair(pRow->QuestID, pGroups));
        return 1;
    }
    CReferenceData::RefQuestRewardItemGroupMap* pGroups = it->second;
    CReferenceData::RefQuestRewardItemGroupMap::iterator itGroup = pGroups->find(pRow->b04);
    if (itGroup == pGroups->end())
    {
        CReferenceData::RefQuestRewardItemList* pList = new CReferenceData::RefQuestRewardItemList;
        pList->insert(std::make_pair((DWORD)pList->size(), pRow));
        pGroups->insert(std::make_pair(pRow->b04, pList));
        return 1;
    }
    CReferenceData::RefQuestRewardItemList* pList = itGroup->second;
    pList->insert(std::make_pair((DWORD)pList->size(), pRow));
    return 1;
}

// CRefInstanceGenerator::LoadEventReward (0x4a7230).
static int RefQuest_LoadEventReward(CReferenceData* pData, CDBRecStream* pRecord)
{
    CRefRow row(pRecord);
    RefEventRewardRow* pRow = new RefEventRewardRow;
    if (!RefRow_Read(row, *pRow))
    {
        delete pRow;
        return 1;
    }
    // BUG(SM-007): a duplicate ID is not inserted and leaks.
    pData->m_mapEventReward.insert(std::make_pair(pRow->ID, pRow));
    return 1;
}

// CRefInstanceGenerator::LoadEventRewardItems (0x4a7350).
static int RefQuest_LoadEventRewardItems(CReferenceData* pData, CDBRecStream* pRecord)
{
    CRefRow row(pRecord);
    RefEventRewardItemsRow* pRow = new RefEventRewardItemsRow;
    if (!RefRow_Read(row, *pRow))
    {
        delete pRow;
        return 1;
    }
    CReferenceData::RefEventRewardItemsMap& map = pData->m_mapEventRewardItems;
    CReferenceData::RefEventRewardItemsMap::iterator it = map.find(pRow->EventRewardID);
    if (it == map.end())
    {
        CReferenceData::RefEventRewardItemList* pList = new CReferenceData::RefEventRewardItemList;
        pList->insert(std::make_pair(pRow->ItemObjID, pRow));
        map.insert(std::make_pair(pRow->EventRewardID, pList));
        return 1;
    }
    // BUG(SM-007): a second row of the same item is not inserted and leaks.
    it->second->insert(std::make_pair(pRow->ItemObjID, pRow));
    return 1;
}

int RefData_BuildQuestRewardItems(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    return RefQuest_BuildRows(pData, pTable, RefQuest_LoadQuestRewardItems);
}

int RefData_BuildEventReward(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    return RefQuest_BuildRows(pData, pTable, RefQuest_LoadEventReward);
}

int RefData_BuildEventRewardItems(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    return RefQuest_BuildRows(pData, pTable, RefQuest_LoadEventRewardItems);
}

// ---------------------------------------------------------------------------------------------
// Event items

static BOOL RefQuest_StrEqualNoCase(const char* psz1, const char* psz2)
{
    return CompareStringA(0x400, NORM_IGNORECASE | SORT_STRINGSORT, psz1, lstrlenA(psz1), psz2,
                          lstrlenA(psz2)) == CSTR_EQUAL;
}

static const char* const s_apszNoEventItem[] =
{
    "ITEM_EVENT_SILSAMO_M",
    "ITEM_EVENT_SILSAMO_F",
    "ITEM_EVENT_E081126_SILKROAD_PLAG_M",
    "ITEM_EVENT_E081126_SILKROAD_PLAG_W",
};

void RefData_BuildEventItemList(CReferenceData* pData)
{
    for (CReferenceData::RefObjHashMap::iterator it = pData->m_hashRefObj.begin();
         it != pData->m_hashRefObj.end(); ++it)
    {
        RefObjCommonRow* pObj = it->second;
        WORD wTypeID = pObj->TypeID;
        if ((wTypeID & 2) || (wTypeID & 0x1C) != 0xC || (wTypeID & 0x60) != 0x20)
            continue;
        WORD wTID3 = (wTypeID >> 7) & 0xF;
        if (wTID3 == 5 || wTID3 == 12)
            continue;
        if ((wTypeID & 0x780) == 0x300 && (wTypeID & 0xF800) == 0x8000)
            continue;
        if (!(pObj->Flags & REFOBJ_CAN_TRADE))
            continue;
        const char* pszCodeName = pObj->CodeName128.c_str();
        BOOL bExcluded = FALSE;
        for (int i = 0; i < 4 && !bExcluded; ++i)
            bExcluded = RefQuest_StrEqualNoCase(s_apszNoEventItem[i], pszCodeName);
        if (bExcluded)
            continue;
        int nClass = static_cast<const RefObjItem*>(pObj)->ItemClass - 1;
        if (RefObj_IsEquipOfCountry(pObj, 0))
        {
            pData->m_map9CC[(DWORD)(nClass / 3)].push_back(pObj);
        }
        else if (RefObj_IsEquipOfCountry(pObj, 2))
        {
            CReferenceData::RefObjListByIDMap* pMap;
            switch (nClass % 3)
            {
            case 0:
                pMap = &pData->m_map9D8;
                break;
            case 1:
                pMap = &pData->m_map9E4;
                break;
            case 2:
                pMap = &pData->m_map9F0;
                break;
            default:
                // item class 0
                BSAssertFail();
                continue;
            }
            (*pMap)[(DWORD)(nClass / 3)].push_back(pObj);
        }
    }
}
