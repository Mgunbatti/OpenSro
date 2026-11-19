// SR_ShardManager reference data, the smaller tables (ServerCommon\RefDataMisc.h): set item groups,
// HWAN levels, events, default quests, the collection book, the monster lists by kind, the special
// code lists, the TypeID tests and the sDataStream parameters. Compiled with SHARD_DEFS.

#include "ServerCommon/RefDataMisc.h"

#include <string.h>

#include <new>

#include "BSNet/NetEngine.h"

#include "LogCapture.h"
#include "TestCheck.h"

// ---------------------------------------------------------------------------------------------
// Fake rows and objects

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

static void PutString(CDBRecStream* pRow, const char* psz)
{
    WORD wLength = (WORD)strlen(psz);
    PutWord(pRow, wLength);
    PutWord(pRow, (WORD)(wLength + 4));
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
        pRecord->AllocData(1024);
        entry.m_listRows.push_back(pRecord);
        return pRecord;
    }
};

// An object row with the fields the functions read (code name +0x14, TypeID +0x80, Rarity +0x89,
// flags +0x188 / +0x18C, level +0x198).
struct FakeObj
{
    BYTE ab[0x260];

    FakeObj(const char* pszCodeName, WORD wTypeID, BYTE btLevel, BYTE btRarity)
    {
        memset(ab, 0, sizeof(ab));
        new (ab + 0x14) std::string(pszCodeName);
        *(WORD*)(ab + 0x80) = wTypeID;
        ab[0x89] = btRarity;
        ab[0x198] = btLevel;
    }

    ~FakeObj()
    {
        ((std::string*)(ab + 0x14))->~basic_string();
    }

    RefObjCommonRow* Row()
    {
        return (RefObjCommonRow*)ab;
    }
};

// TypeID of a bionic object 1/2/TID3/TID4 and of an item 3/TID2/TID3/TID4.
static WORD CharTID(int nTID3, int nTID4)
{
    return (WORD)(2 | (1 << 2) | (2 << 5) | (nTID3 << 7) | (nTID4 << 11));
}

static WORD ItemTID(int nTID2, int nTID3, int nTID4)
{
    return (WORD)((3 << 2) | (nTID2 << 5) | (nTID3 << 7) | (nTID4 << 11));
}

