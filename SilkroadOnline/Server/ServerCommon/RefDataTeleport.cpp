#include "ServerCommon/RefDataTeleport.h"

#include <stdio.h>
#include <string.h>
#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "BSLib/Util/StringCheck.h"
#include "ServerFramework/ServerDataExtern.h"

static const char s_szReferenceDataFile[] =
    "D:\\WORK2005\\Source\\SilkroadOnline\\Server\\ServerCommon\\ReferenceData.cpp";
static const char s_szRefInstanceGeneratorFile[] =
    "D:\\WORK2005\\Source\\SilkroadOnline\\Server\\ServerCommon\\RefInstanceGenerator.cpp";

// ---------------------------------------------------------------------------------------------
// Helpers

// The value of a key, NULL when the key is not there.
template <class TMap, class TKey>
static typename TMap::mapped_type RefTeleport_FindValue(TMap& map, const TKey& key)
{
    typename TMap::iterator it = map.find(key);
    if (it == map.end())
        return NULL;
    return it->second;
}

// The members of the object row (RefObjCommon, not ported here) that a teleport uses: the
// teleport of the object (+0x190) and its set of teleport flags (+0x90: 7 resurrection point,
// 9 resurrection target, 8 pets allowed).
static RefTeleportRow*& RefObj_Teleport(RefObjCommonRow* pRefObj)
{
    return *reinterpret_cast<RefTeleportRow**>(reinterpret_cast<BYTE*>(pRefObj) + 0x190);
}

static std::set<BYTE>& RefObj_TeleportFlags(RefObjCommonRow* pRefObj)
{
    return *reinterpret_cast<std::set<BYTE>*>(reinterpret_cast<BYTE*>(pRefObj) + 0x90);
}

typedef int (*PFN_REF_TELEPORT_LOAD)(CReferenceData* pData, CDBRecord* pRecord);

// The row loop of the RefData_BuildRef<Table> builders: 0 for an empty table, a row the loader
// fails asserts and stops the loop (0).
static int RefTeleport_BuildRows(CReferenceData* pData, RefTableEntry* pTable,
                                 PFN_REF_TELEPORT_LOAD pfnLoad)
{
    if (pTable->m_listRows.size() == 0)
        return 0;
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        if (!pfnLoad(pData, *it))
        {
            BSAssertFail();
            return 0;
        }
    }
    return 1;
}

// RefData_AddRow<Table>: a row the reader rejects is freed; a key that is already in the map
// frees the row and fails the build.
template <class T, class TMap>
static int RefTeleport_AddRowUnique(TMap& map, CDBRecord* pRecord)
{
    CRefRow row(static_cast<CDBRecStream*>(pRecord));
    T* pRow = new T;
    DWORD dwKey = (DWORD)RefRow_Read(row, *pRow);
    if (!dwKey)
    {
        delete pRow;
        return 1;
    }
    if (RefTeleport_FindValue(map, dwKey))
    {
        delete pRow;
        BSAssertFail();
        return 0;
    }
    map.insert(typename TMap::value_type(dwKey, pRow));
    return 1;
}

// ---------------------------------------------------------------------------------------------
// _RefRegionBindAssocServer (slot 2)

// CRefInstanceGenerator::LoadStringList (0x4a54a0): a copy of every row with an AssocServer.
static int RefData_LoadRegionBindAssocServer(CReferenceData* pData, CDBRecord* pRecord)
{
    RefRegionBindAssocServerData* pRow = new RefRegionBindAssocServerData;
    *pRow = *static_cast<CRefRegionBindAssocServer*>(pRecord);
    if (pRow->AssocServer)
        pData->m_vecRegionBindAssocServer.push_back(pRow);
    else
        delete pRow;
    return 1;
}

int RefData_BuildRegionBindAssocServer(CReferenceData* pData, RefTableEntry* pTable,
                                       unsigned int)
{
    int nResult = RefTeleport_BuildRows(pData, pTable, RefData_LoadRegionBindAssocServer);
    if (nResult != 1)
        return nResult;
    return RefData_CheckRegionAssocServerSetting(pData);
}

