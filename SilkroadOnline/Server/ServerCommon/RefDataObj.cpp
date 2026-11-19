#include "ServerCommon/RefDataObj.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "BSLib/BSDebug.h"
#include "ServerCommon/RefDataQuest.h"
#include "ServerFramework/ServerDataExtern.h"

static const char s_szReferenceDataFile[] =
    "D:\\WORK2005\\Source\\SilkroadOnline\\Server\\ServerCommon\\ReferenceData.cpp";

PFN_REFOBJ_APPLY_SPECIAL_MONSTER g_pfnApplySpecialMonsterCode = NULL;

// The singleton checks of the readers (dword_8a8944 asserted, then used).
static CReferenceData* RefObj_GetData()
{
    if (!g_pReferenceData)
        BSAssertFail();
    return g_pReferenceData;
}

// ---------------------------------------------------------------------------------------------
// TypeID predicates

BOOL RefObjTID_IsItemEtcType13(const WORD& wTypeID)
{
    return RefTypeID_Is(wTypeID, 0, 3, 1, 13);
}

BOOL RefObj_IsAccessoryType(const WORD& wTypeID)
{
    if (!RefTypeID_Is(wTypeID, 0, 3, 1))
        return FALSE;
    WORD wTID3 = (WORD)((wTypeID >> 7) & 0xF);
    return wTID3 == 5 || wTID3 == 12;
}

BOOL RefObj_IsSiegeGuard_TID4_1(const WORD& wTypeID)
{
    return RefTypeID_Is(wTypeID, 1, 1, 2, 4, 1);
}

BOOL RefObj_IsSiegeGuard_TID4_3(const WORD& wTypeID)
{
    return RefTypeID_Is(wTypeID, 1, 1, 2, 4, 3);
}

BOOL RefObj_IsSiegeGuard_TID4_4(const WORD& wTypeID)
{
    return RefTypeID_Is(wTypeID, 1, 1, 2, 4, 4);
}

BOOL RefObj_IsGuildMercenary(const WORD& wTypeID)
{
    return RefTypeID_Is(wTypeID, 1, 1, 2, 3, 5);
}

// RefTypeID_Is_3_1_13_4 (0x51ae00 area): equipment 3/1/13/4.
static BOOL RefTypeID_Is_3_1_13_4(WORD wTypeID)
{
    return RefTypeID_Is(wTypeID, 0, 3, 1, 13, 4);
}

// ---------------------------------------------------------------------------------------------
// RefObjCommon

RefObjCommon::RefObjCommon()
    : ID(0), OrgObjID(0), TypeID(0), Flags(0), MaxContain(0), pRegion(NULL), Dir(0), OffsetX(0),
      OffsetY(0), OffsetZ(0), Speed1(0), Speed2(0), Scale(0), BCHeight(0), BCRadius(0), Link(0),
      pTeleport(NULL)
{
    for (int i = 0; i < 4; ++i)
    {
        ReqLevel[i].Type = 0;
        ReqLevel[i].Level = 0;
    }
}

RefObjCommon::~RefObjCommon()
{
}

int RefObjCommon::Read(CRefRow& row, int)
{
    if (!row.Service())
        return 0;
    row >> ID >> CodeName128 >> ObjName128 >> OrgObjCodeName128;
    if (!_stricmp(OrgObjCodeName128.c_str(), "xxx"))
        OrgObjCodeName128.clear();
    row.Skip();                                 // NameStrID128
    row >> DescStrID128;
    if (!_stricmp(DescStrID128.c_str(), "xxx"))
        DescStrID128.clear();

    BYTE btCash, btBionic, btTID1, btTID2, btTID3, btTID4;
    row >> btCash >> btBionic >> btTID1 >> btTID2 >> btTID3 >> btTID4;
    TypeID = RefTypeID_Pack(btCash, btBionic, btTID1, btTID2, btTID3, btTID4);

    DWORD dwDecayTime;
    row >> dwDecayTime;
    DecayTime = (float)((double)dwDecayTime / 1000.0);
    row >> Country >> Rarity;

    BYTE btCanTrade, btCanSell, btCanBuy, btCanBorrow, btCanDrop, btCanPick, btCanRepair;
    BYTE btCanRevive, btCanUse, btCanThrow;
    row >> btCanTrade >> btCanSell >> btCanBuy >> btCanBorrow >> btCanDrop >> btCanPick
        >> btCanRepair >> btCanRevive >> btCanUse >> btCanThrow;
    if (!btCanTrade && btCanDrop == 1)
        btCanDrop = 0;
    Flags = (btCanTrade != 0 ? REFOBJ_CAN_TRADE : 0) | (btCanSell != 0 ? REFOBJ_CAN_SELL : 0);
    if ((char)btCanBorrow < 0)
        Flags |= REFOBJ_CAN_BORROW_80;
    if (btCanBorrow & 0x40)
        Flags |= REFOBJ_CAN_BORROW_40;
    Flags |= (btCanPick != 0 ? REFOBJ_CAN_PICK : 0) | (btCanRepair != 0 ? REFOBJ_CAN_REPAIR : 0)
        | (btCanRevive != 0 ? REFOBJ_CAN_REVIVE : 0) | (btCanUse != 0 ? REFOBJ_CAN_USE : 0)
        | (btCanThrow != 0 ? REFOBJ_CAN_THROW : 0)
        | (16 * (btCanDrop & 1 | ((btCanDrop & 2) << 9)));

    row >> Price >> CostRepair >> CostRevive >> CostBorrow >> KeepingFee >> SellPrice;
    for (int i = 0; i < 4; ++i)
        row >> ReqLevel[i].Type >> ReqLevel[i].Level;
    if (btTID1 != 3 || btTID2 != 3 || btTID3 != 13 || btTID4 != 13)
    {
        for (int i = 0; i < 4; ++i)
        {
            for (int j = i + 1; j < 4; ++j)
            {
                if (ReqLevel[i].Type != -1 && ReqLevel[j].Type != -1
                    && ReqLevel[i].Type == ReqLevel[j].Type)
                {
                    LogPrintf(0x2000001,
                              "ReqLevelType[%d] == ReqLevelType[%d] are same value!, "
                              "RefObjCommonID[%d]", i, j, ID);
                    BSAssertFail();
                }
            }
        }
    }

    row >> MaxContain;
    WORD wRegionID;
    row >> wRegionID;
    pRegion = NULL;
    if (wRegionID)
    {
        CReferenceData* pData = RefObj_GetData();
        CReferenceData::RefRegionMap::iterator it = pData->m_mapRegion.find(wRegionID);
        if (it != pData->m_mapRegion.end())
            pRegion = it->second;
    }
    row >> Dir >> OffsetX >> OffsetY >> OffsetZ >> Speed1 >> Speed2 >> Scale >> BCHeight
        >> BCRadius >> EventID;
    for (int i = 0; i < 5; ++i)
        row >> AssocFile[i];

    GP = 0;
    std::string strTag("GP:");
    std::string strFile(AssocFile[3]);
    if (_stricmp(strFile.c_str(), "xxx"))
    {
        std::string::size_type nPos = strFile.find(strTag.c_str(), 0, strTag.size());
        if (nPos != std::string::npos)
            GP = atoi(strFile.substr(nPos + strTag.size()).c_str());
    }
    row >> Link;
    return ID;
}

