#include "ServerCommon/ReferenceData.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "BSLib/BSDebug.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerCommon/RefDataMisc.h"
#include "ServerCommon/RefDataObj.h"
#include "ServerCommon/RefDataQuest.h"
#include "ServerCommon/RefDataTeleport.h"
#include "ServerFramework/ServerDataExtern.h"

CReferenceData* g_pReferenceData = NULL;

// The static object of the original (0x89b020).
static CReferenceData s_ReferenceData;

// ---------------------------------------------------------------------------------------------
// Row reader

// RefDataRow_ReadString (0x4a1e30): WORD length, WORD field size (header included), the
// characters. The read position always moves by the field size. A length of 0 leaves the
// string as it was.
CRefRow& CRefRow::operator>>(std::string& value)
{
    int nSize = (int)m_pRow->m_nWritePos;
    int nPos = (int)m_pRow->m_nReadPos;
    // BUG(SM-006): an overrun asserts and rethrows without an exception (terminate).
    if (nPos + 2 > nSize)
    {
        BSAssertFail();
        throw;
    }
    WORD wLength = *(const WORD*)(m_pRow->m_pBuffer + nPos);
    m_pRow->m_nReadPos = nPos + 2;
    if (nPos + 4 > nSize)
    {
        BSAssertFail();
        throw;
    }
    WORD wFieldSize = *(const WORD*)(m_pRow->m_pBuffer + nPos + 2);
    m_pRow->m_nReadPos = nPos + 4;
    if (wLength)
    {
        if (wLength + nPos + 4 > nSize)
            BSAssertFail();
        value.resize(wLength);
        if (wLength + (int)m_pRow->m_nReadPos > (int)m_pRow->m_nWritePos)
        {
            BSAssertFail();
            throw;
        }
        memcpy(&value[0], m_pRow->m_pBuffer + m_pRow->m_nReadPos, wLength);
        m_pRow->m_nReadPos += wLength;
    }
    m_pRow->m_nReadPos += wFieldSize - wLength - 4;
    ++m_pRow->m_nFieldCount;
    return *this;
}

CRefRow& CRefRow::Skip()
{
    CDBTableInfo* pInfo = m_pRow->GetTableInfo();
    for (DBFieldGroupMap::iterator itGroup = pInfo->m_mapGroups.begin();
         itGroup != pInfo->m_mapGroups.end(); ++itGroup)
    {
        DBColumnVector& vecColumns = itGroup->second.vecColumns;
        for (size_t i = 0; i < vecColumns.size(); ++i)
        {
            if (vecColumns.at(i).nOrdinal == (int)m_pRow->m_nFieldCount + 1)
            {
                m_pRow->m_nReadPos += vecColumns.at(i).nSize;
                ++m_pRow->m_nFieldCount;
                return *this;
            }
        }
    }
    ++m_pRow->m_nFieldCount;
    return *this;
}

// ---------------------------------------------------------------------------------------------
// Row readers

int RefRow_Read(CRefRow& row, RefUIString& r)
{
    row.Id(r.ID) >> r.GroupCodeName128 >> r.ValueCodeName128 >> r.Value >> r.Type;
    return r.ID;
}

int RefRow_Read(CRefRow& row, RefTriggerCategory& r)
{
    if (!row.Service())
        return 0;
    row.Id(r.ID) >> r.CodeName128 >> r.ObjName128 >> r.IndexNumber;
    return r.ID;
}

int RefRow_Read(CRefRow& row, RefTrigger& r)
{
    if (!row.Service())
        return 0;
    row.Id(r.ID) >> r.CodeName128 >> r.IsActive >> r.IsRepeat >> r.Comment512 >> r.IndexNumber;
    return r.ID;
}

int RefRow_Read(CRefRow& row, RefTriggerBind& r)
{
    if (!row.Service())
        return 0;
    row.Id(r.ID) >> r.ID1 >> r.ID2;
    return r.ID;
}

int RefRow_Read(CRefRow& row, RefTriggerCommon& r)
{
    if (!row.Service())
        return 0;
    row.Id(r.ID) >> r.CodeName128 >> r.ObjName128 >> r.TID1 >> r.TID2 >> r.TID3 >> r.TID4;
    return r.ID;
}

int RefRow_Read(CRefRow& row, RefTriggerEvent& r)
{
    if (!row.Service())
        return 0;
    row.Id(r.ID) >> r.Value;
    return r.ID;
}

int RefRow_Read(CRefRow& row, RefTriggerCondition& r)
{
    if (!row.Service())
        return 0;
    row.Id(r.ID) >> r.RefTriggerCommonID >> r.OnTrue >> r.OnFalse >> r.Sequence >>
        r.ParamGroupCodeName128;
    return r.ID;
}

int RefRow_Read(CRefRow& row, RefTriggerAction& r)
{
    if (!row.Service())
        return 0;
    row.Id(r.ID) >> r.RefTriggerCommonID >> r.Delay >> r.ParamGroupCodeName128;
    return r.ID;
}

int RefRow_Read(CRefRow& row, RefTriggerVariable& r)
{
    if (!row.Service())
        return 0;
    row.Id(r.ID) >> r.BindTriggerID >> r.CodeName128 >> r.Type >> r.Value >> r.Comment129;
    return r.ID;
}

int RefRow_Read(CRefRow& row, RefGameWorldGroup& r)
{
    row.Id(r.ID) >> r.CodeName128 >> r.ObjName128 >> r.ConfigGroupCodeName128;
    return r.ID;
}

// RefKeyValue4Str::Read (0x49a680): Service, ID and four strings.
static int RefRow_ReadKeyValue4Str(CRefRow& row, int& nID, std::string& s1, std::string& s2,
                                   std::string& s3, std::string& s4)
{
    if (!row.Service())
        return 0;
    row.Id(nID) >> s1 >> s2 >> s3 >> s4;
    return nID;
}

int RefRow_Read(CRefRow& row, RefShardContentConfig& r)
{
    return RefRow_ReadKeyValue4Str(row, r.ID, r.CodeName128, r.CodeDesc128, r.Value, r.Type);
}

int RefRow_Read(CRefRow& row, RefTriggerConditionParam& r)
{
    return RefRow_ReadKeyValue4Str(row, r.ID, r.GroupCodeName128, r.ValueCodeName128, r.Value,
                                   r.Type);
}

int RefRow_Read(CRefRow& row, RefTriggerActionParam& r)
{
    return RefRow_ReadKeyValue4Str(row, r.ID, r.GroupCodeName128, r.ValueCodeName128, r.Value,
                                   r.Type);
}

int RefRow_Read(CRefRow& row, RefGameWorldGroupConfig& r)
{
    return RefRow_ReadKeyValue4Str(row, r.ID, r.GroupCodeName128, r.ValueCodeName128, r.Value,
                                   r.Type);
}

int RefRow_Read(CRefRow& row, RefGameWorldNPC& r)
{
    row >> r.ID >> r.strKey >> r.Entry.str00;
    for (int i = 0; i < 4; ++i)
        row >> r.Entry.aw1C[i];
    for (int i = 0; i < 10; ++i)
        row >> r.Entry.adw24[i];
    return r.ID;
}

int RefRow_Read(CRefRow& row, RefGameWorldConfig& r)
{
    if (!row.Service())
        return 0;
    row >> r.ID >> r.GroupCodeName128 >> r.ValueCodeName128 >> r.Value >> r.Type;
    return r.ID;
}

// The row is read whatever the Service column says; Service is the result.
int RefRow_Read(CRefRow& row, RefObjCharExtraSkill& r)
{
    DWORD dwService;
    row >> dwService >> r.RefObjID;
    for (int i = 0; i < 20; ++i)
        row >> r.aSkillID[i];
    return (int)dwService;
}

// The row of every TacticsID is kept (no Service column).
int RefRow_Read(CRefRow& row, RefAISkill& r)
{
    row >> r.TacticsID >> r.SkillCodeName >> r.b20 >> r.dw24 >> r.dw28;
    return (int)r.TacticsID;
}

// ---------------------------------------------------------------------------------------------
// Server event, rent item and quest reward readers

// Util_SystemTimeDiffSeconds (0x461240): seconds from tsFrom to tsTo, both local times
// (CTime with the DST flag -1).
static __int64 RefData_TimestampDiffSeconds(const TIMESTAMP_STRUCT& tsFrom,
                                            const TIMESTAMP_STRUCT& tsTo)
{
    const TIMESTAMP_STRUCT* apts[2] = { &tsFrom, &tsTo };
    __time64_t at[2];
    for (int i = 0; i < 2; ++i)
    {
        struct tm t;
        memset(&t, 0, sizeof(t));
        t.tm_year = apts[i]->year - 1900;
        t.tm_mon = apts[i]->month - 1;
        t.tm_mday = apts[i]->day;
        t.tm_hour = apts[i]->hour;
        t.tm_min = apts[i]->minute;
        t.tm_sec = apts[i]->second;
        t.tm_isdst = -1;
        at[i] = _mktime64(&t);
    }
    return at[1] - at[0];
}

// GM_GetLocalTimestamp (0x41b490): the local time, fraction 0.
static void RefData_GetLocalTimestamp(TIMESTAMP_STRUCT& ts)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    ts.year = st.wYear;
    ts.month = st.wMonth;
    ts.day = st.wDay;
    ts.hour = st.wHour;
    ts.minute = st.wMinute;
    ts.second = st.wSecond;
    ts.fraction = 0;
}

// The row is kept when its end time is not past.
int RefRow_Read(CRefRow& row, RefServerEvent& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    DWORD dwDropped;
    row >> r.ID >> r.b08 >> r.dw0C >> r.str10 >> r.tsBegin >> r.tsEnd >> r.b4C >> r.b4D >> r.w4E >>
        dwDropped >> r.b50 >> r.dw54 >> r.b58;
    TIMESTAMP_STRUCT tsNow;
    RefData_GetLocalTimestamp(tsNow);
    return RefData_TimestampDiffSeconds(r.tsEnd, tsNow) <= 0;
}

BOOL RefServerEventReward::Read(CRefRow& row)
{
    row >> Service;
    if (!Service)
        return FALSE;
    row >> ID >> ServerEventID >> dw10 >> b14 >> Type >> b16;
    return CReferenceData::GetInstance()->FindServerEvent(ServerEventID) != NULL;
}

static void RefData_ClampUnit(float& f)
{
    if (f > 1.0f)
        f = 1.0f;
    else if (f < 0.0f)
        f = 0.0f;
}

BOOL RefServerEventReward_ExpUPForPlayers::Read(CRefRow& row)
{
    BYTE bPercent1;
    BYTE bPercent2;
    row >> dw18 >> bPercent1 >> bPercent2;
    if (!dw18)
        return FALSE;
    fRate1 = (float)(bPercent1 / 100.0);
    fRate2 = (float)(bPercent2 / 100.0);
    RefData_ClampUnit(fRate1);
    RefData_ClampUnit(fRate2);
    return TRUE;
}

BOOL RefServerEventReward_SpawnMonster::Read(CRefRow& row)
{
    DWORD dwValue;
    row >> dwValue >> af1C[0] >> af1C[1] >> af1C[2];
    w18 = (WORD)dwValue;
    return TRUE;
}

// Service, the key, the columns; with bit 1 of the flags and a count of 1..5, five value
// columns of which the first <count> are kept and the others skipped.
int RefRow_Read(CRefRow& row, RefRentItem& r, std::string& strKey)
{
    DWORD dwService;
    row >> dwService;
    if (!dwService)
        return 0;
    BYTE bCount;
    row >> strKey >> r.dw00 >> r.b04 >> r.b05 >> r.dwFlags >> r.ts0C >> r.ts1C >> bCount;
    if ((r.dwFlags & 2) != 0 && bCount && bCount <= 5)
    {
        for (int i = 0; i < 5; ++i)
        {
            DWORD dwValue = 0;
            if (bCount)
            {
                row >> dwValue;
                r.vecValue.push_back(dwValue);
                --bCount;
            }
            else
            {
                row.Skip();
            }
        }
    }
    return 1;
}

