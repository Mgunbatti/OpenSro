#include "ServerCommon/RefRecords.h"

#include <string.h>

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
SR_IMPLEMENT_RECORD(CRefRegion, CDBRecStream, "_REFREGION", DBTABLE_04, NULL, 0)

// 0x529070 ctor, 0x5290d0 dtor, 0x51f340 / 0x51f360 slots 2 / 3, 0x61cd80 table description.
SR_IMPLEMENT_RECORD(CRefLevel, CDBRecord, "_REFLEVEL", DBTABLE_04, NULL, 0)

// 0x528fa0 ctor, 0x529020 dtor, 0x51f370 / 0x51f390 slots 2 / 3, 0x61d000 table description.
SR_IMPLEMENT_RECORD(CRefClimate, CDBRecord, "_REFCLIMATE", DBTABLE_04, NULL, 0)

RefClimateData::RefClimateData()
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

// 0x529120 ctor, 0x529180 dtor, 0x51ff10 / 0x51ff30 slots 2 / 3, 0x626c00 table description.
SR_IMPLEMENT_RECORD(CRefRegionBindAssocServer, CDBRecord, "_REFREGIONBINDASSOCSERVER", DBTABLE_04, NULL, 0)

// 0x52b000 ctor, 0x51f3d0 / 0x51f3f0 slots 2 / 3, 0x61d280 table description.
SR_IMPLEMENT_RECORD(CRefCharGenData, CDBRecStream, "_REFCHARGEN", DBTABLE_04, NULL, 0)

// 0x529440 ctor, 0x51f400 / 0x51f420 slots 2 / 3, 0x61d4f0 table description.
SR_IMPLEMENT_RECORD(CRefObjCommon, CDBRecStream, "_REFOBJCOMMON", DBTABLE_04, NULL, 0)

// 0x5294f0 ctor, 0x529550 dtor, 0x51f430 / 0x51f450 slots 2 / 3, 0x61d770 table description.
SR_IMPLEMENT_RECORD(CRefObjItem, CRefObjDerived, "_REFOBJITEM", DBTABLE_04, NULL, 0)

// 0x5295a0 ctor, 0x529600 dtor, 0x51f460 / 0x51f480 slots 2 / 3, 0x61d9e0 table description.
SR_IMPLEMENT_RECORD(CRefObjChar, CRefObjDerived, "_REFOBJCHAR", DBTABLE_04, NULL, 0)

// 0x529650 ctor, 0x5296b0 dtor, 0x51f4f0 / 0x51f510 slots 2 / 3, 0x61eaf0 table description.
SR_IMPLEMENT_RECORD(CRefObjStruct, CRefObjDerived, "_REFOBJSTRUCT", DBTABLE_04, NULL, 0)

// 0x52d070 ctor, 0x520450 / 0x520470 slots 2 / 3, 0x62b080 table description.
SR_IMPLEMENT_RECORD(CRefObjCharExtraSkill, CDBRecStream, "_REFOBJCHAREXTRASKILL", DBTABLE_04, NULL, 0)

// 0x5292a0 ctor, 0x529300 dtor, 0x51fdf0 / 0x51fe10 slots 2 / 3, 0x625cd0 table description.
SR_IMPLEMENT_RECORD(CRefSkill, CDBRecord, "_REFSKILL", DBTABLE_04, NULL, 0)

// 0x529370 ctor, 0x5293d0 dtor, 0x51fdc0 / 0x51fde0 slots 2 / 3, 0x6259f0 table description.
SR_IMPLEMENT_RECORD(CRefSkillMastery, CDBRecord, "_REFSKILLMASTERY", DBTABLE_04, NULL, 0)

// 0x529700 ctor, 0x529760 dtor, 0x51f520 / 0x51f540 slots 2 / 3, 0x61ed70 table description.
SR_IMPLEMENT_RECORD(CRefMagicOpt, CDBRecord, "_REFMAGICOPT", DBTABLE_04, NULL, 0)

// 0x5297d0 ctor, 0x529830 dtor, 0x51f550 / 0x51f570 slots 2 / 3, 0x61f010 table description.
SR_IMPLEMENT_RECORD(CRefMagicOptAssign, CDBRecord, "_REFMAGICOPTASSIGN", DBTABLE_04, NULL, 0)

// 0x52c810 ctor, 0x51fca0 / 0x51fcc0 slots 2 / 3, 0x61e610 table description.
SR_IMPLEMENT_RECORD(CRefMagicOptGroup, CDBRecStream, "_REFMAGICOPTGROUP", DBTABLE_04, NULL, 0)

// 0x52ce60 ctor, 0x51fcd0 / 0x51fcf0 slots 2 / 3, 0x61e880 table description.
SR_IMPLEMENT_RECORD(CRefSetItemGroup, CDBRecStream, "_REFSETITEMGROUP", DBTABLE_04, NULL, 0)

// 0x52cc30 ctor, 0x520300 / 0x520320 slots 2 / 3, 0x62a170 table description.
SR_IMPLEMENT_RECORD(CRefAbilityByItemOptLevel, CDBRecStream, "_REFABILITYBYITEMOPTLEVEL", DBTABLE_04, NULL, 0)

