#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "ServerCommon/ReferenceData.h"

// The object rows of the reference data (ServerCommon\ReferenceData.cpp of the original):
// _RefObjCommon with the derived parts of _RefObjItem / _RefObjChar / _RefObjStruct, the objects
// built in code (DUMMY_OBJECT, EVENT_DAEMON), the post-processing of slot 0xB, CRefCharGen
// (slot 0x37) and the skill tables (slots 4 and 5).

// ---------------------------------------------------------------------------------------------
// TypeID (WORD at RefObjCommon +0x80): bit 0 CashItem, bit 1 Bionic, bits 2-4 TID1, 5-6 TID2,
// 7-10 TID3, 11-15 TID4.

inline BOOL RefTypeID_Is(WORD w, int nBionic, int nTID1, int nTID2 = -1, int nTID3 = -1,
                         int nTID4 = -1)
{
    if (((w & 2) != 0) != (nBionic != 0))
        return FALSE;
    if ((w & 0x1C) != (nTID1 << 2))
        return FALSE;
    if (nTID2 >= 0 && (w & 0x60) != (nTID2 << 5))
        return FALSE;
    if (nTID3 >= 0 && (w & 0x780) != (nTID3 << 7))
        return FALSE;
    if (nTID4 >= 0 && (w & 0xF800) != (nTID4 << 11))
        return FALSE;
    return TRUE;
}

// The packing of the six TypeID columns (inlined in RefObjCommon_ReadAndCreate 0x4a2cf0 and
// RefObjCommon::vf01 0x48edc0).
inline WORD RefTypeID_Pack(BYTE btCash, BYTE btBionic, BYTE btTID1, BYTE btTID2, BYTE btTID3,
                           BYTE btTID4)
{
    return (WORD)(btCash & 1 | 2 * (btBionic & 1 | 2 * (btTID1 & 7 | 8 * ((btTID4 << 6)
        | btTID2 & 3 | (4 * (btTID3 & 0xF)) & 0x3F))));
}

BOOL RefObjTID_IsItemEtcType13(const WORD& wTypeID);           // 0x4a13c0  item 3/1/13
BOOL RefObj_IsAccessoryType(const WORD& wTypeID);              // 0x4eaa60  item 3/1/5 or 3/1/12
BOOL RefObj_IsSiegeGuard_TID4_1(const WORD& wTypeID);          // 0x4ebc10  char 1/2/4/1
BOOL RefObj_IsSiegeGuard_TID4_3(const WORD& wTypeID);          // 0x4ebb90  char 1/2/4/3
BOOL RefObj_IsSiegeGuard_TID4_4(const WORD& wTypeID);          // 0x4ebbd0  char 1/2/4/4
BOOL RefObj_IsGuildMercenary(const WORD& wTypeID);             // 0x4ebdd0  char 1/2/3/5

// ---------------------------------------------------------------------------------------------
// RefObjCommon (vftable 0x78c814, 0x198 bytes)

// One ReqLevelType / ReqLevel column pair.
struct RefObjReqLevel
{
    int              Type;                       // +0
    BYTE             Level;                      // +4
};

C_ASSERT(sizeof(RefObjReqLevel) == 8);

// The bits of RefObjCommon::Flags.
enum
{
    REFOBJ_CAN_TRADE = 0x1,
    REFOBJ_CAN_SELL = 0x2,
    REFOBJ_CAN_DROP = 0x10,                      // CanDrop bit 0 (bit 1 is 0x4000)
    REFOBJ_CAN_PICK = 0x20,
    REFOBJ_CAN_REPAIR = 0x40,
    REFOBJ_CAN_REVIVE = 0x80,
    REFOBJ_CAN_USE = 0x100,
    REFOBJ_CAN_THROW = 0x200,
    REFOBJ_CAN_BE_VEHICLE = 0x400,               // RefObjChar
    REFOBJ_CAN_CONTROL = 0x800,                  // RefObjChar
    REFOBJ_CAN_BORROW_80 = 0x1000,               // CanBorrow bit 7
    REFOBJ_CAN_BORROW_40 = 0x2000,               // CanBorrow bit 6
};

class RefObjCommon
{
public:
    // RefObjCommon::ctor (0x519440, InitMembers 0x5194e0).
    RefObjCommon();
    // RefObjCommon::dtor (0x4a1a50, DestroyMembers 0x4a1b10; vf00 0x4a1af0 deletes).
    virtual ~RefObjCommon();

    // RefObjCommon::vf01 (0x48edc0): reads the _RefObjCommon row; the ID, 0 for a row out of
    // service. The derived classes read their own table after it.
    virtual int Read(CRefRow& row, int nReserved);

