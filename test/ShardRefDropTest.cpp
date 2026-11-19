// SR_ShardManager reference data, drop tables (SilkroadOnline\Server\ServerCommon\RefDataDrop.h):
// the builders of slots 0x28..0x36 and 0x57 over small fake row sets. Compiled with SHARD_DEFS.

#include "ServerCommon/ReferenceData.h"
#include "ServerCommon/RefDataObj.h"

#include <string.h>

#include <new>

#include "BSNet/NetEngine.h"

#include "LogCapture.h"
#include "TestCheck.h"

// ---------------------------------------------------------------------------------------------
// Fake rows and objects

template <class T>
static void Put(CDBRecStream* pRow, T value)
{
    pRow->WriteRaw(sizeof(T), &value);
}

static void PutString(CDBRecStream* pRow, const char* psz, WORD wFieldSize)
{
    WORD wLength = (WORD)strlen(psz);
    Put(pRow, wLength);
    Put(pRow, wFieldSize);
    pRow->WriteRaw(wLength, psz);
    for (int i = wLength + 4; i < wFieldSize; ++i)
        Put(pRow, (BYTE)0xCC);
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

    void Rewind()
    {
        for (CDBRecordList::iterator it = entry.m_listRows.begin();
             it != entry.m_listRows.end(); ++it)
            static_cast<CDBRecStream*>(*it)->m_nReadPos = 0;
    }
};

// A _RefObjCommon row with the fields the drop tables use and the monster's drop vectors.
struct FakeObj
{
    union
    {
        BYTE    ab[sizeof(RefObjChar)];
        double  dAlign;
    };

    FakeObj(int nID, WORD wTypeID, BYTE bCountry, BYTE bRarity, BYTE bClass)
    {
        memset(ab, 0, sizeof(ab));
        *(int*)(ab + 0x8) = nID;
        *(WORD*)(ab + 0x80) = wTypeID;
        ab[0x88] = bCountry;
        ab[0x89] = bRarity;
        ab[0x1A8] = bClass;
        new (&Char()->m_vecAssignedItemRndDrop) RefObjRndDropVector;
        for (int i = 0; i < REF_CUSTOMIZING_DROP_GROUP_COUNT; ++i)
            new (&Char()->m_avecCustomizingReservedItemDrop[i]) RefObjCustomizingDropVector;
        CReferenceData::GetInstance()->m_hashRefObj[nID] = Row();
    }

    ~FakeObj()
    {
        CReferenceData::GetInstance()->m_hashRefObj.erase(*(int*)(ab + 0x8));
        Char()->m_vecAssignedItemRndDrop.~RefObjRndDropVector();
        for (int i = 0; i < REF_CUSTOMIZING_DROP_GROUP_COUNT; ++i)
            Char()->m_avecCustomizingReservedItemDrop[i].~RefObjCustomizingDropVector();
    }

    RefObjChar* Char()
    {
        return (RefObjChar*)ab;
    }

    RefObjCommonRow* Row()
    {
        return (RefObjCommonRow*)ab;
    }
};

// ---------------------------------------------------------------------------------------------

