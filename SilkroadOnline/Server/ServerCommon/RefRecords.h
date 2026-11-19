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

// _RefSiegeFortress row (188 bytes): RefSiegeFortress::InitMembers 0x514f70.
struct RefSiegeFortress
{
    RefSiegeFortress();

    BYTE             Service;                    // +0
    int              FortressID;                 // +4
    std::string      CodeName128;                // +8
    std::string      Name;                       // +24
    std::string      NameID128;                  // +40
    std::string      LinkedTeleportCodeName;     // +5C
    BYTE             Scale;                      // +78
    short            MaxAdmission;               // +7A
    BYTE             MaxGuard;                   // +7C
    BYTE             MaxBarricade;               // +7D
    short            TaxTargets;                 // +7E
    int              RequestFee;                 // +80
    std::string      CrestPath128;               // +84
    std::string      RequestNPCName128;          // +A0
};

C_ASSERT(sizeof(RefSiegeFortress) == 188);

// _RefSiegeFortressBattleRank row (68 bytes): RefSiegeFortressBattleRank::InitMembers 0x514f00.
struct RefSiegeFortressBattleRank
{
    RefSiegeFortressBattleRank();

    BYTE             Service;                    // +0
    BYTE             RankLvl;                    // +1
    std::string      RankName;                   // +4
    int              ReqPKCount;                 // +20
    int              BindedSkillID;              // +24
    std::string      CrestPath128;               // +28
};

C_ASSERT(sizeof(RefSiegeFortressBattleRank) == 68);

// _RefSiegeStructUpgrade row (172 bytes): RefSiegeStructUpgrade::InitMembers 0x514e70.
struct RefSiegeStructUpgrade
{
    RefSiegeStructUpgrade();

    BYTE             Service;                    // +0
    std::string      Structname;                 // +4
    std::string      BaseStructcodename;         // +20
    std::string      UpgradeStructname[4];       // +3C  UpgradeStructname1..UpgradeStructname4
};

C_ASSERT(sizeof(RefSiegeStructUpgrade) == 172);

// _RefSiegeQuest row (40 bytes): RefSiegeQuest::InitMembers 0x514d40.
struct RefSiegeQuest
{
    RefSiegeQuest();

    BYTE             Service;                    // +0
    int              QuestID;                    // +4
    std::string      QuestName;                  // +8
    BYTE             QuestType;                  // +24
    BYTE             RewardConditionTargetCount; // +25
    BYTE             IsAccumulation;             // +26
};

C_ASSERT(sizeof(RefSiegeQuest) == 40);

// Parsed _RefTriggerCategory row (64 bytes, Read 0x499ff0): InitMembers 0x4a7bc0 /
// DestroyMembers 0x4a7c20.
struct RefTriggerCategory
{
    int              ID;                         // +0
    std::string      CodeName128;                // +4
    std::string      ObjName128;                 // +20
    int              IndexNumber;                // +3C
};

C_ASSERT(sizeof(RefTriggerCategory) == 64);

// Parsed _RefTrigger row (68 bytes, Read 0x49a0a0): InitMembers 0x4a7d60 / DestroyMembers
// 0x4a7dc0.
struct RefTrigger
{
    int              ID;                         // +0
    std::string      CodeName128;                // +4
    BYTE             IsActive;                   // +20
    BYTE             IsRepeat;                   // +21
    std::string      Comment512;                 // +24
    int              IndexNumber;                // +40
};

C_ASSERT(sizeof(RefTrigger) == 68);

// Parsed _RefTriggerVariable row (96 bytes, Read 0x49a550): InitMembers 0x4a7f00 /
// DestroyMembers 0x4a7f70.
struct RefTriggerVariable
{
    int              ID;                         // +0
    int              BindTriggerID;              // +4
    std::string      CodeName128;                // +8
    std::string      Type;                       // +24
    int              Value;                      // +40
    std::string      Comment129;                 // +44
};

C_ASSERT(sizeof(RefTriggerVariable) == 96);