    // RefObjCommon::CopyFrom (0x4ebf60): the data part (+8 on); vptr and +4 are kept.
    void CopyFrom(const RefObjCommon& src);

    WORD GetTypeID() const
    {
        return TypeID;
    }

    DWORD            m_dw04;                     // +4    not initialised
    int              ID;                         // +8
    int              OrgObjID;                   // +C    IndexObjByLinkName
    std::string      CodeName128;                // +10
    std::string      ObjName128;                 // +2C
    std::string      OrgObjCodeName128;          // +48   "xxx" -> empty
    std::string      DescStrID128;               // +64   "xxx" -> empty
    WORD             TypeID;                     // +80
    float            DecayTime;                  // +84   seconds
    BYTE             Country;                    // +88
    BYTE             Rarity;                     // +89
    DWORD            Flags;                      // +8C   REFOBJ_*
    std::set<DWORD>  m_set90;                    // +90   TODO(type) not filled by the readers
    int              Price;                      // +9C
    int              CostBorrow;                 // +A0
    int              CostRepair;                 // +A4
    int              CostRevive;                 // +A8
    int              KeepingFee;                 // +AC
    int              SellPrice;                  // +B0
    RefObjReqLevel   ReqLevel[4];                // +B4
    int              MaxContain;                 // +D4
    RefRegionRow*    pRegion;                    // +D8   RegionID column
    WORD             Dir;                        // +DC
    WORD             OffsetX;                    // +DE
    WORD             OffsetY;                    // +E0
    WORD             OffsetZ;                    // +E2
    WORD             Speed1;                     // +E4
    WORD             Speed2;                     // +E6
    int              Scale;                      // +E8
    WORD             BCHeight;                   // +EC
    WORD             BCRadius;                   // +EE
    int              EventID;                    // +F0
    std::string      AssocFile[5];               // +F4   Obj, Drop, Icon, 1, 2
    int              GP;                         // +180  "GP:<n>" in AssocFile1_128
    int              Link;                       // +184
    DWORD            m_dw188;                    // +188  set by the special monster code list
    DWORD            m_dw18C;                    // +18C  set by the special monster code list
    DWORD            m_dw190;                    // +190
    DWORD            m_dw194;                    // +194
};

C_ASSERT(FIELD_OFFSET(RefObjCommon, CodeName128) == 0x10);
C_ASSERT(FIELD_OFFSET(RefObjCommon, TypeID) == 0x80);
C_ASSERT(FIELD_OFFSET(RefObjCommon, m_set90) == 0x90);
C_ASSERT(FIELD_OFFSET(RefObjCommon, ReqLevel) == 0xB4);
C_ASSERT(FIELD_OFFSET(RefObjCommon, pRegion) == 0xD8);
C_ASSERT(FIELD_OFFSET(RefObjCommon, AssocFile) == 0xF4);
C_ASSERT(FIELD_OFFSET(RefObjCommon, GP) == 0x180);
C_ASSERT(sizeof(RefObjCommon) == 0x198);

// The objects built in code (vftables 0x78cde0 / 0x78cdec, no own members).
class RefObjDummy : public RefObjCommon
{
};

class RefObjEventDaemon : public RefObjCommon
{
};

// ---------------------------------------------------------------------------------------------
// RefObjItem (vftable 0x78c820, 0x540 bytes)

// A lower / upper bound column pair and its increment column (0 for the tables without one).
struct RefObjItemRange
{
    float            L;                          // +0
    float            U;                          // +4
    float            Inc;                        // +8
};

// One ParamN / DescN_128 column pair (RefParam::ctor, 0x20 bytes).
struct RefParam
{
    RefParam()
        : Param(0)
    {
    }

    int              Param;                      // +0
    std::string      Desc;                       // +4
};

C_ASSERT(sizeof(RefParam) == 0x20);

// The sDataStream objects of an item, by tag name (RefObjItemLinkTables::ctor 0x48ea10 / dtor
// 0x48ea80): target A, target P, and all of them (owned, RefObjItem_ClearDataStreams 0x655eb0).
// The base of the tag objects (vftable 0x794550; the tag classes and their parsers are not
// ported yet).
class sDataStream
{
public:
    virtual ~sDataStream()
    {
    }
};

struct RefObjItemLinkTables
{
    typedef std::map<std::string, sDataStream*> DataStreamMap;

    ~RefObjItemLinkTables();

    DataStreamMap    mapA;                       // +0
    DataStreamMap    mapP;                       // +C
    DataStreamMap    mapAll;                     // +18
};

C_ASSERT(sizeof(RefObjItemLinkTables) == 0x24);