// 0x52cb80 ctor, 0x520330 / 0x520350 slots 2 / 3, 0x62a3e0 table description.
SR_IMPLEMENT_RECORD(CRefMagicOptByItemOptLevel, CDBRecStream, "_REFMAGICOPTBYITEMOPTLEVEL", DBTABLE_04, NULL, 0)

// 0x52cad0 ctor, 0x520360 / 0x520380 slots 2 / 3, 0x62a650 table description.
SR_IMPLEMENT_RECORD(CRefSkillByItemOptLevel, CDBRecStream, "_REFSKILLBYITEMOPTLEVEL", DBTABLE_04, NULL, 0)

// 0x52d1d0 ctor, 0x5204b0 / 0x5204d0 slots 2 / 3, 0x62b560 table description.
SR_IMPLEMENT_RECORD(CRefAlchemyMerit, CDBRecStream, "_REFALCHEMYMERIT", DBTABLE_04, NULL, 0)

// 0x52c550 ctor, 0x51fe50 / 0x51fe70 slots 2 / 3, 0x626200 table description.
SR_IMPLEMENT_RECORD(CRefHWANLevel, CDBRecStream, "_REFHWANLEVEL", DBTABLE_04, NULL, 0)

// 0x52b2c0 ctor, 0x51fd90 / 0x51fdb0 slots 2 / 3, 0x625780 table description.
SR_IMPLEMENT_RECORD(CRefAISkill, CDBRecStream, "TAB_REFAISKILL", DBTABLE_04, NULL, 0)

// 0x52b0b0 ctor, 0x51fd00 / 0x51fd20 slots 2 / 3, 0x625030 table description.
SR_IMPLEMENT_RECORD(CRefHive, CDBRecStream, "TAB_REFHIVE", DBTABLE_04, NULL, 0)

// 0x52b160 ctor, 0x51fd30 / 0x51fd50 slots 2 / 3, 0x6252a0 table description.
SR_IMPLEMENT_RECORD(CRefTactics, CDBRecStream, "TAB_REFTACTICS", DBTABLE_04, NULL, 0)

// 0x52b210 ctor, 0x51fd60 / 0x51fd80 slots 2 / 3, 0x625510 table description.
SR_IMPLEMENT_RECORD(CRefNest, CDBRecStream, "TAB_REFNEST", DBTABLE_04, NULL, 0)

// ---------------------------------------------------------------------------------------------
// World, teleport, quest and event records

// 0x5298a0 ctor, 0x51f580 / 0x51f5a0 slots 2 / 3, 0x61f2c0 table description.
SR_IMPLEMENT_RECORD(CRefTeleport, CDBRecStream, "_REFTELEPORT", DBTABLE_04, NULL, 0)

// 0x529950 ctor, 0x51f5b0 / 0x51f5d0 slots 2 / 3, 0x61f530 table description.
SR_IMPLEMENT_RECORD(CRefTeleLink, CDBRecStream, "_REFTELELINK", DBTABLE_04, NULL, 0)

// 0x52cce0 ctor, 0x52cd40 dtor, 0x520390 / 0x5203b0 slots 2 / 3, 0x62a8c0 table description.
SR_IMPLEMENT_RECORD(CRefOptionalTeleport, CDBRecStream, "_REFOPTIONALTELEPORT", DBTABLE_04, NULL, 0)

// 0x529a00 ctor, 0x51f5e0 / 0x51f600 slots 2 / 3, 0x61f7a0 table description.
SR_IMPLEMENT_RECORD(CRefEventZone, CDBRecStream, "_REFEVENTZONE", DBTABLE_04, NULL, 0)

// 0x52c600 ctor, 0x51f610 / - slots 2 / 3, 0x61fa10 table description.
SR_IMPLEMENT_RECORD(CRefEvent, CDBRecStream, "_REFEVENT", DBTABLE_04, NULL, 0)

// 0x52ca20 ctor, 0x51f640 / 0x51f660 slots 2 / 3, 0x61fc80 table description.
SR_IMPLEMENT_RECORD(CRefDefaultQuest, CDBRecStream, "_REFCHARDEFAULT_QUEST", DBTABLE_04, NULL, 0)

// 0x52b370 ctor, 0x52b3d0 dtor, 0x51fe20 / 0x51fe40 slots 2 / 3, 0x625f70 table description.
SR_IMPLEMENT_RECORD(CRefQuest, CDBRecord, "_REFQUEST", DBTABLE_04, NULL, 0)

// 0x52d5f0 ctor, 0x5205d0 / 0x5205f0 slots 2 / 3, 0x62c400 table description.
SR_IMPLEMENT_RECORD(CRefQuestReward, CDBRecStream, "_RefQuestReward", DBTABLE_04, NULL, 0)

// 0x52d6a0 ctor, 0x520600 / 0x520620 slots 2 / 3, 0x62c670 table description.
SR_IMPLEMENT_RECORD(CRefQuestRewardItems, CDBRecStream, "_RefQuestRewardItems", DBTABLE_04, NULL, 0)

