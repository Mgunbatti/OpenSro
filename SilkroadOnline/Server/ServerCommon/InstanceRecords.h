#pragma once

#include <string.h>

#include <map>
#include <list>
#include <string>

#include "BSLib/DB/Database.h"
#include "ServerCommon/ByteStream.h"
#include "ServerCommon/RecordT.h"

// Instance record classes of SR_ShardManager (ServerCommon; CShardDB::RegisterInstanceTables opens
// one CDBTable per class, CTableKeeper::RegisterTable_<T> gives it a CCustomAllocator<T, ...>).
//
// Every record is a CDBRecordT (RecordT.h) over a struct of its table columns. The table
// description declares each column with its name and member offset in its field group (0: key,
// 1: read only, 2..63: update groups, one dirty bit each); m_nDataOffset is 24, or 32 after the
// CInstanceObj part. The comment before each record lists the original constructor, destructor,
// slot 2 / 3 and table description (StaticInit_TableInfo) addresses.
//
// The columns are changed through setters (inlined everywhere in the original): a new value is
// only stored when it differs; a table without DBTABLE_UPDATE asserts and keeps the old value;
// a column of an update group marks its dirty bit. InitData (slot 5) resets the columns the
// original resets, in its order.

// The parsed _RefObjCommon row of the reference data (SM_ref, not part of this file). The
// instance records only keep a pointer to it and read a few fields by their offsets.
struct RefObjCommonRow;

// RefData_HashFindAt108 (0x42e280) on the CReferenceData singleton (0x8a8944, asserted when not
// created): the object row of an object id, NULL when there is none. Defined by the reference
// data (SM_ref).
RefObjCommonRow* ReferenceData_FindRefObj(int nRefObjID);

inline const std::string& RefObj_GetCodeName(const RefObjCommonRow* pRefObj)     // +0x14
{
    return *(const std::string*)((const BYTE*)pRefObj + 0x14);
}

inline WORD RefObj_GetTypeID(const RefObjCommonRow* pRefObj)                     // +0x80
{
    return *(const WORD*)((const BYTE*)pRefObj + 0x80);
}

inline BYTE RefObj_GetByte198(const RefObjCommonRow* pRefObj)                    // +0x198
{
    return *((const BYTE*)pRefObj + 0x198);
}

inline int RefObj_GetDword250(const RefObjCommonRow* pRefObj)                    // +0x250
{
    return *(const int*)((const BYTE*)pRefObj + 0x250);
}

template <class T>
struct InstanceRecordValue
{
    typedef T Type;
};

// The column setter. nDirtyGroup is the update group whose bit is set (below 2: none); it is the
// column's field group except where the original passes another one.
template <class T>
inline void InstanceRecord_SetColumn(CDBRecord* pRecord, const CDBTableInfo& info, T& member,
                                     typename InstanceRecordValue<T>::Type value,
                                     int nDirtyGroup)
{
    if (member != value)
    {
        if (info.m_dwFlags & DBTABLE_UPDATE)
        {
            if (nDirtyGroup >= 2)
                pRecord->m_qwDirty |= (unsigned __int64)1 << nDirtyGroup;
            member = value;
        }
        else
        {
            BSAssertFail();
        }
    }
}

inline void InstanceRecord_SetColumn(CDBRecord* pRecord, const CDBTableInfo& info,
                                     std::string& member, const std::string& value,
                                     int nDirtyGroup)
{
    if (member.compare(value) != 0)
    {
        if (info.m_dwFlags & DBTABLE_UPDATE)
        {
            member.assign(value, 0, std::string::npos);
            if (nDirtyGroup >= 2)
                pRecord->m_qwDirty |= (unsigned __int64)1 << nDirtyGroup;
        }
        else
        {
            BSAssertFail();
        }
    }
}

// Timestamps compare year .. second (the fraction is ignored) and are copied whole.
inline void InstanceRecord_SetColumn(CDBRecord* pRecord, const CDBTableInfo& info,
                                     TIMESTAMP_STRUCT& member, const TIMESTAMP_STRUCT& value,
                                     int nDirtyGroup)
{
    if (value.year != member.year || value.month != member.month || value.day != member.day ||
        value.hour != member.hour || value.minute != member.minute ||
        value.second != member.second)
    {
        if (info.m_dwFlags & DBTABLE_UPDATE)
        {
            if (nDirtyGroup >= 2)
                pRecord->m_qwDirty |= (unsigned __int64)1 << nDirtyGroup;
            member = value;
        }
        else
        {
            BSAssertFail();
        }
    }
}

// Sets year .. second of a timestamp (the fraction is kept).
inline void InstanceRecord_SetDate(TIMESTAMP_STRUCT& ts, int nYear, int nMonth, int nDay)
{
    ts.year = (SQLSMALLINT)nYear;
    ts.month = (SQLUSMALLINT)nMonth;
    ts.day = (SQLUSMALLINT)nDay;
    ts.hour = 0;
    ts.minute = 0;
    ts.second = 0;
}

#define SR_SET_COLUMN(member, value, group)                                                    \
    InstanceRecord_SetColumn(this, s_TableInfo, member, value, group)
#define SR_COPY_COLUMN(pDest, member, group)                                                   \
    InstanceRecord_SetColumn(pDest, s_TableInfo, (pDest)->member, member, group)


// 0x4a1f10: TypeID 1-2-3-3 (character, NPC, COS, TypeID4 3).
inline BOOL InstanceRecord_IsCOSTypeID4_3(WORD wTypeID)
{
    return (wTypeID & 2) != 0 && (wTypeID & 0x1C) == 4 && (wTypeID & 0x60) == 0x40 &&
           (wTypeID & 0x780) == 0x180 && (wTypeID & 0xF800) == 0x1800;
}

// 0x61b690: TypeID 1-2-3-3 or 1-2-3-4.
inline BOOL InstanceRecord_IsCOSTypeID4_3or4(WORD wTypeID)
{
    if ((wTypeID & 2) == 0 || (wTypeID & 0x1C) != 4 || (wTypeID & 0x60) != 0x40 ||
        (wTypeID & 0x780) != 0x180)
    {
        return FALSE;
    }
    WORD wTypeID4 = (WORD)(wTypeID >> 11);
    return wTypeID4 == 3 || wTypeID4 == 4;
}

// =============================================================================================
// Base classes

// Base of the object records (character, item, structure): the reference object row and the slots
// 9 / 10. RTC 0x7815c4, ctor 0x520ad0, dtor 0x520b80 (slot 1 0x520b60).
class CInstanceObj : public CDBRecord
{
    BS_DECLARE_RUNTIME_CLASS(CInstanceObj)

public:
    CInstanceObj();
    virtual ~CInstanceObj();

    // 0x520b40 / 0x520b50: the base has no table.
    virtual const char* GetTableName();                                     // 2
    virtual CDBTableInfo* GetTableInfo();                                   // 3
    // 0x520b30
    virtual void InitData();                                                // 5
    // Looks the reference object up (asserts that it is not set yet).
    virtual void ResolveRefObj() = 0;                                       // 9
    // Writes the object to a stream; nMode selects the packet layout.
    virtual void Write(CByteStream* pStream, int nMode) = 0;               // 10

public:
    RefObjCommonRow*    m_pRefObj;      // +18
    DWORD               m_dw1C;         // +1C
};

C_ASSERT(FIELD_OFFSET(CInstanceObj, m_pRefObj) == 0x18);
C_ASSERT(sizeof(CInstanceObj) == 0x20);

// Base of the character records (PC, COS, siege fortress objects). RTC 0x7815d8, ctor 0x520bd0,
// dtor 0x520c30 (slot 1 0x520c90).
class CInstanceChar : public CInstanceObj
{
    BS_DECLARE_RUNTIME_CLASS(CInstanceChar)

public:
    CInstanceChar();
    virtual ~CInstanceChar();

    virtual const char* GetName() = 0;                                      // 11
    virtual BYTE GetLevel() = 0;                                            // 12
    // 0x520c80: a value of the reference object row.
    virtual int GetRefObjDword250();                                        // 13
    virtual int GetHP() = 0;                                                // 14
    virtual int GetMP() = 0;                                                // 15
    virtual void SetHP(int nHP) = 0;                                        // 16
    virtual void SetMP(int nMP) = 0;                                        // 17
};

C_ASSERT(sizeof(CInstanceChar) == 0x20);

// Base of the slot records (inventories, chests): the slot number is the only column. RTC
// 0x781614, ctor 0x5321f0, dtor 0x532250 (slot 1 0x5323a0).
class CInstanceSlot : public CDBRecord
{
    BS_DECLARE_RUNTIME_CLASS(CInstanceSlot)

public:
    CInstanceSlot();
    virtual ~CInstanceSlot();

    virtual BYTE GetSlot() = 0;                                             // 9
};

C_ASSERT(sizeof(CInstanceSlot) == 0x18);

// Interface of the timed job records (no runtime class of its own: slot 0 is CDBRecord's).
// ctor 0x5322f0, dtor 0x532350 (slot 1 0x5323c0).
class ITimedJobCommonData : public CDBRecord
{
public:
    ITimedJobCommonData();
    virtual ~ITimedJobCommonData();

    // 0x524830: every column to zero through the setters, then the dirty bits.
    virtual void InitData();                                                // 5
    virtual void SetID(int nID) = 0;                                        // 9
    virtual void SetCharID(int nCharID) = 0;                                // 10
    virtual void SetCategory(BYTE byCategory) = 0;                          // 11
    virtual void SetJobID(int nJobID) = 0;                                  // 12
    virtual void SetTimeToKeep(int nTimeToKeep) = 0;                        // 13
    virtual void SetData1(int nData) = 0;                                   // 14
    virtual void SetData2(int nData) = 0;                                   // 15
    virtual void SetData3(int nData) = 0;                                   // 16
    virtual void SetData4(int nData) = 0;                                   // 17
    virtual void SetData5(int nData) = 0;                                   // 18
    virtual void SetData6(int nData) = 0;                                   // 19
    virtual void SetData7(int nData) = 0;                                   // 20
    virtual void SetData8(int nData) = 0;                                   // 21
    virtual void SetSerial64(__int64 nSerial) = 0;                          // 22
    virtual void SetJID(int nJID) = 0;                                      // 23
    virtual int GetID() = 0;                                                // 24
    virtual int GetCharID() = 0;                                            // 25
    virtual BYTE GetCategory() = 0;                                         // 26
    virtual int GetJobID() = 0;                                             // 27
    virtual int GetTimeToKeep() = 0;                                        // 28
    virtual int GetData1() = 0;                                             // 29
    virtual int GetData2() = 0;                                             // 30
    virtual int GetData3() = 0;                                             // 31
    virtual int GetData4() = 0;                                             // 32
    virtual int GetData5() = 0;                                             // 33
    virtual int GetData6() = 0;                                             // 34
    virtual int GetData7() = 0;                                             // 35
    virtual int GetData8() = 0;                                             // 36
    virtual __int64 GetSerial64() = 0;                                      // 37
    virtual int GetJID() = 0;                                               // 38
};