void RefObjCommon::CopyFrom(const RefObjCommon& src)
{
    ID = src.ID;
    OrgObjID = src.OrgObjID;
    CodeName128 = src.CodeName128;
    ObjName128 = src.ObjName128;
    OrgObjCodeName128 = src.OrgObjCodeName128;
    DescStrID128 = src.DescStrID128;
    TypeID = src.TypeID;
    DecayTime = src.DecayTime;
    Country = src.Country;
    Rarity = src.Rarity;
    Flags = src.Flags;
    if (this != &src)
        TeleportFlags = src.TeleportFlags;
    Price = src.Price;
    CostBorrow = src.CostBorrow;
    CostRepair = src.CostRepair;
    CostRevive = src.CostRevive;
    KeepingFee = src.KeepingFee;
    SellPrice = src.SellPrice;
    for (int i = 0; i < 4; ++i)
        ReqLevel[i] = src.ReqLevel[i];
    MaxContain = src.MaxContain;
    pRegion = src.pRegion;
    Dir = src.Dir;
    OffsetX = src.OffsetX;
    OffsetY = src.OffsetY;
    OffsetZ = src.OffsetZ;
    Speed1 = src.Speed1;
    Speed2 = src.Speed2;
    Scale = src.Scale;
    BCHeight = src.BCHeight;
    BCRadius = src.BCRadius;
    EventID = src.EventID;
    for (int i = 0; i < 5; ++i)
        AssocFile[i] = src.AssocFile[i];
    GP = src.GP;
    Link = src.Link;
    SpecialFlag1 = src.SpecialFlag1;
    SpecialFlag2 = src.SpecialFlag2;
}

// ---------------------------------------------------------------------------------------------
// RefObjItem

RefObjItemLinkTables::~RefObjItemLinkTables()
{
    // RefObjItem_ClearDataStreams: the A / P maps only refer to the objects of mapAll.
    mapA.clear();
    mapP.clear();
    for (DataStreamMap::iterator it = mapAll.begin(); it != mapAll.end(); ++it)
    {
        if (it->second)
            delete it->second;
    }
    mapAll.clear();
}

RefObjItem::RefObjItem()
    : CoolTimeID(0), CoolTime(0), pLinkTables(NULL), MaxMagicOptCount(0), ChildItemCount(0)
{
    memset(&Dur, 0, (BYTE*)&Quivered - (BYTE*)&Dur);
    memset(&PAttackMin, 0, (BYTE*)&Params[0] - (BYTE*)&PAttackMin);
}

RefObjItem::~RefObjItem()
{
    if (pLinkTables)
    {
        delete pLinkTables;
        pLinkTables = NULL;
    }
}

// L and U; the increment column when bInc, 0 otherwise; the bounds / 1000 when bPermill.
static void RefObjItem_ReadRange(CRefRow& row, RefObjItemRange& r, BOOL bInc, BOOL bPermill)
{
    row >> r.L >> r.U;
    if (bPermill)
    {
        r.L = (float)(r.L / 1000.0);
        r.U = (float)(r.U / 1000.0);
    }
    if (bInc)
        row >> r.Inc;
    else
        r.Inc = 0.0f;
}