// Parsed _RefTriggerCommon row (68 bytes, Read 0x49a230): InitMembers 0x4a80d0 / DestroyMembers
// 0x4a8130.
struct RefTriggerCommon
{
    int              ID;                         // +0
    std::string      CodeName128;                // +4
    std::string      ObjName128;                 // +20
    short            TID1;                       // +3C
    short            TID2;                       // +3E
    short            TID3;                       // +40
    short            TID4;                       // +42
};

C_ASSERT(sizeof(RefTriggerCommon) == 68);

// Parsed _RefTriggerCondition row (96 bytes, Read 0x49a3c0): InitMembers 0x4a8270 /
// DestroyMembers 0x4a82e0.
struct RefTriggerCondition
{
    int              ID;                         // +0
    int              RefTriggerCommonID;         // +4
    std::string      OnTrue;                     // +8
    std::string      OnFalse;                    // +24
    short            Sequence;                   // +40
    std::string      ParamGroupCodeName128;      // +44
};

C_ASSERT(sizeof(RefTriggerCondition) == 96);

// Parsed _RefTriggerAction row (40 bytes, Read 0x49a490): InitMembers 0x4a8440 / DestroyMembers
// 0x4a8490.
struct RefTriggerAction
{
    int              ID;                         // +0
    int              RefTriggerCommonID;         // +4
    int              Delay;                      // +8
    std::string      ParamGroupCodeName128;      // +C
};

C_ASSERT(sizeof(RefTriggerAction) == 40);

// Parsed _RefTriggerConditionParam row (116 bytes, RefKeyValue4Str::Read 0x49a680): InitMembers
// 0x4a8600 / DestroyMembers 0x4a86f0.
struct RefTriggerConditionParam
{
    int              ID;                         // +0
    std::string      GroupCodeName128;           // +4
    std::string      ValueCodeName128;           // +20
    std::string      Value;                      // +3C
    std::string      Type;                       // +58
};

C_ASSERT(sizeof(RefTriggerConditionParam) == 116);

// Parsed _RefTriggerActionParam row (116 bytes, RefKeyValue4Str::Read 0x49a680): InitMembers
// 0x4a88c0 / DestroyMembers 0x4a89b0.
struct RefTriggerActionParam
{
    int              ID;                         // +0
    std::string      GroupCodeName128;           // +4
    std::string      ValueCodeName128;           // +20
    std::string      Value;                      // +3C
    std::string      Type;                       // +58
};

C_ASSERT(sizeof(RefTriggerActionParam) == 116);

// Parsed _RefGameWorldGroup row (88 bytes, Read 0x49a620): InitMembers 0x4a8df0 /
// DestroyMembers 0x4a8e60.
struct RefGameWorldGroup
{
    int              ID;                         // +0
    std::string      CodeName128;                // +4
    std::string      ObjName128;                 // +20
    std::string      ConfigGroupCodeName128;     // +3C
};

C_ASSERT(sizeof(RefGameWorldGroup) == 88);

// Parsed _RefGameWorldGroup_Config row (116 bytes, RefKeyValue4Str::Read 0x49a680): InitMembers
// 0x4a8b80 / DestroyMembers 0x4a8c70.
struct RefGameWorldGroupConfig
{
    int              ID;                         // +0
    std::string      GroupCodeName128;           // +4
    std::string      ValueCodeName128;           // +20
    std::string      Value;                      // +3C
    std::string      Type;                       // +58
};

C_ASSERT(sizeof(RefGameWorldGroupConfig) == 116);

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

// ---------------------------------------------------------------------------------------------
// Shop records

// _REFSHOPOBJECT
class CRefShopObject : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefShopObject)

public:
    CRefShopObject();
    virtual ~CRefShopObject();
};

C_ASSERT(sizeof(CRefShopObject) == 0x30);

// _REFSHOPTABGROUP
class CRefShopTabGroup : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefShopTabGroup)

public:
    CRefShopTabGroup();
    virtual ~CRefShopTabGroup();
};

C_ASSERT(sizeof(CRefShopTabGroup) == 0x30);

// _REFSHOPGROUP
class CRefShopGroup : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefShopGroup)

public:
    CRefShopGroup();
    virtual ~CRefShopGroup();
};

C_ASSERT(sizeof(CRefShopGroup) == 0x30);

