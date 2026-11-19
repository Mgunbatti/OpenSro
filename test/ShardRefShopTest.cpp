// SR_ShardManager reference data, shop tables (SilkroadOnline\Server\ServerCommon\RefDataShop.h):
// the builder of slot 0x27 over one small set of fake rows per table, the lookups by country and
// code name, the second silk price, the trade goods and the empty-table failure. Compiled with
// SHARD_DEFS.

#include "ServerCommon/ReferenceData.h"
#include "ServerCommon/RefDataObj.h"

#include <string.h>

#include "TestCheck.h"

// ---------------------------------------------------------------------------------------------
// Fake rows: the columns in the stream format CDBRecStream::SetField writes.

static void PutByte(CDBRecStream* pRow, BYTE b)
{
    pRow->WriteRaw(1, &b);
}

static void PutWord(CDBRecStream* pRow, WORD w)
{
    pRow->WriteRaw(2, &w);
}

static void PutDword(CDBRecStream* pRow, DWORD dw)
{
    pRow->WriteRaw(4, &dw);
}

static void PutQword(CDBRecStream* pRow, unsigned __int64 n)
{
    pRow->WriteRaw(8, &n);
}

// WORD length, WORD field size (header included), the characters.
static void PutString(CDBRecStream* pRow, const char* psz)
{
    WORD wLength = (WORD)strlen(psz);
    PutWord(pRow, wLength);
    PutWord(pRow, (WORD)(wLength + 4));
    pRow->WriteRaw(wLength, psz);
}

static void PutParams(CDBRecStream* pRow)
{
    for (int i = 0; i < 4; ++i)
    {
        PutDword(pRow, i);
        PutString(pRow, "desc");
    }
}

// Cash, TID1..TID4, item code name, then the two condition columns and the parameters.
static void PutItemCondition(CDBRecStream* pRow, const BYTE* pabTID, const char* pszItem,
                             BYTE b00, DWORD dw04)
{
    if (pabTID)
    {
        for (int i = 0; i < 5; ++i)
            PutByte(pRow, pabTID[i]);
        PutString(pRow, pszItem);
    }
    PutByte(pRow, b00);
    PutDword(pRow, dw04);
    PutParams(pRow);
}

static void PutTimestamp(CDBRecStream* pRow, SQLSMALLINT nYear)
{
    TIMESTAMP_STRUCT ts;
    memset(&ts, 0, sizeof(ts));
    ts.year = nYear;
    pRow->WriteRaw(sizeof(ts), &ts);
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

    // A row out of service.
    void AddDropped()
    {
        PutByte(AddRow(), 0);
    }
};

// Object rows (RefObjItem): ID, TypeID, the available operations (Flags), and the durability /
// stack bounds the scrap item rows are checked against.
static BYTE s_abItem[sizeof(RefObjItem)];
static BYTE s_abGood[sizeof(RefObjItem)];

static RefObjCommonRow* AddObject(CReferenceData& data, BYTE* pab, const char* pszCodeName,
                                  DWORD dwID, WORD wTypeID)
{
    RefObjItem* pObj = (RefObjItem*)pab;
    pObj->ID = dwID;
    pObj->TypeID = wTypeID;
    pObj->Flags = REFOBJ_CAN_SELL;
    pObj->MaxStack = 10;
    pObj->Dur.L = 1.0f;
    pObj->Dur.U = 10.0f;
    data.m_hashRefObj[dwID] = pObj;
    data.m_hashRefObjByCodeName[pszCodeName] = pObj;
    return pObj;
}

static void PutPackageItem(CDBRecStream* pRow, DWORD dwID, const char* pszCodeName)
{
    PutByte(pRow, 1);
    PutDword(pRow, 0);
    PutDword(pRow, dwID);
    PutString(pRow, pszCodeName);
    PutWord(pRow, 1);
    PutString(pRow, "EXPAND_TERM_ALL");
    PutParams(pRow);
}

static void PutScrap(CDBRecStream* pRow, const char* pszPackage, const char* pszItem)
{
    PutByte(pRow, 1);
    PutDword(pRow, 0);
    PutString(pRow, pszPackage);
    PutString(pRow, pszItem);
    PutByte(pRow, 0);                               // OptLevel
    PutQword(pRow, 0);                              // Variance
    PutDword(pRow, 5);                              // durability / stack
    PutByte(pRow, 0);                               // no magic options
    for (int i = 0; i < 12; ++i)
        PutQword(pRow, 0);
    PutParams(pRow);
}