int RefObjItem::Read(CRefRow& row, int nReserved)
{
    int nID = RefObjCommon::Read(row, nReserved);
    if (Country == 2)
    {
        // The original box text is a long Korean note about country specific drop items.
        BSMessageBoxPrintf("[**] country 2 item");
        BSAssertFail();
    }
    if (!nID || !Link)
        BSAssertFail();
    CDBRecStream* pItemRow = RefData_FindRowInTable(RefObj_GetData(), 0xC, Link);
    if (!pItemRow)
    {
        LogPrintf(0x2000000, "Can't find associated item data [RefObjCommon ID: %d, RefItem ID: %d]",
                  nID, Link);
        // BUG(SM-041): the original goes on reading from the NULL row and crashes.
        BSAssertFail();
    }

    CRefRow item(pItemRow);
    int nItemID;
    item >> nItemID;
    if (nItemID != Link)
    {
        BSMessageBoxPrintf("ItemID don't differ with Link from RefObjItem");
        if (nItemID != Link && !BSAssertDialog(461, s_szReferenceDataFile, "dwItemID == m_dwLink"))
            DebugBreak();
    }
    item >> MaxStack >> ReqGender >> ReqStr >> ReqInt >> ItemClass >> SetID;
    if (!ItemClass)
    {
        BSMessageBoxPrintf("ItemClass must be not zero");
        if (!ItemClass && !BSAssertDialog(484, s_szReferenceDataFile, "m_btItemClass > 0"))
            DebugBreak();
    }
    RefObjItem_ReadRange(item, Dur, FALSE, FALSE);
    RefObjItem_ReadRange(item, PD, TRUE, FALSE);
    RefObjItem_ReadRange(item, ER, TRUE, FALSE);
    RefObjItem_ReadRange(item, PAR, TRUE, FALSE);
    RefObjItem_ReadRange(item, BR, FALSE, FALSE);
    RefObjItem_ReadRange(item, MD, TRUE, FALSE);
    RefObjItem_ReadRange(item, MAR, TRUE, FALSE);
    RefObjItem_ReadRange(item, PDStr, FALSE, TRUE);
    RefObjItem_ReadRange(item, MDInt, FALSE, TRUE);
    item >> Quivered;
    for (int i = 0; i < 5; ++i)
        item >> Ammo_TID4[i];
    item >> SpeedClass >> TwoHanded >> Range;
    RefObjItem_ReadRange(item, PAttackMin, FALSE, FALSE);
    RefObjItem_ReadRange(item, PAttackMax, TRUE, FALSE);
    RefObjItem_ReadRange(item, MAttackMin, FALSE, FALSE);
    RefObjItem_ReadRange(item, MAttackMax, TRUE, FALSE);
    RefObjItem_ReadRange(item, PAStrMin, FALSE, TRUE);
    RefObjItem_ReadRange(item, PAStrMax, FALSE, TRUE);
    RefObjItem_ReadRange(item, MAIntMin, FALSE, TRUE);
    RefObjItem_ReadRange(item, MAIntMax, FALSE, TRUE);
    RefObjItem_ReadRange(item, HR, TRUE, FALSE);
    RefObjItem_ReadRange(item, CHR, FALSE, FALSE);

    CoolTimeID = 0;
    CoolTime = 0;
    for (int i = 0; i < PARAM_COUNT; ++i)
    {
        item >> Params[i].Param >> Params[i].Desc;
        // Not ported yet: the per-parameter checks of the original (the "cooltime" parameter
        // into CoolTimeID / CoolTime, the item skill binding of the consumables).
    }
    item >> MaxMagicOptCount;

    int nMaxMagicOpt = 9;
    if (RefTypeID_Is(TypeID, 0, 3, 1, 13, 1))
        nMaxMagicOpt = 2;
    else if (RefTypeID_Is(TypeID, 0, 3, 1, 13, 2))
        nMaxMagicOpt = 4;
    else if (RefTypeID_Is(TypeID, 0, 3, 1, 13, 3))
        nMaxMagicOpt = 1;
    else if (RefTypeID_Is(TypeID, 0, 3, 1, 13, 4))
        nMaxMagicOpt = 2;
    if (MaxMagicOptCount > nMaxMagicOpt)
    {
        // The original text goes on with a Korean note.
        BSMessageBoxPrintf("Attachable MagicOptCount has wrong value! (MagOptCount(%d) < 0 || "
                           "MagOptCount(%d) > %d)", MaxMagicOptCount, MaxMagicOptCount,
                           nMaxMagicOpt);
        BSAssertFail();
        return 0;
    }
    item >> ChildItemCount;
    if (RefTypeID_Is(TypeID, 0, 3, 1, 13) && ChildItemCount > 1)
    {
        BSMessageBoxPrintf("Attachable AvatarItem Count has wrong value! (ItemCount(%d) < 0 || "
                           "ItemCount(%d) > %d)", ChildItemCount, ChildItemCount, 1);
        BSAssertFail();
        return 0;
    }
    item >> m_dw528;
    // Not ported yet: the link tables / sDataStream parameters of 3/3/13/14 and 3/3/13/15
    // (RefObjItem_ParseDataStreams 0x655fa0), the special item registration, the
    // FixedTermCosItem free parameters and the avatar item check.
    return nID;
}

BOOL RefObjItem_IsWithinMaxStack(const RefObjItem* pItem, WORD wCount)
{
    if (!pItem)
        BSAssertFail();
    return (int)wCount <= pItem->MaxStack;
}

BOOL RefObj_IsEquipItem(const RefObjCommon* pObj)
{
    if (!pObj)
    {
        BSAssertFail();
        return FALSE;
    }
    return RefTypeID_Is(pObj->TypeID, 0, 3, 1);
}

BOOL RefObj_IsEquipOfCountry(const RefObjCommon* pObj, BYTE btRarity)
{
    return RefTypeID_Is(pObj->TypeID, 0, 3, 1) && btRarity == pObj->Rarity;
}

BOOL RefObj_IsValidSlotForItem(const RefObjCommon* pObj, BYTE btSlot)
{
    if (RefObj_IsEquipItem(pObj))
        return btSlot < 12;
    return btSlot == 0;
}

void RefObjItem_GetDurabilityRange(const RefObjItem* pItem, int& nMin, int& nMax)
{
    if (!RefObj_IsEquipItem(pItem))
        return;
    if (RefObj_IsAccessoryType(pItem->TypeID))
    {
        nMin = 1;
        nMax = 1;
    }
    else if (RefTypeID_Is_3_1_13_4(pItem->TypeID))
    {
        nMin = 0;
        nMax = 0;
    }
    else
    {
        nMin = (int)(__int64)pItem->Dur.L;
        nMax = (int)(__int64)pItem->Dur.U;
    }
}