static void TestSelectionTables(CReferenceData& data)
{
    RefMagicOptTables& t = data.m_MagicOpt;

    FakeTable<CRefDropGold> gold;
    CDBRecStream* pRow = gold.AddRow();
    Put(pRow, (BYTE)3);
    Put(pRow, 0.5f);
    Put(pRow, (DWORD)100);
    Put(pRow, (DWORD)200);
    pRow = gold.AddRow();                       // level 0: freed
    Put(pRow, (BYTE)0);
    Put(pRow, 1.0f);
    Put(pRow, (DWORD)1);
    Put(pRow, (DWORD)2);
    CHECK(data.BuildFromTable(&gold.entry, 0x28) == 1);
    CHECK(t.m_mapDropGold.size() == 1 && t.m_mapDropGold.count(3) == 1);
    if (t.m_mapDropGold.count(3))
    {
        RefDropGold* p = t.m_mapDropGold[3];
        CHECK(p->Prob == 50 && p->GoldMin == 100 && p->GoldMax == 200);
    }
    // the same level again fails the build
    gold.Rewind();
    CHECK(data.BuildFromTable(&gold.entry, 0x28) == 0 && t.m_mapDropGold.size() == 1);

    // the level is the column + 1
    FakeTable<CRefDropOptLevelSel> opt;
    pRow = opt.AddRow();
    Put(pRow, (BYTE)4);
    Put(pRow, 0.25f);
    Put(pRow, (DWORD)7);
    CHECK(data.BuildFromTable(&opt.entry, 0x29) == 1);
    CHECK(t.m_mapDropOptLevelSel.count(5) == 1);
    if (t.m_mapDropOptLevelSel.count(5))
    {
        RefDropOptLevelSel* p = t.m_mapDropOptLevelSel[5];
        CHECK(p->Level == 5 && p->Prob == 250000 && p->dw08 == 7);
    }

    // _RefDropClassSel_Recover: 7 classes, running total * 1000000 -> class
    FakeTable<CRefDropClassSel_Recover> recover;
    pRow = recover.AddRow();
    Put(pRow, (DWORD)12);
    const float afProb[7] = { 0.25f, 0.0f, 0.5f, 0.0000005f, 0.0f, 0.125f, 0.0f };
    for (int i = 0; i < 7; ++i)
        Put(pRow, afProb[i]);
    CHECK(data.BuildFromTable(&recover.entry, 0x2C) == 1);
    CHECK(t.m_amapDropClassSel[2].count(12) == 1);
    if (t.m_amapDropClassSel[2].count(12))
    {
        RefDropClassSel& sel = *t.m_amapDropClassSel[2][12];
        CHECK(sel.size() == 3);
        CHECK(sel[250000] == 0 && sel[750000] == 2 && sel[875000] == 5);
    }

    // slot 0x31 (_RefDropClassSel_AlchemyTablet) fills map 10
    FakeTable<CRefDropClassSel_AlchemyTablet> tablet;
    pRow = tablet.AddRow();
    Put(pRow, (DWORD)1);
    for (int i = 0; i < 12; ++i)
        Put(pRow, i == 11 ? 1.0f : 0.0f);
    CHECK(data.BuildFromTable(&tablet.entry, 0x31) == 1);
    CHECK(t.m_amapDropClassSel[10].count(1) == 1 && t.m_amapDropClassSel[7].empty());
    if (t.m_amapDropClassSel[10].count(1))
        CHECK((*t.m_amapDropClassSel[10][1])[1000000] == 11);
}

static void PutAssign(CDBRecStream* pRow, int nBits, int nRefObjID, DWORD dwWeight, int nClass)
{
    Put(pRow, nBits);
    Put(pRow, nRefObjID);
    Put(pRow, dwWeight);
    Put(pRow, (DWORD)0x88);
    Put(pRow, nClass);
    Put(pRow, (DWORD)0x99);
}

