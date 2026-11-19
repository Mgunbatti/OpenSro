#pragma once

#include <windows.h>

#include <map>
#include <string>
#include <vector>

// The drop tables of the reference data (part of ServerCommon\ReferenceData.cpp of the
// original): _RefDropGold, _RefDropOptLevelSel, the ten _RefDropClassSel_* tables,
// _RefDropItemAssign (the drop class tables), _RefDropItemGroup,
// _RefMonster_AssignedItemRndDrop and _RefCustomizingReservedItemDropForMonster.

class CReferenceData;
struct RefTableEntry;
class RefObjCommon;
typedef RefObjCommon RefObjCommonRow;

// ---------------------------------------------------------------------------------------------
// Rows

// _RefDropGold row (12 bytes, RefDropGold::Read 0x4961e0; key: the level column).
struct RefDropGold
{
    DWORD            Prob;                       // +0  probability * 100, truncated
    DWORD            GoldMin;                    // +4
    DWORD            GoldMax;                    // +8
};

C_ASSERT(sizeof(RefDropGold) == 12);

// _RefDropOptLevelSel row (12 bytes, RefDropOptLevelSel::Read 0x4962b0; key: Level, which
// the reader increments).
struct RefDropOptLevelSel
{
    RefDropOptLevelSel()
        : Level(0), Prob(0), dw08(0)
    {
    }

    BYTE             Level;                      // +0  the column + 1
    DWORD            Prob;                       // +4  probability * 1000000, truncated
    DWORD            dw08;                       // +8
};

C_ASSERT(sizeof(RefDropOptLevelSel) == 12);

// _RefDropClassSel_* row (12 bytes, RefData_RecCtor_RefDropClassSel 0x4d3160 / RecDtor
// 0x4dcee0): cumulative probability * 1000000 -> class index, for the classes whose
// probability is above 0.000001.
typedef std::map<DWORD, DWORD> RefDropClassSel;

C_ASSERT(sizeof(RefDropClassSel) == 12);

// The parsed _RefDropItemAssign row (16 bytes, RefDropItemAssign::Read 0x4964a0), kept as the
// value of a drop class entry.
struct RefDropItemAssign
{
    RefObjCommonRow* pItem;                      // +0
    DWORD            Weight;                     // +4  added to the entry's total
    DWORD            dw08;                       // +8
    DWORD            dw0C;                       // +C
};

C_ASSERT(sizeof(RefDropItemAssign) == 16);

// DropClassEntry (28 bytes, ctor 0x51f0d0, copy 0x51bbd0, assignment 0x51ba50): the items of
// one class index. mapProb is keyed by the running total of the weights; mapItem lists the
// equipment by TypeID (pointers into mapProb).
struct RefDropClassEntry
{
    RefDropClassEntry()
        : dwCount(0)
    {
    }

    DWORD                                   dwCount;  // +0   total weight
    std::map<DWORD, RefDropItemAssign>      mapProb;  // +4
    std::map<DWORD, std::vector<RefDropItemAssign*> > mapItem; // +10
};

C_ASSERT(sizeof(RefDropClassEntry) == 0x1C);

// _RefDropItemGroup row (44 bytes, RefDropItemGroup::InitMembers 0x514870).
struct RefDropItemGroupRow
{
    RefDropItemGroupRow()
        : ID(0), RefItemID(0), SelectRatio(0.0f), RefMagicGroupID(0)
    {
    }

    DWORD            ID;                         // +0   key of the group map
    std::string      CodeName;                   // +4
    DWORD            RefItemID;                  // +20
    float            SelectRatio;                // +24
    DWORD            RefMagicGroupID;            // +28
};

C_ASSERT(sizeof(RefDropItemGroupRow) == 0x2C);

// The part of a _RefMonster_AssignedItemRndDrop row a monster keeps (48 bytes,
// RefMonsterAssignedItemRndDropEntry::Init 0x5149c0; the monster's vector at RefObjCommon
// +0x298). Column names are not known.
struct RefMonsterAssignedItemRndDropEntry
{
    RefMonsterAssignedItemRndDropEntry()
        : ItemGroupID(0), b20(0), DropAmountMin(0), DropAmountMax(0), f24(0.0f)
    {
    }

    DWORD            ItemGroupID;                // +0
    std::string      str04;                      // +4
    BYTE             b20;                        // +20
    BYTE             DropAmountMin;              // +21
    BYTE             DropAmountMax;              // +22
    float            f24;                        // +24
    DWORD            dw28;                       // +28  not initialised
    DWORD            dw2C;                       // +2C  not initialised
};

