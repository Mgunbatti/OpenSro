#include "ServerCommon/RefDataShop.h"

#include <stdlib.h>

#include <algorithm>

#include "BSLib/BSDebug.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerCommon/RefDataMisc.h"
#include "ServerCommon/ReferenceData.h"

// The source paths of the assert dialogs.
static const char s_szReferenceDataCpp[] =
    "D:\\WORK2005\\Source\\SilkroadOnline\\Server\\ServerCommon\\ReferenceData.cpp";
static const char s_szReferenceDataH[] =
    "d:\\work2005\\source\\silkroadonline\\server\\servercommon\\ReferenceData.h";

// The code name of "no row" in the shop tables.
static const char s_szNone[] = "xxx";

// Object fields the shop tables read (see InstanceRecords.h for the others).
static DWORD RefShop_GetObjID(const RefObjCommonRow* pRefObj)                   // +8
{
    return *(const DWORD*)((const BYTE*)pRefObj + 0x8);
}

static DWORD RefShop_GetObjAvailableOPs(const RefObjCommonRow* pRefObj)         // +8C
{
    return *(const DWORD*)((const BYTE*)pRefObj + 0x8C);
}

// TypeID 3/3/3/8 without the bit 1: a trade good (IsSpecialty).
static BOOL RefShop_IsSpecialty(WORD wTID)
{
    return (wTID & 2) == 0 && (wTID & 0x1C) == 0xC && (wTID & 0x60) == 0x60 &&
           (wTID & 0x780) == 0x400;
}

static CReferenceData* RefShop_GetReferenceData()
{
    if (!g_pReferenceData)
        BSAssertFail();
    return g_pReferenceData;
}

// ---------------------------------------------------------------------------------------------
// Rows

RefScrapOfPackageItem::RefScrapOfPackageItem()
    : Service(0), RefPackageItemID(0), RefItemID(0), OptLevel(0), Variance(0), m_dwData(0),
      m_btMagParamNum(0), m_pRefObjItem(NULL)
{
    for (int i = 0; i < 12; ++i)
        MagParam[i] = 0;
    for (int j = 0; j < 4; ++j)
    {
        aParam[j].Param = -1;
        aParam[j].Desc.assign(s_szNone, 3);
    }
}

// DestroyMembers (0x518fd0): the scrap items and the two policy lists are freed.
RefPackageItem::~RefPackageItem()
{
    for (std::vector<RefScrapOfPackageItem*>::iterator it =
             m_vecAssignedScrapOfPackageItems.begin();
         it != m_vecAssignedScrapOfPackageItems.end(); ++it)
    {
        if (*it)
            delete *it;
    }
    m_vecAssignedScrapOfPackageItems.clear();
    for (std::vector<RefRewardPolicyToSellPackageItem*>::iterator it =
             m_vecRewardPolicyToSell.begin();
         it != m_vecRewardPolicyToSell.end(); ++it)
    {
        if (*it)
            delete *it;
    }
    m_vecRewardPolicyToSell.clear();
    for (std::vector<RefConditionToSellPackageItem*>::iterator it = m_vecConditionToSell.begin();
         it != m_vecConditionToSell.end(); ++it)
    {
        if (*it)
            delete *it;
    }
    m_vecConditionToSell.clear();
}

// DestroyMembers (0x517830): the tabs are only dropped, the access permissions and treat items
// freed.
RefShop::~RefShop()
{
    m_vecRefShopTabs.clear();
    for (std::vector<RefAccessPermissionOfShop*>::iterator it = m_vecAccessPermission.begin();
         it != m_vecAccessPermission.end(); ++it)
    {
        if (*it)
            delete *it;
    }
    m_vecAccessPermission.clear();
    for (std::multimap<DWORD, RefTreatItemOfShop*>::iterator it = m_mmapTreatItem.begin();
         it != m_mmapTreatItem.end(); ++it)
    {
        if (it->second)
            delete it->second;
    }
    m_mmapTreatItem.clear();
}

void RefShopTab::AssignGoods(RefShopGoods* pGoods)
{
    if (!pGoods)
    {
        BSAssertFail();
        return;
    }
    if (pGoods->m_btSlotIndex >= MAX_ITEM_PER_SHOP_TAB &&
        !BSAssertDialog(1809, s_szReferenceDataH,
                        "0 <= pGoods->m_btSlotIndex && pGoods->m_btSlotIndex < "
                        "MAX_ITEM_PER_SHOP_TAB"))
        DebugBreak();
    if (pGoods->m_pRefPackageItem->m_vecAssignedScrapOfPackageItems.size() == 0 &&
        !BSAssertDialog(1820, s_szReferenceDataH,
                        "pGoods->m_pRefPackageItem->m_vecAssignedScrapOfPackageItems.size()"))
        DebugBreak();
    if (m_AssignedGoods.at(pGoods->m_btSlotIndex) &&
        !BSAssertDialog(1822, s_szReferenceDataH, "!m_AssignedGoods[pGoods->m_btSlotIndex]"))
        DebugBreak();
    m_AssignedGoods.at(pGoods->m_btSlotIndex) = pGoods;
}

// RefShopTabGroup::AddShopTab (0x5165b0).
static void RefShopTabGroup_AddShopTab(RefShopTabGroup* pTabGroup, RefShopTab* pRefShopTab)
{
    std::vector<RefShopTab*>& vecTabs = pTabGroup->m_vecRefShopTabs;
    if (std::find(vecTabs.begin(), vecTabs.end(), pRefShopTab) != vecTabs.end() &&
        !BSAssertDialog(1574, s_szReferenceDataH,
                        "m_vecRefShopTabs.end() == std::find(m_vecRefShopTabs.begin(), "
                        "m_vecRefShopTabs.end(), pRefShopTab)"))
        DebugBreak();
    vecTabs.push_back(pRefShopTab);
}

// RefShop::AddTabGroups (0x495be0): the tabs of the tab group are appended to the shop.
static void RefShop_AddTabGroup(RefShopTabGroup* pTabGroup, RefShop* pShop)
{
    if (!pTabGroup)
        BSAssertFail();
    for (std::vector<RefShopTab*>::iterator it = pTabGroup->m_vecRefShopTabs.begin();
         it != pTabGroup->m_vecRefShopTabs.end(); ++it)
        pShop->m_vecRefShopTabs.push_back(*it);
}

