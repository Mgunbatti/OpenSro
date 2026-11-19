#include "ServerCommon/RefDataMisc.h"

#include "BSLib/BSDebug.h"
#include "BSNet/NetEngineLog.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerCommon/RefDataObj.h"

// The gacha item sets (slot 0x47), read against the item rows of RefDataObj.h.

static const char s_szReferenceDataFile[] =
    "D:\\WORK2005\\Source\\SilkroadOnline\\Server\\ServerCommon\\ReferenceData.cpp";

// The checks of one free parameter against the item (the durability bounds are converted as
// the original does, _ftol2).
static void RefGachaItemSet_CheckEntry(CGachaFreeParamEntry& entry, const RefObjItem* pItem)
{
    if (entry.bValid != 1)
        return;
    switch (entry.btType)
    {
    case CGachaFreeParamEntry::TYPE_DURABILITY:
        if (!RefObjItem_IsValidDurabilityOrStack(pItem, (DWORD)entry.nValue))
        {
            BSAssertFail();
            __int64 nMin = (__int64)pItem->Dur.L;
            if (nMin > (__int64)pItem->Dur.U)
            {
                NetEngineLog(0x2000001,
                             "CLAMP() ==> min(%.3f) exceeded max(%.3f) value), File: %s, Line: %d",
                             (double)(__int64)pItem->Dur.L, (double)(__int64)pItem->Dur.U,
                             s_szReferenceDataFile, 4329);
                entry.nValue = (__int64)pItem->Dur.L;
            }
            if (entry.nValue < (__int64)pItem->Dur.L)
                entry.nValue = (__int64)pItem->Dur.L;
            else if (entry.nValue >= (__int64)pItem->Dur.U)
                entry.nValue = (__int64)pItem->Dur.U;
        }
        break;
    case CGachaFreeParamEntry::TYPE_MAGIC_OPTION:
        {
            if (!RefObj_IsEquipItem(pItem))
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
            if (!RefMagicOpt_CountApplicableParams(*pLevel, pItem))
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
        if (!RefObj_IsValidSlotForItem(pItem, (BYTE)entry.nValue))
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
    const RefObjItem* pItem = static_cast<const RefObjItem*>(pObj);
    if (r.Count > (DWORD)pItem->MaxStack)
        BSAssertFail();
    WORD wTypeID = pObj->TypeID;
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
            RefGachaItemSet_CheckEntry(*r.pFreeParam->m_vecEntry.at(j), pItem);
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
