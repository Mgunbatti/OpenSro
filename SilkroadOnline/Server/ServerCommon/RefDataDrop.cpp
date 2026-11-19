#include "ServerCommon/ReferenceData.h"

#include <string.h>

#include "BSLib/BSDebug.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerFramework/ServerDataExtern.h"

// ---------------------------------------------------------------------------------------------
// Object fields used here

static int RefObj_GetID(const RefObjCommonRow* pRefObj)                           // +0x8
{
    return *(const int*)((const BYTE*)pRefObj + 0x8);
}

static BYTE RefObj_GetCountry(const RefObjCommonRow* pRefObj)                     // +0x88
{
    return *((const BYTE*)pRefObj + 0x88);
}

static BYTE RefObj_GetRarity(const RefObjCommonRow* pRefObj)                      // +0x89
{
    return *((const BYTE*)pRefObj + 0x89);
}

static BYTE RefObj_GetDropClassIndex(const RefObjCommonRow* pRefObj)              // +0x1A8
{
    return *((const BYTE*)pRefObj + 0x1A8);
}

// TID1 3 (item), not a cash item (bit 1).
static BOOL RefDrop_IsItem(WORD wTypeID)
{
    return (wTypeID & 2) == 0 && (wTypeID & 0x1C) == 0xC;
}

// ... and TID2 1 (equipment).
static BOOL RefDrop_IsEquip(WORD wTypeID)
{
    return RefDrop_IsItem(wTypeID) && (wTypeID & 0x60) == 0x20;
}

// ... and TID2 3 (expendable) with the given TID3 (bits 7-10).
static BOOL RefDrop_IsExpendable(WORD wTypeID, WORD wTID3)
{
    return RefDrop_IsItem(wTypeID) && (wTypeID & 0x60) == 0x60 && (wTypeID & 0x780) == wTID3;
}

// RefTypeID_Is_3_3_11_<n> (0x51f2c0 / 0x51f300 / 0x51dd20 / 0x51dd60).
static BOOL RefDrop_IsAlchemy(WORD wTypeID, WORD wTID4)
{
    return RefDrop_IsExpendable(wTypeID, 0x580) && (wTypeID & 0xF800) == wTID4;
}

// ---------------------------------------------------------------------------------------------
// Common builder parts

// One row of a drop table; FALSE fails the build.
typedef int (*PFN_DROP_ADD_ROW)(CReferenceData* pData, CRefRow& row, unsigned int nSlot);

// RefData_BuildRef<Table> of the drop tables (0x4cf5d0, 0x4cf640, 0x4cf6b0, 0x4d0290,
// 0x4d0300, 0x4d01c0): FALSE for an empty table; a row that fails asserts and stops the build.
template <PFN_DROP_ADD_ROW pfnAddRow>
static int RefDrop_BuildRows(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot)
{
    if (pTable->m_listRows.size() == 0)
        return 0;
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        CRefRow row(static_cast<CDBRecStream*>(*it));
        if (!pfnAddRow(pData, row, nSlot))
        {
            BSAssertFail();
            return 0;
        }
    }
    return 1;
}

