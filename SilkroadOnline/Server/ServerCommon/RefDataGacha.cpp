#include "ServerCommon/RefDataMisc.h"

#include "BSLib/BSDebug.h"
#include "BSNet/NetEngineLog.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerCommon/RefDataObj.h"

// The gacha item sets (slot 0x47). Kept apart from RefDataMisc.cpp because the reader needs the
// item checks of the object rows (SM_rf_obj).

static const char s_szReferenceDataFile[] =
    "D:\\WORK2005\\Source\\SilkroadOnline\\Server\\ServerCommon\\ReferenceData.cpp";

static DWORD RefObj_GetMaxStack(const RefObjCommonRow* pObj)                     // +0x198
{
    return *(const DWORD*)((const BYTE*)pObj + 0x198);
}

// The durability bounds of an item (RefObjItem Dur.L / Dur.U, +0x1B0 / +0x1B4) as the original
// converts them (_ftol2).
static __int64 RefObj_GetDurMin(const RefObjCommonRow* pObj)
{
    return (__int64)*(const float*)((const BYTE*)pObj + 0x1B0);
}

static __int64 RefObj_GetDurMax(const RefObjCommonRow* pObj)
{
    return (__int64)*(const float*)((const BYTE*)pObj + 0x1B4);
}

// The checks of one free parameter against the item.
static void RefGachaItemSet_CheckEntry(CGachaFreeParamEntry& entry, const RefObjCommonRow* pObj)
{
    if (entry.bValid != 1)
        return;
    switch (entry.btType)
    {
    case CGachaFreeParamEntry::TYPE_DURABILITY:
        if (!RefObjItem_IsValidDurabilityOrStack(static_cast<const RefObjItem*>(pObj),
                                                (DWORD)entry.nValue))
        {
            BSAssertFail();
            __int64 nMin = RefObj_GetDurMin(pObj);
            if (nMin > RefObj_GetDurMax(pObj))
            {
                NetEngineLog(0x2000001,
                             "CLAMP() ==> min(%.3f) exceeded max(%.3f) value), File: %s, Line: %d",
                             (double)RefObj_GetDurMin(pObj), (double)RefObj_GetDurMax(pObj),
                             s_szReferenceDataFile, 4329);
                entry.nValue = RefObj_GetDurMin(pObj);
            }
            if (entry.nValue < RefObj_GetDurMin(pObj))
                entry.nValue = RefObj_GetDurMin(pObj);
            else if (entry.nValue >= RefObj_GetDurMax(pObj))
                entry.nValue = RefObj_GetDurMax(pObj);
        }
        break;
    case CGachaFreeParamEntry::TYPE_MAGIC_OPTION:
        {
            if (!RefObj_IsEquipItem(pObj))
                BSAssertFail();
            CReferenceData* pData = CReferenceData::GetInstance();
            RefMagicOptGroupByName* pGroup = pData->m_MagicOpt.FindMagicOptById(entry.dwCode);
            if (!pGroup)
            {
                BSAssertFail();
                break;
            }
            RefMagicOptLevel* pLevel = MagOptEachLevel_FindByID(pGroup, entry.btLevel);
            if (!pLevel)
            {
                BSAssertFail();
                break;
            }
            DWORD dwValue = (DWORD)entry.nValue;
            if (!RefMagicOpt_CountApplicableParams(*pLevel, pObj))
            {
                BSAssertFail();
                break;
            }
            RefMagicOptLevel* pRange = MagOptEachLevel_FindByID(pGroup, pLevel->MLevel);
            if (!pRange || pRange->pDesc->GetMin() > dwValue || pRange->pDesc->GetMax() < dwValue)
                BSAssertFail();
        }
        break;
    case CGachaFreeParamEntry::TYPE_OPT_LEVEL:
        if (!RefObj_IsValidSlotForItem(pObj, (BYTE)entry.nValue))
        {
            BSAssertFail();
            if (entry.nValue < 0)
                entry.nValue = 0;
            else if (entry.nValue >= 12)
                entry.nValue = 12;
        }
        break;
    }
}

int RefRow_Read(CRefRow& row, RefGachaItemSetRow& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    row >> r.dw04 >> r.RefItemID >> r.w0C >> r.Count >> r.dw10 >> r.b14;
    for (int i = 0; i < 4; ++i)
        row >> r.Param[i].Param >> r.Param[i].Desc;

    RefObjCommonRow* pObj = CReferenceData::GetInstance()->FindRefObj(r.RefItemID);
    if (!pObj)
    {
        BSAssertFail();
        // BUG(SM-072): the original goes on with the NULL object and crashes.
        return 0;
    }
    if (!r.Count)
        BSAssertFail();
    if (r.Count > RefObj_GetMaxStack(pObj))
        BSAssertFail();
    WORD wTypeID = RefObj_GetTypeID(pObj);
    if ((wTypeID & 2) == 0 && (wTypeID & 0x1C) == 0xC && (wTypeID & 0x60) == 0x20 &&
        r.Count != 1)
    {
        BSAssertFail();
    }
    if (r.pFreeParam)
        BSAssertFail();
    r.pFreeParam = new CGachaFreeParam;
    for (int i = 0; i < 4; ++i)
    {
        if (!r.Param[i].Param)
            continue;
        // The entries of the earlier Desc columns are checked again.
        r.pFreeParam->Parse(r.Param[i].Desc);
        for (unsigned int j = 0; j < r.pFreeParam->m_vecEntry.size(); ++j)
            RefGachaItemSet_CheckEntry(*r.pFreeParam->m_vecEntry.at(j), pObj);
    }
    return r.Service;
}

int RefData_LoadGachaItemSet(CReferenceData* pData, CDBRecord* pRecord)
{
    CRefRow row(static_cast<CDBRecStream*>(pRecord));
    RefGachaItemSetRow* pRow = new RefGachaItemSetRow;
    if (RefRow_Read(row, *pRow))
        pData->m_vecGachaItemSet.push_back(pRow);
    else
        delete pRow;
    return 1;
}

int RefData_BuildGachaItemSet(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    if (pTable->m_listRows.size() == 0)
        return 0;
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        if (!RefData_LoadGachaItemSet(pData, *it))
        {
            BSAssertFail();
            return 0;
        }
    }
    // Not ported yet: CRefInstanceGenerator::BuildWinLoseRewardTable (0x4ab710), which the
    // original calls here when the rows are loaded.
    return 1;
}
