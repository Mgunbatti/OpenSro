// SR_ShardManager reference data, region and teleport tables
// (SilkroadOnline\Server\ServerCommon\RefDataTeleport.h): the region bind check, regions and
// their links, teleports, teleport links, the slot 0x5F rows and the event zone reader, built
// from small fake row sets. Compiled with SHARD_DEFS.

#include "ServerCommon/RefDataTeleport.h"

#include <process.h>
#include <string.h>

#include <new>

#include "BSNet/NetEngine.h"

#include "LogCapture.h"
#include "TestCheck.h"

// ---------------------------------------------------------------------------------------------
// Fake rows in the stream format of CDBRecStream::SetField

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

static void PutString(CDBRecStream* pRow, const char* psz)
{
    WORD wLength = (WORD)strlen(psz);
    PutWord(pRow, wLength);
    PutWord(pRow, (WORD)(wLength + 4));
    pRow->WriteRaw(wLength, psz);
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

    TRecord* AddRow()
    {
        TRecord* pRecord = new TRecord;
        pRecord->AllocData(512);
        entry.m_listRows.push_back(pRecord);
        return pRecord;
    }

    TRecord* AddRecord()
    {
        TRecord* pRecord = new TRecord;
        entry.m_listRows.push_back(pRecord);
        return pRecord;
    }
};

// ---------------------------------------------------------------------------------------------
// Message boxes: a thread presses OK and keeps the text

static volatile LONG s_bStopCloser = 0;
static char s_szLastBox[2048];