C_ASSERT(sizeof(ITimedJobCommonData) == 0x18);

// =============================================================================================
// Character, object and slot records

// _USER
struct InstanceUserData
{
    int              UserJID;               // +18
    int              CharID;                // +1C
};

class CInstanceUser : public CDBRecordT<CInstanceUser, CDBRecord, InstanceUserData>
{
    SR_RECORD_MEMBERS(CInstanceUser)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceUser, UserJID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceUser, CharID) == 0x1C);
C_ASSERT(sizeof(CInstanceUser) == 0x20);

// _CHAR
struct InstancePCData
{
    int              CharID;                // +20
    BYTE             Deleted;               // +24
    int              RefObjID;              // +28
    std::string      CharName16;            // +2C
    std::string      NickName16;            // +48
    BYTE             Scale;                 // +64
    BYTE             CurLevel;              // +65
    BYTE             MaxLevel;              // +66
    __int64          ExpOffset;             // +68
    int              SExpOffset;            // +70
    short            Strength;              // +74
    short            Intellect;             // +76
    __int64          RemainGold;            // +78
    int              RemainSkillPoint;      // +80
    short            RemainStatPoint;       // +84
    BYTE             RemainHwanCount;       // +86
    int              GatheredExpPoint;      // +88
    int              HP;                    // +8C
    int              MP;                    // +90
    short            LatestRegion;          // +94
    float            PosX;                  // +98
    float            PosY;                  // +9C
    float            PosZ;                  // +A0
    int              AppointedTeleport;     // +A4
    BYTE             AutoInvestExp;         // +A8
    int              InventorySize;         // +AC
    BYTE             DailyPK;               // +B0
    short            TotalPK;               // +B2
    int              PKPenaltyPoint;        // +B4
    int              GuildID;               // +B8
    TIMESTAMP_STRUCT LastLogout;            // +BC
    short            TelRegion;             // +CC
    float            TelPosX;               // +D0
    float            TelPosY;               // +D4
    float            TelPosZ;               // +D8
    short            DiedRegion;            // +DC
    float            DiedPosX;              // +E0
    float            DiedPosY;              // +E4
    float            DiedPosZ;              // +E8
    short            WorldID;               // +EC
    short            TelWorldID;            // +EE
    short            DiedWorldID;           // +F0
    BYTE             HwanLevel;             // +F2
};

class CInstancePC : public CDBRecordT<CInstancePC, CInstanceChar, InstancePCData>
{
    SR_RECORD_MEMBERS(CInstancePC)

public:
    virtual void InitData();                                                // 5
    // 0x520ce0: a PC record is never copied.
    virtual void CopyTo(CDBRecord* pDest);                                  // 8
    virtual void ResolveRefObj();                                           // 9
    virtual void Write(CByteStream* pStream, int nMode);                    // 10
    virtual const char* GetName();                                          // 11
    virtual BYTE GetLevel();                                                // 12
    virtual int GetHP();                                                    // 14
    virtual int GetMP();                                                    // 15
    virtual void SetHP(int nHP);                                            // 16
    virtual void SetMP(int nMP);                                            // 17
    virtual const char* GetNickName();                                      // 18

    // 0x429150 (CInstanceChar::SetCharName of the analysis)
    void SetCharName(std::string strName);
};

C_ASSERT(FIELD_OFFSET(CInstancePC, CharID) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstancePC, Deleted) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstancePC, RefObjID) == 0x28);
C_ASSERT(FIELD_OFFSET(CInstancePC, CharName16) == 0x2C);
C_ASSERT(FIELD_OFFSET(CInstancePC, NickName16) == 0x48);
C_ASSERT(FIELD_OFFSET(CInstancePC, Scale) == 0x64);
C_ASSERT(FIELD_OFFSET(CInstancePC, CurLevel) == 0x65);
C_ASSERT(FIELD_OFFSET(CInstancePC, MaxLevel) == 0x66);
C_ASSERT(FIELD_OFFSET(CInstancePC, ExpOffset) == 0x68);
C_ASSERT(FIELD_OFFSET(CInstancePC, SExpOffset) == 0x70);
C_ASSERT(FIELD_OFFSET(CInstancePC, Strength) == 0x74);
C_ASSERT(FIELD_OFFSET(CInstancePC, Intellect) == 0x76);
C_ASSERT(FIELD_OFFSET(CInstancePC, RemainGold) == 0x78);
C_ASSERT(FIELD_OFFSET(CInstancePC, RemainSkillPoint) == 0x80);
C_ASSERT(FIELD_OFFSET(CInstancePC, RemainStatPoint) == 0x84);
C_ASSERT(FIELD_OFFSET(CInstancePC, RemainHwanCount) == 0x86);
C_ASSERT(FIELD_OFFSET(CInstancePC, GatheredExpPoint) == 0x88);
C_ASSERT(FIELD_OFFSET(CInstancePC, HP) == 0x8C);
C_ASSERT(FIELD_OFFSET(CInstancePC, MP) == 0x90);
C_ASSERT(FIELD_OFFSET(CInstancePC, LatestRegion) == 0x94);
C_ASSERT(FIELD_OFFSET(CInstancePC, PosX) == 0x98);
C_ASSERT(FIELD_OFFSET(CInstancePC, PosY) == 0x9C);
C_ASSERT(FIELD_OFFSET(CInstancePC, PosZ) == 0xA0);
C_ASSERT(FIELD_OFFSET(CInstancePC, AppointedTeleport) == 0xA4);
C_ASSERT(FIELD_OFFSET(CInstancePC, AutoInvestExp) == 0xA8);
C_ASSERT(FIELD_OFFSET(CInstancePC, InventorySize) == 0xAC);
C_ASSERT(FIELD_OFFSET(CInstancePC, DailyPK) == 0xB0);
C_ASSERT(FIELD_OFFSET(CInstancePC, TotalPK) == 0xB2);
C_ASSERT(FIELD_OFFSET(CInstancePC, PKPenaltyPoint) == 0xB4);
C_ASSERT(FIELD_OFFSET(CInstancePC, GuildID) == 0xB8);
C_ASSERT(FIELD_OFFSET(CInstancePC, LastLogout) == 0xBC);
C_ASSERT(FIELD_OFFSET(CInstancePC, TelRegion) == 0xCC);
C_ASSERT(FIELD_OFFSET(CInstancePC, TelPosX) == 0xD0);
C_ASSERT(FIELD_OFFSET(CInstancePC, TelPosY) == 0xD4);
C_ASSERT(FIELD_OFFSET(CInstancePC, TelPosZ) == 0xD8);
C_ASSERT(FIELD_OFFSET(CInstancePC, DiedRegion) == 0xDC);
C_ASSERT(FIELD_OFFSET(CInstancePC, DiedPosX) == 0xE0);
C_ASSERT(FIELD_OFFSET(CInstancePC, DiedPosY) == 0xE4);
C_ASSERT(FIELD_OFFSET(CInstancePC, DiedPosZ) == 0xE8);
C_ASSERT(FIELD_OFFSET(CInstancePC, WorldID) == 0xEC);
C_ASSERT(FIELD_OFFSET(CInstancePC, TelWorldID) == 0xEE);
C_ASSERT(FIELD_OFFSET(CInstancePC, DiedWorldID) == 0xF0);
C_ASSERT(FIELD_OFFSET(CInstancePC, HwanLevel) == 0xF2);
C_ASSERT(sizeof(CInstancePC) == 0xF8);

// _CHARCOS
struct InstanceCOSData
{
    int              ID;                    // +20
    int              OwnerCharID;           // +24
    int              RefCharID;             // +28
    int              HP;                    // +2C
    int              MP;                    // +30
    int              KeeperNPC;             // +34
    BYTE             State;                 // +38
    std::string      CharName;              // +3C
    BYTE             Lvl;                   // +58
    __int64          ExpOffset;             // +60
    short            HGP;                   // +68
    int              PetOption;             // +6C
    TIMESTAMP_STRUCT RentEndTime;           // +70
};

class CInstanceCOS : public CDBRecordT<CInstanceCOS, CInstanceChar, InstanceCOSData>
{
    SR_RECORD_MEMBERS(CInstanceCOS)

public:
    virtual void InitData();                                                // 5
    // 0x522ee0: copies the columns through the setters of pDest.
    virtual void CopyTo(CDBRecord* pDest);                                  // 8
    virtual void ResolveRefObj();                                           // 9
    virtual void Write(CByteStream* pStream, int nMode);                    // 10
    virtual const char* GetName();                                          // 11
    virtual BYTE GetLevel();                                                // 12
    virtual int GetHP();                                                    // 14
    virtual int GetMP();                                                    // 15
    virtual void SetHP(int nHP);                                            // 16
    virtual void SetMP(int nMP);                                            // 17

    // 0x522aa0 / 0x522b50 (SetName / SetTime_112 of the analysis)
    void SetCharName(std::string strName);
    void SetRentEndTime(TIMESTAMP_STRUCT tsTime);
};

C_ASSERT(FIELD_OFFSET(CInstanceCOS, ID) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceCOS, OwnerCharID) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceCOS, RefCharID) == 0x28);
C_ASSERT(FIELD_OFFSET(CInstanceCOS, HP) == 0x2C);
C_ASSERT(FIELD_OFFSET(CInstanceCOS, MP) == 0x30);
C_ASSERT(FIELD_OFFSET(CInstanceCOS, KeeperNPC) == 0x34);
C_ASSERT(FIELD_OFFSET(CInstanceCOS, State) == 0x38);
C_ASSERT(FIELD_OFFSET(CInstanceCOS, CharName) == 0x3C);
C_ASSERT(FIELD_OFFSET(CInstanceCOS, Lvl) == 0x58);
C_ASSERT(FIELD_OFFSET(CInstanceCOS, ExpOffset) == 0x60);
C_ASSERT(FIELD_OFFSET(CInstanceCOS, HGP) == 0x68);
C_ASSERT(FIELD_OFFSET(CInstanceCOS, PetOption) == 0x6C);
C_ASSERT(FIELD_OFFSET(CInstanceCOS, RentEndTime) == 0x70);
C_ASSERT(sizeof(CInstanceCOS) == 0x80);

