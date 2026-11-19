// SR_ShardManager reference data (SilkroadOnline\Server\ServerCommon\ReferenceData.h): the row
// reader, BuildAllTables over a few tables built from small fake row sets, the lookups and the
// log line of an empty table. Compiled with SHARD_DEFS.

#include "ServerCommon/ReferenceData.h"

#include <string.h>

#include "BSNet/NetEngine.h"

#include "LogCapture.h"
#include "TestCheck.h"

// ---------------------------------------------------------------------------------------------
// Fake rows: the columns in the stream format CDBRecStream::SetField writes.

static void PutDword(CDBRecStream* pRow, DWORD dw)
{
    pRow->WriteRaw(4, &dw);
}

static void PutWord(CDBRecStream* pRow, WORD w)
{
    pRow->WriteRaw(2, &w);
}

static void PutByte(CDBRecStream* pRow, BYTE b)
{
    pRow->WriteRaw(1, &b);
}

// WORD length, WORD field size (header included), the characters, padding to the field size.
static void PutString(CDBRecStream* pRow, const char* psz, WORD wFieldSize)
{
    WORD wLength = (WORD)strlen(psz);
    PutWord(pRow, wLength);
    PutWord(pRow, wFieldSize);
    pRow->WriteRaw(wLength, psz);
    for (int i = wLength + 4; i < wFieldSize; ++i)
        PutByte(pRow, 0xCC);
}

template <class TRecord>
struct FakeTable
{
    CDBTable table;
    RefTableEntry entry;

    FakeTable()
    {
        table.m_pInfo = &TRecord::s_TableInfo;
        entry.m_pTable = &table;
    }

    ~FakeTable()
    {
        for (CDBRecordList::iterator it = entry.m_listRows.begin();
             it != entry.m_listRows.end(); ++it)
            delete *it;
        table.m_pInfo = NULL;
    }

    CDBRecStream* AddRow()
    {
        TRecord* pRecord = new TRecord;
        pRecord->AllocData(256);
        entry.m_listRows.push_back(pRecord);
        return pRecord;
    }
};

// ---------------------------------------------------------------------------------------------

static void TestRowReader()
{
    CRefTrigger record;
    record.AllocData(64);
    PutString(&record, "abc", 10);
    PutString(&record, "", 6);
    PutDword(&record, 7);

    CRefRow row(&record);
    std::string s1, s2("old");
    DWORD dw = 0;
    row >> s1 >> s2 >> dw;
    CHECK(s1 == "abc" && s2 == "old" && dw == 7);
    CHECK(record.m_nReadPos == 20 && record.m_nFieldCount == 3);
}