static unsigned __stdcall MessageBoxCloser(void*)
{
    while (!s_bStopCloser)
    {
        HWND hWnd = FindWindowA("#32770", "BSObj Plugin");
        char szText[sizeof(s_szLastBox)];
        if (hWnd && GetDlgItemTextA(hWnd, 0xFFFF, szText, sizeof(szText)) > 0)
        {
            memcpy(s_szLastBox, szText, sizeof(szText));
            PostMessageA(hWnd, WM_CLOSE, 0, 0);
            Sleep(50);
        }
        Sleep(10);
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------

static void AddBind(CReferenceData& data, const char* pszName, int nAssocServer)
{
    RefRegionBindAssocServerData* p = new RefRegionBindAssocServerData;
    p->AreaName = pszName;
    p->AssocServer = nAssocServer;
    data.m_vecRegionBindAssocServer.push_back(p);
}

static void ClearBind(CReferenceData& data)
{
    for (size_t i = 0; i < data.m_vecRegionBindAssocServer.size(); ++i)
        delete data.m_vecRegionBindAssocServer[i];
    data.m_vecRegionBindAssocServer.clear();
}

static void TestRegionBindCheck(CReferenceData& data)
{
    s_szLastBox[0] = 0;
    AddBind(data, "CHINA", 1);
    AddBind(data, "FORT_JA_AREA", 1);
    AddBind(data, "FORT_BJ_AREA", 1);
    AddBind(data, "West_China", 2);
    AddBind(data, "FORT_DW_AREA", 2);
    CHECK(RefData_CheckRegionAssocServerSetting(&data) == 1);
    CHECK(RefData_FindAssocServerByAreaName(&data, "west_CHINA") == 2);
    CHECK(RefData_FindAssocServerByAreaName(&data, "Mars") == 0);

    AddBind(data, "FORT_HM_AREA", 3);
    CHECK(RefData_CheckRegionAssocServerSetting(&data) == 0);
    CHECK(strstr(s_szLastBox, "[_RefRegionBindAssocServer]") != NULL &&
          strstr(s_szLastBox, "'FORT_HM_AREA' (3) and 'West_China' (2)") != NULL);
    ClearBind(data);

    // a fortress without its region and an unknown fortress are both "no hardcoded entry"
    s_szLastBox[0] = 0;
    AddBind(data, "FORT_SK_AREA", 5);
    CHECK(RefData_CheckRegionAssocServerSetting(&data) == 0);
    CHECK(strstr(s_szLastBox, "There's no hardcoded entry for Sirge Fortress!!") != NULL);
    ClearBind(data);
    s_szLastBox[0] = 0;
    AddBind(data, "Ca", 4);
    AddBind(data, "FORT_SK_AREA", 4);
    AddBind(data, "FORT_ZZ_AREA", 4);
    CHECK(RefData_CheckRegionAssocServerSetting(&data) == 0);
    CHECK(strstr(s_szLastBox, "no hardcoded entry") != NULL);
    ClearBind(data);
}

static void TestRegionBindTable(CReferenceData& data)
{
    FakeTable<CRefRegionBindAssocServer> bind;
    const char* apszName[] = { "CHINA", "FORT_JA_AREA", "UNUSED", "CHINA" };
    int anServer[] = { 1, 1, 0, 7 };
    for (int i = 0; i < 4; ++i)
    {
        CRefRegionBindAssocServer* p = bind.AddRecord();
        p->AreaName = apszName[i];
        p->AssocServer = anServer[i];
    }
    CHECK(data.BuildFromTable(&bind.entry, 2) == 1);
    CHECK(data.m_vecRegionBindAssocServer.size() == 3);
    CHECK(RefData_FindAssocServerByAreaName(&data, "china") == 1);
}

static void PutRegion(CDBRecStream* pRow, WORD wID, const char* pszContinent,
                      const char* pszArea, DWORD dwClimate, const DWORD* pdwLinks)
{
    PutWord(pRow, wID);
    PutByte(pRow, 10);
    PutByte(pRow, 20);
    PutString(pRow, pszContinent);
    PutString(pRow, pszArea);
    PutByte(pRow, 1);
    PutDword(pRow, dwClimate);
    PutDword(pRow, 500);
    PutDword(pRow, 600);
    PutDword(pRow, 0);
    PutString(pRow, "file");
    for (int i = 0; i < 10; ++i)
        PutDword(pRow, pdwLinks[i]);
}

static void TestRegions(CReferenceData& data, RefTableVector& tables,
                        FakeTable<CRefRegion>& region)
{
    RefClimateData* pClimate = new RefClimateData;
    data.m_mapClimate[5] = pClimate;
    DWORD adwLinks100[10] = { 101, 0, 999 };
    DWORD adwLinks101[10] = { 100 };
    PutRegion(region.AddRow(), 100, "CHINA", "CH_A", 5, adwLinks100);
    PutRegion(region.AddRow(), 101, "china", "CH_B", 0, adwLinks101);
    PutRegion(region.AddRow(), 102, "MARS", "NOWHERE", 0, adwLinks101);     // no AssocServer
    tables[3] = &region.entry;
    CHECK(data.BuildFromTable(&region.entry, 3) == 1);
    CHECK(data.m_mapRegion.size() == 2);
    RefRegionRow* p100 = data.FindRegion(100);
    RefRegionRow* p101 = data.FindRegion(101);
    CHECK(p100 && p101 && data.FindRegion(102) == NULL);
    if (p100 && p101)
    {
        CHECK(p100->AssocServer == 1 && p100->pClimate == pClimate && p101->pClimate == NULL);
        CHECK(p100->ContinentName == "CHINA" && p100->MaxCapacity == 500 &&
              p100->AssocObjID == 600 && p100->AssocFile256 == "file" &&
              p100->IsBattleField == 1);
        CHECK(p100->apLinkedRegion[0] == p101 && p100->apLinkedRegion[1] == NULL &&
              p100->apLinkedRegion[2] == NULL && p101->apLinkedRegion[0] == p100);
        CHECK(strcmp(RefData_GetRegionName(101), "CH_B") == 0);
    }
}

static void PutTeleport(CDBRecStream* pRow, DWORD dwService, DWORD dwID, const char* pszCode,
                        DWORD dwAssocObj, BYTE bResurrect, BYTE bCanBringCOS)
{
    PutDword(pRow, dwService);
    PutDword(pRow, dwID);
    PutString(pRow, pszCode);
    PutDword(pRow, dwAssocObj);
    PutWord(pRow, 100);
    PutWord(pRow, 1);
    PutWord(pRow, 2);
    PutWord(pRow, 3);
    PutWord(pRow, 4);
    PutByte(pRow, bResurrect);
    PutByte(pRow, 0);
    PutWord(pRow, 50);
    PutByte(pRow, bCanBringCOS);
}

static void TestTeleports(CReferenceData& data, std::set<BYTE>*& pFlags, BYTE* pObj)
{
    FakeTable<CRefTeleport> teleport;
    PutTeleport(teleport.AddRow(), 1, 1, "TEL_A", 0, 0, 0);
    PutTeleport(teleport.AddRow(), 1, 2, "TEL_B", 777, 0, 0);
    PutTeleport(teleport.AddRow(), 0, 4, "OFF", 0, 0, 0);
    PutTeleport(teleport.AddRow(), 1, 3, "TEL_C", 555, 1, 1);
    pFlags = new (pObj + 0x90) std::set<BYTE>;
    pFlags->insert(1);
    data.m_hashRefObj[555] = (RefObjCommonRow*)pObj;

    CNetEngine engine;
    IBSNet* pOld = g_pBSNet;
    g_pBSNet = &engine;
    LogCapture_Start();
    CHECK(data.BuildFromTable(&teleport.entry, 0x12) == 1);
    std::vector<CAPTURED_LOG> logs = LogCapture_Drain();
    LogCapture_Stop();
    g_pBSNet = pOld;
    CHECK(logs.size() == 1 && logs[0].dwFlags == 0x2000000 &&
          logs[0].strText == "can't find associated ref obj with this teleport[tel id: TEL_B]");
    CHECK(data.m_mapTeleport.size() == 3 && data.m_mapTeleportByCodeName.size() == 3);
    RefTeleportRow* p3 = data.m_mapTeleportByCodeName["TEL_C"];
    CHECK(p3 && p3->ID == 3);
    if (p3)
    {
        CHECK(p3->pGenRegion == data.FindRegion(100) && p3->GenPos_X == 1 &&
              p3->GenWorldID == 4 && p3->GenAreaRadius == 50 && p3->CanBringCOS == 1);
        CHECK(p3->pAssocRefObj == (RefObjCommonRow*)pObj);
        CHECK(*(RefTeleportRow**)(pObj + 0x190) == p3);
        CHECK(pFlags->size() == 1 && pFlags->count(7) == 1);
    }
}

static void PutTeleLink(CDBRecStream* pRow, DWORD dwService, DWORD dwOwner, DWORD dwTarget,
                        DWORD dwRestrict)
{
    PutDword(pRow, dwService);
    PutDword(pRow, dwOwner);
    PutDword(pRow, dwTarget);
    PutDword(pRow, 1000);
    PutByte(pRow, 1);
    PutByte(pRow, 2);
    PutByte(pRow, 3);
    for (int i = 0; i < 5; ++i)
    {
        PutDword(pRow, (i == 0 || i == 3) ? dwRestrict : 0);
        PutDword(pRow, 10 + i);
        PutDword(pRow, 20 + i);
    }
}

static void TestTeleLinks(CReferenceData& data, std::set<BYTE>* pFlags)
{
    FakeTable<CRefTeleLink> link;
    PutTeleLink(link.AddRow(), 1, 1, 2, 5);
    PutTeleLink(link.AddRow(), 0, 1, 3, 5);                    // out of service
    PutTeleLink(link.AddRow(), 1, 3, 1, 0);
    CHECK(data.BuildFromTable(&link.entry, 0x13) == 1);
    RefTeleportRow* p1 = data.m_mapTeleport[1];
    RefTeleportRow* p3 = data.m_mapTeleport[3];
    CHECK(p1->vecLink.size() == 1 && p3->vecLink.size() == 1);
    if (p1->vecLink.size() == 1)
    {
        RefTeleLink* pLink = p1->vecLink[0];
        CHECK(pLink->pTargetTeleport == data.m_mapTeleport[2] && pLink->Fee == 1000 &&
              pLink->CheckResult == 3 && pLink->vecRestrict.size() == 2);
        CHECK(pLink->vecRestrict.size() == 2 && pLink->vecRestrict[1].Data1 == 13);
    }
    CHECK(pFlags->size() == 2 && pFlags->count(8) == 1);
}

static void PutMerit(CDBRecStream* pRow, DWORD dwService, BYTE bIndex, BYTE bKey,
                     const char* psz)
{
    PutDword(pRow, dwService);
    PutByte(pRow, bIndex);
    PutString(pRow, psz);
    PutByte(pRow, bKey);
    float f = 1.5f;
    for (int i = 0; i < 4; ++i)
        pRow->WriteRaw(4, &f);
    for (int i = 0; i < 3; ++i)
    {
        __int64 n = 100 + i;
        pRow->WriteRaw(8, &n);
        PutString(pRow, "s");
    }
}

static void TestAlchemyMerit(CReferenceData& data)
{
    FakeTable<CRefTeleport> merit;      // any stream record class
    PutMerit(merit.AddRow(), 1, 2, 4, "FIRST");
    PutMerit(merit.AddRow(), 0, 3, 4, "OFF");
    PutMerit(merit.AddRow(), 1, 2, 4, "SECOND");
    CHECK(data.BuildFromTable(&merit.entry, 0x5F) == 1);
    CHECK(data.m_vecGameWorldPosition.size() == 7 && data.m_vecGameWorldPosition[2].size() == 1 &&
          data.m_vecGameWorldPosition[3].size() == 0);
    RefGameWorldPositionRow* p = data.m_vecGameWorldPosition[2][4];
    CHECK(p->str04 == "FIRST" && p->af20[3] == 1.5f && p->an30[2] == 102 &&
          p->astr48[2] == "s");
}

static void TestEventZoneReader()
{
    CRefTeleport record;
    record.AllocData(512);
    PutDword(&record, 1);
    PutDword(&record, 9);
    PutString(&record, "EVT_Zone_A");
    PutString(&record, "teleport");
    for (int i = 0; i < 5; ++i)
        PutDword(&record, 0);
    PutString(&record, "tel_a");
    for (int i = 1; i < 5; ++i)
        PutString(&record, "");
    CRefRow row(&record);
    RefTeleportLinkedEvent ev;
    CHECK(RefRow_Read(row, ev) == 9);
    CHECK(ev.CodeName128 == "evt_zone_a" && ev.adwParam[4] == 1 && ev.dwDC == 0 &&
          ev.astrParam[0] == "tel_a");
}

int main()
{
    CReferenceData* pData = CReferenceData::GetInstance();
    RefTableVector tables(REF_TABLE_SLOT_COUNT, (RefTableEntry*)NULL);
    pData->m_pTables = &tables;
    HANDLE hCloser = (HANDLE)_beginthreadex(NULL, 0, MessageBoxCloser, NULL, 0, NULL);

    static BYTE abObj[0x260];
    std::set<BYTE>* pFlags = NULL;
    {
        FakeTable<CRefRegion> region;
        TestRegionBindCheck(*pData);
        TestRegionBindTable(*pData);
        TestRegions(*pData, tables, region);
        TestTeleports(*pData, pFlags, abObj);
        TestTeleLinks(*pData, pFlags);
        TestAlchemyMerit(*pData);
        TestEventZoneReader();
        tables[3] = NULL;
    }

    InterlockedExchange(&s_bStopCloser, 1);
    WaitForSingleObject(hCloser, INFINITE);
    CloseHandle(hCloser);
    pData->m_hashRefObj.erase(555);
    pFlags->~set();
    pData->m_pTables = NULL;
    return TEST_RESULT("ShardRefTeleportTest");
}
