// SR_ShardManager reference data (SilkroadOnline\Server\ServerCommon\ReferenceData.h): the row
// reader, BuildAllTables over a few tables built from small fake row sets, the lookups and the
// log line of an empty table. Compiled with SHARD_DEFS.

#include "ServerCommon/ReferenceData.h"

#include <string.h>

#include <new>

#include "BSNet/NetEngine.h"

#include "LogCapture.h"
#include "TestCheck.h"

// ---------------------------------------------------------------------------------------------
// Fake rows: the columns in the stream format CDBRecStream::SetField writes.

static void PutDword(CDBRecStream* pRow, DWORD dw)
{
    pRow->WriteRaw(4, &dw);
}

static void PutWord(CDBRecStream* pRow, WORD w)
{
    pRow->WriteRaw(2, &w);
}

static void PutByte(CDBRecStream* pRow, BYTE b)
{
    pRow->WriteRaw(1, &b);
}

// WORD length, WORD field size (header included), the characters, padding to the field size.
static void PutString(CDBRecStream* pRow, const char* psz, WORD wFieldSize)
{
    WORD wLength = (WORD)strlen(psz);
    PutWord(pRow, wLength);
    PutWord(pRow, wFieldSize);
    pRow->WriteRaw(wLength, psz);
    for (int i = wLength + 4; i < wFieldSize; ++i)
        PutByte(pRow, 0xCC);
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
        pRecord->AllocData(256);
        entry.m_listRows.push_back(pRecord);
        return pRecord;
    }

    // A CDBRecord row (columns in the members).
    TRecord* AddRecord()
    {
        TRecord* pRecord = new TRecord;
        entry.m_listRows.push_back(pRecord);
        return pRecord;
    }
};

// ---------------------------------------------------------------------------------------------

static void TestRowReader()
{
    CRefTrigger record;
    record.AllocData(64);
    PutString(&record, "abc", 10);
    PutString(&record, "", 6);
    PutDword(&record, 7);

    CRefRow row(&record);
    std::string s1, s2("old");
    DWORD dw = 0;
    row >> s1 >> s2 >> dw;
    CHECK(s1 == "abc" && s2 == "old" && dw == 7);
    CHECK(record.m_nReadPos == 20 && record.m_nFieldCount == 3);
}

static void TestTriggerTables(CReferenceData& data, RefTableVector& tables)
{
    FakeTable<CRefTrigger> trigger;
    CDBRecStream* pRow = trigger.AddRow();
    PutDword(pRow, 1);
    PutDword(pRow, 10);
    PutString(pRow, "TRIGGER_A", 20);
    PutByte(pRow, 1);
    PutByte(pRow, 0);
    PutString(pRow, "comment", 12);
    PutDword(pRow, 3);
    pRow = trigger.AddRow();                    // out of service: dropped
    PutDword(pRow, 0);
    PutDword(pRow, 11);
    pRow = trigger.AddRow();                    // duplicate ID: the first row stays
    PutDword(pRow, 1);
    PutDword(pRow, 10);
    PutString(pRow, "TRIGGER_B", 20);
    PutByte(pRow, 0);
    PutByte(pRow, 0);
    PutString(pRow, "", 4);
    PutDword(pRow, 4);
    tables[0x6E] = &trigger.entry;

    FakeTable<CRefTriggerBindEvent> bind;
    for (int i = 0; i < 2; ++i)
    {
        pRow = bind.AddRow();
        PutDword(pRow, 1);
        PutDword(pRow, 100 + i);
        PutDword(pRow, 10);
        PutDword(pRow, 500 + i);
    }
    tables[0x70] = &bind.entry;

    FakeTable<CRefTriggerConditionParam> param;
    pRow = param.AddRow();
    PutDword(pRow, 1);
    PutDword(pRow, 7);
    PutString(pRow, "GROUP", 16);
    PutString(pRow, "VALUE_CODE", 16);
    PutString(pRow, "42", 8);
    PutString(pRow, "int", 8);
    tables[0x77] = &param.entry;

    FakeTable<CRefGameWorldGroup_Config> empty;
    tables[0x7B] = &empty.entry;

    FakeTable<CRefGameWorldGroup> group;
    pRow = group.AddRow();
    PutDword(pRow, 3);
    PutString(pRow, "GROUP_A", 12);
    PutString(pRow, "obj", 8);
    PutString(pRow, "CONFIG_A", 12);
    tables[0x7A] = &group.entry;

    CNetEngine engine;
    IBSNet* pOld = g_pBSNet;
    g_pBSNet = &engine;
    LogCapture_Start();
    CHECK(data.BuildAllTables() == TRUE);
    std::vector<CAPTURED_LOG> logs = LogCapture_Drain();
    LogCapture_Stop();
    g_pBSNet = pOld;

    CHECK(data.m_mapTrigger.size() == 1);
    if (data.m_mapTrigger.size() == 1)
    {
        RefTrigger* p = data.m_mapTrigger.begin()->second;
        CHECK(p->ID == 10 && p->CodeName128 == "TRIGGER_A" && p->IsActive == 1);
        CHECK(p->Comment512 == "comment" && p->IndexNumber == 3);
    }
    CHECK(data.m_mmapTriggerBindEvent.count(10) == 2);
    CHECK(data.m_mapTriggerConditionParam.size() == 1);
    if (data.m_mapTriggerConditionParam.size() == 1)
    {
        RefTriggerConditionParam* p = data.m_mapTriggerConditionParam["GROUP"];
        CHECK(p->ID == 7 && p->ValueCodeName128 == "VALUE_CODE" && p->Value == "42");
        CHECK(p->Type == "int");
    }
    CHECK(data.m_mapGameWorldGroup.size() == 1 &&
          data.m_mapGameWorldGroup[3]->ConfigGroupCodeName128 == "CONFIG_A");

    bool bLogged = false;
    for (size_t i = 0; i < logs.size(); ++i)
    {
        if (logs[i].dwFlags == 0x2000000 &&
            logs[i].strText == "!!! there is no record in this[_RefGameWorldGroup_Config] "
                               "table !!!")
            bLogged = true;
    }
    CHECK(bLogged);

    // an empty table fails its builder
    CHECK(data.BuildFromTable(&empty.entry, 0x7B) == 0);
    // a second build reads the rows again from their start; the duplicate is not inserted
    static_cast<CDBRecStream*>(group.entry.m_listRows.front())->m_nReadPos = 0;
    CHECK(data.BuildFromTable(&group.entry, 0x7A) == 1 && data.m_mapGameWorldGroup.size() == 1);

    for (size_t i = 0; i < tables.size(); ++i)
        tables[i] = NULL;
}