// 0x52d750 ctor, 0x520630 / 0x520650 slots 2 / 3, 0x62c8e0 table description.
SR_IMPLEMENT_RECORD(CRefEventReward, CDBRecStream, "_RefEventReward", DBTABLE_04, NULL, 0)

// 0x52d800 ctor, 0x520660 / 0x520680 slots 2 / 3, 0x62cb50 table description.
SR_IMPLEMENT_RECORD(CRefEventRewardItems, CDBRecStream, "_RefEventRewardItems", DBTABLE_04, NULL, 0)

// 0x52b440 ctor, 0x52b4a0 dtor, 0x51fe80 / 0x51fea0 slots 2 / 3, 0x626470 table description.
SR_IMPLEMENT_RECORD(CRefGameWorld, CDBRecord, "_REFGAME_WORLD", DBTABLE_04, NULL, 0)

// 0x52cfc0 ctor, 0x520420 / 0x520440 slots 2 / 3, 0x62ae10 table description.
SR_IMPLEMENT_RECORD(CRefGameWorldNPC, CDBRecStream, "_REFGAMEWORLDNPC", DBTABLE_04, NULL, 0)

// 0x52d120 ctor, 0x52d180 dtor, 0x520480 / 0x5204a0 slots 2 / 3, 0x62b2f0 table description.
SR_IMPLEMENT_RECORD(CRefGameWorldConfig, CDBRecStream, "_REFGAME_WORLD_CONFIG", DBTABLE_04, NULL, 0)

// 0x52b510 ctor, 0x52b570 dtor, 0x51feb0 / 0x51fed0 slots 2 / 3, 0x626700 table description.
SR_IMPLEMENT_RECORD(CRefInstanceWorldRegion, CDBRecord, "_REFINSTANCE_WORLD_REGION", DBTABLE_04, NULL, 0)

// 0x52b5c0 ctor, 0x52b620 dtor, 0x51fee0 / 0x51ff00 slots 2 / 3, 0x626980 table description.
SR_IMPLEMENT_RECORD(CRefInstanceWorldStartPos, CDBRecord, "_REFINSTANCE_WORLD_START_POS", DBTABLE_04, NULL, 0)

// 0x52cda0 ctor, 0x52ce00 dtor, 0x5203c0 / 0x5203e0 slots 2 / 3, 0x62ab90 table description.
SR_IMPLEMENT_RECORD(CRefShardContentConfig, CDBRecStream, "_REFSHARDCONTENTCONFIG", DBTABLE_04, NULL, 0)

// 0x52da10 ctor, 0x52da70 dtor, 0x5206f0 / 0x520710 slots 2 / 3, 0x62d2b0 table description.
SR_IMPLEMENT_RECORD(CRefUIString, CDBRecStream, "_RefUIString_Mt", DBTABLE_04, NULL, 0)

// 0x52d280 ctor, 0x5204e0 / 0x520500 slots 2 / 3, 0x62b7d0 table description.
SR_IMPLEMENT_RECORD(CRefServerEvent, CDBRecStream, "_RefServerEvent", DBTABLE_04, NULL, 0)

// 0x52d330 ctor, 0x520510 / 0x520530 slots 2 / 3, 0x62ba40 table description.
SR_IMPLEMENT_RECORD(CRefServerEventReward, CDBRecStream, "_RefServerEventReward", DBTABLE_04, NULL, 0)

// 0x52d3e0 ctor, 0x520540 / 0x520560 slots 2 / 3, 0x62bcb0 table description.
SR_IMPLEMENT_RECORD(CRefServerEventReward_ExpUPForPlayers, CDBRecStream, "_RefServerEventReward_ExpUPForPlayers", DBTABLE_04, NULL, 0)

// 0x52d490 ctor, 0x520570 / 0x520590 slots 2 / 3, 0x62bf20 table description.
SR_IMPLEMENT_RECORD(CRefServerEventReward_SpawnMonster, CDBRecStream, "_RefServerEventReward_SpawnMonster", DBTABLE_04, NULL, 0)

// 0x52d540 ctor, 0x5205a0 / 0x5205c0 slots 2 / 3, 0x62c190 table description.
SR_IMPLEMENT_RECORD(CRefRentItem, CDBRecStream, "_RefRentItem", DBTABLE_04, NULL, 0)

// 0x52d8b0 ctor, 0x520690 / 0x5206b0 slots 2 / 3, 0x62cdc0 table description.
SR_IMPLEMENT_RECORD(CRefCollectionBook_Theme, CDBRecStream, "_RefCollectionBook_Theme", DBTABLE_04, NULL, 0)

// 0x52d960 ctor, 0x5206c0 / 0x5206e0 slots 2 / 3, 0x62d030 table description.
SR_IMPLEMENT_RECORD(CRefCollectionBook_Item, CDBRecStream, "_RefCollectionBook_Item", DBTABLE_04, NULL, 0)

// 0x52b670 ctor, 0x51ff40 / 0x51ff60 slots 2 / 3, 0x626ed0 table description.
SR_IMPLEMENT_RECORD(CRefRankingTraderActivity, CDBRecStream, "TAB_REFRANKING_TRADERACTIVITY", DBTABLE_04, NULL, 0)