void RefPackageItem::AddScrapItem(RefScrapOfPackageItem* pScrapItem)
{
    if (!pScrapItem)
        BSAssertFail();
    m_vecAssignedScrapOfPackageItems.push_back(pScrapItem);
    if (RefShop_IsSpecialty(RefObj_GetTypeID(m_vecAssignedScrapOfPackageItems.at(0)->m_pRefObjItem))
        != RefShop_IsSpecialty(RefObj_GetTypeID(pScrapItem->m_pRefObjItem)))
    {
        BSMessageBoxPrintf("(%s) Specialty don't discord with Normal Type Item",
                           CodeName128.c_str());
        if (RefShop_IsSpecialty(
                RefObj_GetTypeID(m_vecAssignedScrapOfPackageItems.at(0)->m_pRefObjItem))
                != RefShop_IsSpecialty(RefObj_GetTypeID(pScrapItem->m_pRefObjItem)) &&
            !BSAssertDialog(3157, s_szReferenceDataCpp,
                            "m_vecAssignedScrapOfPackageItems[0]->m_InstanceInfoOfItem."
                            "m_pRefObjItem->GetTID().IsSpecialty() == pScrapItem->"
                            "m_InstanceInfoOfItem.m_pRefObjItem->GetTID().IsSpecialty()"))
            DebugBreak();
    }
    m_bSpecialty = RefShop_IsSpecialty(
        RefObj_GetTypeID(m_vecAssignedScrapOfPackageItems.at(0)->m_pRefObjItem));
}