// _STRUCT
struct InstanceStructData
{
    InstanceStructData();

    int              m_nRefObjID;           // +20
    DWORD            m_dw24;                // +24
};

class CInstanceStruct : public CDBRecordT<CInstanceStruct, CInstanceObj, InstanceStructData>
{
    SR_RECORD_MEMBERS(CInstanceStruct)

public:
    virtual void InitData();                                                // 5
    virtual void ResolveRefObj();                                           // 9
    virtual void Write(CByteStream* pStream, int nMode);                    // 10
};

C_ASSERT(FIELD_OFFSET(CInstanceStruct, m_nRefObjID) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceStruct, m_dw24) == 0x24);
C_ASSERT(sizeof(CInstanceStruct) == 0x28);

// _DUMMY
class CInstanceDummy : public CDBRecordT<CInstanceDummy, CInstanceObj, void>
{
    SR_RECORD_MEMBERS(CInstanceDummy)

public:
    virtual void ResolveRefObj();                                           // 9
    virtual void Write(CByteStream* pStream, int nMode);                    // 10
};

C_ASSERT(sizeof(CInstanceDummy) == 0x20);

// _EVENTDAEMON
class CInstanceEventDaemon : public CDBRecordT<CInstanceEventDaemon, CInstanceObj, void>
{
    SR_RECORD_MEMBERS(CInstanceEventDaemon)

public:
    virtual void ResolveRefObj();                                           // 9
    virtual void Write(CByteStream* pStream, int nMode);                    // 10
};

C_ASSERT(sizeof(CInstanceEventDaemon) == 0x20);

// _INVENTORY
struct InstanceInventoryData
{
    BYTE             Slot;                  // +18
};

class CInstanceInventory : public CDBRecordT<CInstanceInventory, CInstanceSlot, InstanceInventoryData>
{
    SR_RECORD_MEMBERS(CInstanceInventory)

public:
    // 0x523780 / 0x5237c0 (shared by the slot records)
    virtual void InitData();                                                // 5
    virtual BYTE GetSlot();                                                 // 9
};

C_ASSERT(FIELD_OFFSET(CInstanceInventory, Slot) == 0x18);
C_ASSERT(sizeof(CInstanceInventory) == 0x20);

// _INVENTORYFORAVATAR
struct InstanceInventoryForAvatarData
{
    BYTE             Slot;                  // +18
};

class CInstanceInventoryForAvatar : public CDBRecordT<CInstanceInventoryForAvatar, CInstanceSlot, InstanceInventoryForAvatarData>
{
    SR_RECORD_MEMBERS(CInstanceInventoryForAvatar)

public:
    // 0x523780 / 0x5237c0 (shared by the slot records)
    virtual void InitData();                                                // 5
    virtual BYTE GetSlot();                                                 // 9
};

C_ASSERT(FIELD_OFFSET(CInstanceInventoryForAvatar, Slot) == 0x18);
C_ASSERT(sizeof(CInstanceInventoryForAvatar) == 0x20);

// _INVENTORYFORLINKEDSTORAGE
struct InstanceInventoryForLinkedStorageData
{
    BYTE             Slot;                  // +18
};

class CInstanceInventoryForLinkedStorage : public CDBRecordT<CInstanceInventoryForLinkedStorage, CInstanceSlot, InstanceInventoryForLinkedStorageData>
{
    SR_RECORD_MEMBERS(CInstanceInventoryForLinkedStorage)

public:
    // 0x523780 / 0x5237c0 (shared by the slot records)
    virtual void InitData();                                                // 5
    virtual BYTE GetSlot();                                                 // 9
};

C_ASSERT(FIELD_OFFSET(CInstanceInventoryForLinkedStorage, Slot) == 0x18);
C_ASSERT(sizeof(CInstanceInventoryForLinkedStorage) == 0x20);

// _CHEST
struct InstanceChestData
{
    BYTE             Slot;                  // +18
};

class CInstanceChest : public CDBRecordT<CInstanceChest, CInstanceSlot, InstanceChestData>
{
    SR_RECORD_MEMBERS(CInstanceChest)

public:
    // 0x523780 / 0x5237c0 (shared by the slot records)
    virtual void InitData();                                                // 5
    virtual BYTE GetSlot();                                                 // 9
};

C_ASSERT(FIELD_OFFSET(CInstanceChest, Slot) == 0x18);
C_ASSERT(sizeof(CInstanceChest) == 0x20);

// _INVCOS
struct InstanceInvCOSData
{
    BYTE             Slot;                  // +18
};

class CInstanceInvCOS : public CDBRecordT<CInstanceInvCOS, CInstanceSlot, InstanceInvCOSData>
{
    SR_RECORD_MEMBERS(CInstanceInvCOS)

public:
    // 0x523780 / 0x5237c0 (shared by the slot records)
    virtual void InitData();                                                // 5
    virtual BYTE GetSlot();                                                 // 9
};

C_ASSERT(FIELD_OFFSET(CInstanceInvCOS, Slot) == 0x18);
C_ASSERT(sizeof(CInstanceInvCOS) == 0x20);

// _GUILDCHEST
struct InstanceGuildChestData
{
    BYTE             Slot;                  // +18
};

class CInstanceGuildChest : public CDBRecordT<CInstanceGuildChest, CInstanceSlot, InstanceGuildChestData>
{
    SR_RECORD_MEMBERS(CInstanceGuildChest)

public:
    // 0x523780 / 0x5237c0 (shared by the slot records)
    virtual void InitData();                                                // 5
    virtual BYTE GetSlot();                                                 // 9
};

C_ASSERT(FIELD_OFFSET(CInstanceGuildChest, Slot) == 0x18);
C_ASSERT(sizeof(CInstanceGuildChest) == 0x20);

// _ITEMQUOTATION
struct InstanceQuotData
{
    int              ID;                    // +18
    int              Service;               // +1C
    int              AssocNPC;              // +20
    int              RefItemID;             // +24
    float            BaseQuot;              // +28
    float            Quot_LB;               // +2C
    float            Quot_UB;               // +30
    int              BaseStockAmount;       // +34
    int              FluctuateAmount;       // +38
    int              CurStockAmount;        // +3C
};

class CInstanceQuot : public CDBRecordT<CInstanceQuot, CDBRecord, InstanceQuotData>
{
    SR_RECORD_MEMBERS(CInstanceQuot)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceQuot, ID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceQuot, Service) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceQuot, AssocNPC) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceQuot, RefItemID) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceQuot, BaseQuot) == 0x28);
C_ASSERT(FIELD_OFFSET(CInstanceQuot, Quot_LB) == 0x2C);
C_ASSERT(FIELD_OFFSET(CInstanceQuot, Quot_UB) == 0x30);
C_ASSERT(FIELD_OFFSET(CInstanceQuot, BaseStockAmount) == 0x34);
C_ASSERT(FIELD_OFFSET(CInstanceQuot, FluctuateAmount) == 0x38);
C_ASSERT(FIELD_OFFSET(CInstanceQuot, CurStockAmount) == 0x3C);
C_ASSERT(sizeof(CInstanceQuot) == 0x40);

// _CHARSKILL
struct InstanceSkillData
{
    int              CharID;                // +18
    int              SkillID;               // +1C
    BYTE             Enable;                // +20
};

class CInstanceSkill : public CDBRecordT<CInstanceSkill, CDBRecord, InstanceSkillData>
{
    SR_RECORD_MEMBERS(CInstanceSkill)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceSkill, CharID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceSkill, SkillID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceSkill, Enable) == 0x20);
C_ASSERT(sizeof(CInstanceSkill) == 0x28);

// _CHARSKILLMASTERY
struct InstanceSkillMasteryData
{
    int              CharID;                // +18
    int              MasteryID;             // +1C
    BYTE             Level;                 // +20
};

class CInstanceSkillMastery : public CDBRecordT<CInstanceSkillMastery, CDBRecord, InstanceSkillMasteryData>
{
    SR_RECORD_MEMBERS(CInstanceSkillMastery)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceSkillMastery, CharID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceSkillMastery, MasteryID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceSkillMastery, Level) == 0x20);
C_ASSERT(sizeof(CInstanceSkillMastery) == 0x28);

// _CHARQUEST
struct InstanceQuestData
{
    int              CharID;                // +18
    int              QuestID;               // +1C
    BYTE             Status;                // +20
    short            AchievementCount;      // +22
    TIMESTAMP_STRUCT StartTime;             // +24
    TIMESTAMP_STRUCT EndTime;               // +34
    __int64          QuestData1;            // +48
    __int64          QuestData2;            // +50
};

class CInstanceQuest : public CDBRecordT<CInstanceQuest, CDBRecord, InstanceQuestData>
{
    SR_RECORD_MEMBERS(CInstanceQuest)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceQuest, CharID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceQuest, QuestID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceQuest, Status) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceQuest, AchievementCount) == 0x22);
C_ASSERT(FIELD_OFFSET(CInstanceQuest, StartTime) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceQuest, EndTime) == 0x34);
C_ASSERT(FIELD_OFFSET(CInstanceQuest, QuestData1) == 0x48);
C_ASSERT(FIELD_OFFSET(CInstanceQuest, QuestData2) == 0x50);
C_ASSERT(sizeof(CInstanceQuest) == 0x58);

// _STATICAVATAR
struct InstanceStaticAvatarData
{
    int              CharID;                // +18
    int              Param1;                // +1C
};

class CInstanceStaticAvatar : public CDBRecordT<CInstanceStaticAvatar, CDBRecord, InstanceStaticAvatarData>
{
    SR_RECORD_MEMBERS(CInstanceStaticAvatar)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceStaticAvatar, CharID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceStaticAvatar, Param1) == 0x1C);
C_ASSERT(sizeof(CInstanceStaticAvatar) == 0x20);

