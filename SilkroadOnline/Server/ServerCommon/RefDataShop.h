#pragma once

#include <map>
#include <string>
#include <vector>

#include "BSLib/DB/Database.h"

// The shop tables of the SR_ShardManager reference data: shops, shop groups, tab groups, tabs and
// their goods, package items with their scrap items and prices, the scrap item conditions and
// reward policies and the stock periods. CRefInstanceGenerator::LoadShopGroup (0x4a9a50) builds
// them all together when slot 0x27 (_RefShop) comes up; slots 0x15..0x26 and 0x5B do nothing of
// their own.

class CReferenceData;
struct RefTableEntry;
class RefObjCommon;
typedef RefObjCommon RefObjCommonRow;
struct RefMagicOptLevel;

struct RefShop;
struct RefShopTab;
struct RefPackageItem;

// Param / ParamDesc column pair (RefParam::ctor 0x515260: 0 and an empty string).
struct RefShopParam
{
    RefShopParam()
        : Param(0)
    {
    }

    int              Param;                      // +0
    std::string      Desc;                       // +4
};

C_ASSERT(sizeof(RefShopParam) == 0x20);

// ---------------------------------------------------------------------------------------------
// Rows

// _RefShopTabGroup row (0x54): RefShopTabGroup::InitMembers 0x515b10 / DestroyMembers 0x515a60
// (the IDA name of the reader is RefShopObject::Read).
struct RefShopTabGroup
{
    RefShopTabGroup()
        : Service(0)
    {
    }

    BYTE                     Service;            // +0
    DWORD                    Country;            // +4
    DWORD                    ID;                 // +8   key of m_mapShopTabGroup
    std::string              CodeName128;        // +C
    std::string              StrID128_Group;     // +28
    std::vector<RefShopTab*> m_vecRefShopTabs;   // +44  not owned
};

C_ASSERT(sizeof(RefShopTabGroup) == 0x54);

// _RefShopGoods row (0xCC): RefShopGoods::InitMembers 0x516520 / dtor 0x4a3c70.
struct RefShopGoods
{
    RefShopGoods()
        : Service(0), m_pRefPackageItem(NULL)
    {
    }

    BYTE                Service;                 // +0
    DWORD               Country;                 // +4
    RefPackageItem*     m_pRefPackageItem;       // +8
    DWORD               dw0C;                    // +C
    std::string         RefPackageItemCodeName;  // +10
    std::string         RefTabCodeName;          // +2C
    BYTE                m_btSlotIndex;           // +48
    RefShopParam        aParam[4];               // +4C
};

C_ASSERT(sizeof(RefShopGoods) == 0xCC);

enum
{
    MAX_ITEM_PER_SHOP_TAB = 240,
};

// _RefShopTab row (0x58): RefShopTab::InitMembers 0x516760 / DestroyMembers 0x5166b0. The goods
// are kept by slot index and not owned by the tab.
struct RefShopTab
{
    // InitMembers: 240 empty goods slots.
    RefShopTab()
        : Service(0), ID(0), RefTabGroupID(0)
    {
        m_AssignedGoods.resize(MAX_ITEM_PER_SHOP_TAB);
        for (std::vector<RefShopGoods*>::iterator it = m_AssignedGoods.begin();
             it != m_AssignedGoods.end(); ++it)
            *it = NULL;
    }

    // RefShopTab::AssignGoods (0x5163d0): stores the goods in its slot (asserts on a bad or
    // taken slot and on a package item without scrap items).
    void AssignGoods(RefShopGoods* pGoods);

    BYTE                       Service;          // +0
    DWORD                      Country;          // +4
    DWORD                      ID;               // +8   key of m_mapShopTab
    std::string                CodeName128;      // +C
    std::string                RefTabGroupCodeName; // +28
    DWORD                      RefTabGroupID;    // +44
    std::vector<RefShopGoods*> m_AssignedGoods;  // +48  MAX_ITEM_PER_SHOP_TAB slots
};

C_ASSERT(sizeof(RefShopTab) == 0x58);

// The 64-byte item type block of the scrap item and treat item rows: the TypeID bits of the
// item kind (Cash, TID1..TID4 columns) and an item code name ("xxx" for none).
// RefScrapItemCondition::InitMembers 0x5152c0 / DestroyMembers 0x5150f0.
struct RefShopItemTID
{
    RefShopItemTID()
        : wTID(0)
    {
    }

    WORD             wTID;                       // +0   TypeID bits (RefObj_GetTypeID layout)
    std::string      ItemCodeName;               // +4
    DWORD            RefItemID;                  // +20  0 for "xxx"
    std::string      str24;                      // +24
};

