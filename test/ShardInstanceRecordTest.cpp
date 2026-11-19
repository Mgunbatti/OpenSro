// SR_ShardManager instance record classes (SilkroadOnline\Server\ServerCommon\InstanceRecords.h):
// record sizes and member offsets, the table descriptions (names, flags, columns per field
// group), the column setters with their dirty bits, InitData, CopyTo and the stream writers.
// Compiled with SHARD_DEFS.

#include "ServerCommon/InstanceRecords.h"
#include "ServerCommon/RefDataObj.h"
#include "ServerCommon/ReferenceData.h"

#include <string.h>
#include <time.h>

#include <new>

#include "TestCheck.h"

// The reference data of the test: one object row with known values at the offsets the records
// read, registered in the real reference data under one id at a time (so a lookup finds it only
// with that id).
static BYTE s_abRefObj[0x260];

static void SetRefObjID(int nRefObjID)
{
    CReferenceData::RefObjHashMap& objects = CReferenceData::GetInstance()->m_hashRefObj;
    objects.clear();
    objects[nRefObjID] = (RefObjCommonRow*)s_abRefObj;
}

static const DBColumnInfo* FindColumn(const CDBTableInfo& info, const char* pszName, int* pGroup)
{
    for (DBFieldGroupMap::const_iterator it = info.m_mapGroups.begin();
         it != info.m_mapGroups.end(); ++it)
    {
        const DBColumnVector& columns = it->second.vecColumns;
        for (DBColumnVector::const_iterator col = columns.begin(); col != columns.end(); ++col)
        {
            if (col->strName == pszName)
            {
                *pGroup = it->first;
                return &*col;
            }
        }
    }
    return NULL;
}

// Column in group nGroup at nOffset (record offset; the description holds it minus the data
// offset).
static bool CheckColumn(const CDBTableInfo& info, const char* pszName, int nOffset, int nGroup)
{
    int nFound = -1;
    const DBColumnInfo* pColumn = FindColumn(info, pszName, &nFound);
    bool bOk = pColumn != NULL && nFound == nGroup &&
               pColumn->nOffset == nOffset - info.m_nDataOffset;
    if (!bOk)
        printf("column %s.%s\n", info.m_strTable.c_str(), pszName);
    return bOk;
}

static void TestLayouts()
{
    CHECK(sizeof(CInstanceUser) == 0x20 && sizeof(CInstancePC) == 0xF8);
    CHECK(sizeof(CInstanceCOS) == 0x80 && sizeof(CInstanceGuild) == 0xA0);
    CHECK(sizeof(CInstanceDummy) == 0x20 && sizeof(CInstanceStruct) == 0x28);
    CHECK(sizeof(CInstanceTimedJob) == 0x60 && sizeof(CInstanceOpenMarket) == 0xA8);
    CHECK(sizeof(CInstanceInventory) == 0x20 && sizeof(CInstanceLogSchedule) == 0x88);
    CHECK(FIELD_OFFSET(CInstancePC, CharID) == 0x20 && FIELD_OFFSET(CInstancePC, HwanLevel) == 0xF2);
    CHECK(FIELD_OFFSET(CInstanceRentItemInfo, MeterRateTime) == 0x48);
    CHECK(FIELD_OFFSET(CInstanceAlliedClan, Ally) == 0x1C);
}