// 0x52b720 ctor, 0x51ff70 / 0x51ff90 slots 2 / 3, 0x627140 table description.
SR_IMPLEMENT_RECORD(CRefRankingRobberActivity, CDBRecStream, "TAB_REFRANKING_ROBBERACTIVITY", DBTABLE_04, NULL, 0)

// 0x52b7d0 ctor, 0x51ffa0 / 0x51ffc0 slots 2 / 3, 0x6273b0 table description.
SR_IMPLEMENT_RECORD(CRefRankingHunterActivity, CDBRecStream, "TAB_REFRANKING_HUNTERACTIVITY", DBTABLE_04, NULL, 0)

// 0x52b880 ctor, 0x51ffd0 / 0x51fff0 slots 2 / 3, 0x627620 table description.
SR_IMPLEMENT_RECORD(CRefRankingTraderContribution, CDBRecStream, "TAB_REFRANKING_TRADERCONTRIBUTION", DBTABLE_04, NULL, 0)

// 0x52b930 ctor, 0x520000 / 0x520020 slots 2 / 3, 0x627890 table description.
SR_IMPLEMENT_RECORD(CRefRankingRobberContribution, CDBRecStream, "TAB_REFRANKING_ROBBERCONTRIBUTION", DBTABLE_04, NULL, 0)

// 0x52b9e0 ctor, 0x520030 / 0x520050 slots 2 / 3, 0x627b00 table description.
SR_IMPLEMENT_RECORD(CRefRankingHunterContribution, CDBRecStream, "TAB_REFRANKING_HUNTERCONTRIBUTION", DBTABLE_04, NULL, 0)

// 0x52ba90 ctor, 0x52bb00 dtor, 0x520060 / 0x520080 slots 2 / 3, 0x627d70 table description.
SR_IMPLEMENT_RECORD(CRefGachaCode, CDBRecord, "_REFGACHACODE", DBTABLE_04, NULL, 0)

// 0x52bb70 ctor, 0x520090 / 0x5200b0 slots 2 / 3, 0x628040 table description.
SR_IMPLEMENT_RECORD(CRefGachaItemSet, CDBRecStream, "_REFGACHAITEMSET", DBTABLE_04, NULL, 0)

// 0x52bc20 ctor, 0x52bc90 dtor, 0x5200c0 / 0x5200e0 slots 2 / 3, 0x6282b0 table description.
SR_IMPLEMENT_RECORD(CRefGachaNpcMap, CDBRecord, "_REFGACHANPCMAP", DBTABLE_04, NULL, 0)

RefGachaNpcMapData::RefGachaNpcMapData()
{
    Service = 0;
    NPC_ID = 0;
    SelectionGachaID = 0;
    WasteGachaID = 0;
}

// ---------------------------------------------------------------------------------------------
// Shop records

// 0x61c8b0 / 0x61c8d0 slots 2 / 3, 0x61ffc0 table description.
SR_IMPLEMENT_RECORD(CRefShopObject, CDBRecStream, "_REFSHOPOBJECT", DBTABLE_04, NULL, 0)

// 0x52a450 ctor, 0x51f670 / 0x51f690 slots 2 / 3, 0x620230 table description.
SR_IMPLEMENT_RECORD(CRefShopTabGroup, CDBRecStream, "_REFSHOPTABGROUP", DBTABLE_04, NULL, 0)

// 0x52a2f0 ctor, 0x51f8e0 / 0x51f900 slots 2 / 3, 0x6204a0 table description.
SR_IMPLEMENT_RECORD(CRefShopGroup, CDBRecStream, "_REFSHOPGROUP", DBTABLE_04, NULL, 0)

// 0x52a3a0 ctor, 0x51f910 / 0x51f930 slots 2 / 3, 0x620710 table description.
SR_IMPLEMENT_RECORD(CRefMappingShopGroup, CDBRecStream, "_REFMAPPINGSHOPGROUP", DBTABLE_04, NULL, 0)

// 0x529e20 ctor, 0x51f6a0 / 0x51f6c0 slots 2 / 3, 0x620980 table description.
SR_IMPLEMENT_RECORD(CRefTreatItemOfShop, CDBRecStream, "_REFTREATITEMOFSHOP", DBTABLE_04, NULL, 0)

// 0x529d70 ctor, 0x529dd0 dtor, 0x51f6d0 / 0x51f6f0 slots 2 / 3, 0x620bf0 table description.
SR_IMPLEMENT_RECORD(CRefAccessPermissionOfShop, CDBRecStream, "_REFACCESSPERMISSIONOFSHOP", DBTABLE_04, NULL, 0)

// 0x529ed0 ctor, 0x51f700 / 0x51f720 slots 2 / 3, 0x620e60 table description.
SR_IMPLEMENT_RECORD(CRefRewardPolicyToSellPackageItem, CDBRecStream, "_REFREWARDPOLICYTOSELLPACKAGEITEM", DBTABLE_04, NULL, 0)

