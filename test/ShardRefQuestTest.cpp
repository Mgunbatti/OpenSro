// SR_ShardManager reference data (SilkroadOnline\Server\ServerCommon\RefDataQuest.h): the hive,
// tactics and nest tables, the ranking packets, the quest / event reward tables and the event
// item lists, built through CReferenceData::BuildFromTable from small fake row sets. Compiled
// with SHARD_DEFS.

#include "ServerCommon/RefDataQuest.h"

#include <string.h>

#include <new>

#include "TestCheck.h"

// ---------------------------------------------------------------------------------------------
// Fake rows (the stream format of CDBRecStream::SetField)

template <class T>
static void Put(CDBRecStream* pRow, T value)
{
    pRow->WriteRaw(sizeof(T), &value);
}

static void PutString(CDBRecStream* pRow, const char* psz)
{
    WORD wLength = (WORD)strlen(psz);
    Put(pRow, wLength);
    Put(pRow, (WORD)(wLength + 4));
    pRow->WriteRaw(wLength, psz);
}

template <class TRecord>
struct FakeTable
{
    CDBTable table;
    RefTableEntry entry;

    FakeTable()
    {
        table.m_pInfo = &TRecord::s_TableInfo;
        entry.m_pTable = &table;
    }

    ~FakeTable()
    {
        for (CDBRecordList::iterator it = entry.m_listRows.begin();
             it != entry.m_listRows.end(); ++it)
            delete *it;
        table.m_pInfo = NULL;
    }

    CDBRecStream* AddRow()
    {
        TRecord* pRecord = new TRecord;
        pRecord->AllocData(512);
        entry.m_listRows.push_back(pRecord);
        return pRecord;
    }
};

static void PutHive(CDBRecStream* pRow, DWORD dwID, WORD wGameWorldID)
{
    Put(pRow, dwID);
    Put(pRow, (BYTE)2);
    Put(pRow, (DWORD)30);
    Put(pRow, 1.5f);
    Put(pRow, (DWORD)4);
    Put(pRow, (DWORD)5);
    Put(pRow, (BYTE)6);
    Put(pRow, wGameWorldID);
    Put(pRow, (WORD)7);
}

static void PutNest(CDBRecStream* pRow, DWORD dwID, DWORD dwHiveID)
{
    Put(pRow, dwID);
    Put(pRow, dwHiveID);
    Put(pRow, (DWORD)5);
    Put(pRow, (WORD)25000);
    Put(pRow, 1.0f);
    Put(pRow, 2.0f);
    Put(pRow, 3.0f);
    Put(pRow, (WORD)90);
    for (int i = 0; i < 6; ++i)
        Put(pRow, (DWORD)(100 + i));
    Put(pRow, (BYTE)1);
    Put(pRow, (BYTE)0);
    Put(pRow, (BYTE)3);
}

static void PutTactics(CDBRecStream* pRow, DWORD dwID)
{
    Put(pRow, dwID);
    Put(pRow, (DWORD)1);
    Put(pRow, (BYTE)2);
    Put(pRow, (DWORD)3);
    Put(pRow, (BYTE)4);
    Put(pRow, (DWORD)5);
    Put(pRow, (BYTE)6);
    Put(pRow, (DWORD)7);
    for (int i = 0; i < 4; ++i)
        Put(pRow, (BYTE)(8 + i));
    Put(pRow, (DWORD)12);
    Put(pRow, (BYTE)13);
    for (int i = 0; i < 8; ++i)
        Put(pRow, (DWORD)(14 + i));
    Put(pRow, (BYTE)22);
    for (int i = 0; i < 8; ++i)
        Put(pRow, (DWORD)(23 + i));
    Put(pRow, (BYTE)31);
    Put(pRow, (DWORD)32);
    Put(pRow, (BYTE)33);
    Put(pRow, (BYTE)34);
    Put(pRow, (DWORD)35);
    Put(pRow, (BYTE)36);
    Put(pRow, (DWORD)37);
    Put(pRow, (BYTE)38);
    Put(pRow, (BYTE)39);
    Put(pRow, (DWORD)40);
    Put(pRow, (DWORD)41);
    PutString(pRow, "desc");
}

