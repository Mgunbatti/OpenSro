#pragma once

#include <string>

#include "BSLib/DB/Database.h"
#include "ServerCommon/RecordT.h"

// Reference record classes of SR_ShardManager (ServerCommon; CShardDB::RegisterRefTables opens one
// CDBTable per class, CTableKeeper::RegisterTable_<T> gives it a CCustomAllocator<T, ...>).
//
// Every class is a CDBRecordT (RecordT.h): runtime class with factory, slots 2 / 3 and the table
// description; slots 4..8 are the August 2011 defaults of CDBRecord / CDBRecStream. The table
// descriptions have flags DBTABLE_04 and only the two empty field groups 0 and 1 (so
// CDBTable::DescribeColumns takes every column of the table into group 1 with offsets of its
// own). The comments in the .cpp list the original constructor, destructor, slot 2 / 3 and table
// description addresses of each class (now its CDBRecordT instance).
//
// Two kinds of record:
// - CDBRecord records (m_nDataOffset 24): the columns land in the members after the CDBRecord
//   part, in table order with natural alignment (the offsets DescribeColumns computes), so the
//   members are the August 2011 table layout;
// - CDBRecStream records (m_nDataOffset 48, not used by their GetData): the columns are appended
//   to the stream buffer and parsed by the reference data readers. Some classes keep a parsed
//   row (m_Row) or bytes no constructor touches after the CDBRecStream part.

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
struct RefRegionData
{
    DWORD            m_adw30[2];                 // +30  not initialised
};

SR_RECORD(CRefRegion, CDBRecStream, RefRegionData)

C_ASSERT(sizeof(CRefRegion) == 0x38);