// 0x529f80 ctor, 0x51f730 / 0x51f750 slots 2 / 3, 0x6210d0 table description.
SR_IMPLEMENT_RECORD(CRefRewardPolicyToBuyScrapItem, CDBRecStream, "_REFREWARDPOLICYTOBUYSCRAPITEM", DBTABLE_04, NULL, 0)

// 0x52a030 ctor, 0x51f760 / 0x51f780 slots 2 / 3, 0x621340 table description.
SR_IMPLEMENT_RECORD(CRefConditionToSellPackageItem, CDBRecStream, "_REFCONDITIONTOSELLPACKAGEITEM", DBTABLE_04, NULL, 0)

// 0x52a240 ctor, 0x51f790 / 0x51f7b0 slots 2 / 3, 0x6215b0 table description.
SR_IMPLEMENT_RECORD(CRefConditionToBuyScrapItem, CDBRecStream, "_REFCONDITIONTOBUYSCRAPITEM", DBTABLE_04, NULL, 0)

// 0x52a0e0 ctor, 0x51f7c0 / 0x51f7e0 slots 2 / 3, 0x621820 table description.
SR_IMPLEMENT_RECORD(CRefRewardPolicyToSellScrapItem, CDBRecStream, "_REFREWARDPOLICYTOSELLSCRAPITEM", DBTABLE_04, NULL, 0)

// 0x52a190 ctor, 0x51f7f0 / 0x51f810 slots 2 / 3, 0x621a90 table description.
SR_IMPLEMENT_RECORD(CRefConditionToSellScrapItem, CDBRecStream, "_REFCONDITIONTOSELLSCRAPITEM", DBTABLE_04, NULL, 0)

// 0x529cc0 ctor, 0x51f820 / 0x51f840 slots 2 / 3, 0x621d00 table description.
SR_IMPLEMENT_RECORD(CRefPricePolicyOfItem, CDBRecStream, "_REFPRICEPOLICYOFITEM", DBTABLE_04, NULL, 0)

// 0x529c10 ctor, 0x51f880 / 0x51f8a0 slots 2 / 3, 0x621f70 table description.
SR_IMPLEMENT_RECORD(CRefPackageItem, CDBRecStream, "_REFPACKAGEITEM", DBTABLE_04, NULL, 0)

// 0x529b60 ctor, 0x51f8b0 / 0x51f8d0 slots 2 / 3, 0x6221e0 table description.
SR_IMPLEMENT_RECORD(CRefScrapOfPackageItem, CDBRecStream, "_REFSCRAPOFPACKAGEITEM", DBTABLE_04, NULL, 0)

// 0x529ab0 ctor, 0x51f850 / 0x51f870 slots 2 / 3, 0x622450 table description.
SR_IMPLEMENT_RECORD(CRefMappingShopWithTab, CDBRecStream, "_REFMAPPINGSHOPWITHTAB", DBTABLE_04, NULL, 0)

// 0x52a500 ctor, 0x51f940 / 0x51f960 slots 2 / 3, 0x6226c0 table description.
SR_IMPLEMENT_RECORD(CRefShopGoods, CDBRecStream, "_REFSHOPGOODS", DBTABLE_04, NULL, 0)

// 0x52a5b0 ctor, 0x51f970 / 0x51f990 slots 2 / 3, 0x622930 table description.
SR_IMPLEMENT_RECORD(CRefShopTab, CDBRecStream, "_REFSHOPTAB", DBTABLE_04, NULL, 0)

// 0x52a660 ctor, 0x51f9a0 / 0x51f9c0 slots 2 / 3, 0x622ba0 table description.
SR_IMPLEMENT_RECORD(CRefShop, CDBRecStream, "_REFSHOP", DBTABLE_04, NULL, 0)

// 0x52cf10 ctor, 0x5203f0 / 0x520410 slots 2 / 3, 0x622e10 table description.
SR_IMPLEMENT_RECORD(CRefShopItemStockPeriod, CDBRecStream, "_REFSHOPITEMSTOCKPERIOD", DBTABLE_04, NULL, 0)

// ---------------------------------------------------------------------------------------------
// Drop records

// 0x52c6b0 ctor, 0x51f490 / 0x51f4b0 slots 2 / 3, 0x61dc50 table description.
SR_IMPLEMENT_RECORD(CRefMonster_AssignedItemDrop, CDBRecStream, "_REFMONSTER_ASSIGNEDITEMDROP", DBTABLE_04, NULL, 0)

// 0x52c760 ctor, 0x51f4c0 / 0x51f4e0 slots 2 / 3, 0x61dec0 table description.
SR_IMPLEMENT_RECORD(CRefCustomizingReservedItemDropForMonster, CDBRecStream, "_REFCUSTOMIZINGRESERVEDITEMDROPFORMONSTER", DBTABLE_04, NULL, 0)

// 0x52c970 ctor, 0x51fc40 / 0x51fc60 slots 2 / 3, 0x61e130 table description.
SR_IMPLEMENT_RECORD(CRefMonster_AssignedItemRndDrop, CDBRecStream, "_REFMONSTER_ASSIGNEDITEMRNDDROP", DBTABLE_04, NULL, 0)