// _REFMAPPINGSHOPGROUP
class CRefMappingShopGroup : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefMappingShopGroup)

public:
    CRefMappingShopGroup();
    virtual ~CRefMappingShopGroup();
};

C_ASSERT(sizeof(CRefMappingShopGroup) == 0x30);

// _REFTREATITEMOFSHOP
class CRefTreatItemOfShop : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTreatItemOfShop)

public:
    CRefTreatItemOfShop();
    virtual ~CRefTreatItemOfShop();
};

C_ASSERT(sizeof(CRefTreatItemOfShop) == 0x30);

// _REFACCESSPERMISSIONOFSHOP
class CRefAccessPermissionOfShop : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefAccessPermissionOfShop)

public:
    CRefAccessPermissionOfShop();
    virtual ~CRefAccessPermissionOfShop();
};

C_ASSERT(sizeof(CRefAccessPermissionOfShop) == 0x30);

// _REFREWARDPOLICYTOSELLPACKAGEITEM
class CRefRewardPolicyToSellPackageItem : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefRewardPolicyToSellPackageItem)

public:
    CRefRewardPolicyToSellPackageItem();
    virtual ~CRefRewardPolicyToSellPackageItem();
};

C_ASSERT(sizeof(CRefRewardPolicyToSellPackageItem) == 0x30);

// _REFREWARDPOLICYTOBUYSCRAPITEM
class CRefRewardPolicyToBuyScrapItem : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefRewardPolicyToBuyScrapItem)

public:
    CRefRewardPolicyToBuyScrapItem();
    virtual ~CRefRewardPolicyToBuyScrapItem();
};

C_ASSERT(sizeof(CRefRewardPolicyToBuyScrapItem) == 0x30);

// _REFCONDITIONTOSELLPACKAGEITEM
class CRefConditionToSellPackageItem : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefConditionToSellPackageItem)

public:
    CRefConditionToSellPackageItem();
    virtual ~CRefConditionToSellPackageItem();
};

C_ASSERT(sizeof(CRefConditionToSellPackageItem) == 0x30);

// _REFCONDITIONTOBUYSCRAPITEM
class CRefConditionToBuyScrapItem : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefConditionToBuyScrapItem)

public:
    CRefConditionToBuyScrapItem();
    virtual ~CRefConditionToBuyScrapItem();
};

C_ASSERT(sizeof(CRefConditionToBuyScrapItem) == 0x30);

// _REFREWARDPOLICYTOSELLSCRAPITEM
class CRefRewardPolicyToSellScrapItem : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefRewardPolicyToSellScrapItem)

public:
    CRefRewardPolicyToSellScrapItem();
    virtual ~CRefRewardPolicyToSellScrapItem();
};

C_ASSERT(sizeof(CRefRewardPolicyToSellScrapItem) == 0x30);

// _REFCONDITIONTOSELLSCRAPITEM
class CRefConditionToSellScrapItem : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefConditionToSellScrapItem)

public:
    CRefConditionToSellScrapItem();
    virtual ~CRefConditionToSellScrapItem();
};

C_ASSERT(sizeof(CRefConditionToSellScrapItem) == 0x30);

// _REFPRICEPOLICYOFITEM
class CRefPricePolicyOfItem : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefPricePolicyOfItem)

public:
    CRefPricePolicyOfItem();
    virtual ~CRefPricePolicyOfItem();
};

C_ASSERT(sizeof(CRefPricePolicyOfItem) == 0x30);

// _REFPACKAGEITEM
class CRefPackageItem : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefPackageItem)

public:
    CRefPackageItem();
    virtual ~CRefPackageItem();
};

C_ASSERT(sizeof(CRefPackageItem) == 0x30);

// _REFSCRAPOFPACKAGEITEM
class CRefScrapOfPackageItem : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefScrapOfPackageItem)

public:
    CRefScrapOfPackageItem();
    virtual ~CRefScrapOfPackageItem();
};

C_ASSERT(sizeof(CRefScrapOfPackageItem) == 0x30);

// _REFMAPPINGSHOPWITHTAB
class CRefMappingShopWithTab : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefMappingShopWithTab)

