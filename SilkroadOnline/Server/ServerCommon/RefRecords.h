#pragma once

#include <string>

#include "BSLib/DB/Database.h"

// Reference record classes of SR_ShardManager (ServerCommon; CShardDB::RegisterRefTables opens one
// CDBTable per class, CTableKeeper::RegisterTable_<T> gives it a CCustomAllocator<T, ...>).
//
// Every class has the CBase slots 0 / 1, slot 2 GetTableName and slot 3 GetTableInfo (its static
// CDBTableInfo); slots 4..8 are the August 2011 defaults of CDBRecord / CDBRecStream. The table
// descriptions are set up by a dynamic initializer per class (InitTableInfo, the
// StaticInit_TableInfo functions of the analysis): table name, flags DBTABLE_04, the two empty
// field groups 0 and 1 (so CDBTable::DescribeColumns takes every column of the table into group
// 1 with offsets of its own), the record class and m_nDataOffset.
//
// Two kinds of record:
// - CDBRecord records (m_nDataOffset 24): the columns land in the members after the CDBRecord
//   part, in table order with natural alignment (the offsets DescribeColumns computes), so the
//   members are the August 2011 table layout;
// - CDBRecStream records (m_nDataOffset 48, not used by their GetData): the columns are appended
//   to the stream buffer and parsed by the reference data readers. Some classes keep a parsed
//   row (m_Row) or bytes no constructor touches after the CDBRecStream part.

// Declares the runtime class with its factory, the table description and slots 2 / 3.
#define SR_DECLARE_REF_RECORD(class_name)                                                  \
    BS_DECLARE_DYNCREATE(class_name)                                                        \
public:                                                                                     \
    static CDBTableInfo s_TableInfo;                                                        \
    static int InitTableInfo();                                                             \
    virtual const char* GetTableName();                                                     \
    virtual CDBTableInfo* GetTableInfo();

// _RefSkill row (560 bytes): RefSkill::InitMembers 0x4a43d0 / DestroyMembers 0x515670 are the
// implicit constructor / destructor; RefSkill::CopyFrom (0x4971d0) shows the offsets.
struct RefSkill
{
    BYTE             Service;                    // +0
    int              ID;                         // +4
    int              GroupID;                    // +8
    std::string      Basic_Code;                 // +C
    std::string      Basic_Name;                 // +28
    std::string      Basic_Group;                // +44
    int              Basic_Original;             // +60
    BYTE             Basic_Level;                // +64
    BYTE             Basic_Activity;             // +65
    int              Basic_ChainCode;            // +68
    int              Basic_RecycleCost;          // +6C
    int              Action_PreparingTime;       // +70
    int              Action_CastingTime;         // +74
    int              Action_ActionDuration;      // +78
    int              Action_ReuseDelay;          // +7C
    int              Action_CoolTime;            // +80
    int              Action_FlyingSpeed;         // +84
    BYTE             Action_Interruptable;       // +88
    int              Action_Overlap;             // +8C
    BYTE             Action_AutoAttackType;      // +90
    BYTE             Action_InTown;              // +91
    short            Action_Range;               // +92
    BYTE             Target_Required;            // +94
    BYTE             TargetType_Animal;          // +95
    BYTE             TargetType_Land;            // +96
    BYTE             TargetType_Building;        // +97
    BYTE             TargetGroup_Self;           // +98
    BYTE             TargetGroup_Ally;           // +99
    BYTE             TargetGroup_Party;          // +9A
    BYTE             TargetGroup_Enemy_M;        // +9B
    BYTE             TargetGroup_Enemy_P;        // +9C
    BYTE             TargetGroup_Neutral;        // +9D
    BYTE             TargetGroup_DontCare;       // +9E
    BYTE             TargetEtc_SelectDeadBody;   // +9F
    int              ReqCommon_Mastery1;         // +A0
    int              ReqCommon_Mastery2;         // +A4
    BYTE             ReqCommon_MasteryLevel1;    // +A8
    BYTE             ReqCommon_MasteryLevel2;    // +A9
    short            ReqCommon_Str;              // +AA
    short            ReqCommon_Int;              // +AC
    int              ReqLearn_Skill1;            // +B0
    int              ReqLearn_Skill2;            // +B4
    int              ReqLearn_Skill3;            // +B8
    BYTE             ReqLearn_SkillLevel1;       // +BC
    BYTE             ReqLearn_SkillLevel2;       // +BD
    BYTE             ReqLearn_SkillLevel3;       // +BE
    int              ReqLearn_SP;                // +C0
    BYTE             ReqLearn_Race;              // +C4
    BYTE             Req_Restriction1;           // +C5
    BYTE             Req_Restriction2;           // +C6
    BYTE             ReqCast_Weapon1;            // +C7
    BYTE             ReqCast_Weapon2;            // +C8
    short            Consume_HP;                 // +CA
    short            Consume_MP;                 // +CC
    short            Consume_HPRatio;            // +CE
    short            Consume_MPRatio;            // +D0
    BYTE             Consume_WHAN;               // +D2
    BYTE             UI_SkillTab;                // +D3
    BYTE             UI_SkillPage;               // +D4
    BYTE             UI_SkillColumn;             // +D5
    BYTE             UI_SkillRow;                // +D6
    std::string      UI_IconFile;                // +D8
    std::string      UI_SkillName;               // +F4
    std::string      UI_SkillToolTip;            // +110
    std::string      UI_SkillToolTip_Desc;       // +12C
    std::string      UI_SkillStudy_Desc;         // +148
    short            AI_AttackChance;            // +164
    BYTE             AI_SkillType;               // +166
    int              Param[50];                  // +168  Param1..Param50
};