// ---------------------------------------------------------------------------------------------

static void TestAITables(CReferenceData& data, RefTableVector& tables)
{
    FakeTable<CRefHive> hive;
    PutHive(hive.AddRow(), 1, 20);
    PutHive(hive.AddRow(), 2, 21);
    PutHive(hive.AddRow(), 0, 22);                 // HiveID 0: dropped
    CHECK(data.BuildFromTable(&hive.entry, 0x38) == 1);
    CHECK(data.m_mapHive.size() == 2);
    RefHiveRow* pHive = data.m_mapHive[1];
    CHECK(pHive->KeepMonsterCountType == 2 && pHive->OverwriteMaxTotalCount == 30);
    CHECK(pHive->MonsterCountPerPC == 1.5f && pHive->MaxIncrease == 5 && pHive->Flag == 6);
    CHECK(pHive->GameWorldID == 20 && pHive->HatchObjType == 7);

    // a duplicate HiveID fails the build
    FakeTable<CRefHive> dup;
    PutHive(dup.AddRow(), 2, 23);
    CHECK(data.BuildFromTable(&dup.entry, 0x38) == 0);
    CHECK(data.m_mapHive[2]->GameWorldID == 21);

    FakeTable<CRefNest> nest;
    PutNest(nest.AddRow(), 11, 1);
    PutNest(nest.AddRow(), 10, 1);
    PutNest(nest.AddRow(), 12, 2);
    tables[0x3B] = &nest.entry;
    CHECK(data.BuildFromTable(&nest.entry, 0x3B) == 1);
    CHECK(data.m_mapNest.size() == 3);
    RefNestRow* pNest = data.m_mapNest[10];
    CHECK(pNest->RegionDBID == 25000 && pNest->LocalPosY == 2.0f && pNest->InitialDir == 90);
    CHECK(pNest->Radius == 100 && pNest->MaxTotalCount == 105 && pNest->Respawn == 0);
    CHECK(pNest->Type == 3);
    // in the order of the nest map
    CHECK(pHive->vecNest.size() == 2 && pHive->vecNest[0] == pNest);
    CHECK(data.m_mapHive[2]->vecNest.size() == 1);
    tables[0x3B] = NULL;

    // tactics: the AI skills are bound by their skill ID
    RefSkill* pSkill = new RefSkill;
    pSkill->ID = 300;
    data.m_mapSkillByCodeName["SKILL_A"] = pSkill;
    RefAISkill aiA;
    aiA.TacticsID = 5;
    aiA.SkillCodeName = "SKILL_A";
    RefAISkill aiX;
    aiX.TacticsID = 6;
    aiX.SkillCodeName = "SKILL_X";
    data.m_mmapAISkill.insert(std::make_pair((DWORD)5, &aiA));
    data.m_mmapAISkill.insert(std::make_pair((DWORD)6, &aiX));
    FakeTable<CRefTactics> tactics;
    PutTactics(tactics.AddRow(), 5);
    PutTactics(tactics.AddRow(), 6);                // unknown skill: dropped
    PutTactics(tactics.AddRow(), 7);                // no AI skill
    CHECK(data.BuildFromTable(&tactics.entry, 0x3A) == 1);
    CHECK(data.m_mapTactics.size() == 2 && data.m_mapTactics.count(6) == 0);
    RefTacticsRow* pTactics = data.m_mapTactics[5];
    CHECK(pTactics->b08 == 2 && pTactics->ab20[3] == 11 && pTactics->adw2C[7] == 21);
    CHECK(pTactics->adw50[0] == 23 && pTactics->b89 == 39 && pTactics->dw90 == 41);
    CHECK(pTactics->str94 == "desc");
    CHECK(pTactics->mapAISkill.size() == 1 && pTactics->mapAISkill[300] == &aiA);
    CHECK(data.m_mapTactics[7]->mapAISkill.empty());

    data.m_mmapAISkill.clear();
    data.m_mapSkillByCodeName.clear();
    delete pSkill;
}

