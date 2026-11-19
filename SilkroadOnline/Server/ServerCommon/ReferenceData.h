#pragma once

#include <map>
#include <string>
#include <vector>
#include <hash_map>

#include "BSLib/DB/Database.h"
#include "ServerCommon/RefRecords.h"

// The reference data of SR_ShardManager (ServerCommon\ReferenceData.cpp and
// RefInstanceGenerator.cpp of the original; both names belong to the one static object 0x89b020,
// g_pReferenceData = dword_8a8944).
//
// CShardDB::RegisterRefTables reads every _Ref table into a table entry (CDBTable + list of
// CDBRecStream rows) and calls BuildAllTables, which builds the containers of this object from
// them, table by table in slot order. A row is parsed by a reader (RefRow_Read overloads) from
// the stream buffer of its record; the containers own the parsed rows.

// ---------------------------------------------------------------------------------------------
// Table entries

// One registered _Ref table: the table and the rows CShardDB selected (records of the table's
// record class). The empty-table check of the builders reads m_listRows' size at +0xC.
struct RefTableEntry
{
    CDBTable*       m_pTable;       // +0
    CDBRecordList   m_listRows;     // +4
};

C_ASSERT(sizeof(RefTableEntry) == 16);

// The table vector of CShardDB (index = slot, NULL for a table that was not registered).
typedef std::vector<RefTableEntry*> RefTableVector;

enum
{
    REF_TABLE_SLOT_COUNT = 124,
};

// ---------------------------------------------------------------------------------------------
// Row reader

// Reads the columns of a CDBRecStream row one after the other (m_nReadPos, m_nFieldCount).
// A scalar past the end of the row asserts and is read anyway; a string asserts and terminates
// (see SM-006).
class CRefRow
{
public:
    explicit CRefRow(CDBRecStream* pRow)
        : m_pRow(pRow)
    {
    }

    template <class T>
    CRefRow& operator>>(T& value)
    {
        // BUG(SM-006): the overrun is only asserted; the value is read past the row.
        if ((int)(m_pRow->m_nReadPos + sizeof(T)) > (int)m_pRow->m_nWritePos)
            BSAssertFail();
        value = *(const T*)(m_pRow->m_pBuffer + m_pRow->m_nReadPos);
        m_pRow->m_nReadPos += sizeof(T);
        ++m_pRow->m_nFieldCount;
        return *this;
    }

    // RefDataRow_ReadString (0x4a1e30).
    CRefRow& operator>>(std::string& value);

    // The leading Service column (DWORD) of most tables: FALSE for a row out of service.
    BOOL Service()
    {
        DWORD dwService;
        *this >> dwService;
        return dwService != 0;
    }

    // The ID column: an ID of 0 asserts (the reader then returns 0 and the row is dropped).
    template <class T>
    CRefRow& Id(T& value)
    {
        *this >> value;
        if (!value)
            BSAssertFail();
        return *this;
    }

private:
    CDBRecStream* m_pRow;
};

// ---------------------------------------------------------------------------------------------
// Parsed rows without a struct in RefRecords.h

// _RefGameWorldBindTriggerCategory, _RefTriggerCategoryBindTrigger, _RefTriggerBindEvent /
// Condition / Action, _RefGameWorldBindGameWorldGroup (12 bytes, RefTriggerBind::Read 0x49a180).
struct RefTriggerBind
{
    int              ID;                         // +0
    int              ID1;                        // +4  owner (key of the multimap)
    int              ID2;                        // +8  bound row
};

C_ASSERT(sizeof(RefTriggerBind) == 12);

// _RefTriggerEvent (8 bytes, RefTriggerEvent::Read 0x49a340).
struct RefTriggerEvent
{
    int              ID;                         // +0
    int              Value;                      // +4
};

C_ASSERT(sizeof(RefTriggerEvent) == 8);

// Row readers (the address is the reader of the original). Each returns the row's ID: 0 for a
// row out of service (or with ID 0), which is then dropped.
int RefRow_Read(CRefRow& row, RefUIString& r);                   // 0x499f80
int RefRow_Read(CRefRow& row, RefTriggerCategory& r);            // 0x499ff0
int RefRow_Read(CRefRow& row, RefTrigger& r);                    // 0x49a0a0
int RefRow_Read(CRefRow& row, RefTriggerBind& r);                // 0x49a180
int RefRow_Read(CRefRow& row, RefTriggerCommon& r);              // 0x49a230
int RefRow_Read(CRefRow& row, RefTriggerEvent& r);               // 0x49a340
int RefRow_Read(CRefRow& row, RefTriggerCondition& r);           // 0x49a3c0
int RefRow_Read(CRefRow& row, RefTriggerAction& r);              // 0x49a490
int RefRow_Read(CRefRow& row, RefTriggerVariable& r);            // 0x49a550
int RefRow_Read(CRefRow& row, RefGameWorldGroup& r);             // 0x49a620
// RefKeyValue4Str::Read 0x49a680 (one function for the four tables).
int RefRow_Read(CRefRow& row, RefShardContentConfig& r);
int RefRow_Read(CRefRow& row, RefTriggerConditionParam& r);
int RefRow_Read(CRefRow& row, RefTriggerActionParam& r);
int RefRow_Read(CRefRow& row, RefGameWorldGroupConfig& r);