public:
    CRefMappingShopWithTab();
    virtual ~CRefMappingShopWithTab();
};

C_ASSERT(sizeof(CRefMappingShopWithTab) == 0x30);

// _REFSHOPGOODS
class CRefShopGoods : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefShopGoods)

public:
    CRefShopGoods();
    virtual ~CRefShopGoods();
};

C_ASSERT(sizeof(CRefShopGoods) == 0x30);

// _REFSHOPTAB
class CRefShopTab : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefShopTab)

public:
    CRefShopTab();
    virtual ~CRefShopTab();
};

C_ASSERT(sizeof(CRefShopTab) == 0x30);

// _REFSHOP
class CRefShop : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefShop)

public:
    CRefShop();
    virtual ~CRefShop();
};

C_ASSERT(sizeof(CRefShop) == 0x30);

// _REFSHOPITEMSTOCKPERIOD
class CRefShopItemStockPeriod : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefShopItemStockPeriod)

public:
    CRefShopItemStockPeriod();
    virtual ~CRefShopItemStockPeriod();
};

C_ASSERT(sizeof(CRefShopItemStockPeriod) == 0x30);

// ---------------------------------------------------------------------------------------------
// Drop records

// _REFMONSTER_ASSIGNEDITEMDROP
class CRefMonster_AssignedItemDrop : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefMonster_AssignedItemDrop)

public:
    CRefMonster_AssignedItemDrop();
    virtual ~CRefMonster_AssignedItemDrop();
};

C_ASSERT(sizeof(CRefMonster_AssignedItemDrop) == 0x30);

// _REFCUSTOMIZINGRESERVEDITEMDROPFORMONSTER
class CRefCustomizingReservedItemDropForMonster : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefCustomizingReservedItemDropForMonster)

public:
    CRefCustomizingReservedItemDropForMonster();
    virtual ~CRefCustomizingReservedItemDropForMonster();
};

C_ASSERT(sizeof(CRefCustomizingReservedItemDropForMonster) == 0x30);

// _REFMONSTER_ASSIGNEDITEMRNDDROP
class CRefMonster_AssignedItemRndDrop : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefMonster_AssignedItemRndDrop)

public:
    CRefMonster_AssignedItemRndDrop();
    virtual ~CRefMonster_AssignedItemRndDrop();
};

C_ASSERT(sizeof(CRefMonster_AssignedItemRndDrop) == 0x30);

// _REFDROPITEMGROUP
class CRefDropItemGroup : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefDropItemGroup)

public:
    CRefDropItemGroup();
    virtual ~CRefDropItemGroup();
};

C_ASSERT(sizeof(CRefDropItemGroup) == 0x30);

// _REFDROPGOLD
class CRefDropGold : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefDropGold)

public:
    CRefDropGold();
    virtual ~CRefDropGold();
};

C_ASSERT(sizeof(CRefDropGold) == 0x30);

// _REFDROPOPTLVLSEL
class CRefDropOptLevelSel : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefDropOptLevelSel)

public:
    CRefDropOptLevelSel();
    virtual ~CRefDropOptLevelSel();
};

C_ASSERT(sizeof(CRefDropOptLevelSel) == 0x30);

// _REFDROPCLASSSEL_EQUIP
class CRefDropClassSel_Equip : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefDropClassSel_Equip)

public:
    CRefDropClassSel_Equip();
    virtual ~CRefDropClassSel_Equip();
};

C_ASSERT(sizeof(CRefDropClassSel_Equip) == 0x30);

// _REFDROPCLASSSEL_RAREEQUIP
class CRefDropClassSel_RareEquip : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefDropClassSel_RareEquip)

public:
    CRefDropClassSel_RareEquip();
    virtual ~CRefDropClassSel_RareEquip();
};

C_ASSERT(sizeof(CRefDropClassSel_RareEquip) == 0x30);

// _REFDROPCLASSSEL_RECOVER
class CRefDropClassSel_Recover : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefDropClassSel_Recover)

public:
    CRefDropClassSel_Recover();
    virtual ~CRefDropClassSel_Recover();
};

C_ASSERT(sizeof(CRefDropClassSel_Recover) == 0x30);