BOOL RefObjItem_IsValidDurability(const RefObjItem* pItem, DWORD dwValue)
{
    if (!pItem)
        BSAssertFail();
    if (!RefTypeID_Is(pItem->TypeID, 0, 3, 1))
        return FALSE;
    BOOL bEquip = RefObj_IsEquipItem(pItem);
    int nMin = 0;
    int nMax = 0;
    RefObjItem_GetDurabilityRange(pItem, nMin, nMax);
    if (!bEquip)
        return dwValue == (DWORD)nMin;
    if (RefObj_IsAccessoryType(pItem->TypeID) || RefTypeID_Is_3_1_13_4(pItem->TypeID))
        return dwValue == (DWORD)nMin;
    return (DWORD)nMin <= dwValue && (DWORD)nMax >= dwValue;
}

BOOL RefObjItem_IsValidDurabilityOrStack(const RefObjItem* pItem, DWORD dwValue)
{
    WORD w = pItem->TypeID;
    if (w & 2)
        return dwValue == 0;
    if (RefTypeID_Is(w, 0, 3, 3))
        return RefObjItem_IsWithinMaxStack(pItem, (WORD)dwValue);
    if (!RefTypeID_Is(w, 0, 3, 1))
        return dwValue == 0;
    return RefObjItem_IsValidDurability(pItem, dwValue);
}

// CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE | SORT_STRINGSORT) of the first cch
// characters.
static BOOL RefObj_CodeNameStartsWith(const std::string& strCodeName, const char* psz, int cch)
{
    return CompareStringA(0x400, 0x10001, strCodeName.c_str(), cch, psz, cch) == CSTR_EQUAL;
}

void RefObjItem_RegisterSpecialItem(const WORD& wTypeID, stdext::hash_set<int>& setID, int nID,
                                    const std::string& strCodeName)
{
    WORD w = wTypeID;
    if (RefTypeID_Is(w, 0, 3, 3, 8) || RefTypeID_Is(w, 0, 3, 2)
        || RefObjTID_IsItemEtcType13(wTypeID)
        || RefObj_CodeNameStartsWith(strCodeName, "ITEM_QNO", 8)
        || RefObj_CodeNameStartsWith(strCodeName, "ITEM_QTUTORIAL", 14)
        || RefObj_CodeNameStartsWith(strCodeName, "ITEM_QSP", 8)
        || RefObj_CodeNameStartsWith(strCodeName, "ITEM_QCX", 8)
        || RefObj_CodeNameStartsWith(strCodeName, "ITEM_ETC_E", 10)
        || RefObj_CodeNameStartsWith(strCodeName, "SN_ITEM_EVENT", 13)
        || RefObj_CodeNameStartsWith(strCodeName, "SN_ITEM_ETC_TAIWAN_50000_GOLD", 29))
    {
        setID.insert(nID);
    }
}

// ---------------------------------------------------------------------------------------------
// RefObjChar

RefObjChar::RefObjChar()
    : InventorySize(0), CanStore(0)
{
}

RefObjChar::~RefObjChar()
{
}

static BOOL RefObjChar_IsCos(WORD w, int nTID4)
{
    return RefTypeID_Is(w, 1, 1, 2, 3, nTID4);
}

int RefObjChar::Read(CRefRow& row, int nReserved)
{
    int nID = RefObjCommon::Read(row, nReserved);
    if (!nID || !Link)
    {
        BSMessageBoxPrintf("ObjCommon ID can't be found within RefObjChar");
        if ((!nID || !Link) && !BSAssertDialog(1147, s_szReferenceDataFile,
                                               "dwObjCommon != NULL && m_dwLink != NULL"))
            DebugBreak();
    }
    CDBRecStream* pCharRow = RefData_FindRowInTable(RefObj_GetData(), 0xD, Link);
    if (!pCharRow)
    {
        LogPrintf(0x2000000,
                  "Can't find associated character data [RefObjCommon ID: %d, RefChar ID: %d]",
                  nID, Link);
        BSAssertFail();
        return nID;
    }

    CRefRow ch(pCharRow);
    int nCharID;
    ch >> nCharID;
    if (nCharID != Link)
    {
        BSMessageBoxPrintf("CharID don't differ with Link from RefObjChar");
        if (nCharID != Link && !BSAssertDialog(1190, s_szReferenceDataFile, "dwCharID == m_dwLink"))
            DebugBreak();
    }
    ch >> Lvl >> CharGender >> MaxHP >> MaxMP;
    if (RefTypeID_Is(TypeID, 1, 1, 2, 1) && !MaxHP
        && !BSAssertDialog(1201, s_szReferenceDataFile, "m_dwMaxHP > 0"))
        DebugBreak();
    for (int i = 0; i < 32; ++i)
        ch >> Resist[i];
    ch >> InventorySize;
    BYTE btStore1, btStore2, btStore3, btStore4;
    ch >> btStore1 >> btStore2 >> btStore3 >> btStore4;
    CanStore = (WORD)((4 * (btStore1 & 7 | (btStore4 << 9))) | (CanStore & 3)
        | (32 * (btStore2 & 3 | (4 * (btStore3 & 0xF)))) & 0x7FF);
    BYTE btCanBeVehicle, btCanControl;
    ch >> btCanBeVehicle >> btCanControl;
    Flags |= (btCanBeVehicle != 0 ? REFOBJ_CAN_BE_VEHICLE : 0)
        | (btCanControl != 0 ? REFOBJ_CAN_CONTROL : 0);
    ch >> DamagePortion >> MaxPassenger >> AssocTactics >> PD >> MD >> PAR >> MAR >> ER >> BR
       >> HR >> CHR >> ExpToGive >> CreepType >> Knockdown >> KO_RecoverTime;
    if (Knockdown && !KO_RecoverTime)
        KO_RecoverTime = 3000;
    HasDefaultSkill = FALSE;
    for (int i = 0; i < 10; ++i)
    {
        ch >> DefaultSkill[i];
        if (DefaultSkill[i])
            HasDefaultSkill = TRUE;
    }
    ch.Skip();                                  // TextureType
    for (int i = 0; i < 10; ++i)
        ch >> Except[i].Value;
    ch >> m_n3FC;

    DecreaseHGPPerSec = 0.0f;
    if (RefObjChar_IsCos(TypeID, 3))
    {
        float f = (float)(100.0 / (double)(unsigned int)(60 * Except[3].Value));
        DecreaseHGPPerSec = f;
        if (f <= 0.000001)
        {
            LogPrintf(0x2000001,
                      "RefObjChar::ExtractData!!!! m_fDecreaseHGPPerSec <= _EPSILON. "
                      "CosCodeName[%s], DecreaseHGPPerSec[%f]", CodeName128.c_str(), (double)f);
        }
    }
    if (!g_pfnApplySpecialMonsterCode)
        BSAssertFail();
    else
        g_pfnApplySpecialMonsterCode(this);

    if (RefObjChar_IsCos(TypeID, 3))
    {
        if (InventorySize)
        {
            if (!BSAssertDialog(1315, s_szReferenceDataFile,
                                "!\"Goldpet'InventorySize isn't 0\""))
                DebugBreak();
            exit(1);
        }
    }
    else if (RefObjChar_IsCos(TypeID, 4) && InventorySize != 140)
    {
        if (!BSAssertDialog(1333, s_szReferenceDataFile, "!\"Silkpet'InventorySize isn't max(140)\""))
            DebugBreak();
        exit(1);
    }
    return nID;
}

