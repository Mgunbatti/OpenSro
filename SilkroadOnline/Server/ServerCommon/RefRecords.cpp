#include "ServerCommon/RefRecords.h"

#include <string.h>

// The dynamic initializer of a reference table description (CRefLevel::StaticInit_TableInfo
// 0x61cd80 and its 123 copies; the analysis has one per class). The sample record and column
// come from the description macros; with no declared column the offset loop changes nothing.
template <class T>
static int RefRecord_InitTableInfo(CDBTableInfo& info, const char* pszTable,
                                   CRuntimeClass* pRecordClass, int nDataOffset)
{
    T record;
    info.m_strTable.assign(pszTable, strlen(pszTable));
    info.m_dwFlags = DBTABLE_04;
    for (int nGroup = DBGROUP_KEY; nGroup <= DBGROUP_ALL; ++nGroup)
    {
        DB_FIELD_GROUP group;
        info.m_mapGroups.insert(DBFieldGroupMap::value_type(nGroup, group));
    }
    DBColumnInfo column;
    info.m_pRecordClass = pRecordClass;
    info.m_nDataOffset = nDataOffset;
    for (DBFieldGroupMap::iterator it = info.m_mapGroups.begin(); it != info.m_mapGroups.end();
         ++it)
    {
        DBColumnVector& columns = it->second.vecColumns;
        for (DBColumnVector::iterator col = columns.begin(); col != columns.end(); ++col)
            col->nOffset -= info.m_nDataOffset;
    }
    return 0;
}