// _REFDROPCLASSSEL_CURE
class CRefDropClassSel_Cure : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefDropClassSel_Cure)

public:
    CRefDropClassSel_Cure();
    virtual ~CRefDropClassSel_Cure();
};

C_ASSERT(sizeof(CRefDropClassSel_Cure) == 0x30);

// _REFDROPCLASSSEL_AMMO
class CRefDropClassSel_Ammo : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefDropClassSel_Ammo)

public:
    CRefDropClassSel_Ammo();
    virtual ~CRefDropClassSel_Ammo();
};

C_ASSERT(sizeof(CRefDropClassSel_Ammo) == 0x30);

// _REFDROPCLASSSEL_SCROLL
class CRefDropClassSel_Scroll : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefDropClassSel_Scroll)

public:
    CRefDropClassSel_Scroll();
    virtual ~CRefDropClassSel_Scroll();
};

C_ASSERT(sizeof(CRefDropClassSel_Scroll) == 0x30);

// _REFDROPCLASSSEL_REINFORCE
class CRefDropClassSel_Reinforce : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefDropClassSel_Reinforce)

public:
    CRefDropClassSel_Reinforce();
    virtual ~CRefDropClassSel_Reinforce();
};

C_ASSERT(sizeof(CRefDropClassSel_Reinforce) == 0x30);

// _REFDROPCLASSSEL_ALCHEMY_TABLET
class CRefDropClassSel_AlchemyTablet : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefDropClassSel_AlchemyTablet)

public:
    CRefDropClassSel_AlchemyTablet();
    virtual ~CRefDropClassSel_AlchemyTablet();
};

C_ASSERT(sizeof(CRefDropClassSel_AlchemyTablet) == 0x30);

// _REFDROPCLASSSEL_ALCHEMY_MAGICSTONE
class CRefDropClassSel_AlchemyMagicStone : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefDropClassSel_AlchemyMagicStone)

public:
    CRefDropClassSel_AlchemyMagicStone();
    virtual ~CRefDropClassSel_AlchemyMagicStone();
};

C_ASSERT(sizeof(CRefDropClassSel_AlchemyMagicStone) == 0x30);

// _REFDROPCLASSSEL_ALCHEMY_ATTRSTONE
class CRefDropClassSel_AlchemyATTRStone : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefDropClassSel_AlchemyATTRStone)

public:
    CRefDropClassSel_AlchemyATTRStone();
    virtual ~CRefDropClassSel_AlchemyATTRStone();
};

C_ASSERT(sizeof(CRefDropClassSel_AlchemyATTRStone) == 0x30);

// _REFDROPITEMASSIGN
class CRefDropItemAssign : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefDropItemAssign)

public:
    CRefDropItemAssign();
    virtual ~CRefDropItemAssign();
};

C_ASSERT(sizeof(CRefDropItemAssign) == 0x30);

// ---------------------------------------------------------------------------------------------
// Siege fortress records

// _REFSIEGEFORTRESS
class CRefSiegeFortress : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefSiegeFortress)

public:
    CRefSiegeFortress();
    virtual ~CRefSiegeFortress();

public:
    RefSiegeFortress m_Data;                     // +18  the row (GetData)
};

C_ASSERT(sizeof(CRefSiegeFortress) == 0xD8);
C_ASSERT(FIELD_OFFSET(CRefSiegeFortress, m_Data) == 24);

// _REFSIEGEFORTRESSGUARD
class CRefSiegeFortressGuard : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefSiegeFortressGuard)

public:
    CRefSiegeFortressGuard();
    virtual ~CRefSiegeFortressGuard();

public:
    BYTE             Service;                    // +18
    int              FortressID;                 // +1C
    int              GuardRefObjID;              // +20
};

C_ASSERT(sizeof(CRefSiegeFortressGuard) == 0x28);
C_ASSERT(FIELD_OFFSET(CRefSiegeFortressGuard, Service) == 24);

// _REFSIEGEFORTRESSITEMFORGE
class CRefSiegeFortressItemForge : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefSiegeFortressItemForge)

public:
    CRefSiegeFortressItemForge();
    virtual ~CRefSiegeFortressItemForge();