static void TestDropClassTables(CReferenceData& data)
{
    FakeObj sword(101, 0x2C, 0, 0, 1);              // equipment, country 0
    FakeObj blade(102, 0x2C, 3, 2, 5);              // rare equipment, every country
    FakeObj potion(103, 0xEC, 1, 0, 1);             // recover
    FakeObj stone(104, 0x15EC, 2, 0, 1);            // alchemy TID4 2
    FakeObj other(105, 0x0004, 0, 0, 1);            // no drop class

    FakeTable<CRefDropItemAssign> assign;
    PutAssign(assign.AddRow(), 1, 101, 10, 2);
    PutAssign(assign.AddRow(), 1, 101, 5, 2);
    PutAssign(assign.AddRow(), 1, 101, 0, 2);       // weight 0: the item already there
    PutAssign(assign.AddRow(), 2, 102, 4, -1);      // group 1, default class 5
    PutAssign(assign.AddRow(), 3, 103, 1, 1);       // first group only
    PutAssign(assign.AddRow(), 2, 104, 2, 12);
    PutAssign(assign.AddRow(), 0, 101, 1, 1);       // no group: skipped
    PutAssign(assign.AddRow(), 1, 999, 1, 1);       // unknown item: logged
    PutAssign(assign.AddRow(), 1, 105, 1, 1);       // no drop class: logged twice

    CNetEngine engine;
    IBSNet* pOld = g_pBSNet;
    g_pBSNet = &engine;
    LogCapture_Start();
    CHECK(data.BuildFromTable(&assign.entry, 0x34) == 1);
    std::vector<CAPTURED_LOG> logs = LogCapture_Drain();
    LogCapture_Stop();
    g_pBSNet = pOld;

    RefDropClassSlot* aSlot = data.m_MagicOpt.m_aDropClass;
    CHECK(aSlot[0].aEntry[2].size() == 36 && aSlot[2].aEntry[0].size() == 7);
    CHECK(aSlot[17].aEntry[1].size() == 2 && aSlot[21].aEntry[2].size() == 12);
    CHECK(aSlot[1].b30 == 3 && aSlot[0].b30 == 1 && aSlot[12].b30 == 3 && aSlot[21].b30 == 1);

    RefDropClassEntry& e = aSlot[0].aEntry[0][1];
    CHECK(e.dwCount == 15 && e.mapProb.size() == 2);
    CHECK(e.mapProb[10].pItem == sword.Row() && e.mapProb[15].Weight == 5);
    CHECK(e.mapProb[10].dw08 == 0x88 && e.mapProb[10].dw0C == 0x99);
    CHECK(e.mapItem.size() == 1 && e.mapItem[0x2C].size() == 3);
    if (e.mapItem[0x2C].size() == 3)
        CHECK(e.mapItem[0x2C][2] == &e.mapProb[15] && e.mapItem[0x2C][0] == &e.mapProb[10]);
    CHECK(aSlot[0].aEntry[1][1].dwCount == 0);

    for (int i = 0; i < 3; ++i)
    {
        RefDropClassEntry& r = aSlot[12].aEntry[i][4];
        CHECK(r.dwCount == 4 && r.mapProb.size() == 1 && r.mapItem[0x2C].size() == 1);
    }
    CHECK(aSlot[1].aEntry[0][4].dwCount == 0);

    CHECK(aSlot[2].aEntry[1][0].dwCount == 1 && aSlot[2].aEntry[1][0].mapItem.empty());
    CHECK(aSlot[13].aEntry[1][0].dwCount == 0);
    CHECK(aSlot[20].aEntry[2][11].dwCount == 2);

    int nUnknownRef = 0, nUnknownItem = 0, nUnbound = 0;
    for (size_t i = 0; i < logs.size(); ++i)
    {
        if (logs[i].dwFlags != 0x2000000)
            continue;
        if (logs[i].strText == "this item is to drop but can't find in _RefObjCommon. check it "
                               "out its existance or service on/off [RefID: 999]")
            ++nUnknownRef;
        if (logs[i].strText == "unknown item of drop assign table!!! [RefID: 105]")
            ++nUnknownItem;
        if (logs[i].strText == "can't find bound item drop group!!! [RefID: 105]")
            ++nUnbound;
    }
    CHECK(nUnknownRef == 1 && nUnknownItem == 1 && nUnbound == 1);
}

static void PutItemGroup(CDBRecStream* pRow, BYTE bService, DWORD dwID, DWORD dwItem)
{
    Put(pRow, bService);
    Put(pRow, dwID);
    PutString(pRow, "GROUP", 12);
    Put(pRow, dwItem);
    Put(pRow, 0.5f);
    Put(pRow, (DWORD)0);
}

static void PutRndDrop(CDBRecStream* pRow, BYTE bService, DWORD dwMonster, DWORD dwGroup)
{
    Put(pRow, bService);
    Put(pRow, dwMonster);
    Put(pRow, dwGroup);
    PutString(pRow, "RND", 8);
    Put(pRow, (BYTE)1);
    Put(pRow, (BYTE)2);
    Put(pRow, (BYTE)3);
    Put(pRow, 0.75f);
    Put(pRow, (DWORD)4);
    Put(pRow, (DWORD)5);
}

static void PutCustomizing(CDBRecStream* pRow, DWORD dwRefObj, BYTE b, DWORD dw, BYTE bGroup)
{
    Put(pRow, dwRefObj);
    Put(pRow, b);
    Put(pRow, dw);
    Put(pRow, bGroup);
    for (DWORD i = 0; i < 5; ++i)
        Put(pRow, 10 + i);
}