// _CharInstanceWorldData
struct CharInstanceGameWorldData
{
    int              CharID;                // +18
    int              DungeonKeyID;          // +1C
    int              WorldID;               // +20
    int              LayerID;               // +24
    TIMESTAMP_STRUCT OpenedTime;            // +28
    short            RegionID;              // +38
    int              PosX;                  // +3C
    int              PosY;                  // +40
    int              PosZ;                  // +44
    BYTE             IsActivated;           // +48
    int              EnterCount;            // +4C
    TIMESTAMP_STRUCT LastEnterTime;         // +50
};

class CCharInstanceGameWorldData : public CDBRecordT<CCharInstanceGameWorldData, CDBRecord, CharInstanceGameWorldData>
{
    SR_RECORD_MEMBERS(CCharInstanceGameWorldData)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CCharInstanceGameWorldData, CharID) == 0x18);
C_ASSERT(FIELD_OFFSET(CCharInstanceGameWorldData, DungeonKeyID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CCharInstanceGameWorldData, WorldID) == 0x20);
C_ASSERT(FIELD_OFFSET(CCharInstanceGameWorldData, LayerID) == 0x24);
C_ASSERT(FIELD_OFFSET(CCharInstanceGameWorldData, OpenedTime) == 0x28);
C_ASSERT(FIELD_OFFSET(CCharInstanceGameWorldData, RegionID) == 0x38);
C_ASSERT(FIELD_OFFSET(CCharInstanceGameWorldData, PosX) == 0x3C);
C_ASSERT(FIELD_OFFSET(CCharInstanceGameWorldData, PosY) == 0x40);
C_ASSERT(FIELD_OFFSET(CCharInstanceGameWorldData, PosZ) == 0x44);
C_ASSERT(FIELD_OFFSET(CCharInstanceGameWorldData, IsActivated) == 0x48);
C_ASSERT(FIELD_OFFSET(CCharInstanceGameWorldData, EnterCount) == 0x4C);
C_ASSERT(FIELD_OFFSET(CCharInstanceGameWorldData, LastEnterTime) == 0x50);
C_ASSERT(sizeof(CCharInstanceGameWorldData) == 0x60);

// _CharTrijob
struct InstanceCharTrijobData
{
    int              CharID;                // +18
    BYTE             JobType;               // +1C
    BYTE             Level;                 // +1D
    int              Exp;                   // +20
    int              Contribution;          // +24
    int              Reward;                // +28
};

class CInstanceCharTrijob : public CDBRecordT<CInstanceCharTrijob, CDBRecord, InstanceCharTrijobData>
{
    SR_RECORD_MEMBERS(CInstanceCharTrijob)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceCharTrijob, CharID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceCharTrijob, JobType) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceCharTrijob, Level) == 0x1D);
C_ASSERT(FIELD_OFFSET(CInstanceCharTrijob, Exp) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceCharTrijob, Contribution) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceCharTrijob, Reward) == 0x28);
C_ASSERT(sizeof(CInstanceCharTrijob) == 0x30);

// _CharTrijobSafeTrade
struct InstanceCharTrijobSafeTradeData
{
    int              CharID;                // +18
    int              AbleCount;             // +1C
    int              Status;                // +20
    TIMESTAMP_STRUCT LastSafeTrade;         // +24
};

class CInstanceCharTrijobSafeTrade : public CDBRecordT<CInstanceCharTrijobSafeTrade, CDBRecord, InstanceCharTrijobSafeTradeData>
{
    SR_RECORD_MEMBERS(CInstanceCharTrijobSafeTrade)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceCharTrijobSafeTrade, CharID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceCharTrijobSafeTrade, AbleCount) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceCharTrijobSafeTrade, Status) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceCharTrijobSafeTrade, LastSafeTrade) == 0x24);
C_ASSERT(sizeof(CInstanceCharTrijobSafeTrade) == 0x38);

// _ClientConfig
struct InstanceClientConfigData
{
    int              CharID;                // +18
    BYTE             ConfigType;            // +1C
    BYTE             SlotSeq;               // +1D
    BYTE             SlotType;              // +1E
    int              Data;                  // +20
};

SR_RECORD(CInstanceClientConfig, CDBRecord, InstanceClientConfigData)

C_ASSERT(FIELD_OFFSET(CInstanceClientConfig, CharID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceClientConfig, ConfigType) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceClientConfig, SlotSeq) == 0x1D);
C_ASSERT(FIELD_OFFSET(CInstanceClientConfig, SlotType) == 0x1E);
C_ASSERT(FIELD_OFFSET(CInstanceClientConfig, Data) == 0x20);
C_ASSERT(sizeof(CInstanceClientConfig) == 0x28);

// _BlockedWhisperers
struct InstanceBlockedWhispererEntityData
{
    int              OwnerID;               // +18
    std::string      TargetName;            // +1C
};

SR_RECORD(CInstanceBlockedWhispererEntity, CDBRecord, InstanceBlockedWhispererEntityData)

C_ASSERT(FIELD_OFFSET(CInstanceBlockedWhispererEntity, OwnerID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceBlockedWhispererEntity, TargetName) == 0x1C);
C_ASSERT(sizeof(CInstanceBlockedWhispererEntity) == 0x38);

// _DELETEDCHAR
struct InstanceDeletedCharData
{
    int              CharID;                // +18
    int              UserJID;               // +1C
    TIMESTAMP_STRUCT DeletedDate;           // +20
};

class CInstanceDeletedChar : public CDBRecordT<CInstanceDeletedChar, CDBRecord, InstanceDeletedCharData>
{
    SR_RECORD_MEMBERS(CInstanceDeletedChar)

public:
    virtual void InitData();                                                // 5

    // 0x40bb30 (DBRecord_SetField1C_Dirty4 of the analysis)
    void SetUserJID(int nUserJID);
};

C_ASSERT(FIELD_OFFSET(CInstanceDeletedChar, CharID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceDeletedChar, UserJID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceDeletedChar, DeletedDate) == 0x20);
C_ASSERT(sizeof(CInstanceDeletedChar) == 0x30);

// _CharCollectionBook
struct InstanceCollectionBookData
{
    int              CharID;                // +18
    int              ThemeID;               // +1C
    int              SlotIndex;             // +20
    TIMESTAMP_STRUCT RegDate;               // +24
};

SR_RECORD(CInstanceCollectionBook, CDBRecord, InstanceCollectionBookData)

C_ASSERT(FIELD_OFFSET(CInstanceCollectionBook, CharID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceCollectionBook, ThemeID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceCollectionBook, SlotIndex) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceCollectionBook, RegDate) == 0x24);
C_ASSERT(sizeof(CInstanceCollectionBook) == 0x38);

// _AssociationReputation
struct InstanceAssociatedReputationData
{
    std::string      AssociationCodeName;   // +18
    int              Reputation;            // +34
    int              PriorOccupation;       // +38
};

class CInstanceAssociatedReputation : public CDBRecordT<CInstanceAssociatedReputation, CDBRecord, InstanceAssociatedReputationData>
{
    SR_RECORD_MEMBERS(CInstanceAssociatedReputation)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceAssociatedReputation, AssociationCodeName) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceAssociatedReputation, Reputation) == 0x34);
C_ASSERT(FIELD_OFFSET(CInstanceAssociatedReputation, PriorOccupation) == 0x38);
C_ASSERT(sizeof(CInstanceAssociatedReputation) == 0x40);

// _ChestInfo
struct InstanceChestInfoData
{
    int              JID;                   // +18
    BYTE             ChestSize;             // +1C
};

class CInstanceChestInfo : public CDBRecordT<CInstanceChestInfo, CDBRecord, InstanceChestInfoData>
{
    SR_RECORD_MEMBERS(CInstanceChestInfo)

public:
    virtual void InitData();                                                // 5
    // 0x525a00: adds to the chest size (BYTE arithmetic).
    virtual void AddChestSize(BYTE byAdd);                                  // 9
};

C_ASSERT(FIELD_OFFSET(CInstanceChestInfo, JID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceChestInfo, ChestSize) == 0x1C);
C_ASSERT(sizeof(CInstanceChestInfo) == 0x20);

// _BindingOptionWithItem
struct InstanceBindingOptionWithItemData
{
    __int64          nItemDBID;             // +18
    BYTE             bOptType;              // +20
    BYTE             nSlot;                 // +21
    int              nOptID;                // +24
    BYTE             nOptLvl;               // +28
    int              nOptValue;             // +2C
    int              nParam1;               // +30
    int              nParam2;               // +34
};

class CInstanceBindingOptionWithItem : public CDBRecordT<CInstanceBindingOptionWithItem, CDBRecord, InstanceBindingOptionWithItemData>
{
    SR_RECORD_MEMBERS(CInstanceBindingOptionWithItem)

public:
    virtual void InitData();                                                // 5
    // 0x5214b0: copies the columns through the setters of pDest.
    virtual void CopyTo(CDBRecord* pDest);                                  // 8
    virtual void Write(CByteStream* pStream, int nMode);                    // 9
    virtual void Read(CByteStream* pStream, int nMode);                     // 10
    // 0x524f10 (shared)
    virtual BYTE GetOptType();                                              // 11
};

C_ASSERT(FIELD_OFFSET(CInstanceBindingOptionWithItem, nItemDBID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceBindingOptionWithItem, bOptType) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceBindingOptionWithItem, nSlot) == 0x21);
C_ASSERT(FIELD_OFFSET(CInstanceBindingOptionWithItem, nOptID) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceBindingOptionWithItem, nOptLvl) == 0x28);
C_ASSERT(FIELD_OFFSET(CInstanceBindingOptionWithItem, nOptValue) == 0x2C);
C_ASSERT(FIELD_OFFSET(CInstanceBindingOptionWithItem, nParam1) == 0x30);
C_ASSERT(FIELD_OFFSET(CInstanceBindingOptionWithItem, nParam2) == 0x34);
C_ASSERT(sizeof(CInstanceBindingOptionWithItem) == 0x38);

// _RentItemInfo
struct InstanceRentItemInfoData
{
    __int64          nItemDBID;             // +18
    int              nRentType;             // +20
    short            nCanDelete;            // +24
    short            nCanRecharge;          // +26
    TIMESTAMP_STRUCT PeriodBeginTime;       // +28
    TIMESTAMP_STRUCT PeriodEndTime;         // +38
    TIMESTAMP_STRUCT MeterRateTime;         // +48
    short            nPackingState;         // +58
    int              nPackingTime;          // +5C
};