C_ASSERT(sizeof(RefSkill) == 560);

// AvailItemGroupN / ReqClassN pair of _RefMagicOpt (0x52e9d0 / 0x52ea20).
struct RefMagicOptParam
{
    std::string      AvailItemGroup;             // +0
    int              ReqClass;                   // +1C
};

C_ASSERT(sizeof(RefMagicOptParam) == 32);

// _RefMagicOpt row (484 bytes): RefMagicOpt::InitMembers 0x52e7d0 / DestroyMembers 0x52e850.
struct RefMagicOpt
{
    int              Service;                    // +0
    short            ID;                         // +4
    std::string      MOptName128;                // +8
    std::string      AttrType;                   // +24
    int              MLevel;                     // +40
    float            Prob;                       // +44
    int              Weight;                     // +48
    int              Param[16];                  // +4C  Param1..Param16
    int              ExcFunc[6];                 // +8C  ExcFunc1..ExcFunc6
    RefMagicOptParam AvailItem[10];              // +A4  AvailItemGroupN / ReqClassN
};

C_ASSERT(sizeof(RefMagicOpt) == 484);

// _RefMagicOptAssign row (708 bytes): RefMagicOptAssign::InitMembers 0x52e900 / DestroyMembers
// 0x52e960.
struct RefMagicOptAssign
{
    int              Service;                    // +0
    BYTE             Race;                       // +4
    BYTE             TID3;                       // +5
    BYTE             TID4;                       // +6
    std::string      AvailMOpt[25];              // +8  AvailMOpt1..AvailMOpt25
};

C_ASSERT(sizeof(RefMagicOptAssign) == 708);

// Parsed _RefOptionalTeleport row (64 bytes): RefData_RecCtor_RefOptionalTeleport 0x4d3340,
// RefOptionalTeleport::Read 0x4984e0.
struct RefOptionalTeleport
{
    RefOptionalTeleport();