static void TestSiegeTables(CReferenceData& data)
{
    FakeTable<CRefSiegeFortress> fortress;
    CRefSiegeFortress* pFortress = fortress.AddRecord();
    pFortress->m_Data.Service = 1;
    pFortress->m_Data.FortressID = 1;
    pFortress->m_Data.CodeName128 = "FORTRESS_JANGAN";
    pFortress->m_Data.RequestFee = 500;
    fortress.AddRecord();                       // out of service: freed
    CHECK(data.BuildFromTable(&fortress.entry, 0x48) == 1);
    CHECK(data.m_vecSiegeFortress.size() == 1);
    if (data.m_vecSiegeFortress.size() == 1)
    {
        CHECK(data.m_vecSiegeFortress[0]->CodeName128 == "FORTRESS_JANGAN");
        CHECK(data.m_vecSiegeFortress[0]->RequestFee == 500);
    }

    // keyed by FortressID whatever the Service column says (SM-009); no fortress: freed
    FakeTable<CRefSiegeFortressGuard> guard;
    for (int i = 0; i < 3; ++i)
    {
        CRefSiegeFortressGuard* p = guard.AddRecord();
        p->FortressID = i < 2 ? 1 : 0;
        p->GuardRefObjID = 100 + i;
    }
    CHECK(data.BuildFromTable(&guard.entry, 0x49) == 1);
    CHECK(data.m_mmapSiegeFortressGuard.size() == 2 &&
          data.m_mmapSiegeFortressGuard.count(1) == 2);

    // summoned monsters by original monster and level
    static BYTE abObj[0x260];
    abObj[0x198] = 3;
    data.m_hashRefObj[1234] = (RefObjCommonRow*)abObj;
    FakeTable<CRefSiegeLvlSummonMonster> summon;
    CRefSiegeLvlSummonMonster* pSummon = summon.AddRecord();
    pSummon->RefObjID = 1234;
    pSummon->RefOrgObjID = 77;
    CHECK(data.BuildFromTable(&summon.entry, 0x4D) == 1);
    CHECK(data.m_mapSiegeLvlSummonMonster.size() == 1);
    if (data.m_mapSiegeLvlSummonMonster.size() == 1)
    {
        CReferenceData::RefSiegeSummonByLevelVector* pLevels =
            data.m_mapSiegeLvlSummonMonster[77];
        CHECK(pLevels->size() == 140 && pLevels->at(2) != NULL && pLevels->at(1) == NULL);
        CHECK(pLevels->at(2)->RefObjID == 1234);
    }
    // the same level again fails the build
    CHECK(data.BuildFromTable(&summon.entry, 0x4D) == 0);
    data.m_hashRefObj.erase(1234);

    // unique key: a duplicate fails the build, a row out of service is skipped
    FakeTable<CRefSiegeBlessBuff> bless;
    CRefSiegeBlessBuff* pBless = bless.AddRecord();
    pBless->Service = 1;
    pBless->BlessID = 5;
    pBless->NeedGold = 0x100000000LL;
    pBless = bless.AddRecord();
    pBless->BlessID = 6;
    CHECK(data.BuildFromTable(&bless.entry, 0x4F) == 1);
    CHECK(data.m_mapSiegeBlessBuff.size() == 1 &&
          data.m_mapSiegeBlessBuff[5]->NeedGold == 0x100000000LL);
    CHECK(data.BuildFromTable(&bless.entry, 0x4F) == 0);

    FakeTable<CRefSiegeQuest> quest;
    CRefSiegeQuest* pQuest = quest.AddRecord();
    pQuest->m_Data.Service = 1;
    pQuest->m_Data.QuestID = 9;
    pQuest->m_Data.QuestType = 2;
    pQuest->m_Data.QuestName = "SIEGE_QUEST";
    CHECK(data.BuildFromTable(&quest.entry, 0x51) == 1);
    CHECK(data.m_mmapSiegeQuest.count(2) == 1 &&
          data.m_mmapSiegeQuest.find(2)->second->QuestName == "SIEGE_QUEST");

    FakeTable<CRefSiegeQuestReward> reward;
    CHECK(data.BuildFromTable(&reward.entry, 0x52) == 0);
}