class CInstanceRentItemInfo : public CDBRecordT<CInstanceRentItemInfo, CDBRecord, InstanceRentItemInfoData>
{
    SR_RECORD_MEMBERS(CInstanceRentItemInfo)

public:
    virtual void InitData();                                                // 5
    // 0x521b00: copies the columns through the setters of pDest.
    virtual void CopyTo(CDBRecord* pDest);                                  // 8
    virtual void Write(CByteStream* pStream, int nMode);                    // 9
    virtual void Read(CByteStream* pStream, int nMode);                     // 10

    // 0x521840 / 0x5218c0 / 0x521940 (SetTime_40 / _56 / _72 of the analysis)
    void SetPeriodBeginTime(TIMESTAMP_STRUCT tsTime);
    void SetPeriodEndTime(TIMESTAMP_STRUCT tsTime);
    void SetMeterRateTime(TIMESTAMP_STRUCT tsTime);
};

C_ASSERT(FIELD_OFFSET(CInstanceRentItemInfo, nItemDBID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceRentItemInfo, nRentType) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceRentItemInfo, nCanDelete) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceRentItemInfo, nCanRecharge) == 0x26);
C_ASSERT(FIELD_OFFSET(CInstanceRentItemInfo, PeriodBeginTime) == 0x28);
C_ASSERT(FIELD_OFFSET(CInstanceRentItemInfo, PeriodEndTime) == 0x38);
C_ASSERT(FIELD_OFFSET(CInstanceRentItemInfo, MeterRateTime) == 0x48);
C_ASSERT(FIELD_OFFSET(CInstanceRentItemInfo, nPackingState) == 0x58);
C_ASSERT(FIELD_OFFSET(CInstanceRentItemInfo, nPackingTime) == 0x5C);
C_ASSERT(sizeof(CInstanceRentItemInfo) == 0x60);

// _TIMEDJOB
struct InstanceTimedJobData
{
    int              ID;                    // +18
    int              CharID;                // +1C
    BYTE             Category;              // +20
    int              JobID;                 // +24
    int              TimeToKeep;            // +28
    int              Data1;                 // +2C
    int              Data2;                 // +30
    int              Data3;                 // +34
    int              Data4;                 // +38
    int              Data5;                 // +3C
    int              Data6;                 // +40
    int              Data7;                 // +44
    int              Data8;                 // +48
    __int64          Serial64;              // +50
    int              JID;                   // +58
};

class CInstanceTimedJob : public CDBRecordT<CInstanceTimedJob, ITimedJobCommonData, InstanceTimedJobData>
{
    SR_RECORD_MEMBERS(CInstanceTimedJob)

public:
    // 0x524c20: jumps to ITimedJobCommonData::InitData.
    virtual void InitData();                                                // 5
    virtual void SetID(int nID);                                            // 9
    virtual void SetCharID(int nCharID);                                    // 10
    virtual void SetCategory(BYTE byCategory);                              // 11
    virtual void SetJobID(int nJobID);                                      // 12
    virtual void SetTimeToKeep(int nTimeToKeep);                            // 13
    virtual void SetData1(int nData);                                       // 14
    virtual void SetData2(int nData);                                       // 15
    virtual void SetData3(int nData);                                       // 16
    virtual void SetData4(int nData);                                       // 17
    virtual void SetData5(int nData);                                       // 18
    virtual void SetData6(int nData);                                       // 19
    virtual void SetData7(int nData);                                       // 20
    virtual void SetData8(int nData);                                       // 21
    virtual void SetSerial64(__int64 nSerial);                              // 22
    virtual void SetJID(int nJID);                                          // 23
    virtual int GetID();                                                    // 24
    virtual int GetCharID();                                                // 25
    virtual BYTE GetCategory();                                             // 26
    virtual int GetJobID();                                                 // 27
    virtual int GetTimeToKeep();                                            // 28
    virtual int GetData1();                                                 // 29
    virtual int GetData2();                                                 // 30
    virtual int GetData3();                                                 // 31
    virtual int GetData4();                                                 // 32
    virtual int GetData5();                                                 // 33
    virtual int GetData6();                                                 // 34
    virtual int GetData7();                                                 // 35
    virtual int GetData8();                                                 // 36
    virtual __int64 GetSerial64();                                          // 37
    virtual int GetJID();                                                   // 38
};

C_ASSERT(FIELD_OFFSET(CInstanceTimedJob, ID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJob, CharID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJob, Category) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJob, JobID) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJob, TimeToKeep) == 0x28);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJob, Data1) == 0x2C);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJob, Data2) == 0x30);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJob, Data3) == 0x34);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJob, Data4) == 0x38);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJob, Data5) == 0x3C);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJob, Data6) == 0x40);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJob, Data7) == 0x44);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJob, Data8) == 0x48);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJob, Serial64) == 0x50);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJob, JID) == 0x58);
C_ASSERT(sizeof(CInstanceTimedJob) == 0x60);

// _TIMEDJOBFORPET
struct InstanceTimedJobForPetData
{
    int              ID;                    // +18
    int              CharID;                // +1C
    BYTE             Category;              // +20
    int              JobID;                 // +24
    int              TimeToKeep;            // +28
    int              Data1;                 // +2C
    int              Data2;                 // +30
    int              Data3;                 // +34
    int              Data4;                 // +38
    int              Data5;                 // +3C
    int              Data6;                 // +40
    int              Data7;                 // +44
    int              Data8;                 // +48
    __int64          Serial64;              // +50
    int              JID;                   // +58
};

class CInstanceTimedJobForPet : public CDBRecordT<CInstanceTimedJobForPet, ITimedJobCommonData, InstanceTimedJobForPetData>
{
    SR_RECORD_MEMBERS(CInstanceTimedJobForPet)

public:
    // 0x524c20: jumps to ITimedJobCommonData::InitData.
    virtual void InitData();                                                // 5
    virtual void SetID(int nID);                                            // 9
    virtual void SetCharID(int nCharID);                                    // 10
    virtual void SetCategory(BYTE byCategory);                              // 11
    virtual void SetJobID(int nJobID);                                      // 12
    virtual void SetTimeToKeep(int nTimeToKeep);                            // 13
    virtual void SetData1(int nData);                                       // 14
    virtual void SetData2(int nData);                                       // 15
    virtual void SetData3(int nData);                                       // 16
    virtual void SetData4(int nData);                                       // 17
    virtual void SetData5(int nData);                                       // 18
    virtual void SetData6(int nData);                                       // 19
    virtual void SetData7(int nData);                                       // 20
    virtual void SetData8(int nData);                                       // 21
    virtual void SetSerial64(__int64 nSerial);                              // 22
    virtual void SetJID(int nJID);                                          // 23
    virtual int GetID();                                                    // 24
    virtual int GetCharID();                                                // 25
    virtual BYTE GetCategory();                                             // 26
    virtual int GetJobID();                                                 // 27
    virtual int GetTimeToKeep();                                            // 28
    virtual int GetData1();                                                 // 29
    virtual int GetData2();                                                 // 30
    virtual int GetData3();                                                 // 31
    virtual int GetData4();                                                 // 32
    virtual int GetData5();                                                 // 33
    virtual int GetData6();                                                 // 34
    virtual int GetData7();                                                 // 35
    virtual int GetData8();                                                 // 36
    virtual __int64 GetSerial64();                                          // 37
    virtual int GetJID();                                                   // 38
};

C_ASSERT(FIELD_OFFSET(CInstanceTimedJobForPet, ID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJobForPet, CharID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJobForPet, Category) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJobForPet, JobID) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJobForPet, TimeToKeep) == 0x28);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJobForPet, Data1) == 0x2C);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJobForPet, Data2) == 0x30);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJobForPet, Data3) == 0x34);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJobForPet, Data4) == 0x38);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJobForPet, Data5) == 0x3C);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJobForPet, Data6) == 0x40);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJobForPet, Data7) == 0x44);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJobForPet, Data8) == 0x48);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJobForPet, Serial64) == 0x50);
C_ASSERT(FIELD_OFFSET(CInstanceTimedJobForPet, JID) == 0x58);
C_ASSERT(sizeof(CInstanceTimedJobForPet) == 0x60);

// =============================================================================================
// Guild, social and training camp records

// _GUILD
struct InstanceGuildData
{
    int              ID;                    // +18
    std::string      Name;                  // +1C
    BYTE             Lvl;                   // +38
    int              GatheredSP;            // +3C
    TIMESTAMP_STRUCT FoundationDate;        // +40
    int              Alliance;              // +50
    std::string      MasterCommentTitle;    // +54
    std::string      MasterComment;         // +70
    int              Booty;                 // +8C
    int              LastCrestRev;          // +90
    int              CurCrestRev;           // +94
    BYTE             MercenaryAttr;         // +98
};

class CInstanceGuild : public CDBRecordT<CInstanceGuild, CDBRecord, InstanceGuildData>
{
    SR_RECORD_MEMBERS(CInstanceGuild)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceGuild, ID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceGuild, Name) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceGuild, Lvl) == 0x38);
C_ASSERT(FIELD_OFFSET(CInstanceGuild, GatheredSP) == 0x3C);
C_ASSERT(FIELD_OFFSET(CInstanceGuild, FoundationDate) == 0x40);
C_ASSERT(FIELD_OFFSET(CInstanceGuild, Alliance) == 0x50);
C_ASSERT(FIELD_OFFSET(CInstanceGuild, MasterCommentTitle) == 0x54);
C_ASSERT(FIELD_OFFSET(CInstanceGuild, MasterComment) == 0x70);
C_ASSERT(FIELD_OFFSET(CInstanceGuild, Booty) == 0x8C);
C_ASSERT(FIELD_OFFSET(CInstanceGuild, LastCrestRev) == 0x90);
C_ASSERT(FIELD_OFFSET(CInstanceGuild, CurCrestRev) == 0x94);
C_ASSERT(FIELD_OFFSET(CInstanceGuild, MercenaryAttr) == 0x98);
C_ASSERT(sizeof(CInstanceGuild) == 0xA0);

