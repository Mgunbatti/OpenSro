#include "ServerCommon/InstanceRecords.h"

#include <string.h>

#include "BSLib/Util/TimeSpan.h"

// =============================================================================================
// Base classes

// ---------------------------------------------------------------------------------------------
// Base classes

BS_IMPLEMENT_RUNTIME_CLASS(CInstanceObj, NULL, NULL, &CDBRecord::classCDBRecord)

CInstanceObj::CInstanceObj()
{
    m_pRefObj = NULL;
}

CInstanceObj::~CInstanceObj()
{
}

const char* CInstanceObj::GetTableName()
{
    BSAssertFail();
    return "Unknown";
}

CDBTableInfo* CInstanceObj::GetTableInfo()
{
    BSAssertFail();
    return NULL;
}

void CInstanceObj::InitData()
{
    m_pRefObj = NULL;
    ClearAllDirty();
}

BS_IMPLEMENT_RUNTIME_CLASS(CInstanceChar, NULL, NULL, &CInstanceObj::classCInstanceObj)

CInstanceChar::CInstanceChar()
{
}

CInstanceChar::~CInstanceChar()
{
}

int CInstanceChar::GetRefObjDword250()
{
    return RefObj_GetDword250(m_pRefObj);
}

BS_IMPLEMENT_RUNTIME_CLASS(CInstanceSlot, NULL, NULL, &CDBRecord::classCDBRecord)

CInstanceSlot::CInstanceSlot()
{
}

CInstanceSlot::~CInstanceSlot()
{
}

ITimedJobCommonData::ITimedJobCommonData()
{
}

ITimedJobCommonData::~ITimedJobCommonData()
{
}

void ITimedJobCommonData::InitData()
{
    SetID(0);
    SetCharID(0);
    SetCategory(0);
    SetJobID(0);
    SetTimeToKeep(0);
    SetData1(0);
    SetData2(0);
    SetData3(0);
    SetData4(0);
    SetData5(0);
    SetData6(0);
    SetData7(0);
    SetData8(0);
    SetSerial64(0);
    SetJID(0);
    ClearAllDirty();
}

// =============================================================================================
// Character, object and slot records

// ---------------------------------------------------------------------------------------------
// CInstanceUser (_USER): ctor 0x52f520, dtor 0x52f580, slots 2 / 3 0x520a50 / 0x520a70, table description 0x62ffe0.