// 0x52c8c0 ctor, 0x51fc70 / 0x51fc90 slots 2 / 3, 0x61e3a0 table description.
SR_IMPLEMENT_RECORD(CRefDropItemGroup, CDBRecStream, "_REFDROPITEMGROUP", DBTABLE_04, NULL, 0)

// 0x52a710 ctor, 0x51f9d0 / 0x51f9f0 slots 2 / 3, 0x623080 table description.
SR_IMPLEMENT_RECORD(CRefDropGold, CDBRecStream, "_REFDROPGOLD", DBTABLE_04, NULL, 0)

// 0x52a7c0 ctor, 0x51fa00 / 0x51fa20 slots 2 / 3, 0x6232f0 table description.
SR_IMPLEMENT_RECORD(CRefDropOptLevelSel, CDBRecStream, "_REFDROPOPTLVLSEL", DBTABLE_04, NULL, 0)

// 0x52a870 ctor, 0x52a8d0 dtor, 0x51fa30 / 0x51fa50 slots 2 / 3, 0x623560 table description.
SR_IMPLEMENT_RECORD(CRefDropClassSel_Equip, CDBRecStream, "_REFDROPCLASSSEL_EQUIP", DBTABLE_04, NULL, 0)

// 0x52a920 ctor, 0x51fa60 / 0x51fa80 slots 2 / 3, 0x6237d0 table description.
SR_IMPLEMENT_RECORD(CRefDropClassSel_RareEquip, CDBRecStream, "_REFDROPCLASSSEL_RAREEQUIP", DBTABLE_04, NULL, 0)

// 0x52a9d0 ctor, 0x51fa90 / 0x51fab0 slots 2 / 3, 0x623a40 table description.
SR_IMPLEMENT_RECORD(CRefDropClassSel_Recover, CDBRecStream, "_REFDROPCLASSSEL_RECOVER", DBTABLE_04, NULL, 0)

// 0x52aa80 ctor, 0x51fac0 / 0x51fae0 slots 2 / 3, 0x623cb0 table description.
SR_IMPLEMENT_RECORD(CRefDropClassSel_Cure, CDBRecStream, "_REFDROPCLASSSEL_CURE", DBTABLE_04, NULL, 0)

// 0x52ab30 ctor, 0x51faf0 / 0x51fb10 slots 2 / 3, 0x623f20 table description.
SR_IMPLEMENT_RECORD(CRefDropClassSel_Ammo, CDBRecStream, "_REFDROPCLASSSEL_AMMO", DBTABLE_04, NULL, 0)

// 0x52abe0 ctor, 0x51fb20 / - slots 2 / 3, 0x624190 table description.
SR_IMPLEMENT_RECORD(CRefDropClassSel_Scroll, CDBRecStream, "_REFDROPCLASSSEL_SCROLL", DBTABLE_04, NULL, 0)

// 0x52ac90 ctor, 0x51fb50 / 0x51fb70 slots 2 / 3, 0x624400 table description.
SR_IMPLEMENT_RECORD(CRefDropClassSel_Reinforce, CDBRecStream, "_REFDROPCLASSSEL_REINFORCE", DBTABLE_04, NULL, 0)

// 0x52ad40 ctor, 0x51fb80 / 0x51fba0 slots 2 / 3, 0x624670 table description.
SR_IMPLEMENT_RECORD(CRefDropClassSel_AlchemyTablet, CDBRecStream, "_REFDROPCLASSSEL_ALCHEMY_TABLET", DBTABLE_04, NULL, 0)

// 0x52adf0 ctor, 0x51fbb0 / 0x51fbd0 slots 2 / 3, 0x6248e0 table description.
SR_IMPLEMENT_RECORD(CRefDropClassSel_AlchemyMagicStone, CDBRecStream, "_REFDROPCLASSSEL_ALCHEMY_MAGICSTONE", DBTABLE_04, NULL, 0)

// 0x52aea0 ctor, 0x51fbe0 / 0x51fc00 slots 2 / 3, 0x624b50 table description.
SR_IMPLEMENT_RECORD(CRefDropClassSel_AlchemyATTRStone, CDBRecStream, "_REFDROPCLASSSEL_ALCHEMY_ATTRSTONE", DBTABLE_04, NULL, 0)

// 0x52af50 ctor, 0x51fc10 / 0x51fc30 slots 2 / 3, 0x624dc0 table description.
SR_IMPLEMENT_RECORD(CRefDropItemAssign, CDBRecStream, "_REFDROPITEMASSIGN", DBTABLE_04, NULL, 0)

// ---------------------------------------------------------------------------------------------
// Siege fortress records

// 0x52bce0 ctor, 0x52bd40 dtor, 0x5200f0 / 0x520110 slots 2 / 3, 0x628540 table description.
SR_IMPLEMENT_RECORD(CRefSiegeFortress, CDBRecord, "_REFSIEGEFORTRESS", DBTABLE_04, NULL, 0)