// CompareStringA with the default locale and no flags: TRUE for equal strings.
static BOOL RefData_StrEqual(const char* psz1, const char* psz2)
{
    return CompareStringA(0x400, 0, psz1, lstrlenA(psz1), psz2, lstrlenA(psz2)) == CSTR_EQUAL;
}

// The quest must be known. A positive n18 is allowed for the two temple quests only.
int RefRow_Read(CRefRow& row, RefQuestReward& r)
{
    BYTE bService;
    row >> bService;
    if (!bService)
        return 0;
    row >> r.QuestID;
    CReferenceData::RefQuestMap& mapQuest = CReferenceData::GetInstance()->m_mapQuest;
    CReferenceData::RefQuestMap::iterator it = mapQuest.find(r.QuestID);
    if (it == mapQuest.end() || !it->second)
        return 0;
    for (int i = 0; i < 8; ++i)
        row.Skip();
    row >> r.dw08 >> r.dw0C >> r.dw10 >> r.dw14 >> r.n18 >> r.QuestCodeName >> r.b38 >> r.b39 >>
        r.b04 >> r.b05 >> r.dw3C >> r.str40 >> r.dw5C >> r.str60 >> r.dw7C >> r.str80;
    if (r.n18 <= 0)
        return 1;
    if (RefData_StrEqual(r.QuestCodeName.c_str(), "TEMPLE_ANUBIS"))
        return 1;
    return RefData_StrEqual(r.QuestCodeName.c_str(), "TEMPLE_HAROERIS");
}

int RefRow_Read(CRefRow& row, RefOptionalTeleport& r)
{
    DWORD dwService;
    row >> dwService;
    if (!dwService)
        return 0;
    WORD wRegionID;
    row.Id(r.ID) >> r.ObjName128;
    row.Skip() >> wRegionID;
    RefRegionRow* pRegion = CReferenceData::GetInstance()->FindRegion(wRegionID);
    if (!pRegion)
        LogPrintf(0x2000001, "Invalid Region!![ID:%d][ObjName128:%s]", r.ID, r.ObjName128.c_str());
    r.dwRegion = (DWORD)(DWORD_PTR)pRegion;
    r.w24 = 0;
    row >> r.Pos_X >> r.Pos_Y >> r.Pos_Z >> r.WorldID;
    row.Skip().Skip() >> r.LevelMin >> r.LevelMax;
    for (int i = 0; i < 3; ++i)
    {
        row >> r.adw34[i];
        row.Skip();
    }
    return r.ID;
}

int RefRec_Read(const CRefGameWorld& rec, RefGameWorld& r)
{
    r = rec.m_Data;
    return r.ID;
}

int RefRec_Read(const CRefClimate& rec, RefClimateData& r)
{
    r = rec;
    return r.ID;
}

int RefRec_Read(const CRefGachaNpcMap& rec, RefGachaNpcMapData& r)
{
    r = rec;
    return r.NPC_ID;
}

int RefRec_Read(const CRefGachaCode& rec, RefGachaCode& r)
{
    r.CodeName128 = rec.m_Data.CodeName128;
    r.GachaSetID = rec.m_Data.GachaSetID;
    return rec.m_Data.GachaSetID;
}

int RefRec_Read(const CRefQuest& rec, RefQuest& r)
{
    if (!rec.m_Data.Service)
        return 0;
    r = rec.m_Data;
    return r.ID;
}

// ---------------------------------------------------------------------------------------------
// Siege table readers: the columns are members of the CDBRecord record (the data part of the
// record class has the layout of the row), so the readers are copies.

int RefRec_Read(const CRefSiegeFortress& rec, RefSiegeFortress& r)
{
    // RefData_CopyRecord_497EC0 is the member-wise copy.
    r = rec.m_Data;
    return r.Service;
}

int RefRec_Read(const CRefSiegeFortressGuard& rec, RefSiegeFortressGuardData& r)
{
    r = rec;
    return r.FortressID;
}

int RefRec_Read(const CRefSiegeFortressItemForge& rec, RefSiegeFortressItemForgeData& r)
{
    r = rec;
    return r.FortressID;
}

int RefRec_Read(const CRefSiegeFortressBattleRank& rec, RefSiegeFortressBattleRank& r)
{
    r = rec.m_Data;
    return r.Service;
}

int RefRec_Read(const CRefSiegeStructUpgrade& rec, RefSiegeStructUpgrade& r)
{
    // RefSiegeStructUpgrade::CopyFrom (0x498020) is the member-wise copy.
    r = rec.m_Data;
    return r.Service;
}

int RefRec_Read(const CRefSiegeLvlSummonMonster& rec, RefSiegeLvlSummonMonsterData& r)
{
    r = rec;
    return r.RefOrgObjID;
}

void RefRec_Read(const CRefSiegeFortressRewards& rec, RefSiegeFortressRewardsData& r)
{
    r = rec;
}

int RefRec_Read(const CRefSiegeBlessBuff& rec, RefSiegeBlessBuffData& r)
{
    r = rec;
    return r.Service != 0;
}

int RefRec_Read(const CRefSiegeDungeon& rec, RefSiegeDungeonData& r)
{
    r = rec;
    return r.Service != 0;
}

int RefRec_Read(const CRefSiegeQuest& rec, RefSiegeQuest& r)
{
    r = rec.m_Data;
    return r.Service != 0;
}

int RefRec_Read(const CRefSiegeQuestReward& rec, RefSiegeQuestRewardData& r)
{
    r = rec;
    return r.Service != 0;
}

// ---------------------------------------------------------------------------------------------
// Table builders

typedef int (*PFN_REF_BUILD)(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot);

// The builder of a table whose rows go into a map / multimap of the object, keyed by a column
// of the row (RefData_Build<Table> + CRefInstanceGenerator::Load<Table>, e.g. 0x4d0b40 /
// 0x4a7b10, or the builders that read the row themselves, 0x4d0a90 / 0x4d0fc0): FALSE for an
// empty table; a row the reader rejects is freed.
template <class T, class TMap, TMap CReferenceData::*pMap, class TKey, TKey T::*pKey>
int RefData_BuildKeyed(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    if (pTable->m_listRows.size() == 0)
        return 0;
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        CRefRow row(static_cast<CDBRecStream*>(*it));
        T* pRow = new T;
        if (RefRow_Read(row, *pRow))
        {
            // BUG(SM-007): a row whose key is already in a map is not inserted and leaks.
            (pData->*pMap).insert(typename TMap::value_type(pRow->*pKey, pRow));
        }
        else
        {
            delete pRow;
        }
    }
    return 1;
}

#define REF_KEYED(T, map_type, member, key_type, key)                                           \
    &RefData_BuildKeyed<T, CReferenceData::map_type, &CReferenceData::member, key_type, &T::key>

// The builder of a table with a loader per row (RefData_BuildRef<Table>, e.g. 0x4cfc70): FALSE
// for an empty table; a row the loader fails asserts and stops the build (FALSE).
typedef int (*PFN_REF_LOAD)(CReferenceData* pData, CDBRecord* pRecord);