public:
    BYTE             Service;                    // +18
    int              FortressID;                 // +1C
    int              RefItemID;                  // +20
    int              ReqGold;                    // +24
    int              ReqGP;                      // +28
    int              ForgeTimeMin;               // +2C
};

C_ASSERT(sizeof(CRefSiegeFortressItemForge) == 0x30);
C_ASSERT(FIELD_OFFSET(CRefSiegeFortressItemForge, Service) == 24);

// _REFSIEGEFORTRESSBATTLERANK
class CRefSiegeFortressBattleRank : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefSiegeFortressBattleRank)

public:
    CRefSiegeFortressBattleRank();
    virtual ~CRefSiegeFortressBattleRank();

public:
    RefSiegeFortressBattleRank m_Data;                     // +18  the row (GetData)
};

C_ASSERT(sizeof(CRefSiegeFortressBattleRank) == 0x60);
C_ASSERT(FIELD_OFFSET(CRefSiegeFortressBattleRank, m_Data) == 24);

// _REFSIEGESTRUCTUPGRADE
class CRefSiegeStructUpgrade : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefSiegeStructUpgrade)

public:
    CRefSiegeStructUpgrade();
    virtual ~CRefSiegeStructUpgrade();

public:
    RefSiegeStructUpgrade m_Data;                     // +18  the row (GetData)
};

C_ASSERT(sizeof(CRefSiegeStructUpgrade) == 0xC8);
C_ASSERT(FIELD_OFFSET(CRefSiegeStructUpgrade, m_Data) == 24);

// _REFSIEGELVLSUMMONMONSTER
class CRefSiegeLvlSummonMonster : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefSiegeLvlSummonMonster)

public:
    CRefSiegeLvlSummonMonster();
    virtual ~CRefSiegeLvlSummonMonster();

public:
    BYTE             Service;                    // +18
    int              RefObjID;                   // +1C
    int              RefOrgObjID;                // +20
};

C_ASSERT(sizeof(CRefSiegeLvlSummonMonster) == 0x28);
C_ASSERT(FIELD_OFFSET(CRefSiegeLvlSummonMonster, Service) == 24);

// _REFSIEGEFORTRESSREWARDS
class CRefSiegeFortressRewards : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefSiegeFortressRewards)

public:
    CRefSiegeFortressRewards();
    virtual ~CRefSiegeFortressRewards();

public:
    BYTE             Service;                    // +18
    int              FortressID;                 // +1C
    BYTE             RewardTypeID;               // +20
    int              RewardValue;                // +24
    BYTE             RewardCount;                // +28
};

C_ASSERT(sizeof(CRefSiegeFortressRewards) == 0x30);
C_ASSERT(FIELD_OFFSET(CRefSiegeFortressRewards, Service) == 24);

// _REFSIEGEBLESSBUFF
class CRefSiegeBlessBuff : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefSiegeBlessBuff)

public:
    CRefSiegeBlessBuff();
    virtual ~CRefSiegeBlessBuff();

public:
    BYTE             Service;                    // +18
    int              BlessID;                    // +1C
    int              FortressID;                 // +20
    int              RefBlessBuffID;             // +24
    __int64          NeedGold;                   // +28
    int              NeedGP;                     // +30
};

C_ASSERT(sizeof(CRefSiegeBlessBuff) == 0x38);
C_ASSERT(FIELD_OFFSET(CRefSiegeBlessBuff, Service) == 24);

// _REFSIEGEDUNGEON
class CRefSiegeDungeon : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefSiegeDungeon)

public:
    CRefSiegeDungeon();
    virtual ~CRefSiegeDungeon();

public:
    BYTE             Service;                    // +18
    int              FortressID;                 // +1C
    int              WorldID;                    // +20
    BYTE             MaxCreateCount;             // +24
    __int64          EntryGold;                  // +28
    int              EntryGP;                    // +30
};

C_ASSERT(sizeof(CRefSiegeDungeon) == 0x38);
C_ASSERT(FIELD_OFFSET(CRefSiegeDungeon, Service) == 24);

// _REFSIEGEQUEST
class CRefSiegeQuest : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefSiegeQuest)