// _REFLEVEL
struct RefLevelData
{
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

SR_RECORD(CRefLevel, CDBRecord, RefLevelData)

C_ASSERT(sizeof(CRefLevel) == 0x48);
C_ASSERT(FIELD_OFFSET(CRefLevel, Lvl) == 24);

// _REFCLIMATE
struct RefClimateData
{
    RefClimateData();

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

SR_RECORD(CRefClimate, CDBRecord, RefClimateData)

C_ASSERT(sizeof(CRefClimate) == 0x30);
C_ASSERT(FIELD_OFFSET(CRefClimate, ID) == 24);

// _REFREGIONBINDASSOCSERVER
struct RefRegionBindAssocServerData
{
    std::string      AreaName;                   // +18
    int              AssocServer;                // +34
};

SR_RECORD(CRefRegionBindAssocServer, CDBRecord, RefRegionBindAssocServerData)

C_ASSERT(sizeof(CRefRegionBindAssocServer) == 0x38);
C_ASSERT(FIELD_OFFSET(CRefRegionBindAssocServer, AreaName) == 24);

// _REFCHARGEN
SR_RECORD(CRefCharGenData, CDBRecStream, void)

C_ASSERT(sizeof(CRefCharGenData) == 0x30);

// _REFOBJCOMMON
SR_RECORD(CRefObjCommon, CDBRecStream, void)

C_ASSERT(sizeof(CRefObjCommon) == 0x30);

// _REFOBJITEM
SR_RECORD(CRefObjItem, CRefObjDerived, void)

C_ASSERT(sizeof(CRefObjItem) == 0x30);

// _REFOBJCHAR
SR_RECORD(CRefObjChar, CRefObjDerived, void)

C_ASSERT(sizeof(CRefObjChar) == 0x30);

// _REFOBJSTRUCT
SR_RECORD(CRefObjStruct, CRefObjDerived, void)

C_ASSERT(sizeof(CRefObjStruct) == 0x30);

// _REFOBJCHAREXTRASKILL
SR_RECORD(CRefObjCharExtraSkill, CDBRecStream, void)

C_ASSERT(sizeof(CRefObjCharExtraSkill) == 0x30);

// _REFSKILL
struct RefSkillData
{
    RefSkill         m_Data;                     // +18  the row (GetData)
};

SR_RECORD(CRefSkill, CDBRecord, RefSkillData)

C_ASSERT(sizeof(CRefSkill) == 0x248);
C_ASSERT(FIELD_OFFSET(CRefSkill, m_Data) == 24);

// _REFSKILLMASTERY
struct RefSkillMasteryData
{
    int              ID;                         // +18
    std::string      Code;                       // +1C
    BYTE             Weapon;                     // +38
};

SR_RECORD(CRefSkillMastery, CDBRecord, RefSkillMasteryData)

C_ASSERT(sizeof(CRefSkillMastery) == 0x40);
C_ASSERT(FIELD_OFFSET(CRefSkillMastery, ID) == 24);

// _REFMAGICOPT
struct RefMagicOptData
{
    RefMagicOpt      m_Data;                     // +18  the row (GetData)
};

SR_RECORD(CRefMagicOpt, CDBRecord, RefMagicOptData)

C_ASSERT(sizeof(CRefMagicOpt) == 0x200);
C_ASSERT(FIELD_OFFSET(CRefMagicOpt, m_Data) == 24);

// _REFMAGICOPTASSIGN
struct RefMagicOptAssignData
{
    RefMagicOptAssign m_Data;                     // +18  the row (GetData)
};

SR_RECORD(CRefMagicOptAssign, CDBRecord, RefMagicOptAssignData)

C_ASSERT(sizeof(CRefMagicOptAssign) == 0x2E0);
C_ASSERT(FIELD_OFFSET(CRefMagicOptAssign, m_Data) == 24);

// _REFMAGICOPTGROUP
SR_RECORD(CRefMagicOptGroup, CDBRecStream, void)

C_ASSERT(sizeof(CRefMagicOptGroup) == 0x30);

// _REFSETITEMGROUP
SR_RECORD(CRefSetItemGroup, CDBRecStream, void)

C_ASSERT(sizeof(CRefSetItemGroup) == 0x30);

// _REFABILITYBYITEMOPTLEVEL
struct RefAbilityByItemOptLevelData
{
    DWORD            m_adw30[2];                 // +30  not initialised
};

SR_RECORD(CRefAbilityByItemOptLevel, CDBRecStream, RefAbilityByItemOptLevelData)

C_ASSERT(sizeof(CRefAbilityByItemOptLevel) == 0x38);

// _REFMAGICOPTBYITEMOPTLEVEL
SR_RECORD(CRefMagicOptByItemOptLevel, CDBRecStream, void)

C_ASSERT(sizeof(CRefMagicOptByItemOptLevel) == 0x30);

// _REFSKILLBYITEMOPTLEVEL
SR_RECORD(CRefSkillByItemOptLevel, CDBRecStream, void)

C_ASSERT(sizeof(CRefSkillByItemOptLevel) == 0x30);

// _REFALCHEMYMERIT
SR_RECORD(CRefAlchemyMerit, CDBRecStream, void)

C_ASSERT(sizeof(CRefAlchemyMerit) == 0x30);

// _REFHWANLEVEL
SR_RECORD(CRefHWANLevel, CDBRecStream, void)

C_ASSERT(sizeof(CRefHWANLevel) == 0x30);

// TAB_REFAISKILL
SR_RECORD(CRefAISkill, CDBRecStream, void)

C_ASSERT(sizeof(CRefAISkill) == 0x30);

// TAB_REFHIVE
SR_RECORD(CRefHive, CDBRecStream, void)

C_ASSERT(sizeof(CRefHive) == 0x30);

// TAB_REFTACTICS
SR_RECORD(CRefTactics, CDBRecStream, void)

C_ASSERT(sizeof(CRefTactics) == 0x30);

// TAB_REFNEST
SR_RECORD(CRefNest, CDBRecStream, void)

C_ASSERT(sizeof(CRefNest) == 0x30);

// ---------------------------------------------------------------------------------------------
// World, teleport, quest and event records

// _REFTELEPORT
SR_RECORD(CRefTeleport, CDBRecStream, void)

C_ASSERT(sizeof(CRefTeleport) == 0x30);

// _REFTELELINK
SR_RECORD(CRefTeleLink, CDBRecStream, void)

C_ASSERT(sizeof(CRefTeleLink) == 0x30);

// _REFOPTIONALTELEPORT
struct RefOptionalTeleportData
{
    RefOptionalTeleport m_Row;                      // +30  parsed row
};

SR_RECORD(CRefOptionalTeleport, CDBRecStream, RefOptionalTeleportData)

C_ASSERT(sizeof(CRefOptionalTeleport) == 0x70);
C_ASSERT(FIELD_OFFSET(CRefOptionalTeleport, m_Row) == 48);

// _REFEVENTZONE
SR_RECORD(CRefEventZone, CDBRecStream, void)

C_ASSERT(sizeof(CRefEventZone) == 0x30);

// _REFEVENT
SR_RECORD(CRefEvent, CDBRecStream, void)

C_ASSERT(sizeof(CRefEvent) == 0x30);

// _REFCHARDEFAULT_QUEST
SR_RECORD(CRefDefaultQuest, CDBRecStream, void)

C_ASSERT(sizeof(CRefDefaultQuest) == 0x30);

// _REFQUEST
struct RefQuestData
{
    RefQuest         m_Data;                     // +18  the row (GetData)
};

SR_RECORD(CRefQuest, CDBRecord, RefQuestData)

C_ASSERT(sizeof(CRefQuest) == 0x108);
C_ASSERT(FIELD_OFFSET(CRefQuest, m_Data) == 24);

// _RefQuestReward
SR_RECORD(CRefQuestReward, CDBRecStream, void)

C_ASSERT(sizeof(CRefQuestReward) == 0x30);

// _RefQuestRewardItems
SR_RECORD(CRefQuestRewardItems, CDBRecStream, void)

C_ASSERT(sizeof(CRefQuestRewardItems) == 0x30);

// _RefEventReward
SR_RECORD(CRefEventReward, CDBRecStream, void)

C_ASSERT(sizeof(CRefEventReward) == 0x30);

// _RefEventRewardItems
SR_RECORD(CRefEventRewardItems, CDBRecStream, void)

C_ASSERT(sizeof(CRefEventRewardItems) == 0x30);

// _REFGAME_WORLD
struct RefGameWorldData
{
    RefGameWorld     m_Data;                     // +18  the row (GetData)
};

SR_RECORD(CRefGameWorld, CDBRecord, RefGameWorldData)

C_ASSERT(sizeof(CRefGameWorld) == 0x68);
C_ASSERT(FIELD_OFFSET(CRefGameWorld, m_Data) == 24);

// _REFGAMEWORLDNPC
SR_RECORD(CRefGameWorldNPC, CDBRecStream, void)

C_ASSERT(sizeof(CRefGameWorldNPC) == 0x30);

// _REFGAME_WORLD_CONFIG
SR_RECORD(CRefGameWorldConfig, CDBRecStream, void)

C_ASSERT(sizeof(CRefGameWorldConfig) == 0x30);

// _REFINSTANCE_WORLD_REGION
struct RefInstanceWorldRegionData
{
    int              WorldID;                    // +18
    short            RegionID;                   // +1C
};

SR_RECORD(CRefInstanceWorldRegion, CDBRecord, RefInstanceWorldRegionData)

C_ASSERT(sizeof(CRefInstanceWorldRegion) == 0x20);
C_ASSERT(FIELD_OFFSET(CRefInstanceWorldRegion, WorldID) == 24);

// _REFINSTANCE_WORLD_START_POS
struct RefInstanceWorldStartPosData
{
    int              WorldID;                    // +18
    short            RegionID;                   // +1C
    int              PosX;                       // +20
    int              PosY;                       // +24
    int              PosZ;                       // +28
    int              Param;                      // +2C
};

SR_RECORD(CRefInstanceWorldStartPos, CDBRecord, RefInstanceWorldStartPosData)

C_ASSERT(sizeof(CRefInstanceWorldStartPos) == 0x30);
C_ASSERT(FIELD_OFFSET(CRefInstanceWorldStartPos, WorldID) == 24);

// _REFSHARDCONTENTCONFIG
struct RefShardContentConfigData
{
    RefShardContentConfig m_Row;                      // +30  parsed row
};

SR_RECORD(CRefShardContentConfig, CDBRecStream, RefShardContentConfigData)

C_ASSERT(sizeof(CRefShardContentConfig) == 0xA8);
C_ASSERT(FIELD_OFFSET(CRefShardContentConfig, m_Row) == 48);

// _RefUIString_Mt
struct RefUIStringData
{
    RefUIString      m_Row;                      // +30  parsed row
};

SR_RECORD(CRefUIString, CDBRecStream, RefUIStringData)

C_ASSERT(sizeof(CRefUIString) == 0xA8);
C_ASSERT(FIELD_OFFSET(CRefUIString, m_Row) == 48);

// _RefServerEvent
SR_RECORD(CRefServerEvent, CDBRecStream, void)

C_ASSERT(sizeof(CRefServerEvent) == 0x30);

// _RefServerEventReward
SR_RECORD(CRefServerEventReward, CDBRecStream, void)

C_ASSERT(sizeof(CRefServerEventReward) == 0x30);

// _RefServerEventReward_ExpUPForPlayers
SR_RECORD(CRefServerEventReward_ExpUPForPlayers, CDBRecStream, void)

C_ASSERT(sizeof(CRefServerEventReward_ExpUPForPlayers) == 0x30);

// _RefServerEventReward_SpawnMonster
SR_RECORD(CRefServerEventReward_SpawnMonster, CDBRecStream, void)

C_ASSERT(sizeof(CRefServerEventReward_SpawnMonster) == 0x30);

// _RefRentItem
SR_RECORD(CRefRentItem, CDBRecStream, void)

C_ASSERT(sizeof(CRefRentItem) == 0x30);

// _RefCollectionBook_Theme
SR_RECORD(CRefCollectionBook_Theme, CDBRecStream, void)

C_ASSERT(sizeof(CRefCollectionBook_Theme) == 0x30);

// _RefCollectionBook_Item
SR_RECORD(CRefCollectionBook_Item, CDBRecStream, void)

C_ASSERT(sizeof(CRefCollectionBook_Item) == 0x30);

// TAB_REFRANKING_TRADERACTIVITY
SR_RECORD(CRefRankingTraderActivity, CDBRecStream, void)

C_ASSERT(sizeof(CRefRankingTraderActivity) == 0x30);

// TAB_REFRANKING_ROBBERACTIVITY
SR_RECORD(CRefRankingRobberActivity, CDBRecStream, void)

C_ASSERT(sizeof(CRefRankingRobberActivity) == 0x30);

// TAB_REFRANKING_HUNTERACTIVITY
SR_RECORD(CRefRankingHunterActivity, CDBRecStream, void)

C_ASSERT(sizeof(CRefRankingHunterActivity) == 0x30);

// TAB_REFRANKING_TRADERCONTRIBUTION
SR_RECORD(CRefRankingTraderContribution, CDBRecStream, void)

C_ASSERT(sizeof(CRefRankingTraderContribution) == 0x30);

// TAB_REFRANKING_ROBBERCONTRIBUTION
SR_RECORD(CRefRankingRobberContribution, CDBRecStream, void)

C_ASSERT(sizeof(CRefRankingRobberContribution) == 0x30);

// TAB_REFRANKING_HUNTERCONTRIBUTION
SR_RECORD(CRefRankingHunterContribution, CDBRecStream, void)

C_ASSERT(sizeof(CRefRankingHunterContribution) == 0x30);

// _REFGACHACODE
struct RefGachaCodeData
{
    RefGachaCode     m_Data;                     // +18  the row (GetData)
};

SR_RECORD(CRefGachaCode, CDBRecord, RefGachaCodeData)

C_ASSERT(sizeof(CRefGachaCode) == 0x38);
C_ASSERT(FIELD_OFFSET(CRefGachaCode, m_Data) == 24);

// _REFGACHAITEMSET
SR_RECORD(CRefGachaItemSet, CDBRecStream, void)

C_ASSERT(sizeof(CRefGachaItemSet) == 0x30);

// _REFGACHANPCMAP
struct RefGachaNpcMapData
{
    RefGachaNpcMapData();

