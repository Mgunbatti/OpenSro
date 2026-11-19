// SR_ShardManager object reference data (SilkroadOnline\Server\ServerCommon\RefDataObj.h): the
// TypeID helpers, slot 0xB (_RefObjCommon with its item / char / struct rows and the
// post-processing), CRefCharGen and the skill tables, built from small fake row sets. Compiled
// with SHARD_DEFS.

#include "ServerCommon/RefDataObj.h"
#include "ServerCommon/InstanceRecords.h"

#include <string.h>

#include "BSNet/NetEngine.h"

#include "LogCapture.h"
#include "TestCheck.h"

static void PutDword(CDBRecStream* pRow, DWORD dw)
{
    pRow->WriteRaw(4, &dw);
}

static void PutFloat(CDBRecStream* pRow, float f)
{
    pRow->WriteRaw(4, &f);
}

static void PutWord(CDBRecStream* pRow, WORD w)
{
    pRow->WriteRaw(2, &w);
}

static void PutByte(CDBRecStream* pRow, BYTE b)
{
    pRow->WriteRaw(1, &b);
}

// Every string field of the fake rows is 20 bytes (the skipped columns are described so).
static void PutString(CDBRecStream* pRow, const char* psz)
{
    WORD wLength = (WORD)strlen(psz);
    PutWord(pRow, wLength);
    PutWord(pRow, 20);
    pRow->WriteRaw(wLength, psz);
    for (int i = wLength + 4; i < 20; ++i)
        PutByte(pRow, 0);
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
        pRecord->AllocData(1024);
        entry.m_listRows.push_back(pRecord);
        return pRecord;
    }

    TRecord* AddRecord()
    {
        TRecord* pRecord = new TRecord;
        entry.m_listRows.push_back(pRecord);
        return pRecord;
    }
};

// Columns 3..7 of _RefObjCommon (the code names and NameStrID128) are 20-byte strings.
static void DescribeObjCommon()
{
    DBColumnVector& vec = CRefObjCommon::s_TableInfo.m_mapGroups[DBGROUP_ALL].vecColumns;
    vec.clear();
    for (int i = 3; i <= 7; ++i)
    {
        DBColumnInfo column;
        column.nOrdinal = i;
        column.nSize = 20;
        vec.push_back(column);
    }
}

static void PutObjCommon(CDBRecStream* pRow, int nID, const char* pszCode, const char* pszOrg,
                         const BYTE* abType, BYTE btCountry, const char* pszFile1, int nLink)
{
    PutDword(pRow, 1);
    PutDword(pRow, nID);
    PutString(pRow, pszCode);
    PutString(pRow, "name");
    PutString(pRow, pszOrg);
    PutString(pRow, "SN_X");                    // NameStrID128 (skipped)
    PutString(pRow, "xxx");                     // DescStrID128
    for (int i = 0; i < 6; ++i)
        PutByte(pRow, abType[i]);
    PutDword(pRow, 2500);                       // DecayTime
    PutByte(pRow, btCountry);
    PutByte(pRow, 0);                           // Rarity
    // CanTrade 0, CanSell 1, CanBuy 1, CanBorrow 0xC0, CanDrop 1 (dropped: no trade), CanPick 1,
    // CanRepair 0, CanRevive 0, CanUse 1, CanThrow 0
    const BYTE abCan[10] = { 0, 1, 1, 0xC0, 1, 1, 0, 0, 1, 0 };
    for (int i = 0; i < 10; ++i)
        PutByte(pRow, abCan[i]);
    for (int i = 0; i < 6; ++i)
        PutDword(pRow, 100 + i);
    for (int i = 0; i < 4; ++i)
    {
        PutDword(pRow, i == 0 ? 1 : (DWORD)-1);
        PutByte(pRow, (BYTE)(10 + i));
    }
    PutDword(pRow, 0);                          // MaxContain
    PutWord(pRow, 0);                           // RegionID
    for (int i = 0; i < 6; ++i)
        PutWord(pRow, 0);
    PutDword(pRow, 0);
    PutWord(pRow, 0);
    PutWord(pRow, 0);
    PutDword(pRow, 0);
    PutString(pRow, "obj");
    PutString(pRow, "drop");
    PutString(pRow, "icon");
    PutString(pRow, pszFile1);
    PutString(pRow, "xxx");
    PutDword(pRow, nLink);
}

