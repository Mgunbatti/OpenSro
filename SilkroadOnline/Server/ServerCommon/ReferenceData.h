#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>
#include <hash_map>
#include <hash_set>

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

// Parsed TAB_REFAISKILL row (44 bytes, InitMembers in CRefInstanceGenerator::LoadAISkill
// 0x4a4560).
struct RefAISkill
{
    DWORD            TacticsID;                  // +0   key of the multimap
    std::string      SkillCodeName;              // +4
    BYTE             b20;                        // +20
    DWORD            dw24;                       // +24
    DWORD            dw28;                       // +28
};

C_ASSERT(sizeof(RefAISkill) == 44);

int RefRow_Read(CRefRow& row, RefAISkill& r);                    // 0x4970e0
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
// RefData_CopyRowHeader_497760: the ID.
int RefRec_Read(const CRefClimate& rec, RefClimateData& r);
// RefGachaNpcMap_CopyRow (0x497e80): NPC_ID; the Service column is copied, not checked.
int RefRec_Read(const CRefGachaNpcMap& rec, RefGachaNpcMapData& r);

// Opaque _RefObjCommon row (see InstanceRecords.h).
struct RefObjCommonRow;

// _RefSkillMastery row (36 bytes).
typedef RefSkillMasteryData RefSkillMasteryRow;

// ---------------------------------------------------------------------------------------------
// Rows of tables whose readers are not ported yet: placeholders of the size the loader
// allocates. Replace each by the real row when its table is ported.

// _RefRegion (RefData_RecCtor_RefRegion 0x4dd630 / RecDtor 0x4dd520: 3 strings, 2 vectors); the
// region name is the string at +0x34.
struct RefRegionRow { BYTE ab[0xB0]; };                    // TODO(type)
// _RefTeleport (RecDtor 0x4dd380: string, words, vector of owned objects).
struct RefTeleportRow { BYTE ab[0x4C]; };                  // TODO(type)
// _RefEventZone (2 strings + string[5]).
struct RefEventZoneRow { BYTE ab[0xE0]; };                 // TODO(type)
// _RefEvent (2 strings).
struct RefEventRow { BYTE ab[0x3C]; };                     // TODO(type)
// _RefDefaultQuest (1 string).
struct RefDefaultQuestRow { BYTE ab[0x24]; };              // TODO(type)
// _RefGachaItemSet (RefGachaItemSet::dtor 0x497900: CGachaFreeParam +152, 4 param strings +24).
struct RefGachaItemSetRow { BYTE ab[0x9C]; };              // TODO(type)
// _RefAbility / _RefMagicOpt / _RefSkillByItemOptLevel (polymorphic, 0x18 / 0x14 bytes).
struct RefItemOptLevelRow { BYTE ab[0x18]; };              // TODO(type)
// _RefShopTab, _RefShop, _RefPackageItem, _RefShopGroup, _RefShopTabGroup.
struct RefShopTabRow { BYTE ab[0x58]; };                   // TODO(type)
struct RefShopRow { BYTE ab[0xD4]; };                      // TODO(type)
struct RefPackageItemRow { BYTE ab[0x188]; };              // TODO(type)
struct RefShopGroupRow { BYTE ab[0xBC]; };                 // TODO(type)
struct RefShopTabGroupRow { BYTE ab[0x54]; };              // TODO(type)
// The rows of the shop table of slot 36, which is never registered (RefData_AddRowRefShopSlot36
// 0x4d1c80, reader 0x494730).
struct RefShopSlot36Row
{
    DWORD            dw00;                       // +0   TODO(type)
    std::string      str04;                      // +4
};

C_ASSERT(sizeof(RefShopSlot36Row) == 0x20);

// _RefShopItemStockPeriod (2 strings).
struct RefShopItemStockPeriodRow { BYTE ab[0x68]; };       // TODO(type)
// _RefConditionTo{Buy,Sell}ScrapItem / _RefRewardPolicyTo{Buy,Sell}ScrapItem: 4 params at +8,
// condition block at +0x88 (RefScrapItemCondition::DestroyMembers 0x5150f0).
struct RefScrapItemRuleRow { BYTE ab[0xD0]; };             // TODO(type)
// _RefMappingShopGroup / _RefMappingShopWithTab (2 code name strings at +20 / +48).
struct RefMappingShopGroupRow { BYTE ab[0x4C]; };          // TODO(type)
struct RefMappingShopWithTabRow { BYTE ab[0x4C]; };        // TODO(type)
// _RefTactics (string at +152, map at +176), _RefNest, _RefHive (vector at +36).
struct RefTacticsRow { BYTE ab[0xBC]; };                   // TODO(type)
struct RefNestRow { BYTE ab[0x3C]; };                      // TODO(type)
struct RefHiveRow { BYTE ab[0x30]; };                      // TODO(type)
// _RefMonster_AssignedItemRndDrop, _RefDropItemGroup, _RefMagicOptGroup (vftable).
struct RefMonsterAssignedItemRndDropRow { BYTE ab[0x34]; }; // TODO(type)
struct RefDropItemGroupRow { BYTE ab[0x2C]; };             // TODO(type)
struct RefMagicOptGroupRow { BYTE ab[0x70]; };             // TODO(type)
// The 0xA0-byte position rows of CRefInstanceGenerator::LoadGameWorldPositions (0x4adb60,
// RefData_ReadPosRecord 0x498a70; dtor 0x50a420: string +8, std::string[3] at +72).
struct RefGameWorldPositionRow { BYTE ab[0xA0]; };         // TODO(type)
// _RefQuestRewardItems (4 strings), _RefEventReward (3 strings), _RefEventRewardItems (3
// strings).
struct RefQuestRewardItemsRow { BYTE ab[0x8C]; };          // TODO(type)
struct RefEventRewardRow { BYTE ab[0x7C]; };               // TODO(type)
struct RefEventRewardItemsRow { BYTE ab[0x6C]; };          // TODO(type)