// The part of a _RefGameWorldNPC row kept per key (76 bytes, the vector element of
// CReferenceData::m_mapGameWorldNPC). Column names are not known.
struct RefGameWorldNPCEntry
{
    std::string      str00;                      // +0
    WORD             aw1C[4];                    // +1C
    DWORD            adw24[10];                  // +24
};

C_ASSERT(sizeof(RefGameWorldNPCEntry) == 76);

// Parsed _RefGameWorldNPC row (108 bytes, no Service column): InitMembers 0x4a6540 /
// DestroyMembers 0x4a6660.
struct RefGameWorldNPC
{
    int                     ID;                  // +0
    std::string             strKey;              // +4   key of the map
    RefGameWorldNPCEntry    Entry;               // +20
};

C_ASSERT(sizeof(RefGameWorldNPC) == 108);

// Parsed _RefGame_World_Config row (116 bytes): InitMembers 0x4a67e0 / DestroyMembers 0x4a68d0.
struct RefGameWorldConfig
{
    int              ID;                         // +0
    std::string      GroupCodeName128;           // +4   key of the map
    std::string      ValueCodeName128;           // +20
    std::string      Value;                      // +3C
    std::string      Type;                       // +58
};

C_ASSERT(sizeof(RefGameWorldConfig) == 116);

// Parsed _RefObjCharExtraSkill row (84 bytes).
struct RefObjCharExtraSkill
{
    int              RefObjID;                   // +0
    int              aSkillID[20];               // +4
};

C_ASSERT(sizeof(RefObjCharExtraSkill) == 84);

int RefRow_Read(CRefRow& row, RefGameWorldNPC& r);               // 0x498700
int RefRow_Read(CRefRow& row, RefGameWorldConfig& r);            // 0x498930
int RefRow_Read(CRefRow& row, RefObjCharExtraSkill& r);          // 0x4989d0

// Readers of the siege tables (CDBRecord records): copy the record columns into the row. The
// return value is what the loader tests (Service, or the key column).
int RefRec_Read(const CRefSiegeFortress& rec, RefSiegeFortress& r);                    // 0x497ea0
int RefRec_Read(const CRefSiegeFortressGuard& rec, RefSiegeFortressGuardData& r);      // 0x497f70
// 0x497f90
int RefRec_Read(const CRefSiegeFortressItemForge& rec, RefSiegeFortressItemForgeData& r);
// 0x497fc0
int RefRec_Read(const CRefSiegeFortressBattleRank& rec, RefSiegeFortressBattleRank& r);
int RefRec_Read(const CRefSiegeStructUpgrade& rec, RefSiegeStructUpgrade& r);          // 0x498000
// 0x498070
int RefRec_Read(const CRefSiegeLvlSummonMonster& rec, RefSiegeLvlSummonMonsterData& r);
void RefRec_Read(const CRefSiegeFortressRewards& rec, RefSiegeFortressRewardsData& r); // 0x498090
int RefRec_Read(const CRefSiegeBlessBuff& rec, RefSiegeBlessBuffData& r);              // 0x4980b0
int RefRec_Read(const CRefSiegeDungeon& rec, RefSiegeDungeonData& r);                  // 0x4980b0
int RefRec_Read(const CRefSiegeQuest& rec, RefSiegeQuest& r);                          // 0x4980d0
int RefRec_Read(const CRefSiegeQuestReward& rec, RefSiegeQuestRewardData& r);          // 0x498110

// Opaque _RefObjCommon row (see InstanceRecords.h).
struct RefObjCommonRow;

// ---------------------------------------------------------------------------------------------
// CReferenceData