    BYTE             Service;                    // +18
    int              NPC_ID;                     // +1C
    int              SelectionGachaID;           // +20
    int              WasteGachaID;               // +24
};

SR_RECORD(CRefGachaNpcMap, CDBRecord, RefGachaNpcMapData)

C_ASSERT(sizeof(CRefGachaNpcMap) == 0x28);
C_ASSERT(FIELD_OFFSET(CRefGachaNpcMap, Service) == 24);

// ---------------------------------------------------------------------------------------------
// Shop records

// _REFSHOPOBJECT
SR_RECORD(CRefShopObject, CDBRecStream, void)

C_ASSERT(sizeof(CRefShopObject) == 0x30);

// _REFSHOPTABGROUP
SR_RECORD(CRefShopTabGroup, CDBRecStream, void)

C_ASSERT(sizeof(CRefShopTabGroup) == 0x30);

// _REFSHOPGROUP
SR_RECORD(CRefShopGroup, CDBRecStream, void)

C_ASSERT(sizeof(CRefShopGroup) == 0x30);

// _REFMAPPINGSHOPGROUP
SR_RECORD(CRefMappingShopGroup, CDBRecStream, void)

C_ASSERT(sizeof(CRefMappingShopGroup) == 0x30);

// _REFTREATITEMOFSHOP
SR_RECORD(CRefTreatItemOfShop, CDBRecStream, void)

C_ASSERT(sizeof(CRefTreatItemOfShop) == 0x30);

// _REFACCESSPERMISSIONOFSHOP
SR_RECORD(CRefAccessPermissionOfShop, CDBRecStream, void)

C_ASSERT(sizeof(CRefAccessPermissionOfShop) == 0x30);

// _REFREWARDPOLICYTOSELLPACKAGEITEM
SR_RECORD(CRefRewardPolicyToSellPackageItem, CDBRecStream, void)

C_ASSERT(sizeof(CRefRewardPolicyToSellPackageItem) == 0x30);

// _REFREWARDPOLICYTOBUYSCRAPITEM
SR_RECORD(CRefRewardPolicyToBuyScrapItem, CDBRecStream, void)

C_ASSERT(sizeof(CRefRewardPolicyToBuyScrapItem) == 0x30);

// _REFCONDITIONTOSELLPACKAGEITEM
SR_RECORD(CRefConditionToSellPackageItem, CDBRecStream, void)

C_ASSERT(sizeof(CRefConditionToSellPackageItem) == 0x30);

// _REFCONDITIONTOBUYSCRAPITEM
SR_RECORD(CRefConditionToBuyScrapItem, CDBRecStream, void)

C_ASSERT(sizeof(CRefConditionToBuyScrapItem) == 0x30);

// _REFREWARDPOLICYTOSELLSCRAPITEM
SR_RECORD(CRefRewardPolicyToSellScrapItem, CDBRecStream, void)

C_ASSERT(sizeof(CRefRewardPolicyToSellScrapItem) == 0x30);

// _REFCONDITIONTOSELLSCRAPITEM
SR_RECORD(CRefConditionToSellScrapItem, CDBRecStream, void)

C_ASSERT(sizeof(CRefConditionToSellScrapItem) == 0x30);

// _REFPRICEPOLICYOFITEM
SR_RECORD(CRefPricePolicyOfItem, CDBRecStream, void)

C_ASSERT(sizeof(CRefPricePolicyOfItem) == 0x30);

// _REFPACKAGEITEM
SR_RECORD(CRefPackageItem, CDBRecStream, void)

C_ASSERT(sizeof(CRefPackageItem) == 0x30);

// _REFSCRAPOFPACKAGEITEM
SR_RECORD(CRefScrapOfPackageItem, CDBRecStream, void)

C_ASSERT(sizeof(CRefScrapOfPackageItem) == 0x30);

// _REFMAPPINGSHOPWITHTAB
SR_RECORD(CRefMappingShopWithTab, CDBRecStream, void)

C_ASSERT(sizeof(CRefMappingShopWithTab) == 0x30);

// _REFSHOPGOODS
SR_RECORD(CRefShopGoods, CDBRecStream, void)

C_ASSERT(sizeof(CRefShopGoods) == 0x30);

// _REFSHOPTAB
SR_RECORD(CRefShopTab, CDBRecStream, void)

C_ASSERT(sizeof(CRefShopTab) == 0x30);

// _REFSHOP
SR_RECORD(CRefShop, CDBRecStream, void)

C_ASSERT(sizeof(CRefShop) == 0x30);

// _REFSHOPITEMSTOCKPERIOD
SR_RECORD(CRefShopItemStockPeriod, CDBRecStream, void)

C_ASSERT(sizeof(CRefShopItemStockPeriod) == 0x30);

// ---------------------------------------------------------------------------------------------
// Drop records

// _REFMONSTER_ASSIGNEDITEMDROP
SR_RECORD(CRefMonster_AssignedItemDrop, CDBRecStream, void)

C_ASSERT(sizeof(CRefMonster_AssignedItemDrop) == 0x30);

// _REFCUSTOMIZINGRESERVEDITEMDROPFORMONSTER
SR_RECORD(CRefCustomizingReservedItemDropForMonster, CDBRecStream, void)

C_ASSERT(sizeof(CRefCustomizingReservedItemDropForMonster) == 0x30);

// _REFMONSTER_ASSIGNEDITEMRNDDROP
SR_RECORD(CRefMonster_AssignedItemRndDrop, CDBRecStream, void)

C_ASSERT(sizeof(CRefMonster_AssignedItemRndDrop) == 0x30);

// _REFDROPITEMGROUP
SR_RECORD(CRefDropItemGroup, CDBRecStream, void)

C_ASSERT(sizeof(CRefDropItemGroup) == 0x30);

// _REFDROPGOLD
SR_RECORD(CRefDropGold, CDBRecStream, void)

C_ASSERT(sizeof(CRefDropGold) == 0x30);

// _REFDROPOPTLVLSEL
SR_RECORD(CRefDropOptLevelSel, CDBRecStream, void)

C_ASSERT(sizeof(CRefDropOptLevelSel) == 0x30);

// _REFDROPCLASSSEL_EQUIP
SR_RECORD(CRefDropClassSel_Equip, CDBRecStream, void)

C_ASSERT(sizeof(CRefDropClassSel_Equip) == 0x30);

// _REFDROPCLASSSEL_RAREEQUIP
SR_RECORD(CRefDropClassSel_RareEquip, CDBRecStream, void)

C_ASSERT(sizeof(CRefDropClassSel_RareEquip) == 0x30);

// _REFDROPCLASSSEL_RECOVER
SR_RECORD(CRefDropClassSel_Recover, CDBRecStream, void)

C_ASSERT(sizeof(CRefDropClassSel_Recover) == 0x30);

// _REFDROPCLASSSEL_CURE
SR_RECORD(CRefDropClassSel_Cure, CDBRecStream, void)

C_ASSERT(sizeof(CRefDropClassSel_Cure) == 0x30);

// _REFDROPCLASSSEL_AMMO
SR_RECORD(CRefDropClassSel_Ammo, CDBRecStream, void)

C_ASSERT(sizeof(CRefDropClassSel_Ammo) == 0x30);

// _REFDROPCLASSSEL_SCROLL
SR_RECORD(CRefDropClassSel_Scroll, CDBRecStream, void)

C_ASSERT(sizeof(CRefDropClassSel_Scroll) == 0x30);

// _REFDROPCLASSSEL_REINFORCE
SR_RECORD(CRefDropClassSel_Reinforce, CDBRecStream, void)

C_ASSERT(sizeof(CRefDropClassSel_Reinforce) == 0x30);

// _REFDROPCLASSSEL_ALCHEMY_TABLET
SR_RECORD(CRefDropClassSel_AlchemyTablet, CDBRecStream, void)

C_ASSERT(sizeof(CRefDropClassSel_AlchemyTablet) == 0x30);

// _REFDROPCLASSSEL_ALCHEMY_MAGICSTONE
SR_RECORD(CRefDropClassSel_AlchemyMagicStone, CDBRecStream, void)

C_ASSERT(sizeof(CRefDropClassSel_AlchemyMagicStone) == 0x30);

// _REFDROPCLASSSEL_ALCHEMY_ATTRSTONE
SR_RECORD(CRefDropClassSel_AlchemyATTRStone, CDBRecStream, void)

C_ASSERT(sizeof(CRefDropClassSel_AlchemyATTRStone) == 0x30);

// _REFDROPITEMASSIGN
SR_RECORD(CRefDropItemAssign, CDBRecStream, void)

C_ASSERT(sizeof(CRefDropItemAssign) == 0x30);

// ---------------------------------------------------------------------------------------------
// Siege fortress records

// _REFSIEGEFORTRESS
struct RefSiegeFortressData
{
    RefSiegeFortress m_Data;                     // +18  the row (GetData)
};

SR_RECORD(CRefSiegeFortress, CDBRecord, RefSiegeFortressData)

C_ASSERT(sizeof(CRefSiegeFortress) == 0xD8);
C_ASSERT(FIELD_OFFSET(CRefSiegeFortress, m_Data) == 24);

// _REFSIEGEFORTRESSGUARD
struct RefSiegeFortressGuardData
{
    RefSiegeFortressGuardData();