// ---------------------------------------------------------------------------------------------
// RefObjStruct

void RefObj_AddUniqueLinkedID(RefRegionRow* pRegion, RefObjCommon* pObj)
{
    std::vector<RefObjCommon*>& vec = pRegion->vecStruct;
    if (std::find(vec.begin(), vec.end(), pObj) != vec.end())
        BSAssertFail();
    // BUG(SM-042): the duplicate is only asserted and added again.
    vec.push_back(pObj);
}

int RefObjStruct::Read(CRefRow& row, int nReserved)
{
    int nID = RefObjCommon::Read(row, nReserved);
    if (!nID || !Link)
        BSAssertFail();
    CDBRecStream* pStructRow = RefData_FindRowInTable(RefObj_GetData(), 0xE, Link);
    if (!pStructRow)
    {
        BSAssertFail();
        return nID;
    }
    CRefRow st(pStructRow);
    int nStructID;
    DWORD dwUnused;
    st >> nStructID;
    if (nStructID != Link)
        BSAssertFail();
    st >> dwUnused;
    if (pRegion)
        RefObj_AddUniqueLinkedID(pRegion, this);
    return nID;
}

// ---------------------------------------------------------------------------------------------
// Slot 0xB

CDBRecStream* RefData_FindRowInTable(CReferenceData* pData, unsigned int nSlot, int nID)
{
    RefTableEntry* pTable = pData->m_pTables->at(nSlot);
    if (!pTable)
        BSAssertFail();
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        CDBRecStream* pRow = static_cast<CDBRecStream*>(*it);
        switch (nSlot)
        {
        case 3:
        case 0x11:
            if (*(const int*)((const BYTE*)pRow + 0x30) == nID)
                return pRow;
            break;
        case 0xC:
        case 0xD:
        case 0xE:
        {
            // The first column, read without moving the row's position.
            int nReadPos = pRow->m_nReadPos;
            int nFieldCount = pRow->m_nFieldCount;
            pRow->m_nFieldCount = 0;
            pRow->m_nReadPos = 0;
            if (pRow->m_nWritePos < 4)
                BSAssertFail();
            int nKey = *(const int*)(pRow->m_pBuffer + pRow->m_nReadPos);
            pRow->m_nReadPos = nReadPos;
            pRow->m_nFieldCount = nFieldCount;
            if (nKey == nID)
                return pRow;
            break;
        }
        default:
            BSAssertFail();
            break;
        }
    }
    return NULL;
}

int RefObjCommon_ReadAndCreate(CReferenceData* pData, CDBRecord* pRecord)
{
    CDBRecStream* pStream = static_cast<CDBRecStream*>(pRecord);
    CRefRow row(pStream);
    if (!row.Service())
        return 1;
    int nID;
    row >> nID;
    if (!nID)
        return 1;
    row.Skip().Skip().Skip().Skip().Skip();
    BYTE btCash, btBionic, btTID1, btTID2, btTID3, btTID4;
    row >> btCash >> btBionic >> btTID1 >> btTID2 >> btTID3 >> btTID4;
    WORD w = RefTypeID_Pack(btCash, btBionic, btTID1, btTID2, btTID3, btTID4);
    BOOL bBionic = (w & 2) != 0;

    RefObjCommon* pObj = NULL;
    if ((bBionic && (w & 0x1C) == 4) || RefTypeID_Is(w, 1, 1, 2, 5) || RefTypeID_Is(w, 1, 1, 2, 4))
        pObj = new RefObjChar;
    else if (RefObj_TypeIdIsClass0C(w))
        pObj = new RefObjItem;
    else if (bBionic || (!RefTypeID_Is(w, 0, 4, 1) && !RefTypeID_Is(w, 0, 4, 2)))
        BSAssertFail();
    else
        pObj = new RefObjStruct;

    pStream->m_nFieldCount = 0;
    pStream->m_nReadPos = 0;
    if (!pObj)
        return 1;
    int nObjID = pObj->Read(row, 0);
    if ((unsigned int)nObjID == 0)
    {
        delete pObj;
        return 0;
    }
    // BUG(SM-040): an ID that is already in the map is not inserted and the object leaks.
    pData->m_hashRefObj.insert(std::make_pair(nObjID, pObj));
    return 1;
}