int RefData_FindAssocServerByAreaName(CReferenceData* pData, const std::string& strName)
{
    CReferenceData::RefRegionBindAssocServerVector& vec = pData->m_vecRegionBindAssocServer;
    for (CReferenceData::RefRegionBindAssocServerVector::iterator it = vec.begin();
         it != vec.end(); ++it)
    {
        const char* pszEntry = (*it)->AreaName.c_str();
        const char* pszName = strName.c_str();
        if (CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE | SORT_STRINGSORT, pszEntry,
                           lstrlenA(pszEntry), pszName, lstrlenA(pszName)) == CSTR_EQUAL)
        {
            return (*it)->AssocServer;
        }
    }
    return 0;
}

// The fortress areas and the region each one belongs to.
static const char* const s_apszFortressRegion[][2] =
{
    { "FORT_JA_AREA", "CHINA" },
    { "FORT_BJ_AREA", "CHINA" },
    { "FORT_DW_AREA", "West_China" },
    { "FORT_HM_AREA", "West_China" },
    { "FORT_HT_AREA", "Oasis_Kingdom" },
    { "FORT_CT_AREA", "Eu" },
    { "FORT_ER_AREA", "Am" },
    { "FORT_SK_AREA", "Ca" },
};

int RefData_CheckRegionAssocServerSetting(CReferenceData* pData)
{
    // AreaName -> AssocServer, the first row of a name wins.
    std::map<std::string, int> mapAssoc;
    CReferenceData::RefRegionBindAssocServerVector& vec = pData->m_vecRegionBindAssocServer;
    for (CReferenceData::RefRegionBindAssocServerVector::iterator it = vec.begin();
         it != vec.end(); ++it)
    {
        if (*it)
            mapAssoc.insert(std::make_pair((*it)->AreaName, (*it)->AssocServer));
    }

    // A fortress area is only checked (and then left out of the scan below) when its region is
    // there as well.
    for (size_t i = 0; i < sizeof(s_apszFortressRegion) / sizeof(s_apszFortressRegion[0]); ++i)
    {
        std::string strFortress(s_apszFortressRegion[i][0]);
        std::string strRegion(s_apszFortressRegion[i][1]);
        if (mapAssoc.find(strFortress) == mapAssoc.end() ||
            mapAssoc.find(strRegion) == mapAssoc.end())
        {
            continue;
        }
        if (mapAssoc[strFortress] != mapAssoc[strRegion])
        {
            char szBuffer[0xA00];
            sprintf_s(szBuffer, sizeof(szBuffer),
                      "!!!! WARNING !!!!\n"
                      "Please double check the region setting in table "
                      "[_RefRegionBindAssocServer]\n"
                      "The un-matched values between '%s' (%d) and '%s' (%d) makes server "
                      "unstable\n"
                      "Make sure these two 'AssocServer' values are equal",
                      s_apszFortressRegion[i][0], mapAssoc[strFortress],
                      s_apszFortressRegion[i][1], mapAssoc[strRegion]);
            BSMessageBoxPrintf(szBuffer);
            return 0;
        }
        mapAssoc.erase(mapAssoc.find(strFortress));
    }

    // BUG(SM-052): a fortress area whose region row is missing is reported as unknown.
    for (std::map<std::string, int>::iterator it = mapAssoc.begin(); it != mapAssoc.end(); ++it)
    {
        std::string strName(it->first);
        if (strName.size() != 0 && strncmp(strName.c_str(), "FORT", 4) == 0)
        {
            BSMessageBoxPrintf("There's no hardcoded entry for Sirge Fortress!! Please update "
                               "source code for entry!!\nor ask your development company for "
                               "update!!");
            return 0;
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------
// _RefRegion (slot 3)

RefRegionRow::RefRegionRow()
    : wRegionID(0), IsBattleField(0), pClimate(NULL), MaxCapacity(0), AssocObjID(0),
      AssocServer(0)
{
    for (int i = 0; i < 10; ++i)
        apLinkedRegion[i] = NULL;
}

int RefRow_Read(CRefRow& row, RefRegionRow& r)
{
    BYTE bX, bZ;
    DWORD dwClimate;
    row >> r.wRegionID >> bX >> bZ >> r.ContinentName >> r.AreaName >> r.IsBattleField >>
        dwClimate >> r.MaxCapacity >> r.AssocObjID >> r.AssocServer >> r.AssocFile256;
    CReferenceData* pData = CReferenceData::GetInstance();
    r.AssocServer = RefData_FindAssocServerByAreaName(pData, r.ContinentName);
    if (!r.AssocServer)
        return 0;
    if (dwClimate)
        r.pClimate = RefTeleport_FindValue(CReferenceData::GetInstance()->m_mapClimate, dwClimate);
    else
        r.pClimate = NULL;
    static_cast<CRefRegion*>(row.Stream())->m_adw30[0] = r.wRegionID;
    return r.wRegionID;
}

int RefRow_ReadLinkedRegions(CRefRow& row, RefRegionRow& r)
{
    if (!r.wRegionID)
        return 0;
    for (int i = 0; i < 10; ++i)
    {
        DWORD dwID;
        row >> dwID;
        // An unknown region goes to a log call that does nothing; the link is NULL.
        r.apLinkedRegion[i] = dwID ? CReferenceData::GetInstance()->FindRegion(dwID) : NULL;
    }
    return 1;
}

static int RefData_AddRowRegion(CReferenceData* pData, CDBRecord* pRecord)
{
    return RefTeleport_AddRowUnique<RefRegionRow>(pData->m_mapRegion, pRecord);
}

// CRefInstanceGenerator::FindRowInTable (0x4a9830) for slot 3: the _RefRegion record whose
// region ID (+0x30) is wID.
static CDBRecord* RefData_FindRegionRecord(CReferenceData* pData, WORD wID)
{
    RefTableEntry* pTable = pData->m_pTables->at(3);
    if (!pTable)
        BSAssertFail();
    for (CDBRecordList::iterator it = pTable->m_listRows.begin();
         it != pTable->m_listRows.end(); ++it)
    {
        if (static_cast<CRefRegion*>(*it)->m_adw30[0] == wID)
            return *it;
    }
    return NULL;
}

int RefData_LinkRegions(CReferenceData* pData)
{
    for (CReferenceData::RefRegionMap::iterator it = pData->m_mapRegion.begin();
         it != pData->m_mapRegion.end(); ++it)
    {
        RefRegionRow* pRegion = it->second;
        if (!pRegion->wRegionID)
            BSAssertFail();
        CDBRecord* pRecord = RefData_FindRegionRecord(pData, pRegion->wRegionID);
        if (!pRecord)
        {
            BSAssertFail();
            return 0;
        }
        CRefRow row(static_cast<CDBRecStream*>(pRecord));
        if (!RefRow_ReadLinkedRegions(row, *pRegion))
            return 0;
    }
    return 1;
}

int RefData_BuildRegion(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    if (RefTeleport_BuildRows(pData, pTable, RefData_AddRowRegion) != 1)
        return 0;
    return RefData_LinkRegions(pData);
}

// ---------------------------------------------------------------------------------------------
// _RefTeleport (slot 0x12)

RefTeleportRow::RefTeleportRow()
    : ID(0), pGenRegion(NULL), w24(0), GenPos_X(0), GenPos_Y(0), GenPos_Z(0), GenWorldID(0),
      pAssocRefObj(NULL), CanBringCOS(0), GenAreaRadius(0)
{
}

RefTeleportRow::~RefTeleportRow()
{
    for (size_t i = 0; i < vecLink.size(); ++i)
        delete vecLink[i];
}

int RefRow_Read(CRefRow& row, RefTeleportRow& r)
{
    r.pAssocRefObj = NULL;
    if (!row.Service())
        return 0;
    DWORD dwAssocRefObjID;
    row.Id(r.ID) >> r.CodeName128;
    row.Skip() >> dwAssocRefObjID;
    row.Skip();
    if (dwAssocRefObjID)
    {
        RefObjCommonRow* pRefObj =
            CReferenceData::GetInstance()->FindRefObj((int)dwAssocRefObjID);
        r.pAssocRefObj = pRefObj;
        if (pRefObj)
        {
            if (RefObj_Teleport(pRefObj))
                BSAssertFail();
            RefObj_Teleport(pRefObj) = &r;
            RefObj_TeleportFlags(pRefObj).clear();
        }
        else
        {
            LogPrintf(0x2000000, "can't find associated ref obj with this teleport[tel id: %s]",
                      r.CodeName128.c_str());
        }
    }
    WORD wGenRegionID;
    row >> wGenRegionID;
    r.pGenRegion = CReferenceData::GetInstance()->FindRegion(wGenRegionID);
    r.w24 = 0;
    BYTE bCanBeResurrectPos, bCanGotoResurrectPos;
    row >> r.GenPos_X >> r.GenPos_Y >> r.GenPos_Z >> r.GenWorldID >> bCanBeResurrectPos >>
        bCanGotoResurrectPos;
    if (r.pAssocRefObj)
    {
        if (bCanBeResurrectPos)
            RefObj_TeleportFlags(r.pAssocRefObj).insert(7);
        if (bCanGotoResurrectPos)
            RefObj_TeleportFlags(r.pAssocRefObj).insert(9);
    }
    row >> r.GenAreaRadius >> r.CanBringCOS;
    row.Skip();
    return (int)r.ID;
}

static int RefData_AddRowTeleport(CReferenceData* pData, CDBRecord* pRecord)
{
    return RefTeleport_AddRowUnique<RefTeleportRow>(pData->m_mapTeleport, pRecord);
}

void RefData_IndexTeleportByCodeName(CReferenceData* pData)
{
    CReferenceData::RefTeleportByCodeNameMap& mapByCodeName = pData->m_mapTeleportByCodeName;
    for (CReferenceData::RefTeleportMap::iterator it = pData->m_mapTeleport.begin();
         it != pData->m_mapTeleport.end(); ++it)
    {
        RefTeleportRow* pRefTeleport = it->second;
        if (mapByCodeName.find(pRefTeleport->CodeName128) != mapByCodeName.end() &&
            !BSAssertDialog(2526, s_szRefInstanceGeneratorFile,
                            "m_RefTeleportByCodeName.end() == "
                            "m_RefTeleportByCodeName.find(pRefTeleport->m_strCodeName)"))
        {
            DebugBreak();
        }
        mapByCodeName.insert(std::make_pair(pRefTeleport->CodeName128, pRefTeleport));
    }
}

int RefData_BuildTeleport(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    if (RefTeleport_BuildRows(pData, pTable, RefData_AddRowTeleport) != 1)
        return 0;
    RefData_IndexTeleportByCodeName(pData);
    return 1;
}

// ---------------------------------------------------------------------------------------------
// _RefTeleLink (slot 0x13)

RefTeleLink::RefTeleLink()
    : pTargetTeleport(NULL), Fee(0), RestrictBindMethod(0), RunTimeTeleportMethod(0),
      CheckResult(0)
{
    vecRestrict.reserve(5);
}

int RefRow_Read(CRefRow& row, RefTeleLink& r)
{
    DWORD dwService, dwOwnerTeleport, dwTargetTeleport;
    row >> dwService;
    if (!dwService)
        return 0;
    row >> dwOwnerTeleport >> dwTargetTeleport;
    if (!dwOwnerTeleport || !dwTargetTeleport)
        BSAssertFail();
    r.pTargetTeleport =
        RefTeleport_FindValue(CReferenceData::GetInstance()->m_mapTeleport, dwTargetTeleport);
    if (!r.pTargetTeleport)
        BSAssertFail();
    row >> r.Fee >> r.RestrictBindMethod >> r.RunTimeTeleportMethod >> r.CheckResult;
    for (int i = 0; i < 5; ++i)
    {
        RefTeleLinkRestrict restrict;
        row >> restrict.Restrict >> restrict.Data1 >> restrict.Data2;
        if (restrict.Restrict)
            r.vecRestrict.push_back(restrict);
    }
    return (int)dwOwnerTeleport;
}

// RefTeleport::AddLink (0x493c00): a teleport that allows pets marks its object with flag 8.
static void RefTeleport_AddLink(RefTeleportRow& teleport, RefTeleLink* pLink)
{
    teleport.vecLink.push_back(pLink);
    if (teleport.pAssocRefObj && teleport.CanBringCOS == 1)
        RefObj_TeleportFlags(teleport.pAssocRefObj).insert(8);
}

static int RefData_LoadTeleLink(CReferenceData* pData, CDBRecord* pRecord)
{
    CRefRow row(static_cast<CDBRecStream*>(pRecord));
    RefTeleLink* pLink = new RefTeleLink;
    DWORD dwOwnerTeleport = (DWORD)RefRow_Read(row, *pLink);
    // BUG(SM-051): a link out of service is not freed.
    if (dwOwnerTeleport)
    {
        RefTeleportRow* pTeleport = RefTeleport_FindValue(pData->m_mapTeleport, dwOwnerTeleport);
        // BUG(SM-050): an unknown owner teleport only asserts; the link is then added to NULL.
        if (!pTeleport)
            BSAssertFail();
        RefTeleport_AddLink(*pTeleport, pLink);
    }
    return 1;
}

// The switch also constructs and destroys an unused RefTeleLink around the build.
int RefData_BuildTeleLink(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    return RefTeleport_BuildRows(pData, pTable, RefData_LoadTeleLink);
}

// ---------------------------------------------------------------------------------------------
// _RefAlchemyMerit (slot 0x5F)

void RefRow_Read(CRefRow& row, RefGameWorldPositionRow& r)
{
    row >> r.bIndex >> r.str04 >> r.bKey;
    for (int i = 0; i < 4; ++i)
        row >> r.af20[i];
    for (int i = 0; i < 3; ++i)
        row >> r.an30[i] >> r.astr48[i];
}

// CRefInstanceGenerator::LoadGameWorldPositions (0x4adb60): seven maps, the rows in service by
// their index and key. An index of 7 or more throws std::out_of_range.
static void RefData_LoadGameWorldPositions(CReferenceData* pData, RefTableEntry* pTable)
{
    CReferenceData::RefGameWorldPositionVector& vec = pData->m_vecGameWorldPosition;
    vec.clear();
    vec.resize(7);
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        CRefRow row(static_cast<CDBRecStream*>(*it));
        DWORD dwService;
        row >> dwService;
        if (!dwService)
            continue;
        RefGameWorldPositionRow* pRow = new RefGameWorldPositionRow;
        RefRow_Read(row, *pRow);
        // BUG(SM-007): a row whose index and key are already there is not inserted and leaks.
        vec.at(pRow->bIndex).insert(std::make_pair(pRow->bKey, pRow));
    }
}

int RefData_BuildAlchemyMerit(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    if (pTable->m_listRows.size() == 0)
        BSMessageBoxPrintf("!!warning!! _RefAlchemyMerit Empty!! 'EquipReslove' will be fail!!");
    RefData_LoadGameWorldPositions(pData, pTable);
    return 1;
}

// ---------------------------------------------------------------------------------------------
// _RefEventZone rows

int RefRow_Read(CRefRow& row, RefTeleportLinkedEvent& r)
{
    DWORD dwService;
    row >> dwService;
    if (!dwService)
        return 0;
    row >> r.ID;
    if (!r.ID && !BSAssertDialog(2129, s_szReferenceDataFile, "dwID != 0"))
        DebugBreak();
    row >> r.CodeName128 >> r.str20;
    char szCodeName[256];
    if (FAILED(StringCchCopyA(szCodeName, sizeof(szCodeName), r.CodeName128.c_str())))
    {
        BSAssertFail();
        return 0;
    }
    String_ToLowerMbcs(szCodeName, &r.CodeName128);
    for (int i = 0; i < 5; ++i)
        row >> r.adwParam[i];
    for (int i = 0; i < 5; ++i)
        row >> r.astrParam[i];
    if (_stricmp(r.str20.c_str(), "TELEPORT") == 0)
    {
        std::string strTeleport(r.astrParam[0]);
        if (_stricmp(strTeleport.c_str(), "xxx") == 0)
            BSAssertFail();
        CReferenceData::RefTeleportMap& mapTeleport = CReferenceData::GetInstance()->m_mapTeleport;
        for (CReferenceData::RefTeleportMap::iterator it = mapTeleport.begin();
             it != mapTeleport.end(); ++it)
        {
            if (_stricmp(it->second->CodeName128.c_str(), strTeleport.c_str()) == 0)
            {
                if (r.adwParam[4])
                    BSAssertFail();
                r.adwParam[4] = it->first;
                break;
            }
        }
    }
    r.dwDC = 0;
    return (int)r.ID;
}