    int              ID;                         // +0
    std::string      ObjName128;                 // +4
    DWORD            dwRegion;                   // +20  region value looked up by RegionID
    WORD             w24;                        // +24
    WORD             Pos_X;                      // +26
    WORD             Pos_Y;                      // +28
    WORD             Pos_Z;                      // +2A
    WORD             WorldID;                    // +2C
    WORD             LevelMin;                   // +2E
    WORD             LevelMax;                   // +30
    DWORD            adw34[3];                   // +34  three later columns
};

C_ASSERT(sizeof(RefOptionalTeleport) == 64);

// _RefQuest row (236 bytes): RefData_RecCtor_RefQuest 0x4d3200 / RefData_RecDtor_RefQuest
// 0x4dcce0.
struct RefQuest
{
    BYTE             Service;                    // +0
    int              ID;                         // +4
    std::string      CodeName;                   // +8
    BYTE             Level;                      // +24
    std::string      DescName;                   // +28
    std::string      NameString;                 // +44
    std::string      PayString;                  // +60
    std::string      ContentsString;             // +7C
    std::string      PayContents;                // +98
    std::string      NoticeNPC;                  // +B4
    std::string      NoticeCondition;            // +D0
};

C_ASSERT(sizeof(RefQuest) == 236);

// _RefGame_World row (80 bytes): RefData_RecCtor_RefGameWorld 0x4d32e0 /
// RefData_RecDtor_RefGameWorld 0x4dcc50.
struct RefGameWorld
{
    int              ID;                         // +0
    std::string      WorldCodeName128;           // +4
    BYTE             Type;                       // +20
    short            WorldMaxCount;              // +22
    short            WorldMaxUserCount;          // +24
    BYTE             WorldEntryType;             // +26
    BYTE             WorldEntranceType;          // +27
    BYTE             WorldLeaveType;             // +28
    int              WorldDurationTime;          // +2C
    int              WorldEmptyRemainTime;       // +30
    std::string      ConfigGroupCodeName128;     // +34
};

C_ASSERT(sizeof(RefGameWorld) == 80);

// Parsed _RefShardContentConfig row (116 bytes, RefKeyValue4Str::Read 0x49a680): InitMembers
// 0x4a6230 / DestroyMembers 0x4a6320.
struct RefShardContentConfig
{
    int              ID;                         // +0
    std::string      CodeName128;                // +4
    std::string      CodeDesc128;                // +20
    std::string      Value;                      // +3C
    std::string      Type;                       // +58
};

C_ASSERT(sizeof(RefShardContentConfig) == 116);

// Parsed _RefUIString_Mt row (116 bytes, RefKeyValue4Str::Read 0x49a680): InitMembers 0x4a7950
// / DestroyMembers 0x4a7a40.
struct RefUIString
{
    int              ID;                         // +0
    std::string      GroupCodeName128;           // +4
    std::string      ValueCodeName128;           // +20
    std::string      Value;                      // +3C
    std::string      Type;                       // +58
};

C_ASSERT(sizeof(RefUIString) == 116);

// _RefGachaCode row (32 bytes): RefData_RecCtor_RefGachaCode 0x4dcbe0.
struct RefGachaCode
{
    RefGachaCode();

    std::string      CodeName128;                // +0
    int              GachaSetID;                 // +1C
};

C_ASSERT(sizeof(RefGachaCode) == 32);

// ---------------------------------------------------------------------------------------------
// Object, skill and magic option records

// Base of the item / character / structure records (no factory, slots 2 and 3 pure).
class CRefObjDerived : public CDBRecStream
{
    BS_DECLARE_RUNTIME_CLASS(CRefObjDerived)

public:
    CRefObjDerived();
    virtual ~CRefObjDerived();
};

C_ASSERT(sizeof(CRefObjDerived) == 0x30);

// _REFREGION
class CRefRegion : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefRegion)

public:
    CRefRegion();
    virtual ~CRefRegion();

public:
    DWORD            m_adw30[2];                 // +30  not initialised
};

C_ASSERT(sizeof(CRefRegion) == 0x38);

// _REFLEVEL
class CRefLevel : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefLevel)