template <PFN_REF_LOAD pfnLoad>
int RefData_BuildLoaded(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
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

#define REF_LOADED(load) &RefData_BuildLoaded<load>

// ---------------------------------------------------------------------------------------------
// Siege table loaders (CRefInstanceGenerator::LoadSiege*, slots 0x48..0x52)

// LoadSiegeFortress (0x4a4980), LoadSiegeFortressBattleRank (0x4a4ca0), LoadSiegeStructUpgrade
// (0x4a4dd0): rows in service are appended to a vector, the others freed.
template <class TRec, class T, std::vector<T*> CReferenceData::*pVector>
int RefData_LoadSiegeVector(CReferenceData* pData, CDBRecord* pRecord)
{
    T* pRow = new T;
    if (RefRec_Read(*static_cast<TRec*>(pRecord), *pRow))
        (pData->*pVector).push_back(pRow);
    else
        delete pRow;
    return 1;
}

// LoadSiegeFortressGuard (0x4a4b50), LoadSiegeFortressItemForge (0x4a4bf0): keyed by
// FortressID; a row without one is freed.
template <class TRec, class T, std::multimap<int, T*> CReferenceData::*pMap>
int RefData_LoadSiegeByFortress(CReferenceData* pData, CDBRecord* pRecord)
{
    T* pRow = new T;
    // BUG(SM-009): the Service column is not looked at.
    int nFortressID = RefRec_Read(*static_cast<TRec*>(pRecord), *pRow);
    if (nFortressID)
        (pData->*pMap).insert(std::make_pair(nFortressID, pRow));
    else
        delete pRow;
    return 1;
}

// LoadSiegeLvlSummonMonster (0x4a4f10): per original monster (RefOrgObjID) a vector of 140
// rows, indexed by the level of the summoned monster - 1. A row without RefOrgObjID, with an
// unknown monster or with a level that is already taken fails the build.
int RefData_LoadSiegeLvlSummonMonster(CReferenceData* pData, CDBRecord* pRecord)
{
    RefSiegeLvlSummonMonsterData* pRow = new RefSiegeLvlSummonMonsterData;
    // BUG(SM-009): the Service column is not looked at.
    DWORD dwOrgObjID = (DWORD)RefRec_Read(*static_cast<CRefSiegeLvlSummonMonster*>(pRecord),
                                          *pRow);
    if (!dwOrgObjID)
    {
        BSAssertFail();
        return 0;
    }
    CReferenceData::RefSiegeLvlSummonMonsterMap& map = pData->m_mapSiegeLvlSummonMonster;
    CReferenceData::RefSiegeLvlSummonMonsterMap::iterator it = map.find(dwOrgObjID);
    CReferenceData::RefSiegeSummonByLevelVector* pLevels;
    if (it == map.end())
    {
        pLevels = new CReferenceData::RefSiegeSummonByLevelVector;
        pLevels->resize(140);
        map.insert(std::make_pair(dwOrgObjID, pLevels));
    }
    else
    {
        pLevels = it->second;
    }
    RefObjCommonRow* pObj = CReferenceData::GetInstance()->FindRefObj(pRow->RefObjID);
    if (!pObj || pLevels->at(RefObj_GetByte198(pObj) - 1))
    {
        BSAssertFail();
        return 0;
    }
    pLevels->at(RefObj_GetByte198(pObj) - 1) = pRow;
    return 1;
}

// LoadSiegeFortressRewards (0x4a50b0): every row, keyed by FortressID.
int RefData_LoadSiegeFortressRewards(CReferenceData* pData, CDBRecord* pRecord)
{
    RefSiegeFortressRewardsData* pRow = new RefSiegeFortressRewardsData;
    // BUG(SM-009): the Service column is not looked at.
    RefRec_Read(*static_cast<CRefSiegeFortressRewards*>(pRecord), *pRow);
    pData->m_mmapSiegeFortressRewards.insert(std::make_pair(pRow->FortressID, pRow));
    return 1;
}

// LoadSiegeBlessBuff (0x4a5170), LoadSiegeDungeon (0x4a5250): unique key in the second column
// (BlessID / FortressID); a duplicate is freed and fails the build.
template <class TRec, class T, int T::*pKey, std::map<DWORD, T*> CReferenceData::*pMap>
int RefData_LoadSiegeUnique(CReferenceData* pData, CDBRecord* pRecord)
{
    T* pRow = new T;
    // BUG(SM-008): a row out of service is neither stored nor freed.
    if (!RefRec_Read(*static_cast<TRec*>(pRecord), *pRow))
        return 1;
    if ((pData->*pMap).find((DWORD)(pRow->*pKey)) != (pData->*pMap).end())
    {
        delete pRow;
        return 0;
    }
    (pData->*pMap).insert(std::make_pair((DWORD)(pRow->*pKey), pRow));
    return 1;
}

// LoadSiegeQuest (0x4a5330): rows in service, keyed by QuestType.
int RefData_LoadSiegeQuest(CReferenceData* pData, CDBRecord* pRecord)
{
    RefSiegeQuest* pRow = new RefSiegeQuest;
    // BUG(SM-008): a row out of service is neither stored nor freed.
    if (RefRec_Read(*static_cast<CRefSiegeQuest*>(pRecord), *pRow))
        pData->m_mmapSiegeQuest.insert(std::make_pair((int)pRow->QuestType, pRow));
    return 1;
}

// LoadSiegeQuestReward (0x4a53e0): rows in service, keyed by QuestID.
int RefData_LoadSiegeQuestReward(CReferenceData* pData, CDBRecord* pRecord)
{
    RefSiegeQuestRewardData* pRow = new RefSiegeQuestRewardData;
    // BUG(SM-008): a row out of service is neither stored nor freed.
    if (RefRec_Read(*static_cast<CRefSiegeQuestReward*>(pRecord), *pRow))
        pData->m_mmapSiegeQuestReward.insert(std::make_pair(pRow->QuestID, pRow));
    return 1;
}

// CRefInstanceGenerator::LoadGameWorldNPC (0x4a63f0): the rows of one key are collected in a
// vector (copies of the row part after the key).
int RefData_LoadGameWorldNPC(CReferenceData* pData, CDBRecord* pRecord)
{
    CRefRow row(static_cast<CDBRecStream*>(pRecord));
    RefGameWorldNPC* pRow = new RefGameWorldNPC;
    if (!RefRow_Read(row, *pRow))
    {
        delete pRow;
        return 1;
    }
    CReferenceData::RefGameWorldNPCMap& map = pData->m_mapGameWorldNPC;
    CReferenceData::RefGameWorldNPCMap::iterator it = map.find(pRow->strKey);
    if (it == map.end())
    {
        CReferenceData::RefGameWorldNPCVector vec;
        vec.push_back(pRow->Entry);
        map.insert(std::make_pair(pRow->strKey, vec));
    }
    else
    {
        it->second.push_back(pRow->Entry);
    }
    // BUG(SM-010): the parsed row is copied and never freed.
    return 1;
}

// ---------------------------------------------------------------------------------------------
// Quest, server event, rent item and quest reward loaders

// RefData_AddRow<Table> (e.g. RefData_AddRowRefQuest 0x4d2ad0): a row the reader rejects is
// freed; a key that is already in the map frees the row and fails the build.
template <class T, class TMap, TMap CReferenceData::*pMap>
int RefData_InsertUnique(CReferenceData* pData, T* pRow, typename TMap::key_type key)
{
    if (!key)
    {
        delete pRow;
        return 1;
    }
    typename TMap::iterator it = (pData->*pMap).find(key);
    if (it != (pData->*pMap).end() && it->second)
    {
        delete pRow;
        BSAssertFail();
        return 0;
    }
    (pData->*pMap).insert(typename TMap::value_type(key, pRow));
    return 1;
}

// For a CDBRecord table.
template <class TRec, class T, class TMap, TMap CReferenceData::*pMap>
int RefData_AddRowUnique(CReferenceData* pData, CDBRecord* pRecord)
{
    T* pRow = new T;
    typename TMap::key_type key = RefRec_Read(*static_cast<TRec*>(pRecord), *pRow);
    return RefData_InsertUnique<T, TMap, pMap>(pData, pRow, key);
}

// For a stream table.
template <class T, class TMap, TMap CReferenceData::*pMap>
int RefData_AddRowUniqueStream(CReferenceData* pData, CDBRecord* pRecord)
{
    CRefRow row(static_cast<CDBRecStream*>(pRecord));
    T* pRow = new T;
    typename TMap::key_type key = RefRow_Read(row, *pRow);
    return RefData_InsertUnique<T, TMap, pMap>(pData, pRow, key);
}

// CRefInstanceGenerator::LoadServerEvent (0x4a69a0): rows in service that have not ended; a
// duplicate ID fails the build.
int RefData_LoadServerEvent(CReferenceData* pData, CDBRecord* pRecord)
{
    CRefRow row(static_cast<CDBRecStream*>(pRecord));
    RefServerEvent* pRow = new RefServerEvent;
    if (!RefRow_Read(row, *pRow))
    {
        delete pRow;
        return 1;
    }
    if (pData->m_mapServerEvent.find(pRow->ID) != pData->m_mapServerEvent.end())
    {
        // BUG(SM-007): the duplicate row is not freed.
        BSAssertFail();
        return 0;
    }
    pData->m_mapServerEvent.insert(std::make_pair(pRow->ID, pRow));
    return 1;
}

// CRefInstanceGenerator::LoadServerEventReward (0x4a6ad0): the base row decides the class of
// the reward; the reward is also listed under its server event.
int RefData_LoadServerEventReward(CReferenceData* pData, CDBRecord* pRecord)
{
    CRefRow row(static_cast<CDBRecStream*>(pRecord));
    RefServerEventReward base;
    if (!base.Read(row))
        return 1;
    RefServerEventReward* pReward;
    switch (base.Type)
    {
    case RefServerEventReward::TYPE_EXP_UP_FOR_PLAYERS:
        pReward = new RefServerEventReward_ExpUPForPlayers(base);
        break;
    case RefServerEventReward::TYPE_PLAIN:
        pReward = new RefServerEventReward(base);
        break;
    case RefServerEventReward::TYPE_SPAWN_MONSTER:
        pReward = new RefServerEventReward_SpawnMonster(base);
        break;
    default:
        BSAssertFail();
        return 0;
    }
    if (pData->m_mapServerEventReward.find(pReward->ID) != pData->m_mapServerEventReward.end())
    {
        // BUG(SM-007): the duplicate reward is not freed.
        BSAssertFail();
        return 0;
    }
    pData->m_mapServerEventReward.insert(std::make_pair(pReward->ID, pReward));
    CReferenceData::GetInstance()->m_mmapServerEventRewardID.insert(
        std::make_pair(pReward->ServerEventID, pReward->ID));
    return 1;
}

// CRefInstanceGenerator::ReadServerEventRewardDetail (0x4a6c90): the derived columns of a
// reward (its class reads them); a row of an unknown reward is skipped.
int RefData_ReadServerEventRewardDetail(CReferenceData* pData, CDBRecord* pRecord)
{
    CRefRow row(static_cast<CDBRecStream*>(pRecord));
    DWORD dwID;
    row >> dwID;
    CReferenceData::RefServerEventRewardMap::iterator it =
        pData->m_mapServerEventReward.find(dwID);
    if (it == pData->m_mapServerEventReward.end())
        return 1;
    return it->second->Read(row);
}

// RefData_BuildRefServerEventReward (0x4d0650), slots 0x61..0x63: the base table, then the two
// detail tables. A failing row asserts twice and stops the build.
static int RefData_BuildServerEventReward(CReferenceData* pData, RefTableEntry* pTable,
                                          unsigned int nSlot)
{
    if (pTable->m_listRows.size() == 0)
        return 0;
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        int nResult = 0;
        if (*it)
        {
            if (nSlot == 0x61)
                nResult = RefData_LoadServerEventReward(pData, *it);
            else if (nSlot > 0x61 && nSlot <= 0x63)
                nResult = RefData_ReadServerEventRewardDetail(pData, *it);
        }
        if (!nResult)
        {
            BSAssertFail();
            BSAssertFail();
            return 0;
        }
    }
    return 1;
}

// CRefInstanceGenerator::LoadRentItem (0x4a6d10): keyed by the key column.
int RefData_LoadRentItem(CReferenceData* pData, CDBRecord* pRecord)
{
    CRefRow row(static_cast<CDBRecStream*>(pRecord));
    std::string strKey;
    RefRentItem* pRow = new RefRentItem;
    // BUG(SM-011): a row out of service is not freed and fails the whole build.
    if (!RefRow_Read(row, *pRow, strKey))
        return 0;
    pData->m_hashRentItem.insert(std::make_pair(strKey, pRow));
    return 1;
}

// CRefInstanceGenerator::LoadQuestReward (0x4a6e30): keyed by QuestID.
int RefData_LoadQuestReward(CReferenceData* pData, CDBRecord* pRecord)
{
    CRefRow row(static_cast<CDBRecStream*>(pRecord));
    RefQuestReward* pRow = new RefQuestReward;
    if (RefRow_Read(row, *pRow))
        pData->m_mapQuestReward.insert(std::make_pair(pRow->QuestID, pRow));
    else
        delete pRow;
    return 1;
}

// Slot 0x3D (RefData_BuildRefGameWorld 0x4cf880, RefData_AddRowRefGameWorld 0x4d2bb0): the game
// worlds by ID, then CRefInstanceGenerator::IndexSkillsByCodeName (0x4aa550), which indexes
// them by WorldCodeName128.
static int RefData_BuildGameWorld(CReferenceData* pData, RefTableEntry* pTable, unsigned int nSlot)
{
    int nResult = RefData_BuildLoaded<RefData_AddRowUnique<CRefGameWorld, RefGameWorld,
        CReferenceData::RefGameWorldMap, &CReferenceData::m_mapGameWorld> >(pData, pTable, nSlot);
    if (nResult == 1)
    {
        for (CReferenceData::RefGameWorldMap::iterator it = pData->m_mapGameWorld.begin();
             it != pData->m_mapGameWorld.end(); ++it)
        {
            pData->m_hashGameWorldByCodeName.insert(
                std::make_pair(it->second->WorldCodeName128, it->second));
        }
    }
    return nResult;
}

// Builders that copy the data part of each CDBRecord row into a new row appended to a vector:
// RefData_BuildRefLevel (0x4ceff0, RefRegion_CopyRow 0x48eb50: a row without Lvl asserts and is
// kept), RefData_BuildRefInstanceWorldRegion (0x4cf8f0, RefInstanceWorldRegion_CopyKey 0x497750:
// a row without WorldID is freed) and RefData_BuildRefInstanceWorldStartPos (0x4cf990,
// RefData_CopyRowHeader_497760). FALSE for an empty table only.
static BOOL RefData_KeepCopy(const RefLevelData& r)
{
    if (!r.Lvl)
        BSAssertFail();
    return TRUE;
}

static BOOL RefData_KeepCopy(const RefInstanceWorldRegionData& r)
{
    return r.WorldID != 0;
}

static BOOL RefData_KeepCopy(const RefInstanceWorldStartPosData&)
{
    return TRUE;
}

template <class TRec, class T, std::vector<T*> CReferenceData::*pVector>
int RefData_BuildCopyVector(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    if (pTable->m_listRows.size() == 0)
        return 0;
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        T* pRow = new T;
        *pRow = *static_cast<TRec*>(*it);
        if (RefData_KeepCopy(*pRow))
            (pData->*pVector).push_back(pRow);
        else
            delete pRow;
    }
    return 1;
}

// A table whose builder is not ported yet: nothing is built and the build goes on.
static int RefData_BuildPending(CReferenceData*, RefTableEntry*, unsigned int)
{
    return 1;
}

// Slot 7 (_RefMagicOpt): a table of fewer than 0x44 rows is an old one and fails the build.
static int RefData_BuildMagicOpt(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    if (pTable->m_listRows.size() < 0x44)
    {
        BSMessageBoxPrintf("ssiba crazy DOWOOL copy newest magic option table!!!!");
        LogPrintf(0x2000000, "ssiba crazy DOWOOL copy newest magic option table!!!!");
        return 0;
    }
    pData->m_MagicOpt.BuildMagicOptTable(pTable->m_listRows);
    return 1;
}

// Slot 8 (_RefMagicOptAssign).
static int RefData_BuildMagicOptAssign(CReferenceData* pData, RefTableEntry* pTable,
                                       unsigned int)
{
    pData->m_MagicOpt.BuildMagicOptAssignTables(pTable->m_listRows);
    return 1;
}

// CRefInstanceGenerator::LoadMagicOptGroup (0x4a5c30): a row the reader rejects is freed; the
// others are appended to the rows of their group key.
int RefData_LoadMagicOptGroup(CReferenceData* pData, CDBRecord* pRecord)
{
    RefMagicOptGroupRow* pRow = new RefMagicOptGroupRow;
    if (!pRow)
    {
        BSAssertFail();
        return 0;
    }
    CRefRow row(static_cast<CDBRecStream*>(pRecord));
    DWORD dwKey = RefRow_Read(row, *pRow);
    if (!dwKey)
    {
        delete pRow;
        return 1;
    }
    CReferenceData::RefMagicOptGroupMap::iterator it = pData->m_mapMagicOptGroup.find(dwKey);
    if (it == pData->m_mapMagicOptGroup.end())
    {
        CReferenceData::RefMagicOptGroupVector vecRow;
        vecRow.push_back(pRow);
        pData->m_mapMagicOptGroup.insert(std::make_pair(dwKey, vecRow));
    }
    else
    {
        it->second.push_back(pRow);
    }
    return 1;
}