C_ASSERT(sizeof(RefShopItemTID) == 0x40);

// _RefConditionTo{Buy,Sell}ScrapItem / _RefRewardPolicyTo{Buy,Sell}ScrapItem row (0xD0):
// InitMembers 0x515520 ... / DestroyMembers 0x5154c0 ...
struct RefScrapItemRule
{
    RefScrapItemRule()
        : b00(0), dw04(0), Service(0)
    {
    }

    BYTE             b00;                        // +0   high part of the map key
    DWORD            dw04;                       // +4   low part of the map key
    RefShopParam     aParam[4];                  // +8
    RefShopItemTID   Item;                       // +88
    BYTE             Service;                    // +C8
    DWORD            Country;                    // +CC
};

C_ASSERT(sizeof(RefScrapItemRule) == 0xD0);

// _RefTreatItemOfShop row (0xD4): InitMembers 0x5170f0 / DestroyMembers 0x517090.
struct RefTreatItemOfShop : public RefScrapItemRule
{
    RefTreatItemOfShop()
        : RefShopID(0)
    {
    }

    DWORD            RefShopID;                  // +D0
};

C_ASSERT(sizeof(RefTreatItemOfShop) == 0xD4);

// _RefAccessPermissionOfShop, _RefRewardPolicyToSellPackageItem and
// _RefConditionToSellPackageItem rows (0x94): InitMembers 0x517640 / 0x518d00 / 0x5187c0. The
// last column is the code name of the shop / package item the row belongs to.
struct RefShopLinkedRule
{
    RefShopLinkedRule()
        : b00(0), dw04(0), Service(0)
    {
    }

    BYTE             b00;                        // +0
    DWORD            dw04;                       // +4
    RefShopParam     aParam[4];                  // +8
    BYTE             Service;                    // +88
    DWORD            Country;                    // +8C
    DWORD            LinkID;                     // +90  ID of the shop / package item
};

C_ASSERT(sizeof(RefShopLinkedRule) == 0x94);

typedef RefShopLinkedRule RefAccessPermissionOfShop;
typedef RefShopLinkedRule RefRewardPolicyToSellPackageItem;
typedef RefShopLinkedRule RefConditionToSellPackageItem;

// _RefScrapOfPackageItem row (0x110): InitMembers 0x518e30 / DestroyMembers 0x518dd0. The item
// instance a package item hands out (m_InstanceInfoOfItem of the original).
struct RefScrapOfPackageItem
{
    RefScrapOfPackageItem();

    BYTE             Service;                    // +0
    DWORD            Country;                    // +4
    DWORD            RefPackageItemID;           // +8
    DWORD            dw0C;                       // +C
    DWORD            RefItemID;                  // +10
    BYTE             OptLevel;                   // +14
    __int64          Variance;                   // +18
    DWORD            m_dwData;                   // +20  durability / stack count
    BYTE             m_btMagParamNum;            // +24
    unsigned __int64 MagParam[12];               // +28  low WORD: magic option, high DWORD: value
    RefObjCommonRow* m_pRefObjItem;              // +88
    DWORD            dw8C;                       // +8C
    RefShopParam     aParam[4];                  // +90  default (-1, "xxx")
};

C_ASSERT(sizeof(RefScrapOfPackageItem) == 0x110);

// _RefPricePolicyOfItem row (0x98): InitMembers 0x518d60.
struct RefPricePolicyOfItem
{
    RefPricePolicyOfItem()
        : Service(0), PaymentDevice(0), dw10(0), Cost(0)
    {
    }

    BYTE             Service;                    // +0
    DWORD            Country;                    // +4
    DWORD            PaymentDevice;              // +8   currency bits
    DWORD            RefPackageItemID;           // +C
    DWORD            dw10;                       // +10
    DWORD            Cost;                       // +14
    RefShopParam     aParam[4];                  // +18
};

C_ASSERT(sizeof(RefPricePolicyOfItem) == 0x98);

enum
{
    REF_PRICE_CURRENCY_COUNT = 16,
};

// The prices of a package item: a bit per currency and its amount.
struct RefPrice
{
    RefPrice()
        : dwMask(0), dw04(0)
    {
        for (int i = 0; i < REF_PRICE_CURRENCY_COUNT; ++i)
            aCost[i] = 0;
    }

    DWORD            dwMask;                     // +0
    DWORD            dw04;                       // +4
    __int64          aCost[REF_PRICE_CURRENCY_COUNT]; // +8
};

C_ASSERT(sizeof(RefPrice) == 0x88);