public:
    CRefLevel();
    virtual ~CRefLevel();

public:
    BYTE             Lvl;                        // +18
    __int64          Exp_C;                      // +20
    int              Exp_M;                      // +28
    int              Cost_M;                     // +2C
    int              Cost_ST;                    // +30
    int              GUST_Mob_Exp;               // +34
    int              JobExp_Trader;              // +38
    int              JobExp_Robber;              // +3C
    int              JobExp_Hunter;              // +40
};

C_ASSERT(sizeof(CRefLevel) == 0x48);
C_ASSERT(FIELD_OFFSET(CRefLevel, Lvl) == 24);

// _REFCLIMATE
class CRefClimate : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefClimate)

public:
    CRefClimate();
    virtual ~CRefClimate();

public:
    int              ID;                         // +18
    BYTE             InitialWeather;             // +1C
    BYTE             InitialAmount;              // +1D
    BYTE             ChangeWeather;              // +1E
    BYTE             Division;                   // +1F
    int              Duration;                   // +20
    int              DurationVariance;           // +24
    BYTE             Snowfall;                   // +28
    BYTE             SnowfallVariance;           // +29
    BYTE             ProbSnow;                   // +2A
    BYTE             Rainfall;                   // +2B
    BYTE             RainfallVariance;           // +2C
    BYTE             ProbRain;                   // +2D
};

C_ASSERT(sizeof(CRefClimate) == 0x30);
C_ASSERT(FIELD_OFFSET(CRefClimate, ID) == 24);

// _REFREGIONBINDASSOCSERVER
class CRefRegionBindAssocServer : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefRegionBindAssocServer)

public:
    CRefRegionBindAssocServer();
    virtual ~CRefRegionBindAssocServer();

public:
    std::string      AreaName;                   // +18
    int              AssocServer;                // +34
};

C_ASSERT(sizeof(CRefRegionBindAssocServer) == 0x38);
C_ASSERT(FIELD_OFFSET(CRefRegionBindAssocServer, AreaName) == 24);

// _REFCHARGEN
class CRefCharGenData : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefCharGenData)

public:
    CRefCharGenData();
    virtual ~CRefCharGenData();
};

C_ASSERT(sizeof(CRefCharGenData) == 0x30);

// _REFOBJCOMMON
class CRefObjCommon : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefObjCommon)

public:
    CRefObjCommon();
    virtual ~CRefObjCommon();
};

C_ASSERT(sizeof(CRefObjCommon) == 0x30);

// _REFOBJITEM
class CRefObjItem : public CRefObjDerived
{
    SR_DECLARE_REF_RECORD(CRefObjItem)

public:
    CRefObjItem();
    virtual ~CRefObjItem();
};

C_ASSERT(sizeof(CRefObjItem) == 0x30);

// _REFOBJCHAR
class CRefObjChar : public CRefObjDerived
{
    SR_DECLARE_REF_RECORD(CRefObjChar)

public:
    CRefObjChar();
    virtual ~CRefObjChar();
};

C_ASSERT(sizeof(CRefObjChar) == 0x30);

// _REFOBJSTRUCT
class CRefObjStruct : public CRefObjDerived
{
    SR_DECLARE_REF_RECORD(CRefObjStruct)

public:
    CRefObjStruct();
    virtual ~CRefObjStruct();
};

C_ASSERT(sizeof(CRefObjStruct) == 0x30);

// _REFOBJCHAREXTRASKILL
class CRefObjCharExtraSkill : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefObjCharExtraSkill)

public:
    CRefObjCharExtraSkill();
    virtual ~CRefObjCharExtraSkill();
};

C_ASSERT(sizeof(CRefObjCharExtraSkill) == 0x30);

// _REFSKILL
class CRefSkill : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefSkill)

public:
    CRefSkill();
    virtual ~CRefSkill();

public:
    RefSkill         m_Data;                     // +18  the row (GetData)
};

