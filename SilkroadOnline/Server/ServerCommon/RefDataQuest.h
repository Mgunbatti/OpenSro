#pragma once

#include "ServerCommon/ReferenceData.h"

// The AI (hive, tactics, nest), ranking and quest / event reward tables of the reference data
// (CRefInstanceGenerator in ServerCommon\RefInstanceGenerator.cpp of the original). The builders
// are the entries of their slots in the table of CReferenceData::BuildFromTable.

// ---------------------------------------------------------------------------------------------
// Ranking rows (only kept while the ranking packets are built)

// TAB_REFRANKING_*ACTIVITY row (44 bytes, RecCtor 0x4dd750).
struct RefRankingActivity
{
    RefRankingActivity()
        : Rank(0), b20(0), dw24(0), b28(0)
    {
    }

    BYTE             Rank;                       // +0   key of the map
    std::string      CharName;                   // +4
    BYTE             b20;                        // +20
    DWORD            dw24;                       // +24
    BYTE             b28;                        // +28
};

C_ASSERT(sizeof(RefRankingActivity) == 0x2C);

// TAB_REFRANKING_*CONTRIBUTION row (40 bytes, RecCtor 0x4dd6e0).
struct RefRankingContribution
{
    RefRankingContribution()
        : Rank(0), b20(0), dw24(0)
    {
    }

    BYTE             Rank;                       // +0   key of the map
    std::string      CharName;                   // +4
    BYTE             b20;                        // +20
    DWORD            dw24;                       // +24
};

C_ASSERT(sizeof(RefRankingContribution) == 0x28);

typedef std::map<DWORD, RefRankingActivity*> RefRankingActivityMap;
typedef std::map<DWORD, RefRankingContribution*> RefRankingContributionMap;

// ---------------------------------------------------------------------------------------------
// Row readers: the key of the row, 0 for a row that is dropped.

int RefRow_Read(CRefRow& row, RefHiveRow& r);                    // RefHive::Read 0x4965f0
// RefTactics::Read (0x496740): binds the _RefAISkill rows of the tactics by their skill; an AI
// skill of an unknown skill asserts, is logged and drops the row.
int RefRow_Read(CRefRow& row, RefTacticsRow& r);
int RefRow_Read(CRefRow& row, RefNestRow& r);                    // RefNest::Read 0x496e60
int RefRow_Read(CRefRow& row, RefRankingActivity& r);            // 0x4977b0: the rank
int RefRow_Read(CRefRow& row, RefRankingContribution& r);        // 0x497860: the rank
// RefQuestRewardItems::Read (0x4996f0): 0 out of service, for a quest without a _RefQuestReward
// row or for an unknown rent item; an unknown item leaves ItemObjID 0.
int RefRow_Read(CRefRow& row, RefQuestRewardItemsRow& r);
// RefEventReward::Read (0x499990): 0 out of service or for an ID below 10000.
int RefRow_Read(CRefRow& row, RefEventRewardRow& r);
// RefEventRewardItems::Read (0x499bc0): 0 out of service, for an unknown event reward, item or
// rent item.
int RefRow_Read(CRefRow& row, RefEventRewardItemsRow& r);

// ---------------------------------------------------------------------------------------------
// Lookups (NULL when there is no such row)

// CRefInstanceGenerator::FindQuestReward (0x49e340).
RefQuestReward* RefData_FindQuestReward(CReferenceData* pData, DWORD dwQuestID);
// CRefInstanceGenerator::FindEventReward (0x49e150).
RefEventRewardRow* RefData_FindEventReward(CReferenceData* pData, DWORD dwID);

// ---------------------------------------------------------------------------------------------
// Builders (the PFN_REF_BUILD entries of their slots)

// Slot 0x38 (RefData_BuildRefHive 0x4cf4f0): the hives by HiveID.
int RefData_BuildHive(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);
// Slot 0x3A (RefData_BuildRefTactics 0x4cf480): the tactics by TacticsID.
int RefData_BuildTactics(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);
// Slot 0x3B (CRefInstanceGenerator::LoadNest 0x4a9e80): the nests by NestID, each appended to
// the nests of its hive.
int RefData_LoadNest(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);
// Slots 0x40 / 0x42 / 0x44 (CRefInstanceGenerator::LoadRankingActivity 0x4a9f80) and 0x41 /
// 0x43 / 0x45 (LoadRankingContribution 0x4aa010): the rows of the trader, robber and hunter
// tables are written into the ranking packet of their type (CReferenceData+0x990).
int RefData_LoadRankingActivity(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);
int RefData_LoadRankingContribution(CReferenceData* pData, RefTableEntry* pTable,
                                    unsigned int nSlot);
// CRefInstanceGenerator::BuildRankingActivityTable (0x4a9240) / BuildRankingContributionTable
// (0x4a9550): the packet of a type (1..3) is rewritten: the row count (BYTE), then per rank the
// rank, the name (WORD length + characters) and the other columns. A packet that overflows
// asserts and throws.
int RefData_BuildRankingActivityTable(RefRankingPackets& packets, BYTE byType,
                                      const RefRankingActivityMap& mapRow);
int RefData_BuildRankingContributionTable(RefRankingPackets& packets, BYTE byType,
                                          const RefRankingContributionMap& mapRow);
// Slot 0x65 (RefData_BuildRefQuestRewardItems 0x4d07f0): quest ID -> group BYTE -> the items.
int RefData_BuildQuestRewardItems(CReferenceData* pData, RefTableEntry* pTable,
                                  unsigned int nSlot);
// Slot 0x66 (RefData_BuildRefEventReward 0x4d0860): the event rewards by ID.
int RefData_BuildEventReward(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);
// Slot 0x67 (RefData_BuildRefEventRewardItems 0x4d08d0): event reward ID -> item -> row.
int RefData_BuildEventRewardItems(CReferenceData* pData, RefTableEntry* pTable,
                                  unsigned int nSlot);

// CRefInstanceGenerator::BuildEventItemList (0x4ab0a0, part of PostProcessObjCommon): the event
// equipment of the objects by item class band, country 0 into +0x9CC, country 2 into +0x9D8 /
// +0x9E4 / +0x9F0 by the item class within its band.
void RefData_BuildEventItemList(CReferenceData* pData);
