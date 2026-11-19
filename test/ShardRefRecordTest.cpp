// SR_ShardManager reference record classes (SilkroadOnline\Server\ServerCommon\RefRecords.h):
// record sizes and row layouts, the table descriptions, the runtime classes and the August 2011
// record slots that store fetched columns. Compiled with SHARD_DEFS.

#include "ServerCommon/RefRecords.h"

#include <new>
#include <string.h>

#include "TestCheck.h"

// ---------------------------------------------------------------------------------------------
// Row layouts: the offsets CDBTable::DescribeColumns gives the columns of a table (natural
// alignment after the previous column) must be the member offsets minus m_nDataOffset.

struct COLUMN_DEF
{
    int nType;
    int nMemberOffset;
};

static bool CheckLayout(const COLUMN_DEF* pColumns, int nCount, int nDataOffset)
{
    int nPrevOffset = 0;
    int nPrevSize = 0;
    for (int i = 0; i < nCount; ++i)
    {
        int nAlign = DB_ColumnTypeAlignment(pColumns[i].nType);
        int nOffset = nAlign * ((nAlign + nPrevSize + nPrevOffset - 1) / nAlign);
        if (nOffset != pColumns[i].nMemberOffset - nDataOffset)
        {
            printf("column %d: bound at %d, member at %d\n", i, nOffset,
                   pColumns[i].nMemberOffset - nDataOffset);
            return false;
        }
        nPrevSize = DB_ColumnTypeSize(pColumns[i].nType);
        nPrevOffset = nOffset;
    }
    return true;
}

#define COL(type, cls, member) { type, FIELD_OFFSET(cls, member) }
#define DATA_COL(type, cls, member) { type, FIELD_OFFSET(cls, m_Data) + FIELD_OFFSET(\
    cls##_DATA, member) }

typedef RefSkill CRefSkill_DATA;
typedef RefSiegeFortress CRefSiegeFortress_DATA;
typedef RefMagicOpt CRefMagicOpt_DATA;