class RefObjItem : public RefObjCommon
{
public:
    enum
    {
        PARAM_COUNT = 20,
    };

    // 0x5197e0.
    RefObjItem();
    // 0x48f9d0 (vf00 0x4a1c10).
    virtual ~RefObjItem();

    // RefObjItem::vf01 (0x48fa80): the common row, then the _RefObjItem row of Link.
    virtual int Read(CRefRow& row, int nReserved);

    int              MaxStack;                   // +198
    BYTE             ReqGender;                  // +19C
    int              ReqStr;                     // +1A0
    int              ReqInt;                     // +1A4
    BYTE             ItemClass;                  // +1A8
    int              SetID;                      // +1AC
    RefObjItemRange  Dur;                        // +1B0  no Inc column
    RefObjItemRange  PD;                         // +1BC
    RefObjItemRange  ER;                         // +1C8
    RefObjItemRange  PAR;                        // +1D4
    RefObjItemRange  BR;                         // +1E0  no Inc column
    RefObjItemRange  MD;                         // +1EC
    RefObjItemRange  MAR;                        // +1F8
    RefObjItemRange  PDStr;                      // +204  / 1000, no Inc column
    RefObjItemRange  MDInt;                      // +210  / 1000, no Inc column
    BYTE             Quivered;                   // +21C
    BYTE             Ammo_TID4[5];               // +21D
    BYTE             SpeedClass;                 // +222
    BYTE             TwoHanded;                  // +223
    WORD             Range;                      // +224
    RefObjItemRange  PAttackMin;                 // +228  no Inc column
    RefObjItemRange  PAttackMax;                 // +234
    RefObjItemRange  MAttackMin;                 // +240  no Inc column
    RefObjItemRange  MAttackMax;                 // +24C
    RefObjItemRange  PAStrMin;                   // +258  / 1000, no Inc column
    RefObjItemRange  PAStrMax;                   // +264  / 1000, no Inc column
    RefObjItemRange  MAIntMin;                   // +270  / 1000, no Inc column
    RefObjItemRange  MAIntMax;                   // +27C  / 1000, no Inc column
    RefObjItemRange  HR;                         // +288
    RefObjItemRange  CHR;                        // +294  no Inc column
    RefParam         Params[PARAM_COUNT];        // +2A0
    int              CoolTimeID;                 // +520  "cooltime" parameter
    int              CoolTime;                   // +524
    DWORD            m_dw528;                    // +528
    RefObjItemLinkTables* pLinkTables;           // +52C
    std::set<DWORD>  m_set530;                   // +530  TODO(type)
    BYTE             MaxMagicOptCount;           // +53C
    BYTE             ChildItemCount;             // +53D
};

C_ASSERT(FIELD_OFFSET(RefObjItem, MaxStack) == 0x198);
C_ASSERT(FIELD_OFFSET(RefObjItem, Dur) == 0x1B0);
C_ASSERT(FIELD_OFFSET(RefObjItem, Quivered) == 0x21C);
C_ASSERT(FIELD_OFFSET(RefObjItem, PAttackMin) == 0x228);
C_ASSERT(FIELD_OFFSET(RefObjItem, Params) == 0x2A0);
C_ASSERT(FIELD_OFFSET(RefObjItem, CoolTimeID) == 0x520);
C_ASSERT(FIELD_OFFSET(RefObjItem, pLinkTables) == 0x52C);
C_ASSERT(FIELD_OFFSET(RefObjItem, MaxMagicOptCount) == 0x53C);
C_ASSERT(sizeof(RefObjItem) == 0x540);

// RefObjItem_IsWithinMaxStack (0x51ac70).
BOOL RefObjItem_IsWithinMaxStack(const RefObjItem* pItem, WORD wCount);
// RefObj_IsEquipItem (0x51ac90): asserts a NULL object and returns FALSE.
BOOL RefObj_IsEquipItem(const RefObjCommon* pObj);
// RefObj_IsValidSlotForItem (0x51ada0): equipment below slot 12, anything else slot 0.
BOOL RefObj_IsValidSlotForItem(const RefObjCommon* pObj, BYTE btSlot);
// RefObjItem_GetDurabilityRange (0x51ae20): accessories 1..1, TypeID 3/1/13/4 0..0, other
// equipment the Dur bounds (truncated); nothing for a non-equipment object.
void RefObjItem_GetDurabilityRange(const RefObjItem* pItem, int& nMin, int& nMax);
// RefObjItem_IsValidDurability (0x51aef0).
BOOL RefObjItem_IsValidDurability(const RefObjItem* pItem, DWORD dwValue);
// RefObjItem_IsValidDurabilityOrStack (0x51adc0): stack for ETC items, durability for
// equipment, 0 for everything else.
BOOL RefObjItem_IsValidDurabilityOrStack(const RefObjItem* pItem, DWORD dwValue);
// RefObjItem_RegisterSpecialItem (0x4a1210): the item ids of quest / event items go into the
// set.
void RefObjItem_RegisterSpecialItem(const WORD& wTypeID, stdext::hash_set<int>& setID, int nID,
                                    const std::string& strCodeName);