int RefData_BuildRefObjCommon(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    if (pTable->m_listRows.size() == 0)
        return 0;
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        if (!RefObjCommon_ReadAndCreate(pData, *it))
        {
            BSAssertFail();
            return 0;
        }
    }
    RefData_PostProcessObjCommon(pData);
    return 1;
}

static void RefData_FillTemplateObj(const RefObjCommon* pTemplate, RefObjCommon* pObj, int nID,
                                    const char* pszCodeName, WORD wTypeID)
{
    pObj->CopyFrom(*pTemplate);
    pObj->ID = nID;
    pObj->OrgObjID = 0;
    pObj->CodeName128.assign(pszCodeName, strlen(pszCodeName));
    pObj->ObjName128.clear();
    pObj->OrgObjCodeName128.clear();
    pObj->Flags = 0;
    pObj->MaxContain = 0;
    pObj->Link = 0;
    pObj->TypeID = wTypeID;
    pObj->Country = 3;
}

void RefData_CreateDummyObj(const RefObjCommon* pTemplate, RefObjCommon* pObj)
{
    RefData_FillTemplateObj(pTemplate, pObj, -1, "DUMMY_OBJECT", 84);
}

void RefData_CreateEventDaemonObj(const RefObjCommon* pTemplate, RefObjCommon* pObj)
{
    RefData_FillTemplateObj(pTemplate, pObj, -2, "EVENT_DAEMON", 116);
}

BOOL RefData_RegisterByLevel(CReferenceData::RefObjByLevelHashMap& hash,
                             const std::string& strCodeName, RefObjCommon* pObj)
{
    CReferenceData::RefObjVector* pVec;
    CReferenceData::RefObjByLevelHashMap::iterator it = hash.find(strCodeName);
    if (it == hash.end())
    {
        pVec = new CReferenceData::RefObjVector;
        pVec->resize(140, NULL);
        hash.insert(std::make_pair(strCodeName, pVec));
    }
    else
    {
        pVec = it->second;
    }
    unsigned int nSlot = (unsigned int)(static_cast<RefObjChar*>(pObj)->Lvl - 1);
    if (pVec->at(nSlot))
    {
        BSAssertFail();
        return FALSE;
    }
    pVec->at(nSlot) = pObj;
    return TRUE;
}

BOOL RefData_RegisterSiegeGuarderRef(CReferenceData::RefObjByLevelHashMap& hash,
                                     const std::string& strCodeName, RefObjCommon* pObj)
{
    WORD w = pObj->TypeID;
    if (!(w & 2) || (w & 0x1C) != 4
        || (!RefObj_IsSiegeGuard_TID4_1(w) && !RefObj_IsSiegeGuard_TID4_4(w)
            && !RefObj_IsSiegeGuard_TID4_3(w)))
        BSAssertFail();
    return RefData_RegisterByLevel(hash, strCodeName, pObj);
}

BOOL RefData_RegisterMercenaryRef(const std::string& strCodeName,
                                  CReferenceData::RefObjByLevelHashMap& hash, RefObjCommon* pObj)
{
    WORD w = pObj->TypeID;
    if (!(w & 2) || (w & 0x1C) != 4 || !RefObj_IsGuildMercenary(w))
        BSAssertFail();
    return RefData_RegisterByLevel(hash, strCodeName, pObj);
}