C_ASSERT(sizeof(CRefSkill) == 0x248);
C_ASSERT(FIELD_OFFSET(CRefSkill, m_Data) == 24);

// _REFSKILLMASTERY
class CRefSkillMastery : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefSkillMastery)

public:
    CRefSkillMastery();
    virtual ~CRefSkillMastery();

public:
    int              ID;                         // +18
    std::string      Code;                       // +1C
    BYTE             Weapon;                     // +38
};

C_ASSERT(sizeof(CRefSkillMastery) == 0x40);
C_ASSERT(FIELD_OFFSET(CRefSkillMastery, ID) == 24);

// _REFMAGICOPT
class CRefMagicOpt : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefMagicOpt)

public:
    CRefMagicOpt();
    virtual ~CRefMagicOpt();

public:
    RefMagicOpt      m_Data;                     // +18  the row (GetData)
};

C_ASSERT(sizeof(CRefMagicOpt) == 0x200);
C_ASSERT(FIELD_OFFSET(CRefMagicOpt, m_Data) == 24);

// _REFMAGICOPTASSIGN
class CRefMagicOptAssign : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefMagicOptAssign)

public:
    CRefMagicOptAssign();
    virtual ~CRefMagicOptAssign();

public:
    RefMagicOptAssign m_Data;                     // +18  the row (GetData)
};

C_ASSERT(sizeof(CRefMagicOptAssign) == 0x2E0);
C_ASSERT(FIELD_OFFSET(CRefMagicOptAssign, m_Data) == 24);

// _REFMAGICOPTGROUP
class CRefMagicOptGroup : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefMagicOptGroup)

public:
    CRefMagicOptGroup();
    virtual ~CRefMagicOptGroup();
};

C_ASSERT(sizeof(CRefMagicOptGroup) == 0x30);

// _REFSETITEMGROUP
class CRefSetItemGroup : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefSetItemGroup)

public:
    CRefSetItemGroup();
    virtual ~CRefSetItemGroup();
};

C_ASSERT(sizeof(CRefSetItemGroup) == 0x30);

// _REFABILITYBYITEMOPTLEVEL
class CRefAbilityByItemOptLevel : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefAbilityByItemOptLevel)

public:
    CRefAbilityByItemOptLevel();
    virtual ~CRefAbilityByItemOptLevel();

public:
    DWORD            m_adw30[2];                 // +30  not initialised
};

C_ASSERT(sizeof(CRefAbilityByItemOptLevel) == 0x38);

// _REFMAGICOPTBYITEMOPTLEVEL
class CRefMagicOptByItemOptLevel : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefMagicOptByItemOptLevel)

public:
    CRefMagicOptByItemOptLevel();
    virtual ~CRefMagicOptByItemOptLevel();
};

C_ASSERT(sizeof(CRefMagicOptByItemOptLevel) == 0x30);

// _REFSKILLBYITEMOPTLEVEL
class CRefSkillByItemOptLevel : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefSkillByItemOptLevel)

public:
    CRefSkillByItemOptLevel();
    virtual ~CRefSkillByItemOptLevel();
};

C_ASSERT(sizeof(CRefSkillByItemOptLevel) == 0x30);

// _REFALCHEMYMERIT
class CRefAlchemyMerit : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefAlchemyMerit)

public:
    CRefAlchemyMerit();
    virtual ~CRefAlchemyMerit();
};

C_ASSERT(sizeof(CRefAlchemyMerit) == 0x30);

// _REFHWANLEVEL
class CRefHWANLevel : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefHWANLevel)

public:
    CRefHWANLevel();
    virtual ~CRefHWANLevel();
};

C_ASSERT(sizeof(CRefHWANLevel) == 0x30);

// TAB_REFAISKILL
class CRefAISkill : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefAISkill)

public:
    CRefAISkill();
    virtual ~CRefAISkill();
};

C_ASSERT(sizeof(CRefAISkill) == 0x30);