// _RefSetItemGroup row (20 bytes): RefSetItemGroup::InitMembers 0x5145c0 / DestroyMembers
// 0x514570.
struct RefSetItemGroup
{
    RefSetItemGroup()
        : dw00(0), dw04(0)
    {
    }

    DWORD                   dw00;                // +0
    DWORD                   dw04;                // +4
    std::map<DWORD, DWORD>  map08;               // +8  TODO(type)
};

C_ASSERT(sizeof(RefSetItemGroup) == 0x14);

// _RefHWANLevel row (100 bytes): RefIdBytePairs::InitMembers 0x514bb0; up to 5 (ID, BYTE)
// pairs (RefData_ReadIdBytePairs5 0x497620).
struct RefIdBytePairs
{
    RefIdBytePairs()
    {
        vecPair.reserve(5);
    }

    std::vector<std::pair<DWORD, BYTE> > vecPair; // +0
    std::string             str10;               // +10
    std::string             str2C;               // +2C
    std::string             str48;               // +48
};

C_ASSERT(sizeof(RefIdBytePairs) == 0x64);

// _RefCollectionBook_Item row (152 bytes): RefCollectionBookItem::InitMembers 0x513770 /
// DestroyMembers 0x513670.
struct RefCollectionBookItem
{
    RefCollectionBookItem()
        : dw00(0), SlotIndex(0), ThemeID(0)
    {
    }

    DWORD            dw00;                       // +0
    std::string      CodeName;                   // +4   key of the item map
    std::string      ItemCodeName;               // +20
    std::string      ThemeCodeName;              // +3C
    DWORD            SlotIndex;                  // +58
    std::string      str5C;                      // +5C
    std::string      str78;                      // +78
    DWORD            ThemeID;                    // +94  set by ValidateCollectionBook
};

C_ASSERT(sizeof(RefCollectionBookItem) == 0x98);

// _RefCollectionBook_Theme row (140 bytes): RefCollectionBookTheme::InitMembers 0x4a7670 /
// DestroyMembers 0x5138f0.
struct RefCollectionBookTheme
{
    RefCollectionBookTheme()
        : dw00(0), ID(0), MaxSlot(0)
    {
    }

    DWORD            dw00;                       // +0
    DWORD            ID;                         // +4   key of the ID map
    std::string      CodeName;                   // +8   key of the theme map
    std::string      str24;                      // +24
    std::string      str40;                      // +40
    std::string      str5C;                      // +5C
    DWORD            MaxSlot;                    // +78
    std::vector<RefCollectionBookItem*> vecItem; // +7C  by slot index
};

C_ASSERT(sizeof(RefCollectionBookTheme) == 0x8C);

// ---------------------------------------------------------------------------------------------
// Containers that own their rows

// The member containers whose destructor deletes every mapped row before the container goes
// (the *_DeleteValues part of the member destructors 0x4ee670 ...).
template <class TBase>
class CRefOwnerMap : public TBase
{
public:
    ~CRefOwnerMap()
    {
        for (typename TBase::iterator it = this->begin(); it != this->end(); ++it)
            delete it->second;
        this->clear();
    }
};

// ---------------------------------------------------------------------------------------------
// CRefCharGen (_RefCharGenData, vftable 0x78d29c): the objects a new character may be created
// with, per race (3) and gender (2).

struct CRefCharGenGender
{
    std::vector<DWORD>      vecCharObjID;        // +0
    std::vector<DWORD>      vecItemObjID;        // +10
};

C_ASSERT(sizeof(CRefCharGenGender) == 0x20);

// CRefCharGen::RaceEntry_ctor (0x50bb40): two genders.
struct CRefCharGenRace
{
    CRefCharGenRace()
    {
        vecGender.resize(2);
    }

    std::vector<CRefCharGenGender> vecGender;    // +0
};

C_ASSERT(sizeof(CRefCharGenRace) == 0x10);

class CRefCharGen
{
public:
    // 0x50b450 (+ InitRaceVector 0x50b580): three races.
    CRefCharGen()
    {
        m_vecRace.resize(3);
    }

    // 0x4ef0e0 (vf00 0x4ef170 is the deleting destructor).
    virtual ~CRefCharGen()
    {
    }

    std::vector<CRefCharGenRace> m_vecRace;      // +4
    stdext::hash_set<int>   m_setObjID;          // +14  every object of the table
};

C_ASSERT(sizeof(CRefCharGen) == 0x3C);

// ---------------------------------------------------------------------------------------------
// Ranking packets (the object at CReferenceData+0x990)

// A packet buffer with a vftable (0x78d2a4, deleting destructor 0x4ec720).
class CStreamBuffer
{
public:
    CStreamBuffer()
        : m_pBuffer(NULL), m_nReadPos(0), m_nWritePos(0), m_nCapacity(0)
    {
    }