// _GUILDMEMBER
struct InstanceGuildMemberData
{
    int              GuildID;               // +18
    int              CharID;                // +1C
    std::string      CharName;              // +20
    BYTE             MemberClass;           // +3C
    BYTE             CharLevel;             // +3D
    int              GP_Donation;           // +40
    TIMESTAMP_STRUCT JoinDate;              // +44
    int              Permission;            // +54
    int              Contribution;          // +58
    int              GuildWarKill;          // +5C
    int              GuildWarKilled;        // +60
    std::string      Nickname;              // +64
    int              RefObjID;              // +80
    BYTE             SiegeAuthority;        // +84
};

class CInstanceGuildMember : public CDBRecordT<CInstanceGuildMember, CDBRecord, InstanceGuildMemberData>
{
    SR_RECORD_MEMBERS(CInstanceGuildMember)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceGuildMember, GuildID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceGuildMember, CharID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceGuildMember, CharName) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceGuildMember, MemberClass) == 0x3C);
C_ASSERT(FIELD_OFFSET(CInstanceGuildMember, CharLevel) == 0x3D);
C_ASSERT(FIELD_OFFSET(CInstanceGuildMember, GP_Donation) == 0x40);
C_ASSERT(FIELD_OFFSET(CInstanceGuildMember, JoinDate) == 0x44);
C_ASSERT(FIELD_OFFSET(CInstanceGuildMember, Permission) == 0x54);
C_ASSERT(FIELD_OFFSET(CInstanceGuildMember, Contribution) == 0x58);
C_ASSERT(FIELD_OFFSET(CInstanceGuildMember, GuildWarKill) == 0x5C);
C_ASSERT(FIELD_OFFSET(CInstanceGuildMember, GuildWarKilled) == 0x60);
C_ASSERT(FIELD_OFFSET(CInstanceGuildMember, Nickname) == 0x64);
C_ASSERT(FIELD_OFFSET(CInstanceGuildMember, RefObjID) == 0x80);
C_ASSERT(FIELD_OFFSET(CInstanceGuildMember, SiegeAuthority) == 0x84);
C_ASSERT(sizeof(CInstanceGuildMember) == 0x88);

// _ALLIEDCLANS
struct InstanceAlliedClanData
{
    int              ID;                    // +18
    int              Ally[8];               // +1C  Ally1..Ally8
    int              LastCrestRev;          // +3C
    int              CurCrestRev;           // +40
};

class CInstanceAlliedClan : public CDBRecordT<CInstanceAlliedClan, CDBRecord, InstanceAlliedClanData>
{
    SR_RECORD_MEMBERS(CInstanceAlliedClan)

public:
    virtual void InitData();                                                // 5

    // Inlined in InitData: an index outside 0..7 asserts and is used anyway.
    void SetAlly(int nIndex, int nGuildID);
};

C_ASSERT(FIELD_OFFSET(CInstanceAlliedClan, ID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceAlliedClan, Ally) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceAlliedClan, LastCrestRev) == 0x3C);
C_ASSERT(FIELD_OFFSET(CInstanceAlliedClan, CurCrestRev) == 0x40);
C_ASSERT(sizeof(CInstanceAlliedClan) == 0x48);

// _GUILDWAR
struct InstanceGuildWarData
{
    int              ID;                    // +18
    BYTE             WarType;               // +1C
    BYTE             VictoryPointIndex;     // +1D
    int              LodgedGold;            // +20
    TIMESTAMP_STRUCT WarEndTime;            // +24
    int              Guild[2];              // +34  Guild1..Guild2
    int              PointGain[2];          // +3C  PointGain1..PointGain2
    int              Data1;                 // +44
    int              Data2;                 // +48
};

class CInstanceGuildWar : public CDBRecordT<CInstanceGuildWar, CDBRecord, InstanceGuildWarData>
{
    SR_RECORD_MEMBERS(CInstanceGuildWar)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceGuildWar, ID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceGuildWar, WarType) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceGuildWar, VictoryPointIndex) == 0x1D);
C_ASSERT(FIELD_OFFSET(CInstanceGuildWar, LodgedGold) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceGuildWar, WarEndTime) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceGuildWar, Guild) == 0x34);
C_ASSERT(FIELD_OFFSET(CInstanceGuildWar, PointGain) == 0x3C);
C_ASSERT(FIELD_OFFSET(CInstanceGuildWar, Data1) == 0x44);
C_ASSERT(FIELD_OFFSET(CInstanceGuildWar, Data2) == 0x48);
C_ASSERT(sizeof(CInstanceGuildWar) == 0x50);

// _GPHistory
struct InstanceGPHistoryData
{
    int              ID;                    // +18
    int              GuildID;               // +1C
    TIMESTAMP_STRUCT UsedTime;              // +20
    std::string      CharName;              // +30
    int              UsedGP;                // +4C
    BYTE             Reason;                // +50
};

class CInstanceGPHistory : public CDBRecordT<CInstanceGPHistory, CDBRecord, InstanceGPHistoryData>
{
    SR_RECORD_MEMBERS(CInstanceGPHistory)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceGPHistory, ID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceGPHistory, GuildID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceGPHistory, UsedTime) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceGPHistory, CharName) == 0x30);
C_ASSERT(FIELD_OFFSET(CInstanceGPHistory, UsedGP) == 0x4C);
C_ASSERT(FIELD_OFFSET(CInstanceGPHistory, Reason) == 0x50);
C_ASSERT(sizeof(CInstanceGPHistory) == 0x58);

// _Friend
struct InstanceFriendData
{
    int              CharID;                // +18
    int              FriendCharID;          // +1C
    std::string      FriendCharName;        // +20
    int              RefObjID;              // +3C
};

class CInstanceFriend : public CDBRecordT<CInstanceFriend, CDBRecord, InstanceFriendData>
{
    SR_RECORD_MEMBERS(CInstanceFriend)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceFriend, CharID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceFriend, FriendCharID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceFriend, FriendCharName) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceFriend, RefObjID) == 0x3C);
C_ASSERT(sizeof(CInstanceFriend) == 0x40);

// _Memo
struct InstanceMemoData
{
    __int64          ID64;                  // +18
    std::string      FromCharName;          // +20
    std::string      Message;               // +3C
    TIMESTAMP_STRUCT Date;                  // +58
    BYTE             Status;                // +68
    int              RefObjID;              // +6C
};

class CInstanceMemo : public CDBRecordT<CInstanceMemo, CDBRecord, InstanceMemoData>
{
    SR_RECORD_MEMBERS(CInstanceMemo)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceMemo, ID64) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceMemo, FromCharName) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceMemo, Message) == 0x3C);
C_ASSERT(FIELD_OFFSET(CInstanceMemo, Date) == 0x58);
C_ASSERT(FIELD_OFFSET(CInstanceMemo, Status) == 0x68);
C_ASSERT(FIELD_OFFSET(CInstanceMemo, RefObjID) == 0x6C);
C_ASSERT(sizeof(CInstanceMemo) == 0x70);

// _TRAININGCAMP
struct InstanceTrainingCampData
{
    int              ID;                    // +18
    TIMESTAMP_STRUCT CreationDate;          // +1C
    BYTE             Rank;                  // +2C
    int              GraduateCount;         // +30
    int              EvaluationPoint;       // +34
    TIMESTAMP_STRUCT LatestEvaluationDate;  // +38
    std::string      CommentTitle;          // +48
    std::string      Comment;               // +64
};

class CInstanceTrainingCamp : public CDBRecordT<CInstanceTrainingCamp, CDBRecord, InstanceTrainingCampData>
{
    SR_RECORD_MEMBERS(CInstanceTrainingCamp)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceTrainingCamp, ID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCamp, CreationDate) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCamp, Rank) == 0x2C);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCamp, GraduateCount) == 0x30);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCamp, EvaluationPoint) == 0x34);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCamp, LatestEvaluationDate) == 0x38);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCamp, CommentTitle) == 0x48);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCamp, Comment) == 0x64);
C_ASSERT(sizeof(CInstanceTrainingCamp) == 0x80);

// _TRAININGCAMPMEMBER
struct InstanceTrainingCampMemberData
{
    int              CampID;                // +18
    int              CharID;                // +1C
    int              RefObjID;              // +20
    std::string      CharName;              // +24
    TIMESTAMP_STRUCT JoinDate;              // +40
    BYTE             MemberClass;           // +50
    BYTE             CharJoinedLevel;       // +51
    BYTE             CharCurLevel;          // +52
    BYTE             CharMaxLevel;          // +53
    int              HonorPoint;            // +54
};

class CInstanceTrainingCampMember : public CDBRecordT<CInstanceTrainingCampMember, CDBRecord, InstanceTrainingCampMemberData>
{
    SR_RECORD_MEMBERS(CInstanceTrainingCampMember)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampMember, CampID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampMember, CharID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampMember, RefObjID) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampMember, CharName) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampMember, JoinDate) == 0x40);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampMember, MemberClass) == 0x50);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampMember, CharJoinedLevel) == 0x51);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampMember, CharCurLevel) == 0x52);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampMember, CharMaxLevel) == 0x53);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampMember, HonorPoint) == 0x54);
C_ASSERT(sizeof(CInstanceTrainingCampMember) == 0x58);

// _TRAININGCAMPHONORRANK
struct InstanceTrainingCampHonorRankData
{
    int              Ranking;               // +18
    int              CampID;                // +1C
    BYTE             Rank;                  // +20
};

class CInstanceTrainingCampHonorRank : public CDBRecordT<CInstanceTrainingCampHonorRank, CDBRecord, InstanceTrainingCampHonorRankData>
{
    SR_RECORD_MEMBERS(CInstanceTrainingCampHonorRank)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampHonorRank, Ranking) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampHonorRank, CampID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampHonorRank, Rank) == 0x20);
C_ASSERT(sizeof(CInstanceTrainingCampHonorRank) == 0x28);

// _TRAININGCAMPBUFFSTATUS
struct InstanceTrainingCampBuffStatusData
{
    int              CampID;                // +18
    int              RecipientCharID;       // +1C
    int              BuffSlotIdx;           // +20
    int              DonorCharID;           // +24
    TIMESTAMP_STRUCT StartingTime;          // +28
    int              RemainBuffPoint;       // +38
    BYTE             BuffType;              // +3C
};

