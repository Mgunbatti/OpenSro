// Stand-ins for the item and magic option checks the scrap item rows of the shop tables use
// (RefDataShop.h). They belong to the object tables (SM_rf_obj) and the magic option tables
// (SM_rf_magicopt); this file goes away when those are ported. Until then every item instance
// is accepted.

#include "ServerCommon/RefDataShop.h"

#include "ServerCommon/ReferenceData.h"

BOOL RefObjItem_IsValidDurabilityOrStack(const RefObjCommonRow*, DWORD)
{
    return TRUE;
}

RefMagicOptLevel* RefData_FindMagicOptLevelEntry(CReferenceData* pData, WORD wMagicOptID)
{
    std::map<WORD, RefMagicOptLevel*>::iterator it =
        pData->m_MagicOpt.m_mapMagicOptLevel.find(wMagicOptID);
    if (it == pData->m_MagicOpt.m_mapMagicOptLevel.end())
        return NULL;
    return it->second;
}

BOOL RefMagicOptLevel_GetValueRange(const RefMagicOptLevel*, DWORD& dwMin, DWORD& dwMax)
{
    dwMin = 0;
    dwMax = 0xFFFFFFFF;
    return TRUE;
}