static void TestMonsterDropTables(CReferenceData& data)
{
    FakeObj item(201, 0x2C, 0, 0, 1);
    FakeObj monster(202, 0x4, 0, 0, 0);

    FakeTable<CRefDropItemGroup> group;
    PutItemGroup(group.AddRow(), 1, 7, 201);
    PutItemGroup(group.AddRow(), 1, 7, 201);
    PutItemGroup(group.AddRow(), 0, 8, 201);        // out of service: into group 0 (SM-030)
    CHECK(data.BuildFromTable(&group.entry, 0x35) == 1);
    CHECK(data.m_mapDropItemGroup.size() == 2 && data.m_mapDropItemGroup[7].size() == 2);
    CHECK(data.m_mapDropItemGroup[0].size() == 1 && data.m_mapDropItemGroup.count(8) == 0);
    std::vector<RefDropItemGroupRow*> vec;
    CHECK(RefData_GetDropItemGroup(&data, 7, vec) && vec.size() == 2);
    if (vec.size() == 2)
    {
        CHECK(vec[1]->CodeName == "GROUP" && vec[1]->RefItemID == 201);
        CHECK(vec[1]->SelectRatio == 0.5f);
    }
    CHECK(!RefData_GetDropItemGroup(&data, 9, vec));

    FakeTable<CRefMonster_AssignedItemRndDrop> rnd;
    PutRndDrop(rnd.AddRow(), 1, 202, 7);
    CHECK(data.BuildFromTable(&rnd.entry, 0x36) == 1);
    RefObjRndDropVector& vecRnd = monster.Char()->m_vecAssignedItemRndDrop;
    CHECK(vecRnd.size() == 1 && data.m_vecMonsterAssignedItemRndDrop.size() == 1);
    if (vecRnd.size() == 1)
    {
        RefMonsterAssignedItemRndDropEntry& r = vecRnd[0];
        CHECK(r.ItemGroupID == 7 && r.str04 == "RND" && r.b20 == 1 && r.DropAmountMin == 2);
        CHECK(r.DropAmountMax == 3 && r.f24 == 0.75f && r.dw28 == 4 && r.dw2C == 5);
    }
    // a row out of service fails the build (SM-031)
    FakeTable<CRefMonster_AssignedItemRndDrop> rndOff;
    PutRndDrop(rndOff.AddRow(), 0, 202, 7);
    CHECK(data.BuildFromTable(&rndOff.entry, 0x36) == 0 && vecRnd.size() == 1);

    FakeTable<CRefCustomizingReservedItemDropForMonster> custom;
    PutCustomizing(custom.AddRow(), 202, 8, 4, 15);
    CHECK(data.BuildFromTable(&custom.entry, 0x57) == 1);
    RefObjCustomizingDropVector& vecCustom = monster.Char()->m_avecCustomizingReservedItemDrop[15];
    CHECK(vecCustom.size() == 1);
    if (vecCustom.size() == 1)
    {
        CHECK(vecCustom[0].b00 == 8 && vecCustom[0].dw04 == 4 && vecCustom[0].GroupID == 15);
        CHECK(vecCustom[0].adw0C[0] == 10 && vecCustom[0].adw0C[4] == 14);
    }
    // group 16 lands on Except[0..1] of the monster (SM-033)
    FakeTable<CRefCustomizingReservedItemDropForMonster> custom16;
    PutCustomizing(custom16.AddRow(), 202, 8, 4, 16);
    CHECK(data.BuildFromTable(&custom16.entry, 0x57) == 1);
    RefObjCustomizingDropVector* pvec16 =
        (RefObjCustomizingDropVector*)&monster.Char()->Except[0];
    CHECK(pvec16->size() == 1 && monster.Char()->Except[0].m_dw04 != 0);
    pvec16->~RefObjCustomizingDropVector();
    memset(monster.Char()->Except, 0, 2 * sizeof(RefObjCharExcept));
    // a value out of range fails the build
    FakeTable<CRefCustomizingReservedItemDropForMonster> customBad;
    PutCustomizing(customBad.AddRow(), 202, 9, 0, 1);
    CHECK(data.BuildFromTable(&customBad.entry, 0x57) == 0);
    CHECK(monster.Char()->m_avecCustomizingReservedItemDrop[1].empty());

    for (size_t i = 0; i < data.m_vecMonsterAssignedItemRndDrop.size(); ++i)
        delete data.m_vecMonsterAssignedItemRndDrop[i];
    data.m_vecMonsterAssignedItemRndDrop.clear();
    for (CReferenceData::RefDropItemGroupMap::iterator it = data.m_mapDropItemGroup.begin();
         it != data.m_mapDropItemGroup.end(); ++it)
    {
        for (size_t i = 0; i < it->second.size(); ++i)
            delete it->second[i];
    }
    data.m_mapDropItemGroup.clear();
}

int main()
{
    CReferenceData* pData = CReferenceData::GetInstance();
    CHECK(pData == g_pReferenceData && pData != NULL);

    TestSelectionTables(*pData);
    TestDropClassTables(*pData);
    TestMonsterDropTables(*pData);

    return TEST_RESULT("ShardRefDropTest");
}