// _RefPackageItem row (0x188): InitMembers 0x519340 / DestroyMembers 0x518fd0 (the IDA name of
// the reader is RefPricePolicyConditionLevel::Read). Owns its scrap items and policies.
struct RefPackageItem
{
    RefPackageItem()
        : Service(0), ID(0), w28(0), m_bSpecialty(0)
    {
    }

    ~RefPackageItem();

    // RefPackageItem::AddScrapItem (0x4952c0).
    void AddScrapItem(RefScrapOfPackageItem* pScrapItem);
    // RefPackageItem::ValidatePrice (0x4954d0): a single item needs the price of its kind; an
    // expandable item without a stock count ends the process.
    void ValidatePrice();
    // RefPackageItem::ValidateCost (0x495650): stores the price of a price policy row; a row
    // without a currency (or a price set the check rejects) ends the process.
    void ValidateCost(const RefPricePolicyOfItem& policy);

    BYTE                       Service;          // +0
    DWORD                      Country;          // +4
    DWORD                      ID;               // +8   key of m_mapPackageItem
    std::string                CodeName128;      // +C
    WORD                       w28;              // +28
    std::string                ExpandTerm;       // +2C  EXPAND_TERM_ALL / EXPAND_TERM_ONE
    RefPrice                   Price;            // +48
    RefShopParam               aParam[4];        // +D0
    std::vector<RefScrapOfPackageItem*> m_vecAssignedScrapOfPackageItems; // +150
    std::vector<RefRewardPolicyToSellPackageItem*> m_vecRewardPolicyToSell; // +160
    std::vector<RefConditionToSellPackageItem*> m_vecConditionToSell; // +170
    BOOL                       m_bSpecialty;     // +180 the first scrap item is a trade good
};

C_ASSERT(FIELD_OFFSET(RefPackageItem, Price) == 0x48);
C_ASSERT(FIELD_OFFSET(RefPackageItem, m_vecAssignedScrapOfPackageItems) == 0x150);
C_ASSERT(sizeof(RefPackageItem) == 0x188);

// _RefShop row (0xD4): InitMembers 0x517ff0 / DestroyMembers 0x517830 (the IDA name of the
// reader is RefShopTabGroup::Read). Owns its access permissions and treat items.
struct RefShop
{
    // InitMembers: room for 10 tabs.
    RefShop()
        : Service(0), Country(0), ID(0)
    {
        m_vecRefShopTabs.reserve(10);
    }

    ~RefShop();

    // RefShop::AddTreatItem (0x516cb0): by the TypeID bits of the row.
    void AddTreatItem(RefTreatItemOfShop* pTreatItem)
    {
        m_mmapTreatItem.insert(std::make_pair((DWORD)pTreatItem->Item.wTID, pTreatItem));
    }

    BYTE                       Service;          // +0
    DWORD                      Country;          // +4
    DWORD                      ID;               // +8   key of m_mapShop
    std::string                CodeName128;      // +C
    RefShopParam               aParam[4];        // +28
    std::vector<RefShopTab*>   m_vecRefShopTabs; // +A8  not owned
    std::vector<RefAccessPermissionOfShop*> m_vecAccessPermission; // +B8
    std::multimap<DWORD, RefTreatItemOfShop*> m_mmapTreatItem; // +C8
};

C_ASSERT(sizeof(RefShop) == 0xD4);

// _RefShopGroup row (0xBC): InitMembers 0x516330 / DestroyMembers 0x5161f0.
struct RefShopGroup
{
    RefShopGroup()
        : Service(0), Country(0), ID(0), RefNPCID(0)
    {
    }

    BYTE                     Service;            // +0
    DWORD                    Country;            // +4
    WORD                     ID;                 // +8   key of m_mapShopGroup
    std::string              CodeName128;        // +C
    DWORD                    RefNPCID;           // +28  0 for "xxx"
    RefShopParam             aParam[4];          // +2C
    std::vector<RefShop*>    m_vecRefShops;      // +AC  not owned
};

C_ASSERT(sizeof(RefShopGroup) == 0xBC);

// _RefMappingShopGroup row (read into a local object): InitMembers 0x5160a0 / DestroyMembers
// 0x516010.
struct RefMappingShopGroup
{
    RefMappingShopGroup()
        : Service(0), RefShopGroupID(0), RefShopID(0), Country(0)
    {
    }

    BYTE             Service;                    // +0
    DWORD            Country;                    // +4
    WORD             RefShopGroupID;             // +8
    DWORD            RefShopID;                  // +C
    std::string      RefShopCodeName;            // +10
    std::string      RefShopGroupCodeName;       // +2C
};

C_ASSERT(sizeof(RefMappingShopGroup) == 0x48);