static void PutCharRow(CDBRecStream* pRow, int nID, BYTE btLevel, BYTE btGender)
{
    PutDword(pRow, nID);
    PutByte(pRow, btLevel);
    PutByte(pRow, btGender);
    PutDword(pRow, 500);                        // MaxHP
    PutDword(pRow, 50);
    for (int i = 0; i < 32; ++i)
        PutDword(pRow, 0);
    PutByte(pRow, 0);                           // InventorySize
    for (int i = 0; i < 4; ++i)
        PutByte(pRow, 1);                       // CanStore
    PutByte(pRow, 1);                           // CanBeVehicle
    PutByte(pRow, 0);
    PutByte(pRow, 0);
    PutWord(pRow, 0);
    for (int i = 0; i < 11; ++i)
        PutDword(pRow, i);
    PutByte(pRow, 1);                           // Knockdown
    PutDword(pRow, 0);                          // KO_RecoverTime -> 3000
    for (int i = 0; i < 10; ++i)
        PutDword(pRow, i == 2 ? 777 : 0);
    for (int i = 0; i < 10; ++i)
        PutDword(pRow, i);
    PutDword(pRow, 9);
}

static void PutItemRow(CDBRecStream* pRow, int nID)
{
    PutDword(pRow, nID);
    PutDword(pRow, 1);                          // MaxStack
    PutByte(pRow, 2);                           // ReqGender
    PutDword(pRow, 0);
    PutDword(pRow, 0);
    PutByte(pRow, 4);                           // ItemClass
    PutDword(pRow, 0);
    // Dur, PD (+Inc), ER (+Inc), PAR (+Inc), BR, MD (+Inc), MAR (+Inc), PDStr, MDInt
    const int anCount1[9] = { 2, 3, 3, 3, 2, 3, 3, 2, 2 };
    for (int i = 0; i < 9; ++i)
        for (int j = 0; j < anCount1[i]; ++j)
            PutFloat(pRow, i == 0 ? (j == 0 ? 10.0f : 50.0f) : (i == 7 ? 500.0f : 1.0f));
    PutByte(pRow, 0);
    for (int i = 0; i < 5; ++i)
        PutByte(pRow, 0);
    PutByte(pRow, 0);
    PutByte(pRow, 0);
    PutWord(pRow, 0);
    const int anCount2[10] = { 2, 3, 2, 3, 2, 2, 2, 2, 3, 2 };
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < anCount2[i]; ++j)
            PutFloat(pRow, 2.0f);
    for (int i = 0; i < 20; ++i)
    {
        PutDword(pRow, (DWORD)-1);
        PutString(pRow, "xxx");
    }
    PutByte(pRow, 3);                           // MaxMagicOptCount
    PutByte(pRow, 0);
    PutDword(pRow, 0);
}

static void ApplySpecialMonster(RefObjChar* pChar)
{
    pChar->m_dw188 = 0;
    pChar->m_dw18C = 0;
}

static void TestTypeID()
{
    CHECK(RefTypeID_Pack(0, 1, 1, 2, 3, 5) == (2 | 1 << 2 | 2 << 5 | 3 << 7 | 5 << 11));
    CHECK(RefTypeID_Pack(1, 0, 3, 1, 6, 2) == (1 | 3 << 2 | 1 << 5 | 6 << 7 | 2 << 11));
    WORD w = RefTypeID_Pack(0, 1, 1, 2, 3, 5);
    CHECK(RefObj_IsGuildMercenary(w) && !RefObj_IsSiegeGuard_TID4_1(w));
    w = RefTypeID_Pack(0, 0, 3, 1, 12, 0);
    CHECK(RefObj_IsAccessoryType(w));
    w = RefTypeID_Pack(0, 0, 3, 1, 13, 0);
    CHECK(RefObjTID_IsItemEtcType13(w) && !RefObj_IsAccessoryType(w));
}