static void TestTables()
{
    const CDBTableInfo& user = CInstanceUser::s_TableInfo;
    CHECK(user.m_strTable == "_USER" && user.m_dwFlags == 5 && user.m_nDataOffset == 24);
    CHECK(user.m_pRecordClass == &CInstanceUser::classCInstanceUser);
    CHECK(CheckColumn(user, "UserJID", 0x18, 0) && CheckColumn(user, "CharID", 0x1C, 2));

    const CDBTableInfo& pc = CInstancePC::s_TableInfo;
    CHECK(pc.m_strTable == "_CHAR" && pc.m_nDataOffset == 32 && pc.m_mapGroups.size() == 14);
    CHECK(CheckColumn(pc, "CharID", 0x20, 0) && CheckColumn(pc, "PosX", 0x98, 5));
    CHECK(CheckColumn(pc, "LastLogout", 0xBC, 10) && CheckColumn(pc, "HwanLevel", 0xF2, 13));

    // indexed names
    CHECK(CheckColumn(CInstanceAlliedClan::s_TableInfo, "Ally1", 0x1C, 2));
    CHECK(CheckColumn(CInstanceAlliedClan::s_TableInfo, "Ally8", 0x38, 2));
    CHECK(CheckColumn(CInstanceGuildWar::s_TableInfo, "PointGain2", 0x40, 3));

    const CDBTableInfo& inv = CInstanceInventory::s_TableInfo;
    CHECK(inv.m_strTable == "_INVENTORY" && inv.m_dwFlags == 4 && CheckColumn(inv, "Slot", 0x18, 2));
    CHECK(CInstanceDummy::s_TableInfo.m_strTable == "_DUMMY" &&
          CInstanceDummy::s_TableInfo.m_mapGroups.size() == 2);
    CHECK(CInstanceSiegeFortressObject::s_TableInfo.m_dwFlags == 0x15);

    // runtime classes
    CHECK(CInstancePC::classCInstancePC.m_pBaseClass == &CInstanceChar::classCInstanceChar);
    CHECK(CInstanceChar::classCInstanceChar.m_pBaseClass == &CInstanceObj::classCInstanceObj);
    CHECK(CInstanceTimedJob::classCInstanceTimedJob.m_pBaseClass == &CDBRecord::classCDBRecord);
    CHECK(CInstanceChest::classCInstanceChest.m_pBaseClass == &CInstanceSlot::classCInstanceSlot);
    CHECK(CInstanceGuild::classCInstanceGuild.m_nObjectSize == 0xA0);
    CBase* pObject = CInstanceQuest::classCInstanceQuest.CreateObject();
    CHECK(pObject != NULL && pObject->GetRuntimeClass() == &CInstanceQuest::classCInstanceQuest);
    CHECK(strcmp(((CDBRecord*)pObject)->GetTableName(), "_CHARQUEST") == 0);
    CInstanceQuest::classCInstanceQuest.m_pfnDeleteObject(pObject);
}

static void TestSetters()
{
    CInstancePC pc;
    pc.ClearAllDirty();
    pc.HP = 5;
    pc.SetHP(5);                                  // unchanged: no dirty bit
    CHECK(pc.m_qwDirty == 0);
    pc.SetHP(100);
    CHECK(pc.HP == 100 && pc.m_qwDirty == (1 << 6) && pc.GetHP() == 100);
    pc.CharName16 = "a";
    pc.SetColumn(&InstancePCData::CharName16, "abc");   // group 1: no dirty bit
    CHECK(pc.CharName16 == "abc" && pc.m_qwDirty == (1 << 6));

    CInstanceDeletedChar deleted;
    deleted.ClearAllDirty();
    deleted.UserJID = 0;
    deleted.SetColumn(&InstanceDeletedCharData::UserJID, 7);
    CHECK(deleted.UserJID == 7 && deleted.m_qwDirty == 4);

    // the generic setter takes the update group from the column list
    CInstanceCOS cos;
    cos.ClearAllDirty();
    cos.SetColumn(&InstanceCOSData::CharName, std::string("pet"));
    CHECK(cos.CharName == "pet" && cos.m_qwDirty == (1 << 7));

    CInstanceChestInfo chest;
    chest.ChestSize = 250;
    chest.ClearAllDirty();
    chest.AddChestSize(10);                       // BYTE arithmetic
    CHECK(chest.ChestSize == 4 && chest.m_qwDirty == 4);

    CInstanceTimedJob job;
    ITimedJobCommonData* pJob = &job;
    pJob->InitData();
    CHECK(job.m_qwDirty == 0 && job.ID == 0 && job.Serial64 == 0);
    pJob->SetSerial64(0x100000000i64);
    pJob->SetTimeToKeep(3);
    CHECK(pJob->GetSerial64() == 0x100000000i64 && job.m_qwDirty == ((1 << 3) | (1 << 2)));
}

static void TestInitData()
{
    CInstanceCOS cos;
    cos.ID = 1;
    cos.OwnerCharID = 2;
    cos.HP = 3;
    cos.CharName = "pet";
    cos.RentEndTime.year = 2011;
    cos.RentEndTime.fraction = 9;
    cos.m_pRefObj = (RefObjCommonRow*)s_abRefObj;
    cos.ClearAllDirty();
    cos.InitData();
    CHECK(cos.ID == 0 && cos.OwnerCharID == 0 && cos.HP == 0 && cos.CharName.empty());
    CHECK(cos.RentEndTime.year == 0 && cos.RentEndTime.fraction == 9 && cos.m_pRefObj == NULL);
    CHECK(cos.m_qwDirty == 0);

    // InitData without the final reset: the dirty bits stay.
    CInstanceCharTrijob trijob;
    trijob.CharID = 0;
    trijob.JobType = 0;
    trijob.Level = 5;
    trijob.Exp = 0;
    trijob.Contribution = 0;
    trijob.Reward = 0;
    trijob.ClearAllDirty();
    trijob.InitData();
    CHECK(trijob.Level == 0 && trijob.m_qwDirty == (1 << 2));

    CInstanceRentItemInfo rent;
    rent.InitData();
    CHECK(rent.PeriodBeginTime.year == 2000 && rent.PeriodBeginTime.month == 1 &&
          rent.MeterRateTime.day == 1 && rent.m_qwDirty == 0);
}