// Slot 0x11 (_RefAbilityByItemOptLevel): builds the three item option level tables; slots 0xF
// and 0x10 build nothing themselves.
static int RefData_BuildItemOptLevel(CReferenceData* pData, RefTableEntry*, unsigned int)
{
    return pData->LoadByItemOptLevelTables();
}

#define P RefData_BuildPending
// Read by the builder of slot 0x27 (RefDataShop.cpp).
#define S RefData_BuildWithShopTables

// The switch of CRefInstanceGenerator::BuildFromTable (0x4ac550), one entry per slot.
static const PFN_REF_BUILD s_apfnBuild[REF_TABLE_SLOT_COUNT] =
{
    &RefData_BuildCopyVector<CRefLevel, RefLevelData, &CReferenceData::m_vecLevel>,  // 0x00
    REF_LOADED((RefData_AddRowUnique<CRefClimate, RefClimateData, CReferenceData::RefClimateMap,
                &CReferenceData::m_mapClimate>)),                           // 0x01
    RefData_BuildRegionBindAssocServer,                                     // 0x02
    RefData_BuildRegion,
    RefData_BuildRefSkillMastery, RefData_BuildRefSkill,
    REF_LOADED((RefData_AddRowUnique<CRefGachaNpcMap, RefGachaNpcMapData,
                CReferenceData::RefGachaNpcMapMap,
                &CReferenceData::m_mapGachaNpcMap>)),                       // 0x06
    RefData_BuildMagicOpt,                                                  // 0x07
    RefData_BuildMagicOptAssign,
    REF_LOADED(RefData_LoadMagicOptGroup),
    REF_LOADED((RefData_AddRowUniqueStream<RefSetItemGroup, CReferenceData::RefSetItemGroupMap,
                &CReferenceData::m_mapSetItemGroup>)),                      // 0x0A
    RefData_BuildRefObjCommon, P, P, P, P,
    P, RefData_BuildItemOptLevel,                                           // 0x10
    RefData_BuildTeleport,                                                  // 0x12
    RefData_BuildTeleLink,
    P, S, S, S,
    S, S, S, S, S, S, S, S,                                                 // 0x18
    S, S, S, S, S, S, S, RefData_BuildShopTables,                           // 0x20
    RefData_BuildDropGold,                                                  // 0x28
    RefData_BuildDropOptLevelSel,
    RefData_BuildDropClassSel, RefData_BuildDropClassSel, RefData_BuildDropClassSel,
    RefData_BuildDropClassSel, RefData_BuildDropClassSel, RefData_BuildDropClassSel,
    RefData_BuildDropClassSel, RefData_BuildDropClassSel, RefData_BuildDropClassSel,
    RefData_BuildDropClassSel,                                              // 0x33
    RefData_BuildDropClassTables,
    RefData_BuildDropItemGroup,
    RefData_BuildMonsterAssignedItemRndDrop,                                // 0x36
    RefData_BuildRefCharGenData,
    RefData_BuildHive,                                                      // 0x38
    REF_KEYED(RefAISkill, RefAISkillMap, m_mmapAISkill, DWORD, TacticsID),  // 0x39
    RefData_BuildTactics,
    RefData_LoadNest,
    REF_LOADED((RefData_AddRowUnique<CRefQuest, RefQuest, CReferenceData::RefQuestMap,
                &CReferenceData::m_mapQuest>)),                             // 0x3C
    RefData_BuildGameWorld,
    &RefData_BuildCopyVector<CRefInstanceWorldRegion, RefInstanceWorldRegionData,
                             &CReferenceData::m_vecInstanceWorldRegion>,    // 0x3E
    &RefData_BuildCopyVector<CRefInstanceWorldStartPos, RefInstanceWorldStartPosData,
                             &CReferenceData::m_vecInstanceWorldStartPos>,
    RefData_LoadRankingActivity, RefData_LoadRankingContribution,           // 0x40
    RefData_LoadRankingActivity, RefData_LoadRankingContribution,
    RefData_LoadRankingActivity, RefData_LoadRankingContribution,
    REF_LOADED((RefData_AddRowUnique<CRefGachaCode, RefGachaCode, CReferenceData::RefGachaCodeMap,
                &CReferenceData::m_mapGachaCode>)),                         // 0x46
    RefData_BuildGachaItemSet,
    REF_LOADED((RefData_LoadSiegeVector<CRefSiegeFortress, RefSiegeFortress,
                &CReferenceData::m_vecSiegeFortress>)),                     // 0x48
    REF_LOADED((RefData_LoadSiegeByFortress<CRefSiegeFortressGuard, RefSiegeFortressGuardData,
                &CReferenceData::m_mmapSiegeFortressGuard>)),
    REF_LOADED((RefData_LoadSiegeByFortress<CRefSiegeFortressItemForge,
                RefSiegeFortressItemForgeData, &CReferenceData::m_mmapSiegeFortressItemForge>)),
    REF_LOADED((RefData_LoadSiegeVector<CRefSiegeFortressBattleRank, RefSiegeFortressBattleRank,
                &CReferenceData::m_vecSiegeFortressBattleRank>)),
    REF_LOADED((RefData_LoadSiegeVector<CRefSiegeStructUpgrade, RefSiegeStructUpgrade,
                &CReferenceData::m_vecSiegeStructUpgrade>)),
    REF_LOADED(RefData_LoadSiegeLvlSummonMonster),                          // 0x4D
    REF_LOADED(RefData_LoadSiegeFortressRewards),
    REF_LOADED((RefData_LoadSiegeUnique<CRefSiegeBlessBuff, RefSiegeBlessBuffData,
                &RefSiegeBlessBuffData::BlessID, &CReferenceData::m_mapSiegeBlessBuff>)),
    REF_LOADED((RefData_LoadSiegeUnique<CRefSiegeDungeon, RefSiegeDungeonData,
                &RefSiegeDungeonData::FortressID, &CReferenceData::m_mapSiegeDungeon>)),
    REF_LOADED(RefData_LoadSiegeQuest),                                     // 0x51
    REF_LOADED(RefData_LoadSiegeQuestReward),
    REF_LOADED(RefData_LoadIdBytePairsTable),                               // 0x53
    REF_LOADED((RefData_AddRowUniqueStream<RefEventRow, CReferenceData::RefEventMap,
                &CReferenceData::m_mapEvent>)),
    REF_LOADED(RefData_LoadRentItem),                                       // 0x55
    P, RefData_BuildCustomizingReservedItemDrop,
    REF_LOADED((RefData_AddRowUniqueStream<RefDefaultQuestRow, CReferenceData::RefDefaultQuestMap,
                &CReferenceData::m_mapDefaultQuest>)),                      // 0x58
    REF_LOADED((RefData_AddRowUniqueStream<RefOptionalTeleport,
                CReferenceData::RefOptionalTeleportMap,
                &CReferenceData::m_mapOptionalTeleport>)),                  // 0x59
    REF_KEYED(RefShardContentConfig, RefShardContentConfigMap, m_mapShardContentConfig,
              std::string, CodeName128),                                    // 0x5A
    S,                                                                      // 0x5B
    REF_LOADED(RefData_LoadGameWorldNPC),
    REF_KEYED(RefObjCharExtraSkill, RefObjCharExtraSkillMap, m_mapObjCharExtraSkill, int,
              RefObjID),
    REF_KEYED(RefGameWorldConfig, RefGameWorldConfigMap, m_mapGameWorldConfig, std::string,
              GroupCodeName128),                                            // 0x5E
    RefData_BuildAlchemyMerit,                                              // 0x5F
    REF_LOADED(RefData_LoadServerEvent),                                    // 0x60
    RefData_BuildServerEventReward,
    RefData_BuildServerEventReward,
    RefData_BuildServerEventReward,
    REF_LOADED(RefData_LoadQuestReward),                                    // 0x64
    RefData_BuildQuestRewardItems,
    RefData_BuildEventReward,
    RefData_BuildEventRewardItems,
    REF_LOADED(RefData_LoadCollectionBookTheme),                            // 0x68
    RefData_BuildCollectionBookItem,
    REF_KEYED(RefUIString, RefUIStringMap, m_mapUIString, std::string, GroupCodeName128),
    REF_KEYED(RefTriggerBind, RefTriggerBindMap, m_mmapGameWorldBindTriggerCategory, int, ID1),
    REF_KEYED(RefTriggerCategory, RefTriggerCategoryMap, m_mapTriggerCategory, int, ID),
    REF_KEYED(RefTriggerBind, RefTriggerBindMap, m_mmapTriggerCategoryBindTrigger, int, ID1),
    REF_KEYED(RefTrigger, RefTriggerMap, m_mapTrigger, int, ID),            // 0x6E
    REF_KEYED(RefTriggerVariable, RefTriggerVariableMap, m_mmapTriggerVariable, int,
              BindTriggerID),
    REF_KEYED(RefTriggerBind, RefTriggerBindMap, m_mmapTriggerBindEvent, int, ID1),
    REF_KEYED(RefTriggerBind, RefTriggerBindMap, m_mmapTriggerBindCondition, int, ID1),
    REF_KEYED(RefTriggerBind, RefTriggerBindMap, m_mmapTriggerBindAction, int, ID1),
    REF_KEYED(RefTriggerCommon, RefTriggerCommonMap, m_mapTriggerCommon, int, ID),
    REF_KEYED(RefTriggerEvent, RefTriggerEventMap, m_mapTriggerEvent, int, ID),
    REF_KEYED(RefTriggerCondition, RefTriggerConditionMap, m_mapTriggerCondition, int, ID),
    REF_KEYED(RefTriggerAction, RefTriggerActionMap, m_mapTriggerAction, int, ID),
    REF_KEYED(RefTriggerConditionParam, RefTriggerConditionParamMap, m_mapTriggerConditionParam,
              std::string, GroupCodeName128),
    REF_KEYED(RefTriggerActionParam, RefTriggerActionParamMap, m_mapTriggerActionParam,
              std::string, GroupCodeName128),
    REF_KEYED(RefTriggerBind, RefTriggerBindMap, m_mmapGameWorldBindGameWorldGroup, int, ID1),
    REF_KEYED(RefGameWorldGroup, RefGameWorldGroupMap, m_mapGameWorldGroup, int, ID),
    REF_KEYED(RefGameWorldGroupConfig, RefGameWorldGroupConfigMap, m_mapGameWorldGroupConfig,
              std::string, GroupCodeName128),                               // 0x7B
};

#undef P
#undef S

// ---------------------------------------------------------------------------------------------
// CReferenceData

template <class TMap>
static void RefData_DeleteValues(TMap& map)
{
    for (typename TMap::iterator it = map.begin(); it != map.end(); ++it)
        delete it->second;
    map.clear();
}

template <class TVector>
static void RefData_DeleteElements(TVector& vec)
{
    for (typename TVector::iterator it = vec.begin(); it != vec.end(); ++it)
        delete *it;
    vec.clear();
}

// Ranking packets ------------------------------------------------------------------------------

// The empty packet of one ranking type: a 1280-byte buffer holding one 0 byte.
static CStreamBuffer* RefData_NewRankingPacket()
{
    CStreamBuffer* pBuffer = new CStreamBuffer;
    if (!pBuffer)
        BSAssertFail();
    if (pBuffer->m_nCapacity < 1280)
    {
        if (pBuffer->m_pBuffer)
        {
            operator delete(pBuffer->m_pBuffer);
            pBuffer->m_pBuffer = NULL;
        }
        pBuffer->m_pBuffer = (BYTE*)operator new(0x500);
        pBuffer->m_nCapacity = 1280;
    }
    if (!pBuffer->m_pBuffer)
        BSAssertFail();
    pBuffer->m_nWritePos = 0;
    pBuffer->m_nReadPos = 0;
    if (!pBuffer->m_pBuffer)
        BSAssertFail();
    if (pBuffer->m_nWritePos + 1 > pBuffer->m_nCapacity)
        BSAssertFailAndThrow();
    pBuffer->m_pBuffer[pBuffer->m_nWritePos] = 0;
    ++pBuffer->m_nWritePos;
    return pBuffer;
}