static void TestRanking(CReferenceData& data, RefTableVector& tables)
{
    FakeTable<CRefRankingRobberActivity> activity;
    const char* apszName[] = { "Bob", "Al", "" };
    for (int i = 0; i < 3; ++i)
    {
        CDBRecStream* pRow = activity.AddRow();
        Put(pRow, (BYTE)(i == 2 ? 0 : 2 - i));      // ranks 2, 1, 0 (dropped)
        PutString(pRow, apszName[i]);
        Put(pRow, (BYTE)(10 + i));
        Put(pRow, (DWORD)(1000 + i));
        Put(pRow, (BYTE)(20 + i));
    }
    tables[0x42] = &activity.entry;
    CHECK(data.BuildFromTable(&activity.entry, 0x42) == 1);
    CStreamBuffer* pPacket = data.m_RankingPackets.m_vecActivity.at(1);
    static const BYTE abActivity[] =
    {
        2,
        1, 2, 0, 'A', 'l', 11, 0xE9, 0x03, 0, 0, 21,
        2, 3, 0, 'B', 'o', 'b', 10, 0xE8, 0x03, 0, 0, 20,
    };
    CHECK(pPacket->m_nWritePos == sizeof(abActivity) && pPacket->m_nReadPos == 0);
    CHECK(memcmp(pPacket->m_pBuffer, abActivity, sizeof(abActivity)) == 0);
    // the other types keep their empty packet
    CHECK(data.m_RankingPackets.m_vecActivity.at(0)->m_nWritePos == 1);

    FakeTable<CRefRankingTraderContribution> contribution;
    CDBRecStream* pRow = contribution.AddRow();
    Put(pRow, (BYTE)1);
    PutString(pRow, "Cy");
    Put(pRow, (BYTE)3);
    Put(pRow, (DWORD)7);
    pRow = contribution.AddRow();                   // a duplicate rank fails the table
    Put(pRow, (BYTE)1);
    PutString(pRow, "Dee");
    Put(pRow, (BYTE)4);
    Put(pRow, (DWORD)8);
    tables[0x41] = &contribution.entry;
    CHECK(data.BuildFromTable(&contribution.entry, 0x41) == 0);
    pPacket = data.m_RankingPackets.m_vecContribution.at(0);
    static const BYTE abContribution[] = { 1, 1, 2, 0, 'C', 'y', 3, 7, 0, 0, 0 };
    CHECK(pPacket->m_nWritePos == sizeof(abContribution));
    CHECK(memcmp(pPacket->m_pBuffer, abContribution, sizeof(abContribution)) == 0);

    // a packet that overflows throws
    RefRankingPackets packets;
    RefRankingContributionMap mapRow;
    RefRankingContribution row;
    row.Rank = 1;
    row.CharName = std::string(1300, 'x');
    mapRow[1] = &row;
    bool bThrown = false;
    try
    {
        RefData_BuildRankingContributionTable(packets, 3, mapRow);
    }
    catch (int)
    {
        bThrown = true;
    }
    CHECK(bThrown);

    tables[0x41] = NULL;
    tables[0x42] = NULL;
}

static void PutQuestRewardItem(CDBRecStream* pRow, BYTE bService, DWORD dwQuestID, BYTE bGroup,
                               const char* pszItem, const char* pszRent, DWORD dwValue)
{
    Put(pRow, bService);
    if (!bService)
        return;
    Put(pRow, dwQuestID);
    Put(pRow, bGroup);
    PutString(pRow, pszItem);
    PutString(pRow, "s0C");
    Put(pRow, dwValue);
    Put(pRow, (DWORD)2);
    PutString(pRow, pszRent);
    Put(pRow, (DWORD)3);
    PutString(pRow, "s50");
    Put(pRow, (DWORD)4);
    PutString(pRow, "s70");
}