static void TestGameWorldTables(CReferenceData& data)
{
    FakeTable<CRefGameWorldNPC> npc;
    for (int i = 0; i < 3; ++i)
    {
        CDBRecStream* pRow = npc.AddRow();
        PutDword(pRow, i < 2 ? 10 + i : 0);     // ID 0: dropped
        PutString(pRow, "WORLD_A", 12);
        PutString(pRow, i ? "NPC_B" : "NPC_A", 12);
        for (int w = 0; w < 4; ++w)
            PutWord(pRow, (WORD)(w + 1));
        for (int d = 0; d < 10; ++d)
            PutDword(pRow, 100 + d);
    }
    CHECK(data.BuildFromTable(&npc.entry, 0x5C) == 1);
    CHECK(data.m_mapGameWorldNPC.size() == 1 && data.m_mapGameWorldNPC["WORLD_A"].size() == 2);
    if (data.m_mapGameWorldNPC["WORLD_A"].size() == 2)
    {
        const RefGameWorldNPCEntry& e = data.m_mapGameWorldNPC["WORLD_A"][1];
        CHECK(e.str00 == "NPC_B" && e.aw1C[3] == 4 && e.adw24[9] == 109);
    }

    FakeTable<CRefObjCharExtraSkill> extra;
    for (int i = 0; i < 2; ++i)
    {
        CDBRecStream* pRow = extra.AddRow();
        PutDword(pRow, i == 0);                 // second row out of service
        PutDword(pRow, 500 + i);
        for (int s = 0; s < 20; ++s)
            PutDword(pRow, s);
    }
    CHECK(data.BuildFromTable(&extra.entry, 0x5D) == 1);
    CHECK(data.m_mapObjCharExtraSkill.size() == 1 &&
          data.m_mapObjCharExtraSkill[500]->aSkillID[19] == 19);

    FakeTable<CRefGameWorldConfig> config;
    CDBRecStream* pRow = config.AddRow();
    PutDword(pRow, 1);
    PutDword(pRow, 3);
    PutString(pRow, "CONFIG_A", 12);
    PutString(pRow, "MAX_USER", 12);
    PutString(pRow, "64", 8);
    PutString(pRow, "int", 8);
    CHECK(data.BuildFromTable(&config.entry, 0x5E) == 1);
    CHECK(data.m_mapGameWorldConfig.size() == 1 &&
          data.m_mapGameWorldConfig["CONFIG_A"]->Value == "64");
}

static void PutTimestamp(CDBRecStream* pRow, SQLSMALLINT nYear)
{
    TIMESTAMP_STRUCT ts = { nYear, 6, 1, 12, 0, 0, 0 };
    pRow->WriteRaw(sizeof(ts), &ts);
}