// TAB_REFHIVE
class CRefHive : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefHive)

public:
    CRefHive();
    virtual ~CRefHive();
};

C_ASSERT(sizeof(CRefHive) == 0x30);

// TAB_REFTACTICS
class CRefTactics : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTactics)

public:
    CRefTactics();
    virtual ~CRefTactics();
};

C_ASSERT(sizeof(CRefTactics) == 0x30);

// TAB_REFNEST
class CRefNest : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefNest)

public:
    CRefNest();
    virtual ~CRefNest();
};

C_ASSERT(sizeof(CRefNest) == 0x30);

// ---------------------------------------------------------------------------------------------
// World, teleport, quest and event records

// _REFTELEPORT
class CRefTeleport : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTeleport)

public:
    CRefTeleport();
    virtual ~CRefTeleport();
};

C_ASSERT(sizeof(CRefTeleport) == 0x30);

// _REFTELELINK
class CRefTeleLink : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTeleLink)

public:
    CRefTeleLink();
    virtual ~CRefTeleLink();
};

C_ASSERT(sizeof(CRefTeleLink) == 0x30);

// _REFOPTIONALTELEPORT
class CRefOptionalTeleport : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefOptionalTeleport)

public:
    CRefOptionalTeleport();
    virtual ~CRefOptionalTeleport();

public:
    RefOptionalTeleport m_Row;                      // +30  parsed row
};

C_ASSERT(sizeof(CRefOptionalTeleport) == 0x70);
C_ASSERT(FIELD_OFFSET(CRefOptionalTeleport, m_Row) == 48);

// _REFEVENTZONE
class CRefEventZone : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefEventZone)

public:
    CRefEventZone();
    virtual ~CRefEventZone();
};

C_ASSERT(sizeof(CRefEventZone) == 0x30);

// _REFEVENT
class CRefEvent : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefEvent)

public:
    CRefEvent();
    virtual ~CRefEvent();
};

C_ASSERT(sizeof(CRefEvent) == 0x30);

// _REFCHARDEFAULT_QUEST
class CRefDefaultQuest : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefDefaultQuest)

public:
    CRefDefaultQuest();
    virtual ~CRefDefaultQuest();
};

C_ASSERT(sizeof(CRefDefaultQuest) == 0x30);

// _REFQUEST
class CRefQuest : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefQuest)

public:
    CRefQuest();
    virtual ~CRefQuest();

public:
    RefQuest         m_Data;                     // +18  the row (GetData)
};

C_ASSERT(sizeof(CRefQuest) == 0x108);
C_ASSERT(FIELD_OFFSET(CRefQuest, m_Data) == 24);

// _RefQuestReward
class CRefQuestReward : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefQuestReward)

public:
    CRefQuestReward();
    virtual ~CRefQuestReward();
};

C_ASSERT(sizeof(CRefQuestReward) == 0x30);

// _RefQuestRewardItems
class CRefQuestRewardItems : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefQuestRewardItems)

public:
    CRefQuestRewardItems();
    virtual ~CRefQuestRewardItems();
};

C_ASSERT(sizeof(CRefQuestRewardItems) == 0x30);

// _RefEventReward
class CRefEventReward : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefEventReward)

public:
    CRefEventReward();
    virtual ~CRefEventReward();
};

C_ASSERT(sizeof(CRefEventReward) == 0x30);

// _RefEventRewardItems
class CRefEventRewardItems : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefEventRewardItems)

public:
    CRefEventRewardItems();
    virtual ~CRefEventRewardItems();
};

C_ASSERT(sizeof(CRefEventRewardItems) == 0x30);

// _REFGAME_WORLD
class CRefGameWorld : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefGameWorld)

public:
    CRefGameWorld();
    virtual ~CRefGameWorld();

public:
    RefGameWorld     m_Data;                     // +18  the row (GetData)
};