RefRankingPackets::RefRankingPackets()
{
    for (int i = 0; i < 3; ++i)
        m_vecActivity.push_back(RefData_NewRankingPacket());
    for (int j = 0; j < 3; ++j)
        m_vecContribution.push_back(RefData_NewRankingPacket());
}

RefRankingPackets::~RefRankingPackets()
{
    for (unsigned int i = 0; i < 3; ++i)
    {
        if (m_vecActivity.at(i))
            delete m_vecActivity.at(i);
    }
    for (unsigned int j = 0; j < 3; ++j)
    {
        if (m_vecContribution.at(j))
            delete m_vecContribution.at(j);
    }
    m_vecActivity.clear();
    m_vecContribution.clear();
}

// Magic option and drop class tables ----------------------------------------------------------

// RefData_InitLevelBandMap (0x519b30): the first level of each of the 42 level bands.
static const int s_aLevelBand[42] =
{
    1, 3, 5, 8, 10, 13, 16, 18, 21, 24, 26, 29, 32, 35, 38, 42, 45, 48, 52, 56, 60, 64, 68, 72,
    76, 80, 85, 90, 94, 99, 104, 109, 114, 120, 127, 134, 141, 148, 156, 164, 173, 183,
};

RefMagicOptTables::RefMagicOptTables()
{
    memset(m_adw00, 0, sizeof(m_adw00));
    for (int i = 0; i < 23; ++i)
        m_adw00[i] = 8;
    for (int nBand = 0; nBand < 42; ++nBand)
        m_mapLevelBand.insert(std::make_pair(s_aLevelBand[nBand], nBand + 1));
}

RefMagicOptTables::~RefMagicOptTables()
{
    for (unsigned int i = 0; i < m_vecMagicOptGroup.size(); ++i)
    {
        if (m_vecMagicOptGroup[i])
            delete m_vecMagicOptGroup[i];
    }
    m_vecMagicOptGroup.clear();
    RefData_DeleteValues(m_mapMagicOptLevel);
}

// The source file the original's assert dialogs name.
static const char s_szReferenceDataFile[] =
    "D:\\WORK2005\\Source\\SilkroadOnline\\Server\\ServerCommon\\ReferenceData.cpp";

// The "xxx" placeholder of the unused AvailItemGroupN / AvailMOptN columns (0x783f08).
static const char s_szUnusedName[] = "xxx";

// Util_StrCmpNoCase (0x41c0b0) as a test: CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE |
// SORT_STRINGSORT) equal.
static BOOL RefData_StrEqualNoCase(const char* psz1, const char* psz2)
{
    return CompareStringA(0x400, 0x10001, psz1, lstrlenA(psz1), psz2, lstrlenA(psz2)) == 2;
}

static BYTE RefObj_GetCountry(const RefObjCommonRow* pObj)                       // +0x88
{
    return *((const BYTE*)pObj + 0x88);
}

static BYTE RefObj_GetItemClass(const RefObjCommonRow* pObj)                     // +0x1A8
{
    return *((const BYTE*)pObj + 0x1A8);
}

// The key of an item in MagOptEachLevelTable: TID3 | TID4 << 16.
static DWORD RefObj_GetMagOptAssignKey(const RefObjCommonRow* pObj)
{
    WORD wTypeID = RefObj_GetTypeID(pObj);
    return ((wTypeID >> 7) & 0xF) | ((DWORD)(wTypeID & 0xF800) << 5);
}

// Parameter descriptors ------------------------------------------------------------------------

void mfunc_single_value::Init(const int* pParam)
{
    m_dwCode = pParam[0];
    m_dw0C = pParam[1];
    m_dwValue = pParam[2];
    m_dw08 = pParam[3];
}

DWORD mfunc_single_value::Random()
{
    return 0;
}

DWORD mfunc_single_value::GetMin()
{
    return m_dwValue;
}

DWORD mfunc_single_value::GetMax()
{
    return m_dwValue;
}

void mfunc_single_range::Init(const int* pParam)
{
    m_dwCode = pParam[0];
    m_dw0C = pParam[1];
    m_dwMin = pParam[2];
    m_dwMax = pParam[3];
    m_dw08 = pParam[4];
    if (m_dwMax < m_dwMin)
        BSAssertFail();
}

DWORD mfunc_single_range::Random()
{
    DWORD dwRange = m_dwMax - m_dwMin;
    if (!dwRange)
        return m_dwMin;
    float fRate = (float)(rand() / 32767.0);
    return (DWORD)(__int64)(fRate * (double)dwRange + (double)m_dwMin);
}

DWORD mfunc_single_range::GetMin()
{
    return m_dwMin;
}

DWORD mfunc_single_range::GetMax()
{
    return m_dwMax;
}

void mfunc_6_values::Init(const int* pParam)
{
    m_dwCode = pParam[0];
    m_dw0C = pParam[1];
    for (int i = 1; i <= 3; ++i)
    {
        WORD wHigh = HIWORD(pParam[i]);
        if (wHigh)
            m_vecValue.push_back(wHigh);
        WORD wLow = LOWORD(pParam[i]);
        if (wLow)
            m_vecValue.push_back(wLow);
    }
    m_dw08 = pParam[4];
}

DWORD mfunc_6_values::Random()
{
    if (m_vecValue.empty())
        return 0;
    return m_vecValue.at(rand() % (int)m_vecValue.size());
}

DWORD mfunc_6_values::GetMin()
{
    if (m_vecValue.empty())
        BSAssertFail();
    return m_vecValue.at(0);
}

DWORD mfunc_6_values::GetMax()
{
    if (m_vecValue.empty())
        BSAssertFail();
    return m_vecValue.at(m_vecValue.size() - 1);
}

// Magic option rows ----------------------------------------------------------------------------

// The parameter codes of the binary search in RefMagicOpt_IsKnownParamCode /
// RefMagicOpt_BuildParamDesc, by the descriptor they get.
static const DWORD s_adwSixValueCodes[] =
{
    0x6275, 0x6572, 0x6573, 0x667A, 0x6870, 0x6872, 0x6D70, 0x7073, 0x7A62, 0x617065, 0x627269,
    0x637269, 0x696E74, 0x726570, 0x737472, 0x61737472, 0x61746861, 0x62726E61, 0x64757275,
    0x6576626C, 0x65766372, 0x68706D70, 0x696E746A, 0x6C75636B, 0x72616C73, 0x72637369,
    0x72646969, 0x72666569, 0x72736C69, 0x72737469, 0x736F6C69, 0x7374726A, 0x756D6475,
};

static const DWORD s_adwRangeCodes[] =
{
    0x657261, 0x687061, 0x687261, 0x6D7061, 0x64617261, 0x64727561, 0x64757261, 0x68706E61,
    0x68707267, 0x696E7461, 0x696E7476, 0x6C756361, 0x6D646961, 0x6D706E61, 0x6D707267,
    0x72656973, 0x73747261, 0x73747276,
};

static const DWORD s_adwSingleValueCodes[] = { 0x6E726570, 0x7265696E };

static BOOL RefMagicOpt_IsCodeIn(DWORD dwCode, const DWORD* pdwCode, int nCount)
{
    for (int i = 0; i < nCount; ++i)
    {
        if (pdwCode[i] == dwCode)
            return TRUE;
    }
    return FALSE;
}

#define REF_COUNT_OF(a) (int)(sizeof(a) / sizeof((a)[0]))

DWORD RefMagicOpt_IsKnownParamCode(DWORD dwCode)
{
    if (RefMagicOpt_IsCodeIn(dwCode, s_adwSixValueCodes, REF_COUNT_OF(s_adwSixValueCodes)) ||
        RefMagicOpt_IsCodeIn(dwCode, s_adwRangeCodes, REF_COUNT_OF(s_adwRangeCodes)) ||
        RefMagicOpt_IsCodeIn(dwCode, s_adwSingleValueCodes, REF_COUNT_OF(s_adwSingleValueCodes)))
        return 1;
    // The search leaves the code in EAX, except in the branch that subtracts its way through
    // 'jtni' and 'vtni'.
    if (dwCode > 0x696E7461 && dwCode < 0x6C756361)
        return dwCode - 0x696E7476;
    return dwCode;
}

void RefMagicOpt_LoadFromRow(RefMagicOptGroupByName& group, const RefMagicOpt& row)
{
    group.MOptName128.assign(row.MOptName128, 0, std::string::npos);
    group.dwParamCode = row.Param[0];
    group.bNoPlus = row.AttrType.find("+", 0, 1) == std::string::npos;
    group.bHasAt = row.AttrType.find("@", 0, 1) != std::string::npos;
    for (int i = 0; i < 6; ++i)
    {
        DWORD dwExcFunc = row.ExcFunc[i];
        if (!dwExcFunc)
            break;
        if (RefMagicOpt_IsKnownParamCode(dwExcFunc) != 1)
            BSAssertFail();
        group.vecExcFunc.push_back(dwExcFunc);
    }
    group.dwParam10 = row.Param[9];
}

void RefMagicOpt_BuildParamDesc(const RefMagicOpt& row, RefMagicOptLevel& level)
{
    level.ID = row.ID;
    level.MLevel = row.MLevel;
    level.dwProb = (DWORD)(__int64)(row.Prob * 1000000.0);
    level.Weight = row.Weight;
    level.pDesc = NULL;
    DWORD dwCode = row.Param[0];
    mfunc_desc* pDesc;
    if (RefMagicOpt_IsCodeIn(dwCode, s_adwSixValueCodes, REF_COUNT_OF(s_adwSixValueCodes)))
    {
        pDesc = new mfunc_6_values;
    }
    else if (RefMagicOpt_IsCodeIn(dwCode, s_adwRangeCodes, REF_COUNT_OF(s_adwRangeCodes)))
    {
        pDesc = new mfunc_single_range;
    }
    else if (RefMagicOpt_IsCodeIn(dwCode, s_adwSingleValueCodes,
                                  REF_COUNT_OF(s_adwSingleValueCodes)))
    {
        pDesc = new mfunc_single_value;
    }
    else
    {
        BSAssertFail();
        return;
    }
    level.pDesc = pDesc;
    if (!pDesc)
        BSAssertFail();
    level.pDesc->Init(row.Param);
    for (int i = 0; i < 10; ++i)
    {
        const std::string& strGroup = row.AvailItem[i].AvailItemGroup;
        if (strGroup.empty())
            break;
        if (RefData_StrEqualNoCase(strGroup.c_str(), s_szUnusedName))
            break;
        RefMagicOpt_ParseAvailItemType(strGroup.c_str(), level.vecAvailItem);
    }
}

// The AvailItemGroup names and the TypeIDs they stand for, in the order they are tested.
struct RefMagicOptItemTypeName
{
    const char*      pszName;
    WORD             awTypeID[6];
};

static const RefMagicOptItemTypeName s_aItemTypeName[] =
{
    { "weapon",            { 812 } },
    { "armor",             { 172, 300, 428, 1196, 1324, 1452 } },
    { "accessory",         { 684, 1580 } },
    { "sword",             { 4908, 15148 } },
    { "blade",             { 6956 } },
    { "spear",             { 9004 } },
    { "tblade",            { 11052 } },
    { "bow",               { 13100 } },
    { "helm",              { 2348, 3372 } },
    { "shoulderguard",     { 4396, 5420 } },
    { "mail",              { 6444, 7468 } },
    { "pants",             { 8492, 9516 } },
    { "gauntlet",          { 10540, 11564 } },
    { "boots",             { 12588, 13612 } },
    { "shield",            { 2604, 4652 } },
    { "earring",           { 2732, 3628 } },
    { "necklace",          { 4780, 5676 } },
    { "ring",              { 6828, 7724 } },
    { "trader",            { 2988 } },
    { "hunter",            { 7084 } },
    { "thief",             { 5036 } },
    { "event",             { 9900 } },
    { "avatar",            { 1708 } },
    { "avatar_helm",       { 3756 } },
    { "avatar_mail",       { 5804 } },
    { "avatar_mail_addon", { 7852 } },
    { "avatar_extra",      { 9900 } },
    { "avatar_special_armlet", { 3884 } },
};

