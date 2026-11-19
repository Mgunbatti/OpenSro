#include "ServerCommon/ReferenceData.h"

#include <string.h>
#include <time.h>

#include "BSLib/BSDebug.h"
#include "ServerCommon/InstanceRecords.h"
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

// Slot 7 (_RefMagicOpt): the column check of the dispatcher; the magic option table itself
// (CRefInstanceGenerator::BuildMagicOptTable 0x51a780) is not ported yet.
static int RefData_BuildMagicOptPending(CReferenceData*, RefTableEntry* pTable, unsigned int)
{
    if (pTable->m_listRows.size() < 0x44)
    {
        BSMessageBoxPrintf("ssiba crazy DOWOOL copy newest magic option table!!!!");
        LogPrintf(0x2000000, "ssiba crazy DOWOOL copy newest magic option table!!!!");
    }
    return 1;
}

// Slot 95 (_RefAlchemyMerit): the empty-table warning of the dispatcher (typo kept); the
// positions (CRefInstanceGenerator::LoadGameWorldPositions 0x4adb60) are not ported yet.
static int RefData_BuildAlchemyMeritPending(CReferenceData*, RefTableEntry* pTable, unsigned int)
{
    if (pTable->m_listRows.size() == 0)
        BSMessageBoxPrintf("!!warning!! _RefAlchemyMerit Empty!! 'EquipReslove' will be fail!!");
    return 1;
}

#define P RefData_BuildPending

// The switch of CRefInstanceGenerator::BuildFromTable (0x4ac550), one entry per slot.
static const PFN_REF_BUILD s_apfnBuild[REF_TABLE_SLOT_COUNT] =
{
    &RefData_BuildCopyVector<CRefLevel, RefLevelData, &CReferenceData::m_vecLevel>,  // 0x00
    REF_LOADED((RefData_AddRowUnique<CRefClimate, RefClimateData, CReferenceData::RefClimateMap,
                &CReferenceData::m_mapClimate>)),                           // 0x01
    P, P, P, P,
    REF_LOADED((RefData_AddRowUnique<CRefGachaNpcMap, RefGachaNpcMapData,
                CReferenceData::RefGachaNpcMapMap,
                &CReferenceData::m_mapGachaNpcMap>)),                       // 0x06
    RefData_BuildMagicOptPending,
    P, P, P, P, P, P, P, P,                                                 // 0x08
    P, P, P, P, P, P, P, P,                                                 // 0x10
    P, P, P, P, P, P, P, P,                                                 // 0x18
    P, P, P, P, P, P, P, P,                                                 // 0x20
    P, P, P, P, P, P, P, P,                                                 // 0x28
    P, P, P, P, P, P, P, P,                                                 // 0x30
    P,                                                                      // 0x38
    REF_KEYED(RefAISkill, RefAISkillMap, m_mmapAISkill, DWORD, TacticsID),  // 0x39
    P, P,
    REF_LOADED((RefData_AddRowUnique<CRefQuest, RefQuest, CReferenceData::RefQuestMap,
                &CReferenceData::m_mapQuest>)),                             // 0x3C
    RefData_BuildGameWorld,
    &RefData_BuildCopyVector<CRefInstanceWorldRegion, RefInstanceWorldRegionData,
                             &CReferenceData::m_vecInstanceWorldRegion>,    // 0x3E
    &RefData_BuildCopyVector<CRefInstanceWorldStartPos, RefInstanceWorldStartPosData,
                             &CReferenceData::m_vecInstanceWorldStartPos>,
    P, P, P, P, P, P, P, P,                                                 // 0x40
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
    P, P,                                                                   // 0x53
    REF_LOADED(RefData_LoadRentItem),                                       // 0x55
    P, P,
    P,                                                                      // 0x58
    REF_LOADED((RefData_AddRowUniqueStream<RefOptionalTeleport,
                CReferenceData::RefOptionalTeleportMap,
                &CReferenceData::m_mapOptionalTeleport>)),                  // 0x59
    REF_KEYED(RefShardContentConfig, RefShardContentConfigMap, m_mapShardContentConfig,
              std::string, CodeName128),                                    // 0x5A
    P,                                                                      // 0x5B
    REF_LOADED(RefData_LoadGameWorldNPC),
    REF_KEYED(RefObjCharExtraSkill, RefObjCharExtraSkillMap, m_mapObjCharExtraSkill, int,
              RefObjID),
    REF_KEYED(RefGameWorldConfig, RefGameWorldConfigMap, m_mapGameWorldConfig, std::string,
              GroupCodeName128),                                            // 0x5E
    RefData_BuildAlchemyMeritPending,
    REF_LOADED(RefData_LoadServerEvent),                                    // 0x60
    RefData_BuildServerEventReward,
    RefData_BuildServerEventReward,
    RefData_BuildServerEventReward,
    REF_LOADED(RefData_LoadQuestReward),                                    // 0x64
    P, P, P,
    P, P,                                                                   // 0x68
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
    return reinterpret_cast<const std::string*>(reinterpret_cast<const BYTE*>(pRegion) + 0x34)
        ->c_str();
}