    virtual ~CStreamBuffer()
    {
        if (m_pBuffer)
        {
            operator delete(m_pBuffer);
            m_pBuffer = NULL;
        }
        m_nReadPos = 0;
        m_nWritePos = 0;
        m_nCapacity = 0;
        m_pBuffer = NULL;
    }

    BYTE*           m_pBuffer;                   // +4
    int             m_nReadPos;                  // +8
    int             m_nWritePos;                 // +C
    int             m_nCapacity;                 // +10
};

C_ASSERT(sizeof(CStreamBuffer) == 0x14);

// The ranking list packets per type (trader, robber, hunter), filled by
// BuildRankingActivityTable (0x4a9240) / BuildRankingContributionTable (0x4a9550).
struct RefRankingPackets
{
    // 0x4a8f10.
    RefRankingPackets();
    // 0x4a9130.
    ~RefRankingPackets();

    std::vector<CStreamBuffer*> m_vecActivity;   // +0
    std::vector<CStreamBuffer*> m_vecContribution; // +10
};

C_ASSERT(sizeof(RefRankingPackets) == 0x20);

// ---------------------------------------------------------------------------------------------
// Magic option and drop class tables (the object at CReferenceData+0x268, RefMagicOptData of
// the analysis; filled by slots 7, 8, 0x29..0x34)

// _RefDropOptLevelSel / _RefDropClassSel rows.
struct RefDropOptLevelSelRow { BYTE ab[0xC]; };            // TODO(type)
struct RefDropClassSelRow { BYTE ab[0xC]; };               // TODO(type) RefDropClassSel::Read
// The groups of magic options by code name (BuildMagicOptTable 0x51a780; dtor 0x4fa110:
// string +4, vectors +44 / +60) and the level entries (dtor 0x4fa460: owned object +16,
// vector +28).
struct RefMagicOptGroupByName { BYTE ab[0x4C]; };          // TODO(type)
struct RefMagicOptLevel { BYTE ab[0x28]; };                // TODO(type)
// The 20-byte value of DropClassEntry's first map.
struct RefDropClassProb { BYTE ab[0x14]; };                // TODO(type)

// DropClassEntry::ctor (0x51f0d0).
struct RefDropClassEntry
{
    RefDropClassEntry()
        : dwCount(0)
    {
    }

    DWORD                               dwCount;  // +0
    std::map<DWORD, RefDropClassProb>   mapProb;  // +4   TODO(type)
    std::map<DWORD, std::vector<RefObjCommonRow*> > mapItem; // +10  TODO(type)
};

C_ASSERT(sizeof(RefDropClassEntry) == 0x1C);

// RefMagicOptData::SlotGroup_ctor (0x50f0d0).
struct RefDropClassSlot
{
    std::vector<RefDropClassEntry> aEntry[3];    // +0
    DWORD                   dw30;                // +30  not initialised
};

C_ASSERT(sizeof(RefDropClassSlot) == 0x34);

enum
{
    REF_DROP_CLASS_SEL_COUNT = 11,
    REF_DROP_CLASS_SLOT_COUNT = 22,
};

class RefMagicOptTables
{
public:
    typedef CRefOwnerMap<std::map<DWORD, RefDropOptLevelSelRow*> > DropOptLevelSelMap;
    typedef CRefOwnerMap<std::map<DWORD, RefDropClassSelRow*> > DropClassSelMap;

    // 0x50eb40.
    RefMagicOptTables();
    // 0x4f87f0.
    ~RefMagicOptTables();

    DWORD                   m_adw00[36];         // +0   the first 23 are 8
    std::map<DWORD, DWORD>  m_map090;            // +90  TODO(type)
    DropOptLevelSelMap      m_mapDropOptLevelSel; // +9C  slot 0x29
    std::map<int, int>      m_mapLevelBand;      // +A8  level -> band (42 entries)
    DropClassSelMap         m_amapDropClassSel[REF_DROP_CLASS_SEL_COUNT]; // +B4  slots 0x2A..0x33
    RefDropClassSlot        m_aDropClass[REF_DROP_CLASS_SLOT_COUNT]; // +138  slot 0x34
    std::vector<RefMagicOptGroupByName*> m_vecMagicOptGroup; // +5B0  slot 7
    std::map<DWORD, RefMagicOptLevel*> m_mapMagicOptLevel; // +5C0  slot 7, by WORD id
};

C_ASSERT(FIELD_OFFSET(RefMagicOptTables, m_map090) == 0x90);
C_ASSERT(FIELD_OFFSET(RefMagicOptTables, m_mapLevelBand) == 0xA8);
C_ASSERT(FIELD_OFFSET(RefMagicOptTables, m_amapDropClassSel) == 0xB4);
C_ASSERT(FIELD_OFFSET(RefMagicOptTables, m_aDropClass) == 0x138);
C_ASSERT(FIELD_OFFSET(RefMagicOptTables, m_vecMagicOptGroup) == 0x5B0);
C_ASSERT(FIELD_OFFSET(RefMagicOptTables, m_mapMagicOptLevel) == 0x5C0);
C_ASSERT(sizeof(RefMagicOptTables) == 0x5CC);

// ---------------------------------------------------------------------------------------------
// CReferenceData