void RefMagicOpt_ParseAvailItemType(const char* pszName,
                                    std::vector<RefMagicOptAvailItem>& vecItem)
{
    RefMagicOptAvailItem item;
    item.nKind = RefMagicOptAvailItem::KIND_TYPE_ID;
    item.nReqClass = 0;
    for (int i = 0; i < REF_COUNT_OF(s_aItemTypeName); ++i)
    {
        if (!RefData_StrEqualNoCase(s_aItemTypeName[i].pszName, pszName))
            continue;
        for (int j = 0; j < 6 && s_aItemTypeName[i].awTypeID[j]; ++j)
        {
            item.dwValue = s_aItemTypeName[i].awTypeID[j];
            vecItem.push_back(item);
        }
        return;
    }
    item.nKind = RefMagicOptAvailItem::KIND_OBJECT;
    RefObjCommonRow* pObj =
        CReferenceData::GetInstance()->FindRefObjByCodeName(std::string(pszName));
    item.dwValue = (DWORD)(DWORD_PTR)pObj;
    vecItem.push_back(item);
    if (!pObj)
        BSAssertFail();
}

BOOL RefMagicOpt_IsApplicableToItem(const RefMagicOptAvailItem& item,
                                    const RefObjCommonRow* pObj)
{
    if (item.nKind == -1)
        BSAssertFail();
    if (!pObj)
        BSAssertFail();
    WORD wObjType = RefObj_GetTypeID(pObj);
    if ((wObjType & 2) || (wObjType & 0x1C) != 0xC || (wObjType & 0x60) != 0x20)
        BSAssertFail();
    if (item.nKind == RefMagicOptAvailItem::KIND_OBJECT)
        return item.dwValue == (DWORD)(DWORD_PTR)pObj;
    if (item.nKind != RefMagicOptAvailItem::KIND_TYPE_ID)
    {
        BSAssertFail();
        return FALSE;
    }
    WORD wType = (WORD)item.dwValue;
    WORD wTID3 = (wType >> 7) & 0xF;
    WORD wObjTID3 = wObjType & 0x780;
    if (wTID3 == 2)
    {
        // Any weapon: TID3 1, 2, 3, 9, 10, 11 or 13.
        if (wObjTID3 != 0x80 && wObjTID3 != 0x100 && wObjTID3 != 0x180 && wObjTID3 != 0x480 &&
            wObjTID3 != 0x500 && wObjTID3 != 0x580 && wObjTID3 != 0x680)
            return FALSE;
    }
    else if (wTID3 != (wObjTID3 >> 7))
    {
        return FALSE;
    }
    WORD wTID4 = wType >> 11;
    if (wTID4 && wTID4 != (wObjType >> 11))
        return FALSE;
    return (int)RefObj_GetItemClass(pObj) >= item.nReqClass;
}

BOOL RefMagicOpt_CountApplicableParams(const RefMagicOptLevel& level,
                                       const RefObjCommonRow* pObj)
{
    for (unsigned int i = 0; i < level.vecAvailItem.size(); ++i)
    {
        if (RefMagicOpt_IsApplicableToItem(level.vecAvailItem.at(i), pObj) == TRUE)
            return TRUE;
    }
    return FALSE;
}

RefMagicOptLevel* MagOptEachLevel_FindByID(const RefMagicOptGroupByName* pGroup, DWORD dwMLevel)
{
    for (unsigned int i = 0; i < pGroup->vecLevel.size(); ++i)
    {
        RefMagicOptLevel* pLevel = pGroup->vecLevel[i];
        if (pLevel->MLevel == dwMLevel)
            return pLevel;
    }
    return NULL;
}

BOOL MagOptEachLevel_IsValueInRange(const RefMagicOptGroupByName* pGroup, DWORD dwValue,
                                    DWORD dwMLevel)
{
    RefMagicOptLevel* pLevel = MagOptEachLevel_FindByID(pGroup, dwMLevel);
    return pLevel && pLevel->pDesc->GetMin() <= dwValue && pLevel->pDesc->GetMax() >= dwValue;
}

RefMagicOptLevel* RefMagicOpt_FindLevelEntryById(WORD wID, const RefMagicOptGroupByName* pGroup)
{
    for (unsigned int i = 0; i < pGroup->vecLevel.size(); ++i)
    {
        RefMagicOptLevel* pLevel = pGroup->vecLevel[i];
        if (pLevel->ID == wID)
            return pLevel;
    }
    return NULL;
}

// Magic option tables --------------------------------------------------------------------------

void RefMagicOptTables::BuildMagicOptTable(CDBRecordList& listRows)
{
    for (CDBRecordList::iterator it = listRows.begin(); it != listRows.end(); ++it)
    {
        const RefMagicOpt& row = static_cast<CRefMagicOpt*>(*it)->m_Data;
        if (!row.Service)
            continue;
        RefMagicOptGroupByName* pGroup = FindMagicOptByCodeName(row.MOptName128);
        if (!pGroup)
        {
            pGroup = new RefMagicOptGroupByName;
            RefMagicOpt_LoadFromRow(*pGroup, row);
            m_vecMagicOptGroup.push_back(pGroup);
        }
        RefMagicOptLevel* pLevel = new RefMagicOptLevel;
        RefMagicOpt_BuildParamDesc(row, *pLevel);
        if (pLevel->pGroup)
            BSAssertFail();
        pLevel->pGroup = pGroup;
        pGroup->vecLevel.push_back(pLevel);
        // A second row of an ID asserts; it stays in its group but not in the map.
        if (m_mapMagicOptLevel.find(pLevel->ID) != m_mapMagicOptLevel.end())
            BSAssertFail();
        m_mapMagicOptLevel.insert(std::make_pair(pLevel->ID, pLevel));
        if (m_mapMagicOptLevel.size() >= 0xFFFF)
            BSAssertFail();
    }
}

RefMagicOptGroupByName* RefMagicOptTables::FindMagicOptByCodeName(const std::string& strName)
{
    if (strName.empty())
        BSAssertFail();
    for (unsigned int i = 0; i < m_vecMagicOptGroup.size(); ++i)
    {
        RefMagicOptGroupByName* pGroup = m_vecMagicOptGroup[i];
        if (!_stricmp(pGroup->MOptName128.c_str(), strName.c_str()))
            return pGroup;
    }
    return NULL;
}

RefMagicOptGroupByName* RefMagicOptTables::FindMagicOptById(DWORD dwParamCode)
{
    for (unsigned int i = 0; i < m_vecMagicOptGroup.size(); ++i)
    {
        RefMagicOptGroupByName* pGroup = m_vecMagicOptGroup[i];
        if (pGroup->dwParamCode == dwParamCode)
            return pGroup;
    }
    return NULL;
}

RefMagicOptLevel* RefMagicOptTables::FindMagicOptLevelEntry(WORD wID)
{
    std::map<WORD, RefMagicOptLevel*>::iterator it = m_mapMagicOptLevel.find(wID);
    if (it == m_mapMagicOptLevel.end())
        return NULL;
    return it->second;
}

mfunc_desc* RefMagicOptTables::FindMagicOption(WORD wID, DWORD* pdwMLevel)
{
    std::map<WORD, RefMagicOptLevel*>::iterator it = m_mapMagicOptLevel.find(wID);
    if (it == m_mapMagicOptLevel.end())
        return NULL;
    if (pdwMLevel)
        *pdwMLevel = it->second->MLevel;
    return it->second->pDesc;
}

MagOptEachLevelTable g_aMagOptEachLevelTable[MAGOPT_EACH_LEVEL_TABLE_COUNT];

void MagOptEachLevelSlots::AddUnique(RefMagicOptGroupByName* pGroup)
{
    std::vector<RefMagicOptGroupByName*>& vecGroup = avecGroup[pGroup->bNoPlus];
    for (unsigned int i = 0; i < vecGroup.size(); ++i)
    {
        if (vecGroup[i] == pGroup)
        {
            BSAssertFail();
            return;
        }
    }
    vecGroup.push_back(pGroup);
}

MagOptEachLevelSlots* MagOptEachLevelTable_GetOrInsert(MagOptEachLevelTable& table, DWORD dwKey)
{
    if (table.find(dwKey) != table.end())
        BSAssertFail();
    table.insert(std::make_pair(dwKey, MagOptEachLevelSlots()));
    if (table.find(dwKey) == table.end())
        BSAssertFail();
    return &table[dwKey];
}

MagOptEachLevelSlots* MagOptEachLevelTable_Find(MagOptEachLevelTable& table, DWORD dwKey)
{
    MagOptEachLevelTable::iterator it = table.find(dwKey);
    if (it == table.end())
        return NULL;
    return &it->second;
}

void RefMagicOptTables::BuildMagicOptAssignTables(CDBRecordList& listRows)
{
    for (CDBRecordList::iterator it = listRows.begin(); it != listRows.end(); ++it)
    {
        const RefMagicOptAssign& row = static_cast<CRefMagicOptAssign*>(*it)->m_Data;
        if (!row.Service)
            continue;
        // BUG(SM-012): a Race of 4 or more only asserts and indexes past the four tables.
        if (row.Race >= MAGOPT_EACH_LEVEL_TABLE_COUNT)
            BSAssertFail();
        MagOptEachLevelTable& table = g_aMagOptEachLevelTable[row.Race];
        if (!row.TID3 || row.TID3 >= 0x10)
            BSAssertFail();
        if (!row.TID4 || row.TID4 >= 0x20)
            BSAssertFail();
        MagOptEachLevelSlots* pSlots =
            MagOptEachLevelTable_GetOrInsert(table, row.TID3 | (row.TID4 << 16));
        for (int i = 0; i < 25; ++i)
        {
            const std::string& strName = row.AvailMOpt[i];
            if (strName.empty())
                break;
            if (RefData_StrEqualNoCase(strName.c_str(), s_szUnusedName))
                break;
            RefMagicOptGroupByName* pGroup = FindMagicOptByCodeName(strName);
            // An unknown name asserts and is then dereferenced.
            if (!pGroup)
                BSAssertFail();
            pSlots->AddUnique(pGroup);
        }
    }
}

MagOptEachLevelSlots* RefData_GetMagicOptAssignForItem(const RefObjCommonRow* pObj)
{
    if (!RefObj_IsEquipItem(pObj))
        return NULL;
    if (RefObj_GetCountry(pObj) >= MAGOPT_EACH_LEVEL_TABLE_COUNT)
        BSAssertFail();
    return MagOptEachLevelTable_Find(g_aMagOptEachLevelTable[RefObj_GetCountry(pObj)],
                                     RefObj_GetMagOptAssignKey(pObj));
}

BOOL RefData_FindAssignedMagicOptEntry(WORD wID, const RefObjCommonRow* pObj,
                                       RefMagicOptGroupByName*& pGroup,
                                       RefMagicOptLevel*& pLevel, DWORD& dwIndex)
{
    if (!pObj)
        BSAssertFail();
    DWORD dwKey = RefObj_GetMagOptAssignKey(pObj);
    if (RefObj_GetCountry(pObj) >= MAGOPT_EACH_LEVEL_TABLE_COUNT)
        BSAssertFail();
    MagOptEachLevelSlots* pSlots =
        MagOptEachLevelTable_Find(g_aMagOptEachLevelTable[RefObj_GetCountry(pObj)], dwKey);
    if (!pSlots)
        return FALSE;
    RefMagicOptGroupByName* pFound = NULL;
    RefMagicOptLevel* pFoundLevel = NULL;
    DWORD dwFound = 0;
    std::vector<RefMagicOptGroupByName*>& vecGroup = pSlots->avecGroup[0];
    for (unsigned int i = 0; i < vecGroup.size(); ++i)
    {
        pFound = vecGroup[i];
        pFoundLevel = RefMagicOpt_FindLevelEntryById(wID, pFound);
        if (pFoundLevel)
        {
            dwFound = i;
            break;
        }
        pFound = NULL;
    }
    pGroup = pFound;
    pLevel = pFoundLevel;
    dwIndex = dwFound;
    return pFound && pFoundLevel;
}