// _RefMappingShopWithTab row (read into a local object): InitMembers 0x517730 / DestroyMembers
// 0x5176a0.
struct RefMappingShopWithTab
{
    RefMappingShopWithTab()
        : Service(0), Country(0), RefShopID(0), RefTabGroupID(0)
    {
    }

    BYTE             Service;                    // +0
    DWORD            Country;                    // +4
    DWORD            RefShopID;                  // +8
    DWORD            RefTabGroupID;              // +C
    std::string      RefShopCodeName;            // +10
    std::string      RefTabGroupCodeName;        // +2C
};

C_ASSERT(sizeof(RefMappingShopWithTab) == 0x48);

// _RefShopItemStockPeriod row (0x68): RecCtor 0x4dd8a0 / RecDtor 0x4dd810. Column names are not
// known.
struct RefShopItemStockPeriod
{
    RefShopItemStockPeriod()
        : Service(0), dw04(0), ID(0), b64(0)
    {
    }

    BYTE             Service;                    // +0
    DWORD            dw04;                       // +4
    DWORD            ID;                         // +8   key of the map
    std::string      str0C;                      // +C
    std::string      str28;                      // +28
    TIMESTAMP_STRUCT ts44;                       // +44
    TIMESTAMP_STRUCT ts54;                       // +54
    BYTE             b64;                        // +64  0 or 1
};

C_ASSERT(sizeof(RefShopItemStockPeriod) == 0x68);

// ---------------------------------------------------------------------------------------------
// Builders and lookups

// CRefInstanceGenerator::LoadShopGroup (0x4a9a50), the builder of slot 0x27: the package item,
// tab group, tab, shop and shop group tables, the scrap item rules and the stock periods, then
// ValidatePackageItemPrices. The first table that is empty or fails stops it (FALSE).
int RefData_BuildShopTables(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);
// The builder of slots 0x15..0x26 and 0x5B: nothing (LoadShopGroup reads them).
int RefData_BuildWithShopTables(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);

// The lookups by country and code name (CRefInstanceGenerator::Find*): NULL when there is none.
RefShopGroup* RefData_FindShopGroup(CReferenceData* pData, const std::string& strCodeName,
                                    int nCountry);                              // 0x49e870
RefShopTabGroup* RefData_FindShopTabGroup(CReferenceData* pData, const std::string& strCodeName,
                                          int nCountry);                        // 0x49ef90
RefShopTab* RefData_FindShopTab(CReferenceData* pData, const std::string& strCodeName,
                                int nCountry);                                  // 0x49f2a0
RefPackageItem* RefData_FindPackageItem(CReferenceData* pData, const std::string& strCodeName,
                                        int nCountry);                          // 0x49fba0
RefShop* RefData_FindShop(CReferenceData* pData, const std::string& strCodeName,
                          int nCountry);                                        // 0x49fef0

// RefPrice_GetCurrencyTypeTable (0x49f630, filled by RefPrice_InitCurrencyTypeTable 0x49f690):
// the kind of each of the 16 currencies (3: silk).
const int* RefPrice_GetCurrencyTypeTable();

// ---------------------------------------------------------------------------------------------
// Item and magic option checks of the scrap item rows. They belong to the object and magic option
// tables; RefDataShopPending.cpp holds stand-ins until those are ported.

// RefObjItem_IsValidDurabilityOrStack (0x51adc0, SM_rf_obj).
BOOL RefObjItem_IsValidDurabilityOrStack(const RefObjCommonRow* pRefObj, DWORD dwData);
// RefObj_IsValidSlotForItem (0x51ada0, SM_rf_obj).
BOOL RefObj_IsValidSlotForItem(const RefObjCommonRow* pRefObj, BYTE btOptLevel);
// RefData_IsMagicOptEntryAllowed (0x51ad30, SM_rf_magicopt).
BOOL RefData_IsMagicOptEntryAllowed(const RefObjCommonRow* pRefObj, WORD wMagicOptID);
// CRefInstanceGenerator::FindMagicOptLevelEntry (0x51aa10, SM_rf_magicopt) on the tables at
// CReferenceData+0x268: NULL when there is none.
RefMagicOptLevel* RefData_FindMagicOptLevelEntry(CReferenceData* pData, WORD wMagicOptID);
// MagOptEachLevel_FindByID (0x49e780, SM_rf_magicopt) and vf03 / vf04 of its value object: the
// value range of a magic option level; FALSE when the level has no such entry.
BOOL RefMagicOptLevel_GetValueRange(const RefMagicOptLevel* pEntry, DWORD& dwMin, DWORD& dwMax);