C_ASSERT(sizeof(CRefGameWorld) == 0x68);
C_ASSERT(FIELD_OFFSET(CRefGameWorld, m_Data) == 24);

// _REFGAMEWORLDNPC
class CRefGameWorldNPC : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefGameWorldNPC)

public:
    CRefGameWorldNPC();
    virtual ~CRefGameWorldNPC();
};

C_ASSERT(sizeof(CRefGameWorldNPC) == 0x30);

// _REFGAME_WORLD_CONFIG
class CRefGameWorldConfig : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefGameWorldConfig)

public:
    CRefGameWorldConfig();
    virtual ~CRefGameWorldConfig();
};

C_ASSERT(sizeof(CRefGameWorldConfig) == 0x30);

// _REFINSTANCE_WORLD_REGION
class CRefInstanceWorldRegion : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefInstanceWorldRegion)

public:
    CRefInstanceWorldRegion();
    virtual ~CRefInstanceWorldRegion();

public:
    int              WorldID;                    // +18
    short            RegionID;                   // +1C
};

C_ASSERT(sizeof(CRefInstanceWorldRegion) == 0x20);
C_ASSERT(FIELD_OFFSET(CRefInstanceWorldRegion, WorldID) == 24);

// _REFINSTANCE_WORLD_START_POS
class CRefInstanceWorldStartPos : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefInstanceWorldStartPos)

public:
    CRefInstanceWorldStartPos();
    virtual ~CRefInstanceWorldStartPos();

public:
    int              WorldID;                    // +18
    short            RegionID;                   // +1C
    int              PosX;                       // +20
    int              PosY;                       // +24
    int              PosZ;                       // +28
    int              Param;                      // +2C
};

C_ASSERT(sizeof(CRefInstanceWorldStartPos) == 0x30);
C_ASSERT(FIELD_OFFSET(CRefInstanceWorldStartPos, WorldID) == 24);

// _REFSHARDCONTENTCONFIG
class CRefShardContentConfig : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefShardContentConfig)

public:
    CRefShardContentConfig();
    virtual ~CRefShardContentConfig();

public:
    RefShardContentConfig m_Row;                      // +30  parsed row
};

C_ASSERT(sizeof(CRefShardContentConfig) == 0xA8);
C_ASSERT(FIELD_OFFSET(CRefShardContentConfig, m_Row) == 48);

// _RefUIString_Mt
class CRefUIString : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefUIString)

public:
    CRefUIString();
    virtual ~CRefUIString();

public:
    RefUIString      m_Row;                      // +30  parsed row
};

C_ASSERT(sizeof(CRefUIString) == 0xA8);
C_ASSERT(FIELD_OFFSET(CRefUIString, m_Row) == 48);

// _RefServerEvent
class CRefServerEvent : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefServerEvent)

public:
    CRefServerEvent();
    virtual ~CRefServerEvent();
};

C_ASSERT(sizeof(CRefServerEvent) == 0x30);

// _RefServerEventReward
class CRefServerEventReward : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefServerEventReward)

public:
    CRefServerEventReward();
    virtual ~CRefServerEventReward();
};

C_ASSERT(sizeof(CRefServerEventReward) == 0x30);

// _RefServerEventReward_ExpUPForPlayers
class CRefServerEventReward_ExpUPForPlayers : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefServerEventReward_ExpUPForPlayers)

public:
    CRefServerEventReward_ExpUPForPlayers();
    virtual ~CRefServerEventReward_ExpUPForPlayers();
};

C_ASSERT(sizeof(CRefServerEventReward_ExpUPForPlayers) == 0x30);

// _RefServerEventReward_SpawnMonster
class CRefServerEventReward_SpawnMonster : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefServerEventReward_SpawnMonster)

public:
    CRefServerEventReward_SpawnMonster();
    virtual ~CRefServerEventReward_SpawnMonster();
};

C_ASSERT(sizeof(CRefServerEventReward_SpawnMonster) == 0x30);