// RefData_AddRowRefDrop* (0x4d27e0, 0x4d2840, 0x4d2910): a row with key 0 is freed; a key that
// is already in the map frees the row and fails the build.
template <class TMap>
static int RefDrop_InsertUnique(TMap& map, typename TMap::mapped_type pRow, DWORD dwKey)
{
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

// ---------------------------------------------------------------------------------------------
// _RefDropGold, _RefDropOptLevelSel, _RefDropClassSel_*

// RefDropGold::Read (0x4961e0): the level.
static DWORD RefDropGold_Read(CRefRow& row, RefDropGold& r)
{
    BYTE bLevel;
    float fProb;
    row >> bLevel >> fProb;
    r.Prob = (DWORD)(__int64)(fProb * 100.0);
    row >> r.GoldMin >> r.GoldMax;
    return bLevel;
}

// RefData_AddRowRefDropGold (0x4d27e0).
static int RefDrop_AddDropGold(CReferenceData* pData, CRefRow& row, unsigned int)
{
    RefDropGold* pRow = new RefDropGold;
    DWORD dwKey = RefDropGold_Read(row, *pRow);
    return RefDrop_InsertUnique(pData->m_MagicOpt.m_mapDropGold, pRow, dwKey);
}

// RefDropOptLevelSel::Read (0x4962b0): the level column + 1, which is also stored.
static DWORD RefDropOptLevelSel_Read(CRefRow& row, RefDropOptLevelSel& r)
{
    float fProb;
    row >> r.Level >> fProb >> r.dw08;
    ++r.Level;
    r.Prob = (DWORD)(__int64)(fProb * 1000000.0);
    return r.Level;
}

// RefData_AddRowRefDropOptLevelSel (0x4d2840).
static int RefDrop_AddDropOptLevelSel(CReferenceData* pData, CRefRow& row, unsigned int)
{
    RefDropOptLevelSel* pRow = new RefDropOptLevelSel;
    DWORD dwKey = RefDropOptLevelSel_Read(row, *pRow);
    return RefDrop_InsertUnique(pData->m_MagicOpt.m_mapDropOptLevelSel, pRow, dwKey);
}

// RefDropClassSel::Read (0x496360): the ID column, then nCount probabilities. Each one above
// 0.000001 is added to a running total (a float); the total * 1000000 is the key of the class
// index.
static DWORD RefDropClassSel_Read(CRefRow& row, RefDropClassSel& r, unsigned int nCount)
{
    DWORD dwID;
    row >> dwID;
    float fTotal = 0.0f;
    for (unsigned int i = 0; i < nCount; ++i)
    {
        float fProb;
        row >> fProb;
        if (fProb > 0.000001f)
        {
            fTotal = fProb + fTotal;
            DWORD dwKey = (DWORD)(__int64)(fTotal * 1000000.0);
            if (r.find(dwKey) != r.end())
                BSAssertFail();
            r.insert(RefDropClassSel::value_type(dwKey, i));
        }
    }
    return dwID;
}

// The map and class count of the slots 0x2A..0x33 (the cases of BuildFromTable); map 7 is
// never filled.
static const struct
{
    BYTE    bMap;
    BYTE    bCount;
} s_aDropClassSel[10] =
{
    { 0, 36 },      // 0x2A _RefDropClassSel_Equip
    { 1, 36 },      // 0x2B _RefDropClassSel_RareEquip
    { 2, 7 },       // 0x2C _RefDropClassSel_Recover
    { 3, 14 },      // 0x2D _RefDropClassSel_Cure
    { 4, 6 },       // 0x2E _RefDropClassSel_Ammo
    { 5, 3 },       // 0x2F _RefDropClassSel_Scroll
    { 6, 2 },       // 0x30 _RefDropClassSel_Reinforce
    { 10, 12 },     // 0x31 _RefDropClassSel_AlchemyTablet
    { 8, 12 },      // 0x32 _RefDropClassSel_AlchemyMagicStone
    { 9, 12 },      // 0x33 _RefDropClassSel_AlchemyATTRStone
};

// RefData_AddRowRefDropClassSel (0x4d2910).
static int RefDrop_AddDropClassSel(CReferenceData* pData, CRefRow& row, unsigned int nSlot)
{
    unsigned int nIndex = nSlot - 0x2A;
    RefDropClassSel* pRow = new RefDropClassSel;
    DWORD dwKey = RefDropClassSel_Read(row, *pRow, s_aDropClassSel[nIndex].bCount);
    RefMagicOptTables::DropClassSelMap& map =
        pData->m_MagicOpt.m_amapDropClassSel[s_aDropClassSel[nIndex].bMap];
    return RefDrop_InsertUnique(map, pRow, dwKey);
}

int RefData_BuildDropGold(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot)
{
    return RefDrop_BuildRows<RefDrop_AddDropGold>(pData, pTable, nSlot);
}

int RefData_BuildDropOptLevelSel(CReferenceData* pData, RefTableEntry* pTable,
                                 unsigned int nSlot)
{
    return RefDrop_BuildRows<RefDrop_AddDropOptLevelSel>(pData, pTable, nSlot);
}

int RefData_BuildDropClassSel(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot)
{
    return RefDrop_BuildRows<RefDrop_AddDropClassSel>(pData, pTable, nSlot);
}

// ---------------------------------------------------------------------------------------------
// _RefDropItemAssign: the drop class tables

enum
{
    REF_DROP_GROUP_COUNT = 2,               // bits of the first column
    REF_DROP_CLASS_PER_GROUP = 11,
};

// The class indexes of each drop class (BuildDropClassTables); class 1 (rare equipment) is
// marked 3, the others 1.
static const BYTE s_abDropClassCount[REF_DROP_CLASS_PER_GROUP] =
{
    36, 36, 7, 14, 6, 3, 2, 12, 12, 12, 12,
};

// RefDropItemAssign::Read (0x4964a0): the group bits; 0 for a row without one or with an
// unknown item (logged). nClass is the class column - 1 (the item's default class when the
// column is negative).
static int RefDropItemAssign_Read(CRefRow& row, RefDropItemAssign& r, int& nClass)
{
    int nGroupBits;
    row >> nGroupBits;
    if (!nGroupBits)
        return 0;
    int nRefObjID;
    row >> nRefObjID;
    r.pItem = CReferenceData::GetInstance()->FindRefObj(nRefObjID);
    if (!r.pItem)
    {
        LogPrintf(0x2000000, "this item is to drop but can't find in _RefObjCommon. check it out "
                             "its existance or service on/off [RefID: %d]", nRefObjID);
        return 0;
    }
    row >> r.Weight >> r.dw08 >> nClass;
    if (nClass < 0)
        nClass = RefObj_GetDropClassIndex(r.pItem);
    if (nClass <= 0)
        BSAssertFail();
    --nClass;
    row >> r.dw0C;
    return nGroupBits;
}

// DropClassEntry_AddItem (0x51de70): adds the weight; the item goes into the map by the new
// total (a weight of 0 keeps the item that is already there). Equipment is also listed by
// TypeID.
static void RefDropClassEntry_AddItem(RefDropClassEntry& entry, const RefDropItemAssign& assign)
{
    entry.dwCount += assign.Weight;
    entry.mapProb.insert(std::make_pair(entry.dwCount, assign));
    WORD wTypeID = RefObj_GetTypeID(assign.pItem);
    if (!RefDrop_IsEquip(wTypeID))
        return;
    DWORD dwKey = wTypeID;
    std::map<DWORD, std::vector<RefDropItemAssign*> >::iterator it = entry.mapItem.find(dwKey);
    if (it == entry.mapItem.end())
    {
        entry.mapItem.insert(std::make_pair(dwKey, std::vector<RefDropItemAssign*>()));
        it = entry.mapItem.find(dwKey);
    }
    if (it == entry.mapItem.end())
        BSAssertFail();
    std::map<DWORD, RefDropItemAssign>::iterator itProb = entry.mapProb.find(entry.dwCount);
    it->second.push_back(&itProb->second);
}

// DropClassTable_AddItem (0x51dda0): the entry of the class index in the table of the item's
// country, or in all three for country 3.
static void RefDropClassSlot_AddItem(RefDropClassSlot& slot, unsigned int nClass,
                                     const RefDropItemAssign& assign)
{
    if (RefObj_GetCountry(assign.pItem) > 3)
        BSAssertFail();
    BYTE bCountry = RefObj_GetCountry(assign.pItem);
    if (bCountry == 3)
    {
        for (int i = 0; i < 3; ++i)
            RefDropClassEntry_AddItem(slot.aEntry[i].at(nClass), assign);
        return;
    }
    // BUG(SM-032): a country above 3 indexes past the three tables of the slot.
    RefDropClassEntry_AddItem(slot.aEntry[bCountry].at(nClass), assign);
}

// The drop class of an item (0..10), -1 for an item no drop class takes.
static int RefDrop_GetDropClass(const RefObjCommonRow* pItem)
{
    WORD wTypeID = RefObj_GetTypeID(pItem);
    if (RefDrop_IsEquip(wTypeID))
        return RefObj_GetRarity(pItem) == 2 ? 1 : 0;
    if (RefDrop_IsExpendable(wTypeID, 0x80))
        return 2;
    if (RefDrop_IsExpendable(wTypeID, 0x100))
        return 3;
    if (RefDrop_IsExpendable(wTypeID, 0x200))
        return 4;
    if (RefDrop_IsExpendable(wTypeID, 0x180))
        return 5;
    if (RefDrop_IsExpendable(wTypeID, 0x500))
        return 6;
    if (RefDrop_IsExpendable(wTypeID, 0x580))
    {
        if (RefDrop_IsAlchemy(wTypeID, 0x800) || RefDrop_IsAlchemy(wTypeID, 0x3800))
            return 8;
        if (RefDrop_IsAlchemy(wTypeID, 0x1000))
            return 9;
        if (RefDrop_IsAlchemy(wTypeID, 0x1800))
            return 10;
        return 7;
    }
    return -1;
}

// CRefInstanceGenerator::BuildDropClassTables (0x51a050): sizes the 22 drop classes (2 groups
// of 11), then adds every _RefDropItemAssign row to the class of its item in the first group
// whose bit the row has. Always TRUE.
int RefData_BuildDropClassTables(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    RefMagicOptTables& tables = pData->m_MagicOpt;
    for (int nGroup = 0; nGroup < REF_DROP_GROUP_COUNT; ++nGroup)
    {
        for (int nClass = 0; nClass < REF_DROP_CLASS_PER_GROUP; ++nClass)
        {
            RefDropClassSlot& slot =
                tables.m_aDropClass[nGroup * REF_DROP_CLASS_PER_GROUP + nClass];
            for (int i = 0; i < 3; ++i)
                slot.aEntry[i].resize(s_abDropClassCount[nClass], RefDropClassEntry());
            slot.b30 = nClass == 1 ? 3 : 1;
        }
    }

    RefDropItemAssign assign;
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        CRefRow row(static_cast<CDBRecStream*>(*it));
        int nClass = 0;
        int nGroupBits = RefDropItemAssign_Read(row, assign, nClass);
        if (!nGroupBits)
            continue;
        if (nClass < 0)
        {
            BSAssertFail();
            continue;
        }
        if (!assign.pItem)
            BSAssertFail();
        int nGroup;
        for (nGroup = 0; nGroup < REF_DROP_GROUP_COUNT; ++nGroup)
        {
            if (((nGroupBits >> nGroup) & 1) == 0)
                continue;
            int nDropClass = RefDrop_GetDropClass(assign.pItem);
            if (nDropClass >= 0)
            {
                RefDropClassSlot_AddItem(
                    tables.m_aDropClass[nGroup * REF_DROP_CLASS_PER_GROUP + nDropClass],
                    (unsigned int)nClass, assign);
                break;
            }
            LogPrintf(0x2000000, "unknown item of drop assign table!!! [RefID: %d]",
                      RefObj_GetID(assign.pItem));
            BSAssertFail();
        }
        if (nGroup == REF_DROP_GROUP_COUNT)
        {
            LogPrintf(0x2000000, "can't find bound item drop group!!! [RefID: %d]",
                      RefObj_GetID(assign.pItem));
            BSAssertFail();
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------
// _RefDropItemGroup

// RefDropItemGroup::Read (0x4925e0): the ID; 0 for a row out of service, with an unknown item
// or an unknown magic option group (the last two with a message box).
static DWORD RefDropItemGroup_Read(CRefRow& row, RefDropItemGroupRow& r)
{
    BYTE bService;
    row >> bService >> r.ID >> r.CodeName >> r.RefItemID >> r.SelectRatio >> r.RefMagicGroupID;
    if (!bService)
        return 0;
    CReferenceData* pData = CReferenceData::GetInstance();
    if (!pData->FindRefObj(r.RefItemID))
    {
        BSMessageBoxPrintf("Can`t find Item(%d) within _RefObjItem Table", r.RefItemID);
        BSAssertFail();
        return 0;
    }
    if (r.RefMagicGroupID)
    {
        // CRefInstanceGenerator::GetMagicOptGroup (0x4ad9a0) copies the group; only the
        // result is used.
        pData = CReferenceData::GetInstance();
        if (pData->m_mapMagicOptGroup.find(r.RefMagicGroupID) == pData->m_mapMagicOptGroup.end())
        {
            BSMessageBoxPrintf("Can`t find MagicOptGroup(ID:%d) within _RefMagicOptGroup table",
                               r.RefMagicGroupID);
            BSAssertFail();
            return 0;
        }
    }
    return r.ID;
}

// CRefInstanceGenerator::LoadDropItemGroup (0x4a5a20): the rows of a group in table order. An
// unknown item fails the build.
static int RefDrop_LoadDropItemGroup(CReferenceData* pData, CRefRow& row, unsigned int)
{
    RefDropItemGroupRow* pRow = new RefDropItemGroupRow;
    // BUG(SM-030): the result is not checked; a rejected row goes into group 0.
    DWORD dwID = RefDropItemGroup_Read(row, *pRow);
    if (!CReferenceData::GetInstance()->FindRefObj(pRow->RefItemID))
    {
        delete pRow;
        BSAssertFail();
        return 0;
    }
    CReferenceData::RefDropItemGroupMap::iterator it = pData->m_mapDropItemGroup.find(dwID);
    if (it == pData->m_mapDropItemGroup.end())
    {
        std::vector<RefDropItemGroupRow*> vecRow;
        vecRow.push_back(pRow);
        pData->m_mapDropItemGroup.insert(
            CReferenceData::RefDropItemGroupMap::value_type(dwID, vecRow));
    }
    else
    {
        it->second.push_back(pRow);
    }
    return 1;
}

int RefData_BuildDropItemGroup(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot)
{
    return RefDrop_BuildRows<RefDrop_LoadDropItemGroup>(pData, pTable, nSlot);
}

BOOL RefData_GetDropItemGroup(CReferenceData* pData, DWORD dwGroupID,
                              std::vector<RefDropItemGroupRow*>& vecRow)
{
    CReferenceData::RefDropItemGroupMap::iterator it = pData->m_mapDropItemGroup.find(dwGroupID);
    if (it == pData->m_mapDropItemGroup.end())
        return FALSE;
    vecRow = it->second;
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// _RefMonster_AssignedItemRndDrop

// RefMonsterAssignedItemRndDrop::Read (0x4927a0): the monster; 0 for a row out of service,
// with an unknown monster, a minimum above the maximum or an unknown item group (the last
// three with a message box).
static DWORD RefMonsterAssignedItemRndDrop_Read(CRefRow& row, RefMonsterAssignedItemRndDropRow& r)
{
    BYTE bService;
    RefMonsterAssignedItemRndDropEntry& e = r.Entry;
    row >> bService >> r.MonsterID >> e.ItemGroupID >> e.str04 >> e.b20 >> e.DropAmountMin
        >> e.DropAmountMax >> e.f24 >> e.dw28 >> e.dw2C;
    if (!bService)
        return 0;
    if (!CReferenceData::GetInstance()->FindRefObj(r.MonsterID))
    {
        BSMessageBoxPrintf("Can't find Monster(%d) within _RefObjCommon Table", r.MonsterID);
        BSAssertFail();
        return 0;
    }
    if (e.DropAmountMin > e.DropAmountMax)
    {
        BSMessageBoxPrintf("Drop amount min value[%d] is bigger than Drop amount max value[%d].",
                           e.DropAmountMin, e.DropAmountMax);
        BSAssertFail();
        return 0;
    }
    std::vector<RefDropItemGroupRow*> vecGroup;
    if (!RefData_GetDropItemGroup(CReferenceData::GetInstance(), e.ItemGroupID, vecGroup))
    {
        BSMessageBoxPrintf("Can't find ItemGroupID(%d) within _RefMonster_AssignedItemRndDrop "
                           "Table", e.ItemGroupID);
        BSAssertFail();
        return 0;
    }
    return r.MonsterID;
}

// CRefInstanceGenerator::LoadMonsterAssignedItemRndDrop (0x4a58e0): the entry goes to the
// monster, the row into the vector of the object.
static int RefDrop_LoadMonsterAssignedItemRndDrop(CReferenceData* pData, CRefRow& row,
                                                  unsigned int)
{
    RefMonsterAssignedItemRndDropRow* pRow = new RefMonsterAssignedItemRndDropRow;
    DWORD dwMonsterID = RefMonsterAssignedItemRndDrop_Read(row, *pRow);
    // BUG(SM-031): a rejected row (0) is looked up as monster 0 and fails the whole build.
    RefObjCommonRow* pMonster = CReferenceData::GetInstance()->FindRefObj(dwMonsterID);
    if (!pMonster)
    {
        delete pRow;
        BSAssertFail();
        return 0;
    }
    RefObj_GetRndDrop(pMonster).push_back(pRow->Entry);
    pData->m_vecMonsterAssignedItemRndDrop.push_back(pRow);
    return 1;
}

int RefData_BuildMonsterAssignedItemRndDrop(CReferenceData* pData, RefTableEntry* pTable,
                                            unsigned int nSlot)
{
    return RefDrop_BuildRows<RefDrop_LoadMonsterAssignedItemRndDrop>(pData, pTable, nSlot);
}

// ---------------------------------------------------------------------------------------------
// _RefCustomizingReservedItemDropForMonster

// RefCustomizingReservedItemDrop::Read (0x492a10): the object; -1 for a group above 16 (with
// a message box, the five values are then not read).
static DWORD RefCustomizingReservedItemDrop_Read(CRefRow& row,
                                                 RefCustomizingReservedItemDropRow& r)
{
    RefCustomizingReservedItemDropEntry& e = r.Entry;
    row >> r.RefCommonID >> e.b00 >> e.dw04 >> e.GroupID;
    if (e.GroupID > 16)
    {
        BSMessageBoxPrintf("error data that is a CustomizingReservedItemDrop Group value. "
                           "[RefCommonID: %d, GroupID: %d]", r.RefCommonID, e.GroupID);
        return (DWORD)-1;
    }
    for (int i = 0; i < 5; ++i)
        row >> e.adw0C[i];
    return r.RefCommonID;
}

// CRefInstanceGenerator::LoadCustomizingReservedItemDrop (0x4a57d0): the entry goes to the
// object's vector of its group; an unknown object or a value out of range fails the build.
static int RefDrop_LoadCustomizingReservedItemDrop(CReferenceData*, CRefRow& row, unsigned int)
{
    RefCustomizingReservedItemDropRow* pRow = new RefCustomizingReservedItemDropRow;
    memset(pRow, 0, sizeof(*pRow));
    DWORD dwRefObjID = RefCustomizingReservedItemDrop_Read(row, *pRow);
    RefObjCommonRow* pRefObj = CReferenceData::GetInstance()->FindRefObj(dwRefObjID);
    const RefCustomizingReservedItemDropEntry& e = pRow->Entry;
    if (pRefObj && e.b00 < 9 && (int)e.dw04 <= 4 && e.GroupID <= 16 && e.GroupID != 0xFF)
    {
        RefObj_GetCustomizingDrop(pRefObj, e.GroupID).push_back(e);
        delete pRow;
        return 1;
    }
    delete pRow;
    BSAssertFail();
    return 0;
}

int RefData_BuildCustomizingReservedItemDrop(CReferenceData* pData, RefTableEntry* pTable,
                                             unsigned int nSlot)
{
    return RefDrop_BuildRows<RefDrop_LoadCustomizingReservedItemDrop>(pData, pTable, nSlot);
}