// 0x52bdb0 ctor, 0x52be10 dtor, 0x520120 / - slots 2 / 3, 0x6287f0 table description.
SR_IMPLEMENT_RECORD(CRefSiegeFortressGuard, CDBRecord, "_REFSIEGEFORTRESSGUARD", DBTABLE_04, NULL, 0)

RefSiegeFortressGuardData::RefSiegeFortressGuardData()
{
    Service = 0;
    FortressID = 0;
    GuardRefObjID = 0;
}

// 0x52be60 ctor, 0x52bed0 dtor, 0x520150 / 0x520170 slots 2 / 3, 0x628a70 table description.
SR_IMPLEMENT_RECORD(CRefSiegeFortressItemForge, CDBRecord, "_REFSIEGEFORTRESSITEMFORGE", DBTABLE_04, NULL, 0)

RefSiegeFortressItemForgeData::RefSiegeFortressItemForgeData()
{
    Service = 0;
    FortressID = 0;
    RefItemID = 0;
    ReqGold = 0;
    ReqGP = 0;
    ForgeTimeMin = 0;
}

// 0x52bf20 ctor, 0x52bf80 dtor, 0x520180 / 0x5201a0 slots 2 / 3, 0x628cf0 table description.
SR_IMPLEMENT_RECORD(CRefSiegeFortressBattleRank, CDBRecord, "_REFSIEGEFORTRESSBATTLERANK", DBTABLE_04, NULL, 0)

// 0x52bff0 ctor, 0x52c050 dtor, 0x5201b0 / 0x5201d0 slots 2 / 3, 0x628f90 table description.
SR_IMPLEMENT_RECORD(CRefSiegeStructUpgrade, CDBRecord, "_REFSIEGESTRUCTUPGRADE", DBTABLE_04, NULL, 0)

// 0x52c0c0 ctor, 0x52c120 dtor, 0x5201e0 / 0x520200 slots 2 / 3, 0x629220 table description.
SR_IMPLEMENT_RECORD(CRefSiegeLvlSummonMonster, CDBRecord, "_REFSIEGELVLSUMMONMONSTER", DBTABLE_04, NULL, 0)

RefSiegeLvlSummonMonsterData::RefSiegeLvlSummonMonsterData()
{
    Service = 0;
    RefObjID = 0;
    RefOrgObjID = 0;
}

// 0x52c170 ctor, 0x52c1e0 dtor, 0x520210 / 0x520230 slots 2 / 3, 0x6294a0 table description.
SR_IMPLEMENT_RECORD(CRefSiegeFortressRewards, CDBRecord, "_REFSIEGEFORTRESSREWARDS", DBTABLE_04, NULL, 0)

RefSiegeFortressRewardsData::RefSiegeFortressRewardsData()
{
    Service = 0;
    FortressID = 0;
    RewardTypeID = 0;
    RewardValue = 0;
    RewardCount = 0;
}

// 0x52c230 ctor, 0x52c2a0 dtor, 0x520240 / 0x520260 slots 2 / 3, 0x629720 table description.
SR_IMPLEMENT_RECORD(CRefSiegeBlessBuff, CDBRecord, "_REFSIEGEBLESSBUFF", DBTABLE_04, NULL, 0)

RefSiegeBlessBuffData::RefSiegeBlessBuffData()
{
    Service = 0;
    BlessID = 0;
    FortressID = 0;
    RefBlessBuffID = 0;
    NeedGold = 0;
    NeedGP = 0;
}

// 0x52c2f0 ctor, 0x52c360 dtor, 0x520270 / 0x520290 slots 2 / 3, 0x6299a0 table description.
SR_IMPLEMENT_RECORD(CRefSiegeDungeon, CDBRecord, "_REFSIEGEDUNGEON", DBTABLE_04, NULL, 0)

RefSiegeDungeonData::RefSiegeDungeonData()
{
    Service = 0;
    FortressID = 0;
    WorldID = 0;
    MaxCreateCount = 0;
    EntryGold = 0;
    EntryGP = 0;
}

// 0x52c3b0 ctor, 0x52c420 dtor, 0x5202a0 / 0x5202c0 slots 2 / 3, 0x629c20 table description.
SR_IMPLEMENT_RECORD(CRefSiegeQuest, CDBRecord, "_REFSIEGEQUEST", DBTABLE_04, NULL, 0)

// 0x52c490 ctor, 0x52c500 dtor, 0x5202d0 / 0x5202f0 slots 2 / 3, 0x629ef0 table description.
SR_IMPLEMENT_RECORD(CRefSiegeQuestReward, CDBRecord, "_REFSIEGEQUESTREWARD", DBTABLE_04, NULL, 0)

RefSiegeQuestRewardData::RefSiegeQuestRewardData()
{
    Service = 0;
    QuestID = 0;
    RewardType = 0;
    RewardRefID = 0;
    RewardValue = 0;
}

// ---------------------------------------------------------------------------------------------
// Trigger and game world group records

// 0x52dad0 ctor, 0x520720 / 0x520740 slots 2 / 3, 0x62d530 table description.
SR_IMPLEMENT_RECORD(CRefGameWorldBindTriggerCategory, CDBRecStream, "_RefGameWorldBindTriggerCategory", DBTABLE_04, NULL, 0)

