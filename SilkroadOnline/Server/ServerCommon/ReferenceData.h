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

    // RefData_SkipServiceFilteredColumn (0x49f880): skips the next column by the size its table
    // description gives it (nothing when the description has no such column).
    CRefRow& Skip();

    CDBRecStream* Stream() const
    {
        return m_pRow;
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

// Parsed _RefServerEvent row (92 bytes): RefServerEvent::InitMembers 0x4a6a80 / DestroyMembers
// 0x514270. Column names are not known.
struct RefServerEvent
{
    BYTE             Service;                    // +0
    DWORD            ID;                         // +4   key of the map
    BYTE             b08;                        // +8
    DWORD            dw0C;                       // +C
    std::string      str10;                      // +10
    TIMESTAMP_STRUCT tsBegin;                    // +2C
    TIMESTAMP_STRUCT tsEnd;                      // +3C
    BYTE             b4C;                        // +4C
    BYTE             b4D;                        // +4D
    WORD             w4E;                        // +4E
    BYTE             b50;                        // +50  (after a column that is read and dropped)
    DWORD            dw54;                       // +54
    BYTE             b58;                        // +58
};

C_ASSERT(sizeof(RefServerEvent) == 92);

// _RefServerEventReward row (24 bytes, vftable 0x78d284). The one virtual reads the row: the
// base row from _RefServerEventReward, the derived part from the table of its type.
class RefServerEventReward
{
public:
    enum
    {
        TYPE_EXP_UP_FOR_PLAYERS = 1,
        TYPE_PLAIN = 2,
        TYPE_SPAWN_MONSTER = 3,
    };

    // RefServerEventReward::vf00 (0x498ea0): FALSE for a row out of service or of an unknown
    // server event.
    virtual BOOL Read(CRefRow& row);

    BYTE             Service;                    // +4
    DWORD            ID;                         // +8   key of the reward map
    DWORD            ServerEventID;              // +C
    DWORD            dw10;                       // +10
    BYTE             b14;                        // +14
    BYTE             Type;                       // +15  TYPE_*
    BYTE             b16;                        // +16
};

C_ASSERT(sizeof(RefServerEventReward) == 24);

// _RefServerEventReward_ExpUPForPlayers (36 bytes, vftable 0x78d28c).
class RefServerEventReward_ExpUPForPlayers : public RefServerEventReward
{
public:
    // CopyBase (0x498fe0) twice: the base columns are copied, the rest is left as it is.
    explicit RefServerEventReward_ExpUPForPlayers(const RefServerEventReward& base)
        : RefServerEventReward(base)
    {
    }

    // 0x499010: FALSE for a row without its first column; the two percentages are clamped to
    // [0, 1].
    virtual BOOL Read(CRefRow& row);

    DWORD            dw18;                       // +18
    float            fRate1;                     // +1C  percent / 100
    float            fRate2;                     // +20  percent / 100
};

C_ASSERT(sizeof(RefServerEventReward_ExpUPForPlayers) == 36);

// _RefServerEventReward_SpawnMonster (40 bytes, vftable 0x78d294).
class RefServerEventReward_SpawnMonster : public RefServerEventReward
{
public:
    explicit RefServerEventReward_SpawnMonster(const RefServerEventReward& base)
        : RefServerEventReward(base)
    {
    }

    // 0x499190: always TRUE.
    virtual BOOL Read(CRefRow& row);

    WORD             w18;                        // +18  a DWORD column kept as WORD
    float            af1C[3];                    // +1C
};

C_ASSERT(sizeof(RefServerEventReward_SpawnMonster) == 40);

// Parsed _RefRentItem row (60 bytes, InitMembers 0x513e80); the key string is not part of it.
struct RefRentItem
{
    DWORD               dw00;                    // +0
    BYTE                b04;                     // +4
    BYTE                b05;                     // +5
    DWORD               dwFlags;                 // +8   bit 1: the value list below is read
    TIMESTAMP_STRUCT    ts0C;                    // +C
    TIMESTAMP_STRUCT    ts1C;                    // +1C
    std::vector<DWORD>  vecValue;                // +2C  up to 5 values
};

C_ASSERT(sizeof(RefRentItem) == 60);

// Parsed _RefQuestReward row (156 bytes): InitMembers 0x4a6ee0 / DestroyMembers 0x513d30.
struct RefQuestReward
{
    DWORD            QuestID;                    // +0   key of the map
    BYTE             b04;                        // +4
    BYTE             b05;                        // +5
    DWORD            dw08;                       // +8
    DWORD            dw0C;                       // +C
    DWORD            dw10;                       // +10
    DWORD            dw14;                       // +14
    int              n18;                        // +18  > 0 only for the temple quests
    std::string      QuestCodeName;              // +1C
    BYTE             b38;                        // +38
    BYTE             b39;                        // +39
    DWORD            dw3C;                       // +3C
    std::string      str40;                      // +40
    DWORD            dw5C;                       // +5C
    std::string      str60;                      // +60
    DWORD            dw7C;                       // +7C
    std::string      str80;                      // +80
};

C_ASSERT(sizeof(RefQuestReward) == 156);

int RefRow_Read(CRefRow& row, RefServerEvent& r);                // 0x498c70
// RefRentItem::Read (0x499240): strKey is the hash_map key.
int RefRow_Read(CRefRow& row, RefRentItem& r, std::string& strKey);
int RefRow_Read(CRefRow& row, RefQuestReward& r);                // 0x499410
// RefOptionalTeleport::Read (0x4984e0): the ID, 0 out of service; an unknown region is logged.
int RefRow_Read(CRefRow& row, RefOptionalTeleport& r);
// RefQuest_LoadRow (0x497550, RefData_CopyRecord_497570 is the copy): the ID, 0 out of service.
int RefRec_Read(const CRefQuest& rec, RefQuest& r);
// RefGameWorld_CopyRow (0x4976d0, RefData_CopyRecord_4976E0 is the copy): the ID.
int RefRec_Read(const CRefGameWorld& rec, RefGameWorld& r);
// RefGachaNpcMap_CopyRow (0x497e80): NPC_ID; the Service column is copied, not checked.
int RefRec_Read(const CRefGachaNpcMap& rec, RefGachaNpcMapData& r);

// Opaque _RefObjCommon row (see InstanceRecords.h).
struct RefObjCommonRow;

// Rows of tables whose readers are not ported yet (opaque).
struct RefRegionRow;            // _RefRegion: the region name is the string at +0x34
struct RefSkillMasteryRow;      // _RefSkillMastery

// ---------------------------------------------------------------------------------------------
// CReferenceData

class CReferenceData
{
public:
    typedef std::map<DWORD, RefRegionRow*>                      RefRegionMap;
    typedef std::vector<RefLevelData*>                          RefLevelVector;
    typedef std::vector<RefInstanceWorldRegionData*>            RefInstanceWorldRegionVector;
    typedef std::vector<RefInstanceWorldStartPosData*>          RefInstanceWorldStartPosVector;
    typedef stdext::hash_map<int, RefObjCommonRow*>             RefObjHashMap;
    typedef stdext::hash_map<std::string, RefObjCommonRow*>     RefObjByCodeNameHashMap;
    typedef std::map<DWORD, RefSkillMasteryRow*>                RefSkillMasteryMap;
    typedef std::map<DWORD, RefSkill*>                          RefSkillMap;
    typedef std::map<DWORD, RefGachaNpcMapData*>                RefGachaNpcMapMap;
    typedef std::map<DWORD, RefGameWorld*>                      RefGameWorldMap;
    typedef stdext::hash_map<std::string, RefGameWorld*>        RefGameWorldByCodeNameHashMap;
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
    typedef std::map<DWORD, RefQuest*>                          RefQuestMap;
    typedef std::map<DWORD, RefServerEvent*>                    RefServerEventMap;
    typedef std::map<DWORD, RefServerEventReward*>              RefServerEventRewardMap;
    typedef std::multimap<DWORD, DWORD>                         RefServerEventRewardIdMap;
    typedef stdext::hash_map<std::string, RefRentItem*>         RefRentItemHashMap;
    typedef std::map<DWORD, RefQuestReward*>                    RefQuestRewardMap;
    typedef std::map<DWORD, RefOptionalTeleport*>               RefOptionalTeleportMap;

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
    // The lookups below return NULL when there is no such row.
    RefRegionRow* FindRegion(DWORD dwID);                               // 0x42db00
    RefGameWorld* FindGameWorld(DWORD dwID);                            // 0x42dc30
    // RefData_HashFindObjAt144 (0x42de30).
    RefObjCommonRow* FindRefObjByCodeName(const std::string& strCodeName);
    RefSkillMasteryRow* FindSkillMastery(DWORD dwID);                   // 0x46f7f0
    RefSkill* FindSkill(DWORD dwID);                                    // 0x46f920
    // CRefGameWorldMgr::FindByCodeName (0x41ae20).
    RefGameWorld* FindGameWorldByCodeName(const std::string& strCodeName);
    // CRefInstanceGenerator::FindServerEvent: NULL when there is none.
    RefServerEvent* FindServerEvent(DWORD dwID);
    // RefData_FindServerEventReward (0x47d980): NULL when there is none.
    RefServerEventReward* FindServerEventReward(DWORD dwID);

public:
    RefTableVector*             m_pTables;                          // +000
    RefRegionMap                m_mapRegion;                        // +004  by region ID
    RefLevelVector              m_vecLevel;                         // +010
    BYTE                        m_ab020[0x48];                      // +020  (not ported yet)
    RefObjHashMap               m_hashRefObj;                       // +068  _RefObjCommon by ID
    RefObjByCodeNameHashMap     m_hashRefObjByCodeName;             // +090
    BYTE                        m_ab0B8[0x58];                      // +0B8  (not ported yet)
    RefGachaNpcMapMap           m_mapGachaNpcMap;                   // +110  by NPC_ID
    BYTE                        m_ab11C[0x760];                     // +11C  (not ported yet)
    RefSkillMasteryMap          m_mapSkillMastery;                  // +87C
    RefSkillMap                 m_mapSkill;                         // +888
    BYTE                        m_ab894[0xC];                       // +894  (not ported yet)
    RefQuestMap                 m_mapQuest;                         // +8A0  by ID
    BYTE                        m_ab8AC[0x80];                      // +8AC  (not ported yet)
    RefGameWorldMap             m_mapGameWorld;                     // +92C  by ID
    RefGameWorldByCodeNameHashMap m_hashGameWorldByCodeName;        // +938
    RefInstanceWorldRegionVector m_vecInstanceWorldRegion;          // +960
    RefInstanceWorldStartPosVector m_vecInstanceWorldStartPos;      // +970
    BYTE                        m_ab980[0x7C];                      // +980  (not ported yet)
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
    RefOptionalTeleportMap      m_mapOptionalTeleport;              // +A8C  by ID
    RefShardContentConfigMap    m_mapShardContentConfig;            // +A98  by CodeName128
    RefGameWorldNPCMap          m_mapGameWorldNPC;                  // +AA4
    RefObjCharExtraSkillMap     m_mapObjCharExtraSkill;             // +AB0  by RefObjID
    RefGameWorldConfigMap       m_mapGameWorldConfig;               // +ABC  by GroupCodeName128
    BYTE                        m_abAC8[0x20];                      // +AC8  (not ported yet)
    RefServerEventMap           m_mapServerEvent;                   // +AE8  by ID
    RefServerEventRewardMap     m_mapServerEventReward;             // +AF4  by reward ID
    RefServerEventRewardIdMap   m_mmapServerEventRewardID;          // +B00  event ID -> reward ID
    RefRentItemHashMap          m_hashRentItem;                     // +B0C  by the key column
    RefQuestRewardMap           m_mapQuestReward;                   // +B34  by QuestID
    BYTE                        m_abB40[0x48];                      // +B40  (not ported yet)
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
C_ASSERT(FIELD_OFFSET(CReferenceData, m_hashRefObjByCodeName) == 0x90);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_vecLevel) == 0x10);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapGachaNpcMap) == 0x110);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_vecInstanceWorldStartPos) == 0x970);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapSkillMastery) == 0x87C);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapSkill) == 0x888);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapGameWorld) == 0x92C);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_hashGameWorldByCodeName) == 0x938);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapQuest) == 0x8A0);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_vecSiegeFortress) == 0x9FC);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_vecSiegeFortressBattleRank) == 0xA24);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapSiegeLvlSummonMonster) == 0xA44);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mmapSiegeQuestReward) == 0xA80);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapShardContentConfig) == 0xA98);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapGameWorldNPC) == 0xAA4);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapGameWorldConfig) == 0xABC);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapServerEvent) == 0xAE8);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_hashRentItem) == 0xB0C);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapQuestReward) == 0xB34);
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

// RefData_GetRegionName (0x486590): name of a region, "Unknown Region" for an unknown id.
const char* RefData_GetRegionName(WORD wRegionID);