static void TestEventAndRewardTables(CReferenceData& data)
{
    // server events: one that has ended is dropped
    FakeTable<CRefServerEvent> event;
    for (int i = 0; i < 2; ++i)
    {
        CDBRecStream* pRow = event.AddRow();
        PutByte(pRow, 1);
        PutDword(pRow, 40 + i);
        PutByte(pRow, 0);
        PutDword(pRow, 0);
        PutString(pRow, "EVENT", 12);
        PutTimestamp(pRow, 2000);
        PutTimestamp(pRow, i ? 2001 : 2099);
        PutByte(pRow, 0);
        PutByte(pRow, 0);
        PutWord(pRow, 0);
        PutDword(pRow, 0);
        PutByte(pRow, 7);
        PutDword(pRow, 0);
        PutByte(pRow, 0);
    }
    CHECK(data.BuildFromTable(&event.entry, 0x60) == 1);
    CHECK(data.m_mapServerEvent.size() == 1 && data.FindServerEvent(40) != NULL &&
          data.FindServerEvent(40)->b50 == 7);

    // a reward of type 1 and its detail row: the percentages are clamped to [0, 1]
    FakeTable<CRefServerEventReward> reward;
    CDBRecStream* pRow = reward.AddRow();
    PutByte(pRow, 1);
    PutDword(pRow, 900);
    PutDword(pRow, 40);
    PutDword(pRow, 0);
    PutByte(pRow, 0);
    PutByte(pRow, RefServerEventReward::TYPE_EXP_UP_FOR_PLAYERS);
    PutByte(pRow, 0);
    CHECK(data.BuildFromTable(&reward.entry, 0x61) == 1);
    FakeTable<CRefServerEventReward_ExpUPForPlayers> exp;
    pRow = exp.AddRow();
    PutDword(pRow, 900);
    PutDword(pRow, 5);
    PutByte(pRow, 50);
    PutByte(pRow, 150);
    pRow = exp.AddRow();                        // unknown reward: skipped
    PutDword(pRow, 901);
    CHECK(data.BuildFromTable(&exp.entry, 0x62) == 1);
    RefServerEventReward_ExpUPForPlayers* pExp =
        static_cast<RefServerEventReward_ExpUPForPlayers*>(data.FindServerEventReward(900));
    CHECK(pExp != NULL && data.FindServerEventReward(901) == NULL);
    if (pExp)
        CHECK(pExp->dw18 == 5 && pExp->fRate1 == 0.5f && pExp->fRate2 == 1.0f);
    CHECK(data.m_mmapServerEventRewardID.count(40) == 1);

    // rent item: two of the five values; a row out of service fails the build (SM-011)
    FakeTable<CRefRentItem> rent;
    pRow = rent.AddRow();
    PutDword(pRow, 1);
    PutString(pRow, "ITEM_RENT", 16);
    PutDword(pRow, 3);
    PutByte(pRow, 0);
    PutByte(pRow, 0);
    PutDword(pRow, 2);
    PutTimestamp(pRow, 2010);
    PutTimestamp(pRow, 2011);
    PutByte(pRow, 2);
    PutDword(pRow, 11);
    PutDword(pRow, 12);
    CHECK(data.BuildFromTable(&rent.entry, 0x55) == 1);
    CHECK(data.m_hashRentItem.size() == 1);
    if (data.m_hashRentItem.size() == 1)
    {
        RefRentItem* pRent = data.m_hashRentItem["ITEM_RENT"];
        CHECK(pRent->dw00 == 3 && pRent->vecValue.size() == 2 && pRent->vecValue[1] == 12);
    }
    pRow = rent.AddRow();
    PutDword(pRow, 0);
    static_cast<CDBRecStream*>(rent.entry.m_listRows.front())->m_nReadPos = 0;
    CHECK(data.BuildFromTable(&rent.entry, 0x55) == 0);

    // quest rewards: the quest must be known; n18 > 0 for the temple quests only
    FakeTable<CRefQuest> quest;
    CRefQuest* pQuest = quest.AddRecord();
    pQuest->m_Data.Service = 1;
    pQuest->m_Data.ID = 7;
    pQuest->m_Data.CodeName = "QNO_TEST";
    CHECK(data.BuildFromTable(&quest.entry, 0x3C) == 1 && data.m_mapQuest.size() == 1);
    CHECK(data.BuildFromTable(&quest.entry, 0x3C) == 0);
    FakeTable<CRefQuestReward> questReward;
    const char* apszName[3] = { "TEMPLE_HAROERIS", "QNO_OTHER", "QNO_UNKNOWN" };
    for (int i = 0; i < 3; ++i)
    {
        pRow = questReward.AddRow();
        PutByte(pRow, 1);
        PutDword(pRow, i < 2 ? 7 : 8);
        for (int d = 0; d < 5; ++d)
            PutDword(pRow, 1);
        PutString(pRow, apszName[i], 20);
        for (int b = 0; b < 4; ++b)
            PutByte(pRow, 0);
        PutDword(pRow, 0);
        PutString(pRow, "", 4);
        PutDword(pRow, 0);
        PutString(pRow, "", 4);
        PutDword(pRow, 0);
        PutString(pRow, "", 4);
    }
    CHECK(data.BuildFromTable(&questReward.entry, 0x64) == 1);
    CHECK(data.m_mapQuestReward.size() == 1 &&
          data.m_mapQuestReward[7]->QuestCodeName == "TEMPLE_HAROERIS");
}