static void TestObjCommon(CReferenceData& data, RefTableVector& tables)
{
    DescribeObjCommon();
    FakeTable<CRefObjCommon> common;
    FakeTable<CRefObjItem> items;
    FakeTable<CRefObjChar> chars;
    FakeTable<CRefObjStruct> structs;
    tables[0xC] = &items.entry;
    tables[0xD] = &chars.entry;
    tables[0xE] = &structs.entry;

    const BYTE abMob[6] = { 0, 1, 1, 2, 1, 0 };
    const BYTE abItem[6] = { 0, 0, 3, 1, 6, 2 };
    const BYTE abGate[6] = { 0, 0, 4, 1, 0, 0 };
    const BYTE abMerc[6] = { 0, 1, 1, 2, 3, 5 };
    const BYTE abPlayer[6] = { 0, 1, 1, 1, 0, 0 };
    PutObjCommon(common.AddRow(), 10, "MOB_A", "xxx", abMob, 0, "xxx", 100);
    PutObjCommon(common.AddRow(), 20, "ITEM_A", "xxx", abItem, 3, "a GP:42", 200);
    PutObjCommon(common.AddRow(), 30, "STORE_CH_GATE", "xxx", abGate, 0, "xxx", 300);
    PutObjCommon(common.AddRow(), 40, "MERC_1", "MOB_A", abMerc, 0, "xxx", 400);
    PutObjCommon(common.AddRow(), 41, "MERC_2", "MOB_A", abMerc, 0, "xxx", 401);
    PutObjCommon(common.AddRow(), 50, "CHAR_A", "xxx", abPlayer, 0, "xxx", 500);
    CDBRecStream* pRow = common.AddRow();       // out of service: skipped
    PutDword(pRow, 0);
    PutCharRow(chars.AddRow(), 100, 5, 0);
    PutCharRow(chars.AddRow(), 400, 2, 0);
    PutCharRow(chars.AddRow(), 401, 2, 0);
    PutCharRow(chars.AddRow(), 500, 1, 1);
    PutItemRow(items.AddRow(), 200);
    pRow = structs.AddRow();
    PutDword(pRow, 300);
    PutDword(pRow, 0);

    g_pfnApplySpecialMonsterCode = ApplySpecialMonster;
    CNetEngine engine;
    IBSNet* pOld = g_pBSNet;
    g_pBSNet = &engine;
    LogCapture_Start();
    CHECK(data.BuildFromTable(&common.entry, 0xB) == 1);
    std::vector<CAPTURED_LOG> logs = LogCapture_Drain();
    LogCapture_Stop();
    g_pBSNet = pOld;

    CHECK(data.m_hashRefObj.size() == 8);       // 6 rows + DUMMY_OBJECT + EVENT_DAEMON
    RefObjChar* pMob = static_cast<RefObjChar*>(data.FindRefObj(10));
    CHECK(pMob != NULL && pMob->Lvl == 5 && pMob->MaxHP == 500 && pMob->KO_RecoverTime == 3000);
    CHECK(pMob->HasDefaultSkill && pMob->DefaultSkill[2] == 777 && pMob->m_n3FC == 9);
    CHECK(pMob->Flags == (REFOBJ_CAN_SELL | REFOBJ_CAN_BORROW_80 | REFOBJ_CAN_BORROW_40
                          | REFOBJ_CAN_PICK | REFOBJ_CAN_USE | REFOBJ_CAN_BE_VEHICLE));
    CHECK(pMob->DecayTime == 2.5f && pMob->OrgObjCodeName128.empty()
          && pMob->DescStrID128.empty());
    CHECK(pMob->ReqLevel[0].Type == 1 && pMob->ReqLevel[3].Level == 13 && pMob->Price == 100
          && pMob->CostBorrow == 103 && pMob->CostRepair == 101);
    CHECK(RefObj_GetTypeID(pMob) == RefTypeID_Pack(0, 1, 1, 2, 1, 0));
    CHECK(RefObj_GetByte198(pMob) == 5);

    RefObjItem* pItem = static_cast<RefObjItem*>(data.FindRefObj(20));
    CHECK(pItem != NULL && pItem->GP == 42 && pItem->ItemClass == 4 && pItem->ReqGender == 2);
    CHECK(pItem->Dur.L == 10.0f && pItem->Dur.U == 50.0f && pItem->Dur.Inc == 0.0f);
    CHECK(pItem->PDStr.L == 0.5f && pItem->MaxMagicOptCount == 3);
    CHECK(RefObjItem_IsValidDurabilityOrStack(pItem, 30) && !RefObjItem_IsValidDurability(pItem, 51));
    CHECK(RefObj_IsValidSlotForItem(pItem, 11) && !RefObj_IsValidSlotForItem(pItem, 12));
    CHECK(RefObj_IsEquipOfCountry(pItem, 0) && !RefObj_IsEquipOfCountry(pItem, 3));  // Rarity

    CHECK(data.FindRefObjByCodeName("STORE_CH_GATE") == data.FindRefObj(30));
    RefObjCommon* pDummy = data.FindRefObj(-1);
    RefObjCommon* pDaemon = data.FindRefObj(-2);
    CHECK(pDummy && pDummy->CodeName128 == "DUMMY_OBJECT" && pDummy->TypeID == 84
          && pDummy->Country == 3 && pDummy->Link == 0);
    CHECK(pDaemon && data.FindRefObjByCodeName("EVENT_DAEMON") == pDaemon
          && pDaemon->TypeID == 116);

    // Two mercenaries of one group on the same level: the second is rejected.
    CHECK(data.FindRefObj(40)->OrgObjID == 10); // MOB_A: same TID1 / TID2
    CReferenceData::RefObjByLevelHashMap::iterator it = data.m_hashMercenary.find("MOB_A");
    CHECK(it != data.m_hashMercenary.end() && it->second->size() == 140);
    CHECK(it->second->at(1) == data.FindRefObj(40) || it->second->at(1) == data.FindRefObj(41));
    BOOL bLogged = FALSE;
    for (size_t i = 0; i < logs.size(); ++i)
        if (strstr(logs[i].strText.c_str(), "Initialize Build Mercenary Failed!!"))
            bLogged = TRUE;
    CHECK(bLogged);
    CHECK(data.m_vecMonsterByLevel.size() == 140 && data.m_vecMonsterByLevel[4].size() == 1);

    // CRefCharGen: the player and the item of all races and genders.
    FakeTable<CRefCharGenData> gen;
    pRow = gen.AddRow();
    PutDword(pRow, 1);
    PutDword(pRow, 50);
    pRow = gen.AddRow();
    PutDword(pRow, 1);
    PutDword(pRow, 20);
    CHECK(data.BuildFromTable(&gen.entry, 0x37) == 1);
    CRefCharGen& cg = data.m_RefCharGen;
    CHECK(cg.m_vecRace[0].vecGender[1].vecCharObjID.size() == 1);
    CHECK(cg.m_vecRace[2].vecGender[0].vecItemObjID.size() == 1);
    const int anItem[4] = { 20, 0, 0, 0 };
    CHECK(CRefCharGen_ValidateNewCharacter(cg, 50, anItem));
    const int anBad[4] = { 0, 99, 0, 0 };
    CHECK(!CRefCharGen_ValidateNewCharacter(cg, 50, anBad));

    tables[0xC] = tables[0xD] = tables[0xE] = NULL;
    for (CReferenceData::RefObjHashMap::iterator itObj = data.m_hashRefObj.begin();
         itObj != data.m_hashRefObj.end(); ++itObj)
        delete itObj->second;
    data.m_hashRefObj.clear();
    data.m_hashRefObjByCodeName.clear();
}

