#pragma once

#include "ServerCommon/ReferenceData.h"

// Regions, teleports and the tables around them (ServerCommon\ReferenceData.cpp and
// RefInstanceGenerator.cpp of the original): the builders of slots 2 (_RefRegionBindAssocServer),
// 3 (_RefRegion), 0x12 (_RefTeleport), 0x13 (_RefTeleLink) and 0x5F (_RefAlchemyMerit), their
// row readers and the checks and indexes the builders run after their table.

// ---------------------------------------------------------------------------------------------
// Table builders (entries of the slot switch of CReferenceData::BuildFromTable)

// Slot 2: RefData_BuildRefRegionBindAssocServer (0x4cfa20, CRefInstanceGenerator::LoadStringList
// 0x4a54a0 per row), then RefData_CheckRegionAssocServerSetting (0x4e6c20).
int RefData_BuildRegionBindAssocServer(CReferenceData* pData, RefTableEntry* pTable,
                                       unsigned int nSlot);
// Slot 3: RefData_BuildRefRegion (0x4cef80, RefData_AddRowRefRegion 0x4d20e0), then
// CRefInstanceGenerator::LinkCharGenRegions (0x4a9930).
int RefData_BuildRegion(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);
// Slot 0x12: RefData_BuildRefTeleport (0x4cf1f0, RefData_AddRowRefTeleport 0x4d22a0), then
// CRefInstanceGenerator::IndexTeleportByCodeName (0x4ada10).
int RefData_BuildTeleport(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);
// Slot 0x13: RefData_BuildRefTeleLink (0x4cf260, CRefInstanceGenerator::LoadTeleLink 0x4a41f0).
int RefData_BuildTeleLink(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);
// Slot 0x5F: the "_RefAlchemyMerit Empty" warning of the switch, then
// CRefInstanceGenerator::LoadGameWorldPositions (0x4adb60). Always 1.
int RefData_BuildAlchemyMerit(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);

// ---------------------------------------------------------------------------------------------
// Steps of the builders

// CRefInstanceGenerator::FindRegionIdByName (0x4a1c80): AssocServer of the first
// _RefRegionBindAssocServer row whose AreaName equals strName (CompareStringA, user locale, case
// ignored); 0 when there is none.
int RefData_FindAssocServerByAreaName(CReferenceData* pData, const std::string& strName);

// RefData_CheckRegionAssocServerSetting (0x4e6c20): every fortress area must have the
// AssocServer of its region (FORT_JA/BJ_AREA - CHINA, FORT_DW/HM_AREA - West_China, FORT_HT_AREA
// - Oasis_Kingdom, FORT_CT_AREA - Eu, FORT_ER_AREA - Am, FORT_SK_AREA - Ca); another FORT* area
// is unknown. A mismatch or an unknown area shows a message box and returns 0.
int RefData_CheckRegionAssocServerSetting(CReferenceData* pData);

// CRefInstanceGenerator::LinkCharGenRegions (0x4a9930): reads the ten LinkedRegion columns of
// every region from its _RefRegion row (RefData_ReadRegionIdArray10 0x48ed10). 0 when a region
// has no row.
int RefData_LinkRegions(CReferenceData* pData);

// CRefInstanceGenerator::IndexTeleportByCodeName (0x4ada10).
void RefData_IndexTeleportByCodeName(CReferenceData* pData);

// ---------------------------------------------------------------------------------------------
// Row readers

// RefData_ReadRegionLinkRow (0x48eb80): the region ID, 0 for a region without AssocServer. The
// ID is also stored in the record (+0x30) for RefData_LinkRegions.
int RefRow_Read(CRefRow& row, RefRegionRow& r);
// RefTeleport::Read (0x493870): the ID, 0 out of service. Registers the teleport with its
// associated object.
int RefRow_Read(CRefRow& row, RefTeleportRow& r);
// RefTeleLink::Read (0x4930f0): OwnerTeleport, 0 out of service.
int RefRow_Read(CRefRow& row, RefTeleLink& r);
// RefData_ReadPosRecord (0x498a70), after the Service column.
void RefRow_Read(CRefRow& row, RefGameWorldPositionRow& r);
// RefData_ReadRegionIdArray10 (0x48ed10): the ten linked regions; 0 for a region without ID.
int RefRow_ReadLinkedRegions(CRefRow& row, RefRegionRow& r);

// _RefEventZone row (224 bytes, RefTeleportLinkedEvent::Read 0x4932e0; the loader is
// RefData_AddRowRefEventZone 0x4d2380).
struct RefTeleportLinkedEvent
{
    DWORD               ID;                      // +0
    std::string         CodeName128;             // +4   lower-cased
    std::string         str20;                   // +20  "TELEPORT": Param[0] names a teleport
    DWORD               adwParam[5];             // +3C  [4]: ID of that teleport
    std::string         astrParam[5];            // +50
    DWORD               dwDC;                    // +DC  0
};

C_ASSERT(FIELD_OFFSET(RefTeleportLinkedEvent, adwParam) == 0x3C);
C_ASSERT(sizeof(RefTeleportLinkedEvent) == 0xE0);

// RefTeleportLinkedEvent::Read (0x4932e0): the ID, 0 out of service or for a code name of 256
// characters or more.
int RefRow_Read(CRefRow& row, RefTeleportLinkedEvent& r);