// 0x52db80 ctor, 0x52dbe0 dtor, 0x520750 / 0x520770 slots 2 / 3, 0x62d7a0 table description.
SR_IMPLEMENT_RECORD(CRefTriggerCategory, CDBRecStream, "_RefTriggerCategory", DBTABLE_04, NULL, 0)

// 0x52dc40 ctor, 0x520780 / 0x5207a0 slots 2 / 3, 0x62da20 table description.
SR_IMPLEMENT_RECORD(CRefTriggerCategoryBindTrigger, CDBRecStream, "_RefTriggerCategoryBindTrigger", DBTABLE_04, NULL, 0)

// 0x52dcf0 ctor, 0x52dd50 dtor, 0x5207b0 / 0x5207d0 slots 2 / 3, 0x62dc90 table description.
SR_IMPLEMENT_RECORD(CRefTrigger, CDBRecStream, "_RefTrigger", DBTABLE_04, NULL, 0)

// 0x52ddb0 ctor, 0x52de10 dtor, 0x5207e0 / 0x520800 slots 2 / 3, 0x62df20 table description.
SR_IMPLEMENT_RECORD(CRefTriggerVariable, CDBRecStream, "_RefTriggerVariable", DBTABLE_04, NULL, 0)

// 0x52de70 ctor, 0x520810 / 0x520830 slots 2 / 3, 0x62e1a0 table description.
SR_IMPLEMENT_RECORD(CRefTriggerBindEvent, CDBRecStream, "_RefTriggerBindEvent", DBTABLE_04, NULL, 0)

// 0x52df20 ctor, 0x52df80 dtor, 0x520840 / 0x520860 slots 2 / 3, 0x62e410 table description.
SR_IMPLEMENT_RECORD(CRefTriggerBindCondition, CDBRecStream, "_RefTriggerBindCondition", DBTABLE_04, NULL, 0)

// 0x52dfd0 ctor, 0x520870 / 0x520890 slots 2 / 3, 0x62e680 table description.
SR_IMPLEMENT_RECORD(CRefTriggerBindAction, CDBRecStream, "_RefTriggerBindAction", DBTABLE_04, NULL, 0)

// 0x52e080 ctor, 0x52e0e0 dtor, 0x5208a0 / 0x5208c0 slots 2 / 3, 0x62e8f0 table description.
SR_IMPLEMENT_RECORD(CRefTriggerCommon, CDBRecStream, "_RefTriggerCommon", DBTABLE_04, NULL, 0)

// 0x52e140 ctor, 0x5208d0 / 0x5208f0 slots 2 / 3, 0x62eb70 table description.
SR_IMPLEMENT_RECORD(CRefTriggerEvent, CDBRecStream, "_RefTriggerEvent", DBTABLE_04, NULL, 0)

// 0x52e1f0 ctor, 0x52e250 dtor, 0x520900 / 0x520920 slots 2 / 3, 0x62edf0 table description.
SR_IMPLEMENT_RECORD(CRefTriggerCondition, CDBRecStream, "_RefTriggerCondition", DBTABLE_04, NULL, 0)

// 0x52e2b0 ctor, 0x52e310 dtor, 0x520930 / 0x520950 slots 2 / 3, 0x62f070 table description.
SR_IMPLEMENT_RECORD(CRefTriggerAction, CDBRecStream, "_RefTriggerAction", DBTABLE_04, NULL, 0)

// 0x52e370 ctor, 0x52e3d0 dtor, 0x520960 / 0x520980 slots 2 / 3, 0x62f340 table description.
SR_IMPLEMENT_RECORD(CRefTriggerConditionParam, CDBRecStream, "_RefTriggerConditionParam", DBTABLE_04, NULL, 0)

// 0x52e430 ctor, 0x52e490 dtor, 0x520990 / 0x5209b0 slots 2 / 3, 0x62f5d0 table description.
SR_IMPLEMENT_RECORD(CRefTriggerActionParam, CDBRecStream, "_RefTriggerActionParam", DBTABLE_04, NULL, 0)

// 0x52e4f0 ctor, 0x5209c0 / 0x5209e0 slots 2 / 3, 0x62f850 table description.
SR_IMPLEMENT_RECORD(CRefGameWorldBindGameWorldGroup, CDBRecStream, "_RefGameWorldBindGameWorldGroup", DBTABLE_04, NULL, 0)

// 0x52e5a0 ctor, 0x52e600 dtor, 0x5209f0 / 0x520a10 slots 2 / 3, 0x62fad0 table description.
SR_IMPLEMENT_RECORD(CRefGameWorldGroup, CDBRecStream, "_RefGameWorldGroup", DBTABLE_04, NULL, 0)

// 0x52e660 ctor, 0x52e6c0 dtor, 0x520a20 / 0x520a40 slots 2 / 3, 0x62fd60 table description.
SR_IMPLEMENT_RECORD(CRefGameWorldGroup_Config, CDBRecStream, "_RefGameWorldGroup_Config", DBTABLE_04, NULL, 0)