static void TestCopyAndStreams()
{
    CInstanceBindingOptionWithItem src;
    src.InitData();
    src.nItemDBID = 0x1122334455i64;
    src.bOptType = 2;
    src.nSlot = 1;
    src.nOptID = 0x1234;
    src.nOptLvl = 3;
    src.nOptValue = 77;
    CInstanceBindingOptionWithItem dst;
    dst.InitData();
    src.CopyTo(&dst);
    CHECK(dst.nItemDBID == src.nItemDBID && dst.nOptValue == 77 && dst.m_qwDirty == 4);

    BYTE abBuffer[64];
    CByteStream stream;
    memset(&stream, 0, sizeof(stream));
    stream.m_pBuffer = abBuffer;
    stream.m_nCapacity = sizeof(abBuffer);
    src.Write(&stream, 1);
    CHECK(stream.m_nWritePos == 9 && abBuffer[0] == 1);
    CHECK(*(DWORD*)(abBuffer + 1) == 0x31234 && *(DWORD*)(abBuffer + 5) == 77);

    stream.m_nWritePos = 0;
    src.Write(&stream, 3);
    CHECK(stream.m_nWritePos == 27);
    CInstanceBindingOptionWithItem read;
    read.Read(&stream, 3);
    CHECK(read.nItemDBID == src.nItemDBID && read.nOptLvl == 3 && read.nOptValue == 77);

    // a full stream throws
    stream.m_nWritePos = sizeof(abBuffer) - 2;
    bool bThrown = false;
    try
    {
        stream.Write<DWORD>(1);
    }
    catch (int)
    {
        bThrown = true;
    }
    CHECK(bThrown);

    CInstanceDummy dummy;
    SetRefObjID(-1);
    dummy.ResolveRefObj();
    CHECK(dummy.m_pRefObj == (RefObjCommonRow*)s_abRefObj);
    stream.m_nWritePos = 0;
    dummy.Write(&stream, 0);
    CHECK(*(int*)abBuffer == -1);

    CInstanceCOS cos;
    cos.CharName.clear();
    new (s_abRefObj + FIELD_OFFSET(RefObjCommon, CodeName128)) std::string("COS_PET");
    cos.m_pRefObj = (RefObjCommonRow*)s_abRefObj;
    CHECK(strcmp(cos.GetName(), "COS_PET") == 0);
}

static void SetRefTypeID(int nTypeID2, int nTypeID3, int nTypeID4)
{
    *(WORD*)(s_abRefObj + 0x80) = (WORD)((3 << 2) | (nTypeID2 << 5) | (nTypeID3 << 7) |
                                         (nTypeID4 << 11));
}