static void TestLayouts()
{
    CHECK(sizeof(CRefLevel) == 0x48 && sizeof(CRefSkill) == 0x248);
    CHECK(sizeof(CRefMagicOpt) == 0x200 && sizeof(CRefMagicOptAssign) == 0x2E0);
    CHECK(sizeof(CRefQuest) == 0x108 && sizeof(CRefSiegeFortress) == 0xD8);
    CHECK(sizeof(CRefShop) == 0x30 && sizeof(CRefUIString) == 0xA8);
    CHECK(sizeof(CRefTrigger) == 0x78 && sizeof(CRefTriggerBindEvent) == 0x40);

    // _RefLevel
    static const COLUMN_DEF level[] =
    {
        COL(DBTYPE_BYTE, CRefLevel, Lvl), COL(DBTYPE_INT64, CRefLevel, Exp_C),
        COL(DBTYPE_INT, CRefLevel, Exp_M), COL(DBTYPE_INT, CRefLevel, Cost_M),
        COL(DBTYPE_INT, CRefLevel, Cost_ST), COL(DBTYPE_INT, CRefLevel, GUST_Mob_Exp),
        COL(DBTYPE_INT, CRefLevel, JobExp_Trader), COL(DBTYPE_INT, CRefLevel, JobExp_Robber),
        COL(DBTYPE_INT, CRefLevel, JobExp_Hunter),
    };
    CHECK(CheckLayout(level, sizeof(level) / sizeof(level[0]), 24));

    // _RefSiegeBlessBuff (a bigint after three ints)
    static const COLUMN_DEF bless[] =
    {
        COL(DBTYPE_BYTE, CRefSiegeBlessBuff, Service),
        COL(DBTYPE_INT, CRefSiegeBlessBuff, BlessID),
        COL(DBTYPE_INT, CRefSiegeBlessBuff, FortressID),
        COL(DBTYPE_INT, CRefSiegeBlessBuff, RefBlessBuffID),
        COL(DBTYPE_INT64, CRefSiegeBlessBuff, NeedGold),
        COL(DBTYPE_INT, CRefSiegeBlessBuff, NeedGP),
    };
    CHECK(CheckLayout(bless, sizeof(bless) / sizeof(bless[0]), 24));

    // _RefSiegeFortress
    static const COLUMN_DEF fortress[] =
    {
        DATA_COL(DBTYPE_BYTE, CRefSiegeFortress, Service),
        DATA_COL(DBTYPE_INT, CRefSiegeFortress, FortressID),
        DATA_COL(DBTYPE_STRING, CRefSiegeFortress, CodeName128),
        DATA_COL(DBTYPE_STRING, CRefSiegeFortress, Name),
        DATA_COL(DBTYPE_STRING, CRefSiegeFortress, NameID128),
        DATA_COL(DBTYPE_STRING, CRefSiegeFortress, LinkedTeleportCodeName),
        DATA_COL(DBTYPE_BYTE, CRefSiegeFortress, Scale),
        DATA_COL(DBTYPE_SHORT, CRefSiegeFortress, MaxAdmission),
        DATA_COL(DBTYPE_BYTE, CRefSiegeFortress, MaxGuard),
        DATA_COL(DBTYPE_BYTE, CRefSiegeFortress, MaxBarricade),
        DATA_COL(DBTYPE_SHORT, CRefSiegeFortress, TaxTargets),
        DATA_COL(DBTYPE_INT, CRefSiegeFortress, RequestFee),
        DATA_COL(DBTYPE_STRING, CRefSiegeFortress, CrestPath128),
        DATA_COL(DBTYPE_STRING, CRefSiegeFortress, RequestNPCName128),
    };
    CHECK(CheckLayout(fortress, sizeof(fortress) / sizeof(fortress[0]), 24));

    // the start of _RefSkill up to the first strings and the tail (RefSkill::CopyFrom offsets)
    static const COLUMN_DEF skill[] =
    {
        DATA_COL(DBTYPE_BYTE, CRefSkill, Service), DATA_COL(DBTYPE_INT, CRefSkill, ID),
        DATA_COL(DBTYPE_INT, CRefSkill, GroupID), DATA_COL(DBTYPE_STRING, CRefSkill, Basic_Code),
        DATA_COL(DBTYPE_STRING, CRefSkill, Basic_Name),
        DATA_COL(DBTYPE_STRING, CRefSkill, Basic_Group),
        DATA_COL(DBTYPE_INT, CRefSkill, Basic_Original),
        DATA_COL(DBTYPE_BYTE, CRefSkill, Basic_Level),
        DATA_COL(DBTYPE_BYTE, CRefSkill, Basic_Activity),
        DATA_COL(DBTYPE_INT, CRefSkill, Basic_ChainCode),
    };
    CHECK(CheckLayout(skill, sizeof(skill) / sizeof(skill[0]), 24));
    CHECK(FIELD_OFFSET(RefSkill, Action_Range) == 146);
    CHECK(FIELD_OFFSET(RefSkill, Consume_HP) == 202);
    CHECK(FIELD_OFFSET(RefSkill, UI_IconFile) == 216);
    CHECK(FIELD_OFFSET(RefSkill, AI_SkillType) == 358);
    CHECK(FIELD_OFFSET(RefSkill, Param) == 360);

    // _RefMagicOpt: the ten AvailItemGroup / ReqClass pairs follow ExcFunc6
    CHECK(FIELD_OFFSET(RefMagicOpt, Prob) == 68);
    CHECK(FIELD_OFFSET(RefMagicOpt, AvailItem) == 164);
    CHECK(FIELD_OFFSET(RefMagicOptParam, ReqClass) == 28);

    // parsed rows of the stream records (constructor offsets of the analysis)
    CHECK(FIELD_OFFSET(CRefUIString, m_Row) == 48);
    CHECK(FIELD_OFFSET(RefUIString, Type) == 88);
    CHECK(FIELD_OFFSET(RefTrigger, Comment512) == 36 && FIELD_OFFSET(RefTrigger, IndexNumber) == 64);
    CHECK(FIELD_OFFSET(RefTriggerCondition, Sequence) == 64);
    CHECK(FIELD_OFFSET(RefOptionalTeleport, LevelMax) == 48);
    CHECK(FIELD_OFFSET(CRefRegion, m_adw30) == 48);
}

// ---------------------------------------------------------------------------------------------
// Table descriptions and runtime classes

static bool CheckTable(CDBRecord* pRecord, const char* pszTable, CRuntimeClass* pClass,
                       int nDataOffset)
{
    CDBTableInfo* pInfo = pRecord->GetTableInfo();
    bool bOk = pInfo != NULL && strcmp(pRecord->GetTableName(), pszTable) == 0
        && pInfo->m_strTable == pszTable && pInfo->m_dwFlags == DBTABLE_04
        && pInfo->m_pRecordClass == pClass && pInfo->m_nDataOffset == nDataOffset
        && pInfo->m_mapGroups.size() == 2 && pInfo->m_nRowSize == 0 && pInfo->m_pTable == NULL
        && pRecord->GetRuntimeClass() == pClass;
    if (bOk)
    {
        DBFieldGroupMap::const_iterator it = pInfo->m_mapGroups.begin();
        bOk = it->first == DBGROUP_KEY && it->second.vecColumns.empty();
        ++it;
        bOk = bOk && it->first == DBGROUP_ALL && it->second.vecColumns.empty();
    }
    if (!bOk)
        printf("table %s\n", pszTable);
    return bOk;
}

