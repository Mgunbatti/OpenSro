#pragma once

#include <string>
#include <vector>
#include <hash_set>

#include "ServerCommon/ReferenceData.h"

// The reference tables and helpers of SR_ShardManager that belong to no larger family: set item
// groups, HWAN levels, events, default quests, the collection book, the monster lists by kind,
// the special item / monster code lists, the sDataStream item parameters and a few TypeID tests.

// ---------------------------------------------------------------------------------------------
// Row readers (each returns the row's key, 0 for a row that is dropped)

// RefData_ReadIdNameRow (0x493610, _RefEvent): Service (BYTE), ID (0 asserts), Name, a string.
// A Name of 256 characters or more asserts and drops the row; else Name is stored in lower case.
int RefRow_Read(CRefRow& row, RefEventRow& r);
// RefData_ReadIdTypeNameRow (0x493740, _RefDefaultQuest): Service (BYTE), ID, a BYTE, Name, a
// BYTE. A Name of 256 characters or more asserts and drops the row.
int RefRow_Read(CRefRow& row, RefDefaultQuestRow& r);
// RefData_ReadIdStringRow (0x494730, the shop table of slot 36): a DWORD and a string; the
// result is not used by the original.
void RefRow_Read(CRefRow& row, RefShopSlot36Row& r);
// RefSetItemGroup::Read (0x492b10): Service (BYTE), ID, four strings that are dropped, a BYTE that
// is skipped, SetMagicMask; for a row in service the ten MagicOptGroupID columns. An unknown
// group is reported and kept; a bit of the mask without a group fails the row.
int RefRow_Read(CRefRow& row, RefSetItemGroup& r);
// RefData_ReadIdBytePairs5 (0x497620): Service (BYTE, not looked at), then five (ID, BYTE)
// pairs; the pairs with an ID are appended.
void RefData_ReadIdBytePairs5(CRefRow& row, RefIdBytePairs& r);
// RefCollectionBookTheme::Read (0x499e10): Service (BYTE), ID, four strings, CompleteNum. The
// Service column is the result.
int RefRow_Read(CRefRow& row, RefCollectionBookTheme& r);
// RefCollectionBookItem::Read (0x499ee0): Service (BYTE), three strings, SlotIndex, two strings.
int RefRow_Read(CRefRow& row, RefCollectionBookItem& r);

// ---------------------------------------------------------------------------------------------
// Table loaders and builders (slots of CRefInstanceGenerator::BuildFromTable)

// CRefInstanceGenerator::LoadIdBytePairsTable (0x4a5600, slot 0x53 through
// RefData_BuildRefHWANLevel 0x4cfa90): every row is appended.
int RefData_LoadIdBytePairsTable(CReferenceData* pData, CDBRecord* pRecord);
// CRefInstanceGenerator::LoadCollectionBookTheme (0x4a7520, slot 0x68 through
// RefData_BuildRefCollectionBook_Theme 0x4d0940): a row out of service is freed; a CodeName that
// is already there is logged, freed and fails the build.
int RefData_LoadCollectionBookTheme(CReferenceData* pData, CDBRecord* pRecord);
// CRefInstanceGenerator::LoadCollectionBookItem (0x4a7700, slot 0x69): the same by ItemCodeName.
int RefData_LoadCollectionBookItem(CReferenceData* pData, CDBRecord* pRecord);
// Slot 0x69 (RefData_BuildRefCollectionBook_Item 0x4d09b0): the items, then
// ValidateCollectionBook.
int RefData_BuildCollectionBookItem(CReferenceData* pData, RefTableEntry* pTable,
                                    unsigned int nSlot);
// CRefInstanceGenerator::ValidateCollectionBook (0x4add80): puts every item into the slot of its
// theme and indexes the themes by ID. Every problem is logged and makes the result FALSE; the
// checks go on.
BOOL RefData_ValidateCollectionBook(CReferenceData* pData);

// CRefInstanceGenerator::FindObjByCodeName (0x4a0980, the name of the analysis): the rent item
// of a code name (m_hashRentItem), NULL when there is none.
RefRentItem* RefData_FindRentItem(CReferenceData* pData, const char* pszCodeName);