// Runtime class with factory, table description and its initializer, slots 2 and 3.
// data_base is the class the row data follows (CDBRecord: 24, CDBRecStream: 48).
#define SR_IMPLEMENT_REF_RECORD(class_name, base_name, data_base, table_name)               \
    BS_IMPLEMENT_DYNCREATE(class_name, &base_name::class##base_name)                         \
    CDBTableInfo class_name::s_TableInfo;                                                   \
    int class_name::InitTableInfo()                                                         \
    {                                                                                       \
        return RefRecord_InitTableInfo<class_name>(s_TableInfo, table_name,                \
                                                   &class_name::class##class_name,          \
                                                   sizeof(data_base));                      \
    }                                                                                       \
    static int s_nTableInfo##class_name = class_name::InitTableInfo();                      \
    const char* class_name::GetTableName()                                                  \
    {                                                                                       \
        return s_TableInfo.m_strTable.c_str();                                              \
    }                                                                                       \
    CDBTableInfo* class_name::GetTableInfo()                                                \
    {                                                                                       \
        return &s_TableInfo;                                                                \
    }


RefOptionalTeleport::RefOptionalTeleport()
{
    dwRegion = 0;
    Pos_Z = 0;
    Pos_Y = 0;
    Pos_X = 0;
    w24 = 0;
}

RefGachaCode::RefGachaCode()
{
    CodeName128.clear();
    GachaSetID = 0;
}

RefSiegeFortress::RefSiegeFortress()
    : Service(0), FortressID(0), Scale(0), MaxAdmission(0), MaxGuard(0), MaxBarricade(0),
      TaxTargets(0), RequestFee(0)
{
}

RefSiegeFortressBattleRank::RefSiegeFortressBattleRank()
    : Service(0), RankLvl(0), ReqPKCount(0), BindedSkillID(0)
{
}

RefSiegeStructUpgrade::RefSiegeStructUpgrade()
    : Service(0)
{
}

RefSiegeQuest::RefSiegeQuest()
    : Service(0), QuestID(0), QuestType(0), RewardConditionTargetCount(0), IsAccumulation(0)
{
    QuestName.clear();
}

// ---------------------------------------------------------------------------------------------
// Object, skill and magic option records

// RTC 0x780c50; CRefObjDerived::ctor / 0x52e780 (destructor).
BS_IMPLEMENT_RUNTIME_CLASS(CRefObjDerived, NULL, NULL, &CDBRecStream::classCDBRecStream)

CRefObjDerived::CRefObjDerived()
{
}

CRefObjDerived::~CRefObjDerived()
{
}

// 0x5291f0 ctor, 0x51f3a0 / 0x51f3c0 slots 2 / 3, 0x61ca10 table description.
SR_IMPLEMENT_REF_RECORD(CRefRegion, CDBRecStream, CDBRecStream, "_REFREGION")

CRefRegion::CRefRegion()
{
}

CRefRegion::~CRefRegion()
{
}

// 0x529070 ctor, 0x5290d0 dtor, 0x51f340 / 0x51f360 slots 2 / 3, 0x61cd80 table description.
SR_IMPLEMENT_REF_RECORD(CRefLevel, CDBRecord, CDBRecord, "_REFLEVEL")

CRefLevel::CRefLevel()
{
}

CRefLevel::~CRefLevel()
{
}

// 0x528fa0 ctor, 0x529020 dtor, 0x51f370 / 0x51f390 slots 2 / 3, 0x61d000 table description.
SR_IMPLEMENT_REF_RECORD(CRefClimate, CDBRecord, CDBRecord, "_REFCLIMATE")

CRefClimate::CRefClimate()
{
    ID = 0;
    InitialWeather = 0;
    InitialAmount = 0;
    ChangeWeather = 0;
    Division = 0;
    Duration = 0;
    DurationVariance = 0;
    Snowfall = 0;
    SnowfallVariance = 0;
    ProbSnow = 0;
    Rainfall = 0;
    RainfallVariance = 0;
    ProbRain = 0;
}

CRefClimate::~CRefClimate()
{
}

// 0x529120 ctor, 0x529180 dtor, 0x51ff10 / 0x51ff30 slots 2 / 3, 0x626c00 table description.
SR_IMPLEMENT_REF_RECORD(CRefRegionBindAssocServer, CDBRecord, CDBRecord,
                        "_REFREGIONBINDASSOCSERVER")

CRefRegionBindAssocServer::CRefRegionBindAssocServer()
{
}

CRefRegionBindAssocServer::~CRefRegionBindAssocServer()
{
}

// 0x52b000 ctor, 0x51f3d0 / 0x51f3f0 slots 2 / 3, 0x61d280 table description.
SR_IMPLEMENT_REF_RECORD(CRefCharGenData, CDBRecStream, CDBRecStream, "_REFCHARGEN")

CRefCharGenData::CRefCharGenData()
{
}

CRefCharGenData::~CRefCharGenData()
{
}

// 0x529440 ctor, 0x51f400 / 0x51f420 slots 2 / 3, 0x61d4f0 table description.
SR_IMPLEMENT_REF_RECORD(CRefObjCommon, CDBRecStream, CDBRecStream, "_REFOBJCOMMON")

CRefObjCommon::CRefObjCommon()
{
}

CRefObjCommon::~CRefObjCommon()
{
}

// 0x5294f0 ctor, 0x529550 dtor, 0x51f430 / 0x51f450 slots 2 / 3, 0x61d770 table description.
SR_IMPLEMENT_REF_RECORD(CRefObjItem, CRefObjDerived, CDBRecStream, "_REFOBJITEM")

CRefObjItem::CRefObjItem()
{
}

CRefObjItem::~CRefObjItem()
{
}

// 0x5295a0 ctor, 0x529600 dtor, 0x51f460 / 0x51f480 slots 2 / 3, 0x61d9e0 table description.
SR_IMPLEMENT_REF_RECORD(CRefObjChar, CRefObjDerived, CDBRecStream, "_REFOBJCHAR")

CRefObjChar::CRefObjChar()
{
}

CRefObjChar::~CRefObjChar()
{
}

// 0x529650 ctor, 0x5296b0 dtor, 0x51f4f0 / 0x51f510 slots 2 / 3, 0x61eaf0 table description.
SR_IMPLEMENT_REF_RECORD(CRefObjStruct, CRefObjDerived, CDBRecStream, "_REFOBJSTRUCT")

CRefObjStruct::CRefObjStruct()
{
}

CRefObjStruct::~CRefObjStruct()
{
}

// 0x52d070 ctor, 0x520450 / 0x520470 slots 2 / 3, 0x62b080 table description.
SR_IMPLEMENT_REF_RECORD(CRefObjCharExtraSkill, CDBRecStream, CDBRecStream, "_REFOBJCHAREXTRASKILL")

CRefObjCharExtraSkill::CRefObjCharExtraSkill()
{
}

CRefObjCharExtraSkill::~CRefObjCharExtraSkill()
{
}

// 0x5292a0 ctor, 0x529300 dtor, 0x51fdf0 / 0x51fe10 slots 2 / 3, 0x625cd0 table description.
SR_IMPLEMENT_REF_RECORD(CRefSkill, CDBRecord, CDBRecord, "_REFSKILL")

CRefSkill::CRefSkill()
{
}

CRefSkill::~CRefSkill()
{
}

// 0x529370 ctor, 0x5293d0 dtor, 0x51fdc0 / 0x51fde0 slots 2 / 3, 0x6259f0 table description.
SR_IMPLEMENT_REF_RECORD(CRefSkillMastery, CDBRecord, CDBRecord, "_REFSKILLMASTERY")

CRefSkillMastery::CRefSkillMastery()
{
}

CRefSkillMastery::~CRefSkillMastery()
{
}

// 0x529700 ctor, 0x529760 dtor, 0x51f520 / 0x51f540 slots 2 / 3, 0x61ed70 table description.
SR_IMPLEMENT_REF_RECORD(CRefMagicOpt, CDBRecord, CDBRecord, "_REFMAGICOPT")

CRefMagicOpt::CRefMagicOpt()
{
}

CRefMagicOpt::~CRefMagicOpt()
{
}

// 0x5297d0 ctor, 0x529830 dtor, 0x51f550 / 0x51f570 slots 2 / 3, 0x61f010 table description.
SR_IMPLEMENT_REF_RECORD(CRefMagicOptAssign, CDBRecord, CDBRecord, "_REFMAGICOPTASSIGN")

CRefMagicOptAssign::CRefMagicOptAssign()
{
}

CRefMagicOptAssign::~CRefMagicOptAssign()
{
}

// 0x52c810 ctor, 0x51fca0 / 0x51fcc0 slots 2 / 3, 0x61e610 table description.
SR_IMPLEMENT_REF_RECORD(CRefMagicOptGroup, CDBRecStream, CDBRecStream, "_REFMAGICOPTGROUP")

CRefMagicOptGroup::CRefMagicOptGroup()
{
}

CRefMagicOptGroup::~CRefMagicOptGroup()
{
}

// 0x52ce60 ctor, 0x51fcd0 / 0x51fcf0 slots 2 / 3, 0x61e880 table description.
SR_IMPLEMENT_REF_RECORD(CRefSetItemGroup, CDBRecStream, CDBRecStream, "_REFSETITEMGROUP")

CRefSetItemGroup::CRefSetItemGroup()
{
}

CRefSetItemGroup::~CRefSetItemGroup()
{
}

// 0x52cc30 ctor, 0x520300 / 0x520320 slots 2 / 3, 0x62a170 table description.
SR_IMPLEMENT_REF_RECORD(CRefAbilityByItemOptLevel, CDBRecStream, CDBRecStream,
                        "_REFABILITYBYITEMOPTLEVEL")

CRefAbilityByItemOptLevel::CRefAbilityByItemOptLevel()
{
}

CRefAbilityByItemOptLevel::~CRefAbilityByItemOptLevel()
{
}

// 0x52cb80 ctor, 0x520330 / 0x520350 slots 2 / 3, 0x62a3e0 table description.
SR_IMPLEMENT_REF_RECORD(CRefMagicOptByItemOptLevel, CDBRecStream, CDBRecStream,
                        "_REFMAGICOPTBYITEMOPTLEVEL")

CRefMagicOptByItemOptLevel::CRefMagicOptByItemOptLevel()
{
}

CRefMagicOptByItemOptLevel::~CRefMagicOptByItemOptLevel()
{
}

// 0x52cad0 ctor, 0x520360 / 0x520380 slots 2 / 3, 0x62a650 table description.
SR_IMPLEMENT_REF_RECORD(CRefSkillByItemOptLevel, CDBRecStream, CDBRecStream,
                        "_REFSKILLBYITEMOPTLEVEL")

CRefSkillByItemOptLevel::CRefSkillByItemOptLevel()
{
}

CRefSkillByItemOptLevel::~CRefSkillByItemOptLevel()
{
}

// 0x52d1d0 ctor, 0x5204b0 / 0x5204d0 slots 2 / 3, 0x62b560 table description.
SR_IMPLEMENT_REF_RECORD(CRefAlchemyMerit, CDBRecStream, CDBRecStream, "_REFALCHEMYMERIT")

CRefAlchemyMerit::CRefAlchemyMerit()
{
}

CRefAlchemyMerit::~CRefAlchemyMerit()
{
}

// 0x52c550 ctor, 0x51fe50 / 0x51fe70 slots 2 / 3, 0x626200 table description.
SR_IMPLEMENT_REF_RECORD(CRefHWANLevel, CDBRecStream, CDBRecStream, "_REFHWANLEVEL")

CRefHWANLevel::CRefHWANLevel()
{
}

CRefHWANLevel::~CRefHWANLevel()
{
}

// 0x52b2c0 ctor, 0x51fd90 / 0x51fdb0 slots 2 / 3, 0x625780 table description.
SR_IMPLEMENT_REF_RECORD(CRefAISkill, CDBRecStream, CDBRecStream, "TAB_REFAISKILL")

CRefAISkill::CRefAISkill()
{
}

CRefAISkill::~CRefAISkill()
{
}

// 0x52b0b0 ctor, 0x51fd00 / 0x51fd20 slots 2 / 3, 0x625030 table description.
SR_IMPLEMENT_REF_RECORD(CRefHive, CDBRecStream, CDBRecStream, "TAB_REFHIVE")

CRefHive::CRefHive()
{
}

CRefHive::~CRefHive()
{
}

// 0x52b160 ctor, 0x51fd30 / 0x51fd50 slots 2 / 3, 0x6252a0 table description.
SR_IMPLEMENT_REF_RECORD(CRefTactics, CDBRecStream, CDBRecStream, "TAB_REFTACTICS")

CRefTactics::CRefTactics()
{
}

CRefTactics::~CRefTactics()
{
}

// 0x52b210 ctor, 0x51fd60 / 0x51fd80 slots 2 / 3, 0x625510 table description.
SR_IMPLEMENT_REF_RECORD(CRefNest, CDBRecStream, CDBRecStream, "TAB_REFNEST")

CRefNest::CRefNest()
{
}

CRefNest::~CRefNest()
{
}

// ---------------------------------------------------------------------------------------------
// World, teleport, quest and event records

// 0x5298a0 ctor, 0x51f580 / 0x51f5a0 slots 2 / 3, 0x61f2c0 table description.
SR_IMPLEMENT_REF_RECORD(CRefTeleport, CDBRecStream, CDBRecStream, "_REFTELEPORT")

CRefTeleport::CRefTeleport()
{
}

CRefTeleport::~CRefTeleport()
{
}

// 0x529950 ctor, 0x51f5b0 / 0x51f5d0 slots 2 / 3, 0x61f530 table description.
SR_IMPLEMENT_REF_RECORD(CRefTeleLink, CDBRecStream, CDBRecStream, "_REFTELELINK")

CRefTeleLink::CRefTeleLink()
{
}

CRefTeleLink::~CRefTeleLink()
{
}

// 0x52cce0 ctor, 0x52cd40 dtor, 0x520390 / 0x5203b0 slots 2 / 3, 0x62a8c0 table description.
SR_IMPLEMENT_REF_RECORD(CRefOptionalTeleport, CDBRecStream, CDBRecStream, "_REFOPTIONALTELEPORT")

CRefOptionalTeleport::CRefOptionalTeleport()
{
}

CRefOptionalTeleport::~CRefOptionalTeleport()
{
}

// 0x529a00 ctor, 0x51f5e0 / 0x51f600 slots 2 / 3, 0x61f7a0 table description.
SR_IMPLEMENT_REF_RECORD(CRefEventZone, CDBRecStream, CDBRecStream, "_REFEVENTZONE")

CRefEventZone::CRefEventZone()
{
}

CRefEventZone::~CRefEventZone()
{
}

// 0x52c600 ctor, 0x51f610 / - slots 2 / 3, 0x61fa10 table description.
SR_IMPLEMENT_REF_RECORD(CRefEvent, CDBRecStream, CDBRecStream, "_REFEVENT")

CRefEvent::CRefEvent()
{
}

CRefEvent::~CRefEvent()
{
}

// 0x52ca20 ctor, 0x51f640 / 0x51f660 slots 2 / 3, 0x61fc80 table description.
SR_IMPLEMENT_REF_RECORD(CRefDefaultQuest, CDBRecStream, CDBRecStream, "_REFCHARDEFAULT_QUEST")

CRefDefaultQuest::CRefDefaultQuest()
{
}

CRefDefaultQuest::~CRefDefaultQuest()
{
}

// 0x52b370 ctor, 0x52b3d0 dtor, 0x51fe20 / 0x51fe40 slots 2 / 3, 0x625f70 table description.
SR_IMPLEMENT_REF_RECORD(CRefQuest, CDBRecord, CDBRecord, "_REFQUEST")

CRefQuest::CRefQuest()
{
}

CRefQuest::~CRefQuest()
{
}

// 0x52d5f0 ctor, 0x5205d0 / 0x5205f0 slots 2 / 3, 0x62c400 table description.
SR_IMPLEMENT_REF_RECORD(CRefQuestReward, CDBRecStream, CDBRecStream, "_RefQuestReward")

CRefQuestReward::CRefQuestReward()
{
}

CRefQuestReward::~CRefQuestReward()
{
}

// 0x52d6a0 ctor, 0x520600 / 0x520620 slots 2 / 3, 0x62c670 table description.
SR_IMPLEMENT_REF_RECORD(CRefQuestRewardItems, CDBRecStream, CDBRecStream, "_RefQuestRewardItems")

CRefQuestRewardItems::CRefQuestRewardItems()
{
}

CRefQuestRewardItems::~CRefQuestRewardItems()
{
}

// 0x52d750 ctor, 0x520630 / 0x520650 slots 2 / 3, 0x62c8e0 table description.
SR_IMPLEMENT_REF_RECORD(CRefEventReward, CDBRecStream, CDBRecStream, "_RefEventReward")

CRefEventReward::CRefEventReward()
{
}

CRefEventReward::~CRefEventReward()
{
}

// 0x52d800 ctor, 0x520660 / 0x520680 slots 2 / 3, 0x62cb50 table description.
SR_IMPLEMENT_REF_RECORD(CRefEventRewardItems, CDBRecStream, CDBRecStream, "_RefEventRewardItems")

CRefEventRewardItems::CRefEventRewardItems()
{
}

CRefEventRewardItems::~CRefEventRewardItems()
{
}

// 0x52b440 ctor, 0x52b4a0 dtor, 0x51fe80 / 0x51fea0 slots 2 / 3, 0x626470 table description.
SR_IMPLEMENT_REF_RECORD(CRefGameWorld, CDBRecord, CDBRecord, "_REFGAME_WORLD")

CRefGameWorld::CRefGameWorld()
{
}

CRefGameWorld::~CRefGameWorld()
{
}

// 0x52cfc0 ctor, 0x520420 / 0x520440 slots 2 / 3, 0x62ae10 table description.
SR_IMPLEMENT_REF_RECORD(CRefGameWorldNPC, CDBRecStream, CDBRecStream, "_REFGAMEWORLDNPC")

CRefGameWorldNPC::CRefGameWorldNPC()
{
}

CRefGameWorldNPC::~CRefGameWorldNPC()
{
}

// 0x52d120 ctor, 0x52d180 dtor, 0x520480 / 0x5204a0 slots 2 / 3, 0x62b2f0 table description.
SR_IMPLEMENT_REF_RECORD(CRefGameWorldConfig, CDBRecStream, CDBRecStream, "_REFGAME_WORLD_CONFIG")

CRefGameWorldConfig::CRefGameWorldConfig()
{
}

CRefGameWorldConfig::~CRefGameWorldConfig()
{
}

// 0x52b510 ctor, 0x52b570 dtor, 0x51feb0 / 0x51fed0 slots 2 / 3, 0x626700 table description.
SR_IMPLEMENT_REF_RECORD(CRefInstanceWorldRegion, CDBRecord, CDBRecord, "_REFINSTANCE_WORLD_REGION")

CRefInstanceWorldRegion::CRefInstanceWorldRegion()
{
}

CRefInstanceWorldRegion::~CRefInstanceWorldRegion()
{
}

// 0x52b5c0 ctor, 0x52b620 dtor, 0x51fee0 / 0x51ff00 slots 2 / 3, 0x626980 table description.
SR_IMPLEMENT_REF_RECORD(CRefInstanceWorldStartPos, CDBRecord, CDBRecord,
                        "_REFINSTANCE_WORLD_START_POS")

CRefInstanceWorldStartPos::CRefInstanceWorldStartPos()
{
}

CRefInstanceWorldStartPos::~CRefInstanceWorldStartPos()
{
}

// 0x52cda0 ctor, 0x52ce00 dtor, 0x5203c0 / 0x5203e0 slots 2 / 3, 0x62ab90 table description.
SR_IMPLEMENT_REF_RECORD(CRefShardContentConfig, CDBRecStream, CDBRecStream,
                        "_REFSHARDCONTENTCONFIG")

CRefShardContentConfig::CRefShardContentConfig()
{
}

CRefShardContentConfig::~CRefShardContentConfig()
{
}

// 0x52da10 ctor, 0x52da70 dtor, 0x5206f0 / 0x520710 slots 2 / 3, 0x62d2b0 table description.
SR_IMPLEMENT_REF_RECORD(CRefUIString, CDBRecStream, CDBRecStream, "_RefUIString_Mt")

CRefUIString::CRefUIString()
{
}

CRefUIString::~CRefUIString()
{
}

// 0x52d280 ctor, 0x5204e0 / 0x520500 slots 2 / 3, 0x62b7d0 table description.
SR_IMPLEMENT_REF_RECORD(CRefServerEvent, CDBRecStream, CDBRecStream, "_RefServerEvent")

CRefServerEvent::CRefServerEvent()
{
}

CRefServerEvent::~CRefServerEvent()
{
}

// 0x52d330 ctor, 0x520510 / 0x520530 slots 2 / 3, 0x62ba40 table description.
SR_IMPLEMENT_REF_RECORD(CRefServerEventReward, CDBRecStream, CDBRecStream, "_RefServerEventReward")

CRefServerEventReward::CRefServerEventReward()
{
}

CRefServerEventReward::~CRefServerEventReward()
{
}

// 0x52d3e0 ctor, 0x520540 / 0x520560 slots 2 / 3, 0x62bcb0 table description.
SR_IMPLEMENT_REF_RECORD(CRefServerEventReward_ExpUPForPlayers, CDBRecStream, CDBRecStream,
                        "_RefServerEventReward_ExpUPForPlayers")

CRefServerEventReward_ExpUPForPlayers::CRefServerEventReward_ExpUPForPlayers()
{
}

CRefServerEventReward_ExpUPForPlayers::~CRefServerEventReward_ExpUPForPlayers()
{
}

// 0x52d490 ctor, 0x520570 / 0x520590 slots 2 / 3, 0x62bf20 table description.
SR_IMPLEMENT_REF_RECORD(CRefServerEventReward_SpawnMonster, CDBRecStream, CDBRecStream,
                        "_RefServerEventReward_SpawnMonster")

CRefServerEventReward_SpawnMonster::CRefServerEventReward_SpawnMonster()
{
}

CRefServerEventReward_SpawnMonster::~CRefServerEventReward_SpawnMonster()
{
}

// 0x52d540 ctor, 0x5205a0 / 0x5205c0 slots 2 / 3, 0x62c190 table description.
SR_IMPLEMENT_REF_RECORD(CRefRentItem, CDBRecStream, CDBRecStream, "_RefRentItem")

CRefRentItem::CRefRentItem()
{
}

CRefRentItem::~CRefRentItem()
{
}

// 0x52d8b0 ctor, 0x520690 / 0x5206b0 slots 2 / 3, 0x62cdc0 table description.
SR_IMPLEMENT_REF_RECORD(CRefCollectionBook_Theme, CDBRecStream, CDBRecStream,
                        "_RefCollectionBook_Theme")

CRefCollectionBook_Theme::CRefCollectionBook_Theme()
{
}

CRefCollectionBook_Theme::~CRefCollectionBook_Theme()
{
}

// 0x52d960 ctor, 0x5206c0 / 0x5206e0 slots 2 / 3, 0x62d030 table description.
SR_IMPLEMENT_REF_RECORD(CRefCollectionBook_Item, CDBRecStream, CDBRecStream,
                        "_RefCollectionBook_Item")

CRefCollectionBook_Item::CRefCollectionBook_Item()
{
}

CRefCollectionBook_Item::~CRefCollectionBook_Item()
{
}

// 0x52b670 ctor, 0x51ff40 / 0x51ff60 slots 2 / 3, 0x626ed0 table description.
SR_IMPLEMENT_REF_RECORD(CRefRankingTraderActivity, CDBRecStream, CDBRecStream,
                        "TAB_REFRANKING_TRADERACTIVITY")

CRefRankingTraderActivity::CRefRankingTraderActivity()
{
}

CRefRankingTraderActivity::~CRefRankingTraderActivity()
{
}

// 0x52b720 ctor, 0x51ff70 / 0x51ff90 slots 2 / 3, 0x627140 table description.
SR_IMPLEMENT_REF_RECORD(CRefRankingRobberActivity, CDBRecStream, CDBRecStream,
                        "TAB_REFRANKING_ROBBERACTIVITY")

CRefRankingRobberActivity::CRefRankingRobberActivity()
{
}

CRefRankingRobberActivity::~CRefRankingRobberActivity()
{
}

// 0x52b7d0 ctor, 0x51ffa0 / 0x51ffc0 slots 2 / 3, 0x6273b0 table description.
SR_IMPLEMENT_REF_RECORD(CRefRankingHunterActivity, CDBRecStream, CDBRecStream,
                        "TAB_REFRANKING_HUNTERACTIVITY")

CRefRankingHunterActivity::CRefRankingHunterActivity()
{
}

CRefRankingHunterActivity::~CRefRankingHunterActivity()
{
}

// 0x52b880 ctor, 0x51ffd0 / 0x51fff0 slots 2 / 3, 0x627620 table description.
SR_IMPLEMENT_REF_RECORD(CRefRankingTraderContribution, CDBRecStream, CDBRecStream,
                        "TAB_REFRANKING_TRADERCONTRIBUTION")

CRefRankingTraderContribution::CRefRankingTraderContribution()
{
}

CRefRankingTraderContribution::~CRefRankingTraderContribution()
{
}

// 0x52b930 ctor, 0x520000 / 0x520020 slots 2 / 3, 0x627890 table description.
SR_IMPLEMENT_REF_RECORD(CRefRankingRobberContribution, CDBRecStream, CDBRecStream,
                        "TAB_REFRANKING_ROBBERCONTRIBUTION")

CRefRankingRobberContribution::CRefRankingRobberContribution()
{
}

CRefRankingRobberContribution::~CRefRankingRobberContribution()
{
}

// 0x52b9e0 ctor, 0x520030 / 0x520050 slots 2 / 3, 0x627b00 table description.
SR_IMPLEMENT_REF_RECORD(CRefRankingHunterContribution, CDBRecStream, CDBRecStream,
                        "TAB_REFRANKING_HUNTERCONTRIBUTION")

CRefRankingHunterContribution::CRefRankingHunterContribution()
{
}

CRefRankingHunterContribution::~CRefRankingHunterContribution()
{
}

// 0x52ba90 ctor, 0x52bb00 dtor, 0x520060 / 0x520080 slots 2 / 3, 0x627d70 table description.
SR_IMPLEMENT_REF_RECORD(CRefGachaCode, CDBRecord, CDBRecord, "_REFGACHACODE")

CRefGachaCode::CRefGachaCode()
{
}

CRefGachaCode::~CRefGachaCode()
{
}

// 0x52bb70 ctor, 0x520090 / 0x5200b0 slots 2 / 3, 0x628040 table description.
SR_IMPLEMENT_REF_RECORD(CRefGachaItemSet, CDBRecStream, CDBRecStream, "_REFGACHAITEMSET")

CRefGachaItemSet::CRefGachaItemSet()
{
}

CRefGachaItemSet::~CRefGachaItemSet()
{
}

// 0x52bc20 ctor, 0x52bc90 dtor, 0x5200c0 / 0x5200e0 slots 2 / 3, 0x6282b0 table description.
SR_IMPLEMENT_REF_RECORD(CRefGachaNpcMap, CDBRecord, CDBRecord, "_REFGACHANPCMAP")

CRefGachaNpcMap::CRefGachaNpcMap()
{
    Service = 0;
    NPC_ID = 0;
    SelectionGachaID = 0;
    WasteGachaID = 0;
}

CRefGachaNpcMap::~CRefGachaNpcMap()
{
}

// ---------------------------------------------------------------------------------------------
// Shop records

// 0x61c8b0 / 0x61c8d0 slots 2 / 3, 0x61ffc0 table description.
SR_IMPLEMENT_REF_RECORD(CRefShopObject, CDBRecStream, CDBRecStream, "_REFSHOPOBJECT")

CRefShopObject::CRefShopObject()
{
}

CRefShopObject::~CRefShopObject()
{
}

// 0x52a450 ctor, 0x51f670 / 0x51f690 slots 2 / 3, 0x620230 table description.
SR_IMPLEMENT_REF_RECORD(CRefShopTabGroup, CDBRecStream, CDBRecStream, "_REFSHOPTABGROUP")

CRefShopTabGroup::CRefShopTabGroup()
{
}

CRefShopTabGroup::~CRefShopTabGroup()
{
}

// 0x52a2f0 ctor, 0x51f8e0 / 0x51f900 slots 2 / 3, 0x6204a0 table description.
SR_IMPLEMENT_REF_RECORD(CRefShopGroup, CDBRecStream, CDBRecStream, "_REFSHOPGROUP")

CRefShopGroup::CRefShopGroup()
{
}

CRefShopGroup::~CRefShopGroup()
{
}

// 0x52a3a0 ctor, 0x51f910 / 0x51f930 slots 2 / 3, 0x620710 table description.
SR_IMPLEMENT_REF_RECORD(CRefMappingShopGroup, CDBRecStream, CDBRecStream, "_REFMAPPINGSHOPGROUP")

CRefMappingShopGroup::CRefMappingShopGroup()
{
}

CRefMappingShopGroup::~CRefMappingShopGroup()
{
}

// 0x529e20 ctor, 0x51f6a0 / 0x51f6c0 slots 2 / 3, 0x620980 table description.
SR_IMPLEMENT_REF_RECORD(CRefTreatItemOfShop, CDBRecStream, CDBRecStream, "_REFTREATITEMOFSHOP")

CRefTreatItemOfShop::CRefTreatItemOfShop()
{
}

CRefTreatItemOfShop::~CRefTreatItemOfShop()
{
}

// 0x529d70 ctor, 0x529dd0 dtor, 0x51f6d0 / 0x51f6f0 slots 2 / 3, 0x620bf0 table description.
SR_IMPLEMENT_REF_RECORD(CRefAccessPermissionOfShop, CDBRecStream, CDBRecStream,
                        "_REFACCESSPERMISSIONOFSHOP")

CRefAccessPermissionOfShop::CRefAccessPermissionOfShop()
{
}

CRefAccessPermissionOfShop::~CRefAccessPermissionOfShop()
{
}

// 0x529ed0 ctor, 0x51f700 / 0x51f720 slots 2 / 3, 0x620e60 table description.
SR_IMPLEMENT_REF_RECORD(CRefRewardPolicyToSellPackageItem, CDBRecStream, CDBRecStream,
                        "_REFREWARDPOLICYTOSELLPACKAGEITEM")

CRefRewardPolicyToSellPackageItem::CRefRewardPolicyToSellPackageItem()
{
}

CRefRewardPolicyToSellPackageItem::~CRefRewardPolicyToSellPackageItem()
{
}

// 0x529f80 ctor, 0x51f730 / 0x51f750 slots 2 / 3, 0x6210d0 table description.
SR_IMPLEMENT_REF_RECORD(CRefRewardPolicyToBuyScrapItem, CDBRecStream, CDBRecStream,
                        "_REFREWARDPOLICYTOBUYSCRAPITEM")

CRefRewardPolicyToBuyScrapItem::CRefRewardPolicyToBuyScrapItem()
{
}

CRefRewardPolicyToBuyScrapItem::~CRefRewardPolicyToBuyScrapItem()
{
}

// 0x52a030 ctor, 0x51f760 / 0x51f780 slots 2 / 3, 0x621340 table description.
SR_IMPLEMENT_REF_RECORD(CRefConditionToSellPackageItem, CDBRecStream, CDBRecStream,
                        "_REFCONDITIONTOSELLPACKAGEITEM")

CRefConditionToSellPackageItem::CRefConditionToSellPackageItem()
{
}

CRefConditionToSellPackageItem::~CRefConditionToSellPackageItem()
{
}

// 0x52a240 ctor, 0x51f790 / 0x51f7b0 slots 2 / 3, 0x6215b0 table description.
SR_IMPLEMENT_REF_RECORD(CRefConditionToBuyScrapItem, CDBRecStream, CDBRecStream,
                        "_REFCONDITIONTOBUYSCRAPITEM")

CRefConditionToBuyScrapItem::CRefConditionToBuyScrapItem()
{
}

CRefConditionToBuyScrapItem::~CRefConditionToBuyScrapItem()
{
}

// 0x52a0e0 ctor, 0x51f7c0 / 0x51f7e0 slots 2 / 3, 0x621820 table description.
SR_IMPLEMENT_REF_RECORD(CRefRewardPolicyToSellScrapItem, CDBRecStream, CDBRecStream,
                        "_REFREWARDPOLICYTOSELLSCRAPITEM")

CRefRewardPolicyToSellScrapItem::CRefRewardPolicyToSellScrapItem()
{
}

CRefRewardPolicyToSellScrapItem::~CRefRewardPolicyToSellScrapItem()
{
}

// 0x52a190 ctor, 0x51f7f0 / 0x51f810 slots 2 / 3, 0x621a90 table description.
SR_IMPLEMENT_REF_RECORD(CRefConditionToSellScrapItem, CDBRecStream, CDBRecStream,
                        "_REFCONDITIONTOSELLSCRAPITEM")

CRefConditionToSellScrapItem::CRefConditionToSellScrapItem()
{
}

CRefConditionToSellScrapItem::~CRefConditionToSellScrapItem()
{
}

// 0x529cc0 ctor, 0x51f820 / 0x51f840 slots 2 / 3, 0x621d00 table description.
SR_IMPLEMENT_REF_RECORD(CRefPricePolicyOfItem, CDBRecStream, CDBRecStream, "_REFPRICEPOLICYOFITEM")

CRefPricePolicyOfItem::CRefPricePolicyOfItem()
{
}

CRefPricePolicyOfItem::~CRefPricePolicyOfItem()
{
}

// 0x529c10 ctor, 0x51f880 / 0x51f8a0 slots 2 / 3, 0x621f70 table description.
SR_IMPLEMENT_REF_RECORD(CRefPackageItem, CDBRecStream, CDBRecStream, "_REFPACKAGEITEM")

CRefPackageItem::CRefPackageItem()
{
}

CRefPackageItem::~CRefPackageItem()
{
}

// 0x529b60 ctor, 0x51f8b0 / 0x51f8d0 slots 2 / 3, 0x6221e0 table description.
SR_IMPLEMENT_REF_RECORD(CRefScrapOfPackageItem, CDBRecStream, CDBRecStream,
                        "_REFSCRAPOFPACKAGEITEM")

CRefScrapOfPackageItem::CRefScrapOfPackageItem()
{
}

CRefScrapOfPackageItem::~CRefScrapOfPackageItem()
{
}

// 0x529ab0 ctor, 0x51f850 / 0x51f870 slots 2 / 3, 0x622450 table description.
SR_IMPLEMENT_REF_RECORD(CRefMappingShopWithTab, CDBRecStream, CDBRecStream,
                        "_REFMAPPINGSHOPWITHTAB")

CRefMappingShopWithTab::CRefMappingShopWithTab()
{
}

CRefMappingShopWithTab::~CRefMappingShopWithTab()
{
}

// 0x52a500 ctor, 0x51f940 / 0x51f960 slots 2 / 3, 0x6226c0 table description.
SR_IMPLEMENT_REF_RECORD(CRefShopGoods, CDBRecStream, CDBRecStream, "_REFSHOPGOODS")

CRefShopGoods::CRefShopGoods()
{
}

CRefShopGoods::~CRefShopGoods()
{
}

// 0x52a5b0 ctor, 0x51f970 / 0x51f990 slots 2 / 3, 0x622930 table description.
SR_IMPLEMENT_REF_RECORD(CRefShopTab, CDBRecStream, CDBRecStream, "_REFSHOPTAB")

CRefShopTab::CRefShopTab()
{
}

CRefShopTab::~CRefShopTab()
{
}

// 0x52a660 ctor, 0x51f9a0 / 0x51f9c0 slots 2 / 3, 0x622ba0 table description.
SR_IMPLEMENT_REF_RECORD(CRefShop, CDBRecStream, CDBRecStream, "_REFSHOP")

CRefShop::CRefShop()
{
}

CRefShop::~CRefShop()
{
}

// 0x52cf10 ctor, 0x5203f0 / 0x520410 slots 2 / 3, 0x622e10 table description.
SR_IMPLEMENT_REF_RECORD(CRefShopItemStockPeriod, CDBRecStream, CDBRecStream,
                        "_REFSHOPITEMSTOCKPERIOD")

CRefShopItemStockPeriod::CRefShopItemStockPeriod()
{
}

CRefShopItemStockPeriod::~CRefShopItemStockPeriod()
{
}

// ---------------------------------------------------------------------------------------------
// Drop records

// 0x52c6b0 ctor, 0x51f490 / 0x51f4b0 slots 2 / 3, 0x61dc50 table description.
SR_IMPLEMENT_REF_RECORD(CRefMonster_AssignedItemDrop, CDBRecStream, CDBRecStream,
                        "_REFMONSTER_ASSIGNEDITEMDROP")

CRefMonster_AssignedItemDrop::CRefMonster_AssignedItemDrop()
{
}

CRefMonster_AssignedItemDrop::~CRefMonster_AssignedItemDrop()
{
}

// 0x52c760 ctor, 0x51f4c0 / 0x51f4e0 slots 2 / 3, 0x61dec0 table description.
SR_IMPLEMENT_REF_RECORD(CRefCustomizingReservedItemDropForMonster, CDBRecStream, CDBRecStream,
                        "_REFCUSTOMIZINGRESERVEDITEMDROPFORMONSTER")

CRefCustomizingReservedItemDropForMonster::CRefCustomizingReservedItemDropForMonster()
{
}

CRefCustomizingReservedItemDropForMonster::~CRefCustomizingReservedItemDropForMonster()
{
}

// 0x52c970 ctor, 0x51fc40 / 0x51fc60 slots 2 / 3, 0x61e130 table description.
SR_IMPLEMENT_REF_RECORD(CRefMonster_AssignedItemRndDrop, CDBRecStream, CDBRecStream,
                        "_REFMONSTER_ASSIGNEDITEMRNDDROP")

CRefMonster_AssignedItemRndDrop::CRefMonster_AssignedItemRndDrop()
{
}

CRefMonster_AssignedItemRndDrop::~CRefMonster_AssignedItemRndDrop()
{
}

// 0x52c8c0 ctor, 0x51fc70 / 0x51fc90 slots 2 / 3, 0x61e3a0 table description.
SR_IMPLEMENT_REF_RECORD(CRefDropItemGroup, CDBRecStream, CDBRecStream, "_REFDROPITEMGROUP")

CRefDropItemGroup::CRefDropItemGroup()
{
}

CRefDropItemGroup::~CRefDropItemGroup()
{
}

// 0x52a710 ctor, 0x51f9d0 / 0x51f9f0 slots 2 / 3, 0x623080 table description.
SR_IMPLEMENT_REF_RECORD(CRefDropGold, CDBRecStream, CDBRecStream, "_REFDROPGOLD")

CRefDropGold::CRefDropGold()
{
}

CRefDropGold::~CRefDropGold()
{
}

// 0x52a7c0 ctor, 0x51fa00 / 0x51fa20 slots 2 / 3, 0x6232f0 table description.
SR_IMPLEMENT_REF_RECORD(CRefDropOptLevelSel, CDBRecStream, CDBRecStream, "_REFDROPOPTLVLSEL")

CRefDropOptLevelSel::CRefDropOptLevelSel()
{
}

CRefDropOptLevelSel::~CRefDropOptLevelSel()
{
}

// 0x52a870 ctor, 0x52a8d0 dtor, 0x51fa30 / 0x51fa50 slots 2 / 3, 0x623560 table description.
SR_IMPLEMENT_REF_RECORD(CRefDropClassSel_Equip, CDBRecStream, CDBRecStream,
                        "_REFDROPCLASSSEL_EQUIP")

CRefDropClassSel_Equip::CRefDropClassSel_Equip()
{
}

CRefDropClassSel_Equip::~CRefDropClassSel_Equip()
{
}

// 0x52a920 ctor, 0x51fa60 / 0x51fa80 slots 2 / 3, 0x6237d0 table description.
SR_IMPLEMENT_REF_RECORD(CRefDropClassSel_RareEquip, CDBRecStream, CDBRecStream,
                        "_REFDROPCLASSSEL_RAREEQUIP")

CRefDropClassSel_RareEquip::CRefDropClassSel_RareEquip()
{
}

CRefDropClassSel_RareEquip::~CRefDropClassSel_RareEquip()
{
}

// 0x52a9d0 ctor, 0x51fa90 / 0x51fab0 slots 2 / 3, 0x623a40 table description.
SR_IMPLEMENT_REF_RECORD(CRefDropClassSel_Recover, CDBRecStream, CDBRecStream,
                        "_REFDROPCLASSSEL_RECOVER")

CRefDropClassSel_Recover::CRefDropClassSel_Recover()
{
}

CRefDropClassSel_Recover::~CRefDropClassSel_Recover()
{
}

// 0x52aa80 ctor, 0x51fac0 / 0x51fae0 slots 2 / 3, 0x623cb0 table description.
SR_IMPLEMENT_REF_RECORD(CRefDropClassSel_Cure, CDBRecStream, CDBRecStream, "_REFDROPCLASSSEL_CURE")

CRefDropClassSel_Cure::CRefDropClassSel_Cure()
{
}

CRefDropClassSel_Cure::~CRefDropClassSel_Cure()
{
}

// 0x52ab30 ctor, 0x51faf0 / 0x51fb10 slots 2 / 3, 0x623f20 table description.
SR_IMPLEMENT_REF_RECORD(CRefDropClassSel_Ammo, CDBRecStream, CDBRecStream, "_REFDROPCLASSSEL_AMMO")

CRefDropClassSel_Ammo::CRefDropClassSel_Ammo()
{
}

CRefDropClassSel_Ammo::~CRefDropClassSel_Ammo()
{
}

// 0x52abe0 ctor, 0x51fb20 / - slots 2 / 3, 0x624190 table description.
SR_IMPLEMENT_REF_RECORD(CRefDropClassSel_Scroll, CDBRecStream, CDBRecStream,
                        "_REFDROPCLASSSEL_SCROLL")

CRefDropClassSel_Scroll::CRefDropClassSel_Scroll()
{
}

CRefDropClassSel_Scroll::~CRefDropClassSel_Scroll()
{
}

// 0x52ac90 ctor, 0x51fb50 / 0x51fb70 slots 2 / 3, 0x624400 table description.
SR_IMPLEMENT_REF_RECORD(CRefDropClassSel_Reinforce, CDBRecStream, CDBRecStream,
                        "_REFDROPCLASSSEL_REINFORCE")

CRefDropClassSel_Reinforce::CRefDropClassSel_Reinforce()
{
}

CRefDropClassSel_Reinforce::~CRefDropClassSel_Reinforce()
{
}

// 0x52ad40 ctor, 0x51fb80 / 0x51fba0 slots 2 / 3, 0x624670 table description.
SR_IMPLEMENT_REF_RECORD(CRefDropClassSel_AlchemyTablet, CDBRecStream, CDBRecStream,
                        "_REFDROPCLASSSEL_ALCHEMY_TABLET")

CRefDropClassSel_AlchemyTablet::CRefDropClassSel_AlchemyTablet()
{
}

CRefDropClassSel_AlchemyTablet::~CRefDropClassSel_AlchemyTablet()
{
}

// 0x52adf0 ctor, 0x51fbb0 / 0x51fbd0 slots 2 / 3, 0x6248e0 table description.
SR_IMPLEMENT_REF_RECORD(CRefDropClassSel_AlchemyMagicStone, CDBRecStream, CDBRecStream,
                        "_REFDROPCLASSSEL_ALCHEMY_MAGICSTONE")

CRefDropClassSel_AlchemyMagicStone::CRefDropClassSel_AlchemyMagicStone()
{
}

CRefDropClassSel_AlchemyMagicStone::~CRefDropClassSel_AlchemyMagicStone()
{
}

// 0x52aea0 ctor, 0x51fbe0 / 0x51fc00 slots 2 / 3, 0x624b50 table description.
SR_IMPLEMENT_REF_RECORD(CRefDropClassSel_AlchemyATTRStone, CDBRecStream, CDBRecStream,
                        "_REFDROPCLASSSEL_ALCHEMY_ATTRSTONE")

CRefDropClassSel_AlchemyATTRStone::CRefDropClassSel_AlchemyATTRStone()
{
}

CRefDropClassSel_AlchemyATTRStone::~CRefDropClassSel_AlchemyATTRStone()
{
}

// 0x52af50 ctor, 0x51fc10 / 0x51fc30 slots 2 / 3, 0x624dc0 table description.
SR_IMPLEMENT_REF_RECORD(CRefDropItemAssign, CDBRecStream, CDBRecStream, "_REFDROPITEMASSIGN")

CRefDropItemAssign::CRefDropItemAssign()
{
}

CRefDropItemAssign::~CRefDropItemAssign()
{
}

// ---------------------------------------------------------------------------------------------
// Siege fortress records

// 0x52bce0 ctor, 0x52bd40 dtor, 0x5200f0 / 0x520110 slots 2 / 3, 0x628540 table description.
SR_IMPLEMENT_REF_RECORD(CRefSiegeFortress, CDBRecord, CDBRecord, "_REFSIEGEFORTRESS")

CRefSiegeFortress::CRefSiegeFortress()
{
}

CRefSiegeFortress::~CRefSiegeFortress()
{
}

// 0x52bdb0 ctor, 0x52be10 dtor, 0x520120 / - slots 2 / 3, 0x6287f0 table description.
SR_IMPLEMENT_REF_RECORD(CRefSiegeFortressGuard, CDBRecord, CDBRecord, "_REFSIEGEFORTRESSGUARD")

CRefSiegeFortressGuard::CRefSiegeFortressGuard()
{
    Service = 0;
    FortressID = 0;
    GuardRefObjID = 0;
}

CRefSiegeFortressGuard::~CRefSiegeFortressGuard()
{
}

// 0x52be60 ctor, 0x52bed0 dtor, 0x520150 / 0x520170 slots 2 / 3, 0x628a70 table description.
SR_IMPLEMENT_REF_RECORD(CRefSiegeFortressItemForge, CDBRecord, CDBRecord,
                        "_REFSIEGEFORTRESSITEMFORGE")

CRefSiegeFortressItemForge::CRefSiegeFortressItemForge()
{
    Service = 0;
    FortressID = 0;
    RefItemID = 0;
    ReqGold = 0;
    ReqGP = 0;
    ForgeTimeMin = 0;
}

CRefSiegeFortressItemForge::~CRefSiegeFortressItemForge()
{
}

// 0x52bf20 ctor, 0x52bf80 dtor, 0x520180 / 0x5201a0 slots 2 / 3, 0x628cf0 table description.
SR_IMPLEMENT_REF_RECORD(CRefSiegeFortressBattleRank, CDBRecord, CDBRecord,
                        "_REFSIEGEFORTRESSBATTLERANK")

CRefSiegeFortressBattleRank::CRefSiegeFortressBattleRank()
{
}

CRefSiegeFortressBattleRank::~CRefSiegeFortressBattleRank()
{
}

// 0x52bff0 ctor, 0x52c050 dtor, 0x5201b0 / 0x5201d0 slots 2 / 3, 0x628f90 table description.
SR_IMPLEMENT_REF_RECORD(CRefSiegeStructUpgrade, CDBRecord, CDBRecord, "_REFSIEGESTRUCTUPGRADE")

CRefSiegeStructUpgrade::CRefSiegeStructUpgrade()
{
}

CRefSiegeStructUpgrade::~CRefSiegeStructUpgrade()
{
}

// 0x52c0c0 ctor, 0x52c120 dtor, 0x5201e0 / 0x520200 slots 2 / 3, 0x629220 table description.
SR_IMPLEMENT_REF_RECORD(CRefSiegeLvlSummonMonster, CDBRecord, CDBRecord,
                        "_REFSIEGELVLSUMMONMONSTER")

CRefSiegeLvlSummonMonster::CRefSiegeLvlSummonMonster()
{
    Service = 0;
    RefObjID = 0;
    RefOrgObjID = 0;
}

CRefSiegeLvlSummonMonster::~CRefSiegeLvlSummonMonster()
{
}

// 0x52c170 ctor, 0x52c1e0 dtor, 0x520210 / 0x520230 slots 2 / 3, 0x6294a0 table description.
SR_IMPLEMENT_REF_RECORD(CRefSiegeFortressRewards, CDBRecord, CDBRecord, "_REFSIEGEFORTRESSREWARDS")

CRefSiegeFortressRewards::CRefSiegeFortressRewards()
{
    Service = 0;
    FortressID = 0;
    RewardTypeID = 0;
    RewardValue = 0;
    RewardCount = 0;
}

CRefSiegeFortressRewards::~CRefSiegeFortressRewards()
{
}

// 0x52c230 ctor, 0x52c2a0 dtor, 0x520240 / 0x520260 slots 2 / 3, 0x629720 table description.
SR_IMPLEMENT_REF_RECORD(CRefSiegeBlessBuff, CDBRecord, CDBRecord, "_REFSIEGEBLESSBUFF")

CRefSiegeBlessBuff::CRefSiegeBlessBuff()
{
    Service = 0;
    BlessID = 0;
    FortressID = 0;
    RefBlessBuffID = 0;
    NeedGold = 0;
    NeedGP = 0;
}

CRefSiegeBlessBuff::~CRefSiegeBlessBuff()
{
}

// 0x52c2f0 ctor, 0x52c360 dtor, 0x520270 / 0x520290 slots 2 / 3, 0x6299a0 table description.
SR_IMPLEMENT_REF_RECORD(CRefSiegeDungeon, CDBRecord, CDBRecord, "_REFSIEGEDUNGEON")

CRefSiegeDungeon::CRefSiegeDungeon()
{
    Service = 0;
    FortressID = 0;
    WorldID = 0;
    MaxCreateCount = 0;
    EntryGold = 0;
    EntryGP = 0;
}

CRefSiegeDungeon::~CRefSiegeDungeon()
{
}

// 0x52c3b0 ctor, 0x52c420 dtor, 0x5202a0 / 0x5202c0 slots 2 / 3, 0x629c20 table description.
SR_IMPLEMENT_REF_RECORD(CRefSiegeQuest, CDBRecord, CDBRecord, "_REFSIEGEQUEST")

CRefSiegeQuest::CRefSiegeQuest()
{
}

CRefSiegeQuest::~CRefSiegeQuest()
{
}

// 0x52c490 ctor, 0x52c500 dtor, 0x5202d0 / 0x5202f0 slots 2 / 3, 0x629ef0 table description.
SR_IMPLEMENT_REF_RECORD(CRefSiegeQuestReward, CDBRecord, CDBRecord, "_REFSIEGEQUESTREWARD")

CRefSiegeQuestReward::CRefSiegeQuestReward()
{
    Service = 0;
    QuestID = 0;
    RewardType = 0;
    RewardRefID = 0;
    RewardValue = 0;
}

CRefSiegeQuestReward::~CRefSiegeQuestReward()
{
}