    BYTE             Service;                    // +18
    int              FortressID;                 // +1C
    int              GuardRefObjID;              // +20
};

SR_RECORD(CRefSiegeFortressGuard, CDBRecord, RefSiegeFortressGuardData)

C_ASSERT(sizeof(CRefSiegeFortressGuard) == 0x28);
C_ASSERT(FIELD_OFFSET(CRefSiegeFortressGuard, Service) == 24);

// _REFSIEGEFORTRESSITEMFORGE
struct RefSiegeFortressItemForgeData
{
    RefSiegeFortressItemForgeData();

    BYTE             Service;                    // +18
    int              FortressID;                 // +1C
    int              RefItemID;                  // +20
    int              ReqGold;                    // +24
    int              ReqGP;                      // +28
    int              ForgeTimeMin;               // +2C
};

SR_RECORD(CRefSiegeFortressItemForge, CDBRecord, RefSiegeFortressItemForgeData)

C_ASSERT(sizeof(CRefSiegeFortressItemForge) == 0x30);
C_ASSERT(FIELD_OFFSET(CRefSiegeFortressItemForge, Service) == 24);

// _REFSIEGEFORTRESSBATTLERANK
struct RefSiegeFortressBattleRankData
{
    RefSiegeFortressBattleRank m_Data;                     // +18  the row (GetData)
};

SR_RECORD(CRefSiegeFortressBattleRank, CDBRecord, RefSiegeFortressBattleRankData)

C_ASSERT(sizeof(CRefSiegeFortressBattleRank) == 0x60);
C_ASSERT(FIELD_OFFSET(CRefSiegeFortressBattleRank, m_Data) == 24);

// _REFSIEGESTRUCTUPGRADE
struct RefSiegeStructUpgradeData
{
    RefSiegeStructUpgrade m_Data;                     // +18  the row (GetData)
};

SR_RECORD(CRefSiegeStructUpgrade, CDBRecord, RefSiegeStructUpgradeData)

C_ASSERT(sizeof(CRefSiegeStructUpgrade) == 0xC8);
C_ASSERT(FIELD_OFFSET(CRefSiegeStructUpgrade, m_Data) == 24);

// _REFSIEGELVLSUMMONMONSTER
struct RefSiegeLvlSummonMonsterData
{
    RefSiegeLvlSummonMonsterData();