BOOL RefData_IsMagicOptEntryAllowed(const RefObjCommonRow* pObj, WORD wID)
{
    if (!pObj)
        BSAssertFail();
    if (!RefObj_IsEquipItem(pObj))
        return wID == 0;
    if (!RefData_GetMagicOptAssignForItem(pObj))
        return FALSE;
    RefMagicOptGroupByName* pGroup = NULL;
    RefMagicOptLevel* pLevel = NULL;
    DWORD dwIndex = 0;
    return RefData_FindAssignedMagicOptEntry(wID, pObj, pGroup, pLevel, dwIndex);
}

// Magic option groups and item option levels ---------------------------------------------------

DWORD RefRow_Read(CRefRow& row, RefMagicOptGroupRow& r)
{
    BYTE bService;
    DWORD dwKey;
    row >> bService >> dwKey >> r.Type >> r.CodeName >> r.MagicOptID >> r.MLevel >>
        r.CustomValue >> r.dw30 >> r.str34 >> r.dw50 >> r.str54;
    if (!bService)
        return 0;
    if (r.Type == 3)
    {
        if (!CReferenceData::GetInstance()->FindSkill(r.MagicOptID))
        {
            BSMessageBoxPrintf("Ooops! Can't find bound skill [%d]", r.MagicOptID);
            return 0;
        }
        return dwKey;
    }
    if (r.Type == 0 || r.Type > 3)
    {
        BSMessageBoxPrintf("can`t find MagicType[%d] extractor!!! [MagicOptID:%d]", r.Type,
                           r.MagicOptID);
        return 0;
    }
    if (!g_pReferenceData)
        BSAssertFail();
    RefMagicOptLevel* pLevel =
        g_pReferenceData->m_MagicOpt.FindMagicOptLevelEntry((WORD)r.MagicOptID);
    if (!pLevel)
    {
        // BUG(SM-013): the original passes the std::string object by value for %s (and the
        // MLevel lands in no specifier); the rewrite passes its characters and the MLevel.
        BSMessageBoxPrintf("can't find MagicOptGroup[%s]:MagicOptLevel[%d] from MagOptEachLevel "
                           "at RefMagicOptByItemOptLevel", r.CodeName.c_str(), r.MLevel);
        return 0;
    }
    if (r.CustomValue &&
        !MagOptEachLevel_IsValueInRange(pLevel->pGroup, r.CustomValue, pLevel->MLevel))
    {
        BSMessageBoxPrintf("CustomValue(RefMagicOptID[%d]) is error data out of range to "
                           "MagicoptionValue : value[%d]", r.MagicOptID, r.CustomValue);
        if (!BSAssertDialog(1566, s_szReferenceDataFile, "FALSE"))
            DebugBreak();
        return 0;
    }
    return dwKey;
}

BOOL CReferenceData::GetMagicOptGroup(DWORD dwKey, RefMagicOptGroupVector& vecRow)
{
    RefMagicOptGroupMap::iterator it = m_mapMagicOptGroup.find(dwKey);
    if (it == m_mapMagicOptGroup.end())
        return FALSE;
    vecRow = it->second;
    return TRUE;
}

unsigned __int64 RefAbilityByItemOptLevel::Read(CDBRecStream* pRow, CDBRecStream*)
{
    CRefRow row(pRow);
    BYTE bService;
    row >> bService >> ID >> RefItemID >> OptLevel;
    if (!bService)
        return 0;
    if (!g_pReferenceData)
        BSAssertFail();
    if (!g_pReferenceData->FindRefObj(RefItemID))
    {
        BSMessageBoxPrintf("Can't find Item(%d) within _RefObjCommon Table", RefItemID);
        BSAssertFail();
        return 0;
    }
    static_cast<CRefAbilityByItemOptLevel*>(pRow)->m_adw30[0] = ID;
    return ((unsigned __int64)RefItemID << 32) | OptLevel;
}

unsigned __int64 RefSkillByItemOptLevel::Read(CDBRecStream* pRow, CDBRecStream* pAbility)
{
    if (!pAbility)
    {
        BSAssertFail();
        return 0;
    }
    unsigned __int64 qwKey = RefAbilityByItemOptLevel::Read(pAbility, NULL);
    CRefRow row(pRow);
    row >> SkillID;
    if (!g_pReferenceData)
        BSAssertFail();
    RefSkill* pSkill = g_pReferenceData->FindSkill(SkillID);
    if (!pSkill)
    {
        BSMessageBoxPrintf("Can't find skill(%d) within _RefSkill Table", SkillID);
        BSAssertFail();
        return 0;
    }
    if (pSkill->Param[0] == 4)
    {
        BSMessageBoxPrintf("Can`t attach passive skill on _RefAbilityByItemOptLevel Table!");
        BSAssertFail();
        return 0;
    }
    return qwKey;
}

unsigned __int64 RefMagicOptByItemOptLevel::Read(CDBRecStream* pRow, CDBRecStream* pAbility)
{
    if (!pAbility)
    {
        BSAssertFail();
        return 0;
    }
    unsigned __int64 qwKey = RefAbilityByItemOptLevel::Read(pAbility, NULL);
    CRefRow row(pRow);
    row >> MagicOptID >> Value;
    row.Skip().Skip();
    if (!g_pReferenceData)
        BSAssertFail();
    RefObjCommonRow* pObj = g_pReferenceData->FindRefObj(RefItemID);
    if (!pObj)
        return 0;
    if (!g_pReferenceData)
        BSAssertFail();
    RefMagicOptLevel* pLevel = g_pReferenceData->m_MagicOpt.FindMagicOptLevelEntry(MagicOptID);
    if (!pLevel)
    {
        BSMessageBoxPrintf("can't find %d from MagOptEachLevel at RefMagicOptByItemOptLevel",
                           MagicOptID);
        return 0;
    }
    if (!MagOptEachLevel_IsValueInRange(pLevel->pGroup, Value, pLevel->MLevel))
    {
        BSMessageBoxPrintf("(%s) error data out of range to MagicoptionValue",
                           RefObj_GetCodeName(pObj).c_str());
        if (!BSAssertDialog(4730, s_szReferenceDataFile, "FALSE"))
            DebugBreak();
        return 0;
    }
    return qwKey;
}

CDBRecStream* CReferenceData::FindRowInTable(unsigned int nSlot, DWORD dwKey)
{
    RefTableEntry* pTable = m_pTables->at(nSlot);
    if (!pTable)
        BSAssertFail();
    for (CDBRecordList::iterator it = pTable->m_listRows.begin();
         it != pTable->m_listRows.end(); ++it)
    {
        CDBRecStream* pRow = static_cast<CDBRecStream*>(*it);
        switch (nSlot)
        {
        case 3:
        case 17:
            if (*(const DWORD*)((const BYTE*)pRow + 0x30) == dwKey)
                return pRow;
            break;
        case 12:
        case 13:
        case 14:
        {
            // The first column, read past the reader's position (which is restored).
            DWORD nReadPos = pRow->m_nReadPos;
            DWORD nFieldCount = pRow->m_nFieldCount;
            pRow->m_nFieldCount = 0;
            pRow->m_nReadPos = 0;
            if ((int)pRow->m_nWritePos < 4)
                BSAssertFail();
            DWORD dwFirst = *(const DWORD*)(pRow->m_pBuffer + pRow->m_nReadPos);
            pRow->m_nReadPos = nReadPos;
            pRow->m_nFieldCount = nFieldCount;
            if (dwFirst == dwKey)
                return pRow;
            break;
        }
        default:
            BSAssertFail();
            break;
        }
    }
    return NULL;
}

// CRefInstanceGenerator::CreateByItemOptLevelRecord (0x4a5e70): one row of slot 15 (magic
// option), 16 (skill) or 17 (ability).
static int RefData_CreateByItemOptLevelRecord(CDBRecStream* pRow,
                                              CReferenceData::RefItemOptLevelMap& map,
                                              unsigned int nSlot)
{
    RefAbilityByItemOptLevel* pObj;
    switch (nSlot)
    {
    case 15:
        pObj = new RefMagicOptByItemOptLevel;
        break;
    case 16:
        pObj = new RefSkillByItemOptLevel;
        break;
    case 17:
    {
        // The ability rows only check their keys (the map is dropped by the caller).
        RefAbilityByItemOptLevel ability;
        unsigned __int64 qwKey = ability.Read(pRow, NULL);
        if (!qwKey)
            return 1;
        if (map.find(qwKey) != map.end())
        {
            BSAssertFail();
            return 0;
        }
        map.insert(std::make_pair(qwKey, CReferenceData::RefItemOptLevelVector()));
        return 1;
    }
    default:
        BSAssertFail();
        BSAssertFail();
        return 0;
    }
    if (!pObj)
    {
        BSAssertFail();
        return 0;
    }
    DWORD dwAbilityID;
    CRefRow row(pRow);
    row >> dwAbilityID;
    if (!g_pReferenceData)
        BSAssertFail();
    CDBRecStream* pAbility = g_pReferenceData->FindRowInTable(0x11, dwAbilityID);
    if (!pAbility)
    {
        BSAssertFail();
        delete pObj;
        return 0;
    }
    pAbility->m_nFieldCount = 0;
    pAbility->m_nReadPos = 0;
    unsigned __int64 qwKey = pObj->Read(pRow, pAbility);
    if (!qwKey)
    {
        BSAssertFail();
        delete pObj;
        return 1;
    }
    CReferenceData::RefItemOptLevelMap::iterator it = map.find(qwKey);
    if (it == map.end())
    {
        CReferenceData::RefItemOptLevelVector vecRow;
        vecRow.push_back(pObj);
        map.insert(std::make_pair(qwKey, vecRow));
    }
    else
    {
        it->second.push_back(pObj);
    }
    return 1;
}

// RefData_BuildRefItemOptLevelTables (0x4d13c0): FALSE for an empty table or a row that fails.
static int RefData_BuildRefItemOptLevelTables(RefTableEntry* pTable,
                                              CReferenceData::RefItemOptLevelMap& map,
                                              unsigned int nSlot)
{
    if (pTable->m_listRows.size() == 0)
        return 0;
    for (CDBRecordList::iterator it = pTable->m_listRows.begin();
         it != pTable->m_listRows.end(); ++it)
    {
        if (!RefData_CreateByItemOptLevelRecord(static_cast<CDBRecStream*>(*it), map, nSlot))
        {
            BSAssertFail();
            return 0;
        }
    }
    return 1;
}

static void RefData_InsertItemOptLevelEnds(CReferenceData::RefItemOptLevelMap& map)
{
    map.insert(std::make_pair((unsigned __int64)0, CReferenceData::RefItemOptLevelVector()));
    map.insert(std::make_pair((unsigned __int64)-1, CReferenceData::RefItemOptLevelVector()));
}

int CReferenceData::LoadByItemOptLevelTables()
{
    RefTableEntry* pAbility = m_pTables->at(0x11);
    RefTableEntry* pMagicOpt = m_pTables->at(0xF);
    RefTableEntry* pSkill = m_pTables->at(0x10);
    RefItemOptLevelMap mapAbility;
    if (!RefData_BuildRefItemOptLevelTables(pAbility, mapAbility, 17))
        return 0;
    RefData_InsertItemOptLevelEnds(m_mapItemOptLevel134);
    if (!RefData_BuildRefItemOptLevelTables(pMagicOpt, m_mapItemOptLevel134, 15))
        return 0;
    RefData_InsertItemOptLevelEnds(m_mapItemOptLevel128);
    if (!RefData_BuildRefItemOptLevelTables(pSkill, m_mapItemOptLevel128, 16))
        return 0;
    return 1;
}

// CReferenceData ------------------------------------------------------------------------------

CReferenceDataInstance::CReferenceDataInstance()
{
    if (g_pReferenceData)
        BSAssertFail();
    g_pReferenceData = static_cast<CReferenceData*>(this);
}