static void TestTriggerTables(CReferenceData& data, RefTableVector& tables)
{
    FakeTable<CRefTrigger> trigger;
    CDBRecStream* pRow = trigger.AddRow();
    PutDword(pRow, 1);
    PutDword(pRow, 10);
    PutString(pRow, "TRIGGER_A", 20);
    PutByte(pRow, 1);
    PutByte(pRow, 0);
    PutString(pRow, "comment", 12);
    PutDword(pRow, 3);
    pRow = trigger.AddRow();                    // out of service: dropped
    PutDword(pRow, 0);
    PutDword(pRow, 11);
    pRow = trigger.AddRow();                    // duplicate ID: the first row stays
    PutDword(pRow, 1);
    PutDword(pRow, 10);
    PutString(pRow, "TRIGGER_B", 20);
    PutByte(pRow, 0);
    PutByte(pRow, 0);
    PutString(pRow, "", 4);
    PutDword(pRow, 4);
    tables[0x6E] = &trigger.entry;

    FakeTable<CRefTriggerBindEvent> bind;
    for (int i = 0; i < 2; ++i)
    {
        pRow = bind.AddRow();
        PutDword(pRow, 1);
        PutDword(pRow, 100 + i);
        PutDword(pRow, 10);
        PutDword(pRow, 500 + i);
    }
    tables[0x70] = &bind.entry;

    FakeTable<CRefTriggerConditionParam> param;
    pRow = param.AddRow();
    PutDword(pRow, 1);
    PutDword(pRow, 7);
    PutString(pRow, "GROUP", 16);
    PutString(pRow, "VALUE_CODE", 16);
    PutString(pRow, "42", 8);
    PutString(pRow, "int", 8);
    tables[0x77] = &param.entry;

    FakeTable<CRefGameWorldGroup_Config> empty;
    tables[0x7B] = &empty.entry;

    FakeTable<CRefGameWorldGroup> group;
    pRow = group.AddRow();
    PutDword(pRow, 3);
    PutString(pRow, "GROUP_A", 12);
    PutString(pRow, "obj", 8);
    PutString(pRow, "CONFIG_A", 12);
    tables[0x7A] = &group.entry;

    CNetEngine engine;
    IBSNet* pOld = g_pBSNet;
    g_pBSNet = &engine;
    LogCapture_Start();
    CHECK(data.BuildAllTables() == TRUE);
    std::vector<CAPTURED_LOG> logs = LogCapture_Drain();
    LogCapture_Stop();
    g_pBSNet = pOld;

    CHECK(data.m_mapTrigger.size() == 1);
    if (data.m_mapTrigger.size() == 1)
    {
        RefTrigger* p = data.m_mapTrigger.begin()->second;
        CHECK(p->ID == 10 && p->CodeName128 == "TRIGGER_A" && p->IsActive == 1);
        CHECK(p->Comment512 == "comment" && p->IndexNumber == 3);
    }
    CHECK(data.m_mmapTriggerBindEvent.count(10) == 2);
    CHECK(data.m_mapTriggerConditionParam.size() == 1);
    if (data.m_mapTriggerConditionParam.size() == 1)
    {
        RefTriggerConditionParam* p = data.m_mapTriggerConditionParam["GROUP"];
        CHECK(p->ID == 7 && p->ValueCodeName128 == "VALUE_CODE" && p->Value == "42");
        CHECK(p->Type == "int");
    }
    CHECK(data.m_mapGameWorldGroup.size() == 1 &&
          data.m_mapGameWorldGroup[3]->ConfigGroupCodeName128 == "CONFIG_A");

    bool bLogged = false;
    for (size_t i = 0; i < logs.size(); ++i)
    {
        if (logs[i].dwFlags == 0x2000000 &&
            logs[i].strText == "!!! there is no record in this[_RefGameWorldGroup_Config] "
                               "table !!!")
            bLogged = true;
    }
    CHECK(bLogged);

    // an empty table fails its builder
    CHECK(data.BuildFromTable(&empty.entry, 0x7B) == 0);
    // a second build reads the rows again from their start; the duplicate is not inserted
    static_cast<CDBRecStream*>(group.entry.m_listRows.front())->m_nReadPos = 0;
    CHECK(data.BuildFromTable(&group.entry, 0x7A) == 1 && data.m_mapGameWorldGroup.size() == 1);

    for (size_t i = 0; i < tables.size(); ++i)
        tables[i] = NULL;
}

static void TestFindRefObj(CReferenceData& data)
{
    static BYTE abObj[0x260];
    RefObjCommonRow* pObj = (RefObjCommonRow*)abObj;
    data.m_hashRefObj[1234] = pObj;
    CHECK(CReferenceData::GetInstance() == &data);
    CHECK(data.FindRefObj(1234) == pObj && data.FindRefObj(1) == NULL);
    data.m_hashRefObj.erase(1234);
}

int main()
{
    CReferenceData* pData = CReferenceData::GetInstance();
    CHECK(pData == g_pReferenceData && pData != NULL);
    RefTableVector tables(REF_TABLE_SLOT_COUNT, (RefTableEntry*)NULL);
    pData->m_pTables = &tables;

    TestRowReader();
    TestTriggerTables(*pData, tables);
    TestFindRefObj(*pData);

    pData->m_pTables = NULL;
    return TEST_RESULT("ShardReferenceDataTest");
}