class CInstanceTrainingCampBuffStatus : public CDBRecordT<CInstanceTrainingCampBuffStatus, CDBRecord, InstanceTrainingCampBuffStatusData>
{
    SR_RECORD_MEMBERS(CInstanceTrainingCampBuffStatus)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampBuffStatus, CampID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampBuffStatus, RecipientCharID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampBuffStatus, BuffSlotIdx) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampBuffStatus, DonorCharID) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampBuffStatus, StartingTime) == 0x28);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampBuffStatus, RemainBuffPoint) == 0x38);
C_ASSERT(FIELD_OFFSET(CInstanceTrainingCampBuffStatus, BuffType) == 0x3C);
C_ASSERT(sizeof(CInstanceTrainingCampBuffStatus) == 0x40);

// =============================================================================================
// Siege fortress records

// _SiegeFortress
struct InstanceSiegeFortressData
{
    int              FortressID;            // +18
    int              GuildID;               // +1C
    short            TaxRatio;              // +20
    __int64          Tax;                   // +28
    BYTE             NPCHired;              // +30
    int              TempGuildID;           // +34
    std::string      Introduction;          // +38
    TIMESTAMP_STRUCT CreatedDungeonTime;    // +54
    BYTE             CreatedDungeonCount;   // +64
    BYTE             IntroductionModificationPermission;// +65
};

class CInstanceSiegeFortress : public CDBRecordT<CInstanceSiegeFortress, CDBRecord, InstanceSiegeFortressData>
{
    SR_RECORD_MEMBERS(CInstanceSiegeFortress)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortress, FortressID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortress, GuildID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortress, TaxRatio) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortress, Tax) == 0x28);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortress, NPCHired) == 0x30);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortress, TempGuildID) == 0x34);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortress, Introduction) == 0x38);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortress, CreatedDungeonTime) == 0x54);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortress, CreatedDungeonCount) == 0x64);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortress, IntroductionModificationPermission) == 0x65);
C_ASSERT(sizeof(CInstanceSiegeFortress) == 0x68);

// _SiegeFortressObject
struct InstanceSiegeFortressObjectData
{
    int              ID;                    // +20
    int              FortressID;            // +24
    int              OwnerGuildID;          // +28
    int              RefObjID;              // +2C
    int              HP;                    // +30
    short            Region;                // +34
    float            PosX;                  // +38
    float            PosY;                  // +3C
    float            PosZ;                  // +40
    float            Direction;             // +44
    BYTE             OwnerLevel;            // +48
};

class CInstanceSiegeFortressObject : public CDBRecordT<CInstanceSiegeFortressObject, CInstanceChar, InstanceSiegeFortressObjectData>
{
    SR_RECORD_MEMBERS(CInstanceSiegeFortressObject)

public:
    virtual void InitData();                                                // 5
    // 0x525460 (shared)
    virtual void ResolveRefObj();                                           // 9
    virtual void Write(CByteStream* pStream, int nMode);                    // 10
    // 0x525430 (shared): the code name of the reference object.
    virtual const char* GetName();                                          // 11
    // 0x5235c0 (shared)
    virtual BYTE GetLevel();                                                // 12
    // 0x524f30 / 0x525690 (shared): HP, no MP.
    virtual int GetHP();                                                    // 14
    virtual int GetMP();                                                    // 15
    virtual void SetHP(int nHP);                                            // 16
    // 0x41a890 (shared empty function)
    virtual void SetMP(int nMP);                                            // 17
};

C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressObject, ID) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressObject, FortressID) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressObject, OwnerGuildID) == 0x28);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressObject, RefObjID) == 0x2C);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressObject, HP) == 0x30);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressObject, Region) == 0x34);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressObject, PosX) == 0x38);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressObject, PosY) == 0x3C);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressObject, PosZ) == 0x40);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressObject, Direction) == 0x44);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressObject, OwnerLevel) == 0x48);
C_ASSERT(sizeof(CInstanceSiegeFortressObject) == 0x50);

// _SIEGEFORTRESSSTRUCT
struct InstanceSiegeFortressStructData
{
    int              FortressID;            // +20
    int              OwnerGuildID;          // +24
    int              RefEventStructID;      // +28
    int              RefObjID;              // +2C
    int              HP;                    // +30
    TIMESTAMP_STRUCT MakeDate;              // +34
    short            State;                 // +44
};

class CInstanceSiegeFortressStruct : public CDBRecordT<CInstanceSiegeFortressStruct, CInstanceChar, InstanceSiegeFortressStructData>
{
    SR_RECORD_MEMBERS(CInstanceSiegeFortressStruct)

public:
    virtual void InitData();                                                // 5
    // 0x525460 (shared)
    virtual void ResolveRefObj();                                           // 9
    virtual void Write(CByteStream* pStream, int nMode);                    // 10
    // 0x525430 (shared): the code name of the reference object.
    virtual const char* GetName();                                          // 11
    // 0x5235c0 (shared)
    virtual BYTE GetLevel();                                                // 12
    // 0x524f30 / 0x525690 (shared): HP, no MP.
    virtual int GetHP();                                                    // 14
    virtual int GetMP();                                                    // 15
    virtual void SetHP(int nHP);                                            // 16
    // 0x41a890 (shared empty function)
    virtual void SetMP(int nMP);                                            // 17
    virtual short GetState();                                               // 18
    virtual void SetState(BYTE byState);                                    // 19
};

C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressStruct, FortressID) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressStruct, OwnerGuildID) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressStruct, RefEventStructID) == 0x28);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressStruct, RefObjID) == 0x2C);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressStruct, HP) == 0x30);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressStruct, MakeDate) == 0x34);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressStruct, State) == 0x44);
C_ASSERT(sizeof(CInstanceSiegeFortressStruct) == 0x48);

// _SiegeFortressItemForge
struct InstanceSiegeFortressItemForgeData
{
    int              FortressID;            // +18
    int              ItemRefID;             // +1C
    short            Amount;                // +20
    BYTE             Finished;              // +22
    TIMESTAMP_STRUCT StartDate;             // +24
    TIMESTAMP_STRUCT FinishDate;            // +34
};

class CInstanceSiegeFortressItemForge : public CDBRecordT<CInstanceSiegeFortressItemForge, CDBRecord, InstanceSiegeFortressItemForgeData>
{
    SR_RECORD_MEMBERS(CInstanceSiegeFortressItemForge)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressItemForge, FortressID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressItemForge, ItemRefID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressItemForge, Amount) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressItemForge, Finished) == 0x22);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressItemForge, StartDate) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressItemForge, FinishDate) == 0x34);
C_ASSERT(sizeof(CInstanceSiegeFortressItemForge) == 0x48);

// _SiegeFortressRequest
struct InstanceSiegeFortressRequestData
{
    int              FortressID;            // +18
    int              GuildID;               // +1C
    BYTE             RequestType;           // +20
};

class CInstanceSiegeFortressRequest : public CDBRecordT<CInstanceSiegeFortressRequest, CDBRecord, InstanceSiegeFortressRequestData>
{
    SR_RECORD_MEMBERS(CInstanceSiegeFortressRequest)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressRequest, FortressID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressRequest, GuildID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressRequest, RequestType) == 0x20);
C_ASSERT(sizeof(CInstanceSiegeFortressRequest) == 0x28);

// _SiegeFortressBattleRecord
struct InstanceSiegeFortressBattleRecordData
{
    int              FortressID;            // +18
    int              CharID;                // +1C
    int              KillCount;             // +20
    int              KilledCount;           // +24
    TIMESTAMP_STRUCT RankUpDate;            // +28
    BYTE             CurRank;               // +38
};

class CInstanceSiegeFortressBattleRecord : public CDBRecordT<CInstanceSiegeFortressBattleRecord, CDBRecord, InstanceSiegeFortressBattleRecordData>
{
    SR_RECORD_MEMBERS(CInstanceSiegeFortressBattleRecord)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressBattleRecord, FortressID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressBattleRecord, CharID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressBattleRecord, KillCount) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressBattleRecord, KilledCount) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressBattleRecord, RankUpDate) == 0x28);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressBattleRecord, CurRank) == 0x38);
C_ASSERT(sizeof(CInstanceSiegeFortressBattleRecord) == 0x40);

// _SiegeFortressStoneState
struct InstanceSiegeFortressStoneStateData
{
    int              FortressID;            // +18
    int              GuildID;               // +1C
    int              AccumulateDamage;      // +20
};

class CInstanceSiegeFortressStoneState : public CDBRecordT<CInstanceSiegeFortressStoneState, CDBRecord, InstanceSiegeFortressStoneStateData>
{
    SR_RECORD_MEMBERS(CInstanceSiegeFortressStoneState)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressStoneState, FortressID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressStoneState, GuildID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceSiegeFortressStoneState, AccumulateDamage) == 0x20);
C_ASSERT(sizeof(CInstanceSiegeFortressStoneState) == 0x28);

// =============================================================================================
// Market, event and log records

// _OpenMarket
struct InstanceOpenMarketData
{
    int              JID;                   // +18
    int              PersnalID;             // +1C
    std::string      CharName16;            // +20
    BYTE             Status;                // +3C
    int              RefItemID;             // +40
    int              TidGroupID;            // +44
    BYTE             ItemClass;             // +48
    __int64          ItemID;                // +50
    int              SellCnt;               // +58
    TIMESTAMP_STRUCT RegDate;               // +5C
    TIMESTAMP_STRUCT EndDate;               // +6C
    __int64          Price;                 // +80
    __int64          Deposit;               // +88
    __int64          SellFee;               // +90
    int              UseCash;               // +98
    __int64          Serial64;              // +A0
};

class CInstanceOpenMarket : public CDBRecordT<CInstanceOpenMarket, CDBRecord, InstanceOpenMarketData>
{
    SR_RECORD_MEMBERS(CInstanceOpenMarket)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, JID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, PersnalID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, CharName16) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, Status) == 0x3C);
C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, RefItemID) == 0x40);
C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, TidGroupID) == 0x44);
C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, ItemClass) == 0x48);
C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, ItemID) == 0x50);
C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, SellCnt) == 0x58);
C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, RegDate) == 0x5C);
C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, EndDate) == 0x6C);
C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, Price) == 0x80);
C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, Deposit) == 0x88);
C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, SellFee) == 0x90);
C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, UseCash) == 0x98);
C_ASSERT(FIELD_OFFSET(CInstanceOpenMarket, Serial64) == 0xA0);
C_ASSERT(sizeof(CInstanceOpenMarket) == 0xA8);