static const DBRecordColumnDef s_aColumnsCInstanceUser[] =
{
    { "UserJID", 0, FIELD_OFFSET(CInstanceUser, UserJID), 0 },
    { "CharID", 0, FIELD_OFFSET(CInstanceUser, CharID), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceUser, CDBRecord, "_USER", 0x5, s_aColumnsCInstanceUser, _countof(s_aColumnsCInstanceUser))

// 0x520a80
void CInstanceUser::InitData()
{
    SR_SET_COLUMN(UserJID, 0, 0);
    SR_SET_COLUMN(CharID, 0, 2);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstancePC (_CHAR): ctor 0x52f5d0, dtor 0x5dca20, slots 2 / 3 0x520cb0 / 0x520cd0, table description 0x630470.

static const DBRecordColumnDef s_aColumnsCInstancePC[] =
{
    { "CharID", 0, FIELD_OFFSET(CInstancePC, CharID), 0 },
    { "Deleted", 0, FIELD_OFFSET(CInstancePC, Deleted), 1 },
    { "RefObjID", 0, FIELD_OFFSET(CInstancePC, RefObjID), 1 },
    { "CharName16", 0, FIELD_OFFSET(CInstancePC, CharName16), 1 },
    { "NickName16", 0, FIELD_OFFSET(CInstancePC, NickName16), 7 },
    { "Scale", 0, FIELD_OFFSET(CInstancePC, Scale), 1 },
    { "CurLevel", 0, FIELD_OFFSET(CInstancePC, CurLevel), 2 },
    { "MaxLevel", 0, FIELD_OFFSET(CInstancePC, MaxLevel), 2 },
    { "ExpOffset", 0, FIELD_OFFSET(CInstancePC, ExpOffset), 2 },
    { "SExpOffset", 0, FIELD_OFFSET(CInstancePC, SExpOffset), 2 },
    { "Strength", 0, FIELD_OFFSET(CInstancePC, Strength), 2 },
    { "Intellect", 0, FIELD_OFFSET(CInstancePC, Intellect), 2 },
    { "RemainGold", 0, FIELD_OFFSET(CInstancePC, RemainGold), 3 },
    { "RemainSkillPoint", 0, FIELD_OFFSET(CInstancePC, RemainSkillPoint), 4 },
    { "RemainStatPoint", 0, FIELD_OFFSET(CInstancePC, RemainStatPoint), 4 },
    { "RemainHwanCount", 0, FIELD_OFFSET(CInstancePC, RemainHwanCount), 4 },
    { "GatheredExpPoint", 0, FIELD_OFFSET(CInstancePC, GatheredExpPoint), 4 },
    { "HP", 0, FIELD_OFFSET(CInstancePC, HP), 6 },
    { "MP", 0, FIELD_OFFSET(CInstancePC, MP), 6 },
    { "LatestRegion", 0, FIELD_OFFSET(CInstancePC, LatestRegion), 5 },
    { "PosX", 0, FIELD_OFFSET(CInstancePC, PosX), 5 },
    { "PosY", 0, FIELD_OFFSET(CInstancePC, PosY), 5 },
    { "PosZ", 0, FIELD_OFFSET(CInstancePC, PosZ), 5 },
    { "AppointedTeleport", 0, FIELD_OFFSET(CInstancePC, AppointedTeleport), 7 },
    { "AutoInvestExp", 0, FIELD_OFFSET(CInstancePC, AutoInvestExp), 7 },
    { "InventorySize", 0, FIELD_OFFSET(CInstancePC, InventorySize), 8 },
    { "DailyPK", 0, FIELD_OFFSET(CInstancePC, DailyPK), 9 },
    { "TotalPK", 0, FIELD_OFFSET(CInstancePC, TotalPK), 9 },
    { "PKPenaltyPoint", 0, FIELD_OFFSET(CInstancePC, PKPenaltyPoint), 9 },
    { "GuildID", 0, FIELD_OFFSET(CInstancePC, GuildID), 1 },
    { "LastLogout", 0, FIELD_OFFSET(CInstancePC, LastLogout), 10 },
    { "TelRegion", 0, FIELD_OFFSET(CInstancePC, TelRegion), 11 },
    { "TelPosX", 0, FIELD_OFFSET(CInstancePC, TelPosX), 11 },
    { "TelPosY", 0, FIELD_OFFSET(CInstancePC, TelPosY), 11 },
    { "TelPosZ", 0, FIELD_OFFSET(CInstancePC, TelPosZ), 11 },
    { "DiedRegion", 0, FIELD_OFFSET(CInstancePC, DiedRegion), 12 },
    { "DiedPosX", 0, FIELD_OFFSET(CInstancePC, DiedPosX), 12 },
    { "DiedPosY", 0, FIELD_OFFSET(CInstancePC, DiedPosY), 12 },
    { "DiedPosZ", 0, FIELD_OFFSET(CInstancePC, DiedPosZ), 12 },
    { "WorldID", 0, FIELD_OFFSET(CInstancePC, WorldID), 5 },
    { "TelWorldID", 0, FIELD_OFFSET(CInstancePC, TelWorldID), 11 },
    { "DiedWorldID", 0, FIELD_OFFSET(CInstancePC, DiedWorldID), 12 },
    { "HwanLevel", 0, FIELD_OFFSET(CInstancePC, HwanLevel), 13 },
};

SR_IMPLEMENT_RECORD(CInstancePC, CInstanceChar, "_CHAR", 0x5, s_aColumnsCInstancePC, _countof(s_aColumnsCInstancePC))

// 0x520cf0
void CInstancePC::InitData()
{
    SR_SET_COLUMN(CharID, 0, 0);
    SR_SET_COLUMN(Deleted, 0, 1);
    SR_SET_COLUMN(RefObjID, 0, 1);
    CharName16.clear();
    NickName16.clear();
    SR_SET_COLUMN(Scale, 0, 1);
    SR_SET_COLUMN(CurLevel, 0, 2);
    SR_SET_COLUMN(MaxLevel, 0, 2);
    SR_SET_COLUMN(ExpOffset, 0, 2);
    SR_SET_COLUMN(SExpOffset, 0, 2);
    SR_SET_COLUMN(Strength, 0, 2);
    SR_SET_COLUMN(Intellect, 0, 2);
    SR_SET_COLUMN(RemainGold, 0, 3);
    SR_SET_COLUMN(RemainSkillPoint, 0, 4);
    SR_SET_COLUMN(RemainStatPoint, 0, 4);
    SR_SET_COLUMN(RemainHwanCount, 0, 4);
    SR_SET_COLUMN(GatheredExpPoint, 0, 4);
    SR_SET_COLUMN(HP, 0, 6);
    SR_SET_COLUMN(MP, 0, 6);
    SR_SET_COLUMN(LatestRegion, 0, 5);
    SR_SET_COLUMN(PosX, 0.0f, 5);
    SR_SET_COLUMN(PosY, 0.0f, 5);
    SR_SET_COLUMN(PosZ, 0.0f, 5);
    SR_SET_COLUMN(AppointedTeleport, 0, 7);
    SR_SET_COLUMN(AutoInvestExp, 0, 7);
    SR_SET_COLUMN(InventorySize, 0, 8);
    SR_SET_COLUMN(DailyPK, 0, 9);
    SR_SET_COLUMN(TotalPK, 0, 9);
    SR_SET_COLUMN(PKPenaltyPoint, 0, 9);
    SR_SET_COLUMN(GuildID, 0, 1);
    SR_SET_COLUMN(TelRegion, 0, 11);
    SR_SET_COLUMN(DiedRegion, 0, 12);
    SR_SET_COLUMN(WorldID, 0, 5);
    SR_SET_COLUMN(TelWorldID, 0, 11);
    SR_SET_COLUMN(DiedWorldID, 0, 12);
    SR_SET_COLUMN(HwanLevel, 0, 13);
    CInstanceObj::InitData();
}

void CInstancePC::CopyTo(CDBRecord* pDest)
{
    (void)pDest;
    BSAssertFail();
}

// 0x521320 (shared with CInstanceCOS / CInstanceItem)
void CInstancePC::ResolveRefObj()
{
    if (m_pRefObj != NULL)
        BSAssertFail();
    m_pRefObj = ReferenceData_FindRefObj(RefObjID);
}

// 0x633070
void CInstancePC::Write(CByteStream* pStream, int nMode)
{
    pStream->Write<DWORD>(RefObjID);
    if (nMode == 1)
    {
        pStream->Write<BYTE>(Scale).Write<BYTE>(CurLevel).Write<BYTE>(MaxLevel)
            .Write<__int64>(ExpOffset).Write<DWORD>(SExpOffset).Write<__int64>(RemainGold)
            .Write<DWORD>(RemainSkillPoint).Write<WORD>(RemainStatPoint)
            .Write<BYTE>(RemainHwanCount).Write<DWORD>(GatheredExpPoint).Write<DWORD>(HP)
            .Write<DWORD>(MP).Write<BYTE>(AutoInvestExp).Write<BYTE>(DailyPK)
            .Write<WORD>(TotalPK).Write<DWORD>(PKPenaltyPoint);
    }
    else
    {
        pStream->Write<BYTE>(Scale);
    }
    pStream->Write<BYTE>(HwanLevel);
}

// 0x521270
const char* CInstancePC::GetName()
{
    return CharName16.c_str();
}

// 0x521290
BYTE CInstancePC::GetLevel()
{
    return CurLevel;
}

// 0x5212a0
int CInstancePC::GetHP()
{
    return HP;
}

// 0x5212b0
int CInstancePC::GetMP()
{
    return MP;
}

// 0x5212c0
void CInstancePC::SetHP(int nHP)
{
    SR_SET_COLUMN(HP, nHP, 6);
}

// 0x5212f0
void CInstancePC::SetMP(int nMP)
{
    SR_SET_COLUMN(MP, nMP, 6);
}

// 0x521280
const char* CInstancePC::GetNickName()
{
    return NickName16.c_str();
}

// ---------------------------------------------------------------------------------------------
// CInstanceCOS (_CHARCOS): ctor 0x52f8b0, dtor 0x52f920, slots 2 / 3 0x522a70 / 0x522a90, table description 0x633260.
// BUG(SM-003): the OwnerCharID setter marks no update group (group 3 is never dirty).

static const DBRecordColumnDef s_aColumnsCInstanceCOS[] =
{
    { "ID", 0, FIELD_OFFSET(CInstanceCOS, ID), 0 },
    { "OwnerCharID", 0, FIELD_OFFSET(CInstanceCOS, OwnerCharID), 3 },
    { "RefCharID", 0, FIELD_OFFSET(CInstanceCOS, RefCharID), 5 },
    { "HP", 0, FIELD_OFFSET(CInstanceCOS, HP), 2 },
    { "MP", 0, FIELD_OFFSET(CInstanceCOS, MP), 2 },
    { "KeeperNPC", 0, FIELD_OFFSET(CInstanceCOS, KeeperNPC), 4 },
    { "State", 0, FIELD_OFFSET(CInstanceCOS, State), 6 },
    { "CharName", 0, FIELD_OFFSET(CInstanceCOS, CharName), 7 },
    { "Lvl", 0, FIELD_OFFSET(CInstanceCOS, Lvl), 8 },
    { "ExpOffset", 0, FIELD_OFFSET(CInstanceCOS, ExpOffset), 8 },
    { "HGP", 0, FIELD_OFFSET(CInstanceCOS, HGP), 9 },
    { "PetOption", 0, FIELD_OFFSET(CInstanceCOS, PetOption), 10 },
    { "RentEndTime", 0, FIELD_OFFSET(CInstanceCOS, RentEndTime), 11 },
};

SR_IMPLEMENT_RECORD(CInstanceCOS, CInstanceChar, "_CHARCOS", 0x5, s_aColumnsCInstanceCOS, _countof(s_aColumnsCInstanceCOS))

// 0x522d20
void CInstanceCOS::InitData()
{
    SR_SET_COLUMN(ID, 0, 0);
    SR_SET_COLUMN(OwnerCharID, 0, 0);  // BUG(SM-003): no update group
    SR_SET_COLUMN(RefCharID, 0, 5);
    SR_SET_COLUMN(HP, 0, 2);
    SR_SET_COLUMN(MP, 0, 2);
    SR_SET_COLUMN(KeeperNPC, 0, 4);
    SR_SET_COLUMN(State, 0, 6);
    SR_SET_COLUMN(ExpOffset, 0, 8);
    SR_SET_COLUMN(HGP, 0, 9);
    SR_SET_COLUMN(Lvl, 0, 8);
    SR_SET_COLUMN(PetOption, 0, 10);
    CharName.clear();
    InstanceRecord_SetDate(RentEndTime, 0, 0, 0);
    CInstanceObj::InitData();
}

void CInstanceCOS::CopyTo(CDBRecord* pDest)
{
    CInstanceCOS* p = static_cast<CInstanceCOS*>(pDest);
    SR_COPY_COLUMN(p, ID, 0);
    // BUG(SM-003): OwnerCharID changes mark no update group.
    SR_COPY_COLUMN(p, OwnerCharID, 0);
    SR_COPY_COLUMN(p, RefCharID, 5);
    SR_COPY_COLUMN(p, HP, 2);
    SR_COPY_COLUMN(p, MP, 2);
    SR_COPY_COLUMN(p, KeeperNPC, 4);
    SR_COPY_COLUMN(p, State, 6);
    SR_COPY_COLUMN(p, ExpOffset, 8);
    SR_COPY_COLUMN(p, HGP, 9);
    SR_COPY_COLUMN(p, Lvl, 8);
    SR_COPY_COLUMN(p, PetOption, 10);
    p->SetColumn(&InstanceCOSData::CharName, CharName);
    p->SetColumn(&InstanceCOSData::RentEndTime, RentEndTime);
}

// 0x521320 (shared with CInstancePC / CInstanceItem)
void CInstanceCOS::ResolveRefObj()
{
    if (m_pRefObj != NULL)
        BSAssertFail();
    m_pRefObj = ReferenceData_FindRefObj(RefCharID);
}

// 0x522c10
void CInstanceCOS::Write(CByteStream* pStream, int nMode)
{
    pStream->Write<DWORD>(RefCharID);
    if (m_pRefObj == NULL)
        BSAssertFail();
    WORD wTypeID = RefObj_GetTypeID(m_pRefObj);
    if (nMode != 1)
        return;
    if (m_pRefObj == NULL)
        BSAssertFail();
    WORD wType = RefObj_GetTypeID(m_pRefObj);
    // TypeID1 1 (character), TypeID2 2 (NPC), TypeID3 3 (COS)
    if ((wType & 2) == 0 || (wType & 0x1C) != 4 || (wType & 0x60) != 0x40 ||
        (wType & 0x780) != 0x180)
    {
        return;
    }
    pStream->Write<DWORD>(HP).Write<DWORD>(MP);
    // 0x4a1f10
    if (InstanceRecord_IsCOSTypeID4_3(wTypeID))
        pStream->Write<__int64>(ExpOffset).Write<BYTE>(Lvl).Write<WORD>(HGP);
    // 0x61b690
    if (InstanceRecord_IsCOSTypeID4_3or4(wTypeID) == TRUE)
        pStream->Write<DWORD>(PetOption);
}

// 0x522bd0: the name, or the code name of the reference object when there is none.
const char* CInstanceCOS::GetName()
{
    if (CharName.size() != 0)
        return CharName.c_str();
    if (m_pRefObj == NULL)
        BSAssertFail();
    return RefObj_GetCodeName(m_pRefObj).c_str();
}

// 0x5235c0 (shared with the siege fortress objects)
BYTE CInstanceCOS::GetLevel()
{
    return RefObj_GetByte198(m_pRefObj);
}

// 0x524f20
int CInstanceCOS::GetHP()
{
    return HP;
}

// 0x524f30
int CInstanceCOS::GetMP()
{
    return MP;
}

// 0x523120
void CInstanceCOS::SetHP(int nHP)
{
    SR_SET_COLUMN(HP, nHP, 2);
}

// 0x523150
void CInstanceCOS::SetMP(int nMP)
{
    SR_SET_COLUMN(MP, nMP, 2);
}

// ---------------------------------------------------------------------------------------------
// CInstanceStruct (_STRUCT): ctor 0x5231b0, dtor 0x523280, slots 2 / 3 0x523180 / 0x5231a0, table description 0x638690.

SR_IMPLEMENT_RECORD(CInstanceStruct, CInstanceObj, "_STRUCT", 0x5, NULL, 0)

InstanceStructData::InstanceStructData()
{
    m_nRefObjID = 0;
}

// 0x523210
void CInstanceStruct::InitData()
{
    m_nRefObjID = 0;
    m_pRefObj = NULL;
    ClearAllDirty();
}

// 0x523220
void CInstanceStruct::ResolveRefObj()
{
    if (m_pRefObj != NULL)
        BSAssertFail();
    m_pRefObj = ReferenceData_FindRefObj(m_nRefObjID);
}

// 0x523260
void CInstanceStruct::Write(CByteStream* pStream, int nMode)
{
    (void)nMode;
    if (m_nRefObjID == 0)
        BSAssertFail();
    pStream->Write<DWORD>(m_nRefObjID);
}

// ---------------------------------------------------------------------------------------------
// CInstanceDummy (_DUMMY): ctor 0x523300, dtor 0x5233e0, slots 2 / 3 0x5232d0 / 0x5232f0, table description 0x638910.

SR_IMPLEMENT_RECORD(CInstanceDummy, CInstanceObj, "_DUMMY", 0x5, NULL, 0)

// 0x523360: the reference object of id -1.
void CInstanceDummy::ResolveRefObj()
{
    if (m_pRefObj != NULL)
        BSAssertFail();
    m_pRefObj = ReferenceData_FindRefObj(-1);
}

// 0x523390
void CInstanceDummy::Write(CByteStream* pStream, int nMode)
{
    (void)nMode;
    pStream->Write<DWORD>((DWORD)-1);
}

// ---------------------------------------------------------------------------------------------
// CInstanceEventDaemon (_EVENTDAEMON): ctor 0x523460, dtor 0x523540, slots 2 / 3 0x523430 / 0x523450, table description 0x638b90.

SR_IMPLEMENT_RECORD(CInstanceEventDaemon, CInstanceObj, "_EVENTDAEMON", 0x5, NULL, 0)

// 0x5234c0: the reference object of id -2.
void CInstanceEventDaemon::ResolveRefObj()
{
    if (m_pRefObj != NULL)
        BSAssertFail();
    m_pRefObj = ReferenceData_FindRefObj(-2);
}

// 0x5234f0
void CInstanceEventDaemon::Write(CByteStream* pStream, int nMode)
{
    (void)nMode;
    pStream->Write<DWORD>((DWORD)-2);
}

// ---------------------------------------------------------------------------------------------
// CInstanceInventory (_INVENTORY): ctor 0x52f6f0, dtor 0x52f750, slots 2 / 3 0x523750 / 0x523770, table description 0x634170.

static const DBRecordColumnDef s_aColumnsCInstanceInventory[] =
{
    { "Slot", 0, FIELD_OFFSET(CInstanceInventory, Slot), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceInventory, CInstanceSlot, "_INVENTORY", 0x4, s_aColumnsCInstanceInventory, _countof(s_aColumnsCInstanceInventory))

// 0x523780
void CInstanceInventory::InitData()
{
    Slot = 0;
    ClearAllDirty();
}

BYTE CInstanceInventory::GetSlot()
{
    return Slot;
}

// ---------------------------------------------------------------------------------------------
// CInstanceInventoryForAvatar (_INVENTORYFORAVATAR): ctor 0x530a20, dtor 0x530a80, slots 2 / 3 0x523790 / 0x5237b0, table description 0x6344e0.

static const DBRecordColumnDef s_aColumnsCInstanceInventoryForAvatar[] =
{
    { "Slot", 0, FIELD_OFFSET(CInstanceInventoryForAvatar, Slot), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceInventoryForAvatar, CInstanceSlot, "_INVENTORYFORAVATAR", 0x4, s_aColumnsCInstanceInventoryForAvatar, _countof(s_aColumnsCInstanceInventoryForAvatar))

// 0x523780
void CInstanceInventoryForAvatar::InitData()
{
    Slot = 0;
    ClearAllDirty();
}

BYTE CInstanceInventoryForAvatar::GetSlot()
{
    return Slot;
}

// ---------------------------------------------------------------------------------------------
// CInstanceInventoryForLinkedStorage (_INVENTORYFORLINKEDSTORAGE): ctor 0x530ad0, dtor 0x530b30, slots 2 / 3 0x5237d0 / 0x5237f0, table description 0x634850.

static const DBRecordColumnDef s_aColumnsCInstanceInventoryForLinkedStorage[] =
{
    { "Slot", 0, FIELD_OFFSET(CInstanceInventoryForLinkedStorage, Slot), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceInventoryForLinkedStorage, CInstanceSlot, "_INVENTORYFORLINKEDSTORAGE", 0x4, s_aColumnsCInstanceInventoryForLinkedStorage, _countof(s_aColumnsCInstanceInventoryForLinkedStorage))

// 0x523780
void CInstanceInventoryForLinkedStorage::InitData()
{
    Slot = 0;
    ClearAllDirty();
}

BYTE CInstanceInventoryForLinkedStorage::GetSlot()
{
    return Slot;
}

// ---------------------------------------------------------------------------------------------
// CInstanceChest (_CHEST): ctor 0x52ffd0, dtor 0x530030, slots 2 / 3 0x523800 / 0x523820, table description 0x634bc0.

static const DBRecordColumnDef s_aColumnsCInstanceChest[] =
{
    { "Slot", 0, FIELD_OFFSET(CInstanceChest, Slot), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceChest, CInstanceSlot, "_CHEST", 0x4, s_aColumnsCInstanceChest, _countof(s_aColumnsCInstanceChest))

// 0x523780
void CInstanceChest::InitData()
{
    Slot = 0;
    ClearAllDirty();
}

BYTE CInstanceChest::GetSlot()
{
    return Slot;
}

// ---------------------------------------------------------------------------------------------
// CInstanceInvCOS (_INVCOS): ctor 0x530340, dtor 0x5303a0, slots 2 / 3 0x523830 / 0x523850, table description 0x634f30.

static const DBRecordColumnDef s_aColumnsCInstanceInvCOS[] =
{
    { "Slot", 0, FIELD_OFFSET(CInstanceInvCOS, Slot), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceInvCOS, CInstanceSlot, "_INVCOS", 0x4, s_aColumnsCInstanceInvCOS, _countof(s_aColumnsCInstanceInvCOS))

// 0x523780
void CInstanceInvCOS::InitData()
{
    Slot = 0;
    ClearAllDirty();
}

BYTE CInstanceInvCOS::GetSlot()
{
    return Slot;
}

// ---------------------------------------------------------------------------------------------
// CInstanceGuildChest (_GUILDCHEST): ctor 0x5303f0, dtor 0x530450, slots 2 / 3 0x5245c0 / 0x5245e0, table description 0x6401f0.

static const DBRecordColumnDef s_aColumnsCInstanceGuildChest[] =
{
    { "Slot", 0, FIELD_OFFSET(CInstanceGuildChest, Slot), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceGuildChest, CInstanceSlot, "_GUILDCHEST", 0x4, s_aColumnsCInstanceGuildChest, _countof(s_aColumnsCInstanceGuildChest))

// 0x523780
void CInstanceGuildChest::InitData()
{
    Slot = 0;
    ClearAllDirty();
}

BYTE CInstanceGuildChest::GetSlot()
{
    return Slot;
}

// ---------------------------------------------------------------------------------------------
// CInstanceQuot (_ITEMQUOTATION): ctor 0x530dd0, dtor 0x530e30, slots 2 / 3 0x523860 / 0x523880, table description 0x6352a0.

static const DBRecordColumnDef s_aColumnsCInstanceQuot[] =
{
    { "ID", 0, FIELD_OFFSET(CInstanceQuot, ID), 0 },
    { "Service", 0, FIELD_OFFSET(CInstanceQuot, Service), 1 },
    { "AssocNPC", 0, FIELD_OFFSET(CInstanceQuot, AssocNPC), 1 },
    { "RefItemID", 0, FIELD_OFFSET(CInstanceQuot, RefItemID), 1 },
    { "BaseQuot", 0, FIELD_OFFSET(CInstanceQuot, BaseQuot), 1 },
    { "Quot_LB", 0, FIELD_OFFSET(CInstanceQuot, Quot_LB), 1 },
    { "Quot_UB", 0, FIELD_OFFSET(CInstanceQuot, Quot_UB), 1 },
    { "BaseStockAmount", 0, FIELD_OFFSET(CInstanceQuot, BaseStockAmount), 1 },
    { "FluctuateAmount", 0, FIELD_OFFSET(CInstanceQuot, FluctuateAmount), 1 },
    { "CurStockAmount", 0, FIELD_OFFSET(CInstanceQuot, CurStockAmount), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceQuot, CDBRecord, "_ITEMQUOTATION", 0x5, s_aColumnsCInstanceQuot, _countof(s_aColumnsCInstanceQuot))

// 0x523890
void CInstanceQuot::InitData()
{
    SR_SET_COLUMN(ID, 0, 0);
    SR_SET_COLUMN(Service, 0, 1);
    SR_SET_COLUMN(AssocNPC, 0, 1);
    SR_SET_COLUMN(RefItemID, 0, 1);
    SR_SET_COLUMN(BaseQuot, 0.0f, 1);
    SR_SET_COLUMN(BaseStockAmount, 0, 1);
    SR_SET_COLUMN(FluctuateAmount, 0, 1);
    SR_SET_COLUMN(Quot_LB, 0.0f, 1);
    SR_SET_COLUMN(Quot_UB, 0.0f, 1);
    SR_SET_COLUMN(CurStockAmount, 0, 2);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceSkill (_CHARSKILL): ctor 0x52fe70, dtor 0x52fed0, slots 2 / 3 0x5239c0 / 0x5239e0, table description 0x637bd0.

static const DBRecordColumnDef s_aColumnsCInstanceSkill[] =
{
    { "CharID", 0, FIELD_OFFSET(CInstanceSkill, CharID), 0 },
    { "SkillID", 0, FIELD_OFFSET(CInstanceSkill, SkillID), 0 },
    { "Enable", 0, FIELD_OFFSET(CInstanceSkill, Enable), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceSkill, CDBRecord, "_CHARSKILL", 0x5, s_aColumnsCInstanceSkill, _countof(s_aColumnsCInstanceSkill))

// 0x5239f0
void CInstanceSkill::InitData()
{
    SR_SET_COLUMN(CharID, 0, 0);
    SR_SET_COLUMN(SkillID, 0, 0);
    SR_SET_COLUMN(Enable, 0, 2);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceSkillMastery (_CHARSKILLMASTERY): ctor 0x52ff20, dtor 0x52ff80, slots 2 / 3 0x523a60 / 0x523a80, table description 0x638130.

static const DBRecordColumnDef s_aColumnsCInstanceSkillMastery[] =
{
    { "CharID", 0, FIELD_OFFSET(CInstanceSkillMastery, CharID), 0 },
    { "MasteryID", 0, FIELD_OFFSET(CInstanceSkillMastery, MasteryID), 0 },
    { "Level", 0, FIELD_OFFSET(CInstanceSkillMastery, Level), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceSkillMastery, CDBRecord, "_CHARSKILLMASTERY", 0x5, s_aColumnsCInstanceSkillMastery, _countof(s_aColumnsCInstanceSkillMastery))

// 0x523a90
void CInstanceSkillMastery::InitData()
{
    SR_SET_COLUMN(CharID, 0, 0);
    SR_SET_COLUMN(MasteryID, 0, 0);
    SR_SET_COLUMN(Level, 0, 2);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceQuest (_CHARQUEST): ctor 0x530290, dtor 0x5302f0, slots 2 / 3 0x523b00 / 0x523b20, table description 0x639760.

static const DBRecordColumnDef s_aColumnsCInstanceQuest[] =
{
    { "CharID", 0, FIELD_OFFSET(CInstanceQuest, CharID), 0 },
    { "QuestID", 0, FIELD_OFFSET(CInstanceQuest, QuestID), 0 },
    { "Status", 0, FIELD_OFFSET(CInstanceQuest, Status), 2 },
    { "AchievementCount", 0, FIELD_OFFSET(CInstanceQuest, AchievementCount), 2 },
    { "StartTime", 0, FIELD_OFFSET(CInstanceQuest, StartTime), 2 },
    { "EndTime", 0, FIELD_OFFSET(CInstanceQuest, EndTime), 2 },
    { "QuestData1", 0, FIELD_OFFSET(CInstanceQuest, QuestData1), 2 },
    { "QuestData2", 0, FIELD_OFFSET(CInstanceQuest, QuestData2), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceQuest, CDBRecord, "_CHARQUEST", 0x5, s_aColumnsCInstanceQuest, _countof(s_aColumnsCInstanceQuest))

// 0x523b30
void CInstanceQuest::InitData()
{
    SR_SET_COLUMN(CharID, 0, 0);
    SR_SET_COLUMN(QuestID, 0, 0);
    SR_SET_COLUMN(Status, 0, 2);
    SR_SET_COLUMN(AchievementCount, 0, 2);
    SR_SET_COLUMN(QuestData1, 0, 2);
    SR_SET_COLUMN(QuestData2, 0, 2);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceStaticAvatar (_STATICAVATAR): ctor 0x530fe0, dtor 0x531040, slots 2 / 3 0x524f90 / 0x524fb0, table description 0x644380.

static const DBRecordColumnDef s_aColumnsCInstanceStaticAvatar[] =
{
    { "CharID", 0, FIELD_OFFSET(CInstanceStaticAvatar, CharID), 0 },
    { "Param1", 0, FIELD_OFFSET(CInstanceStaticAvatar, Param1), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceStaticAvatar, CDBRecord, "_STATICAVATAR", 0x5, s_aColumnsCInstanceStaticAvatar, _countof(s_aColumnsCInstanceStaticAvatar))

// 0x524fc0
void CInstanceStaticAvatar::InitData()
{
    SR_SET_COLUMN(CharID, 0, 0);
    SR_SET_COLUMN(Param1, 0, 2);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CCharInstanceGameWorldData (_CharInstanceWorldData): ctor 0x5304a0, dtor 0x530500, slots 2 / 3 0x525010 / 0x525030, table description 0x6447f0.

static const DBRecordColumnDef s_aColumnsCCharInstanceGameWorldData[] =
{
    { "CharID", 0, FIELD_OFFSET(CCharInstanceGameWorldData, CharID), 0 },
    { "DungeonKeyID", 0, FIELD_OFFSET(CCharInstanceGameWorldData, DungeonKeyID), 2 },
    { "WorldID", 0, FIELD_OFFSET(CCharInstanceGameWorldData, WorldID), 0 },
    { "LayerID", 0, FIELD_OFFSET(CCharInstanceGameWorldData, LayerID), 2 },
    { "OpenedTime", 0, FIELD_OFFSET(CCharInstanceGameWorldData, OpenedTime), 1 },
    { "RegionID", 0, FIELD_OFFSET(CCharInstanceGameWorldData, RegionID), 3 },
    { "PosX", 0, FIELD_OFFSET(CCharInstanceGameWorldData, PosX), 3 },
    { "PosY", 0, FIELD_OFFSET(CCharInstanceGameWorldData, PosY), 3 },
    { "PosZ", 0, FIELD_OFFSET(CCharInstanceGameWorldData, PosZ), 3 },
    { "IsActivated", 0, FIELD_OFFSET(CCharInstanceGameWorldData, IsActivated), 4 },
    { "EnterCount", 0, FIELD_OFFSET(CCharInstanceGameWorldData, EnterCount), 5 },
    { "LastEnterTime", 0, FIELD_OFFSET(CCharInstanceGameWorldData, LastEnterTime), 6 },
};

SR_IMPLEMENT_RECORD(CCharInstanceGameWorldData, CDBRecord, "_CharInstanceWorldData", 0x5, s_aColumnsCCharInstanceGameWorldData, _countof(s_aColumnsCCharInstanceGameWorldData))

// 0x525040
void CCharInstanceGameWorldData::InitData()
{
    SR_SET_COLUMN(CharID, 0, 0);
    SR_SET_COLUMN(DungeonKeyID, 0, 2);
    SR_SET_COLUMN(WorldID, 0, 0);
    SR_SET_COLUMN(LayerID, 0, 2);
    SR_SET_COLUMN(RegionID, 0, 3);
    SR_SET_COLUMN(PosX, 0, 3);
    SR_SET_COLUMN(PosY, 0, 3);
    SR_SET_COLUMN(PosZ, 0, 3);
    SR_SET_COLUMN(IsActivated, 0, 4);
    SR_SET_COLUMN(EnterCount, 0, 5);
    memset(&LastEnterTime, 0, sizeof(LastEnterTime));
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceCharTrijob (_CharTrijob): ctor 0x530550, dtor 0x5305b0, slots 2 / 3 0x5251b0 / 0x5251d0, table description 0x645610.
// InitData leaves the dirty bits as they are.

static const DBRecordColumnDef s_aColumnsCInstanceCharTrijob[] =
{
    { "CharID", 0, FIELD_OFFSET(CInstanceCharTrijob, CharID), 0 },
    { "JobType", 0, FIELD_OFFSET(CInstanceCharTrijob, JobType), 3 },
    { "Level", 0, FIELD_OFFSET(CInstanceCharTrijob, Level), 2 },
    { "Exp", 0, FIELD_OFFSET(CInstanceCharTrijob, Exp), 2 },
    { "Contribution", 0, FIELD_OFFSET(CInstanceCharTrijob, Contribution), 2 },
    { "Reward", 0, FIELD_OFFSET(CInstanceCharTrijob, Reward), 4 },
};

SR_IMPLEMENT_RECORD(CInstanceCharTrijob, CDBRecord, "_CharTrijob", 0x5, s_aColumnsCInstanceCharTrijob, _countof(s_aColumnsCInstanceCharTrijob))

// 0x5251e0
void CInstanceCharTrijob::InitData()
{
    SR_SET_COLUMN(CharID, 0, 0);
    SR_SET_COLUMN(JobType, 0, 3);
    SR_SET_COLUMN(Level, 0, 2);
    SR_SET_COLUMN(Exp, 0, 2);
    SR_SET_COLUMN(Contribution, 0, 2);
    SR_SET_COLUMN(Reward, 0, 4);
}

// ---------------------------------------------------------------------------------------------
// CInstanceCharTrijobSafeTrade (_CharTrijobSafeTrade): ctor 0x531820, dtor 0x531880, slots 2 / 3 0x525e30 / 0x525e50, table description 0x64ef70.
// InitData leaves the dirty bits as they are.

static const DBRecordColumnDef s_aColumnsCInstanceCharTrijobSafeTrade[] =
{
    { "CharID", 0, FIELD_OFFSET(CInstanceCharTrijobSafeTrade, CharID), 0 },
    { "AbleCount", 0, FIELD_OFFSET(CInstanceCharTrijobSafeTrade, AbleCount), 2 },
    { "Status", 0, FIELD_OFFSET(CInstanceCharTrijobSafeTrade, Status), 2 },
    { "LastSafeTrade", 0, FIELD_OFFSET(CInstanceCharTrijobSafeTrade, LastSafeTrade), 3 },
};

SR_IMPLEMENT_RECORD(CInstanceCharTrijobSafeTrade, CDBRecord, "_CharTrijobSafeTrade", 0x15, s_aColumnsCInstanceCharTrijobSafeTrade, _countof(s_aColumnsCInstanceCharTrijobSafeTrade))

// 0x525e60
void CInstanceCharTrijobSafeTrade::InitData()
{
    SR_SET_COLUMN(CharID, 0, 0);
    SR_SET_COLUMN(AbleCount, 0, 2);
    SR_SET_COLUMN(Status, 0, 2);
    memset(&LastSafeTrade, 0, sizeof(LastSafeTrade));
}

// ---------------------------------------------------------------------------------------------
// CInstanceClientConfig (_ClientConfig): ctor 0x531090, dtor 0x5310f0, slots 2 / 3 0x525d00 / 0x525d20, table description 0x64dbb0.

static const DBRecordColumnDef s_aColumnsCInstanceClientConfig[] =
{
    { "CharID", 0, FIELD_OFFSET(CInstanceClientConfig, CharID), 0 },
    { "ConfigType", 0, FIELD_OFFSET(CInstanceClientConfig, ConfigType), 0 },
    { "SlotSeq", 0, FIELD_OFFSET(CInstanceClientConfig, SlotSeq), 0 },
    { "SlotType", 0, FIELD_OFFSET(CInstanceClientConfig, SlotType), 2 },
    { "Data", 0, FIELD_OFFSET(CInstanceClientConfig, Data), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceClientConfig, CDBRecord, "_ClientConfig", 0x5, s_aColumnsCInstanceClientConfig, _countof(s_aColumnsCInstanceClientConfig))

// ---------------------------------------------------------------------------------------------
// CInstanceBlockedWhispererEntity (_BlockedWhisperers): ctor 0x531310, dtor 0x531380, slots 2 / 3 0x525e00 / 0x525e20, table description 0x64e2f0.

static const DBRecordColumnDef s_aColumnsCInstanceBlockedWhispererEntity[] =
{
    { "OwnerID", 0, FIELD_OFFSET(CInstanceBlockedWhispererEntity, OwnerID), 0 },
    { "TargetName", 0, FIELD_OFFSET(CInstanceBlockedWhispererEntity, TargetName), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceBlockedWhispererEntity, CDBRecord, "_BlockedWhisperers", 0x5, s_aColumnsCInstanceBlockedWhispererEntity, _countof(s_aColumnsCInstanceBlockedWhispererEntity))

// ---------------------------------------------------------------------------------------------
// CInstanceDeletedChar (_DELETEDCHAR): ctor 0x531ae0, dtor 0x531b40, slots 2 / 3 0x5247b0 / 0x5247d0, table description 0x641c10.

static const DBRecordColumnDef s_aColumnsCInstanceDeletedChar[] =
{
    { "CharID", 0, FIELD_OFFSET(CInstanceDeletedChar, CharID), 0 },
    { "UserJID", 0, FIELD_OFFSET(CInstanceDeletedChar, UserJID), 2 },
    { "DeletedDate", 0, FIELD_OFFSET(CInstanceDeletedChar, DeletedDate), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceDeletedChar, CDBRecord, "_DELETEDCHAR", 0x5, s_aColumnsCInstanceDeletedChar, _countof(s_aColumnsCInstanceDeletedChar))

// 0x5247e0
void CInstanceDeletedChar::InitData()
{
    SR_SET_COLUMN(CharID, 0, 0);
    SR_SET_COLUMN(UserJID, 0, 2);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceCollectionBook (_CharCollectionBook): ctor 0x531a30, dtor 0x531a90, slots 2 / 3 0x526190 / 0x5261b0, table description 0x652210.

static const DBRecordColumnDef s_aColumnsCInstanceCollectionBook[] =
{
    { "CharID", 0, FIELD_OFFSET(CInstanceCollectionBook, CharID), 2 },
    { "ThemeID", 0, FIELD_OFFSET(CInstanceCollectionBook, ThemeID), 2 },
    { "SlotIndex", 0, FIELD_OFFSET(CInstanceCollectionBook, SlotIndex), 2 },
    { "RegDate", 0, FIELD_OFFSET(CInstanceCollectionBook, RegDate), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceCollectionBook, CDBRecord, "_CharCollectionBook", 0x6, s_aColumnsCInstanceCollectionBook, _countof(s_aColumnsCInstanceCollectionBook))

// ---------------------------------------------------------------------------------------------
// CInstanceAssociatedReputation (_AssociationReputation): ctor 0x531b90, dtor 0x531c00, slots 2 / 3 0x525ef0 / 0x525f10, table description 0x64f5c0.
// InitData leaves the dirty bits as they are.

static const DBRecordColumnDef s_aColumnsCInstanceAssociatedReputation[] =
{
    { "AssociationCodeName", 0, FIELD_OFFSET(CInstanceAssociatedReputation, AssociationCodeName), 0 },
    { "Reputation", 0, FIELD_OFFSET(CInstanceAssociatedReputation, Reputation), 2 },
    { "PriorOccupation", 0, FIELD_OFFSET(CInstanceAssociatedReputation, PriorOccupation), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceAssociatedReputation, CDBRecord, "_AssociationReputation", 0x15, s_aColumnsCInstanceAssociatedReputation, _countof(s_aColumnsCInstanceAssociatedReputation))

// 0x525f20
void CInstanceAssociatedReputation::InitData()
{
    AssociationCodeName.clear();
    SR_SET_COLUMN(Reputation, 0, 2);
    SR_SET_COLUMN(PriorOccupation, 0, 2);
}

// ---------------------------------------------------------------------------------------------
// CInstanceChestInfo (_ChestInfo): ctor 0x531770, dtor 0x5317d0, slots 2 / 3 0x525980 / 0x5259a0, table description 0x649d00.

static const DBRecordColumnDef s_aColumnsCInstanceChestInfo[] =
{
    { "JID", 0, FIELD_OFFSET(CInstanceChestInfo, JID), 0 },
    { "ChestSize", 0, FIELD_OFFSET(CInstanceChestInfo, ChestSize), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceChestInfo, CDBRecord, "_ChestInfo", 0x5, s_aColumnsCInstanceChestInfo, _countof(s_aColumnsCInstanceChestInfo))

// 0x5259b0
void CInstanceChestInfo::InitData()
{
    SR_SET_COLUMN(JID, 0, 0);
    SR_SET_COLUMN(ChestSize, 0, 2);
    ClearAllDirty();
}

void CInstanceChestInfo::AddChestSize(BYTE byAdd)
{
    SR_SET_COLUMN(ChestSize, (BYTE)(ChestSize + byAdd), 2);
}

// ---------------------------------------------------------------------------------------------
// CInstanceBindingOptionWithItem (_BindingOptionWithItem): ctor 0x530b80, dtor 0x530be0, slots 2 / 3 0x521360 / 0x521380, table description 0x64fb00.

static const DBRecordColumnDef s_aColumnsCInstanceBindingOptionWithItem[] =
{
    { "nItemDBID", 0, FIELD_OFFSET(CInstanceBindingOptionWithItem, nItemDBID), 0 },
    { "bOptType", 0, FIELD_OFFSET(CInstanceBindingOptionWithItem, bOptType), 0 },
    { "nSlot", 0, FIELD_OFFSET(CInstanceBindingOptionWithItem, nSlot), 0 },
    { "nOptID", 0, FIELD_OFFSET(CInstanceBindingOptionWithItem, nOptID), 2 },
    { "nOptLvl", 0, FIELD_OFFSET(CInstanceBindingOptionWithItem, nOptLvl), 2 },
    { "nOptValue", 0, FIELD_OFFSET(CInstanceBindingOptionWithItem, nOptValue), 2 },
    { "nParam1", 0, FIELD_OFFSET(CInstanceBindingOptionWithItem, nParam1), 2 },
    { "nParam2", 0, FIELD_OFFSET(CInstanceBindingOptionWithItem, nParam2), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceBindingOptionWithItem, CDBRecord, "_BindingOptionWithItem", 0x5, s_aColumnsCInstanceBindingOptionWithItem, _countof(s_aColumnsCInstanceBindingOptionWithItem))

// 0x521390
void CInstanceBindingOptionWithItem::InitData()
{
    SR_SET_COLUMN(nItemDBID, 0, 0);
    SR_SET_COLUMN(bOptType, 0, 0);
    SR_SET_COLUMN(nSlot, 0, 0);
    SR_SET_COLUMN(nOptID, 0, 2);
    SR_SET_COLUMN(nOptLvl, 0, 2);
    SR_SET_COLUMN(nOptValue, 0, 2);
    SR_SET_COLUMN(nParam1, 0, 2);
    SR_SET_COLUMN(nParam2, 0, 2);
    ClearAllDirty();
}

void CInstanceBindingOptionWithItem::CopyTo(CDBRecord* pDest)
{
    CInstanceBindingOptionWithItem* p = static_cast<CInstanceBindingOptionWithItem*>(pDest);
    SR_COPY_COLUMN(p, nItemDBID, 0);
    SR_COPY_COLUMN(p, bOptType, 0);
    SR_COPY_COLUMN(p, nSlot, 0);
    SR_COPY_COLUMN(p, nOptID, 2);
    SR_COPY_COLUMN(p, nOptLvl, 2);
    SR_COPY_COLUMN(p, nOptValue, 2);
    SR_COPY_COLUMN(p, nParam1, 2);
    SR_COPY_COLUMN(p, nParam2, 2);
}

// 0x5215f0: mode 3 the whole row; modes 1 and 4 the slot and one packed value (option id,
// level << 16 and, type 1: nParam1, type 2: nOptValue; other types 0); other modes nothing.
void CInstanceBindingOptionWithItem::Write(CByteStream* pStream, int nMode)
{
    if (nMode == 3)
    {
        pStream->Write<__int64>(nItemDBID).Write<BYTE>(bOptType).Write<BYTE>(nSlot)
            .Write<DWORD>(nOptID).Write<BYTE>(nOptLvl).Write<DWORD>(nOptValue)
            .Write<DWORD>(nParam1).Write<DWORD>(nParam2);
        return;
    }
    if (nMode != 1 && nMode != 4)
        return;
    pStream->Write<BYTE>(nSlot);
    DWORD dwLow = 0;
    DWORD dwHigh = 0;
    if (bOptType == 1)
    {
        dwLow = (WORD)nOptID | ((DWORD)nOptLvl << 16);
        dwHigh = nParam1;
    }
    if (bOptType == 2)
    {
        dwLow = (WORD)nOptID | ((DWORD)nOptLvl << 16);
        dwHigh = nOptValue;
    }
    pStream->Write<__int64>(((__int64)dwHigh << 32) | dwLow);
}

// 0x521700: mode 3 reads the whole row.
void CInstanceBindingOptionWithItem::Read(CByteStream* pStream, int nMode)
{
    if (nMode != 3)
        return;
    pStream->Read(nItemDBID);
    pStream->Read(bOptType);
    pStream->Read(nSlot);
    pStream->Read(nOptID);
    pStream->Read(nOptLvl);
    pStream->Read(nOptValue);
    pStream->Read(nParam1);
    pStream->Read(nParam2);
}

BYTE CInstanceBindingOptionWithItem::GetOptType()
{
    return bOptType;
}

// ---------------------------------------------------------------------------------------------
// CInstanceRentItemInfo (_RentItemInfo): ctor 0x530c30, dtor 0x530c90, slots 2 / 3 0x521810 / 0x521830, table description 0x650520.

static const DBRecordColumnDef s_aColumnsCInstanceRentItemInfo[] =
{
    { "nItemDBID", 0, FIELD_OFFSET(CInstanceRentItemInfo, nItemDBID), 0 },
    { "nRentType", 0, FIELD_OFFSET(CInstanceRentItemInfo, nRentType), 2 },
    { "nCanDelete", 0, FIELD_OFFSET(CInstanceRentItemInfo, nCanDelete), 2 },
    { "nCanRecharge", 0, FIELD_OFFSET(CInstanceRentItemInfo, nCanRecharge), 2 },
    { "PeriodBeginTime", 0, FIELD_OFFSET(CInstanceRentItemInfo, PeriodBeginTime), 2 },
    { "PeriodEndTime", 0, FIELD_OFFSET(CInstanceRentItemInfo, PeriodEndTime), 2 },
    { "MeterRateTime", 0, FIELD_OFFSET(CInstanceRentItemInfo, MeterRateTime), 2 },
    { "nPackingState", 0, FIELD_OFFSET(CInstanceRentItemInfo, nPackingState), 2 },
    { "nPackingTime", 0, FIELD_OFFSET(CInstanceRentItemInfo, nPackingTime), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceRentItemInfo, CDBRecord, "_RentItemInfo", 0x5, s_aColumnsCInstanceRentItemInfo, _countof(s_aColumnsCInstanceRentItemInfo))

// 0x5219c0
void CInstanceRentItemInfo::InitData()
{
    SR_SET_COLUMN(nItemDBID, 0, 0);
    SR_SET_COLUMN(nRentType, 0, 2);
    SR_SET_COLUMN(nCanDelete, 0, 2);
    SR_SET_COLUMN(nCanRecharge, 0, 2);
    InstanceRecord_SetDate(PeriodBeginTime, 2000, 1, 1);
    InstanceRecord_SetDate(PeriodEndTime, 2000, 1, 1);
    InstanceRecord_SetDate(MeterRateTime, 2000, 1, 1);
    SR_SET_COLUMN(nPackingState, 0, 2);
    SR_SET_COLUMN(nPackingTime, 0, 2);
    ClearAllDirty();
}

void CInstanceRentItemInfo::CopyTo(CDBRecord* pDest)
{
    CInstanceRentItemInfo* p = static_cast<CInstanceRentItemInfo*>(pDest);
    SR_COPY_COLUMN(p, nItemDBID, 0);
    SR_COPY_COLUMN(p, nRentType, 2);
    SR_COPY_COLUMN(p, nCanDelete, 2);
    SR_COPY_COLUMN(p, nCanRecharge, 2);
    p->SetColumn(&InstanceRentItemInfoData::PeriodBeginTime, PeriodBeginTime);
    p->SetColumn(&InstanceRentItemInfoData::PeriodEndTime, PeriodEndTime);
    p->SetColumn(&InstanceRentItemInfoData::MeterRateTime, MeterRateTime);
    SR_COPY_COLUMN(p, nPackingState, 2);
    SR_COPY_COLUMN(p, nPackingTime, 2);
}

static CDateTime RentItemInfo_ToDateTime(const TIMESTAMP_STRUCT& ts)
{
    return CDateTime(ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second);
}

// 0x521c70: modes 1 and 3 the rent type, the delete flag and the period (type bit 0: begin and
// end as time values) or meter (bit 1: the remaining seconds, at least 0); mode 4 asserts.
void CInstanceRentItemInfo::Write(CByteStream* pStream, int nMode)
{
    if (nMode != 1 && nMode != 3)
    {
        if (nMode == 4)
            BSAssertFail();
        return;
    }
    pStream->Write<DWORD>(nRentType).Write<WORD>(nCanDelete);
    if (nRentType & 1)
    {
        CDateTime tBegin = RentItemInfo_ToDateTime(PeriodBeginTime);
        CDateTime tEnd = RentItemInfo_ToDateTime(PeriodEndTime);
        __time32_t tEndTime = tEnd.GetTime();
        __time32_t tBeginTime = tBegin.GetTime();
        pStream->Write<DWORD>(tBeginTime).Write<DWORD>(tEndTime);
    }
    if (nRentType & 2)
    {
        CTimeSpan span;
        if (nPackingState == 1)
        {
            span = CTimeSpan(0, 0, nPackingTime, 0);
        }
        else
        {
            CDateTime tMeter = RentItemInfo_ToDateTime(MeterRateTime);
            CDateTime tNow = CDateTime::GetCurrent();
            span = tMeter - tNow;
        }
        LONG lSeconds = span.GetTotalSeconds();
        if (lSeconds < 0)
            lSeconds = 0;
        pStream->Write<WORD>(nCanRecharge).Write<DWORD>(lSeconds);
    }
}

// 0x521dd0: modes 1 and 3 read what Write wrote; the times are skipped.
void CInstanceRentItemInfo::Read(CByteStream* pStream, int nMode)
{
    if (nMode != 1 && nMode != 3)
    {
        if (nMode == 4)
            BSAssertFail();
        return;
    }
    pStream->Read(nRentType);
    pStream->Skip(4);
    pStream->Read(nCanDelete);
    if (nRentType & 1)
    {
        pStream->Skip(4);
        pStream->Skip(4);
    }
    if (nRentType & 2)
    {
        pStream->Read(nCanRecharge);
        pStream->Skip(4);
    }
}

// ---------------------------------------------------------------------------------------------
// CInstanceTimedJob (_TIMEDJOB): ctor 0x530e80, dtor 0x530ee0, slots 2 / 3 0x5248f0 / 0x524910, table description 0x642160.

static const DBRecordColumnDef s_aColumnsCInstanceTimedJob[] =
{
    { "ID", 0, FIELD_OFFSET(CInstanceTimedJob, ID), 0 },
    { "CharID", 0, FIELD_OFFSET(CInstanceTimedJob, CharID), 0 },
    { "Category", 0, FIELD_OFFSET(CInstanceTimedJob, Category), 1 },
    { "JobID", 0, FIELD_OFFSET(CInstanceTimedJob, JobID), 1 },
    { "TimeToKeep", 0, FIELD_OFFSET(CInstanceTimedJob, TimeToKeep), 2 },
    { "Data1", 0, FIELD_OFFSET(CInstanceTimedJob, Data1), 3 },
    { "Data2", 0, FIELD_OFFSET(CInstanceTimedJob, Data2), 3 },
    { "Data3", 0, FIELD_OFFSET(CInstanceTimedJob, Data3), 3 },
    { "Data4", 0, FIELD_OFFSET(CInstanceTimedJob, Data4), 3 },
    { "Data5", 0, FIELD_OFFSET(CInstanceTimedJob, Data5), 3 },
    { "Data6", 0, FIELD_OFFSET(CInstanceTimedJob, Data6), 3 },
    { "Data7", 0, FIELD_OFFSET(CInstanceTimedJob, Data7), 3 },
    { "Data8", 0, FIELD_OFFSET(CInstanceTimedJob, Data8), 3 },
    { "Serial64", 0, FIELD_OFFSET(CInstanceTimedJob, Serial64), 3 },
    { "JID", 0, FIELD_OFFSET(CInstanceTimedJob, JID), 3 },
};

SR_IMPLEMENT_RECORD_RTC(CInstanceTimedJob, ITimedJobCommonData, &CDBRecord::classCDBRecord, "_TIMEDJOB", 0x5,
                        s_aColumnsCInstanceTimedJob, _countof(s_aColumnsCInstanceTimedJob))

// 0x524c20
void CInstanceTimedJob::InitData()
{
    ITimedJobCommonData::InitData();
}

// Setters 0x524920.. (slots 9..23), getters 0x524bc0.. (folded, slots 24..38).
void CInstanceTimedJob::SetID(int nID)
{
    SR_SET_COLUMN(ID, nID, 0);
}

void CInstanceTimedJob::SetCharID(int nCharID)
{
    SR_SET_COLUMN(CharID, nCharID, 0);
}

void CInstanceTimedJob::SetCategory(BYTE byCategory)
{
    SR_SET_COLUMN(Category, byCategory, 0);
}

void CInstanceTimedJob::SetJobID(int nJobID)
{
    SR_SET_COLUMN(JobID, nJobID, 0);
}

void CInstanceTimedJob::SetTimeToKeep(int nTimeToKeep)
{
    SR_SET_COLUMN(TimeToKeep, nTimeToKeep, 2);
}

void CInstanceTimedJob::SetData1(int nData)
{
    SR_SET_COLUMN(Data1, nData, 3);
}

void CInstanceTimedJob::SetData2(int nData)
{
    SR_SET_COLUMN(Data2, nData, 3);
}

void CInstanceTimedJob::SetData3(int nData)
{
    SR_SET_COLUMN(Data3, nData, 3);
}

void CInstanceTimedJob::SetData4(int nData)
{
    SR_SET_COLUMN(Data4, nData, 3);
}

void CInstanceTimedJob::SetData5(int nData)
{
    SR_SET_COLUMN(Data5, nData, 3);
}

void CInstanceTimedJob::SetData6(int nData)
{
    SR_SET_COLUMN(Data6, nData, 3);
}

void CInstanceTimedJob::SetData7(int nData)
{
    SR_SET_COLUMN(Data7, nData, 3);
}

void CInstanceTimedJob::SetData8(int nData)
{
    SR_SET_COLUMN(Data8, nData, 3);
}

void CInstanceTimedJob::SetSerial64(__int64 nSerial)
{
    SR_SET_COLUMN(Serial64, nSerial, 3);
}

void CInstanceTimedJob::SetJID(int nJID)
{
    SR_SET_COLUMN(JID, nJID, 3);
}

int CInstanceTimedJob::GetID()
{
    return ID;
}

int CInstanceTimedJob::GetCharID()
{
    return CharID;
}

BYTE CInstanceTimedJob::GetCategory()
{
    return Category;
}

int CInstanceTimedJob::GetJobID()
{
    return JobID;
}

int CInstanceTimedJob::GetTimeToKeep()
{
    return TimeToKeep;
}

int CInstanceTimedJob::GetData1()
{
    return Data1;
}

int CInstanceTimedJob::GetData2()
{
    return Data2;
}

int CInstanceTimedJob::GetData3()
{
    return Data3;
}

int CInstanceTimedJob::GetData4()
{
    return Data4;
}

int CInstanceTimedJob::GetData5()
{
    return Data5;
}

int CInstanceTimedJob::GetData6()
{
    return Data6;
}

int CInstanceTimedJob::GetData7()
{
    return Data7;
}

int CInstanceTimedJob::GetData8()
{
    return Data8;
}

__int64 CInstanceTimedJob::GetSerial64()
{
    return Serial64;
}

int CInstanceTimedJob::GetJID()
{
    return JID;
}

// ---------------------------------------------------------------------------------------------
// CInstanceTimedJobForPet (_TIMEDJOBFORPET): ctor 0x530f30, dtor 0x530f90, slots 2 / 3 0x524c30 / 0x524c50, table description 0x643270.

static const DBRecordColumnDef s_aColumnsCInstanceTimedJobForPet[] =
{
    { "ID", 0, FIELD_OFFSET(CInstanceTimedJobForPet, ID), 0 },
    { "CharID", 0, FIELD_OFFSET(CInstanceTimedJobForPet, CharID), 0 },
    { "Category", 0, FIELD_OFFSET(CInstanceTimedJobForPet, Category), 1 },
    { "JobID", 0, FIELD_OFFSET(CInstanceTimedJobForPet, JobID), 1 },
    { "TimeToKeep", 0, FIELD_OFFSET(CInstanceTimedJobForPet, TimeToKeep), 2 },
    { "Data1", 0, FIELD_OFFSET(CInstanceTimedJobForPet, Data1), 3 },
    { "Data2", 0, FIELD_OFFSET(CInstanceTimedJobForPet, Data2), 3 },
    { "Data3", 0, FIELD_OFFSET(CInstanceTimedJobForPet, Data3), 3 },
    { "Data4", 0, FIELD_OFFSET(CInstanceTimedJobForPet, Data4), 3 },
    { "Data5", 0, FIELD_OFFSET(CInstanceTimedJobForPet, Data5), 3 },
    { "Data6", 0, FIELD_OFFSET(CInstanceTimedJobForPet, Data6), 3 },
    { "Data7", 0, FIELD_OFFSET(CInstanceTimedJobForPet, Data7), 3 },
    { "Data8", 0, FIELD_OFFSET(CInstanceTimedJobForPet, Data8), 3 },
    { "Serial64", 0, FIELD_OFFSET(CInstanceTimedJobForPet, Serial64), 3 },
    { "JID", 0, FIELD_OFFSET(CInstanceTimedJobForPet, JID), 3 },
};

SR_IMPLEMENT_RECORD_RTC(CInstanceTimedJobForPet, ITimedJobCommonData, &CDBRecord::classCDBRecord, "_TIMEDJOBFORPET", 0x5,
                        s_aColumnsCInstanceTimedJobForPet, _countof(s_aColumnsCInstanceTimedJobForPet))

// 0x524c20
void CInstanceTimedJobForPet::InitData()
{
    ITimedJobCommonData::InitData();
}

// Setters 0x524c60.. (slots 9..23), getters 0x524bc0.. (folded, slots 24..38).
void CInstanceTimedJobForPet::SetID(int nID)
{
    SR_SET_COLUMN(ID, nID, 0);
}

void CInstanceTimedJobForPet::SetCharID(int nCharID)
{
    SR_SET_COLUMN(CharID, nCharID, 0);
}

void CInstanceTimedJobForPet::SetCategory(BYTE byCategory)
{
    SR_SET_COLUMN(Category, byCategory, 0);
}

void CInstanceTimedJobForPet::SetJobID(int nJobID)
{
    SR_SET_COLUMN(JobID, nJobID, 0);
}

void CInstanceTimedJobForPet::SetTimeToKeep(int nTimeToKeep)
{
    SR_SET_COLUMN(TimeToKeep, nTimeToKeep, 2);
}

void CInstanceTimedJobForPet::SetData1(int nData)
{
    SR_SET_COLUMN(Data1, nData, 3);
}

void CInstanceTimedJobForPet::SetData2(int nData)
{
    SR_SET_COLUMN(Data2, nData, 3);
}

void CInstanceTimedJobForPet::SetData3(int nData)
{
    SR_SET_COLUMN(Data3, nData, 3);
}

void CInstanceTimedJobForPet::SetData4(int nData)
{
    SR_SET_COLUMN(Data4, nData, 3);
}

void CInstanceTimedJobForPet::SetData5(int nData)
{
    SR_SET_COLUMN(Data5, nData, 3);
}

void CInstanceTimedJobForPet::SetData6(int nData)
{
    SR_SET_COLUMN(Data6, nData, 3);
}

void CInstanceTimedJobForPet::SetData7(int nData)
{
    SR_SET_COLUMN(Data7, nData, 3);
}

void CInstanceTimedJobForPet::SetData8(int nData)
{
    SR_SET_COLUMN(Data8, nData, 3);
}

void CInstanceTimedJobForPet::SetSerial64(__int64 nSerial)
{
    SR_SET_COLUMN(Serial64, nSerial, 3);
}

void CInstanceTimedJobForPet::SetJID(int nJID)
{
    SR_SET_COLUMN(JID, nJID, 3);
}

int CInstanceTimedJobForPet::GetID()
{
    return ID;
}

int CInstanceTimedJobForPet::GetCharID()
{
    return CharID;
}

BYTE CInstanceTimedJobForPet::GetCategory()
{
    return Category;
}

int CInstanceTimedJobForPet::GetJobID()
{
    return JobID;
}

int CInstanceTimedJobForPet::GetTimeToKeep()
{
    return TimeToKeep;
}

int CInstanceTimedJobForPet::GetData1()
{
    return Data1;
}

int CInstanceTimedJobForPet::GetData2()
{
    return Data2;
}

int CInstanceTimedJobForPet::GetData3()
{
    return Data3;
}

int CInstanceTimedJobForPet::GetData4()
{
    return Data4;
}

int CInstanceTimedJobForPet::GetData5()
{
    return Data5;
}

int CInstanceTimedJobForPet::GetData6()
{
    return Data6;
}

int CInstanceTimedJobForPet::GetData7()
{
    return Data7;
}

int CInstanceTimedJobForPet::GetData8()
{
    return Data8;
}

__int64 CInstanceTimedJobForPet::GetSerial64()
{
    return Serial64;
}

int CInstanceTimedJobForPet::GetJID()
{
    return JID;
}

// =============================================================================================
// Guild, social and training camp records

// ---------------------------------------------------------------------------------------------
// CInstanceGuild (_GUILD): ctor 0x52f9a0, dtor 0x52fa30, slots 2 / 3 0x523fd0 / 0x523ff0, table description 0x63c670.

static const DBRecordColumnDef s_aColumnsCInstanceGuild[] =
{
    { "ID", 0, FIELD_OFFSET(CInstanceGuild, ID), 0 },
    { "Name", 0, FIELD_OFFSET(CInstanceGuild, Name), 1 },
    { "Lvl", 0, FIELD_OFFSET(CInstanceGuild, Lvl), 2 },
    { "GatheredSP", 0, FIELD_OFFSET(CInstanceGuild, GatheredSP), 3 },
    { "FoundationDate", 0, FIELD_OFFSET(CInstanceGuild, FoundationDate), 1 },
    { "Alliance", 0, FIELD_OFFSET(CInstanceGuild, Alliance), 4 },
    { "MasterCommentTitle", 0, FIELD_OFFSET(CInstanceGuild, MasterCommentTitle), 5 },
    { "MasterComment", 0, FIELD_OFFSET(CInstanceGuild, MasterComment), 5 },
    { "Booty", 0, FIELD_OFFSET(CInstanceGuild, Booty), 6 },
    { "LastCrestRev", 0, FIELD_OFFSET(CInstanceGuild, LastCrestRev), 7 },
    { "CurCrestRev", 0, FIELD_OFFSET(CInstanceGuild, CurCrestRev), 7 },
    { "MercenaryAttr", 0, FIELD_OFFSET(CInstanceGuild, MercenaryAttr), 8 },
};

SR_IMPLEMENT_RECORD(CInstanceGuild, CDBRecord, "_GUILD", 0x5, s_aColumnsCInstanceGuild, _countof(s_aColumnsCInstanceGuild))

// 0x524000
void CInstanceGuild::InitData()
{
    SR_SET_COLUMN(ID, 0, 0);
    Name.clear();
    SR_SET_COLUMN(Lvl, 0, 2);
    SR_SET_COLUMN(GatheredSP, 0, 3);
    SR_SET_COLUMN(Alliance, 0, 4);
    MasterCommentTitle.clear();
    MasterComment.clear();
    SR_SET_COLUMN(Booty, 0, 6);
    SR_SET_COLUMN(LastCrestRev, 0, 7);
    SR_SET_COLUMN(CurCrestRev, 0, 7);
    SR_SET_COLUMN(MercenaryAttr, 0, 8);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceGuildMember (_GUILDMEMBER): ctor 0x52fbf0, dtor 0x52fc70, slots 2 / 3 0x524160 / 0x524180, table description 0x63d480.

static const DBRecordColumnDef s_aColumnsCInstanceGuildMember[] =
{
    { "GuildID", 0, FIELD_OFFSET(CInstanceGuildMember, GuildID), 0 },
    { "CharID", 0, FIELD_OFFSET(CInstanceGuildMember, CharID), 0 },
    { "CharName", 0, FIELD_OFFSET(CInstanceGuildMember, CharName), 1 },
    { "MemberClass", 0, FIELD_OFFSET(CInstanceGuildMember, MemberClass), 3 },
    { "CharLevel", 0, FIELD_OFFSET(CInstanceGuildMember, CharLevel), 4 },
    { "GP_Donation", 0, FIELD_OFFSET(CInstanceGuildMember, GP_Donation), 2 },
    { "JoinDate", 0, FIELD_OFFSET(CInstanceGuildMember, JoinDate), 1 },
    { "Permission", 0, FIELD_OFFSET(CInstanceGuildMember, Permission), 5 },
    { "Contribution", 0, FIELD_OFFSET(CInstanceGuildMember, Contribution), 6 },
    { "GuildWarKill", 0, FIELD_OFFSET(CInstanceGuildMember, GuildWarKill), 7 },
    { "GuildWarKilled", 0, FIELD_OFFSET(CInstanceGuildMember, GuildWarKilled), 8 },
    { "Nickname", 0, FIELD_OFFSET(CInstanceGuildMember, Nickname), 9 },
    { "RefObjID", 0, FIELD_OFFSET(CInstanceGuildMember, RefObjID), 1 },
    { "SiegeAuthority", 0, FIELD_OFFSET(CInstanceGuildMember, SiegeAuthority), 10 },
};

SR_IMPLEMENT_RECORD(CInstanceGuildMember, CDBRecord, "_GUILDMEMBER", 0x5, s_aColumnsCInstanceGuildMember, _countof(s_aColumnsCInstanceGuildMember))

// 0x524190
void CInstanceGuildMember::InitData()
{
    SR_SET_COLUMN(GuildID, 0, 0);
    SR_SET_COLUMN(CharID, 0, 0);
    CharName.clear();
    SR_SET_COLUMN(MemberClass, 0, 3);
    SR_SET_COLUMN(CharLevel, 0, 4);
    SR_SET_COLUMN(GP_Donation, 0, 2);
    SR_SET_COLUMN(Permission, 0, 5);
    SR_SET_COLUMN(Contribution, 0, 6);
    SR_SET_COLUMN(GuildWarKill, 0, 7);
    SR_SET_COLUMN(GuildWarKilled, 0, 8);
    Nickname.clear();
    SR_SET_COLUMN(RefObjID, 0, 1);
    SR_SET_COLUMN(SiegeAuthority, 0, 10);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceAlliedClan (_ALLIEDCLANS): ctor 0x52fd10, dtor 0x52fd70, slots 2 / 3 0x524330 / 0x524350, table description 0x63e470.

static const DBRecordColumnDef s_aColumnsCInstanceAlliedClan[] =
{
    { "ID", 0, FIELD_OFFSET(CInstanceAlliedClan, ID), 0 },
    { "Ally", 1, FIELD_OFFSET(CInstanceAlliedClan, Ally[0]), 2 },
    { "Ally", 2, FIELD_OFFSET(CInstanceAlliedClan, Ally[1]), 2 },
    { "Ally", 3, FIELD_OFFSET(CInstanceAlliedClan, Ally[2]), 2 },
    { "Ally", 4, FIELD_OFFSET(CInstanceAlliedClan, Ally[3]), 2 },
    { "Ally", 5, FIELD_OFFSET(CInstanceAlliedClan, Ally[4]), 2 },
    { "Ally", 6, FIELD_OFFSET(CInstanceAlliedClan, Ally[5]), 2 },
    { "Ally", 7, FIELD_OFFSET(CInstanceAlliedClan, Ally[6]), 2 },
    { "Ally", 8, FIELD_OFFSET(CInstanceAlliedClan, Ally[7]), 2 },
    { "LastCrestRev", 0, FIELD_OFFSET(CInstanceAlliedClan, LastCrestRev), 3 },
    { "CurCrestRev", 0, FIELD_OFFSET(CInstanceAlliedClan, CurCrestRev), 3 },
};

SR_IMPLEMENT_RECORD(CInstanceAlliedClan, CDBRecord, "_ALLIEDCLANS", 0x5, s_aColumnsCInstanceAlliedClan, _countof(s_aColumnsCInstanceAlliedClan))

// 0x524360
void CInstanceAlliedClan::InitData()
{
    SR_SET_COLUMN(ID, 0, 0);
    for (int i = 0; i < 8; ++i)
        SetAlly(i, 0);
    SR_SET_COLUMN(LastCrestRev, 0, 3);
    SR_SET_COLUMN(CurCrestRev, 0, 3);
    ClearAllDirty();
}

void CInstanceAlliedClan::SetAlly(int nIndex, int nGuildID)
{
    if (nIndex < 0 || nIndex >= 8)
        BSAssertFail();
    SR_SET_COLUMN(Ally[nIndex], nGuildID, 2);
}

// ---------------------------------------------------------------------------------------------
// CInstanceGuildWar (_GUILDWAR): ctor 0x52fdc0, dtor 0x52fe20, slots 2 / 3 0x524410 / 0x524430, table description 0x63f3b0.

static const DBRecordColumnDef s_aColumnsCInstanceGuildWar[] =
{
    { "ID", 0, FIELD_OFFSET(CInstanceGuildWar, ID), 0 },
    { "WarType", 0, FIELD_OFFSET(CInstanceGuildWar, WarType), 2 },
    { "VictoryPointIndex", 0, FIELD_OFFSET(CInstanceGuildWar, VictoryPointIndex), 2 },
    { "LodgedGold", 0, FIELD_OFFSET(CInstanceGuildWar, LodgedGold), 2 },
    { "WarEndTime", 0, FIELD_OFFSET(CInstanceGuildWar, WarEndTime), 2 },
    { "Guild", 1, FIELD_OFFSET(CInstanceGuildWar, Guild[0]), 3 },
    { "Guild", 2, FIELD_OFFSET(CInstanceGuildWar, Guild[1]), 3 },
    { "PointGain", 1, FIELD_OFFSET(CInstanceGuildWar, PointGain[0]), 3 },
    { "PointGain", 2, FIELD_OFFSET(CInstanceGuildWar, PointGain[1]), 3 },
    { "Data1", 0, FIELD_OFFSET(CInstanceGuildWar, Data1), 4 },
    { "Data2", 0, FIELD_OFFSET(CInstanceGuildWar, Data2), 4 },
};

SR_IMPLEMENT_RECORD(CInstanceGuildWar, CDBRecord, "_GUILDWAR", 0x5, s_aColumnsCInstanceGuildWar, _countof(s_aColumnsCInstanceGuildWar))

// 0x524440
void CInstanceGuildWar::InitData()
{
    SR_SET_COLUMN(ID, 0, 0);
    SR_SET_COLUMN(WarType, 0, 2);
    memset(&WarEndTime, 0, sizeof(WarEndTime));
    SR_SET_COLUMN(VictoryPointIndex, 0, 2);
    SR_SET_COLUMN(LodgedGold, 0, 2);
    SR_SET_COLUMN(Guild[0], 0, 3);
    SR_SET_COLUMN(Guild[1], 0, 3);
    SR_SET_COLUMN(PointGain[0], 0, 3);
    SR_SET_COLUMN(PointGain[1], 0, 3);
    SR_SET_COLUMN(Data1, 0, 4);
    SR_SET_COLUMN(Data2, 0, 4);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceGPHistory (_GPHistory): ctor 0x52fb00, dtor 0x52fb70, slots 2 / 3 0x524690 / 0x5246b0, table description 0x640ba0.
// InitData stores directly (the table has no update statements).

static const DBRecordColumnDef s_aColumnsCInstanceGPHistory[] =
{
    { "ID", 0, FIELD_OFFSET(CInstanceGPHistory, ID), 0 },
    { "GuildID", 0, FIELD_OFFSET(CInstanceGPHistory, GuildID), 2 },
    { "UsedTime", 0, FIELD_OFFSET(CInstanceGPHistory, UsedTime), 2 },
    { "CharName", 0, FIELD_OFFSET(CInstanceGPHistory, CharName), 2 },
    { "UsedGP", 0, FIELD_OFFSET(CInstanceGPHistory, UsedGP), 2 },
    { "Reason", 0, FIELD_OFFSET(CInstanceGPHistory, Reason), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceGPHistory, CDBRecord, "_GPHistory", 0x16, s_aColumnsCInstanceGPHistory, _countof(s_aColumnsCInstanceGPHistory))

// 0x5246c0
void CInstanceGPHistory::InitData()
{
    ClearAllDirty();
    ID = 0;
    GuildID = 0;
    UsedGP = 0;
    Reason = 0;
}

// ---------------------------------------------------------------------------------------------
// CInstanceFriend (_Friend): ctor 0x530080, dtor 0x5300f0, slots 2 / 3 0x5245f0 / 0x524610, table description 0x640560.

static const DBRecordColumnDef s_aColumnsCInstanceFriend[] =
{
    { "CharID", 0, FIELD_OFFSET(CInstanceFriend, CharID), 0 },
    { "FriendCharID", 0, FIELD_OFFSET(CInstanceFriend, FriendCharID), 2 },
    { "FriendCharName", 0, FIELD_OFFSET(CInstanceFriend, FriendCharName), 2 },
    { "RefObjID", 0, FIELD_OFFSET(CInstanceFriend, RefObjID), 1 },
};

SR_IMPLEMENT_RECORD(CInstanceFriend, CDBRecord, "_Friend", 0x5, s_aColumnsCInstanceFriend, _countof(s_aColumnsCInstanceFriend))

// 0x524620
void CInstanceFriend::InitData()
{
    SR_SET_COLUMN(CharID, 0, 0);
    SR_SET_COLUMN(FriendCharID, 0, 2);
    FriendCharName.clear();
    SR_SET_COLUMN(RefObjID, 0, 1);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceMemo (_Memo): ctor 0x530170, dtor 0x5301f0, slots 2 / 3 0x5246f0 / 0x524710, table description 0x6413d0.

static const DBRecordColumnDef s_aColumnsCInstanceMemo[] =
{
    { "ID64", 0, FIELD_OFFSET(CInstanceMemo, ID64), 0 },
    { "FromCharName", 0, FIELD_OFFSET(CInstanceMemo, FromCharName), 1 },
    { "Message", 0, FIELD_OFFSET(CInstanceMemo, Message), 1 },
    { "Date", 0, FIELD_OFFSET(CInstanceMemo, Date), 1 },
    { "Status", 0, FIELD_OFFSET(CInstanceMemo, Status), 2 },
    { "RefObjID", 0, FIELD_OFFSET(CInstanceMemo, RefObjID), 1 },
};

SR_IMPLEMENT_RECORD(CInstanceMemo, CDBRecord, "_Memo", 0x5, s_aColumnsCInstanceMemo, _countof(s_aColumnsCInstanceMemo))

// 0x524720
void CInstanceMemo::InitData()
{
    SR_SET_COLUMN(ID64, 0, 0);
    FromCharName.clear();
    Message.clear();
    SR_SET_COLUMN(Status, 0, 2);
    SR_SET_COLUMN(RefObjID, 0, 1);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceTrainingCamp (_TRAININGCAMP): ctor 0x530600, dtor 0x530680, slots 2 / 3 0x523c20 / 0x523c40, table description 0x63a190.

static const DBRecordColumnDef s_aColumnsCInstanceTrainingCamp[] =
{
    { "ID", 0, FIELD_OFFSET(CInstanceTrainingCamp, ID), 0 },
    { "CreationDate", 0, FIELD_OFFSET(CInstanceTrainingCamp, CreationDate), 1 },
    { "Rank", 0, FIELD_OFFSET(CInstanceTrainingCamp, Rank), 2 },
    { "GraduateCount", 0, FIELD_OFFSET(CInstanceTrainingCamp, GraduateCount), 3 },
    { "EvaluationPoint", 0, FIELD_OFFSET(CInstanceTrainingCamp, EvaluationPoint), 3 },
    { "LatestEvaluationDate", 0, FIELD_OFFSET(CInstanceTrainingCamp, LatestEvaluationDate), 3 },
    { "CommentTitle", 0, FIELD_OFFSET(CInstanceTrainingCamp, CommentTitle), 4 },
    { "Comment", 0, FIELD_OFFSET(CInstanceTrainingCamp, Comment), 4 },
};

SR_IMPLEMENT_RECORD(CInstanceTrainingCamp, CDBRecord, "_TRAININGCAMP", 0x5, s_aColumnsCInstanceTrainingCamp, _countof(s_aColumnsCInstanceTrainingCamp))

// 0x523c50
void CInstanceTrainingCamp::InitData()
{
    SR_SET_COLUMN(ID, 0, 0);
    SR_SET_COLUMN(GraduateCount, 0, 3);
    SR_SET_COLUMN(Rank, 0, 2);
    SR_SET_COLUMN(EvaluationPoint, 0, 3);
    memset(&CreationDate, 0, sizeof(CreationDate));
    memset(&LatestEvaluationDate, 0, sizeof(LatestEvaluationDate));
    CommentTitle.clear();
    Comment.clear();
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceTrainingCampMember (_TRAININGCAMPMEMBER): ctor 0x530720, dtor 0x530790, slots 2 / 3 0x523d10 / 0x523d30, table description 0x63abb0.

static const DBRecordColumnDef s_aColumnsCInstanceTrainingCampMember[] =
{
    { "CampID", 0, FIELD_OFFSET(CInstanceTrainingCampMember, CampID), 0 },
    { "CharID", 0, FIELD_OFFSET(CInstanceTrainingCampMember, CharID), 0 },
    { "RefObjID", 0, FIELD_OFFSET(CInstanceTrainingCampMember, RefObjID), 1 },
    { "CharName", 0, FIELD_OFFSET(CInstanceTrainingCampMember, CharName), 1 },
    { "JoinDate", 0, FIELD_OFFSET(CInstanceTrainingCampMember, JoinDate), 1 },
    { "MemberClass", 0, FIELD_OFFSET(CInstanceTrainingCampMember, MemberClass), 1 },
    { "CharJoinedLevel", 0, FIELD_OFFSET(CInstanceTrainingCampMember, CharJoinedLevel), 1 },
    { "CharCurLevel", 0, FIELD_OFFSET(CInstanceTrainingCampMember, CharCurLevel), 2 },
    { "CharMaxLevel", 0, FIELD_OFFSET(CInstanceTrainingCampMember, CharMaxLevel), 2 },
    { "HonorPoint", 0, FIELD_OFFSET(CInstanceTrainingCampMember, HonorPoint), 3 },
};

SR_IMPLEMENT_RECORD(CInstanceTrainingCampMember, CDBRecord, "_TRAININGCAMPMEMBER", 0x5, s_aColumnsCInstanceTrainingCampMember, _countof(s_aColumnsCInstanceTrainingCampMember))

// 0x523d40
void CInstanceTrainingCampMember::InitData()
{
    SR_SET_COLUMN(CampID, 0, 0);
    SR_SET_COLUMN(CharID, 0, 0);
    SR_SET_COLUMN(RefObjID, 0, 1);
    SR_SET_COLUMN(MemberClass, 0, 1);
    SR_SET_COLUMN(CharJoinedLevel, 0, 1);
    SR_SET_COLUMN(CharCurLevel, 0, 2);
    SR_SET_COLUMN(CharMaxLevel, 0, 2);
    SR_SET_COLUMN(HonorPoint, 0, 3);
    memset(&JoinDate, 0, sizeof(JoinDate));
    CharName.clear();
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceTrainingCampHonorRank (_TRAININGCAMPHONORRANK): ctor 0x531400, dtor 0x531460, slots 2 / 3 0x523e50 / 0x523e70, table description 0x63b7c0.

static const DBRecordColumnDef s_aColumnsCInstanceTrainingCampHonorRank[] =
{
    { "Ranking", 0, FIELD_OFFSET(CInstanceTrainingCampHonorRank, Ranking), 0 },
    { "CampID", 0, FIELD_OFFSET(CInstanceTrainingCampHonorRank, CampID), 3 },
    { "Rank", 0, FIELD_OFFSET(CInstanceTrainingCampHonorRank, Rank), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceTrainingCampHonorRank, CDBRecord, "_TRAININGCAMPHONORRANK", 0x5, s_aColumnsCInstanceTrainingCampHonorRank, _countof(s_aColumnsCInstanceTrainingCampHonorRank))

// 0x523e80
void CInstanceTrainingCampHonorRank::InitData()
{
    SR_SET_COLUMN(Ranking, 0, 0);
    SR_SET_COLUMN(CampID, 0, 3);
    SR_SET_COLUMN(Rank, 0, 2);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceTrainingCampBuffStatus (_TRAININGCAMPBUFFSTATUS): ctor 0x530810, dtor 0x530870, slots 2 / 3 0x523ef0 / 0x523f10, table description 0x63bd20.

static const DBRecordColumnDef s_aColumnsCInstanceTrainingCampBuffStatus[] =
{
    { "CampID", 0, FIELD_OFFSET(CInstanceTrainingCampBuffStatus, CampID), 0 },
    { "RecipientCharID", 0, FIELD_OFFSET(CInstanceTrainingCampBuffStatus, RecipientCharID), 0 },
    { "BuffSlotIdx", 0, FIELD_OFFSET(CInstanceTrainingCampBuffStatus, BuffSlotIdx), 0 },
    { "DonorCharID", 0, FIELD_OFFSET(CInstanceTrainingCampBuffStatus, DonorCharID), 2 },
    { "StartingTime", 0, FIELD_OFFSET(CInstanceTrainingCampBuffStatus, StartingTime), 3 },
    { "RemainBuffPoint", 0, FIELD_OFFSET(CInstanceTrainingCampBuffStatus, RemainBuffPoint), 4 },
    { "BuffType", 0, FIELD_OFFSET(CInstanceTrainingCampBuffStatus, BuffType), 3 },
};

SR_IMPLEMENT_RECORD(CInstanceTrainingCampBuffStatus, CDBRecord, "_TRAININGCAMPBUFFSTATUS", 0x5, s_aColumnsCInstanceTrainingCampBuffStatus, _countof(s_aColumnsCInstanceTrainingCampBuffStatus))

// 0x523f20
void CInstanceTrainingCampBuffStatus::InitData()
{
    SR_SET_COLUMN(CampID, 0, 0);
    SR_SET_COLUMN(RecipientCharID, 0, 0);
    SR_SET_COLUMN(DonorCharID, 0, 2);
    SR_SET_COLUMN(RemainBuffPoint, 0, 4);
    SR_SET_COLUMN(BuffType, 0, 3);
    ClearAllDirty();
}

// =============================================================================================
// Siege fortress records

// ---------------------------------------------------------------------------------------------
// CInstanceSiegeFortress (_SiegeFortress): ctor 0x401500, dtor None, slots 2 / 3 0x401180 / 0x4011a0, table description 0x646880.

static const DBRecordColumnDef s_aColumnsCInstanceSiegeFortress[] =
{
    { "FortressID", 0, FIELD_OFFSET(CInstanceSiegeFortress, FortressID), 0 },
    { "GuildID", 0, FIELD_OFFSET(CInstanceSiegeFortress, GuildID), 2 },
    { "TaxRatio", 0, FIELD_OFFSET(CInstanceSiegeFortress, TaxRatio), 3 },
    { "Tax", 0, FIELD_OFFSET(CInstanceSiegeFortress, Tax), 4 },
    { "NPCHired", 0, FIELD_OFFSET(CInstanceSiegeFortress, NPCHired), 5 },
    { "TempGuildID", 0, FIELD_OFFSET(CInstanceSiegeFortress, TempGuildID), 6 },
    { "Introduction", 0, FIELD_OFFSET(CInstanceSiegeFortress, Introduction), 7 },
    { "CreatedDungeonTime", 0, FIELD_OFFSET(CInstanceSiegeFortress, CreatedDungeonTime), 8 },
    { "CreatedDungeonCount", 0, FIELD_OFFSET(CInstanceSiegeFortress, CreatedDungeonCount), 9 },
    { "IntroductionModificationPermission", 0, FIELD_OFFSET(CInstanceSiegeFortress, IntroductionModificationPermission), 10 },
};

SR_IMPLEMENT_RECORD(CInstanceSiegeFortress, CDBRecord, "_SiegeFortress", 0x5, s_aColumnsCInstanceSiegeFortress, _countof(s_aColumnsCInstanceSiegeFortress))

// 0x401290
void CInstanceSiegeFortress::InitData()
{
    SR_SET_COLUMN(FortressID, 0, 0);
    SR_SET_COLUMN(GuildID, 0, 2);
    SR_SET_COLUMN(TaxRatio, 0, 3);
    SR_SET_COLUMN(Tax, 0, 4);
    SR_SET_COLUMN(NPCHired, 0, 5);
    SR_SET_COLUMN(TempGuildID, 0, 6);
    Introduction.clear();
    SR_SET_COLUMN(CreatedDungeonCount, 0, 9);
    SR_SET_COLUMN(IntroductionModificationPermission, 0, 10);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceSiegeFortressObject (_SiegeFortressObject): ctor 0x5314b0, dtor 0x5dd9e0, slots 2 / 3 0x525400 / -, table description 0x647490.
// InitData keeps the reference object pointer.

static const DBRecordColumnDef s_aColumnsCInstanceSiegeFortressObject[] =
{
    { "ID", 0, FIELD_OFFSET(CInstanceSiegeFortressObject, ID), 0 },
    { "FortressID", 0, FIELD_OFFSET(CInstanceSiegeFortressObject, FortressID), 0 },
    { "OwnerGuildID", 0, FIELD_OFFSET(CInstanceSiegeFortressObject, OwnerGuildID), 1 },
    { "RefObjID", 0, FIELD_OFFSET(CInstanceSiegeFortressObject, RefObjID), 1 },
    { "HP", 0, FIELD_OFFSET(CInstanceSiegeFortressObject, HP), 2 },
    { "Region", 0, FIELD_OFFSET(CInstanceSiegeFortressObject, Region), 3 },
    { "PosX", 0, FIELD_OFFSET(CInstanceSiegeFortressObject, PosX), 3 },
    { "PosY", 0, FIELD_OFFSET(CInstanceSiegeFortressObject, PosY), 3 },
    { "PosZ", 0, FIELD_OFFSET(CInstanceSiegeFortressObject, PosZ), 3 },
    { "Direction", 0, FIELD_OFFSET(CInstanceSiegeFortressObject, Direction), 3 },
    { "OwnerLevel", 0, FIELD_OFFSET(CInstanceSiegeFortressObject, OwnerLevel), 1 },
};

SR_IMPLEMENT_RECORD(CInstanceSiegeFortressObject, CInstanceChar, "_SiegeFortressObject", 0x15, s_aColumnsCInstanceSiegeFortressObject, _countof(s_aColumnsCInstanceSiegeFortressObject))

// 0x5254c0
void CInstanceSiegeFortressObject::InitData()
{
    SR_SET_COLUMN(ID, 0, 0);
    SR_SET_COLUMN(FortressID, 0, 0);
    SR_SET_COLUMN(OwnerGuildID, 0, 1);
    SR_SET_COLUMN(RefObjID, 0, 1);
    SR_SET_COLUMN(HP, 0, 2);
    SR_SET_COLUMN(Region, 0, 3);
    SR_SET_COLUMN(PosX, 0.0f, 3);
    SR_SET_COLUMN(PosY, 0.0f, 3);
    SR_SET_COLUMN(PosZ, 0.0f, 3);
    SR_SET_COLUMN(Direction, 0.0f, 3);
    SR_SET_COLUMN(OwnerLevel, 0, 1);
    ClearAllDirty();
}

void CInstanceSiegeFortressObject::ResolveRefObj()
{
    if (m_pRefObj != NULL)
        BSAssertFail();
    m_pRefObj = ReferenceData_FindRefObj(RefObjID);
}

// 0x5254a0
void CInstanceSiegeFortressObject::Write(CByteStream* pStream, int nMode)
{
    (void)nMode;
    pStream->Write<DWORD>(RefObjID);
}

const char* CInstanceSiegeFortressObject::GetName()
{
    if (m_pRefObj == NULL)
        BSAssertFail();
    return RefObj_GetCodeName(m_pRefObj).c_str();
}

BYTE CInstanceSiegeFortressObject::GetLevel()
{
    return RefObj_GetByte198(m_pRefObj);
}

int CInstanceSiegeFortressObject::GetHP()
{
    return HP;
}

int CInstanceSiegeFortressObject::GetMP()
{
    return 0;
}

// 0x525660
void CInstanceSiegeFortressObject::SetHP(int nHP)
{
    SR_SET_COLUMN(HP, nHP, 2);
}

void CInstanceSiegeFortressObject::SetMP(int nMP)
{
    (void)nMP;
}

// ---------------------------------------------------------------------------------------------
// CInstanceSiegeFortressStruct (_SIEGEFORTRESSSTRUCT): ctor 0x530970, dtor None, slots 2 / 3 0x523590 / 0x5235b0, table description 0x638e10.

static const DBRecordColumnDef s_aColumnsCInstanceSiegeFortressStruct[] =
{
    { "FortressID", 0, FIELD_OFFSET(CInstanceSiegeFortressStruct, FortressID), 2 },
    { "OwnerGuildID", 0, FIELD_OFFSET(CInstanceSiegeFortressStruct, OwnerGuildID), 3 },
    { "RefEventStructID", 0, FIELD_OFFSET(CInstanceSiegeFortressStruct, RefEventStructID), 0 },
    { "RefObjID", 0, FIELD_OFFSET(CInstanceSiegeFortressStruct, RefObjID), 0 },
    { "HP", 0, FIELD_OFFSET(CInstanceSiegeFortressStruct, HP), 3 },
    { "MakeDate", 0, FIELD_OFFSET(CInstanceSiegeFortressStruct, MakeDate), 3 },
    { "State", 0, FIELD_OFFSET(CInstanceSiegeFortressStruct, State), 3 },
};

SR_IMPLEMENT_RECORD(CInstanceSiegeFortressStruct, CInstanceChar, "_SIEGEFORTRESSSTRUCT", 0x5, s_aColumnsCInstanceSiegeFortressStruct, _countof(s_aColumnsCInstanceSiegeFortressStruct))

// 0x5235f0
void CInstanceSiegeFortressStruct::InitData()
{
    SR_SET_COLUMN(FortressID, 0, 2);
    SR_SET_COLUMN(OwnerGuildID, 0, 3);
    SR_SET_COLUMN(RefEventStructID, 0, 0);
    SR_SET_COLUMN(RefObjID, 0, 0);
    SR_SET_COLUMN(HP, 0, 3);
    SR_SET_COLUMN(State, 0, 3);
    CInstanceObj::InitData();
}

void CInstanceSiegeFortressStruct::ResolveRefObj()
{
    if (m_pRefObj != NULL)
        BSAssertFail();
    m_pRefObj = ReferenceData_FindRefObj(RefObjID);
}

// 0x5235d0
void CInstanceSiegeFortressStruct::Write(CByteStream* pStream, int nMode)
{
    (void)nMode;
    pStream->Write<DWORD>(RefObjID);
    pStream->Write<DWORD>(HP);
}

const char* CInstanceSiegeFortressStruct::GetName()
{
    if (m_pRefObj == NULL)
        BSAssertFail();
    return RefObj_GetCodeName(m_pRefObj).c_str();
}

BYTE CInstanceSiegeFortressStruct::GetLevel()
{
    return RefObj_GetByte198(m_pRefObj);
}

int CInstanceSiegeFortressStruct::GetHP()
{
    return HP;
}

int CInstanceSiegeFortressStruct::GetMP()
{
    return 0;
}

// 0x523720
void CInstanceSiegeFortressStruct::SetHP(int nHP)
{
    SR_SET_COLUMN(HP, nHP, 3);
}

void CInstanceSiegeFortressStruct::SetMP(int nMP)
{
    (void)nMP;
}

// 0x5236d0
short CInstanceSiegeFortressStruct::GetState()
{
    return State;
}

// 0x5236e0
void CInstanceSiegeFortressStruct::SetState(BYTE byState)
{
    SR_SET_COLUMN(State, byState, 3);
}

// ---------------------------------------------------------------------------------------------
// CInstanceSiegeFortressItemForge (_SiegeFortressItemForge): ctor 0x531560, dtor 0x5315c0, slots 2 / 3 0x5256a0 / 0x5256c0, table description 0x6481c0.

static const DBRecordColumnDef s_aColumnsCInstanceSiegeFortressItemForge[] =
{
    { "FortressID", 0, FIELD_OFFSET(CInstanceSiegeFortressItemForge, FortressID), 0 },
    { "ItemRefID", 0, FIELD_OFFSET(CInstanceSiegeFortressItemForge, ItemRefID), 0 },
    { "Amount", 0, FIELD_OFFSET(CInstanceSiegeFortressItemForge, Amount), 2 },
    { "Finished", 0, FIELD_OFFSET(CInstanceSiegeFortressItemForge, Finished), 3 },
    { "StartDate", 0, FIELD_OFFSET(CInstanceSiegeFortressItemForge, StartDate), 1 },
    { "FinishDate", 0, FIELD_OFFSET(CInstanceSiegeFortressItemForge, FinishDate), 1 },
};

SR_IMPLEMENT_RECORD(CInstanceSiegeFortressItemForge, CDBRecord, "_SiegeFortressItemForge", 0x15, s_aColumnsCInstanceSiegeFortressItemForge, _countof(s_aColumnsCInstanceSiegeFortressItemForge))

// 0x5256d0
void CInstanceSiegeFortressItemForge::InitData()
{
    SR_SET_COLUMN(FortressID, 0, 0);
    SR_SET_COLUMN(ItemRefID, 0, 0);
    SR_SET_COLUMN(Amount, 0, 2);
    SR_SET_COLUMN(Finished, 0, 3);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceSiegeFortressRequest (_SiegeFortressRequest): ctor 0x5308c0, dtor 0x530920, slots 2 / 3 0x525760 / 0x525780, table description 0x648a00.

static const DBRecordColumnDef s_aColumnsCInstanceSiegeFortressRequest[] =
{
    { "FortressID", 0, FIELD_OFFSET(CInstanceSiegeFortressRequest, FortressID), 0 },
    { "GuildID", 0, FIELD_OFFSET(CInstanceSiegeFortressRequest, GuildID), 0 },
    { "RequestType", 0, FIELD_OFFSET(CInstanceSiegeFortressRequest, RequestType), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceSiegeFortressRequest, CDBRecord, "_SiegeFortressRequest", 0x15, s_aColumnsCInstanceSiegeFortressRequest, _countof(s_aColumnsCInstanceSiegeFortressRequest))

// 0x525790
void CInstanceSiegeFortressRequest::InitData()
{
    SR_SET_COLUMN(FortressID, 0, 0);
    SR_SET_COLUMN(GuildID, 0, 0);
    SR_SET_COLUMN(RequestType, 0, 2);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceSiegeFortressBattleRecord (_SiegeFortressBattleRecord): ctor 0x531610, dtor 0x531670, slots 2 / 3 0x525800 / 0x525820, table description 0x648f60.

static const DBRecordColumnDef s_aColumnsCInstanceSiegeFortressBattleRecord[] =
{
    { "FortressID", 0, FIELD_OFFSET(CInstanceSiegeFortressBattleRecord, FortressID), 0 },
    { "CharID", 0, FIELD_OFFSET(CInstanceSiegeFortressBattleRecord, CharID), 0 },
    { "KillCount", 0, FIELD_OFFSET(CInstanceSiegeFortressBattleRecord, KillCount), 2 },
    { "KilledCount", 0, FIELD_OFFSET(CInstanceSiegeFortressBattleRecord, KilledCount), 2 },
    { "RankUpDate", 0, FIELD_OFFSET(CInstanceSiegeFortressBattleRecord, RankUpDate), 3 },
    { "CurRank", 0, FIELD_OFFSET(CInstanceSiegeFortressBattleRecord, CurRank), 3 },
};

SR_IMPLEMENT_RECORD(CInstanceSiegeFortressBattleRecord, CDBRecord, "_SiegeFortressBattleRecord", 0x15, s_aColumnsCInstanceSiegeFortressBattleRecord, _countof(s_aColumnsCInstanceSiegeFortressBattleRecord))

// 0x525830
void CInstanceSiegeFortressBattleRecord::InitData()
{
    SR_SET_COLUMN(FortressID, 0, 0);
    SR_SET_COLUMN(CharID, 0, 0);
    SR_SET_COLUMN(KillCount, 0, 2);
    SR_SET_COLUMN(KilledCount, 0, 2);
    SR_SET_COLUMN(CurRank, 0, 3);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceSiegeFortressStoneState (_SiegeFortressStoneState): ctor 0x5316c0, dtor 0x531720, slots 2 / 3 0x5258e0 / 0x525900, table description 0x6497a0.

static const DBRecordColumnDef s_aColumnsCInstanceSiegeFortressStoneState[] =
{
    { "FortressID", 0, FIELD_OFFSET(CInstanceSiegeFortressStoneState, FortressID), 0 },
    { "GuildID", 0, FIELD_OFFSET(CInstanceSiegeFortressStoneState, GuildID), 0 },
    { "AccumulateDamage", 0, FIELD_OFFSET(CInstanceSiegeFortressStoneState, AccumulateDamage), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceSiegeFortressStoneState, CDBRecord, "_SiegeFortressStoneState", 0x15, s_aColumnsCInstanceSiegeFortressStoneState, _countof(s_aColumnsCInstanceSiegeFortressStoneState))

// 0x525910
void CInstanceSiegeFortressStoneState::InitData()
{
    SR_SET_COLUMN(FortressID, 0, 0);
    SR_SET_COLUMN(GuildID, 0, 0);
    SR_SET_COLUMN(AccumulateDamage, 0, 2);
    ClearAllDirty();
}

// =============================================================================================
// Market, event and log records

// ---------------------------------------------------------------------------------------------
// CInstanceOpenMarket (_OpenMarket): ctor 0x530ce0, dtor 0x530d50, slots 2 / 3 0x525f80 / 0x525fa0, table description 0x651050.
// InitData leaves the dirty bits as they are.

static const DBRecordColumnDef s_aColumnsCInstanceOpenMarket[] =
{
    { "JID", 0, FIELD_OFFSET(CInstanceOpenMarket, JID), 0 },
    { "PersnalID", 0, FIELD_OFFSET(CInstanceOpenMarket, PersnalID), 0 },
    { "CharName16", 0, FIELD_OFFSET(CInstanceOpenMarket, CharName16), 0 },
    { "Status", 0, FIELD_OFFSET(CInstanceOpenMarket, Status), 2 },
    { "RefItemID", 0, FIELD_OFFSET(CInstanceOpenMarket, RefItemID), 2 },
    { "TidGroupID", 0, FIELD_OFFSET(CInstanceOpenMarket, TidGroupID), 2 },
    { "ItemClass", 0, FIELD_OFFSET(CInstanceOpenMarket, ItemClass), 2 },
    { "ItemID", 0, FIELD_OFFSET(CInstanceOpenMarket, ItemID), 2 },
    { "SellCnt", 0, FIELD_OFFSET(CInstanceOpenMarket, SellCnt), 2 },
    { "RegDate", 0, FIELD_OFFSET(CInstanceOpenMarket, RegDate), 2 },
    { "EndDate", 0, FIELD_OFFSET(CInstanceOpenMarket, EndDate), 2 },
    { "Price", 0, FIELD_OFFSET(CInstanceOpenMarket, Price), 2 },
    { "Deposit", 0, FIELD_OFFSET(CInstanceOpenMarket, Deposit), 2 },
    { "SellFee", 0, FIELD_OFFSET(CInstanceOpenMarket, SellFee), 2 },
    { "UseCash", 0, FIELD_OFFSET(CInstanceOpenMarket, UseCash), 2 },
    { "Serial64", 0, FIELD_OFFSET(CInstanceOpenMarket, Serial64), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceOpenMarket, CDBRecord, "_OpenMarket", 0x5, s_aColumnsCInstanceOpenMarket, _countof(s_aColumnsCInstanceOpenMarket))

// 0x525fb0
void CInstanceOpenMarket::InitData()
{
    SR_SET_COLUMN(JID, 0, 0);
    SR_SET_COLUMN(PersnalID, 0, 0);
    SR_SET_COLUMN(Status, 0, 2);
    SR_SET_COLUMN(RefItemID, 0, 2);
    SR_SET_COLUMN(TidGroupID, 0, 2);
    SR_SET_COLUMN(ItemClass, 0, 2);
    SR_SET_COLUMN(ItemID, 0, 2);
    SR_SET_COLUMN(SellCnt, 0, 2);
    SR_SET_COLUMN(Price, 0, 2);
    SR_SET_COLUMN(Deposit, 0, 2);
    SR_SET_COLUMN(SellFee, 0, 2);
    CharName16.clear();
    memset(&RegDate, 0, sizeof(RegDate));
    memset(&EndDate, 0, sizeof(EndDate));
}

// ---------------------------------------------------------------------------------------------
// CInstanceFleaMarketNetwork (_FleaMarketNetwork): ctor 0x531140, dtor 0x5311a0, slots 2 / 3 0x5252b0 / 0x5252d0, table description 0x645e50.
// InitData leaves the dirty bits as they are.

static const DBRecordColumnDef s_aColumnsCInstanceFleaMarketNetwork[] =
{
    { "AbleOpen", 0, FIELD_OFFSET(CInstanceFleaMarketNetwork, AbleOpen), 2 },
    { "CharID", 0, FIELD_OFFSET(CInstanceFleaMarketNetwork, CharID), 0 },
    { "Slot", 0, FIELD_OFFSET(CInstanceFleaMarketNetwork, Slot), 0 },
    { "TidGroupID", 0, FIELD_OFFSET(CInstanceFleaMarketNetwork, TidGroupID), 2 },
    { "ItemClass", 0, FIELD_OFFSET(CInstanceFleaMarketNetwork, ItemClass), 2 },
    { "ItemCount", 0, FIELD_OFFSET(CInstanceFleaMarketNetwork, ItemCount), 2 },
    { "MakeZone", 0, FIELD_OFFSET(CInstanceFleaMarketNetwork, MakeZone), 2 },
    { "Cash", 0, FIELD_OFFSET(CInstanceFleaMarketNetwork, Cash), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceFleaMarketNetwork, CDBRecord, "_FleaMarketNetwork", 0x5, s_aColumnsCInstanceFleaMarketNetwork, _countof(s_aColumnsCInstanceFleaMarketNetwork))

// 0x5252e0
void CInstanceFleaMarketNetwork::InitData()
{
    SR_SET_COLUMN(AbleOpen, 0, 2);
    SR_SET_COLUMN(CharID, 0, 0);
    SR_SET_COLUMN(Slot, 0, 0);
    SR_SET_COLUMN(TidGroupID, 0, 2);
    SR_SET_COLUMN(ItemClass, 0, 2);
    SR_SET_COLUMN(ItemCount, 0, 2);
    SR_SET_COLUMN(MakeZone, 0, 2);
    SR_SET_COLUMN(Cash, 0, 2);
}

// ---------------------------------------------------------------------------------------------
// CInstanceShopItemStockQuantity (_ShopItemStockQuantity): ctor 0x5311f0, dtor 0x531270, slots 2 / 3 0x525d30 / 0x525d50, table description 0x64e740.

static const DBRecordColumnDef s_aColumnsCInstanceShopItemStockQuantity[] =
{
    { "Service", 0, FIELD_OFFSET(CInstanceShopItemStockQuantity, Service), 0 },
    { "Country", 0, FIELD_OFFSET(CInstanceShopItemStockQuantity, Country), 0 },
    { "RefShopGroupCodeName", 0, FIELD_OFFSET(CInstanceShopItemStockQuantity, RefShopGroupCodeName), 1 },
    { "RefPackageItemCodeName", 0, FIELD_OFFSET(CInstanceShopItemStockQuantity, RefPackageItemCodeName), 1 },
    { "ConstStockQuantity", 0, FIELD_OFFSET(CInstanceShopItemStockQuantity, ConstStockQuantity), 1 },
    { "StockQuantity", 0, FIELD_OFFSET(CInstanceShopItemStockQuantity, StockQuantity), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceShopItemStockQuantity, CDBRecord, "_ShopItemStockQuantity", 0x5, s_aColumnsCInstanceShopItemStockQuantity, _countof(s_aColumnsCInstanceShopItemStockQuantity))

// 0x525d60
void CInstanceShopItemStockQuantity::InitData()
{
    SR_SET_COLUMN(Service, 0, 0);
    SR_SET_COLUMN(ConstStockQuantity, 0, 1);
    SR_SET_COLUMN(Country, 0, 0);
    RefShopGroupCodeName.clear();
    RefPackageItemCodeName.clear();
    SR_SET_COLUMN(StockQuantity, 0, 2);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceServerEvent (_ServerEvent): ctor 0x5318d0, dtor 0x531930, slots 2 / 3 0x525a30 / 0x525a50, table description 0x64a170.

static const DBRecordColumnDef s_aColumnsCInstanceServerEvent[] =
{
    { "ID", 0, FIELD_OFFSET(CInstanceServerEvent, ID), 0 },
    { "CompletionValue", 0, FIELD_OFFSET(CInstanceServerEvent, CompletionValue), 2 },
    { "AchievementCondition", 0, FIELD_OFFSET(CInstanceServerEvent, AchievementCondition), 2 },
    { "ProgressCount", 0, FIELD_OFFSET(CInstanceServerEvent, ProgressCount), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceServerEvent, CDBRecord, "_ServerEvent", 0x5, s_aColumnsCInstanceServerEvent, _countof(s_aColumnsCInstanceServerEvent))

// 0x525a60
void CInstanceServerEvent::InitData()
{
    SR_SET_COLUMN(ID, 0, 0);
    SR_SET_COLUMN(CompletionValue, 0, 2);
    SR_SET_COLUMN(AchievementCondition, 0, 2);
    SR_SET_COLUMN(ProgressCount, 0, 2);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceServerEventReward (_ServerEventReward): ctor 0x531980, dtor 0x5319e0, slots 2 / 3 0x525b00 / 0x525b20, table description 0x64a7c0.
// BUG(SM-004): InitData resets RewardID without marking its update group.

static const DBRecordColumnDef s_aColumnsCInstanceServerEventReward[] =
{
    { "ServerEventID", 0, FIELD_OFFSET(CInstanceServerEventReward, ServerEventID), 0 },
    { "RewardID", 0, FIELD_OFFSET(CInstanceServerEventReward, RewardID), 2 },
    { "RemainRewardTime", 0, FIELD_OFFSET(CInstanceServerEventReward, RemainRewardTime), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceServerEventReward, CDBRecord, "_ServerEventReward", 0x17, s_aColumnsCInstanceServerEventReward, _countof(s_aColumnsCInstanceServerEventReward))

// 0x525ba0
void CInstanceServerEventReward::InitData()
{
    SR_SET_COLUMN(ServerEventID, 0, 0);
    SR_SET_COLUMN(RewardID, 0, 0);  // BUG(SM-004): no update group
    SR_SET_COLUMN(RemainRewardTime, 0, 2);
    ClearAllDirty();
}

// ---------------------------------------------------------------------------------------------
// CInstanceLogChar (_LOGEVENTCHAR): ctor 0x531c80, dtor 0x531d00, slots 2 / 3 0x525c10 / 0x525c30, table description 0x64ad10.

static const DBRecordColumnDef s_aColumnsCInstanceLogChar[] =
{
    { "EventTime", 0, FIELD_OFFSET(CInstanceLogChar, EventTime), 2 },
    { "EventID", 0, FIELD_OFFSET(CInstanceLogChar, EventID), 2 },
    { "Data1", 0, FIELD_OFFSET(CInstanceLogChar, Data1), 2 },
    { "Data2", 0, FIELD_OFFSET(CInstanceLogChar, Data2), 2 },
    { "EventPos", 0, FIELD_OFFSET(CInstanceLogChar, EventPos), 2 },
    { "strDesc", 0, FIELD_OFFSET(CInstanceLogChar, strDesc), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceLogChar, CDBRecord, "_LOGEVENTCHAR", 0x4, s_aColumnsCInstanceLogChar, _countof(s_aColumnsCInstanceLogChar))

// ---------------------------------------------------------------------------------------------
// CInstanceLogItem (_LOGEVENTITEM): ctor 0x531da0, dtor 0x531e20, slots 2 / 3 0x525c40 / 0x525c60, table description 0x64b540.

static const DBRecordColumnDef s_aColumnsCInstanceLogItem[] =
{
    { "EventTime", 0, FIELD_OFFSET(CInstanceLogItem, EventTime), 2 },
    { "ItemRefID", 0, FIELD_OFFSET(CInstanceLogItem, ItemRefID), 2 },
    { "dwData", 0, FIELD_OFFSET(CInstanceLogItem, dwData), 2 },
    { "TargetStorage", 0, FIELD_OFFSET(CInstanceLogItem, TargetStorage), 2 },
    { "Operation", 0, FIELD_OFFSET(CInstanceLogItem, Operation), 2 },
    { "Slot_From", 0, FIELD_OFFSET(CInstanceLogItem, Slot_From), 2 },
    { "Slot_To", 0, FIELD_OFFSET(CInstanceLogItem, Slot_To), 2 },
    { "EventPos", 0, FIELD_OFFSET(CInstanceLogItem, EventPos), 2 },
    { "strDesc", 0, FIELD_OFFSET(CInstanceLogItem, strDesc), 2 },
    { "Serial64", 0, FIELD_OFFSET(CInstanceLogItem, Serial64), 2 },
    { "Gold", 0, FIELD_OFFSET(CInstanceLogItem, Gold), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceLogItem, CDBRecord, "_LOGEVENTITEM", 0x4, s_aColumnsCInstanceLogItem, _countof(s_aColumnsCInstanceLogItem))

// ---------------------------------------------------------------------------------------------
// CInstanceLogSiegeFortress (_LOGEVENTSIEGEFORTRESS): ctor 0x531ec0, dtor 0x531f30, slots 2 / 3 0x525c70 / 0x525c90, table description 0x64c240.

static const DBRecordColumnDef s_aColumnsCInstanceLogSiegeFortress[] =
{
    { "FortressID", 0, FIELD_OFFSET(CInstanceLogSiegeFortress, FortressID), 2 },
    { "EventTime", 0, FIELD_OFFSET(CInstanceLogSiegeFortress, EventTime), 2 },
    { "EventID", 0, FIELD_OFFSET(CInstanceLogSiegeFortress, EventID), 2 },
    { "CharID", 0, FIELD_OFFSET(CInstanceLogSiegeFortress, CharID), 2 },
    { "Data1", 0, FIELD_OFFSET(CInstanceLogSiegeFortress, Data1), 2 },
    { "Data2", 0, FIELD_OFFSET(CInstanceLogSiegeFortress, Data2), 2 },
    { "strDesc", 0, FIELD_OFFSET(CInstanceLogSiegeFortress, strDesc), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceLogSiegeFortress, CDBRecord, "_LOGEVENTSIEGEFORTRESS", 0x4, s_aColumnsCInstanceLogSiegeFortress, _countof(s_aColumnsCInstanceLogSiegeFortress))

// ---------------------------------------------------------------------------------------------
// CInstanceLogServerEvent (_LOGSERVEREVENT): ctor 0x531fb0, dtor 0x532020, slots 2 / 3 0x525ca0 / 0x525cc0, table description 0x64cb60.

static const DBRecordColumnDef s_aColumnsCInstanceLogServerEvent[] =
{
    { "ID", 0, FIELD_OFFSET(CInstanceLogServerEvent, ID), 2 },
    { "EventTime", 0, FIELD_OFFSET(CInstanceLogServerEvent, EventTime), 2 },
    { "ServerEventID", 0, FIELD_OFFSET(CInstanceLogServerEvent, ServerEventID), 2 },
    { "LogType", 0, FIELD_OFFSET(CInstanceLogServerEvent, LogType), 2 },
    { "strDesc", 0, FIELD_OFFSET(CInstanceLogServerEvent, strDesc), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceLogServerEvent, CDBRecord, "_LOGSERVEREVENT", 0x4, s_aColumnsCInstanceLogServerEvent, _countof(s_aColumnsCInstanceLogServerEvent))

// ---------------------------------------------------------------------------------------------
// CInstanceLogSchedule (_LogSchedule): ctor 0x5320a0, dtor 0x532120, slots 2 / 3 0x525cd0 / 0x525cf0, table description 0x64d2a0.

static const DBRecordColumnDef s_aColumnsCInstanceLogSchedule[] =
{
    { "ID", 0, FIELD_OFFSET(CInstanceLogSchedule, ID), 2 },
    { "ServerType", 0, FIELD_OFFSET(CInstanceLogSchedule, ServerType), 2 },
    { "ServerBodyID", 0, FIELD_OFFSET(CInstanceLogSchedule, ServerBodyID), 2 },
    { "ScheduleDefine", 0, FIELD_OFFSET(CInstanceLogSchedule, ScheduleDefine), 2 },
    { "ScheduleIdx", 0, FIELD_OFFSET(CInstanceLogSchedule, ScheduleIdx), 2 },
    { "Type", 0, FIELD_OFFSET(CInstanceLogSchedule, Type), 2 },
    { "OccureTime", 0, FIELD_OFFSET(CInstanceLogSchedule, OccureTime), 2 },
};

SR_IMPLEMENT_RECORD(CInstanceLogSchedule, CDBRecord, "_LogSchedule", 0x4, s_aColumnsCInstanceLogSchedule, _countof(s_aColumnsCInstanceLogSchedule))