C_ASSERT(sizeof(RefMonsterAssignedItemRndDropEntry) == 0x30);

// _RefMonster_AssignedItemRndDrop row (52 bytes, InitMembers 0x514940).
struct RefMonsterAssignedItemRndDropRow
{
    RefMonsterAssignedItemRndDropRow()
        : MonsterID(0)
    {
    }

    DWORD                               MonsterID;  // +0
    RefMonsterAssignedItemRndDropEntry  Entry;      // +4
};

C_ASSERT(sizeof(RefMonsterAssignedItemRndDropRow) == 0x34);

// The part of a _RefCustomizingReservedItemDropForMonster row a monster keeps (32 bytes; the
// monster's vectors by group at RefObjCommon +0x2A8). Column names are not known.
struct RefCustomizingReservedItemDropEntry
{
    BYTE             b00;                        // +0   below 9
    DWORD            dw04;                       // +4   at most 4
    BYTE             GroupID;                    // +8   0..16
    DWORD            adw0C[5];                   // +C
};

C_ASSERT(sizeof(RefCustomizingReservedItemDropEntry) == 0x20);

// _RefCustomizingReservedItemDropForMonster row (36 bytes, zeroed by
// CRefInstanceGenerator::LoadCustomizingReservedItemDrop 0x4a57d0).
struct RefCustomizingReservedItemDropRow
{
    DWORD                               RefCommonID; // +0
    RefCustomizingReservedItemDropEntry Entry;       // +4
};

C_ASSERT(sizeof(RefCustomizingReservedItemDropRow) == 0x24);

enum
{
    REF_CUSTOMIZING_DROP_GROUP_COUNT = 17,
};

// The drop vectors of a monster in its _RefObjCommon row.
typedef std::vector<RefMonsterAssignedItemRndDropEntry> RefObjRndDropVector;      // +0x298
typedef std::vector<RefCustomizingReservedItemDropEntry> RefObjCustomizingDropVector; // +0x2A8

inline RefObjRndDropVector& RefObj_GetRndDrop(RefObjCommonRow* pRefObj)
{
    return *(RefObjRndDropVector*)((BYTE*)pRefObj + 0x298);
}

inline RefObjCustomizingDropVector& RefObj_GetCustomizingDrop(RefObjCommonRow* pRefObj,
                                                              BYTE bGroup)
{
    return ((RefObjCustomizingDropVector*)((BYTE*)pRefObj + 0x2A8))[bGroup];
}

// ---------------------------------------------------------------------------------------------
// Builders (entries of the BuildFromTable switch, see ReferenceData.cpp)

// RefData_BuildRefDropGold (0x4cf5d0), slot 0x28.
int RefData_BuildDropGold(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);
// RefData_BuildRefDropOptLevelSel (0x4cf640), slot 0x29.
int RefData_BuildDropOptLevelSel(CReferenceData* pData, RefTableEntry* pTable,
                                 unsigned int nSlot);
// RefData_BuildRefDropClassSel (0x4cf6b0), slots 0x2A..0x33.
int RefData_BuildDropClassSel(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);
// CRefInstanceGenerator::BuildDropClassTables (0x51a050), slot 0x34 (_RefDropItemAssign).
int RefData_BuildDropClassTables(CReferenceData* pData, RefTableEntry* pTable,
                                 unsigned int nSlot);
// RefData_BuildRefDropItemGroup (0x4d0290), slot 0x35.
int RefData_BuildDropItemGroup(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);
// RefData_BuildRefMonster_AssignedItemRndDrop (0x4d0300), slot 0x36.
int RefData_BuildMonsterAssignedItemRndDrop(CReferenceData* pData, RefTableEntry* pTable,
                                            unsigned int nSlot);
// RefData_BuildRefCustomizingReservedItemDropForMonster (0x4d01c0), slot 0x57.
int RefData_BuildCustomizingReservedItemDrop(CReferenceData* pData, RefTableEntry* pTable,
                                             unsigned int nSlot);

// CRefInstanceGenerator::GetDropItemGroup (0x4ad930): copies the rows of a drop item group;
// FALSE when there is no such group.
BOOL RefData_GetDropItemGroup(CReferenceData* pData, DWORD dwGroupID,
                              std::vector<RefDropItemGroupRow*>& vecRow);