class CReferenceData
{
public:
    typedef stdext::hash_map<int, RefObjCommonRow*>             RefObjHashMap;
    typedef std::map<std::string, RefShardContentConfig*>       RefShardContentConfigMap;
    typedef std::map<std::string, RefUIString*>                 RefUIStringMap;
    typedef std::multimap<int, RefTriggerBind*>                 RefTriggerBindMap;
    typedef std::map<int, RefTriggerCategory*>                  RefTriggerCategoryMap;
    typedef std::map<int, RefTrigger*>                          RefTriggerMap;
    typedef std::multimap<int, RefTriggerVariable*>             RefTriggerVariableMap;
    typedef std::map<int, RefTriggerCommon*>                    RefTriggerCommonMap;
    typedef std::map<int, RefTriggerEvent*>                     RefTriggerEventMap;
    typedef std::map<int, RefTriggerCondition*>                 RefTriggerConditionMap;
    typedef std::map<int, RefTriggerAction*>                    RefTriggerActionMap;
    typedef std::map<std::string, RefTriggerConditionParam*>    RefTriggerConditionParamMap;
    typedef std::map<std::string, RefTriggerActionParam*>       RefTriggerActionParamMap;
    typedef std::map<int, RefGameWorldGroup*>                   RefGameWorldGroupMap;
    typedef std::map<std::string, RefGameWorldGroupConfig*>     RefGameWorldGroupConfigMap;
    typedef std::vector<RefSiegeFortress*>                      RefSiegeFortressVector;
    typedef std::multimap<int, RefSiegeFortressGuardData*>      RefSiegeFortressGuardMap;
    typedef std::multimap<int, RefSiegeFortressItemForgeData*>  RefSiegeFortressItemForgeMap;
    typedef std::vector<RefSiegeFortressBattleRank*>            RefSiegeFortressBattleRankVector;
    typedef std::vector<RefSiegeStructUpgrade*>                 RefSiegeStructUpgradeVector;
    typedef std::vector<RefSiegeLvlSummonMonsterData*>          RefSiegeSummonByLevelVector;
    typedef std::map<DWORD, RefSiegeSummonByLevelVector*>       RefSiegeLvlSummonMonsterMap;
    typedef std::multimap<int, RefSiegeFortressRewardsData*>    RefSiegeFortressRewardsMap;
    typedef std::map<DWORD, RefSiegeBlessBuffData*>             RefSiegeBlessBuffMap;
    typedef std::map<DWORD, RefSiegeDungeonData*>               RefSiegeDungeonMap;
    typedef std::multimap<int, RefSiegeQuest*>                  RefSiegeQuestMap;
    typedef std::multimap<int, RefSiegeQuestRewardData*>        RefSiegeQuestRewardMap;
    typedef std::vector<RefGameWorldNPCEntry>                   RefGameWorldNPCVector;
    typedef std::map<std::string, RefGameWorldNPCVector>        RefGameWorldNPCMap;
    typedef std::map<int, RefObjCharExtraSkill*>                RefObjCharExtraSkillMap;
    typedef std::map<std::string, RefGameWorldConfig*>          RefGameWorldConfigMap;

    // Static object of the original (dynamic initializer 0x50a9b0): sets g_pReferenceData.
    CReferenceData();
    // Frees the rows of the containers declared here.
    ~CReferenceData();

    // GetSingleton_CRefInstanceGenerator (0x41c5c0): asserts when there is no object.
    static CReferenceData* GetInstance();

    // CRefInstanceGenerator::BuildAllTables (0x4ad5a0): builds every registered table in slot
    // order. An empty table is logged ("there is no record") and skipped; the first table that
    // fails stops the build with a message box. FALSE then, TRUE otherwise.
    BOOL BuildAllTables();
    // CRefInstanceGenerator::BuildFromTable (0x4ac550): the builder of one slot.
    int BuildFromTable(RefTableEntry* pTable, unsigned int nSlot);