void RefData_PostProcessObjCommon(CReferenceData* pData)
{
    CReferenceData::RefObjHashMap& hashObj = pData->m_hashRefObj;
    CReferenceData::RefObjHashMap::iterator it;

    // IndexObjByCodeName (0x4aa640).
    for (it = hashObj.begin(); it != hashObj.end(); ++it)
    {
        if (pData->m_hashRefObjByCodeName.find(it->second->CodeName128)
            != pData->m_hashRefObjByCodeName.end())
            BSAssertFail();
        pData->m_hashRefObjByCodeName.insert(std::make_pair(it->second->CodeName128, it->second));
    }
    // IndexObjByLinkName (0x4aa780): the ID of the original object of the same TID1 / TID2.
    for (it = hashObj.begin(); it != hashObj.end(); ++it)
    {
        RefObjCommon* pObj = it->second;
        if (pObj->OrgObjCodeName128.empty())
            continue;
        CReferenceData::RefObjByCodeNameHashMap::iterator itOrg =
            pData->m_hashRefObjByCodeName.find(pObj->OrgObjCodeName128);
        RefObjCommon* pOrg = itOrg == pData->m_hashRefObjByCodeName.end() ? NULL : itOrg->second;
        WORD w = pObj->TypeID;
        if (((w & 0x1C) == 4 && (w & 0x60) == 0x40 && (w & 0x780) == 0x280)
            || ((w & 0x1C) == 4 && (w & 0x60) == 0x40 && (w & 0x780) == 0x100
                && (w & 0xF800) == 0x800))
            continue;
        if (!pOrg || ((BYTE)w ^ (BYTE)pOrg->TypeID) & 0x1C || ((BYTE)w ^ (BYTE)pOrg->TypeID) & 0x60)
            pObj->OrgObjID = 0;
        else
            pObj->OrgObjID = pOrg->ID;
    }
    // BuildHunterMonsterList / BuildThiefMonsterList (RefDataMisc.cpp).
    RefData_BuildHunterMonsterList(pData);
    RefData_BuildThiefMonsterList(pData);
    // BuildMercenaryList (0x4aaaa0).
    for (it = hashObj.begin(); it != hashObj.end(); ++it)
    {
        RefObjCommon* pObj = it->second;
        if (RefTypeID_Is(pObj->TypeID, 1, 1, 2, 3, 5)
            && !RefData_RegisterMercenaryRef(pObj->OrgObjCodeName128, pData->m_hashMercenary,
                                             pObj))
        {
            LogPrintf(0x2000000, "Initialize Build Mercenary Failed!! RefObjCodeName[%s]",
                      pObj->CodeName128.c_str());
        }
    }
    // BuildMonsterTypeTable (0x4aacb0): the monsters except hunters and thieves, by level.
    pData->m_vecMonsterByLevel.resize(140);
    for (it = hashObj.begin(); it != hashObj.end(); ++it)
    {
        RefObjCommon* pObj = it->second;
        WORD w = pObj->TypeID;
        if (!RefTypeID_Is(w, 1, 1, 2, 1) || RefTypeID_Is(w, 1, 1, 2, 1, 2)
            || RefTypeID_Is(w, 1, 1, 2, 1, 3))
            continue;
        unsigned int nLevel = (unsigned int)(static_cast<RefObjChar*>(pObj)->Lvl - 1);
        if (nLevel >= 0x8C)
            BSAssertFail();
        pData->m_vecMonsterByLevel.at(nLevel).push_back(pObj);
    }
    // BuildTrapMonsterList (RefDataMisc.cpp) and BuildEventItemList (0x4ab0a0, RefDataQuest.cpp).
    RefData_BuildTrapMonsterList(pData, std::string("TQ"));
    RefData_BuildEventItemList(pData);
    // BuildSiegeGuarderList (0x4aab70): the first failure stops the list.
    for (it = hashObj.begin(); it != hashObj.end(); ++it)
    {
        RefObjCommon* pObj = it->second;
        WORD w = pObj->TypeID;
        if (RefObj_IsSiegeGuard_TID4_1(w) || RefObj_IsSiegeGuard_TID4_4(w)
            || RefObj_IsSiegeGuard_TID4_3(w))
        {
            if (!RefData_RegisterSiegeGuarderRef(pData->m_hashSiegeGuarder,
                                                 pObj->OrgObjCodeName128, pObj))
            {
                LogPrintf(0x2000000, "Initialize Build SiegeGuarder Failed!! RefObjCodeName[%s]",
                          pObj->CodeName128.c_str());
                break;
            }
        }
    }
    // CountSpecialItems (0x4adce0).
    for (it = hashObj.begin(); it != hashObj.end(); ++it)
    {
        if (RefTypeID_Is(it->second->TypeID, 0, 3, 3, 10, 4))
            pData->m_vecSpecialItem.push_back(it->second);
    }

    RefObjCommon* pTemplate = pData->FindRefObjByCodeName("STORE_CH_GATE");
    if (!pTemplate)
        BSAssertFail();
    RefObjCommon* pDummy = new RefObjDummy;
    RefData_CreateDummyObj(pTemplate, pDummy);
    if (pData->FindRefObj(-1))
        BSAssertFail();
    if (pData->m_hashRefObjByCodeName.find(pDummy->CodeName128)
        != pData->m_hashRefObjByCodeName.end())
        BSAssertFail();
    hashObj.insert(std::make_pair(-1, pDummy));
    pData->m_hashRefObjByCodeName.insert(std::make_pair(pDummy->CodeName128, pDummy));

    RefObjCommon* pDaemon = new RefObjEventDaemon;
    RefData_CreateEventDaemonObj(pTemplate, pDaemon);
    if (pData->FindRefObj(-2))
        BSAssertFail();
    if (pData->m_hashRefObjByCodeName.find(pDaemon->CodeName128)
        != pData->m_hashRefObjByCodeName.end())
        BSAssertFail();
    hashObj.insert(std::make_pair(-2, pDaemon));
    pData->m_hashRefObjByCodeName.insert(std::make_pair(pDaemon->CodeName128, pDaemon));
}

// ---------------------------------------------------------------------------------------------
// CRefCharGen

void CRefCharGen_AddItemToRaceList(CRefCharGenGender& gender, int nRefObjID)
{
    RefObjCommon* pObj = RefObj_GetData()->FindRefObj(nRefObjID);
    if (!pObj)
        BSAssertFail();
    WORD w = pObj->TypeID;
    std::vector<DWORD>* pVec = NULL;
    if (w & 2)
    {
        if ((w & 0x1C) == 4)
            pVec = &gender.vecCharObjID;
    }
    else if ((w & 0x1C) == 0xC && (w & 0x60) == 0x20)
    {
        pVec = &gender.vecItemObjID;
    }
    if (!pVec || std::find(pVec->begin(), pVec->end(), (DWORD)nRefObjID) != pVec->end())
    {
        BSAssertFail();
        return;
    }
    pVec->push_back(nRefObjID);
}

void CRefCharGen_AddItemByRace(BYTE btGender, CRefCharGenRace& race, int nRefObjID)
{
    if (btGender < 2)
    {
        CRefCharGen_AddItemToRaceList(race.vecGender.at(btGender), nRefObjID);
    }
    else if (btGender == 2)
    {
        for (unsigned int i = 0; i < race.vecGender.size(); ++i)
            CRefCharGen_AddItemToRaceList(race.vecGender.at(i), nRefObjID);
    }
    else
    {
        BSAssertFail();
    }
}