// ---------------------------------------------------------------------------------------------
// RefObjChar (vftable 0x78cdd4, 0x408 bytes)

struct RefObjCharUnk30 { BYTE ab[0x30]; };       // TODO(type) the 48-byte vector elements

// One Except_N column (8 bytes, the second DWORD is not read).
struct RefObjCharExcept
{
    int              Value;                      // +0
    DWORD            m_dw04;                     // +4
};

class RefObjChar : public RefObjCommon
{
public:
    // 0x4a3040.
    RefObjChar();
    // 0x4a3110.
    virtual ~RefObjChar();

    // RefObjChar::vf01 (0x4915f0): the common row, then the _RefObjChar row of Link.
    virtual int Read(CRefRow& row, int nReserved);

    BYTE             Lvl;                        // +198
    BYTE             CharGender;                 // +199
    int              MaxHP;                      // +19C
    int              MaxMP;                      // +1A0
    int              Resist[32];                 // +1A4
    BYTE             InventorySize;              // +224
    WORD             CanStore;                   // +226  CanStore_TID1..4 bit fields
    BYTE             DamagePortion;              // +228
    WORD             MaxPassenger;               // +22A
    int              AssocTactics;               // +22C
    int              PD;                         // +230
    int              MD;                         // +234
    int              PAR;                        // +238
    int              MAR;                        // +23C
    int              ER;                         // +240
    int              BR;                         // +244
    int              HR;                         // +248
    int              CHR;                        // +24C
    int              ExpToGive;                  // +250
    int              CreepType;                  // +254
    BYTE             Knockdown;                  // +258
    int              KO_RecoverTime;             // +25C  3000 when 0 and Knockdown is set
    int              DefaultSkill[10];           // +260
    std::vector<RefObjCharUnk30> m_vec288;       // +288  TODO(type)
    std::vector<RefObjCharUnk30> m_vec298;       // +298  TODO(type)
    std::vector<DWORD> m_avec2A8[16];            // +2A8  TODO(type)
    RefObjCharExcept Except[10];                 // +3A8
    float            DecreaseHGPPerSec;          // +3F8
    int              m_n3FC;                     // +3FC  the last column
    BOOL             HasDefaultSkill;            // +400
    DWORD            m_dw404;                    // +404
};

C_ASSERT(FIELD_OFFSET(RefObjChar, Lvl) == 0x198);
C_ASSERT(FIELD_OFFSET(RefObjChar, InventorySize) == 0x224);
C_ASSERT(FIELD_OFFSET(RefObjChar, ExpToGive) == 0x250);
C_ASSERT(FIELD_OFFSET(RefObjChar, DefaultSkill) == 0x260);
C_ASSERT(FIELD_OFFSET(RefObjChar, m_avec2A8) == 0x2A8);
C_ASSERT(FIELD_OFFSET(RefObjChar, Except) == 0x3A8);
C_ASSERT(FIELD_OFFSET(RefObjChar, HasDefaultSkill) == 0x400);
C_ASSERT(sizeof(RefObjChar) == 0x408);

// The special monster code list (singleton dword_8a8940, SpecialMonsterCodeList_ApplyToChar
// 0x4a2650) belongs to another part of the server: RefObjChar::Read calls it through this hook
// (the original asserts when the list does not exist).
typedef void (*PFN_REFOBJ_APPLY_SPECIAL_MONSTER)(RefObjChar* pChar);
extern PFN_REFOBJ_APPLY_SPECIAL_MONSTER g_pfnApplySpecialMonsterCode;

// ---------------------------------------------------------------------------------------------
// RefObjStruct (vftable 0x78d278, 0x198 bytes)

class RefObjStruct : public RefObjCommon
{
public:
    // RefObjStruct::vf01 (0x493020): the common row, then the _RefObjStruct row of Link; the
    // object is added to its region's list.
    virtual int Read(CRefRow& row, int nReserved);
};

C_ASSERT(sizeof(RefObjStruct) == 0x198);

