// SR_ShardManager instance record classes (SilkroadOnline\Server\ServerCommon\InstanceRecords.h):
// record sizes and member offsets, the table descriptions (names, flags, columns per field
// group), the column setters with their dirty bits, InitData, CopyTo and the stream writers.
// Compiled with SHARD_DEFS.

#include "ServerCommon/InstanceRecords.h"

#include <string.h>

#include <new>

#include "TestCheck.h"

// The reference data of the test: one object row with known values at the offsets the records
// read.
static BYTE s_abRefObj[0x260];
static int s_nLastRefObjID;

RefObjCommonRow* ReferenceData_FindRefObj(int nRefObjID)
{
    s_nLastRefObjID = nRefObjID;
    return (RefObjCommonRow*)s_abRefObj;
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
    pc.SetCharName("abc");                        // group 1: no dirty bit
    CHECK(pc.CharName16 == "abc" && pc.m_qwDirty == (1 << 6));

    CInstanceDeletedChar deleted;
    deleted.ClearAllDirty();
    deleted.UserJID = 0;
    deleted.SetUserJID(7);
    CHECK(deleted.UserJID == 7 && deleted.m_qwDirty == 4);

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
    dummy.ResolveRefObj();
    CHECK(s_nLastRefObjID == -1 && dummy.m_pRefObj == (RefObjCommonRow*)s_abRefObj);
    stream.m_nWritePos = 0;
    dummy.Write(&stream, 0);
    CHECK(*(int*)abBuffer == -1);

    CInstanceCOS cos;
    cos.CharName.clear();
    new (s_abRefObj + 0x14) std::string("COS_PET");
    cos.m_pRefObj = (RefObjCommonRow*)s_abRefObj;
    CHECK(strcmp(cos.GetName(), "COS_PET") == 0);
}

int main()
{
    TestLayouts();
    TestTables();
    TestSetters();
    TestInitData();
    TestCopyAndStreams();
    return TEST_RESULT("ShardInstanceRecordTest");
}