// Registers the object as g_pReferenceData before the members are built and clears it after
// they are gone (an empty base of the original: the check and store come first in 0x50a9b0,
// the clear last in 0x4ec7a0).
class CReferenceData;

class CReferenceDataInstance
{
protected:
    CReferenceDataInstance();
    ~CReferenceDataInstance();
};

class CReferenceData : public CReferenceDataInstance
{
public:
    typedef CRefOwnerMap<std::map<DWORD, RefRegionRow*> >        RefRegionMap;
    typedef std::vector<RefLevelData*>                          RefLevelVector;
    typedef CRefOwnerMap<std::map<DWORD, RefClimateData*> >      RefClimateMap;
    typedef std::vector<RefInstanceWorldRegionData*>            RefInstanceWorldRegionVector;
    typedef std::vector<RefInstanceWorldStartPosData*>          RefInstanceWorldStartPosVector;
    typedef stdext::hash_map<int, RefObjCommonRow*>             RefObjHashMap;
    typedef stdext::hash_map<std::string, RefObjCommonRow*>     RefObjByCodeNameHashMap;
    typedef CRefOwnerMap<std::map<DWORD, RefTeleportRow*> >      RefTeleportMap;
    typedef std::map<std::string, RefTeleportRow*>              RefTeleportByCodeNameMap;
    typedef CRefOwnerMap<std::map<DWORD, RefEventZoneRow*> >     RefEventZoneMap;
    typedef CRefOwnerMap<std::map<DWORD, RefEventRow*> >         RefEventMap;
    typedef CRefOwnerMap<std::map<DWORD, RefDefaultQuestRow*> >  RefDefaultQuestMap;
    typedef std::vector<RefGachaItemSetRow*>                    RefGachaItemSetVector;
    typedef CRefOwnerMap<std::map<DWORD, RefGachaItemSetRow*> >  RefWinLoseRewardSetMap;
    typedef CRefOwnerMap<std::map<DWORD, RefWinLoseRewardSetMap*> > RefWinLoseRewardMap;
    typedef CRefOwnerMap<std::map<DWORD, RefGachaNpcMapData*> >  RefGachaNpcMapMap;
    typedef CRefOwnerMap<std::map<DWORD, RefGachaCode*> >        RefGachaCodeMap;
    typedef std::vector<RefItemOptLevelRow*>                    RefItemOptLevelVector;
    typedef std::map<__int64, RefItemOptLevelVector>            RefItemOptLevelMap;
    typedef CRefOwnerMap<std::map<DWORD, RefSetItemGroup*> >     RefSetItemGroupMap;
    typedef CRefOwnerMap<std::map<DWORD, RefShopTabRow*> >       RefShopTabMap;
    typedef CRefOwnerMap<std::map<DWORD, RefShopRow*> >          RefShopMap;
    typedef CRefOwnerMap<std::map<DWORD, RefPackageItemRow*> >   RefPackageItemMap;
    typedef CRefOwnerMap<std::map<DWORD, RefShopGroupRow*> >     RefShopGroupMap;
    typedef CRefOwnerMap<std::map<DWORD, RefShopTabGroupRow*> >  RefShopTabGroupMap;
    typedef CRefOwnerMap<std::map<DWORD, RefShopSlot36Row*> >    RefShopSlot36Map;
    typedef CRefOwnerMap<std::map<DWORD, RefShopItemStockPeriodRow*> > RefShopItemStockPeriodMap;
    typedef std::map<DWORD, RefScrapItemRuleRow*>               RefScrapItemRuleMap;
    typedef std::vector<RefMappingShopGroupRow*>                RefMappingShopGroupVector;
    typedef std::vector<RefMappingShopWithTabRow*>              RefMappingShopWithTabVector;
    // Two-level indexes by an int key, then by code name (FindShopGroup 0x49e870 ...).
    typedef std::map<int, std::map<std::string, RefShopGroupRow*>*> RefShopGroupByCodeNameMap;
    typedef std::map<int, std::map<std::string, RefShopRow*>*>  RefShopByCodeNameMap;
    typedef std::map<int, std::map<std::string, RefPackageItemRow*>*> RefPackageItemByCodeNameMap;
    typedef std::map<int, std::map<std::string, RefShopTabRow*>*> RefShopTabByCodeNameMap;
    typedef std::map<int, std::map<std::string, RefShopTabGroupRow*>*>
        RefShopTabGroupByCodeNameMap;
    typedef CRefOwnerMap<std::map<DWORD, RefTacticsRow*> >       RefTacticsMap;
    typedef CRefOwnerMap<std::map<DWORD, RefNestRow*> >          RefNestMap;
    typedef CRefOwnerMap<std::map<DWORD, RefHiveRow*> >          RefHiveMap;
    typedef std::multimap<DWORD, RefAISkill*>                   RefAISkillMap;
    typedef std::vector<RefMonsterAssignedItemRndDropRow*>      RefMonsterAssignedItemRndDropVector;
    typedef std::map<DWORD, RefDropItemGroupRow*>               RefDropItemGroupMap;
    typedef std::map<DWORD, RefMagicOptGroupRow*>               RefMagicOptGroupMap;
    typedef CRefOwnerMap<std::map<DWORD, RefSkillMasteryRow*> >  RefSkillMasteryMap;
    typedef CRefOwnerMap<std::map<DWORD, RefSkill*> >            RefSkillMap;
    typedef std::map<std::string, RefSkill*>                    RefSkillByCodeNameMap;
    typedef CRefOwnerMap<std::map<DWORD, RefQuest*> >            RefQuestMap;
    typedef std::vector<RefObjCommonRow*>                       RefObjVector;
    // Monsters by level band (3) or by level (140).
    typedef std::vector<RefObjVector>                           RefObjListVector;
    typedef std::vector<RefIdBytePairs*>                        RefHWANLevelVector;
    // Siege guards / guild mercenaries by code name, 140 slots by level.
    typedef stdext::hash_map<std::string, RefObjVector*>        RefObjByLevelHashMap;
    typedef CRefOwnerMap<std::map<DWORD, RefGameWorld*> >        RefGameWorldMap;
    typedef stdext::hash_map<std::string, RefGameWorld*>        RefGameWorldByCodeNameHashMap;
    typedef std::vector<std::string*>                           RefRegionBindAssocServerVector;
    typedef std::map<std::string, RefObjVector>                 RefObjListByCodeNameMap;
    typedef std::map<DWORD, RefObjVector>                       RefObjListByIDMap;
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
    typedef std::vector<RefObjCommonRow*>                       RefSpecialItemVector;
    // Per game world: the positions by index.
    typedef std::map<BYTE, RefGameWorldPositionRow*>            RefGameWorldPositionMap;
    typedef std::vector<RefGameWorldPositionMap>                RefGameWorldPositionVector;
    typedef std::map<DWORD, RefServerEvent*>                    RefServerEventMap;
    typedef std::map<DWORD, RefServerEventReward*>              RefServerEventRewardMap;
    typedef std::multimap<DWORD, DWORD>                         RefServerEventRewardIdMap;
    typedef stdext::hash_map<std::string, RefRentItem*>         RefRentItemHashMap;
    typedef std::map<DWORD, RefQuestReward*>                    RefQuestRewardMap;
    // Quest ID -> BYTE -> the items in insertion order.
    typedef std::map<DWORD, RefQuestRewardItemsRow*>            RefQuestRewardItemList;
    typedef std::map<BYTE, RefQuestRewardItemList*>             RefQuestRewardItemGroupMap;
    typedef std::map<DWORD, RefQuestRewardItemGroupMap*>        RefQuestRewardItemsMap;
    typedef std::map<DWORD, RefEventRewardRow*>                 RefEventRewardMap;
    typedef std::map<DWORD, RefEventRewardItemsRow*>            RefEventRewardItemList;
    typedef std::map<DWORD, RefEventRewardItemList*>            RefEventRewardItemsMap;
    typedef std::map<DWORD, RefCollectionBookTheme*>            RefCollectionBookThemeByIDMap;
    typedef std::map<std::string, RefCollectionBookTheme*>      RefCollectionBookThemeMap;
    typedef std::map<std::string, RefCollectionBookItem*>       RefCollectionBookItemMap;
    typedef CRefOwnerMap<std::map<DWORD, RefOptionalTeleport*> > RefOptionalTeleportMap;