static void TestRewardTables(CReferenceData& data)
{
    static BYTE abObj[0x260];
    RefObjCommonRow* pObj = (RefObjCommonRow*)abObj;
    *(int*)(abObj + 8) = 4321;
    data.m_hashRefObjByCodeName["ITEM_A"] = pObj;
    RefRentItem rent;
    data.m_hashRentItem["RENT_OK"] = &rent;
    RefQuestReward* pReward = new RefQuestReward;
    data.m_mapQuestReward[7] = pReward;

    FakeTable<CRefQuestRewardItems> items;
    PutQuestRewardItem(items.AddRow(), 1, 7, 1, "ITEM_A", "xxx", 10);
    PutQuestRewardItem(items.AddRow(), 1, 7, 1, "ITEM_B", "RENT_OK", 11);  // unknown item: ID 0
    PutQuestRewardItem(items.AddRow(), 1, 7, 2, "ITEM_A", "xxx", 12);
    PutQuestRewardItem(items.AddRow(), 1, 8, 1, "ITEM_A", "xxx", 13);      // unknown quest
    PutQuestRewardItem(items.AddRow(), 1, 7, 1, "ITEM_A", "XXX", 14);      // unknown rent item
    PutQuestRewardItem(items.AddRow(), 0, 7, 1, "", "", 0);                 // out of service
    CHECK(data.BuildFromTable(&items.entry, 0x65) == 1);
    CHECK(data.m_mapQuestRewardItems.size() == 1);
    CReferenceData::RefQuestRewardItemGroupMap* pGroups = data.m_mapQuestRewardItems[7];
    CHECK(pGroups->size() == 2 && (*pGroups)[1]->size() == 2 && (*pGroups)[2]->size() == 1);
    RefQuestRewardItemsRow* pItem = (*(*pGroups)[1])[0];
    CHECK(pItem->ItemObjID == 4321 && pItem->dw28 == 10 && pItem->str0C == "s0C");
    CHECK(pItem->str30 == "xxx" && pItem->dw6C == 4 && pItem->str70 == "s70");
    pItem = (*(*pGroups)[1])[1];
    CHECK(pItem->ItemObjID == 0 && pItem->dw28 == 11 && pItem->str30 == "RENT_OK");

    FakeTable<CRefEventReward> reward;
    for (int i = 0; i < 2; ++i)
    {
        CDBRecStream* pRow = reward.AddRow();
        Put(pRow, (BYTE)1);
        Put(pRow, (DWORD)(i == 0 ? 10001 : 9999));  // below 10000: dropped
        for (int n = 0; n < 4; ++n)
            Put(pRow, (DWORD)(n + 1));
        for (int n = 0; n < 4; ++n)
            Put(pRow, (BYTE)(n + 5));
        Put(pRow, (DWORD)9);
        PutString(pRow, "a");
        Put(pRow, (DWORD)10);
        PutString(pRow, "b");
        Put(pRow, (DWORD)11);
        PutString(pRow, "c");
    }
    CHECK(data.BuildFromTable(&reward.entry, 0x66) == 1);
    CHECK(data.m_mapEventReward.size() == 1);
    RefEventRewardRow* pEvent = RefData_FindEventReward(&data, 10001);
    CHECK(pEvent != NULL && RefData_FindEventReward(&data, 9999) == NULL);
    CHECK(pEvent->dw08 == 1 && pEvent->dw14 == 4 && pEvent->b18 == 5 && pEvent->b19 == 6);
    CHECK(pEvent->b04 == 7 && pEvent->b05 == 8 && pEvent->dw1C == 9 && pEvent->str60 == "c");

    FakeTable<CRefEventRewardItems> eventItems;
    const char* apszItem[] = { "ITEM_A", "ITEM_Z", "ITEM_A" };
    const char* apszRent[] = { "RENT_OK", "xxx", "xxx" };
    for (int i = 0; i < 3; ++i)
    {
        CDBRecStream* pRow = eventItems.AddRow();
        Put(pRow, (BYTE)1);
        Put(pRow, (DWORD)(i == 2 ? 10002 : 10001));   // unknown event reward
        PutString(pRow, apszItem[i]);
        Put(pRow, (DWORD)(50 + i));
        Put(pRow, 0.25f);
        PutString(pRow, apszRent[i]);
        Put(pRow, (DWORD)1);
        PutString(pRow, "d");
        Put(pRow, (DWORD)2);
        PutString(pRow, "e");
    }
    CHECK(data.BuildFromTable(&eventItems.entry, 0x67) == 1);
    CHECK(data.m_mapEventRewardItems.size() == 1);
    CReferenceData::RefEventRewardItemList* pList = data.m_mapEventRewardItems[10001];
    CHECK(pList->size() == 1);
    RefEventRewardItemsRow* pEventItem = (*pList)[4321];
    CHECK(pEventItem->dw08 == 50 && pEventItem->f0C == 0.25f && pEventItem->str10 == "RENT_OK");
    CHECK(pEventItem->str50 == "e");

    CHECK(RefData_FindQuestReward(&data, 7) == pReward && RefData_FindQuestReward(&data, 8) == NULL);
    data.m_hashRefObjByCodeName.clear();
    data.m_hashRentItem.clear();
}