    BYTE             Service;                    // +18
    int              RefObjID;                   // +1C
    int              RefOrgObjID;                // +20
};

SR_RECORD(CRefSiegeLvlSummonMonster, CDBRecord, RefSiegeLvlSummonMonsterData)

C_ASSERT(sizeof(CRefSiegeLvlSummonMonster) == 0x28);
C_ASSERT(FIELD_OFFSET(CRefSiegeLvlSummonMonster, Service) == 24);

// _REFSIEGEFORTRESSREWARDS
struct RefSiegeFortressRewardsData
{
    RefSiegeFortressRewardsData();

    BYTE             Service;                    // +18
    int              FortressID;                 // +1C
    BYTE             RewardTypeID;               // +20
    int              RewardValue;                // +24
    BYTE             RewardCount;                // +28
};

SR_RECORD(CRefSiegeFortressRewards, CDBRecord, RefSiegeFortressRewardsData)

C_ASSERT(sizeof(CRefSiegeFortressRewards) == 0x30);
C_ASSERT(FIELD_OFFSET(CRefSiegeFortressRewards, Service) == 24);

// _REFSIEGEBLESSBUFF
struct RefSiegeBlessBuffData
{
    RefSiegeBlessBuffData();

    BYTE             Service;                    // +18
    int              BlessID;                    // +1C
    int              FortressID;                 // +20
    int              RefBlessBuffID;             // +24
    __int64          NeedGold;                   // +28
    int              NeedGP;                     // +30
};

SR_RECORD(CRefSiegeBlessBuff, CDBRecord, RefSiegeBlessBuffData)

C_ASSERT(sizeof(CRefSiegeBlessBuff) == 0x38);
C_ASSERT(FIELD_OFFSET(CRefSiegeBlessBuff, Service) == 24);

// _REFSIEGEDUNGEON
struct RefSiegeDungeonData
{
    RefSiegeDungeonData();

