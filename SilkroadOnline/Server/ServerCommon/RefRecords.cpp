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