struct FakeObj
{
    BYTE ab[0x260];
};

static RefObjCommonRow* MakeObj(FakeObj& obj, int nID, WORD wTypeID, BYTE bCountry,
                                BYTE bClass, DWORD dwFlags, const char* pszCodeName)
{
    memset(obj.ab, 0, sizeof(obj.ab));
    *(int*)(obj.ab + 8) = nID;
    new (obj.ab + 0x14) std::string(pszCodeName);
    *(WORD*)(obj.ab + 0x80) = wTypeID;
    obj.ab[0x89] = bCountry;
    *(DWORD*)(obj.ab + 0x8C) = dwFlags;
    obj.ab[0x1A8] = bClass;
    return (RefObjCommonRow*)obj.ab;
}

static void TestEventItemList(CReferenceData& data)
{
    WORD wArmor = (WORD)((3 << 2) | (1 << 5) | (1 << 7));
    WORD wShield = (WORD)((3 << 2) | (1 << 5) | (5 << 7));
    static FakeObj aObj[6];
    RefObjCommonRow* apObj[6];
    apObj[0] = MakeObj(aObj[0], 1, wArmor, 0, 7, 1, "ITEM_EU_1");    // country 0, band 2
    apObj[1] = MakeObj(aObj[1], 2, wArmor, 2, 5, 1, "ITEM_EV_1");    // country 2, band 1, r 1
    apObj[2] = MakeObj(aObj[2], 3, wArmor, 2, 3, 0, "ITEM_EV_2");    // no event flag
    apObj[3] = MakeObj(aObj[3], 4, wShield, 0, 3, 1, "ITEM_EV_3");   // TID3 5
    apObj[4] = MakeObj(aObj[4], 5, wArmor, 0, 3, 1, "item_event_silsamo_m");
    apObj[5] = MakeObj(aObj[5], 6, wArmor, 2, 0, 1, "ITEM_EV_4");    // item class 0: asserts
    for (int i = 0; i < 6; ++i)
        data.m_hashRefObj[i + 1] = apObj[i];
    RefData_BuildEventItemList(&data);
    CHECK(data.m_map9CC.size() == 1 && data.m_map9CC[2].size() == 1 &&
          data.m_map9CC[2][0] == apObj[0]);
    CHECK(data.m_map9E4.size() == 1 && data.m_map9E4[1].size() == 1 &&
          data.m_map9E4[1][0] == apObj[1]);
    CHECK(data.m_map9D8.empty() && data.m_map9F0.empty());
    data.m_hashRefObj.clear();
    data.m_map9CC.clear();
    data.m_map9E4.clear();
    for (int i = 0; i < 6; ++i)
        reinterpret_cast<std::string*>(aObj[i].ab + 0x14)->~basic_string();
}

int main()
{
    CReferenceData* pData = CReferenceData::GetInstance();
    RefTableVector tables(REF_TABLE_SLOT_COUNT, (RefTableEntry*)NULL);
    pData->m_pTables = &tables;

    TestAITables(*pData, tables);
    TestRanking(*pData, tables);
    TestRewardTables(*pData);
    TestEventItemList(*pData);

    pData->m_pTables = NULL;
    return TEST_RESULT("ShardRefQuestTest");
}