static void TestSkills(CReferenceData& data)
{
    FakeTable<CRefSkillMastery> mastery;
    CRefSkillMastery* pMastery = mastery.AddRecord();
    pMastery->ID = 257;
    pMastery->Code = "MASTERY_A";
    pMastery = mastery.AddRecord();             // ID 0: dropped
    pMastery->ID = 0;
    CHECK(data.BuildFromTable(&mastery.entry, 4) == 1);
    CHECK(data.m_mapSkillMastery.size() == 1 && data.FindSkillMastery(257)->Code == "MASTERY_A");

    FakeTable<CRefSkill> skill;
    CRefSkill* pSkill = skill.AddRecord();
    pSkill->m_Data.Service = 1;
    pSkill->m_Data.ID = 7;
    pSkill->m_Data.Basic_Code = "SKILL_A";
    pSkill = skill.AddRecord();                 // out of service
    pSkill->m_Data.Service = 0;
    pSkill->m_Data.ID = 8;
    CHECK(data.BuildFromTable(&skill.entry, 5) == 1);
    CHECK(data.m_mapSkill.size() == 1 && data.FindSkill(7)->Basic_Code == "SKILL_A");
    CHECK(data.m_mapSkillByCodeName.size() == 1 && data.m_mapSkillByCodeName["SKILL_A"]->ID == 7);
}

int main()
{
    CReferenceData* pData = CReferenceData::GetInstance();
    RefTableVector tables(REF_TABLE_SLOT_COUNT, (RefTableEntry*)NULL);
    pData->m_pTables = &tables;

    TestTypeID();
    TestObjCommon(*pData, tables);
    TestSkills(*pData);

    pData->m_pTables = NULL;
    return TEST_RESULT("ShardRefObjTest");
}
