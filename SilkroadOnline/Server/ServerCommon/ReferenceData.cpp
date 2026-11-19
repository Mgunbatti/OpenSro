#include "ServerCommon/ReferenceData.h"

#include <string.h>

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
    P, P, P, P, P, P, P, RefData_BuildMagicOptPending,                      // 0x00
    P, P, P, P, P, P, P, P,                                                 // 0x08
    P, P, P, P, P, P, P, P,                                                 // 0x10
    P, P, P, P, P, P, P, P,                                                 // 0x18
    P, P, P, P, P, P, P, P,                                                 // 0x20
    P, P, P, P, P, P, P, P,                                                 // 0x28
    P, P, P, P, P, P, P, P,                                                 // 0x30
    P, P, P, P, P, P, P, P,                                                 // 0x38
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
    P, P, P, P, P,                                                          // 0x53
    P, P,                                                                   // 0x58
    REF_KEYED(RefShardContentConfig, RefShardContentConfigMap, m_mapShardContentConfig,
              std::string, CodeName128),                                    // 0x5A
    P,                                                                      // 0x5B
    REF_LOADED(RefData_LoadGameWorldNPC),
    REF_KEYED(RefObjCharExtraSkill, RefObjCharExtraSkillMap, m_mapObjCharExtraSkill, int,
              RefObjID),
    REF_KEYED(RefGameWorldConfig, RefGameWorldConfigMap, m_mapGameWorldConfig, std::string,
              GroupCodeName128),                                            // 0x5E
    RefData_BuildAlchemyMeritPending,
    P, P, P, P, P, P, P, P,                                                 // 0x60
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

CReferenceData::CReferenceData()
    : m_pTables(NULL)
{
    g_pReferenceData = this;
}

template <class TVector>
static void RefData_DeleteElements(TVector& vec)
{
    for (typename TVector::iterator it = vec.begin(); it != vec.end(); ++it)
        delete *it;
    vec.clear();
}

CReferenceData::~CReferenceData()
{
    RefData_DeleteElements(m_vecSiegeFortress);
    RefData_DeleteValues(m_mmapSiegeFortressGuard);
    RefData_DeleteValues(m_mmapSiegeFortressItemForge);
    RefData_DeleteElements(m_vecSiegeFortressBattleRank);
    RefData_DeleteElements(m_vecSiegeStructUpgrade);
    for (RefSiegeLvlSummonMonsterMap::iterator it = m_mapSiegeLvlSummonMonster.begin();
         it != m_mapSiegeLvlSummonMonster.end(); ++it)
        RefData_DeleteElements(*it->second);
    RefData_DeleteValues(m_mapSiegeLvlSummonMonster);
    RefData_DeleteValues(m_mmapSiegeFortressRewards);
    RefData_DeleteValues(m_mapSiegeBlessBuff);
    RefData_DeleteValues(m_mapSiegeDungeon);
    RefData_DeleteValues(m_mmapSiegeQuest);
    RefData_DeleteValues(m_mmapSiegeQuestReward);
    m_mapGameWorldNPC.clear();
    RefData_DeleteValues(m_mapObjCharExtraSkill);
    RefData_DeleteValues(m_mapGameWorldConfig);
    RefData_DeleteValues(m_mapShardContentConfig);
    RefData_DeleteValues(m_mapUIString);
    RefData_DeleteValues(m_mmapGameWorldBindTriggerCategory);
    RefData_DeleteValues(m_mapTriggerCategory);
    RefData_DeleteValues(m_mmapTriggerCategoryBindTrigger);
    RefData_DeleteValues(m_mapTrigger);
    RefData_DeleteValues(m_mmapTriggerVariable);
    RefData_DeleteValues(m_mmapTriggerBindEvent);
    RefData_DeleteValues(m_mmapTriggerBindCondition);
    RefData_DeleteValues(m_mmapTriggerBindAction);
    RefData_DeleteValues(m_mapTriggerCommon);
    RefData_DeleteValues(m_mapTriggerEvent);
    RefData_DeleteValues(m_mapTriggerCondition);
    RefData_DeleteValues(m_mapTriggerAction);
    RefData_DeleteValues(m_mapTriggerConditionParam);
    RefData_DeleteValues(m_mapTriggerActionParam);
    RefData_DeleteValues(m_mmapGameWorldBindGameWorldGroup);
    RefData_DeleteValues(m_mapGameWorldGroup);
    RefData_DeleteValues(m_mapGameWorldGroupConfig);
    if (g_pReferenceData == this)
        g_pReferenceData = NULL;
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

// The lookup the instance records use (InstanceRecords.h): the singleton check of the callers,
// then RefData_HashFindAt108.
RefObjCommonRow* ReferenceData_FindRefObj(int nRefObjID)
{
    return CReferenceData::GetInstance()->FindRefObj(nRefObjID);
}