void RefPackageItem::ValidatePrice()
{
    if (m_vecAssignedScrapOfPackageItems.size() == 1)
    {
        DWORD dwMask = Price.dwMask;
        if (RefObj_GetTypeID(m_vecAssignedScrapOfPackageItems.at(0)->m_pRefObjItem) & 1)
        {
            RefPrice_GetCurrencyTypeTable();
            if (!(dwMask & 2))
                BSMessageBoxPrintf("(%s) register silk price.", CodeName128.c_str());
        }
        else
        {
            RefPrice_GetCurrencyTypeTable();
            if (!(dwMask & 1))
                BSMessageBoxPrintf("(%s) register gold price.", CodeName128.c_str());
        }
        return;
    }
    for (std::vector<RefScrapOfPackageItem*>::iterator it =
             m_vecAssignedScrapOfPackageItems.begin();
         it != m_vecAssignedScrapOfPackageItems.end(); ++it)
    {
        RefScrapOfPackageItem* pRefScrap = *it;
        WORD wTID = RefObj_GetTypeID(pRefScrap->m_pRefObjItem);
        if ((wTID & 2) == 0 && (wTID & 0x1C) == 0xC && (wTID & 0x60) == 0x60 &&
            !pRefScrap->m_dwData)
        {
            BSMessageBoxPrintf("(%s) Expandable Items must be set StockCount > 0",
                               CodeName128.c_str());
            if (!pRefScrap->m_dwData &&
                !BSAssertDialog(3265, s_szReferenceDataCpp,
                                "pRefScrap->m_InstanceInfoOfItem.m_dwData"))
                DebugBreak();
            exit(0);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Prices

// RefPrice_InitCurrencyTypeTable (0x49f690).
static const int s_anCurrencyType[REF_PRICE_CURRENCY_COUNT] =
{
    1, 2, 2, 3, 2, 3, 4, 4, 4, 4, 4, 2, 2, 4, 4, 4,
};

const int* RefPrice_GetCurrencyTypeTable()
{
    return s_anCurrencyType;
}

// RefPrice_AllCurrenciesOfKind (0x49f830): TRUE when there is a price and each of them is of
// the kind.
static BOOL RefPrice_AllCurrenciesOfKind(int nKind, DWORD dwMask, const int* pTable)
{
    for (int i = 0; i < REF_PRICE_CURRENCY_COUNT; ++i)
    {
        if (((1 << i) & dwMask) && pTable[i] != nKind)
            return FALSE;
    }
    return dwMask != 0;
}

// RefPrice_HasOnlyAllowedCurrencies (0x49f7e0).
static BOOL RefPrice_HasOnlyAllowedCurrencies(DWORD dwMask, const int* pTable)
{
    for (int nKind = 0; nKind < 5; ++nKind)
    {
        // BUG(SM-020): the prices cannot all be of kind 2 and of another kind at once, so the
        // check never fails.
        if (nKind != 2 && RefPrice_AllCurrenciesOfKind(2, dwMask, pTable) &&
            RefPrice_AllCurrenciesOfKind(nKind, dwMask, pTable))
            return FALSE;
    }
    return TRUE;
}

// RefPrice_GetSilkCost (0x49f780): the amount of the first silk price, 0 without one.
static __int64 RefPrice_GetSilkCost(const RefPrice& price)
{
    for (int i = 0; i < REF_PRICE_CURRENCY_COUNT; ++i)
    {
        if (((1 << i) & price.dwMask) && RefPrice_GetCurrencyTypeTable()[i] == 3)
            return price.aCost[i];
    }
    return 0;
}

// RefPrice_SetCost (0x49f5b0): a second silk price is refused.
static void RefPrice_SetCost(RefPrice& price, int nCurrency, __int64 nCost)
{
    if (nCurrency >= REF_PRICE_CURRENCY_COUNT)
        BSAssertFail();
    if (nCost == 0x7FFFFFFFFFFFFFFFi64)
        BSAssertFail();
    const int* pTable = RefPrice_GetCurrencyTypeTable();
    if (nCurrency >= REF_PRICE_CURRENCY_COUNT)
        BSAssertFail();
    if (pTable[nCurrency] == 3 && RefPrice_GetSilkCost(price))
    {
        BSAssertFail();
        return;
    }
    price.dwMask |= 1 << nCurrency;
    if (nCurrency < REF_PRICE_CURRENCY_COUNT)
        price.aCost[nCurrency] = nCost;
}

void RefPackageItem::ValidateCost(const RefPricePolicyOfItem& policy)
{
    if (!RefPrice_HasOnlyAllowedCurrencies(Price.dwMask, RefPrice_GetCurrencyTypeTable()))
    {
        BSMessageBoxPrintf("Remove all price of (%s) except silkprice suit.",
                           CodeName128.c_str());
        if (!BSAssertDialog(3291, s_szReferenceDataCpp, "FALSE"))
            DebugBreak();
        exit(0);
    }
    for (int nCurrency = 0; nCurrency < REF_PRICE_CURRENCY_COUNT; ++nCurrency)
    {
        if ((1 << nCurrency) & policy.PaymentDevice)
        {
            RefPrice_SetCost(Price, nCurrency, (__int64)policy.Cost);
            return;
        }
    }
    BSMessageBoxPrintf("insert cost to %s", CodeName128.c_str());
    if (!BSAssertDialog(3319, s_szReferenceDataCpp, "FALSE"))
        DebugBreak();
    exit(0);
}

// ---------------------------------------------------------------------------------------------
// Lookups by country and code name

template <class T>
static T* RefShop_FindByCodeName(std::map<int, std::map<std::string, T*>*>& index,
                                 const std::string& strCodeName, int nCountry)
{
    typename std::map<int, std::map<std::string, T*>*>::iterator it = index.find(nCountry);
    if (it == index.end())
        return NULL;
    std::map<std::string, T*>* pByCodeName = it->second;
    typename std::map<std::string, T*>::iterator itRow = pByCodeName->find(strCodeName);
    if (itRow == pByCodeName->end())
        return NULL;
    return itRow->second;
}

// The code name map of a country, created when there is none yet.
template <class T>
static std::map<std::string, T*>* RefShop_GetCodeNameMap(
    std::map<int, std::map<std::string, T*>*>& index, int nCountry)
{
    typename std::map<int, std::map<std::string, T*>*>::iterator it = index.find(nCountry);
    if (it != index.end())
        return it->second;
    std::map<std::string, T*>* pByCodeName = new std::map<std::string, T*>;
    index.insert(std::make_pair(nCountry, pByCodeName));
    return pByCodeName;
}

// A code name that is already there asserts; the first row keeps it.
template <class T>
static void RefShop_IndexByCodeName(std::map<int, std::map<std::string, T*>*>& index, T* pRow)
{
    std::map<std::string, T*>* pByCodeName = RefShop_GetCodeNameMap(index, (int)pRow->Country);
    if (pByCodeName->find(pRow->CodeName128) != pByCodeName->end())
        BSAssertFail();
    pByCodeName->insert(std::make_pair(pRow->CodeName128, pRow));
}

RefShopGroup* RefData_FindShopGroup(CReferenceData* pData, const std::string& strCodeName,
                                    int nCountry)
{
    return RefShop_FindByCodeName(pData->m_mapShopGroupByCodeName, strCodeName, nCountry);
}

RefShopTabGroup* RefData_FindShopTabGroup(CReferenceData* pData, const std::string& strCodeName,
                                          int nCountry)
{
    return RefShop_FindByCodeName(pData->m_mapShopTabGroupByCodeName, strCodeName, nCountry);
}

RefShopTab* RefData_FindShopTab(CReferenceData* pData, const std::string& strCodeName,
                                int nCountry)
{
    return RefShop_FindByCodeName(pData->m_mapShopTabByCodeName, strCodeName, nCountry);
}

RefPackageItem* RefData_FindPackageItem(CReferenceData* pData, const std::string& strCodeName,
                                        int nCountry)
{
    return RefShop_FindByCodeName(pData->m_mapPackageItemByCodeName, strCodeName, nCountry);
}

RefShop* RefData_FindShop(CReferenceData* pData, const std::string& strCodeName, int nCountry)
{
    return RefShop_FindByCodeName(pData->m_mapShopByCodeName, strCodeName, nCountry);
}

// The map value of a key, NULL when there is none (STL_map_*_FindValue).
template <class TMap>
static typename TMap::mapped_type RefShop_FindValue(TMap& map, typename TMap::key_type key)
{
    typename TMap::iterator it = map.find(key);
    if (it == map.end())
        return NULL;
    return it->second;
}

// ---------------------------------------------------------------------------------------------
// Row readers (the address is the reader of the original). A reader returns the key of the row,
// 0 when the row is dropped.

// RefShopData_ReadCondition4 (0x493c80).
template <class T>
static void RefShop_ReadCondition4(CRefRow& row, T& r)
{
    row >> r.b00 >> r.dw04;
    for (int i = 0; i < 4; ++i)
        row >> r.aParam[i].Param >> r.aParam[i].Desc;
}

// The four Param / ParamDesc pairs at the end of most rows.
static void RefShop_ReadParams(CRefRow& row, RefParam* pParam)
{
    for (int i = 0; i < 4; ++i)
        row >> pParam[i].Param >> pParam[i].Desc;
}

// RefShopData_ReadItemTID (0x493d20).
static void RefShop_ReadItemTID(CRefRow& row, RefShopItemTID& r)
{
    BYTE btCash, btType1, btType2, btType3, btType4;
    row >> btCash >> btType1 >> btType2 >> btType3 >> btType4 >> r.ItemCodeName;
    if (btCash >= 2 &&
        !BSAssertDialog(2466, s_szReferenceDataCpp, "btCash < MAX_CASHITEM_TID_COUNT"))
        DebugBreak();
    if (btType1 >= 8 && !BSAssertDialog(2467, s_szReferenceDataCpp, "btType1 < MAX_TID1_COUNT"))
        DebugBreak();
    if (btType2 >= 4 && !BSAssertDialog(2468, s_szReferenceDataCpp, "btType2 < MAX_TID2_COUNT"))
        DebugBreak();
    if (btType3 >= 16 &&
        !BSAssertDialog(2469, s_szReferenceDataCpp, "btType3 < MAX_TID3_COUNT"))
        DebugBreak();
    if (btType4 >= 32 &&
        !BSAssertDialog(2470, s_szReferenceDataCpp, "btType4 < MAX_TID4_COUNT"))
        DebugBreak();
    r.wTID = (WORD)((btCash & 1) | (4 * ((btType1 & 7) | (btType4 << 9))) | (r.wTID & 2) |
                    ((32 * ((btType2 & 3) | (4 * (btType3 & 0xF)))) & 0x7FF));
    if (r.ItemCodeName.compare(s_szNone) != 0)
    {
        RefObjCommonRow* pRefObj = RefShop_GetReferenceData()->FindRefObjByCodeName(
            r.ItemCodeName);
        if (pRefObj)
            r.RefItemID = RefShop_GetObjID(pRefObj);
        else
            BSMessageBoxPrintf("can't find %s from _RefObjCommon. check _RefObjCommon",
                               r.ItemCodeName.c_str());
    }
    else
    {
        r.RefItemID = 0;
    }
}

// The leading Service, Country and code name columns of the rows that belong to a shop or a
// package item: FALSE for a row out of service or of an unknown shop / package item (the rest
// of the row is not read then). "xxx" links to nothing (ID 0).
template <class T, T* (*pfnFind)(CReferenceData*, const std::string&, int)>
static BOOL RefShop_ReadLink(CRefRow& row, BYTE& btService, DWORD& dwCountry, DWORD& dwLinkID,
                             const char* pszFormat)
{
    row >> btService;
    if (!btService)
        return FALSE;
    std::string strCodeName;
    row >> dwCountry >> strCodeName;
    if (strCodeName == s_szNone)
    {
        dwLinkID = 0;
        return TRUE;
    }
    T* pLinked = pfnFind(RefShop_GetReferenceData(), strCodeName, (int)dwCountry);
    if (!pLinked)
    {
        BSMessageBoxPrintf(pszFormat, strCodeName.c_str());
        return FALSE;
    }
    dwLinkID = pLinked->ID;
    return TRUE;
}

// RefTreatItemOfShop::Read (0x493f90): the shop ID.
static DWORD RefRow_Read(CRefRow& row, RefTreatItemOfShop& r)
{
    if (!RefShop_ReadLink<RefShop, RefData_FindShop>(
            row, r.Service, r.Country, r.RefShopID,
            "can't find %s from RefShop. check _RefTreatItemOfShop"))
        return 0;
    RefShop_ReadItemTID(row, r.Item);
    RefShop_ReadCondition4(row, r);
    return r.RefShopID;
}

// RefAccessPermissionOfShop::Read (0x494160): the shop ID.
static DWORD RefRow_ReadAccessPermission(CRefRow& row, RefAccessPermissionOfShop& r)
{
    if (!RefShop_ReadLink<RefShop, RefData_FindShop>(
            row, r.Service, r.Country, r.LinkID,
            "can't find %s from RefShop. check _RefAccessPermissionOfShop"))
        return 0;
    RefShop_ReadCondition4(row, r);
    return r.LinkID;
}

// RefRewardPolicyToSellPackageItem::Read (0x494320): the package item ID.
static DWORD RefRow_ReadRewardPolicyToSell(CRefRow& row, RefRewardPolicyToSellPackageItem& r)
{
    if (!RefShop_ReadLink<RefPackageItem, RefData_FindPackageItem>(
            row, r.Service, r.Country, r.LinkID,
            "can't find %s from _RefPackageItem. check _RefRewardPolicyToSellPackageItem"))
        return 0;
    RefShop_ReadCondition4(row, r);
    return r.LinkID;
}

// RefConditionToSellPackageItem::Read (0x494570): the package item ID.
static DWORD RefRow_ReadConditionToSell(CRefRow& row, RefConditionToSellPackageItem& r)
{
    if (!RefShop_ReadLink<RefPackageItem, RefData_FindPackageItem>(
            row, r.Service, r.Country, r.LinkID,
            "can't find %s from _RefPackageItem. check _RefConditionToSellPackageItem"))
        return 0;
    RefShop_ReadCondition4(row, r);
    return r.LinkID;
}

// RefScrapItemPolicy::Read (0x4944e0): the key (b00 << 32 | dw04).
static unsigned __int64 RefRow_Read(CRefRow& row, RefScrapItemRule& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    row >> r.Country;
    RefShop_ReadItemTID(row, r.Item);
    RefShop_ReadCondition4(row, r);
    return ((unsigned __int64)r.b00 << 32) | r.dw04;
}

// RefShopObject::Read (0x494770): the tab group ID.
static DWORD RefRow_Read(CRefRow& row, RefShopTabGroup& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    row >> r.Country >> r.ID >> r.CodeName128 >> r.StrID128_Group;
    return r.ID;
}

// RefPricePolicyOfItem::Read (0x494810): the package item ID.
static DWORD RefRow_Read(CRefRow& row, RefPricePolicyOfItem& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    std::string strPackageItem;
    row >> r.Country >> strPackageItem >> r.PaymentDevice;
    row.Skip();
    row >> r.Cost;
    RefPackageItem* pPackageItem = RefData_FindPackageItem(RefShop_GetReferenceData(),
                                                           strPackageItem, (int)r.Country);
    if (!pPackageItem)
    {
        BSMessageBoxPrintf("can't find %s from _RefPackageItem. check _RefPricePolicyOfItem",
                           strPackageItem.c_str());
        return 0;
    }
    r.RefPackageItemID = pPackageItem->ID;
    RefShop_ReadParams(row, r.aParam);
    return r.RefPackageItemID;
}

// RefScrapOfPackageItem::Read (0x494a10): the package item ID. An unknown item ends the process;
// an item instance the item does not allow is rejected.
static DWORD RefRow_Read(CRefRow& row, RefScrapOfPackageItem& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    std::string strPackageItem, strItem;
    row >> r.Country >> strPackageItem >> strItem >> r.OptLevel >> r.Variance >> r.m_dwData
        >> r.m_btMagParamNum;
    CReferenceData* pData = RefShop_GetReferenceData();
    RefPackageItem* pPackageItem = RefData_FindPackageItem(pData, strPackageItem,
                                                           (int)r.Country);
    if (!pPackageItem)
    {
        BSMessageBoxPrintf("can't find %s from _RefPackageItem. check _RefScrapOfPackageItem",
                           strPackageItem.c_str());
        return 0;
    }
    r.RefPackageItemID = pPackageItem->ID;
    RefObjCommonRow* pRefObj = RefShop_GetReferenceData()->FindRefObjByCodeName(strItem);
    if (!pRefObj)
    {
        BSMessageBoxPrintf("can't find assoc Item with this Package [PackageID: %s, ItemCode: %s]",
                           strPackageItem.c_str(), strItem.c_str());
        exit(0);
    }
    r.RefItemID = RefShop_GetObjID(pRefObj);
    for (int i = 0; i < 12; ++i)
        row >> r.MagParam[i];
    RefShop_ReadParams(row, r.aParam);
    RefObjCommonRow* pRefItem = RefShop_GetReferenceData()->FindRefObj(r.RefItemID);
    r.m_pRefObjItem = pRefItem;
    if (r.Service)
    {
        if (!RefObjItem_IsValidDurabilityOrStack(pRefItem, r.m_dwData))
        {
            BSMessageBoxPrintf("error data that is a durability or maxstack of a item. "
                               "[PackageID: %s, ItemCode: %s]",
                               strPackageItem.c_str(), strItem.c_str());
            if (!BSAssertDialog(2989, s_szReferenceDataCpp, "FALSE"))
                DebugBreak();
            return 0;
        }
        if (!RefObj_IsValidSlotForItem(r.m_pRefObjItem, r.OptLevel))
        {
            BSMessageBoxPrintf("error data that is a magicoption level. "
                               "[PackageID: %s, ItemCode: %s]",
                               strPackageItem.c_str(), strItem.c_str());
            if (!BSAssertDialog(3006, s_szReferenceDataCpp, "FALSE"))
                DebugBreak();
            return 0;
        }
        if (r.m_btMagParamNum >= 12 &&
            !BSAssertDialog(3011, s_szReferenceDataCpp,
                            "m_InstanceInfoOfItem.m_btMagParamNum < MAX_MAGPARAM_PER_ITEM"))
            DebugBreak();
        for (int nParam = 0; nParam < r.m_btMagParamNum; ++nParam)
        {
            WORD wMagicOptID = (WORD)r.MagParam[nParam];
            DWORD dwValue = (DWORD)(r.MagParam[nParam] >> 32);
            if (!RefData_IsMagicOptEntryAllowed(pRefItem, wMagicOptID))
            {
                BSMessageBoxPrintf("error data that is a magicoption value. "
                                   "[PackageID: %s, ItemCode: %s]",
                                   strPackageItem.c_str(), strItem.c_str());
                if (!BSAssertDialog(3034, s_szReferenceDataCpp, "FALSE"))
                    DebugBreak();
                return 0;
            }
            RefMagicOptLevel* pEntry = RefData_FindMagicOptLevelEntry(RefShop_GetReferenceData(),
                                                                      wMagicOptID);
            if (!pEntry)
            {
                BSMessageBoxPrintf("(%s) can't find %d from MagOptEachLevel. "
                                   "check _RefScrapOfPackageItem",
                                   strPackageItem.c_str(), (int)wMagicOptID);
                return 0;
            }
            DWORD dwMin, dwMax;
            if (!RefMagicOptLevel_GetValueRange(pEntry, dwMin, dwMax) || dwMin > dwValue ||
                dwMax < dwValue)
            {
                BSMessageBoxPrintf("(%s) error data out of range to MagicoptionValue",
                                   strPackageItem.c_str());
                if (!BSAssertDialog(3069, s_szReferenceDataCpp, "FALSE"))
                    DebugBreak();
                return 0;
            }
        }
    }
    row.Skip();
    return r.RefPackageItemID;
}

// RefPricePolicyConditionLevel::Read (0x495130): the package item ID.
static DWORD RefRow_Read(CRefRow& row, RefPackageItem& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    row >> r.Country >> r.ID >> r.CodeName128 >> r.w28 >> r.ExpandTerm;
    if (r.ExpandTerm.compare("EXPAND_TERM_ALL") != 0 &&
        r.ExpandTerm.compare("EXPAND_TERM_ONE") != 0)
    {
        BSMessageBoxPrintf("insert a PricePolicyConditionLevel %s", r.CodeName128.c_str());
        if (!BSAssertDialog(3110, s_szReferenceDataCpp, "FALSE"))
            DebugBreak();
    }
    row.Skip();
    row.Skip();
    row.Skip();
    RefShop_ReadParams(row, r.aParam);
    return r.ID;
}

// RefShopGoods::Read (0x495740): the tab ID. Every item of the package must be sellable; cash
// items only go into MALL_ tabs; trade goods are collected in CReferenceData+0x22C.
static DWORD RefRow_Read(CRefRow& row, RefShopGoods& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    row >> r.Country >> r.RefTabCodeName >> r.RefPackageItemCodeName >> r.m_btSlotIndex;
    RefShopTab* pTab = RefData_FindShopTab(RefShop_GetReferenceData(), r.RefTabCodeName,
                                           (int)r.Country);
    if (!pTab)
    {
        BSMessageBoxPrintf("can't find %s from _RefShopTab. check _RefShopGoods",
                           r.RefTabCodeName.c_str());
        return 0;
    }
    DWORD dwTabID = pTab->ID;
    RefPackageItem* pPackageItem = RefData_FindPackageItem(
        RefShop_GetReferenceData(), r.RefPackageItemCodeName, (int)r.Country);
    if (!pPackageItem)
    {
        BSMessageBoxPrintf("can't find %s from _RefPackageItem. check _RefShopGoods",
                           r.RefPackageItemCodeName.c_str());
        return 0;
    }
    RefShop_ReadParams(row, r.aParam);
    r.m_pRefPackageItem = pPackageItem;
    for (std::vector<RefScrapOfPackageItem*>::iterator it =
             pPackageItem->m_vecAssignedScrapOfPackageItems.begin();
         it != pPackageItem->m_vecAssignedScrapOfPackageItems.end(); ++it)
    {
        RefObjCommonRow* pRefItem = (*it)->m_pRefObjItem;
        if ((!pRefItem || !(RefShop_GetObjAvailableOPs(pRefItem) & 2)) &&
            !BSAssertDialog(3400, s_szReferenceDataCpp,
                            "pRefItem != NULL && (pRefItem->m_dwAvailableOPs & "
                            "OBJ_INTERACT_FLAG_CAN_SELL)"))
            DebugBreak();
        WORD wTID = RefObj_GetTypeID(pRefItem);
        if ((wTID & 2) == 0 && (wTID & 0x1C) == 0xC && (wTID & 1) &&
            pTab->CodeName128.find("MALL_", 0, 5) == std::string::npos)
            BSAssertFail();
        if (RefShop_IsSpecialty(RefObj_GetTypeID(pRefItem)))
            RefShop_GetReferenceData()->m_set22C.insert(RefShop_GetObjID(pRefItem));
    }
    return dwTabID;
}

// RefShopTab::Read (0x495a10): the tab ID; the tab group must exist.
static DWORD RefRow_Read(CRefRow& row, RefShopTab& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    row >> r.Country >> r.ID >> r.CodeName128 >> r.RefTabGroupCodeName;
    r.RefTabGroupID = 0;
    row.Skip();
    RefShopTabGroup* pTabGroup = RefData_FindShopTabGroup(RefShop_GetReferenceData(),
                                                          r.RefTabGroupCodeName, (int)r.Country);
    if (!pTabGroup)
    {
        BSMessageBoxPrintf("can't find %s from _RefShopTabGroup. check _RefShopTab",
                           r.RefTabGroupCodeName.c_str());
        return 0;
    }
    r.RefTabGroupID = pTabGroup->ID;
    return r.ID;
}

// RefShopTabGroup::Read (0x495b10): the shop ID.
static DWORD RefRow_Read(CRefRow& row, RefShop& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    row >> r.Country >> r.ID >> r.CodeName128;
    RefShop_ReadParams(row, r.aParam);
    return r.ID;
}

// RefShopGroup::Read (0x495c40): the shop group ID; the NPC must exist ("xxx": none).
static DWORD RefRow_Read(CRefRow& row, RefShopGroup& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    std::string strNPC;
    row >> r.Country >> r.ID >> r.CodeName128 >> strNPC;
    if (strNPC == s_szNone)
    {
        r.RefNPCID = 0;
    }
    else
    {
        RefObjCommonRow* pNPC = RefShop_GetReferenceData()->FindRefObjByCodeName(strNPC);
        if (!pNPC)
        {
            BSMessageBoxPrintf("can't find assoc NPC with this shop [shopGroup: %s, NPC: %s]",
                               r.CodeName128.c_str(), strNPC.c_str());
            if (!BSAssertDialog(3539, s_szReferenceDataCpp, "FALSE"))
                DebugBreak();
            return 0;
        }
        r.RefNPCID = RefShop_GetObjID(pNPC);
    }
    RefShop_ReadParams(row, r.aParam);
    return r.ID;
}

// RefMappingShopGroup::Read (0x495e80): the shop group ID.
static DWORD RefRow_Read(CRefRow& row, RefMappingShopGroup& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    row >> r.Country >> r.RefShopGroupCodeName >> r.RefShopCodeName;
    RefShopGroup* pShopGroup = RefData_FindShopGroup(RefShop_GetReferenceData(),
                                                     r.RefShopGroupCodeName, (int)r.Country);
    if (!pShopGroup)
    {
        BSMessageBoxPrintf("can't find %s from _RefShopGroup. check _RefMappingShopGroup",
                           r.RefShopGroupCodeName.c_str());
        return 0;
    }
    r.RefShopGroupID = pShopGroup->ID;
    RefShop* pShop = RefData_FindShop(RefShop_GetReferenceData(), r.RefShopCodeName,
                                      (int)r.Country);
    if (!pShop)
    {
        BSMessageBoxPrintf("can't find %s from _RefShop. check _RefMappingShopGroup",
                           r.RefShopCodeName.c_str());
        return 0;
    }
    r.RefShopID = pShop->ID;
    return r.RefShopGroupID;
}

// RefMappingShopWithTab::Read (0x495f90): the shop ID.
static DWORD RefRow_Read(CRefRow& row, RefMappingShopWithTab& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    row >> r.Country >> r.RefShopCodeName >> r.RefTabGroupCodeName;
    RefShop* pShop = RefData_FindShop(RefShop_GetReferenceData(), r.RefShopCodeName,
                                      (int)r.Country);
    if (!pShop)
    {
        BSMessageBoxPrintf("can't find %s from _RefShop. check RefMappingShopWithTab",
                           r.RefShopCodeName.c_str());
        return 0;
    }
    r.RefShopID = pShop->ID;
    RefShopTabGroup* pTabGroup = RefData_FindShopTabGroup(
        RefShop_GetReferenceData(), r.RefTabGroupCodeName, (int)r.Country);
    if (!pTabGroup)
    {
        BSMessageBoxPrintf("can't find %s from _RefShopTabGroup. check _RefMappingShopWithTab",
                           r.RefTabGroupCodeName.c_str());
        return 0;
    }
    r.RefTabGroupID = pTabGroup->ID;
    return r.RefShopID;
}

// RefShopItemStockPeriod::Read (0x4960a0): the ID; a type other than 0 / 1 drops the row.
static DWORD RefRow_Read(CRefRow& row, RefShopItemStockPeriod& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    row >> r.dw04 >> r.ID >> r.str0C >> r.str28 >> r.ts44 >> r.ts54 >> r.b64;
    if (r.b64 != 0 && r.b64 != 1)
        return 0;
    return r.ID;
}

// ---------------------------------------------------------------------------------------------
// Loaders (one row each)

// A row keyed by its ID in a map and by country and code name in an index (the case 0 parts of
// the loaders): a rejected row is freed; an ID that is already there frees the row and fails.
template <class T, class TMap, class TIndex>
static int RefShop_AddIndexedRow(TMap& map, TIndex& index, T* pRow, DWORD dwKey)
{
    if (!dwKey)
    {
        delete pRow;
        return 1;
    }
    if (RefShop_FindValue(map, dwKey))
    {
        delete pRow;
        BSAssertFail();
        return 0;
    }
    map.insert(std::make_pair(dwKey, pRow));
    RefShop_IndexByCodeName(index, pRow);
    return 1;
}

// The loaders get the row and the table number LoadShopGroup gives the table.
typedef int (*PFN_SHOP_LOAD)(CReferenceData* pData, CDBRecStream* pRecord, int nTable);

// CRefInstanceGenerator::LoadPackageItemTables (0x4a31a0).
static int RefShop_LoadPackageItemTables(CReferenceData* pData, CDBRecStream* pRecord,
                                         int nTable)
{
    CRefRow row(pRecord);
    switch (nTable)
    {
    case 0:                                                     // _RefPackageItem
        {
            RefPackageItem* pRow = new RefPackageItem;
            DWORD dwKey = RefRow_Read(row, *pRow);
            return RefShop_AddIndexedRow(pData->m_mapPackageItem,
                                         pData->m_mapPackageItemByCodeName, pRow, dwKey);
        }
    case 1:                                                     // _RefScrapOfPackageItem
        {
            RefScrapOfPackageItem* pRow = new RefScrapOfPackageItem;
            DWORD dwKey = RefRow_Read(row, *pRow);
            if (!dwKey)
            {
                delete pRow;
                return 1;
            }
            RefPackageItem* pPackageItem = RefShop_FindValue(pData->m_mapPackageItem, dwKey);
            if (!pPackageItem)
                BSAssertFail();
            pPackageItem->AddScrapItem(pRow);
            return 1;
        }
    case 2:                                                     // _RefPricePolicyOfItem
        {
            // BUG(SM-021): the price policy row is never stored nor freed.
            RefPricePolicyOfItem* pRow = new RefPricePolicyOfItem;
            DWORD dwKey = RefRow_Read(row, *pRow);
            if (!dwKey)
                return 1;
            RefPackageItem* pPackageItem = RefShop_FindValue(pData->m_mapPackageItem, dwKey);
            if (!pPackageItem)
                BSAssertFail();
            pPackageItem->ValidateCost(*pRow);
            return 1;
        }
    case 3:                                                     // _RefRewardPolicyToSell...
        {
            RefRewardPolicyToSellPackageItem* pRow = new RefRewardPolicyToSellPackageItem;
            DWORD dwKey = RefRow_ReadRewardPolicyToSell(row, *pRow);
            if (!dwKey)
            {
                delete pRow;
                return 1;
            }
            RefPackageItem* pPackageItem = RefShop_FindValue(pData->m_mapPackageItem, dwKey);
            if (!pPackageItem)
                BSAssertFail();
            pPackageItem->m_vecRewardPolicyToSell.push_back(pRow);
            return 1;
        }
    case 4:                                                     // _RefConditionToSell...
        {
            RefConditionToSellPackageItem* pRow = new RefConditionToSellPackageItem;
            DWORD dwKey = RefRow_ReadConditionToSell(row, *pRow);
            if (!dwKey)
            {
                delete pRow;
                return 1;
            }
            RefPackageItem* pPackageItem = RefShop_FindValue(pData->m_mapPackageItem, dwKey);
            if (!pPackageItem)
                BSAssertFail();
            pPackageItem->m_vecConditionToSell.push_back(pRow);
            return 1;
        }
    }
    BSAssertFail();
    return 0;
}

// CRefInstanceGenerator::LoadShopObject (0x4a3fd0): _RefShopTabGroup.
static int RefShop_LoadShopTabGroup(CReferenceData* pData, CDBRecStream* pRecord, int)
{
    CRefRow row(pRecord);
    RefShopTabGroup* pRow = new RefShopTabGroup;
    DWORD dwKey = RefRow_Read(row, *pRow);
    return RefShop_AddIndexedRow(pData->m_mapShopTabGroup, pData->m_mapShopTabGroupByCodeName,
                                 pRow, dwKey);
}

// RefData_AddRowRefShopSlot36 (0x4d1c80) / RefData_AddRowRefShopItemStockPeriod (0x4d1d60): a
// rejected row is freed; an ID that is already there frees the row and fails.
template <class T, class TMap, TMap CReferenceData::*pMap>
static int RefShop_AddRowUnique(CReferenceData* pData, CDBRecStream* pRecord, int)
{
    CRefRow row(pRecord);
    T* pRow = new T;
    DWORD dwKey = RefRow_Read(row, *pRow);
    if (!dwKey)
    {
        delete pRow;
        return 1;
    }
    if (RefShop_FindValue(pData->*pMap, dwKey))
    {
        delete pRow;
        BSAssertFail();
        return 0;
    }
    (pData->*pMap).insert(std::make_pair(dwKey, pRow));
    return 1;
}

// CRefInstanceGenerator::LoadShopTabAndGoods (0x4a3940).
static int RefShop_LoadShopTabAndGoods(CReferenceData* pData, CDBRecStream* pRecord, int nTable)
{
    CRefRow row(pRecord);
    if (nTable == 0)                                            // _RefShopTab
    {
        RefShopTab* pRow = new RefShopTab;
        DWORD dwKey = RefRow_Read(row, *pRow);
        if (!RefShop_AddIndexedRow(pData->m_mapShopTab, pData->m_mapShopTabByCodeName, pRow,
                                   dwKey))
            return 0;
        if (!dwKey)
            return 1;
        std::map<std::string, RefShopTabGroup*>* pByCodeName =
            RefShop_GetCodeNameMap(pData->m_mapShopTabGroupByCodeName, (int)pRow->Country);
        std::map<std::string, RefShopTabGroup*>::iterator it =
            pByCodeName->find(pRow->RefTabGroupCodeName);
        if (it == pByCodeName->end())
            BSAssertFail();
        RefShopTabGroup_AddShopTab(it->second, pRow);
        return 1;
    }
    if (nTable == 1)                                            // _RefShopGoods
    {
        RefShopGoods* pRow = new RefShopGoods;
        DWORD dwKey = RefRow_Read(row, *pRow);
        if (!dwKey)
        {
            delete pRow;
            return 1;
        }
        RefShopTab* pTab = RefShop_FindValue(pData->m_mapShopTab, dwKey);
        if (!pTab)
            BSAssertFail();
        // BUG(SM-022): the tab does not own its goods; they are never freed.
        pTab->AssignGoods(pRow);
        return 1;
    }
    BSAssertFail();
    return 0;
}

// CRefInstanceGenerator::LoadShopTables (0x4a3580).
static int RefShop_LoadShopTables(CReferenceData* pData, CDBRecStream* pRecord, int nTable)
{
    CRefRow row(pRecord);
    switch (nTable)
    {
    case 0:                                                     // _RefShop
        {
            RefShop* pRow = new RefShop;
            DWORD dwKey = RefRow_Read(row, *pRow);
            return RefShop_AddIndexedRow(pData->m_mapShop, pData->m_mapShopByCodeName, pRow,
                                         dwKey);
        }
    case 1:                                                     // _RefMappingShopWithTab
        {
            RefMappingShopWithTab mapping;
            DWORD dwKey = RefRow_Read(row, mapping);
            if (!dwKey)
                return 1;
            RefShop* pShop = RefShop_FindValue(pData->m_mapShop, dwKey);
            if (!pShop)
                BSAssertFail();
            RefShopTabGroup* pTabGroup = RefShop_FindValue(pData->m_mapShopTabGroup,
                                                           mapping.RefTabGroupID);
            if (!pTabGroup)
                BSAssertFail();
            RefShop_AddTabGroup(pTabGroup, pShop);
            return 1;
        }
    case 2:                                                     // _RefAccessPermissionOfShop
        {
            RefAccessPermissionOfShop* pRow = new RefAccessPermissionOfShop;
            DWORD dwKey = RefRow_ReadAccessPermission(row, *pRow);
            if (!dwKey)
            {
                delete pRow;
                return 1;
            }
            RefShop* pShop = RefShop_FindValue(pData->m_mapShop, dwKey);
            if (!pShop)
                BSAssertFail();
            pShop->m_vecAccessPermission.push_back(pRow);
            return 1;
        }
    case 3:                                                     // _RefTreatItemOfShop
        {
            RefTreatItemOfShop* pRow = new RefTreatItemOfShop;
            DWORD dwKey = RefRow_Read(row, *pRow);
            if (!dwKey)
            {
                delete pRow;
                return 1;
            }
            RefShop* pShop = RefShop_FindValue(pData->m_mapShop, dwKey);
            if (!pShop)
                BSAssertFail();
            pShop->AddTreatItem(pRow);
            return 1;
        }
    }
    BSAssertFail();
    return 0;
}

// CRefInstanceGenerator::LoadShopGroupTables (0x4a3d10).
static int RefShop_LoadShopGroupTables(CReferenceData* pData, CDBRecStream* pRecord, int nTable)
{
    CRefRow row(pRecord);
    if (nTable == 0)                                            // _RefShopGroup
    {
        RefShopGroup* pRow = new RefShopGroup;
        DWORD dwKey = RefRow_Read(row, *pRow);
        return RefShop_AddIndexedRow(pData->m_mapShopGroup, pData->m_mapShopGroupByCodeName,
                                     pRow, dwKey);
    }
    if (nTable == 1)                                            // _RefMappingShopGroup
    {
        RefMappingShopGroup mapping;
        DWORD dwKey = RefRow_Read(row, mapping);
        if (!dwKey)
            return 1;
        RefShopGroup* pShopGroup = RefShop_FindValue(pData->m_mapShopGroup, dwKey);
        if (!pShopGroup)
            BSAssertFail();
        RefShop* pShop = RefShop_FindValue(pData->m_mapShop, mapping.RefShopID);
        if (!pShop)
            BSAssertFail();
        pShopGroup->m_vecRefShops.push_back(pShop);
        return 1;
    }
    BSAssertFail();
    return 0;
}

// CRefInstanceGenerator::LoadConditionToBuyScrapItem (0x4a4680) and the three loaders like it
// (0x4a4740, 0x4a4800, 0x4a48c0): rows in service by their key.
template <CReferenceData::RefScrapItemRuleMap CReferenceData::*pMap>
static int RefShop_LoadScrapItemRule(CReferenceData* pData, CDBRecStream* pRecord, int)
{
    CRefRow row(pRecord);
    RefScrapItemRule* pRow = new RefScrapItemRule;
    unsigned __int64 nKey = RefRow_Read(row, *pRow);
    if (nKey)
        (pData->*pMap).insert(std::make_pair(nKey, pRow));
    else
        delete pRow;
    return 1;
}

// The builders of the shop tables (RefData_BuildRefShopPackageTables 0x4ce910 ...): FALSE for
// an empty table; a row the loader fails asserts and stops the build (FALSE).
static int RefShop_BuildTable(CReferenceData* pData, RefTableEntry* pTable, PFN_SHOP_LOAD pfnLoad,
                              int nTable)
{
    if (pTable->m_listRows.size() == 0)
        return 0;
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        if (!pfnLoad(pData, static_cast<CDBRecStream*>(*it), nTable))
        {
            BSAssertFail();
            return 0;
        }
    }
    return 1;
}

// CRefInstanceGenerator::ValidatePackageItemPrices (0x4a99e0).
static void RefShop_ValidatePackageItemPrices(CReferenceData* pData)
{
    for (CReferenceData::RefPackageItemMap::iterator it = pData->m_mapPackageItem.begin();
         it != pData->m_mapPackageItem.end(); ++it)
        it->second->ValidatePrice();
}

int RefData_BuildShopTables(CReferenceData* pData, RefTableEntry*, unsigned int)
{
    RefTableVector& vecTables = *pData->m_pTables;
    RefTableEntry* pPackageItem = vecTables.at(0x17);
    RefTableEntry* pRewardPolicyToSellScrapItem = vecTables.at(0x1E);
    RefTableEntry* pRewardPolicyToBuyScrapItem = vecTables.at(0x1C);
    RefTableEntry* pConditionToSellPackageItem = vecTables.at(0x20);
    RefTableEntry* pConditionToBuyScrapItem = vecTables.at(0x1D);
    RefTableEntry* pRewardPolicyToSellPackageItem = vecTables.at(0x1B);
    RefTableEntry* pConditionToSellScrapItem = vecTables.at(0x1F);
    RefTableEntry* pScrapOfPackageItem = vecTables.at(0x16);
    RefTableEntry* pMappingShopWithTab = vecTables.at(0x15);
    RefTableEntry* pShopTab = vecTables.at(0x26);
    RefTableEntry* pShopGoods = vecTables.at(0x25);
    RefTableEntry* pShop = vecTables.at(0x27);
    RefTableEntry* pPricePolicyOfItem = vecTables.at(0x18);
    RefTableEntry* pAccessPermissionOfShop = vecTables.at(0x19);
    RefTableEntry* pTreatItemOfShop = vecTables.at(0x1A);
    RefTableEntry* pShopGroup = vecTables.at(0x21);
    RefTableEntry* pMappingShopGroup = vecTables.at(0x22);
    RefTableEntry* pShopTabGroup = vecTables.at(0x23);
    RefTableEntry* pShopSlot36 = vecTables.at(0x24);
    RefTableEntry* pShopItemStockPeriod = vecTables.at(0x5B);

    if (!RefShop_BuildTable(pData, pPackageItem, RefShop_LoadPackageItemTables, 0) ||
        !RefShop_BuildTable(pData, pScrapOfPackageItem, RefShop_LoadPackageItemTables, 1) ||
        !RefShop_BuildTable(pData, pPricePolicyOfItem, RefShop_LoadPackageItemTables, 2) ||
        !RefShop_BuildTable(pData, pRewardPolicyToSellPackageItem,
                            RefShop_LoadPackageItemTables, 3) ||
        !RefShop_BuildTable(pData, pConditionToSellPackageItem,
                            RefShop_LoadPackageItemTables, 4) ||
        !RefShop_BuildTable(pData, pShopTabGroup, RefShop_LoadShopTabGroup, 0))
        return 0;
    // Slot 0x24 is never registered: its table is only read when it is there.
    if (pShopSlot36 &&
        !RefShop_BuildTable(pData, pShopSlot36,
                            RefShop_AddRowUnique<RefShopSlot36Row,
                                                 CReferenceData::RefShopSlot36Map,
                                                 &CReferenceData::m_mapShopSlot36>, 0))
        return 0;
    if (!RefShop_BuildTable(pData, pShopTab, RefShop_LoadShopTabAndGoods, 0) ||
        !RefShop_BuildTable(pData, pShopGoods, RefShop_LoadShopTabAndGoods, 1) ||
        !RefShop_BuildTable(pData, pShop, RefShop_LoadShopTables, 0) ||
        !RefShop_BuildTable(pData, pShopGroup, RefShop_LoadShopGroupTables, 0) ||
        !RefShop_BuildTable(pData, pMappingShopGroup, RefShop_LoadShopGroupTables, 1) ||
        !RefShop_BuildTable(pData, pMappingShopWithTab, RefShop_LoadShopTables, 1) ||
        !RefShop_BuildTable(pData, pAccessPermissionOfShop, RefShop_LoadShopTables, 2) ||
        !RefShop_BuildTable(pData, pTreatItemOfShop, RefShop_LoadShopTables, 3) ||
        !RefShop_BuildTable(pData, pConditionToBuyScrapItem,
                            RefShop_LoadScrapItemRule<
                                &CReferenceData::m_mapConditionToBuyScrapItem>, 0) ||
        !RefShop_BuildTable(pData, pConditionToSellScrapItem,
                            RefShop_LoadScrapItemRule<
                                &CReferenceData::m_mapConditionToSellScrapItem>, 0) ||
        !RefShop_BuildTable(pData, pRewardPolicyToSellScrapItem,
                            RefShop_LoadScrapItemRule<
                                &CReferenceData::m_mapRewardPolicyToSellScrapItem>, 0) ||
        !RefShop_BuildTable(pData, pRewardPolicyToBuyScrapItem,
                            RefShop_LoadScrapItemRule<
                                &CReferenceData::m_mapRewardPolicyToBuyScrapItem>, 0))
        return 0;
    // The stock periods are optional: an empty table is skipped.
    if (pShopItemStockPeriod->m_listRows.size() &&
        !RefShop_BuildTable(pData, pShopItemStockPeriod,
                            RefShop_AddRowUnique<RefShopItemStockPeriod,
                                                 CReferenceData::RefShopItemStockPeriodMap,
                                                 &CReferenceData::m_mapShopItemStockPeriod>, 0))
        return 0;
    RefShop_ValidatePackageItemPrices(pData);
    return 1;
}

int RefData_BuildWithShopTables(CReferenceData*, RefTableEntry*, unsigned int)
{
    return 1;
}