// _RefRentItem
class CRefRentItem : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefRentItem)

public:
    CRefRentItem();
    virtual ~CRefRentItem();
};

C_ASSERT(sizeof(CRefRentItem) == 0x30);

// _RefCollectionBook_Theme
class CRefCollectionBook_Theme : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefCollectionBook_Theme)

public:
    CRefCollectionBook_Theme();
    virtual ~CRefCollectionBook_Theme();
};

C_ASSERT(sizeof(CRefCollectionBook_Theme) == 0x30);

// _RefCollectionBook_Item
class CRefCollectionBook_Item : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefCollectionBook_Item)

public:
    CRefCollectionBook_Item();
    virtual ~CRefCollectionBook_Item();
};

C_ASSERT(sizeof(CRefCollectionBook_Item) == 0x30);

// TAB_REFRANKING_TRADERACTIVITY
class CRefRankingTraderActivity : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefRankingTraderActivity)

public:
    CRefRankingTraderActivity();
    virtual ~CRefRankingTraderActivity();
};

C_ASSERT(sizeof(CRefRankingTraderActivity) == 0x30);

// TAB_REFRANKING_ROBBERACTIVITY
class CRefRankingRobberActivity : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefRankingRobberActivity)

public:
    CRefRankingRobberActivity();
    virtual ~CRefRankingRobberActivity();
};

C_ASSERT(sizeof(CRefRankingRobberActivity) == 0x30);

// TAB_REFRANKING_HUNTERACTIVITY
class CRefRankingHunterActivity : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefRankingHunterActivity)

public:
    CRefRankingHunterActivity();
    virtual ~CRefRankingHunterActivity();
};

C_ASSERT(sizeof(CRefRankingHunterActivity) == 0x30);

// TAB_REFRANKING_TRADERCONTRIBUTION
class CRefRankingTraderContribution : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefRankingTraderContribution)

public:
    CRefRankingTraderContribution();
    virtual ~CRefRankingTraderContribution();
};

C_ASSERT(sizeof(CRefRankingTraderContribution) == 0x30);

// TAB_REFRANKING_ROBBERCONTRIBUTION
class CRefRankingRobberContribution : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefRankingRobberContribution)

public:
    CRefRankingRobberContribution();
    virtual ~CRefRankingRobberContribution();
};

C_ASSERT(sizeof(CRefRankingRobberContribution) == 0x30);

// TAB_REFRANKING_HUNTERCONTRIBUTION
class CRefRankingHunterContribution : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefRankingHunterContribution)

public:
    CRefRankingHunterContribution();
    virtual ~CRefRankingHunterContribution();
};

C_ASSERT(sizeof(CRefRankingHunterContribution) == 0x30);

// _REFGACHACODE
class CRefGachaCode : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefGachaCode)

public:
    CRefGachaCode();
    virtual ~CRefGachaCode();

public:
    RefGachaCode     m_Data;                     // +18  the row (GetData)
};

C_ASSERT(sizeof(CRefGachaCode) == 0x38);
C_ASSERT(FIELD_OFFSET(CRefGachaCode, m_Data) == 24);

// _REFGACHAITEMSET
class CRefGachaItemSet : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefGachaItemSet)

public:
    CRefGachaItemSet();
    virtual ~CRefGachaItemSet();
};

C_ASSERT(sizeof(CRefGachaItemSet) == 0x30);

// _REFGACHANPCMAP
class CRefGachaNpcMap : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefGachaNpcMap)

public:
    CRefGachaNpcMap();
    virtual ~CRefGachaNpcMap();

public:
    BYTE             Service;                    // +18
    int              NPC_ID;                     // +1C
    int              SelectionGachaID;           // +20
    int              WasteGachaID;               // +24
};

C_ASSERT(sizeof(CRefGachaNpcMap) == 0x28);
C_ASSERT(FIELD_OFFSET(CRefGachaNpcMap, Service) == 24);