// ---------------------------------------------------------------------------------------------
// Monster lists by kind (CRefInstanceGenerator::PostProcessObjCommon)

// BuildHunterMonsterList (0x4ab320) / BuildThiefMonsterList (0x4aa8e0): the hunter (TypeID
// 1/2/1/3) / thief (1/2/1/2) monsters into 3 kinds x 140 levels: MOB_HUNTER / MOB_THIEF 0,
// MOB_EU_HUNTER / MOB_EU_THIEF 1, any other 2. A level above 140 asserts.
void RefData_BuildHunterMonsterList(CReferenceData* pData);
void RefData_BuildThiefMonsterList(CReferenceData* pData);
// BuildTrapMonsterList (0x4aae20, called with "TQ"): the monsters (TypeID 1/2/1, not thieves or
// hunters, Rarity not 3 or 8) whose code name starts with "MOB_" + strKind, except
// MOB_TQ_QINSHITRAP* and MOB_TQ_SNAKETRAP*, into m_mapTrapMonster[strKind].
void RefData_BuildTrapMonsterList(CReferenceData* pData, const std::string& strKind);

// ---------------------------------------------------------------------------------------------
// Special item / monster code lists (singletons dword_8a893c / dword_8a8940)

// One code name and the two flag words it adds to an object (0x28 bytes).
struct SpecialCodeEntry
{
    std::string      strCodeName;                // +0
    DWORD            m_dw1C;                     // +1C  not initialised
    DWORD            dwFlag1;                    // +20  ORed into RefObjCommon +0x188
    DWORD            dwFlag2;                    // +24  ORed into RefObjCommon +0x18C
};

C_ASSERT(sizeof(SpecialCodeEntry) == 0x28);

class CSpecialCodeList
{
public:
    // SpecialItemCodeList_CreateInstance (0x4a1f80) / SpecialMonsterCodeList_CreateInstance
    // (0x4a23d0): a second instance asserts.
    static CSpecialCodeList* CreateItemList();
    static CSpecialCodeList* CreateMonsterList();
    // SpecialItemCodeList_Destroy (0x4a2020) / SpecialMonsterCodeList_Destroy (0x4a2470).
    static void DestroyItemList();
    static void DestroyMonsterList();

    static CSpecialCodeList* GetItemList();
    static CSpecialCodeList* GetMonsterList();

    // SpecialItemCodeList_Add (0x4a21e0).
    void Add(const char* pszCodeName, DWORD dwFlag1, DWORD dwFlag2);
    // SpecialItemCodeList_ApplyToItem (0x4a22e0): clears the two flag words of the object, then
    // ORs in the flags of every entry whose code name equals the object's.
    void ApplyToItem(RefObjCommonRow* pObj);
    // SpecialMonsterCodeList_ApplyToChar (0x4a2650): clears the two flag words, then takes the
    // flags of the first entry the object's code name starts with.
    void ApplyToChar(RefObjCommonRow* pObj);

    DWORD                           m_dw00;      // +0
    std::vector<SpecialCodeEntry*>  m_vecEntry;  // +4

private:
    void Clear();
};

C_ASSERT(sizeof(CSpecialCodeList) == 0x14);

// The two flag words of an object (RefObjCommon +0x188 / +0x18C).
inline DWORD& RefObj_SpecialFlag1(RefObjCommonRow* pObj)
{
    return *(DWORD*)((BYTE*)pObj + 0x188);
}

inline DWORD& RefObj_SpecialFlag2(RefObjCommonRow* pObj)
{
    return *(DWORD*)((BYTE*)pObj + 0x18C);
}

// ServerConfigExtraMap_GetInstance (0x4a0e00, the name of the analysis): the function-static
// set of the special item IDs that RefObjItem::Read fills (RefObjItem_RegisterSpecialItem).
stdext::hash_set<int>& RefObjItem_GetSpecialItemIDSet();

// ---------------------------------------------------------------------------------------------
// TypeID tests (bits 1: bionic, 2-4 TID1, 5-6 TID2, 7-10 TID3, 11-15 TID4)