public:
    CRefSiegeQuest();
    virtual ~CRefSiegeQuest();

public:
    RefSiegeQuest    m_Data;                     // +18  the row (GetData)
};

C_ASSERT(sizeof(CRefSiegeQuest) == 0x40);
C_ASSERT(FIELD_OFFSET(CRefSiegeQuest, m_Data) == 24);

// _REFSIEGEQUESTREWARD
class CRefSiegeQuestReward : public CDBRecord
{
    SR_DECLARE_REF_RECORD(CRefSiegeQuestReward)

public:
    CRefSiegeQuestReward();
    virtual ~CRefSiegeQuestReward();

public:
    BYTE             Service;                    // +18
    int              QuestID;                    // +1C
    BYTE             RewardType;                 // +20
    int              RewardRefID;                // +24
    int              RewardValue;                // +28
};

C_ASSERT(sizeof(CRefSiegeQuestReward) == 0x30);
C_ASSERT(FIELD_OFFSET(CRefSiegeQuestReward, Service) == 24);

// ---------------------------------------------------------------------------------------------
// Trigger and game world group records

// _RefGameWorldBindTriggerCategory
class CRefGameWorldBindTriggerCategory : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefGameWorldBindTriggerCategory)

public:
    CRefGameWorldBindTriggerCategory();
    virtual ~CRefGameWorldBindTriggerCategory();

public:
    DWORD            m_adw30[4];                 // +30  not initialised
};

C_ASSERT(sizeof(CRefGameWorldBindTriggerCategory) == 0x40);

// _RefTriggerCategory
class CRefTriggerCategory : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTriggerCategory)

public:
    CRefTriggerCategory();
    virtual ~CRefTriggerCategory();

public:
    RefTriggerCategory m_Row;                      // +30  parsed row
};

C_ASSERT(sizeof(CRefTriggerCategory) == 0x70);
C_ASSERT(FIELD_OFFSET(CRefTriggerCategory, m_Row) == 48);

// _RefTriggerCategoryBindTrigger
class CRefTriggerCategoryBindTrigger : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTriggerCategoryBindTrigger)

public:
    CRefTriggerCategoryBindTrigger();
    virtual ~CRefTriggerCategoryBindTrigger();

public:
    DWORD            m_adw30[4];                 // +30  not initialised
};

C_ASSERT(sizeof(CRefTriggerCategoryBindTrigger) == 0x40);

// _RefTrigger
class CRefTrigger : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTrigger)

public:
    CRefTrigger();
    virtual ~CRefTrigger();

public:
    RefTrigger       m_Row;                      // +30  parsed row
};

C_ASSERT(sizeof(CRefTrigger) == 0x78);
C_ASSERT(FIELD_OFFSET(CRefTrigger, m_Row) == 48);

// _RefTriggerVariable
class CRefTriggerVariable : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTriggerVariable)

public:
    CRefTriggerVariable();
    virtual ~CRefTriggerVariable();

public:
    RefTriggerVariable m_Row;                      // +30  parsed row
};

C_ASSERT(sizeof(CRefTriggerVariable) == 0x90);
C_ASSERT(FIELD_OFFSET(CRefTriggerVariable, m_Row) == 48);

// _RefTriggerBindEvent
class CRefTriggerBindEvent : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTriggerBindEvent)

public:
    CRefTriggerBindEvent();
    virtual ~CRefTriggerBindEvent();

public:
    DWORD            m_adw30[4];                 // +30  not initialised
};

C_ASSERT(sizeof(CRefTriggerBindEvent) == 0x40);

// _RefTriggerBindCondition
class CRefTriggerBindCondition : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTriggerBindCondition)

public:
    CRefTriggerBindCondition();
    virtual ~CRefTriggerBindCondition();

public:
    DWORD            m_adw30[4];                 // +30  not initialised
};

C_ASSERT(sizeof(CRefTriggerBindCondition) == 0x40);

// _RefTriggerBindAction
class CRefTriggerBindAction : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTriggerBindAction)

public:
    CRefTriggerBindAction();
    virtual ~CRefTriggerBindAction();