static void PutPrice(CDBRecStream* pRow, const char* pszPackage, DWORD dwDevice, DWORD dwCost)
{
    PutByte(pRow, 1);
    PutDword(pRow, 0);
    PutString(pRow, pszPackage);
    PutDword(pRow, dwDevice);
    PutDword(pRow, dwCost);
    PutParams(pRow);
}

static void TestShopTables(CReferenceData& data, RefTableVector& tables)
{
    // TypeID 3/1/6/2 (an item), 3/3/3/8 (a trade good)
    RefObjCommonRow* pItem = AddObject(data, s_abItem, "ITEM_A", 101, 0x132C);
    AddObject(data, s_abGood, "ITEM_GOOD", 102, 0x46C);

    FakeTable<CRefPackageItem> package;
    FakeTable<CRefScrapOfPackageItem> scrap;
    FakeTable<CRefPricePolicyOfItem> price;
    FakeTable<CRefRewardPolicyToSellPackageItem> rewardToSell;
    FakeTable<CRefConditionToSellPackageItem> conditionToSell;
    FakeTable<CRefShopTabGroup> tabGroup;
    FakeTable<CRefShopTab> tab;
    FakeTable<CRefShopGoods> goods;
    FakeTable<CRefShop> shop;
    FakeTable<CRefShopGroup> shopGroup;
    FakeTable<CRefMappingShopGroup> mappingGroup;
    FakeTable<CRefMappingShopWithTab> mappingTab;
    FakeTable<CRefAccessPermissionOfShop> access;
    FakeTable<CRefTreatItemOfShop> treat;
    FakeTable<CRefConditionToBuyScrapItem> conditionToBuyScrap;
    FakeTable<CRefConditionToSellScrapItem> conditionToSellScrap;
    FakeTable<CRefRewardPolicyToSellScrapItem> rewardToSellScrap;
    FakeTable<CRefRewardPolicyToBuyScrapItem> rewardToBuyScrap;
    FakeTable<CRefShopItemStockPeriod> stockPeriod;
    tables[0x17] = &package.entry;
    tables[0x16] = &scrap.entry;
    tables[0x18] = &price.entry;
    tables[0x1B] = &rewardToSell.entry;
    tables[0x20] = &conditionToSell.entry;
    tables[0x23] = &tabGroup.entry;
    tables[0x26] = &tab.entry;
    tables[0x25] = &goods.entry;
    tables[0x27] = &shop.entry;
    tables[0x21] = &shopGroup.entry;
    tables[0x22] = &mappingGroup.entry;
    tables[0x15] = &mappingTab.entry;
    tables[0x19] = &access.entry;
    tables[0x1A] = &treat.entry;
    tables[0x1D] = &conditionToBuyScrap.entry;
    tables[0x1F] = &conditionToSellScrap.entry;
    tables[0x1E] = &rewardToSellScrap.entry;
    tables[0x1C] = &rewardToBuyScrap.entry;
    tables[0x5B] = &stockPeriod.entry;

    // an empty _RefPackageItem fails the build before anything is read
    CHECK(data.BuildFromTable(&shop.entry, 0x27) == 0);
    CHECK(data.m_mapPackageItem.empty());
    // the other slots build nothing of their own
    CHECK(data.BuildFromTable(&tab.entry, 0x26) == 1 && data.BuildFromTable(NULL, 0x5B) == 1);

    PutPackageItem(package.AddRow(), 10, "PACKAGE_ITEM_A");
    PutPackageItem(package.AddRow(), 11, "PACKAGE_ITEM_GOOD");
    package.AddDropped();
    PutScrap(scrap.AddRow(), "PACKAGE_ITEM_A", "ITEM_A");
    PutScrap(scrap.AddRow(), "PACKAGE_ITEM_GOOD", "ITEM_GOOD");
    // gold 500, silk 100; the second silk price is refused
    PutPrice(price.AddRow(), "PACKAGE_ITEM_A", 1, 500);
    PutPrice(price.AddRow(), "PACKAGE_ITEM_A", 8, 100);
    PutPrice(price.AddRow(), "PACKAGE_ITEM_A", 8, 200);
    PutPrice(price.AddRow(), "PACKAGE_ITEM_GOOD", 1, 7);
    rewardToSell.AddDropped();
    {
        // "xxx" links to no package item: read, then dropped
        CDBRecStream* pRow = conditionToSell.AddRow();
        PutByte(pRow, 1);
        PutDword(pRow, 0);
        PutString(pRow, "xxx");
        PutItemCondition(pRow, NULL, NULL, 0, 0);
        pRow = conditionToSell.AddRow();
        PutByte(pRow, 1);
        PutDword(pRow, 0);
        PutString(pRow, "PACKAGE_ITEM_A");
        PutItemCondition(pRow, NULL, NULL, 4, 44);
    }
    {
        CDBRecStream* pRow = tabGroup.AddRow();
        PutByte(pRow, 1);
        PutDword(pRow, 0);
        PutDword(pRow, 5);
        PutString(pRow, "STORE_TABGROUP_A");
        PutString(pRow, "SN_TABGROUP_A");
    }
    {
        CDBRecStream* pRow = tab.AddRow();
        PutByte(pRow, 1);
        PutDword(pRow, 0);
        PutDword(pRow, 7);
        PutString(pRow, "STORE_TAB_A");
        PutString(pRow, "STORE_TABGROUP_A");
    }
    const char* apszGoods[2] = { "PACKAGE_ITEM_A", "PACKAGE_ITEM_GOOD" };
    for (int i = 0; i < 2; ++i)
    {
        CDBRecStream* pRow = goods.AddRow();
        PutByte(pRow, 1);
        PutDword(pRow, 0);
        PutString(pRow, "STORE_TAB_A");
        PutString(pRow, apszGoods[i]);
        PutByte(pRow, (BYTE)(3 + i));
        PutParams(pRow);
    }
    {
        CDBRecStream* pRow = shop.AddRow();
        PutByte(pRow, 1);
        PutDword(pRow, 0);
        PutDword(pRow, 20);
        PutString(pRow, "STORE_A");
        PutParams(pRow);
    }
    {
        CDBRecStream* pRow = shopGroup.AddRow();
        PutByte(pRow, 1);
        PutDword(pRow, 0);
        PutWord(pRow, 30);
        PutString(pRow, "GROUP_A");
        PutString(pRow, "xxx");
        PutParams(pRow);
    }
    {
        CDBRecStream* pRow = mappingGroup.AddRow();
        PutByte(pRow, 1);
        PutDword(pRow, 0);
        PutString(pRow, "GROUP_A");
        PutString(pRow, "STORE_A");
        pRow = mappingTab.AddRow();
        PutByte(pRow, 1);
        PutDword(pRow, 0);
        PutString(pRow, "STORE_A");
        PutString(pRow, "STORE_TABGROUP_A");
        pRow = access.AddRow();
        PutByte(pRow, 1);
        PutDword(pRow, 0);
        PutString(pRow, "STORE_A");
        PutItemCondition(pRow, NULL, NULL, 1, 2);
    }
    const BYTE abTID[5] = { 1, 3, 1, 6, 2 };
    {
        CDBRecStream* pRow = treat.AddRow();
        PutByte(pRow, 1);
        PutDword(pRow, 0);
        PutString(pRow, "STORE_A");
        PutItemCondition(pRow, abTID, "ITEM_A", 0, 0);
    }
    {
        CDBRecStream* pRow = conditionToBuyScrap.AddRow();
        PutByte(pRow, 1);
        PutDword(pRow, 0);
        PutItemCondition(pRow, abTID, "xxx", 2, 9);
    }
    conditionToSellScrap.AddDropped();
    rewardToSellScrap.AddDropped();
    rewardToBuyScrap.AddDropped();
    for (BYTE bType = 1; bType <= 2; ++bType)
    {
        CDBRecStream* pRow = stockPeriod.AddRow();
        PutByte(pRow, 1);
        PutDword(pRow, 0);
        PutDword(pRow, 40 + bType);
        PutString(pRow, "a");
        PutString(pRow, "b");
        PutTimestamp(pRow, 2011);
        PutTimestamp(pRow, 2012);
        PutByte(pRow, bType);                       // 2 drops the row
    }

    CHECK(data.BuildFromTable(&shop.entry, 0x27) == 1);

    // package items, their scrap items and prices
    CHECK(data.m_mapPackageItem.size() == 2);
    RefPackageItem* pPackage = RefData_FindPackageItem(&data, "PACKAGE_ITEM_A", 0);
    CHECK(pPackage && pPackage == data.m_mapPackageItem[10] && pPackage->ID == 10);
    CHECK(RefData_FindPackageItem(&data, "PACKAGE_ITEM_A", 1) == NULL);
    CHECK(pPackage->m_vecAssignedScrapOfPackageItems.size() == 1);
    RefScrapOfPackageItem* pScrap = pPackage->m_vecAssignedScrapOfPackageItems[0];
    CHECK(pScrap->RefItemID == 101 && pScrap->m_pRefObjItem == pItem && pScrap->m_dwData == 5);
    CHECK(!pPackage->m_bSpecialty);
    CHECK(pPackage->Price.dwMask == 9 && pPackage->Price.aCost[0] == 500 &&
          pPackage->Price.aCost[3] == 100);
    CHECK(pPackage->m_vecRewardPolicyToSell.empty() && pPackage->m_vecConditionToSell.size() == 1 &&
          pPackage->m_vecConditionToSell[0]->dw04 == 44);
    RefPackageItem* pGoodPackage = data.m_mapPackageItem[11];
    CHECK(pGoodPackage->m_bSpecialty && pGoodPackage->Price.aCost[0] == 7);

    // tab group, tab and goods (240 slots)
    RefShopTabGroup* pTabGroup = RefData_FindShopTabGroup(&data, "STORE_TABGROUP_A", 0);
    RefShopTab* pTab = RefData_FindShopTab(&data, "STORE_TAB_A", 0);
    CHECK(pTabGroup && pTab && pTab->RefTabGroupID == 5);
    CHECK(pTabGroup->m_vecRefShopTabs.size() == 1 && pTabGroup->m_vecRefShopTabs[0] == pTab);
    CHECK(pTab->m_AssignedGoods.size() == MAX_ITEM_PER_SHOP_TAB);
    CHECK(pTab->m_AssignedGoods[3] && pTab->m_AssignedGoods[3]->m_pRefPackageItem == pPackage);
    CHECK(pTab->m_AssignedGoods[4] && !pTab->m_AssignedGoods[2]);
    // the trade good of the second goods row is collected
    CHECK(data.m_set22C.size() == 1 && data.m_set22C.count(102) == 1);

    // shop, shop group, mappings, access permission and treat item
    RefShop* pShop = RefData_FindShop(&data, "STORE_A", 0);
    CHECK(pShop && pShop == data.m_mapShop[20]);
    CHECK(pShop->m_vecRefShopTabs.size() == 1 && pShop->m_vecRefShopTabs[0] == pTab);
    CHECK(pShop->m_vecAccessPermission.size() == 1 &&
          pShop->m_vecAccessPermission[0]->LinkID == 20);
    // Cash 1, TID1 3, TID2 1, TID3 6, TID4 2
    WORD wTID = 1 | (3 << 2) | (1 << 5) | (6 << 7) | (2 << 11);
    CHECK(pShop->m_mmapTreatItem.size() == 1 && pShop->m_mmapTreatItem.count(wTID) == 1);
    CHECK(pShop->m_mmapTreatItem.find(wTID)->second->Item.RefItemID == 101);
    RefShopGroup* pShopGroup = RefData_FindShopGroup(&data, "GROUP_A", 0);
    CHECK(pShopGroup && pShopGroup->ID == 30 && pShopGroup->RefNPCID == 0);
    CHECK(pShopGroup->m_vecRefShops.size() == 1 && pShopGroup->m_vecRefShops[0] == pShop);

    // scrap item rules by (b00 << 32 | dw04), stock periods
    CHECK(data.m_mapConditionToBuyScrapItem.size() == 1 &&
          data.m_mapConditionToBuyScrapItem.count(((unsigned __int64)2 << 32) | 9) == 1);
    CHECK(data.m_mapConditionToSellScrapItem.empty() &&
          data.m_mapRewardPolicyToBuyScrapItem.empty());
    CHECK(data.m_mapShopItemStockPeriod.size() == 1 && data.m_mapShopItemStockPeriod.count(41));
    CHECK(data.m_mapShopSlot36.empty());

    for (int nSlot = 0x15; nSlot <= 0x27; ++nSlot)
        tables[nSlot] = NULL;
    tables[0x5B] = NULL;
}

static void TestCurrencyTable()
{
    const int* pTable = RefPrice_GetCurrencyTypeTable();
    CHECK(pTable[0] == 1 && pTable[3] == 3 && pTable[5] == 3 && pTable[11] == 2 &&
          pTable[15] == 4);
}

int main()
{
    CReferenceData* pData = CReferenceData::GetInstance();
    RefTableVector tables(REF_TABLE_SLOT_COUNT, (RefTableEntry*)NULL);
    pData->m_pTables = &tables;

    TestShopTables(*pData, tables);
    TestCurrencyTable();

    pData->m_pTables = NULL;
    return TEST_RESULT("ShardRefShopTest");
}