BOOL RefTID_Is_1_2_4(const WORD& wTypeID);           // 0x5199e0  bionic 1/2/4
BOOL RefTID_Is_1_2_5(const WORD& wTypeID);           // 0x519a10  bionic 1/2/5
BOOL RefTID_Is_3_1_13_4(const WORD& wTypeID);        // 0x51dce0
BOOL RefTID_Is_3_3_11_3(const WORD& wTypeID);        // 0x51dd20
BOOL RefTID_Is_3_3_11_7(const WORD& wTypeID);        // 0x51dd60

// ---------------------------------------------------------------------------------------------
// sDataStream: the parameters of an item in "[NAME,...]" form (RefObjItem_ParseDataStreams).
// Parse reads one parameter from strData at *pnPos (the position after the tag) and moves *pnPos
// past it; FALSE (after an assert) when a part is missing.

// sDataStream_ExtractToken (0x655520): skips the spaces at nPos (sDataStream_GetParsePos
// 0x6554f0), then copies the text up to pszDelim into strOut and returns the position after the
// delimiter; -1 when nPos is past the end or there is no delimiter. pszPrefix is searched but
// its position is not used.
int sDataStream_ExtractToken(const std::string& strData, int nPos, const char* pszPrefix,
                             const char* pszDelim, std::string& strOut);

class sDataStream
{
public:
    enum
    {
        TARGET_A = 0,
        TARGET_P = 1,
    };

    // 0x654bb0.
    sDataStream()
    {
    }

    virtual ~sDataStream()
    {
    }

    virtual BOOL Parse(const std::string& strData, int* pnPos) = 0;

    // sDataStream_ParseTargetAP (0x655650): "A" or "P" into m_nTarget; anything else asserts.
    void ParseTarget(const std::string& strToken);

    DWORD            m_dw04;                     // +4   not initialised
    std::string      m_strName;                  // +8
    int              m_nTarget;                  // +24  TARGET_*, not initialised
};

C_ASSERT(sizeof(sDataStream) == 0x28);

// "[NAME]" (vftable slot 1 0x655690): m_nTarget = TARGET_P.
class UsedObjectUnLimit : public sDataStream
{
public:
    virtual BOOL Parse(const std::string& strData, int* pnPos);
};

class UIU : public UsedObjectUnLimit
{
};

class USU : public UsedObjectUnLimit
{
};

// "[NAME,VALUE]" (0x655790): m_nTarget = TARGET_P.
class PSU : public UsedObjectUnLimit
{
public:
    PSU()
    {
    }

    virtual BOOL Parse(const std::string& strData, int* pnPos);

    int              m_nValue;                   // +28
};

C_ASSERT(sizeof(PSU) == 0x2C);

// "[NAME,A|P,VALUE1,VALUE2]" (0x655910).
class UsedObjectLimit : public sDataStream
{
public:
    virtual BOOL Parse(const std::string& strData, int* pnPos);

    int              m_nValue1;                  // +28
    int              m_nValue2;                  // +2C
};

C_ASSERT(sizeof(UsedObjectLimit) == 0x30);

class UIL : public UsedObjectLimit
{
};

class USL : public UsedObjectLimit
{
};

class UQL : public UsedObjectLimit
{
};

// "[NAME,A|P,VALUE]" (0x655ac0).
class BUFF_ITEM : public sDataStream
{
public:
    virtual BOOL Parse(const std::string& strData, int* pnPos);

    int              m_nValue;                   // +28
};

C_ASSERT(sizeof(BUFF_ITEM) == 0x2C);

// "[NAME,A|P,VALUE,V1,V2,V3,V4,V5]" (0x655c40): the five values go into m_vecStatus.
class BUF_CHARACTER_STATUS : public sDataStream
{
public:
    virtual BOOL Parse(const std::string& strData, int* pnPos);

    int              m_nValue;                   // +28
    std::vector<int> m_vecStatus;                // +2C
};

C_ASSERT(sizeof(BUF_CHARACTER_STATUS) == 0x3C);