    // RefData_HashFindAt108 (0x42e280): the object row of an id, NULL when there is none.
    RefObjCommonRow* FindRefObj(int nRefObjID);

public:
    RefTableVector*             m_pTables;                          // +000
    BYTE                        m_ab004[0x64];                      // +004  (not ported yet)
    RefObjHashMap               m_hashRefObj;                       // +068  _RefObjCommon by ID
    BYTE                        m_ab090[0x96C];                     // +090  (not ported yet)
    RefSiegeFortressVector      m_vecSiegeFortress;                 // +9FC
    RefSiegeFortressGuardMap    m_mmapSiegeFortressGuard;           // +A0C  by FortressID
    RefSiegeFortressItemForgeMap m_mmapSiegeFortressItemForge;      // +A18  by FortressID
    RefSiegeFortressBattleRankVector m_vecSiegeFortressBattleRank;  // +A24
    RefSiegeStructUpgradeVector m_vecSiegeStructUpgrade;            // +A34
    RefSiegeLvlSummonMonsterMap m_mapSiegeLvlSummonMonster;         // +A44  by RefOrgObjID
    RefSiegeFortressRewardsMap  m_mmapSiegeFortressRewards;         // +A50  by FortressID
    RefSiegeBlessBuffMap        m_mapSiegeBlessBuff;                // +A5C  by BlessID
    RefSiegeDungeonMap          m_mapSiegeDungeon;                  // +A68  by FortressID
    RefSiegeQuestMap            m_mmapSiegeQuest;                   // +A74  by QuestType
    RefSiegeQuestRewardMap      m_mmapSiegeQuestReward;             // +A80  by QuestID
    BYTE                        m_abA8C[0xC];                       // +A8C  (not ported yet)
    RefShardContentConfigMap    m_mapShardContentConfig;            // +A98  by CodeName128
    RefGameWorldNPCMap          m_mapGameWorldNPC;                  // +AA4
    RefObjCharExtraSkillMap     m_mapObjCharExtraSkill;             // +AB0  by RefObjID
    RefGameWorldConfigMap       m_mapGameWorldConfig;               // +ABC  by GroupCodeName128
    BYTE                        m_abAC8[0xC0];                      // +AC8  (not ported yet)
    RefUIStringMap              m_mapUIString;                      // +B88  by GroupCodeName128
    RefTriggerBindMap           m_mmapGameWorldBindTriggerCategory; // +B94
    RefTriggerCategoryMap       m_mapTriggerCategory;               // +BA0
    RefTriggerBindMap           m_mmapTriggerCategoryBindTrigger;   // +BAC
    RefTriggerMap               m_mapTrigger;                       // +BB8
    RefTriggerVariableMap       m_mmapTriggerVariable;              // +BC4  by BindTriggerID
    RefTriggerBindMap           m_mmapTriggerBindEvent;             // +BD0
    RefTriggerBindMap           m_mmapTriggerBindCondition;         // +BDC
    RefTriggerBindMap           m_mmapTriggerBindAction;            // +BE8
    RefTriggerCommonMap         m_mapTriggerCommon;                 // +BF4
    RefTriggerEventMap          m_mapTriggerEvent;                  // +C00
    RefTriggerConditionMap      m_mapTriggerCondition;              // +C0C
    RefTriggerActionMap         m_mapTriggerAction;                 // +C18
    RefTriggerConditionParamMap m_mapTriggerConditionParam;         // +C24  by GroupCodeName128
    RefTriggerActionParamMap    m_mapTriggerActionParam;            // +C30  by GroupCodeName128
    RefTriggerBindMap           m_mmapGameWorldBindGameWorldGroup;  // +C3C
    RefGameWorldGroupMap        m_mapGameWorldGroup;                // +C48
    RefGameWorldGroupConfigMap  m_mapGameWorldGroupConfig;          // +C54  by GroupCodeName128
};

C_ASSERT(FIELD_OFFSET(CReferenceData, m_hashRefObj) == 0x68);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_ab090) == 0x90);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_vecSiegeFortress) == 0x9FC);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_vecSiegeFortressBattleRank) == 0xA24);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapSiegeLvlSummonMonster) == 0xA44);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mmapSiegeQuestReward) == 0xA80);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapShardContentConfig) == 0xA98);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapGameWorldNPC) == 0xAA4);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapGameWorldConfig) == 0xABC);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapUIString) == 0xB88);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapGameWorldGroupConfig) == 0xC54);
C_ASSERT(sizeof(CReferenceData) == 0xC60);

// dword_8a8944.
extern CReferenceData* g_pReferenceData;

// ---------------------------------------------------------------------------------------------
// Object type helpers and lookups

// TypeID of an object (WORD at RefObjCommon +0x80): bits 2-4 TID1, 5-6 TID2, 7-10 TID3, 11-15
// TID4. Weapons are TID1 3, TID2 1, TID3 6.
BOOL RefItem_IsStaffWeaponType(const WORD& wTypeID);         // 0x42dd90  TID4 10, 11, 15
BOOL RefItem_IsSwordWeaponType(const WORD& wTypeID);         // 0x42dde0  TID4 2, 3, 7, 8
BOOL RefObj_TypeIdIsClass0C(const WORD& wTypeID);            // 0x42e260  TID1 3
// RefObj_PackTypeID (0x42e1f0): six WORDs into the bit fields of one DWORD (bits 0-5: the
// first - 16, then 6-9, 10-14, 15-19, 20-25 and 26-31).
void RefObj_PackTypeID(DWORD& dwPacked, const WORD* pawValue);

// RefData_GetObjectCodeName (0x486550): CodeName128 of an object, "Unknown Object" for an
// unknown id.
const char* RefData_GetObjectCodeName(int nRefObjID);