static BOOL HasLog(const std::vector<CAPTURED_LOG>& logs, const char* pszText)
{
    for (size_t i = 0; i < logs.size(); ++i)
    {
        if (logs[i].strText == pszText)
            return TRUE;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------------------------

static void TestEventAndDefaultQuest(CReferenceData& data)
{
    FakeTable<CRefEvent> event;
    CDBRecStream* pRow = event.AddRow();
    PutByte(pRow, 1);
    PutDword(pRow, 3);
    PutString(pRow, "EVENT_Xmas");
    PutString(pRow, "desc");
    pRow = event.AddRow();                      // out of service
    PutByte(pRow, 0);
    pRow = event.AddRow();                      // name of 256 characters: dropped
    PutByte(pRow, 1);
    PutDword(pRow, 4);
    PutString(pRow, std::string(256, 'A').c_str());
    PutString(pRow, "");
    CHECK(data.BuildFromTable(&event.entry, 0x54) == 1);
    CHECK(data.m_mapEvent.size() == 1 && data.m_mapEvent[3]->Name == "event_xmas" &&
          data.m_mapEvent[3]->str20 == "desc");

    pRow = event.AddRow();                      // the same ID again fails the build
    PutByte(pRow, 1);
    PutDword(pRow, 3);
    PutString(pRow, "OTHER");
    PutString(pRow, "");
    CHECK(data.BuildFromTable(&event.entry, 0x54) == 0);

    FakeTable<CRefDefaultQuest> quest;
    pRow = quest.AddRow();
    PutByte(pRow, 1);
    PutDword(pRow, 9);
    PutByte(pRow, 2);
    PutString(pRow, "QNO_Default");
    PutByte(pRow, 5);
    CHECK(data.BuildFromTable(&quest.entry, 0x58) == 1);
    CHECK(data.m_mapDefaultQuest.size() == 1);
    RefDefaultQuestRow* pQuest = data.m_mapDefaultQuest[9];
    CHECK(pQuest->b04 == 2 && pQuest->Name == "QNO_Default" && pQuest->b05 == 5);

    CRefEvent slot36;
    slot36.AllocData(64);
    PutDword(&slot36, 77);
    PutString(&slot36, "GROUP");
    CRefRow row(&slot36);
    RefShopSlot36Row shop;
    RefRow_Read(row, shop);
    CHECK(shop.dw00 == 77 && shop.str04 == "GROUP");
}

static void PutSetItemGroup(CDBRecStream* pRow, BYTE btService, DWORD dwID, DWORD dwMask,
                            const DWORD* pdwGroup)
{
    PutByte(pRow, btService);
    PutDword(pRow, dwID);
    for (int i = 0; i < 4; ++i)
        PutString(pRow, "s");
    PutByte(pRow, 0xEE);
    PutDword(pRow, dwMask);
    for (int i = 0; i < 10; ++i)
        PutDword(pRow, pdwGroup[i]);
}

static void TestSetItemGroupAndHWAN(CReferenceData& data)
{
    data.m_mapMagicOptGroup[10].push_back(NULL);
    data.m_mapMagicOptGroup[11].push_back(NULL);
    DWORD adwGroup[10] = { 10, 11, 0, 0, 0, 0, 0, 0, 0, 10 };
    FakeTable<CRefSetItemGroup> group;
    PutSetItemGroup(group.AddRow(), 1, 5, 0x3, adwGroup);
    PutSetItemGroup(group.AddRow(), 0, 6, 0x3, adwGroup);
    CHECK(data.BuildFromTable(&group.entry, 0xA) == 1);
    CHECK(data.m_mapSetItemGroup.size() == 1);
    RefSetItemGroup* pGroup = data.m_mapSetItemGroup[5];
    CHECK(pGroup->ID == 5 && pGroup->SetMagicMask == 3 && pGroup->mapMagicOptGroup.size() == 3);
    CHECK(pGroup->mapMagicOptGroup[1] == 10 && pGroup->mapMagicOptGroup[2] == 11 &&
          pGroup->mapMagicOptGroup[0x200] == 10);
    data.m_mapMagicOptGroup.clear();

    FakeTable<CRefHWANLevel> hwan;
    CDBRecStream* pRow = hwan.AddRow();
    PutByte(pRow, 0);                           // the Service column is not looked at
    for (int i = 0; i < 5; ++i)
    {
        PutDword(pRow, i == 2 ? 0 : 100 + i);
        PutByte(pRow, (BYTE)i);
    }
    CHECK(data.BuildFromTable(&hwan.entry, 0x53) == 1);
    CHECK(data.m_vecHWANLevel.size() == 1 && data.m_vecHWANLevel[0]->vecPair.size() == 4);
    CHECK(data.m_vecHWANLevel[0]->vecPair[2].first == 103 &&
          data.m_vecHWANLevel[0]->vecPair[2].second == 3);
}

static void PutTheme(FakeTable<CRefCollectionBook_Theme>& table, DWORD dwID, const char* pszCode,
                     DWORD dwComplete)
{
    CDBRecStream* pRow = table.AddRow();
    PutByte(pRow, 1);
    PutDword(pRow, dwID);
    PutString(pRow, pszCode);
    PutString(pRow, "a");
    PutString(pRow, "b");
    PutString(pRow, "c");
    PutDword(pRow, dwComplete);
}

static void PutItem(FakeTable<CRefCollectionBook_Item>& table, const char* pszCode,
                    const char* pszTheme, DWORD dwSlot)
{
    CDBRecStream* pRow = table.AddRow();
    PutByte(pRow, 1);
    PutString(pRow, pszCode);
    PutString(pRow, "x");
    PutString(pRow, pszTheme);
    PutDword(pRow, dwSlot);
    PutString(pRow, "y");
    PutString(pRow, "z");
}

static void TestCollectionBook(CReferenceData& data)
{
    WORD wBook = ItemTID(3, 12, 5);
    FakeObj item1("ITEM_BOOK_1", wBook, 0, 0);
    FakeObj item2("ITEM_BOOK_2", wBook, 0, 0);
    FakeObj item3("ITEM_BOOK_3", wBook, 0, 0);
    FakeObj sword("ITEM_SWORD", ItemTID(1, 6, 2), 0, 0);
    data.m_hashRefObjByCodeName["ITEM_BOOK_1"] = item1.Row();
    data.m_hashRefObjByCodeName["ITEM_BOOK_2"] = item2.Row();
    data.m_hashRefObjByCodeName["ITEM_BOOK_3"] = item3.Row();
    data.m_hashRefObjByCodeName["ITEM_SWORD"] = sword.Row();

    FakeTable<CRefCollectionBook_Theme> theme;
    PutTheme(theme, 1, "THEME_A", 2);
    PutTheme(theme, 2, "THEME_B", 3);
    PutTheme(theme, 1, "THEME_C", 1);           // ID of THEME_A again
    FakeTable<CRefCollectionBook_Item> item;
    PutItem(item, "ITEM_BOOK_1", "THEME_A", 1);
    PutItem(item, "ITEM_BOOK_2", "THEME_A", 2);
    PutItem(item, "ITEM_BOOK_3", "THEME_B", 4); // above CompleteNum
    PutItem(item, "ITEM_SWORD", "THEME_B", 1);  // no collection book item

    CNetEngine engine;
    IBSNet* pOld = g_pBSNet;
    g_pBSNet = &engine;
    LogCapture_Start();
    CHECK(data.BuildFromTable(&theme.entry, 0x68) == 1);
    CHECK(data.BuildFromTable(&item.entry, 0x69) == 0);
    std::vector<CAPTURED_LOG> logs = LogCapture_Drain();

    CHECK(data.m_mapCollectionBookTheme.size() == 3 && data.m_mapCollectionBookItem.size() == 4);
    RefCollectionBookTheme* pThemeA = data.m_mapCollectionBookTheme["THEME_A"];
    CHECK(pThemeA->CompleteNum == 2 && pThemeA->vecItem.size() == 3);
    CHECK(pThemeA->vecItem[0] == NULL &&
          pThemeA->vecItem[1] == data.m_mapCollectionBookItem["ITEM_BOOK_1"] &&
          data.m_mapCollectionBookItem["ITEM_BOOK_2"]->ThemeID == 1);
    CHECK(data.m_mapCollectionBookThemeByID.size() == 2 &&
          data.m_mapCollectionBookThemeByID[1] == pThemeA);
    CHECK(HasLog(logs, "ITEM_SWORD is not 'CollectionBookItem'"));
    CHECK(HasLog(logs, "In 'CollectionBook_Item' ITEM_BOOK_3. 'SlotIndex(4)' is over "
                       "THEME_B->CompleteNum(3)"));
    CHECK(HasLog(logs, "Theme 'THEME_C' already has this ID : 1"));
    CHECK(!HasLog(logs, "Theme 'THEME_A' has empty index : 1"));

    // a theme code name that is already there fails the build
    FakeTable<CRefCollectionBook_Theme> again;
    PutTheme(again, 7, "THEME_A", 1);
    CHECK(data.BuildFromTable(&again.entry, 0x68) == 0);
    logs = LogCapture_Drain();
    CHECK(HasLog(logs, "Already exists '_RefCollectionBook_Theme' CodeName : THEME_A"));
    LogCapture_Stop();
    g_pBSNet = pOld;

    data.ClearCollectionBook();
    data.m_hashRefObjByCodeName.clear();
}

static void TestMonsterLists(CReferenceData& data)
{
    FakeObj thief("MOB_THIEF_A", CharTID(1, 2), 10, 0);
    FakeObj thiefEU("mob_eu_thief_b", CharTID(1, 2), 10, 0);
    FakeObj thiefOther("MOB_OTHER", CharTID(1, 2), 140, 0);
    FakeObj hunter("MOB_HUNTER_A", CharTID(1, 3), 1, 0);
    FakeObj trap("MOB_TQ_BLACKSNAKE", CharTID(1, 1), 50, 0);
    FakeObj trapUnique("MOB_TQ_WHITESNAKE", CharTID(1, 1), 50, 3);
    FakeObj snake("MOB_TQ_SNAKETRAP_1", CharTID(1, 1), 50, 0);
    FakeObj npc("NPC_TQ_X", CharTID(2, 0), 50, 0);
    FakeObj* apObj[] = { &thief, &thiefEU, &thiefOther, &hunter, &trap, &trapUnique, &snake,
                         &npc };
    for (int i = 0; i < 8; ++i)
        data.m_hashRefObj[100 + i] = apObj[i]->Row();

    RefData_BuildHunterMonsterList(&data);
    RefData_BuildThiefMonsterList(&data);
    RefData_BuildTrapMonsterList(&data, "TQ");
    CHECK(data.m_vecThiefMonster.size() == 3 && data.m_vecThiefMonster[1].size() == 140);
    CHECK(data.m_vecThiefMonster[0][9].size() == 1 &&
          data.m_vecThiefMonster[0][9][0] == thief.Row());
    CHECK(data.m_vecThiefMonster[1][9].size() == 1 &&
          data.m_vecThiefMonster[2][139].size() == 1);
    CHECK(data.m_vecHunterMonster[0][0].size() == 1 &&
          data.m_vecHunterMonster[0][0][0] == hunter.Row());
    CHECK(data.m_mapTrapMonster.size() == 1 && data.m_mapTrapMonster["TQ"].size() == 1 &&
          data.m_mapTrapMonster["TQ"][0] == trap.Row());
    data.m_hashRefObj.clear();
    data.m_vecThiefMonster.clear();
    data.m_vecHunterMonster.clear();
    data.m_mapTrapMonster.clear();
}

static void TestSpecialCodeLists(CReferenceData& data)
{
    CSpecialCodeList* pItems = CSpecialCodeList::CreateItemList();
    CSpecialCodeList* pMonsters = CSpecialCodeList::CreateMonsterList();
    CHECK(pItems == CSpecialCodeList::GetItemList() && pItems->m_vecEntry.size() == 2);
    CHECK(pMonsters == CSpecialCodeList::GetMonsterList() && pMonsters->m_vecEntry.size() == 1);

    FakeObj ticket("ITEM_ETC_E070919_TICKET", ItemTID(3, 1, 1), 0, 0);
    FakeObj ticket2("ITEM_ETC_E070919_TICKET_2", ItemTID(3, 1, 1), 0, 0);
    RefObj_SpecialFlag2(ticket.Row()) = 9;
    pItems->ApplyToItem(ticket.Row());
    pItems->ApplyToItem(ticket2.Row());
    CHECK(RefObj_SpecialFlag1(ticket.Row()) == 2 && RefObj_SpecialFlag2(ticket.Row()) == 0);
    CHECK(RefObj_SpecialFlag1(ticket2.Row()) == 0);

    FakeObj mob("MOB_TQ_SKELETON", CharTID(1, 1), 1, 0);
    FakeObj other("MOB_CH_TIGER", CharTID(1, 1), 1, 0);
    pMonsters->ApplyToChar(mob.Row());
    pMonsters->ApplyToChar(other.Row());
    CHECK(RefObj_SpecialFlag1(mob.Row()) == 0 && RefObj_SpecialFlag2(mob.Row()) == 1);
    CHECK(RefObj_SpecialFlag2(other.Row()) == 0);

    CSpecialCodeList::DestroyItemList();
    CSpecialCodeList::DestroyMonsterList();
    CHECK(CSpecialCodeList::GetItemList() == NULL && pItems->m_vecEntry.empty());

    // the rent items by code name
    RefRentItem* pRent = new RefRentItem;
    data.m_hashRentItem["ITEM_RENT"] = pRent;
    CHECK(RefData_FindRentItem(&data, "ITEM_RENT") == pRent);
    CHECK(RefData_FindRentItem(&data, "ITEM_NONE") == NULL);
    data.m_hashRentItem.clear();
    delete pRent;

    RefObjItem_GetSpecialItemIDSet().insert(5);
    CHECK(RefObjItem_GetSpecialItemIDSet().count(5) == 1);
}

static void TestTypeIDs()
{
    WORD w = CharTID(4, 1);
    CHECK(RefTID_Is_1_2_4(w) && !RefTID_Is_1_2_5(w));
    w = CharTID(5, 0);
    CHECK(RefTID_Is_1_2_5(w) && !RefTID_Is_1_2_4(w));
    w = ItemTID(1, 13, 4);
    CHECK(RefTID_Is_3_1_13_4(w) && !RefTID_Is_3_3_11_3(w));
    w = ItemTID(3, 11, 3);
    CHECK(RefTID_Is_3_3_11_3(w) && !RefTID_Is_3_3_11_7(w));
    w = ItemTID(3, 11, 7);
    CHECK(RefTID_Is_3_3_11_7(w));
}

static void TestDataStreams()
{
    std::string strOut;
    CHECK(sDataStream_ExtractToken("  abc, d]", 0, NULL, ",", strOut) == 6 && strOut == "abc");
    CHECK(sDataStream_ExtractToken("abc", 0, NULL, ",", strOut) == -1);
    CHECK(sDataStream_ExtractToken("abc", 4, NULL, ",", strOut) == -1);

    std::string strData = "[BUFF,P,30]";
    int nPos = 1;
    BUFF_ITEM buff;
    CHECK(buff.Parse(strData, &nPos) && nPos == (int)strData.size());
    CHECK(buff.m_strName == "BUFF" && buff.m_nTarget == sDataStream::TARGET_P &&
          buff.m_nValue == 30);

    strData = "[STATUS,A,1,2,3,4,5,6]";
    nPos = 1;
    BUF_CHARACTER_STATUS status;
    CHECK(status.Parse(strData, &nPos) && status.m_nTarget == sDataStream::TARGET_A);
    CHECK(status.m_nValue == 1 && status.m_vecStatus.size() == 5 && status.m_vecStatus[4] == 6);

    strData = "[LIMIT,A,7,8]";
    nPos = 1;
    UQL limit;
    CHECK(limit.Parse(strData, &nPos) && limit.m_nValue1 == 7 && limit.m_nValue2 == 8);

    strData = "[PSU, 12]";
    nPos = 1;
    PSU psu;
    CHECK(psu.Parse(strData, &nPos) && psu.m_strName == "PSU" && psu.m_nValue == 12 &&
          psu.m_nTarget == sDataStream::TARGET_P);

    strData = "[UNLIMIT]";
    nPos = 1;
    UIU unlimit;
    CHECK(unlimit.Parse(strData, &nPos) && unlimit.m_strName == "UNLIMIT");
    CHECK(!unlimit.Parse(strData, &nPos) && nPos == -1);
}

static void PutGachaRow(CDBRecStream* pRow, BYTE btService, DWORD dwItem, WORD wCount,
                        const char* pszDesc)
{
    PutByte(pRow, btService);
    PutDword(pRow, 1);
    PutDword(pRow, dwItem);
    PutWord(pRow, 0);
    PutWord(pRow, wCount);
    PutDword(pRow, 0);
    PutByte(pRow, 0);
    for (int i = 0; i < 4; ++i)
    {
        PutDword(pRow, i == 0 ? 1 : 0);
        PutString(pRow, i == 0 ? pszDesc : "xxx");
    }
}

static void TestGacha(CReferenceData& data)
{
    CHECK(Util_PackFourCC("intv") == 0x696E7476 && Util_PackFourCC("ab") == 0x6162);

    CGachaFreeParam param;
    param.Parse("<D:5>");                       // no [BIIV]: nothing
    CHECK(param.m_vecEntry.empty());
    param.Parse("[BIIV]<D:150><M:intv,2,-7><O:3>");
    CHECK(param.m_vecEntry.size() == 3);
    CHECK(param.m_vecEntry[0]->btType == 'D' && param.m_vecEntry[0]->nValue == 150);
    CHECK(param.m_vecEntry[1]->btType == 'M' && param.m_vecEntry[1]->dwCode == 0x696E7476 &&
          param.m_vecEntry[1]->btLevel == 2 && param.m_vecEntry[1]->nValue == -7);
    CHECK(param.m_vecEntry[2]->btType == 'O' && param.m_vecEntry[2]->nValue == 3);

    FakeObj item("ITEM_ETC_GACHA", ItemTID(3, 1, 1), 0, 0);
    *(DWORD*)(item.ab + 0x198) = 10;
    *(float*)(item.ab + 0x1B0) = 10.0f;
    *(float*)(item.ab + 0x1B4) = 100.0f;
    data.m_hashRefObj[500] = item.Row();
    FakeTable<CRefGachaItemSet> table;
    PutGachaRow(table.AddRow(), 1, 500, 5, "[BIIV]<D:150><O:20>");
    PutGachaRow(table.AddRow(), 0, 500, 5, "");
    CHECK(RefData_BuildGachaItemSet(&data, &table.entry, 0x47) == 1);
    CHECK(data.m_vecGachaItemSet.size() == 1);
    RefGachaItemSetRow* pRow = data.m_vecGachaItemSet[0];
    CHECK(pRow->RefItemID == 500 && pRow->Count == 5 && pRow->Param[0].Param == 1);
    CHECK(pRow->pFreeParam && pRow->pFreeParam->m_vecEntry.size() == 2);
    CHECK(pRow->pFreeParam->m_vecEntry[0]->nValue == 100);  // durability clamped
    CHECK(pRow->pFreeParam->m_vecEntry[1]->nValue == 12);   // option level clamped
    delete pRow;
    data.m_vecGachaItemSet.clear();
    data.m_hashRefObj.clear();
}

int main()
{
    CReferenceData* pData = CReferenceData::GetInstance();
    RefTableVector tables(REF_TABLE_SLOT_COUNT, (RefTableEntry*)NULL);
    pData->m_pTables = &tables;

    TestEventAndDefaultQuest(*pData);
    TestSetItemGroupAndHWAN(*pData);
    TestCollectionBook(*pData);
    TestMonsterLists(*pData);
    TestSpecialCodeLists(*pData);
    TestTypeIDs();
    TestDataStreams();
    TestGacha(*pData);

    pData->m_pTables = NULL;
    return TEST_RESULT("ShardRefMiscTest");
}