static void TestFindRefObj(CReferenceData& data)
{
    static BYTE abObj[0x260];
    RefObjCommonRow* pObj = (RefObjCommonRow*)abObj;
    data.m_hashRefObj[1234] = pObj;
    CHECK(strcmp(RefData_GetObjectCodeName(1), "Unknown Object") == 0);
    std::string* pstrCodeName = new (abObj + 0x14) std::string("ITEM_CH_SWORD_01_A");
    CHECK(strcmp(RefData_GetObjectCodeName(1234), "ITEM_CH_SWORD_01_A") == 0);
    pstrCodeName->~basic_string();
    CHECK(CReferenceData::GetInstance() == &data);
    CHECK(data.FindRefObj(1234) == pObj && data.FindRefObj(1) == NULL);
    data.m_hashRefObj.erase(1234);

    static BYTE abRegion[0x60];
    std::string* pstrRegion = new (abRegion + 0x34) std::string("CHINA_JANGAN");
    data.m_mapRegion[25000] = (RefRegionRow*)abRegion;
    CHECK(strcmp(RefData_GetRegionName(25000), "CHINA_JANGAN") == 0);
    CHECK(strcmp(RefData_GetRegionName(1), "Unknown Region") == 0);

    // optional teleports: the region is looked up, an unknown one is logged
    FakeTable<CRefOptionalTeleport> teleport;
    for (int i = 0; i < 2; ++i)
    {
        CDBRecStream* pRow = teleport.AddRow();
        PutDword(pRow, 1);
        PutDword(pRow, 30 + i);
        PutString(pRow, "TELEPORT", 12);
        PutWord(pRow, i ? 1 : 25000);
        for (int w = 0; w < 6; ++w)
            PutWord(pRow, (WORD)(100 + w));
        for (int d = 0; d < 3; ++d)
            PutDword(pRow, 7);
    }
    CNetEngine engine;
    IBSNet* pOld = g_pBSNet;
    g_pBSNet = &engine;
    LogCapture_Start();
    CHECK(data.BuildFromTable(&teleport.entry, 0x59) == 1);
    std::vector<CAPTURED_LOG> logs = LogCapture_Drain();
    LogCapture_Stop();
    g_pBSNet = pOld;
    CHECK(data.m_mapOptionalTeleport.size() == 2);
    if (data.m_mapOptionalTeleport.size() == 2)
    {
        RefOptionalTeleport* p = data.m_mapOptionalTeleport[30];
        CHECK(p->dwRegion == (DWORD)(DWORD_PTR)abRegion && p->Pos_X == 100);
        CHECK(p->LevelMax == 105 && p->adw34[2] == 7);
        CHECK(data.m_mapOptionalTeleport[31]->dwRegion == 0);
    }
    CHECK(logs.size() == 1 && logs[0].dwFlags == 0x2000001 &&
          logs[0].strText == "Invalid Region!![ID:31][ObjName128:TELEPORT]");
    data.m_mapRegion.clear();
    pstrRegion->~basic_string();

    RefGameWorld world;
    data.m_hashGameWorldByCodeName["INS_TEMPLE_SETH"] = &world;
    CHECK(data.FindGameWorldByCodeName("INS_TEMPLE_SETH") == &world);
    CHECK(data.FindGameWorldByCodeName("INS_NONE") == NULL && data.FindSkill(1) == NULL);
    data.m_hashGameWorldByCodeName.clear();

    // game worlds by ID and by code name
    FakeTable<CRefGameWorld> gameWorld;
    CRefGameWorld* pWorld = gameWorld.AddRecord();
    pWorld->m_Data.ID = 3;
    pWorld->m_Data.WorldCodeName128 = "INS_TEMPLE_SETH";
    gameWorld.AddRecord()->m_Data.ID = 0;       // ID 0: freed
    CHECK(data.BuildFromTable(&gameWorld.entry, 0x3D) == 1);
    CHECK(data.FindGameWorld(3) != NULL &&
          data.FindGameWorldByCodeName("INS_TEMPLE_SETH") == data.FindGameWorld(3));
    CHECK(data.BuildFromTable(&gameWorld.entry, 0x3D) == 0);

    // copies into vectors: an instance world region without WorldID is freed
    FakeTable<CRefInstanceWorldRegion> worldRegion;
    worldRegion.AddRecord()->WorldID = 3;
    worldRegion.AddRecord()->WorldID = 0;
    CHECK(data.BuildFromTable(&worldRegion.entry, 0x3E) == 1);
    CHECK(data.m_vecInstanceWorldRegion.size() == 1 &&
          data.m_vecInstanceWorldRegion[0]->WorldID == 3);
    FakeTable<CRefLevel> level;
    level.AddRecord()->Lvl = 1;
    level.AddRecord()->Exp_M = 9;               // no Lvl: asserts, kept
    CHECK(data.BuildFromTable(&level.entry, 0x00) == 1);
    CHECK(data.m_vecLevel.size() == 2 && data.m_vecLevel[1]->Exp_M == 9);

    FakeTable<CRefClimate> climate;
    climate.AddRecord()->ID = 4;
    climate.AddRecord()->ID = 4;                // duplicate: fails the build
    CHECK(data.BuildFromTable(&climate.entry, 0x01) == 0 && data.m_mapClimate.size() == 1);

    // gacha NPCs by NPC_ID
    FakeTable<CRefGachaNpcMap> gachaNpc;
    CRefGachaNpcMap* pNpc = gachaNpc.AddRecord();
    pNpc->NPC_ID = 2000;
    pNpc->WasteGachaID = 5;
    CHECK(data.BuildFromTable(&gachaNpc.entry, 0x06) == 1);
    CHECK(data.m_mapGachaNpcMap.size() == 1 && data.m_mapGachaNpcMap[2000]->WasteGachaID == 5);
}