void CRefCharGen_AddEntry(CRefCharGen& gen, int nRefObjID)
{
    RefObjCommon* pObj = RefObj_GetData()->FindRefObj(nRefObjID);
    if (!pObj)
    {
        LogPrintf(0x2000001,
                  "[CRefCharGen::AddEntry] There is no Refobj like this or Service is 0 (ObjID : %d)",
                  nRefObjID);
        return;
    }
    WORD w = pObj->TypeID;
    BYTE btCountry = pObj->Country;
    BYTE btGender = 0xFF;
    if (w & 2)
    {
        if ((w & 0x1C) == 4)
            btGender = static_cast<RefObjChar*>(pObj)->CharGender;
    }
    else if ((w & 0x1C) == 0xC && (w & 0x60) == 0x20)
    {
        btGender = static_cast<RefObjItem*>(pObj)->ReqGender;
    }
    if (btGender == 0xFF)
        BSAssertFail();
    if (btCountry < 3)
    {
        CRefCharGen_AddItemByRace(btGender, gen.m_vecRace.at(btCountry), nRefObjID);
    }
    else if (btCountry == 3)
    {
        for (unsigned int i = 0; i < 3; ++i)
            CRefCharGen_AddItemByRace(btGender, gen.m_vecRace.at(i), nRefObjID);
    }
    gen.m_setObjID.insert(nRefObjID);
}

int CRefCharGen_ReadRow(CRefCharGen& gen, CRefRow& row)
{
    DWORD dwService;
    int nRefObjID;
    row >> dwService >> nRefObjID;
    if (dwService)
        CRefCharGen_AddEntry(gen, nRefObjID);
    return 1;
}

BOOL CRefCharGen_IsAllowedItem(CRefCharGen& gen, int nItemID, BYTE btRace, BYTE btGender)
{
    if (gen.m_setObjID.find(nItemID) == gen.m_setObjID.end())
        return FALSE;
    RefObjCommon* pObj = RefObj_GetData()->FindRefObj(nItemID);
    if (!pObj || !RefTypeID_Is(pObj->TypeID, 0, 3, 1))
    {
        BSAssertFail();
        return FALSE;
    }
    if (pObj->Country != btRace && pObj->Country != 3)
        return FALSE;
    BYTE btReqGender = static_cast<RefObjItem*>(pObj)->ReqGender;
    return btReqGender == btGender || btReqGender == 2;
}

BOOL CRefCharGen_ValidateNewCharacter(CRefCharGen& gen, int nRefObjID, const int* anItemID)
{
    if (!nRefObjID)
        return FALSE;
    RefObjCommon* pObj = RefObj_GetData()->FindRefObj(nRefObjID);
    if (!pObj || !RefTypeID_Is(pObj->TypeID, 1, 1, 1))
        return FALSE;
    BYTE btRace = pObj->Country;
    BYTE btGender = static_cast<RefObjChar*>(pObj)->CharGender;
    for (int i = 0; i < 4; ++i)
    {
        if (anItemID[i] && CRefCharGen_IsAllowedItem(gen, anItemID[i], btRace, btGender) != 1)
            return FALSE;
    }
    return TRUE;
}

int RefData_BuildRefCharGenData(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    if (pTable->m_listRows.size() == 0)
        return 0;
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        CRefRow row(static_cast<CDBRecStream*>(*it));
        if (!CRefCharGen_ReadRow(pData->m_RefCharGen, row))
        {
            BSAssertFail();
            return 0;
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------
// Skill tables

int RefRec_Read(const CRefSkillMastery& rec, RefSkillMasteryData& r)
{
    r = rec;
    return r.ID;
}

int RefRec_Read(const CRefSkill& rec, RefSkill& r)
{
    if (!rec.m_Data.Service)
        return 0;
    r = rec.m_Data;
    return r.ID;
}

template <class TRec, class T, class TMap, TMap CReferenceData::*pMap>
static int RefData_BuildUnique(CReferenceData* pData, RefTableEntry* pTable)
{
    if (pTable->m_listRows.size() == 0)
        return 0;
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        T* pRow = new T;
        int nKey = RefRec_Read(*static_cast<TRec*>(*it), *pRow);
        if (!nKey)
        {
            delete pRow;
            continue;
        }
        typename TMap::iterator itFound = (pData->*pMap).find(nKey);
        if (itFound != (pData->*pMap).end() && itFound->second)
        {
            delete pRow;
            BSAssertFail();
            BSAssertFail();
            return 0;
        }
        (pData->*pMap).insert(typename TMap::value_type(nKey, pRow));
    }
    return 1;
}

int RefData_BuildRefSkillMastery(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    return RefData_BuildUnique<CRefSkillMastery, RefSkillMasteryData,
        CReferenceData::RefSkillMasteryMap, &CReferenceData::m_mapSkillMastery>(pData, pTable);
}

int RefData_BuildRefSkill(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    RefData_BuildUnique<CRefSkill, RefSkill, CReferenceData::RefSkillMap,
        &CReferenceData::m_mapSkill>(pData, pTable);
    RefData_BuildRefSkillByCodename(pData);
    return 1;
}

void RefData_BuildRefSkillByCodename(CReferenceData* pData)
{
    for (CReferenceData::RefSkillMap::iterator it = pData->m_mapSkill.begin();
         it != pData->m_mapSkill.end(); ++it)
    {
        RefSkill* pSkill = it->second;
        std::string strCode(pSkill->Basic_Code.c_str());
        BOOL bExists = pData->m_mapSkillByCodeName.find(strCode)
            != pData->m_mapSkillByCodeName.end();
        if (bExists
            && !BSAssertDialog(2383,
                   "d:\\work2005\\source\\silkroadonline\\server\\sr_shardmanager\\../"
                   "ServerCommon/RefInstanceGenerator.h",
                   "m_RefSkillByCodename.end() == "
                   "m_RefSkillByCodename.find(pRefSkill->m_Basic_Code.c_str())"))
            DebugBreak();
        pData->m_mapSkillByCodeName.insert(std::make_pair(pSkill->Basic_Code, pSkill));
    }
}