CReferenceDataInstance::~CReferenceDataInstance()
{
    g_pReferenceData = NULL;
}

CReferenceData::CReferenceData()
{
    m_pTables = NULL;
}

// The containers below own their rows only here: everything else is either freed by its member
// destructor (CRefOwnerMap) or left to the end of the process.
CReferenceData::~CReferenceData()
{
    for (unsigned int i = 0; i < m_vecLevel.size(); ++i)
    {
        if (m_vecLevel[i])
            delete m_vecLevel[i];
    }
    m_vecLevel.clear();
    RefData_DeleteValues(m_mapConditionToBuyScrapItem);
    RefData_DeleteValues(m_mapRewardPolicyToSellScrapItem);
    RefData_DeleteValues(m_mapConditionToSellScrapItem);
    RefData_DeleteValues(m_mapRewardPolicyToBuyScrapItem);
    RefData_DeleteElements(m_vecMappingShopGroup);
    RefData_DeleteElements(m_vecMappingShopWithTab);
    RefData_DeleteValues(m_mapShopGroupByCodeName);
    RefData_DeleteValues(m_mapShopByCodeName);
    RefData_DeleteValues(m_mapPackageItemByCodeName);
    RefData_DeleteValues(m_mapShopTabByCodeName);
    RefData_DeleteValues(m_mapShopTabGroupByCodeName);
    m_set22C.clear();
    for (RefSiegeLvlSummonMonsterMap::iterator it = m_mapSiegeLvlSummonMonster.begin();
         it != m_mapSiegeLvlSummonMonster.end(); ++it)
    {
        RefData_DeleteElements(*it->second);
        delete it->second;
    }
    m_mapSiegeLvlSummonMonster.clear();
    RefData_DeleteValues(m_mmapSiegeFortressRewards);
    RefData_DeleteValues(m_mapSiegeBlessBuff);
    RefData_DeleteValues(m_mapSiegeDungeon);
    RefData_DeleteValues(m_mmapSiegeQuest);
    RefData_DeleteValues(m_mmapSiegeQuestReward);
    // The item option level rows are freed and their slots cleared; the maps keep the slots.
    RefItemOptLevelMap* apItemOptLevel[2] = { &m_mapItemOptLevel128, &m_mapItemOptLevel134 };
    for (int nMap = 0; nMap < 2; ++nMap)
    {
        for (RefItemOptLevelMap::iterator it = apItemOptLevel[nMap]->begin();
             it != apItemOptLevel[nMap]->end(); ++it)
        {
            for (RefItemOptLevelVector::iterator itRow = it->second.begin();
                 itRow != it->second.end(); ++itRow)
            {
                if (*itRow)
                {
                    delete *itRow;
                    *itRow = NULL;
                }
            }
        }
    }
    // Each map of positions is copied and the rows are freed through the copy.
    for (unsigned int nWorld = 0; nWorld < m_vecGameWorldPosition.size(); ++nWorld)
    {
        RefGameWorldPositionMap mapPosition = m_vecGameWorldPosition[nWorld];
        for (RefGameWorldPositionMap::iterator it = mapPosition.begin(); it != mapPosition.end();
             ++it)
        {
            if (it->second)
                delete it->second;
        }
        mapPosition.clear();
    }
    m_vecGameWorldPosition.clear();
    if (!m_mapServerEvent.empty())
        RefData_DeleteValues(m_mapServerEvent);
    if (!m_mapServerEventReward.empty())
        RefData_DeleteValues(m_mapServerEventReward);
    m_mmapServerEventRewardID.clear();
    RefData_DeleteValues(m_mapQuestReward);
    for (RefQuestRewardItemsMap::iterator it = m_mapQuestRewardItems.begin();
         it != m_mapQuestRewardItems.end(); ++it)
    {
        RefQuestRewardItemGroupMap* pGroups = it->second;
        if (!pGroups)
            continue;
        for (RefQuestRewardItemGroupMap::iterator itGroup = pGroups->begin();
             itGroup != pGroups->end(); ++itGroup)
        {
            if (itGroup->second)
            {
                RefData_DeleteValues(*itGroup->second);
                delete itGroup->second;
            }
        }
        delete pGroups;
    }
    m_mapQuestRewardItems.clear();
    RefData_DeleteValues(m_mapEventReward);
    for (RefEventRewardItemsMap::iterator it = m_mapEventRewardItems.begin();
         it != m_mapEventRewardItems.end(); ++it)
    {
        if (it->second)
        {
            RefData_DeleteValues(*it->second);
            delete it->second;
        }
    }
    m_mapEventRewardItems.clear();
    ClearCollectionBook();
}

void CReferenceData::ClearCollectionBook()
{
    for (RefCollectionBookItemMap::iterator it = m_mapCollectionBookItem.begin();
         it != m_mapCollectionBookItem.end(); ++it)
    {
        if (it->second)
            delete it->second;
    }
    for (RefCollectionBookThemeMap::iterator it = m_mapCollectionBookTheme.begin();
         it != m_mapCollectionBookTheme.end(); ++it)
    {
        if (it->second)
            delete it->second;
    }
    m_mapCollectionBookItem.clear();
    m_mapCollectionBookTheme.clear();
    m_mapCollectionBookThemeByID.clear();
}

CReferenceData* CReferenceData::GetInstance()
{
    if (!g_pReferenceData)
        BSAssertFail();
    return g_pReferenceData;
}

BOOL CReferenceData::BuildAllTables()
{
    if (!m_pTables)
        BSAssertFail();
    for (unsigned int i = 0; i < m_pTables->size(); ++i)
    {
        RefTableEntry* pTable = m_pTables->at(i);
        if (!pTable)
            continue;
        if (pTable->m_listRows.size() == 0)
        {
            LogPrintf(0x2000000, "!!! there is no record in this[%s] table !!!",
                      pTable->m_pTable->m_pInfo->m_strTable.c_str());
            continue;
        }
        if (!BuildFromTable(pTable, i))
        {
            BSMessageBoxPrintf("!!! failed to build reference data from this[%s] table",
                               pTable->m_pTable->m_pInfo->m_strTable.c_str());
            return FALSE;
        }
    }
    return TRUE;
}

int CReferenceData::BuildFromTable(RefTableEntry* pTable, unsigned int nSlot)
{
    if (nSlot >= REF_TABLE_SLOT_COUNT)
    {
        BSAssertFail();
        return 0;
    }
    return s_apfnBuild[nSlot](this, pTable, nSlot);
}

RefObjCommonRow* CReferenceData::FindRefObj(int nRefObjID)
{
    RefObjHashMap::iterator it = m_hashRefObj.find(nRefObjID);
    if (it == m_hashRefObj.end())
        return NULL;
    return it->second;
}

// The value of a key, NULL when the key is not there.
template <class TMap, class TKey>
static typename TMap::mapped_type RefData_FindValue(TMap& map, const TKey& key)
{
    typename TMap::iterator it = map.find(key);
    if (it == map.end())
        return NULL;
    return it->second;
}

RefRegionRow* CReferenceData::FindRegion(DWORD dwID)
{
    return RefData_FindValue(m_mapRegion, dwID);
}

RefGameWorld* CReferenceData::FindGameWorld(DWORD dwID)
{
    return RefData_FindValue(m_mapGameWorld, dwID);
}

RefObjCommonRow* CReferenceData::FindRefObjByCodeName(const std::string& strCodeName)
{
    return RefData_FindValue(m_hashRefObjByCodeName, strCodeName);
}

RefSkillMasteryRow* CReferenceData::FindSkillMastery(DWORD dwID)
{
    return RefData_FindValue(m_mapSkillMastery, dwID);
}

RefSkill* CReferenceData::FindSkill(DWORD dwID)
{
    return RefData_FindValue(m_mapSkill, dwID);
}

RefGameWorld* CReferenceData::FindGameWorldByCodeName(const std::string& strCodeName)
{
    return RefData_FindValue(m_hashGameWorldByCodeName, strCodeName);
}

RefServerEvent* CReferenceData::FindServerEvent(DWORD dwID)
{
    return RefData_FindValue(m_mapServerEvent, dwID);
}

RefServerEventReward* CReferenceData::FindServerEventReward(DWORD dwID)
{
    return RefData_FindValue(m_mapServerEventReward, dwID);
}

BOOL CReferenceData::GetShardContentConfigInt32(const std::string& strCodeName,
                                                const std::string& strDesc,
                                                std::vector<int>& vecValue)
{
    RefShardContentConfigMap::iterator itEnd = m_mapShardContentConfig.upper_bound(strCodeName);
    for (RefShardContentConfigMap::iterator it = m_mapShardContentConfig.lower_bound(strCodeName);
         it != itEnd; ++it)
    {
        RefShardContentConfig* pConfig = it->second;
        const char* pszDesc = pConfig->CodeDesc128.c_str();
        const char* pszWanted = strDesc.c_str();
        if (CompareStringA(0x400, 0, pszDesc, lstrlenA(pszDesc), pszWanted, lstrlenA(pszWanted)) !=
            CSTR_EQUAL)
            continue;
        const char* pszType = pConfig->Type.c_str();
        if (CompareStringA(0x400, 0, pszType, lstrlenA(pszType), "INT32", lstrlenA("INT32")) ==
            CSTR_EQUAL)
            vecValue.push_back(atol(pConfig->Value.c_str()));
    }
    return !vecValue.empty();
}

// The lookup the instance records use (InstanceRecords.h): the singleton check of the callers,
// then RefData_HashFindAt108.
RefObjCommonRow* ReferenceData_FindRefObj(int nRefObjID)
{
    return CReferenceData::GetInstance()->FindRefObj(nRefObjID);
}

// ---------------------------------------------------------------------------------------------
// Object type helpers and lookups

static BOOL RefItem_IsWeaponType(WORD wTypeID)
{
    return (wTypeID & 2) == 0 && (wTypeID & 0x1C) == 0xC && (wTypeID & 0x60) == 0x20 &&
           (wTypeID & 0x780) == 0x300;
}

BOOL RefItem_IsStaffWeaponType(const WORD& wTypeID)
{
    if (!RefItem_IsWeaponType(wTypeID))
        return FALSE;
    WORD wTID4 = wTypeID >> 11;
    return wTID4 == 15 || wTID4 == 11 || wTID4 == 10;
}

BOOL RefItem_IsSwordWeaponType(const WORD& wTypeID)
{
    if (!RefItem_IsWeaponType(wTypeID))
        return FALSE;
    WORD wTID4 = wTypeID >> 11;
    return wTID4 == 2 || wTID4 == 3 || wTID4 == 7 || wTID4 == 8;
}

BOOL RefObj_TypeIdIsClass0C(const WORD& wTypeID)
{
    return (wTypeID & 2) == 0 && (wTypeID & 0x1C) == 0xC;
}

static void RefObj_SetBits(DWORD& dw, DWORD dwValue, int nShift, DWORD dwMask)
{
    dw = (dw & ~dwMask) | ((dwValue << nShift) & dwMask);
}

void RefObj_PackTypeID(DWORD& dwPacked, const WORD* pawValue)
{
    RefObj_SetBits(dwPacked, (DWORD)pawValue[0] - 16, 0, 0x3F);
    RefObj_SetBits(dwPacked, pawValue[1], 6, 0x3C0);
    RefObj_SetBits(dwPacked, pawValue[2], 10, 0x7C00);
    RefObj_SetBits(dwPacked, pawValue[3], 15, 0xF8000);
    RefObj_SetBits(dwPacked, pawValue[4], 20, 0x3F00000);
    RefObj_SetBits(dwPacked, pawValue[5], 26, 0xFC000000);
}

const char* RefData_GetObjectCodeName(int nRefObjID)
{
    RefObjCommonRow* pObj = CReferenceData::GetInstance()->FindRefObj(nRefObjID);
    if (!pObj)
        return "Unknown Object";
    return RefObj_GetCodeName(pObj).c_str();
}

const char* RefData_GetRegionName(WORD wRegionID)
{
    RefRegionRow* pRegion = CReferenceData::GetInstance()->FindRegion(wRegionID);
    if (!pRegion)
        return "Unknown Region";
    return pRegion->AreaName.c_str();
}