static void TestTypeHelpers()
{
    // TID1 3, TID2 1, TID3 6 and TID4 2 / 10 / 4
    WORD wSword = (WORD)((3 << 2) | (1 << 5) | (6 << 7) | (2 << 11));
    WORD wStaff = (WORD)((3 << 2) | (1 << 5) | (6 << 7) | (10 << 11));
    WORD wBow = (WORD)((3 << 2) | (1 << 5) | (6 << 7) | (4 << 11));
    CHECK(RefItem_IsSwordWeaponType(wSword) && !RefItem_IsStaffWeaponType(wSword));
    CHECK(RefItem_IsStaffWeaponType(wStaff) && !RefItem_IsSwordWeaponType(wStaff));
    CHECK(!RefItem_IsSwordWeaponType(wBow) && !RefItem_IsStaffWeaponType(wBow));
    WORD wNotItem = (WORD)(wSword | 2);
    CHECK(!RefItem_IsSwordWeaponType(wNotItem) && RefObj_TypeIdIsClass0C(wBow));
    CHECK(!RefObj_TypeIdIsClass0C(wNotItem));

    WORD awValue[6] = { 17, 2, 3, 4, 5, 6 };
    DWORD dw = 0xFFFFFFFF;
    RefObj_PackTypeID(dw, awValue);
    CHECK(dw == (1 | (2 << 6) | (3 << 10) | (4 << 15) | (5 << 20) | (6 << 26)));
}

int main()
{
    CReferenceData* pData = CReferenceData::GetInstance();
    CHECK(pData == g_pReferenceData && pData != NULL);
    RefTableVector tables(REF_TABLE_SLOT_COUNT, (RefTableEntry*)NULL);
    pData->m_pTables = &tables;

    TestRowReader();
    TestTriggerTables(*pData, tables);
    TestSiegeTables(*pData);
    TestGameWorldTables(*pData);
    TestEventAndRewardTables(*pData);
    TestFindRefObj(*pData);
    TestTypeHelpers();

    pData->m_pTables = NULL;
    return TEST_RESULT("ShardReferenceDataTest");
}