// The structures of a region (vector at RefRegion +0x90, RefObj_AddUniqueLinkedID 0x4a0450).
inline std::vector<RefObjCommon*>& RefRegion_GetStructs(RefRegionRow* pRegion)
{
    return *(std::vector<RefObjCommon*>*)((BYTE*)pRegion + 0x90);
}

void RefObj_AddUniqueLinkedID(RefRegionRow* pRegion, RefObjCommon* pObj);

// ---------------------------------------------------------------------------------------------
// Slot 0xB and its post-processing

// CRefInstanceGenerator::FindRowInTable (0x4a9830): the row of a registered table whose key is
// nID (slots 3 / 0x11: the record's +0x30; slots 0xC..0xE: the first column), NULL when there is
// none.
CDBRecStream* RefData_FindRowInTable(CReferenceData* pData, unsigned int nSlot, int nID);

// RefObjCommon_ReadAndCreate (0x4a2cf0): creates the object of the row's TypeID and reads it.
int RefObjCommon_ReadAndCreate(CReferenceData* pData, CDBRecord* pRecord);
// Slot 0xB: RefData_BuildRefObjCommon (0x4cf170) + CRefInstanceGenerator::PostProcessObjCommon
// (0x4aa0a0).
int RefData_BuildRefObjCommon(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);
void RefData_PostProcessObjCommon(CReferenceData* pData);

// CReferenceData::CreateDummyObj (0x4ec330) / CreateEventDaemonObj (0x4ebef0): the object of
// id -1 / -2 copied from the template.
void RefData_CreateDummyObj(const RefObjCommon* pTemplate, RefObjCommon* pObj);
void RefData_CreateEventDaemonObj(const RefObjCommon* pTemplate, RefObjCommon* pObj);

// CReferenceData::RegisterSiegeGuarderRef (0x4eab10) / RegisterMercenaryRef (0x4ebc50): the
// object into the 140 level slots of its code name; FALSE when the slot is taken.
BOOL RefData_RegisterByLevel(CReferenceData::RefObjByLevelHashMap& hash,
                             const std::string& strCodeName, RefObjCommon* pObj);
BOOL RefData_RegisterSiegeGuarderRef(CReferenceData::RefObjByLevelHashMap& hash,
                                     const std::string& strCodeName, RefObjCommon* pObj);
BOOL RefData_RegisterMercenaryRef(const std::string& strCodeName,
                                  CReferenceData::RefObjByLevelHashMap& hash, RefObjCommon* pObj);

// ---------------------------------------------------------------------------------------------
// CRefCharGen (slot 0x37)

// CRefCharGen::ReadRow (0x4a4190) / AddEntry (0x48d790) / AddItemByRace (0x48e900) /
// AddItemToRaceList (0x48d670).
int CRefCharGen_ReadRow(CRefCharGen& gen, CRefRow& row);
void CRefCharGen_AddEntry(CRefCharGen& gen, int nRefObjID);
void CRefCharGen_AddItemByRace(BYTE btGender, CRefCharGenRace& race, int nRefObjID);
void CRefCharGen_AddItemToRaceList(CRefCharGenGender& gender, int nRefObjID);
// CRefCharGen::IsAllowedItem (0x48d990): the item is in the table and fits race and gender.
BOOL CRefCharGen_IsAllowedItem(CRefCharGen& gen, int nItemID, BYTE btRace, BYTE btGender);
// CRefCharGen::ValidateNewCharacter (0x48d8d0): a player character and up to 4 start items.
BOOL CRefCharGen_ValidateNewCharacter(CRefCharGen& gen, int nRefObjID, const int* anItemID);
// Slot 0x37: RefData_BuildRefCharGenData (0x4cf0f0).
int RefData_BuildRefCharGenData(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);

// ---------------------------------------------------------------------------------------------
// Skill tables (slots 4 and 5)

// RefData_AddRowRefSkillMastery (0x4d29f0): the copy of the record, its ID.
int RefRec_Read(const CRefSkillMastery& rec, RefSkillMasteryData& r);
// RefSkill_LoadRow_4971B0 + RefSkill::CopyFrom (0x4971d0): 0 for a skill out of service.
int RefRec_Read(const CRefSkill& rec, RefSkill& r);
// Slot 4: RefData_BuildRefSkillMastery (0x4cf730).
int RefData_BuildRefSkillMastery(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);
// Slot 5: RefData_BuildRefSkill (0x4cf7a0, CRefInstanceGenerator::LoadSkill 0x4a42a0 / AddSkill
// 0x4afdb0) + CReferenceData::BuildRefSkillByCodename (0x4e9820).
int RefData_BuildRefSkill(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);
void RefData_BuildRefSkillByCodename(CReferenceData* pData);