static void TestTables()
{
    CRefLevel level;
    CHECK(CheckTable(&level, "_REFLEVEL", &CRefLevel::classCRefLevel, 24));
    CHECK(level.GetTableInfo() == &CRefLevel::s_TableInfo);
    CRefSkill skill;
    CHECK(CheckTable(&skill, "_REFSKILL", &CRefSkill::classCRefSkill, 24));
    CRefRegion region;
    CHECK(CheckTable(&region, "_REFREGION", &CRefRegion::classCRefRegion, 48));
    CRefUIString uiString;
    CHECK(CheckTable(&uiString, "_RefUIString_Mt", &CRefUIString::classCRefUIString, 48));
    CRefHive hive;
    CHECK(CheckTable(&hive, "TAB_REFHIVE", &CRefHive::classCRefHive, 48));
    CRefDefaultQuest defaultQuest;
    CHECK(CheckTable(&defaultQuest, "_REFCHARDEFAULT_QUEST",
                     &CRefDefaultQuest::classCRefDefaultQuest, 48));
    CRefObjItem item;
    CHECK(CheckTable(&item, "_REFOBJITEM", &CRefObjItem::classCRefObjItem, 48));
    CRefRankingHunterContribution ranking;
    CHECK(CheckTable(&ranking, "TAB_REFRANKING_HUNTERCONTRIBUTION",
                     &CRefRankingHunterContribution::classCRefRankingHunterContribution, 48));
    CRefGameWorldGroup_Config config;
    CHECK(CheckTable(&config, "_RefGameWorldGroup_Config",
                     &CRefGameWorldGroup_Config::classCRefGameWorldGroup_Config, 48));

    // runtime classes: size, factory, base chain
    CHECK(CRefSkill::classCRefSkill.m_nObjectSize == 0x248);
    CHECK(strcmp(CRefSkill::classCRefSkill.m_lpszClassName, "CRefSkill") == 0);
    CHECK(CRefObjItem::classCRefObjItem.m_pBaseClass == &CRefObjDerived::classCRefObjDerived);
    CHECK(CRefObjDerived::classCRefObjDerived.m_pfnCreateObject == NULL);
    CHECK(CRefObjDerived::classCRefObjDerived.m_pBaseClass == &CDBRecStream::classCDBRecStream);
    CHECK(CRefObjChar::classCRefObjChar.IsDerivedFrom(&CDBRecord::classCDBRecord));
    CHECK(!CRefLevel::classCRefLevel.IsDerivedFrom(&CDBRecStream::classCDBRecStream));
    CBase* pObject = CRefSiegeQuest::classCRefSiegeQuest.CreateObject();
    CHECK(pObject != NULL && pObject->GetRuntimeClass() == &CRefSiegeQuest::classCRefSiegeQuest);
    CRefSiegeQuest::classCRefSiegeQuest.m_pfnDeleteObject(pObject);
}

// ---------------------------------------------------------------------------------------------
// Constructors

static void TestConstructors()
{
    BYTE abRaw[sizeof(CRefClimate)];
    memset(abRaw, 0xCC, sizeof(abRaw));
    CRefClimate* pClimate = new (abRaw) CRefClimate;
    CHECK(pClimate->ID == 0 && pClimate->Duration == 0 && pClimate->ProbRain == 0);
    CHECK(pClimate->m_qwDirty == 0 && pClimate->m_pOwner == NULL);
    pClimate->~CRefClimate();

    BYTE abLevel[sizeof(CRefLevel)];
    memset(abLevel, 0xCC, sizeof(abLevel));
    CRefLevel* pLevel = new (abLevel) CRefLevel;
    CHECK(pLevel->Exp_M == (int)0xCCCCCCCC);    // no column initialised
    pLevel->~CRefLevel();

    BYTE abBless[sizeof(CRefSiegeBlessBuff)];
    memset(abBless, 0xCC, sizeof(abBless));
    CRefSiegeBlessBuff* pBless = new (abBless) CRefSiegeBlessBuff;
    CHECK(pBless->Service == 0 && pBless->NeedGold == 0 && pBless->NeedGP == 0);
    pBless->~CRefSiegeBlessBuff();

    CRefSiegeFortress fortress;
    CHECK(fortress.m_Data.FortressID == 0 && fortress.m_Data.RequestFee == 0);
    CHECK(fortress.m_Data.TaxTargets == 0 && fortress.m_Data.CodeName128.empty());
    CRefGachaCode gacha;
    CHECK(gacha.m_Data.GachaSetID == 0 && gacha.m_Data.CodeName128.empty());
    CRefSiegeQuest quest;
    CHECK(quest.m_Data.QuestID == 0 && quest.m_Data.IsAccumulation == 0);

    BYTE abTeleport[sizeof(CRefOptionalTeleport)];
    memset(abTeleport, 0xCC, sizeof(abTeleport));
    CRefOptionalTeleport* pTeleport = new (abTeleport) CRefOptionalTeleport;
    CHECK(pTeleport->m_Row.dwRegion == 0 && pTeleport->m_Row.Pos_Z == 0);
    CHECK(pTeleport->m_Row.WorldID == 0xCCCC);
    CHECK(pTeleport->m_pBuffer == NULL && pTeleport->m_nCapacity == 0);
    pTeleport->~CRefOptionalTeleport();
}