public:
    DWORD            m_adw30[4];                 // +30  not initialised
};

C_ASSERT(sizeof(CRefTriggerBindAction) == 0x40);

// _RefTriggerCommon
class CRefTriggerCommon : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTriggerCommon)

public:
    CRefTriggerCommon();
    virtual ~CRefTriggerCommon();

public:
    RefTriggerCommon m_Row;                      // +30  parsed row
};

C_ASSERT(sizeof(CRefTriggerCommon) == 0x78);
C_ASSERT(FIELD_OFFSET(CRefTriggerCommon, m_Row) == 48);

// _RefTriggerEvent
class CRefTriggerEvent : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTriggerEvent)

public:
    CRefTriggerEvent();
    virtual ~CRefTriggerEvent();

public:
    DWORD            m_adw30[2];                 // +30  not initialised
};

C_ASSERT(sizeof(CRefTriggerEvent) == 0x38);

// _RefTriggerCondition
class CRefTriggerCondition : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTriggerCondition)

public:
    CRefTriggerCondition();
    virtual ~CRefTriggerCondition();

public:
    RefTriggerCondition m_Row;                      // +30  parsed row
};

C_ASSERT(sizeof(CRefTriggerCondition) == 0x90);
C_ASSERT(FIELD_OFFSET(CRefTriggerCondition, m_Row) == 48);

// _RefTriggerAction
class CRefTriggerAction : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTriggerAction)

public:
    CRefTriggerAction();
    virtual ~CRefTriggerAction();

public:
    RefTriggerAction m_Row;                      // +30  parsed row
};

C_ASSERT(sizeof(CRefTriggerAction) == 0x58);
C_ASSERT(FIELD_OFFSET(CRefTriggerAction, m_Row) == 48);

// _RefTriggerConditionParam
class CRefTriggerConditionParam : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTriggerConditionParam)

public:
    CRefTriggerConditionParam();
    virtual ~CRefTriggerConditionParam();

public:
    RefTriggerConditionParam m_Row;                      // +30  parsed row
};

C_ASSERT(sizeof(CRefTriggerConditionParam) == 0xA8);
C_ASSERT(FIELD_OFFSET(CRefTriggerConditionParam, m_Row) == 48);

// _RefTriggerActionParam
class CRefTriggerActionParam : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefTriggerActionParam)

public:
    CRefTriggerActionParam();
    virtual ~CRefTriggerActionParam();

public:
    RefTriggerActionParam m_Row;                      // +30  parsed row
};

C_ASSERT(sizeof(CRefTriggerActionParam) == 0xA8);
C_ASSERT(FIELD_OFFSET(CRefTriggerActionParam, m_Row) == 48);

// _RefGameWorldBindGameWorldGroup
class CRefGameWorldBindGameWorldGroup : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefGameWorldBindGameWorldGroup)

public:
    CRefGameWorldBindGameWorldGroup();
    virtual ~CRefGameWorldBindGameWorldGroup();

public:
    DWORD            m_adw30[4];                 // +30  not initialised
};

C_ASSERT(sizeof(CRefGameWorldBindGameWorldGroup) == 0x40);

// _RefGameWorldGroup
class CRefGameWorldGroup : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefGameWorldGroup)

public:
    CRefGameWorldGroup();
    virtual ~CRefGameWorldGroup();

public:
    RefGameWorldGroup m_Row;                      // +30  parsed row
};

C_ASSERT(sizeof(CRefGameWorldGroup) == 0x88);
C_ASSERT(FIELD_OFFSET(CRefGameWorldGroup, m_Row) == 48);

// _RefGameWorldGroup_Config
class CRefGameWorldGroup_Config : public CDBRecStream
{
    SR_DECLARE_REF_RECORD(CRefGameWorldGroup_Config)

public:
    CRefGameWorldGroup_Config();
    virtual ~CRefGameWorldGroup_Config();

public:
    RefGameWorldGroupConfig m_Row;                      // +30  parsed row
};

C_ASSERT(sizeof(CRefGameWorldGroup_Config) == 0xA8);
C_ASSERT(FIELD_OFFSET(CRefGameWorldGroup_Config, m_Row) == 48);