    // Static object of the original (dynamic initializer 0x50a9b0): sets g_pReferenceData.
    CReferenceData();
    // 0x4ec7a0 (the atexit destructor of the static object): frees the rows of the containers
    // that do not own them, then the members go in reverse order.
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
    // CReferenceData::GetShardContentConfigInt32 (0x65b810): appends the value of every
    // _RefShardContentConfig row of the code name whose CodeDesc128 equals strDesc and whose
    // Type is INT32; TRUE when vecValue is not empty afterwards.
    BOOL GetShardContentConfigInt32(const std::string& strCodeName, const std::string& strDesc,
                                    std::vector<int>& vecValue);

    // CReferenceData::ClearCollectionBook (0x50a830): frees the collection book items and
    // themes and empties the three collection book maps.
    void ClearCollectionBook();

public:
    RefTableVector*             m_pTables;                          // +000
    RefRegionMap                m_mapRegion;                        // +004  slot 3, by ID
    RefLevelVector              m_vecLevel;                         // +010  slot 0
    RefClimateMap               m_mapClimate;                       // +020  slot 1, by ID
    CRefCharGen                 m_RefCharGen;                       // +02C  slot 0x37
    RefObjHashMap               m_hashRefObj;                       // +068  slot 0xB, by ID
    RefObjByCodeNameHashMap     m_hashRefObjByCodeName;             // +090
    RefTeleportMap              m_mapTeleport;                      // +0B8  slot 0x12
    RefTeleportByCodeNameMap    m_mapTeleportByCodeName;            // +0C4
    RefEventZoneMap             m_mapEventZone;                     // +0D0  slot 0x14
    RefEventMap                 m_mapEvent;                         // +0DC  slot 0x54
    RefDefaultQuestMap          m_mapDefaultQuest;                  // +0E8  slot 0x58
    RefGachaItemSetVector       m_vecGachaItemSet;                  // +0F4  slot 0x47
    RefWinLoseRewardMap         m_mapWinLoseReward;                 // +104  slot 0x47
    RefGachaNpcMapMap           m_mapGachaNpcMap;                   // +110  slot 6, by NPC_ID
    RefGachaCodeMap             m_mapGachaCode;                     // +11C  slot 0x46
    RefItemOptLevelMap          m_mapItemOptLevel128;               // +128  slots 0xF..0x11
    RefItemOptLevelMap          m_mapItemOptLevel134;               // +134  slots 0xF..0x11
    RefSetItemGroupMap          m_mapSetItemGroup;                  // +140  slot 0xA
    RefShopTabMap               m_mapShopTab;                       // +14C  slot 0x27
    RefShopMap                  m_mapShop;                          // +158  slot 0x27
    RefPackageItemMap           m_mapPackageItem;                   // +164  slot 0x27
    RefShopGroupMap             m_mapShopGroup;                     // +170  slot 0x27
    RefShopTabGroupMap          m_mapShopTabGroup;                  // +17C  slot 0x27
    RefShopSlot36Map            m_mapShopSlot36;                    // +188  slot 0x24
    RefShopItemStockPeriodMap   m_mapShopItemStockPeriod;           // +194  slot 0x5B
    RefScrapItemRuleMap         m_mapConditionToBuyScrapItem;       // +1A0  slot 0x27 (0x1D)
    RefScrapItemRuleMap         m_mapRewardPolicyToSellScrapItem;   // +1AC  slot 0x27 (0x1E)
    RefScrapItemRuleMap         m_mapConditionToSellScrapItem;      // +1B8  slot 0x27 (0x1F)
    RefScrapItemRuleMap         m_mapRewardPolicyToBuyScrapItem;    // +1C4  slot 0x27 (0x1C)
    RefMappingShopGroupVector   m_vecMappingShopGroup;              // +1D0  slot 0x27
    RefMappingShopWithTabVector m_vecMappingShopWithTab;            // +1E0  slot 0x27
    RefShopGroupByCodeNameMap   m_mapShopGroupByCodeName;           // +1F0
    RefShopByCodeNameMap        m_mapShopByCodeName;                // +1FC
    RefPackageItemByCodeNameMap m_mapPackageItemByCodeName;         // +208
    RefShopTabByCodeNameMap     m_mapShopTabByCodeName;             // +214
    RefShopTabGroupByCodeNameMap m_mapShopTabGroupByCodeName;       // +220
    std::set<DWORD>             m_set22C;                           // +22C  TODO(type)
    RefTacticsMap               m_mapTactics;                       // +238  slot 0x3A
    RefNestMap                  m_mapNest;                          // +244  slot 0x3B
    RefHiveMap                  m_mapHive;                          // +250  slot 0x38
    RefAISkillMap               m_mmapAISkill;                      // +25C  slot 0x39
    RefMagicOptTables           m_MagicOpt;                         // +268  slots 7, 8, 0x29..0x34
    std::vector<DWORD>          m_vec834;                           // +834  TODO(type)
    std::vector<DWORD>          m_vec844;                           // +844  TODO(type)
    RefMonsterAssignedItemRndDropVector m_vecMonsterAssignedItemRndDrop; // +854  slot 0x36
    RefDropItemGroupMap         m_mapDropItemGroup;                 // +864  slot 0x35
    RefMagicOptGroupMap         m_mapMagicOptGroup;                 // +870  slot 9
    RefSkillMasteryMap          m_mapSkillMastery;                  // +87C  slot 4
    RefSkillMap                 m_mapSkill;                         // +888  slot 5
    RefSkillByCodeNameMap       m_mapSkillByCodeName;               // +894  slot 5
    RefQuestMap                 m_mapQuest;                         // +8A0  slot 0x3C, by ID
    RefObjListVector            m_vecThiefMonster;                  // +8AC  3 level bands
    RefObjListVector            m_vecHunterMonster;                 // +8BC  3 level bands
    RefHWANLevelVector          m_vecHWANLevel;                     // +8CC  slot 0x53
    RefObjByLevelHashMap        m_hashMercenary;                    // +8DC
    RefObjByLevelHashMap        m_hashSiegeGuarder;                 // +904
    RefGameWorldMap             m_mapGameWorld;                     // +92C  slot 0x3D, by ID
    RefGameWorldByCodeNameHashMap m_hashGameWorldByCodeName;        // +938
    RefInstanceWorldRegionVector m_vecInstanceWorldRegion;          // +960  slot 0x3E
    RefInstanceWorldStartPosVector m_vecInstanceWorldStartPos;      // +970  slot 0x3F
    RefRegionBindAssocServerVector m_vecRegionBindAssocServer;      // +980  slot 2
    RefRankingPackets           m_RankingPackets;                   // +990  slots 0x40..0x45
    RefObjListVector            m_vecMonsterByLevel;                // +9B0  140 levels
    RefObjListByCodeNameMap     m_mapTrapMonster;                   // +9C0
    RefObjListByIDMap           m_map9CC;                           // +9CC  TODO(type)
    RefObjListByIDMap           m_map9D8;                           // +9D8  TODO(type)
    RefObjListByIDMap           m_map9E4;                           // +9E4  TODO(type)
    RefObjListByIDMap           m_map9F0;                           // +9F0  TODO(type)
    RefSiegeFortressVector      m_vecSiegeFortress;                 // +9FC  slot 0x48
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
    RefOptionalTeleportMap      m_mapOptionalTeleport;              // +A8C  slot 0x59, by ID
    RefShardContentConfigMap    m_mapShardContentConfig;            // +A98  by CodeName128
    RefGameWorldNPCMap          m_mapGameWorldNPC;                  // +AA4  slot 0x5C
    RefObjCharExtraSkillMap     m_mapObjCharExtraSkill;             // +AB0  by RefObjID
    RefGameWorldConfigMap       m_mapGameWorldConfig;               // +ABC  by GroupCodeName128
    RefSpecialItemVector        m_vecSpecialItem;                   // +AC8  CountSpecialItems
    RefGameWorldPositionVector  m_vecGameWorldPosition;             // +AD8  slot 0x5F
    RefServerEventMap           m_mapServerEvent;                   // +AE8  by ID
    RefServerEventRewardMap     m_mapServerEventReward;             // +AF4  by reward ID
    RefServerEventRewardIdMap   m_mmapServerEventRewardID;          // +B00  event ID -> reward ID
    RefRentItemHashMap          m_hashRentItem;                     // +B0C  by the key column
    RefQuestRewardMap           m_mapQuestReward;                   // +B34  by QuestID
    RefQuestRewardItemsMap      m_mapQuestRewardItems;              // +B40  slot 0x65
    RefEventRewardMap           m_mapEventReward;                   // +B4C  slot 0x66
    RefEventRewardItemsMap      m_mapEventRewardItems;              // +B58  slot 0x67
    RefCollectionBookThemeByIDMap m_mapCollectionBookThemeByID;     // +B64  ValidateCollectionBook
    RefCollectionBookThemeMap   m_mapCollectionBookTheme;           // +B70  slot 0x68
    RefCollectionBookItemMap    m_mapCollectionBookItem;            // +B7C  slot 0x69
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

#define REF_MEMBER_AT(member, offset) C_ASSERT(FIELD_OFFSET(CReferenceData, member) == offset)
REF_MEMBER_AT(m_mapRegion, 0x004);
REF_MEMBER_AT(m_vecLevel, 0x010);
REF_MEMBER_AT(m_mapClimate, 0x020);
REF_MEMBER_AT(m_RefCharGen, 0x02C);
REF_MEMBER_AT(m_hashRefObj, 0x068);
REF_MEMBER_AT(m_hashRefObjByCodeName, 0x090);
REF_MEMBER_AT(m_mapTeleport, 0x0B8);
REF_MEMBER_AT(m_mapTeleportByCodeName, 0x0C4);
REF_MEMBER_AT(m_mapEventZone, 0x0D0);
REF_MEMBER_AT(m_mapEvent, 0x0DC);
REF_MEMBER_AT(m_mapDefaultQuest, 0x0E8);
REF_MEMBER_AT(m_vecGachaItemSet, 0x0F4);
REF_MEMBER_AT(m_mapWinLoseReward, 0x104);
REF_MEMBER_AT(m_mapGachaNpcMap, 0x110);
REF_MEMBER_AT(m_mapGachaCode, 0x11C);
REF_MEMBER_AT(m_mapItemOptLevel128, 0x128);
REF_MEMBER_AT(m_mapItemOptLevel134, 0x134);
REF_MEMBER_AT(m_mapSetItemGroup, 0x140);
REF_MEMBER_AT(m_mapShopTab, 0x14C);
REF_MEMBER_AT(m_mapShop, 0x158);
REF_MEMBER_AT(m_mapPackageItem, 0x164);
REF_MEMBER_AT(m_mapShopGroup, 0x170);
REF_MEMBER_AT(m_mapShopTabGroup, 0x17C);
REF_MEMBER_AT(m_mapShopSlot36, 0x188);
REF_MEMBER_AT(m_mapShopItemStockPeriod, 0x194);
REF_MEMBER_AT(m_mapConditionToBuyScrapItem, 0x1A0);
REF_MEMBER_AT(m_mapRewardPolicyToSellScrapItem, 0x1AC);
REF_MEMBER_AT(m_mapConditionToSellScrapItem, 0x1B8);
REF_MEMBER_AT(m_mapRewardPolicyToBuyScrapItem, 0x1C4);
REF_MEMBER_AT(m_vecMappingShopGroup, 0x1D0);
REF_MEMBER_AT(m_vecMappingShopWithTab, 0x1E0);
REF_MEMBER_AT(m_mapShopGroupByCodeName, 0x1F0);
REF_MEMBER_AT(m_mapShopByCodeName, 0x1FC);
REF_MEMBER_AT(m_mapPackageItemByCodeName, 0x208);
REF_MEMBER_AT(m_mapShopTabByCodeName, 0x214);
REF_MEMBER_AT(m_mapShopTabGroupByCodeName, 0x220);
REF_MEMBER_AT(m_set22C, 0x22C);
REF_MEMBER_AT(m_mapTactics, 0x238);
REF_MEMBER_AT(m_mapNest, 0x244);
REF_MEMBER_AT(m_mapHive, 0x250);
REF_MEMBER_AT(m_mmapAISkill, 0x25C);
REF_MEMBER_AT(m_MagicOpt, 0x268);
REF_MEMBER_AT(m_vec834, 0x834);
REF_MEMBER_AT(m_vec844, 0x844);
REF_MEMBER_AT(m_vecMonsterAssignedItemRndDrop, 0x854);
REF_MEMBER_AT(m_mapDropItemGroup, 0x864);
REF_MEMBER_AT(m_mapMagicOptGroup, 0x870);
REF_MEMBER_AT(m_mapSkillMastery, 0x87C);
REF_MEMBER_AT(m_mapSkill, 0x888);
REF_MEMBER_AT(m_mapSkillByCodeName, 0x894);
REF_MEMBER_AT(m_mapQuest, 0x8A0);
REF_MEMBER_AT(m_vecThiefMonster, 0x8AC);
REF_MEMBER_AT(m_vecHunterMonster, 0x8BC);
REF_MEMBER_AT(m_vecHWANLevel, 0x8CC);
REF_MEMBER_AT(m_hashMercenary, 0x8DC);
REF_MEMBER_AT(m_hashSiegeGuarder, 0x904);
REF_MEMBER_AT(m_mapGameWorld, 0x92C);
REF_MEMBER_AT(m_hashGameWorldByCodeName, 0x938);
REF_MEMBER_AT(m_vecInstanceWorldRegion, 0x960);
REF_MEMBER_AT(m_vecInstanceWorldStartPos, 0x970);
REF_MEMBER_AT(m_vecRegionBindAssocServer, 0x980);
REF_MEMBER_AT(m_RankingPackets, 0x990);
REF_MEMBER_AT(m_vecMonsterByLevel, 0x9B0);
REF_MEMBER_AT(m_mapTrapMonster, 0x9C0);
REF_MEMBER_AT(m_map9CC, 0x9CC);
REF_MEMBER_AT(m_map9D8, 0x9D8);
REF_MEMBER_AT(m_map9E4, 0x9E4);
REF_MEMBER_AT(m_map9F0, 0x9F0);
REF_MEMBER_AT(m_vecSiegeFortress, 0x9FC);
REF_MEMBER_AT(m_mmapSiegeFortressGuard, 0xA0C);
REF_MEMBER_AT(m_mmapSiegeFortressItemForge, 0xA18);
REF_MEMBER_AT(m_vecSiegeFortressBattleRank, 0xA24);
REF_MEMBER_AT(m_vecSiegeStructUpgrade, 0xA34);
REF_MEMBER_AT(m_mapSiegeLvlSummonMonster, 0xA44);
REF_MEMBER_AT(m_mmapSiegeFortressRewards, 0xA50);
REF_MEMBER_AT(m_mapSiegeBlessBuff, 0xA5C);
REF_MEMBER_AT(m_mapSiegeDungeon, 0xA68);
REF_MEMBER_AT(m_mmapSiegeQuest, 0xA74);
REF_MEMBER_AT(m_mmapSiegeQuestReward, 0xA80);
REF_MEMBER_AT(m_mapOptionalTeleport, 0xA8C);
REF_MEMBER_AT(m_mapShardContentConfig, 0xA98);
REF_MEMBER_AT(m_mapGameWorldNPC, 0xAA4);
REF_MEMBER_AT(m_mapObjCharExtraSkill, 0xAB0);
REF_MEMBER_AT(m_mapGameWorldConfig, 0xABC);
REF_MEMBER_AT(m_vecSpecialItem, 0xAC8);
REF_MEMBER_AT(m_vecGameWorldPosition, 0xAD8);
REF_MEMBER_AT(m_mapServerEvent, 0xAE8);
REF_MEMBER_AT(m_mapServerEventReward, 0xAF4);
REF_MEMBER_AT(m_mmapServerEventRewardID, 0xB00);
REF_MEMBER_AT(m_hashRentItem, 0xB0C);
REF_MEMBER_AT(m_mapQuestReward, 0xB34);
REF_MEMBER_AT(m_mapQuestRewardItems, 0xB40);
REF_MEMBER_AT(m_mapEventReward, 0xB4C);
REF_MEMBER_AT(m_mapEventRewardItems, 0xB58);
REF_MEMBER_AT(m_mapCollectionBookThemeByID, 0xB64);
REF_MEMBER_AT(m_mapCollectionBookTheme, 0xB70);
REF_MEMBER_AT(m_mapCollectionBookItem, 0xB7C);
REF_MEMBER_AT(m_mapUIString, 0xB88);
REF_MEMBER_AT(m_mmapGameWorldBindTriggerCategory, 0xB94);
REF_MEMBER_AT(m_mapTriggerCategory, 0xBA0);
REF_MEMBER_AT(m_mmapTriggerCategoryBindTrigger, 0xBAC);
REF_MEMBER_AT(m_mapTrigger, 0xBB8);
REF_MEMBER_AT(m_mmapTriggerVariable, 0xBC4);
REF_MEMBER_AT(m_mmapTriggerBindEvent, 0xBD0);
REF_MEMBER_AT(m_mmapTriggerBindCondition, 0xBDC);
REF_MEMBER_AT(m_mmapTriggerBindAction, 0xBE8);
REF_MEMBER_AT(m_mapTriggerCommon, 0xBF4);
REF_MEMBER_AT(m_mapTriggerEvent, 0xC00);
REF_MEMBER_AT(m_mapTriggerCondition, 0xC0C);
REF_MEMBER_AT(m_mapTriggerAction, 0xC18);
REF_MEMBER_AT(m_mapTriggerConditionParam, 0xC24);
REF_MEMBER_AT(m_mapTriggerActionParam, 0xC30);
REF_MEMBER_AT(m_mmapGameWorldBindGameWorldGroup, 0xC3C);
REF_MEMBER_AT(m_mapGameWorldGroup, 0xC48);
REF_MEMBER_AT(m_mapGameWorldGroupConfig, 0xC54);
#undef REF_MEMBER_AT
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