// ---------------------------------------------------------------------------------------------
// Field writes: CDBRecord::SetField into the members, CDBRecStream::SetField into the buffer

static DBColumnInfo MakeColumn(int nType, int nOffset, int nSize)
{
    DBColumnInfo col;
    col.nType = nType;
    col.nOffset = nOffset;
    col.nSize = nSize;
    return col;
}

static void TestFieldWrites()
{
    CRefLevel level;
    level.m_qwDirty = 3;
    level.AllocData(64);
    CHECK(level.m_qwDirty == 0);
    CHECK(level.GetData() == (BYTE*)&level.Lvl);
    __int64 llExp = 0x123456789i64;
    DBColumnInfo colExp = MakeColumn(DBTYPE_INT64, 8, 8);
    level.SetField(&colExp, &llExp);
    CHECK(level.Exp_C == 0x123456789i64);

    CRefRegionBindAssocServer assoc;
    char szArea[8] = "abc    ";
    DBColumnInfo colArea = MakeColumn(DBTYPE_STRING, 0, 8);
    assoc.SetField(&colArea, szArea);
    CHECK(assoc.AreaName == "abc");
    CHECK(strcmp(szArea, "abc") == 0);          // trimmed in the fetch buffer
    char szBlank[8] = "       ";
    assoc.SetField(&colArea, szBlank);
    CHECK(assoc.AreaName == " ");               // SM-002: the first blank stays
    int nServer = 7;
    DBColumnInfo colServer = MakeColumn(DBTYPE_INT, 28, 4);
    assoc.SetField(&colServer, &nServer);
    CHECK(assoc.AssocServer == 7);

    // stream: {int}, then a string as {WORD length, WORD size, size - 4 bytes}
    CRefShop shop;
    shop.AllocData(32);
    CHECK(shop.m_pBuffer != NULL && shop.m_nCapacity == 32 && shop.m_nWritePos == 0);
    int nId = 0x11223344;
    DBColumnInfo colId = MakeColumn(DBTYPE_INT, 0, 4);
    shop.SetField(&colId, &nId);
    char szName[12] = "ab         ";
    DBColumnInfo colName = MakeColumn(DBTYPE_STRING, 0, 12);
    shop.SetField(&colName, szName);
    CHECK(shop.m_nWritePos == 4 + 2 + 2 + 8);
    CHECK(*(int*)shop.m_pBuffer == 0x11223344);
    CHECK(*(WORD*)(shop.m_pBuffer + 4) == 2 && *(WORD*)(shop.m_pBuffer + 6) == 12);
    CHECK(memcmp(shop.m_pBuffer + 8, "ab", 3) == 0);
    char szEmpty[8] = { 0 };
    DBColumnInfo colEmpty = MakeColumn(DBTYPE_STRING, 0, 8);
    BYTE* pOld = shop.m_pBuffer;
    shop.AllocData(16);                          // large enough: kept, positions cleared
    CHECK(shop.m_pBuffer == pOld && shop.m_nWritePos == 0 && shop.m_nCapacity == 32);
    shop.m_pBuffer[6] = 0xFF;
    shop.SetField(&colEmpty, szEmpty);
    CHECK(*(WORD*)shop.m_pBuffer == 0 && *(WORD*)(shop.m_pBuffer + 2) == 8);
    CHECK(*(DWORD*)(shop.m_pBuffer + 4) == 0);   // zeros for an empty string
    CHECK(shop.GetData() == shop.m_pBuffer);
    shop.AllocData(64);                          // too small: replaced
    CHECK(shop.m_nCapacity == 64 && shop.m_nWritePos == 0);
    shop.CopyTo(NULL);
}

int main()
{
    TestLayouts();
    TestTables();
    TestConstructors();
    TestFieldWrites();
    return TEST_RESULT("ShardRefRecordTest");
}