static void TestItem()
{
    CHECK(sizeof(CInstanceItem) == 0xD8);
    const CDBTableInfo& info = CInstanceItem::s_TableInfo;
    CHECK(info.m_strTable == "_ITEMS" && info.m_dwFlags == 5 && info.m_nDataOffset == 32);
    CHECK(info.m_pRecordClass == &CInstanceItem::classCInstanceItem);
    CHECK(CInstanceItem::classCInstanceItem.m_pBaseClass == &CInstanceObj::classCInstanceObj);
    CHECK(CheckColumn(info, "ID64", 0x20, 0) && CheckColumn(info, "OptLevel", 0x2C, 3));
    CHECK(CheckColumn(info, "MagParam1", 0x60, 5) && CheckColumn(info, "MagParam12", 0xB8, 5));
    CHECK(CheckColumn(info, "Serial64", 0xC0, 1) && CheckColumn(info, "CreaterName", 0x3C, 1));

    CInstanceItem item;
    item.ID64 = 9;
    item.OptLevel = 4;
    item.CreaterName = "smith";
    item.MagParamNum = 2;
    item.MagParam[0] = 7;
    item.MagParam[1] = 8;
    item.Serial64 = 5;
    item.InitData();                              // the magic parameters stay
    CHECK(item.ID64 == 0 && item.OptLevel == 0 && item.CreaterName.empty());
    CHECK(item.MagParamNum == 0 && item.MagParam[1] == 8 && item.Serial64 == 0);
    CHECK(item.m_pRentItemInfo == NULL && item.m_pRefObj == NULL && item.m_qwDirty == 0);

    // equipment 3-1-6: rent, id, plus, variance, durability, params, binding options
    item.RefItemID = 100;
    item.OptLevel = 4;
    item.Variance = 0x55;
    item.Data = 30;
    item.MagParamNum = 2;
    CInstanceBindingOptionWithItem option;
    option.InitData();
    option.bOptType = 2;
    option.nSlot = 0;
    option.nOptID = 0x10;
    option.nOptValue = 5;
    item.AddBindingOption(2, &option);
    SetRefTypeID(1, 6, 1);
    SetRefObjID(100);
    item.ResolveRefObj();
    CHECK(item.m_pRefObj == (RefObjCommonRow*)s_abRefObj);

    BYTE abBuffer[128];
    CByteStream stream;
    memset(&stream, 0, sizeof(stream));
    stream.m_pBuffer = abBuffer;
    stream.m_nCapacity = sizeof(abBuffer);
    item.Write(&stream, 1);
    CHECK(stream.m_nWritePos == 51 && *(DWORD*)abBuffer == 0 && *(DWORD*)(abBuffer + 4) == 100);
    CHECK(abBuffer[8] == 4 && *(__int64*)(abBuffer + 9) == 0x55 && *(DWORD*)(abBuffer + 17) == 30);
    CHECK(abBuffer[21] == 2 && *(__int64*)(abBuffer + 22) == 7 && *(__int64*)(abBuffer + 30) == 8);
    CHECK(abBuffer[38] == 1 && abBuffer[39] == 0 && abBuffer[40] == 2 && abBuffer[41] == 1);
    CHECK(abBuffer[42] == 0 && *(DWORD*)(abBuffer + 47) == 5);

    // mode 2: plus with the binding option values
    stream.m_nWritePos = 0;
    item.Write(&stream, 2);
    CHECK(stream.m_nWritePos == 5 && abBuffer[4] == 9);

    // mode 3 round trip
    item.CreaterName = "smith";
    item.Serial64 = 0x123456789i64;
    stream.m_nWritePos = 0;
    item.Write(&stream, 3);
    CHECK(stream.m_nWritePos == 4 + 1 + 8 + 4 + 2 + 5 + 8 + 1 + 16);
    CInstanceItem read;
    read.m_pRefObj = NULL;
    read.Read(&stream, 3);
    CHECK(read.RefItemID == 100 && read.Variance == 0x55 && read.CreaterName == "smith");
    CHECK(read.Serial64 == 0x123456789i64 && read.MagParamNum == 2 && read.MagParam[1] == 8);
    CHECK(read.m_pRefObj == (RefObjCommonRow*)s_abRefObj);
    CHECK(stream.m_nReadPos == stream.m_nWritePos);

    // ETC 3-3-8: durability as WORD, then the creator name
    SetRefTypeID(3, 8, 1);
    stream.m_nWritePos = 0;
    item.Write(&stream, 1);
    CHECK(stream.m_nWritePos == 4 + 4 + 2 + 2 + 5 && *(WORD*)(abBuffer + 8) == 30);

    // 3-1-14: Data is an expiry time
    SetRefTypeID(1, 14, 1);
    item.Data = (int)_time32(NULL) + 1000;
    stream.m_nWritePos = 0;
    item.Write(&stream, 1);
    DWORD dwExpiry = *(DWORD*)(abBuffer + 17);
    CHECK((dwExpiry & 0x80000000) != 0 && (dwExpiry & 0x7FFFFFFF) > 990 &&
          (dwExpiry & 0x7FFFFFFF) <= 1000);

    // CopyTo: ID64 becomes 0, the update groups are marked
    CInstanceItem copy;
    copy.InitData();
    item.ID64 = 77;
    item.m_mapBindingOptions.clear();
    item.CopyTo(&copy);
    CHECK(copy.ID64 == 0 && copy.RefItemID == 100 && copy.CreaterName == "smith");
    CHECK(copy.MagParam[0] == 7 && copy.m_pRefObj == item.m_pRefObj);
    CHECK(copy.m_pRentItemInfo == NULL && copy.m_mapBindingOptions.empty());
    CHECK(copy.m_qwDirty == ((1 << 2) | (1 << 3) | (1 << 4) | (1 << 5)));
}

int main()
{
    TestLayouts();
    TestTables();
    TestSetters();
    TestInitData();
    TestCopyAndStreams();
    TestItem();
    return TEST_RESULT("ShardInstanceRecordTest");
}