// _FleaMarketNetwork
struct InstanceFleaMarketNetworkData
{
    BYTE             AbleOpen;              // +18
    int              CharID;                // +1C
    BYTE             Slot;                  // +20
    int              TidGroupID;            // +24
    BYTE             ItemClass;             // +28
    int              ItemCount;             // +2C
    BYTE             MakeZone;              // +30
    __int64          Cash;                  // +38
};

class CInstanceFleaMarketNetwork : public CDBRecordT<CInstanceFleaMarketNetwork, CDBRecord, InstanceFleaMarketNetworkData>
{
    SR_RECORD_MEMBERS(CInstanceFleaMarketNetwork)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceFleaMarketNetwork, AbleOpen) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceFleaMarketNetwork, CharID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceFleaMarketNetwork, Slot) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceFleaMarketNetwork, TidGroupID) == 0x24);
C_ASSERT(FIELD_OFFSET(CInstanceFleaMarketNetwork, ItemClass) == 0x28);
C_ASSERT(FIELD_OFFSET(CInstanceFleaMarketNetwork, ItemCount) == 0x2C);
C_ASSERT(FIELD_OFFSET(CInstanceFleaMarketNetwork, MakeZone) == 0x30);
C_ASSERT(FIELD_OFFSET(CInstanceFleaMarketNetwork, Cash) == 0x38);
C_ASSERT(sizeof(CInstanceFleaMarketNetwork) == 0x40);

// _ShopItemStockQuantity
struct InstanceShopItemStockQuantityData
{
    BYTE             Service;               // +18
    int              Country;               // +1C
    std::string      RefShopGroupCodeName;  // +20
    std::string      RefPackageItemCodeName;// +3C
    short            ConstStockQuantity;    // +58
    short            StockQuantity;         // +5A
};

class CInstanceShopItemStockQuantity : public CDBRecordT<CInstanceShopItemStockQuantity, CDBRecord, InstanceShopItemStockQuantityData>
{
    SR_RECORD_MEMBERS(CInstanceShopItemStockQuantity)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceShopItemStockQuantity, Service) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceShopItemStockQuantity, Country) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceShopItemStockQuantity, RefShopGroupCodeName) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceShopItemStockQuantity, RefPackageItemCodeName) == 0x3C);
C_ASSERT(FIELD_OFFSET(CInstanceShopItemStockQuantity, ConstStockQuantity) == 0x58);
C_ASSERT(FIELD_OFFSET(CInstanceShopItemStockQuantity, StockQuantity) == 0x5A);
C_ASSERT(sizeof(CInstanceShopItemStockQuantity) == 0x60);

// _ServerEvent
struct InstanceServerEventData
{
    int              ID;                    // +18
    int              CompletionValue;       // +1C
    int              AchievementCondition;  // +20
    int              ProgressCount;         // +24
};

class CInstanceServerEvent : public CDBRecordT<CInstanceServerEvent, CDBRecord, InstanceServerEventData>
{
    SR_RECORD_MEMBERS(CInstanceServerEvent)

public:
    virtual void InitData();                                                // 5
};

C_ASSERT(FIELD_OFFSET(CInstanceServerEvent, ID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceServerEvent, CompletionValue) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceServerEvent, AchievementCondition) == 0x20);
C_ASSERT(FIELD_OFFSET(CInstanceServerEvent, ProgressCount) == 0x24);
C_ASSERT(sizeof(CInstanceServerEvent) == 0x28);

// _ServerEventReward
struct InstanceServerEventRewardData
{
    int              ServerEventID;         // +18
    int              RewardID;              // +1C
    int              RemainRewardTime;      // +20
};

class CInstanceServerEventReward : public CDBRecordT<CInstanceServerEventReward, CDBRecord, InstanceServerEventRewardData>
{
    SR_RECORD_MEMBERS(CInstanceServerEventReward)

public:
    virtual void InitData();                                                // 5

    // 0x525b70 (SetValue_32 of the analysis)
    void SetRemainRewardTime(int nTime);
};

C_ASSERT(FIELD_OFFSET(CInstanceServerEventReward, ServerEventID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceServerEventReward, RewardID) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceServerEventReward, RemainRewardTime) == 0x20);
C_ASSERT(sizeof(CInstanceServerEventReward) == 0x28);

// _LOGEVENTCHAR
struct InstanceLogCharData
{
    TIMESTAMP_STRUCT EventTime;             // +18
    int              EventID;               // +28
    int              Data1;                 // +2C
    int              Data2;                 // +30
    std::string      EventPos;              // +34
    std::string      strDesc;               // +50
};

SR_RECORD(CInstanceLogChar, CDBRecord, InstanceLogCharData)

C_ASSERT(FIELD_OFFSET(CInstanceLogChar, EventTime) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceLogChar, EventID) == 0x28);
C_ASSERT(FIELD_OFFSET(CInstanceLogChar, Data1) == 0x2C);
C_ASSERT(FIELD_OFFSET(CInstanceLogChar, Data2) == 0x30);
C_ASSERT(FIELD_OFFSET(CInstanceLogChar, EventPos) == 0x34);
C_ASSERT(FIELD_OFFSET(CInstanceLogChar, strDesc) == 0x50);
C_ASSERT(sizeof(CInstanceLogChar) == 0x70);

// _LOGEVENTITEM
struct InstanceLogItemData
{
    TIMESTAMP_STRUCT EventTime;             // +18
    int              ItemRefID;             // +28
    int              dwData;                // +2C
    BYTE             TargetStorage;         // +30
    BYTE             Operation;             // +31
    BYTE             Slot_From;             // +32
    BYTE             Slot_To;               // +33
    std::string      EventPos;              // +34
    std::string      strDesc;               // +50
    __int64          Serial64;              // +70
    __int64          Gold;                  // +78
};

SR_RECORD(CInstanceLogItem, CDBRecord, InstanceLogItemData)

C_ASSERT(FIELD_OFFSET(CInstanceLogItem, EventTime) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceLogItem, ItemRefID) == 0x28);
C_ASSERT(FIELD_OFFSET(CInstanceLogItem, dwData) == 0x2C);
C_ASSERT(FIELD_OFFSET(CInstanceLogItem, TargetStorage) == 0x30);
C_ASSERT(FIELD_OFFSET(CInstanceLogItem, Operation) == 0x31);
C_ASSERT(FIELD_OFFSET(CInstanceLogItem, Slot_From) == 0x32);
C_ASSERT(FIELD_OFFSET(CInstanceLogItem, Slot_To) == 0x33);
C_ASSERT(FIELD_OFFSET(CInstanceLogItem, EventPos) == 0x34);
C_ASSERT(FIELD_OFFSET(CInstanceLogItem, strDesc) == 0x50);
C_ASSERT(FIELD_OFFSET(CInstanceLogItem, Serial64) == 0x70);
C_ASSERT(FIELD_OFFSET(CInstanceLogItem, Gold) == 0x78);
C_ASSERT(sizeof(CInstanceLogItem) == 0x80);

// _LOGEVENTSIEGEFORTRESS
struct InstanceLogSiegeFortressData
{
    int              FortressID;            // +18
    TIMESTAMP_STRUCT EventTime;             // +1C
    int              EventID;               // +2C
    int              CharID;                // +30
    int              Data1;                 // +34
    int              Data2;                 // +38
    std::string      strDesc;               // +3C
};

SR_RECORD(CInstanceLogSiegeFortress, CDBRecord, InstanceLogSiegeFortressData)

C_ASSERT(FIELD_OFFSET(CInstanceLogSiegeFortress, FortressID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceLogSiegeFortress, EventTime) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceLogSiegeFortress, EventID) == 0x2C);
C_ASSERT(FIELD_OFFSET(CInstanceLogSiegeFortress, CharID) == 0x30);
C_ASSERT(FIELD_OFFSET(CInstanceLogSiegeFortress, Data1) == 0x34);
C_ASSERT(FIELD_OFFSET(CInstanceLogSiegeFortress, Data2) == 0x38);
C_ASSERT(FIELD_OFFSET(CInstanceLogSiegeFortress, strDesc) == 0x3C);
C_ASSERT(sizeof(CInstanceLogSiegeFortress) == 0x58);

// _LOGSERVEREVENT
struct InstanceLogServerEventData
{
    int              ID;                    // +18
    TIMESTAMP_STRUCT EventTime;             // +1C
    int              ServerEventID;         // +2C
    int              LogType;               // +30
    std::string      strDesc;               // +34
};

SR_RECORD(CInstanceLogServerEvent, CDBRecord, InstanceLogServerEventData)

C_ASSERT(FIELD_OFFSET(CInstanceLogServerEvent, ID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceLogServerEvent, EventTime) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceLogServerEvent, ServerEventID) == 0x2C);
C_ASSERT(FIELD_OFFSET(CInstanceLogServerEvent, LogType) == 0x30);
C_ASSERT(FIELD_OFFSET(CInstanceLogServerEvent, strDesc) == 0x34);
C_ASSERT(sizeof(CInstanceLogServerEvent) == 0x50);

// _LogSchedule
struct InstanceLogScheduleData
{
    int              ID;                    // +18
    std::string      ServerType;            // +1C
    int              ServerBodyID;          // +38
    std::string      ScheduleDefine;        // +3C
    int              ScheduleIdx;           // +58
    std::string      Type;                  // +5C
    TIMESTAMP_STRUCT OccureTime;            // +78
};

SR_RECORD(CInstanceLogSchedule, CDBRecord, InstanceLogScheduleData)

C_ASSERT(FIELD_OFFSET(CInstanceLogSchedule, ID) == 0x18);
C_ASSERT(FIELD_OFFSET(CInstanceLogSchedule, ServerType) == 0x1C);
C_ASSERT(FIELD_OFFSET(CInstanceLogSchedule, ServerBodyID) == 0x38);
C_ASSERT(FIELD_OFFSET(CInstanceLogSchedule, ScheduleDefine) == 0x3C);
C_ASSERT(FIELD_OFFSET(CInstanceLogSchedule, ScheduleIdx) == 0x58);
C_ASSERT(FIELD_OFFSET(CInstanceLogSchedule, Type) == 0x5C);
C_ASSERT(FIELD_OFFSET(CInstanceLogSchedule, OccureTime) == 0x78);
C_ASSERT(sizeof(CInstanceLogSchedule) == 0x88);