    BYTE             Service;                    // +18
    int              FortressID;                 // +1C
    int              WorldID;                    // +20
    BYTE             MaxCreateCount;             // +24
    __int64          EntryGold;                  // +28
    int              EntryGP;                    // +30
};

SR_RECORD(CRefSiegeDungeon, CDBRecord, RefSiegeDungeonData)

C_ASSERT(sizeof(CRefSiegeDungeon) == 0x38);
C_ASSERT(FIELD_OFFSET(CRefSiegeDungeon, Service) == 24);

// _REFSIEGEQUEST
struct RefSiegeQuestData
{
    RefSiegeQuest    m_Data;                     // +18  the row (GetData)
};

SR_RECORD(CRefSiegeQuest, CDBRecord, RefSiegeQuestData)

C_ASSERT(sizeof(CRefSiegeQuest) == 0x40);
C_ASSERT(FIELD_OFFSET(CRefSiegeQuest, m_Data) == 24);

// _REFSIEGEQUESTREWARD
struct RefSiegeQuestRewardData
{
    RefSiegeQuestRewardData();

    BYTE             Service;                    // +18
    int              QuestID;                    // +1C
    BYTE             RewardType;                 // +20
    int              RewardRefID;                // +24
    int              RewardValue;                // +28
};

SR_RECORD(CRefSiegeQuestReward, CDBRecord, RefSiegeQuestRewardData)

C_ASSERT(sizeof(CRefSiegeQuestReward) == 0x30);
C_ASSERT(FIELD_OFFSET(CRefSiegeQuestReward, Service) == 24);

// ---------------------------------------------------------------------------------------------
// Trigger and game world group records

// _RefGameWorldBindTriggerCategory
struct RefGameWorldBindTriggerCategoryData
{
    DWORD            m_adw30[4];                 // +30  not initialised
};

SR_RECORD(CRefGameWorldBindTriggerCategory, CDBRecStream, RefGameWorldBindTriggerCategoryData)

C_ASSERT(sizeof(CRefGameWorldBindTriggerCategory) == 0x40);

// _RefTriggerCategory
struct RefTriggerCategoryData
{
    RefTriggerCategory m_Row;                      // +30  parsed row
};

SR_RECORD(CRefTriggerCategory, CDBRecStream, RefTriggerCategoryData)

C_ASSERT(sizeof(CRefTriggerCategory) == 0x70);
C_ASSERT(FIELD_OFFSET(CRefTriggerCategory, m_Row) == 48);

// _RefTriggerCategoryBindTrigger
struct RefTriggerCategoryBindTriggerData
{
    DWORD            m_adw30[4];                 // +30  not initialised
};

SR_RECORD(CRefTriggerCategoryBindTrigger, CDBRecStream, RefTriggerCategoryBindTriggerData)

C_ASSERT(sizeof(CRefTriggerCategoryBindTrigger) == 0x40);

// _RefTrigger
struct RefTriggerData
{
    RefTrigger       m_Row;                      // +30  parsed row
};

SR_RECORD(CRefTrigger, CDBRecStream, RefTriggerData)

C_ASSERT(sizeof(CRefTrigger) == 0x78);
C_ASSERT(FIELD_OFFSET(CRefTrigger, m_Row) == 48);

// _RefTriggerVariable
struct RefTriggerVariableData
{
    RefTriggerVariable m_Row;                      // +30  parsed row
};

SR_RECORD(CRefTriggerVariable, CDBRecStream, RefTriggerVariableData)

C_ASSERT(sizeof(CRefTriggerVariable) == 0x90);
C_ASSERT(FIELD_OFFSET(CRefTriggerVariable, m_Row) == 48);

// _RefTriggerBindEvent
struct RefTriggerBindEventData
{
    DWORD            m_adw30[4];                 // +30  not initialised
};

SR_RECORD(CRefTriggerBindEvent, CDBRecStream, RefTriggerBindEventData)

C_ASSERT(sizeof(CRefTriggerBindEvent) == 0x40);

// _RefTriggerBindCondition
struct RefTriggerBindConditionData
{
    DWORD            m_adw30[4];                 // +30  not initialised
};

SR_RECORD(CRefTriggerBindCondition, CDBRecStream, RefTriggerBindConditionData)

C_ASSERT(sizeof(CRefTriggerBindCondition) == 0x40);

// _RefTriggerBindAction
struct RefTriggerBindActionData
{
    DWORD            m_adw30[4];                 // +30  not initialised
};

SR_RECORD(CRefTriggerBindAction, CDBRecStream, RefTriggerBindActionData)

C_ASSERT(sizeof(CRefTriggerBindAction) == 0x40);

// _RefTriggerCommon
struct RefTriggerCommonData
{
    RefTriggerCommon m_Row;                      // +30  parsed row
};

SR_RECORD(CRefTriggerCommon, CDBRecStream, RefTriggerCommonData)

C_ASSERT(sizeof(CRefTriggerCommon) == 0x78);
C_ASSERT(FIELD_OFFSET(CRefTriggerCommon, m_Row) == 48);

// _RefTriggerEvent
struct RefTriggerEventData
{
    DWORD            m_adw30[2];                 // +30  not initialised
};

SR_RECORD(CRefTriggerEvent, CDBRecStream, RefTriggerEventData)

C_ASSERT(sizeof(CRefTriggerEvent) == 0x38);

// _RefTriggerCondition
struct RefTriggerConditionData
{
    RefTriggerCondition m_Row;                      // +30  parsed row
};

SR_RECORD(CRefTriggerCondition, CDBRecStream, RefTriggerConditionData)

C_ASSERT(sizeof(CRefTriggerCondition) == 0x90);
C_ASSERT(FIELD_OFFSET(CRefTriggerCondition, m_Row) == 48);

// _RefTriggerAction
struct RefTriggerActionData
{
    RefTriggerAction m_Row;                      // +30  parsed row
};

SR_RECORD(CRefTriggerAction, CDBRecStream, RefTriggerActionData)

C_ASSERT(sizeof(CRefTriggerAction) == 0x58);
C_ASSERT(FIELD_OFFSET(CRefTriggerAction, m_Row) == 48);

// _RefTriggerConditionParam
struct RefTriggerConditionParamData
{
    RefTriggerConditionParam m_Row;                      // +30  parsed row
};

SR_RECORD(CRefTriggerConditionParam, CDBRecStream, RefTriggerConditionParamData)

C_ASSERT(sizeof(CRefTriggerConditionParam) == 0xA8);
C_ASSERT(FIELD_OFFSET(CRefTriggerConditionParam, m_Row) == 48);

// _RefTriggerActionParam
struct RefTriggerActionParamData
{
    RefTriggerActionParam m_Row;                      // +30  parsed row
};

SR_RECORD(CRefTriggerActionParam, CDBRecStream, RefTriggerActionParamData)

C_ASSERT(sizeof(CRefTriggerActionParam) == 0xA8);
C_ASSERT(FIELD_OFFSET(CRefTriggerActionParam, m_Row) == 48);

// _RefGameWorldBindGameWorldGroup
struct RefGameWorldBindGameWorldGroupData
{
    DWORD            m_adw30[4];                 // +30  not initialised
};

SR_RECORD(CRefGameWorldBindGameWorldGroup, CDBRecStream, RefGameWorldBindGameWorldGroupData)

C_ASSERT(sizeof(CRefGameWorldBindGameWorldGroup) == 0x40);

// _RefGameWorldGroup
struct RefGameWorldGroupData
{
    RefGameWorldGroup m_Row;                      // +30  parsed row
};

SR_RECORD(CRefGameWorldGroup, CDBRecStream, RefGameWorldGroupData)

C_ASSERT(sizeof(CRefGameWorldGroup) == 0x88);
C_ASSERT(FIELD_OFFSET(CRefGameWorldGroup, m_Row) == 48);

// _RefGameWorldGroup_Config
struct RefGameWorldGroup_ConfigData
{
    RefGameWorldGroupConfig m_Row;                      // +30  parsed row
};

SR_RECORD(CRefGameWorldGroup_Config, CDBRecStream, RefGameWorldGroup_ConfigData)

C_ASSERT(sizeof(CRefGameWorldGroup_Config) == 0xA8);
C_ASSERT(FIELD_OFFSET(CRefGameWorldGroup_Config, m_Row) == 48);
