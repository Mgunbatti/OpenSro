#include "ServerCommon/RefDataMisc.h"

#include <stdlib.h>
#include <string.h>

#include "BSLib/BSDebug.h"
#include "BSLib/Util/StringCheck.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerCommon/RefDataObj.h"
#include "ServerFramework/ServerDataExtern.h"

// The source file the original's assert dialogs name.
static const char s_szReferenceDataFile[] =
    "D:\\WORK2005\\Source\\SilkroadOnline\\Server\\ServerCommon\\ReferenceData.cpp";

// The names the readers copy into a 256-character buffer (StringCchCopy): a longer one fails.
static BOOL RefData_FitsNameBuffer(const std::string& str)
{
    return strlen(str.c_str()) < 256;
}

// ---------------------------------------------------------------------------------------------
// Row readers

int RefRow_Read(CRefRow& row, RefEventRow& r)
{
    BYTE btService;
    row >> btService;
    if (!btService)
        return 0;
    row >> r.ID;
    if (!r.ID && !BSAssertDialog(2209, s_szReferenceDataFile, "dwID != 0"))
        DebugBreak();
    row >> r.Name >> r.str20;
    if (!RefData_FitsNameBuffer(r.Name))
    {
        BSAssertFail();
        return 0;
    }
    char szName[256];
    memcpy(szName, r.Name.c_str(), strlen(r.Name.c_str()) + 1);
    String_ToLowerMbcs(szName, &r.Name);
    return r.ID;
}

int RefRow_Read(CRefRow& row, RefDefaultQuestRow& r)
{
    BYTE btService;
    row >> btService;
    if (!btService)
        return 0;
    row >> r.ID >> r.b04 >> r.Name >> r.b05;
    if (!RefData_FitsNameBuffer(r.Name))
    {
        BSAssertFail();
        return 0;
    }
    return r.ID;
}

DWORD RefRow_Read(CRefRow& row, RefShopSlot36Row& r)
{
    row >> r.dw00 >> r.str04;
    return r.dw00;
}

int RefRow_Read(CRefRow& row, RefSetItemGroup& r)
{
    BYTE btService;
    row >> btService >> r.ID;
    std::string str1, str2, str3, str4;
    row >> str1 >> str2 >> str3 >> str4;
    BYTE btSkipped;
    row >> btSkipped >> r.SetMagicMask;
    if (!btService)
        return 0;

    DWORD dwBit = 1;
    for (int i = 0; i < 10; ++i, dwBit *= 2)
    {
        DWORD dwGroupID;
        row >> dwGroupID;
        if ((dwBit & r.SetMagicMask) && !dwGroupID)
        {
            BSMessageBoxPrintf("Error data found : SetMagicMask[%d] is turn on. but, "
                               "MagicGroupID is NULL", dwBit & r.SetMagicMask);
            BSAssertFail();
            return 0;
        }
        if (dwGroupID)
        {
            CReferenceData::RefMagicOptGroupVector vecGroup;
            if (!CReferenceData::GetInstance()->GetMagicOptGroup(dwGroupID, vecGroup))
            {
                BSMessageBoxPrintf("Error data found : RefMagicOptGroup data is not exist. "
                                   "MagicOptGroupID[%d] is invalid value", dwGroupID);
                BSAssertFail();
            }
            r.mapMagicOptGroup.insert(std::make_pair(dwBit, dwGroupID));
        }
    }
    return r.ID;
}

void RefData_ReadIdBytePairs5(CRefRow& row, RefIdBytePairs& r)
{
    BYTE btService;
    row >> btService;
    for (int i = 0; i < 5; ++i)
    {
        DWORD dwID;
        BYTE btValue;
        row >> dwID >> btValue;
        if (dwID)
            r.vecPair.push_back(std::make_pair(dwID, btValue));
    }
}

int RefRow_Read(CRefRow& row, RefCollectionBookTheme& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    row >> r.ID >> r.CodeName >> r.str24 >> r.str40 >> r.str5C >> r.CompleteNum;
    return r.Service;
}

int RefRow_Read(CRefRow& row, RefCollectionBookItem& r)
{
    row >> r.Service;
    if (!r.Service)
        return 0;
    row >> r.ItemCodeName >> r.str20 >> r.ThemeCodeName >> r.SlotIndex >> r.str5C >> r.str78;
    return r.Service;
}

// ---------------------------------------------------------------------------------------------
// Loaders and builders

int RefData_LoadIdBytePairsTable(CReferenceData* pData, CDBRecord* pRecord)
{
    CRefRow row(static_cast<CDBRecStream*>(pRecord));
    RefIdBytePairs* pRow = new RefIdBytePairs;
    RefData_ReadIdBytePairs5(row, *pRow);
    pData->m_vecHWANLevel.push_back(pRow);
    return 1;
}

// The collection book maps do not own their rows (ClearCollectionBook frees them).
template <class T, class TMap>
static int RefData_LoadCollectionBookRow(CDBRecord* pRecord, TMap& map,
                                         std::string T::*pKey, const char* pszFormat)
{
    CRefRow row(static_cast<CDBRecStream*>(pRecord));
    T* pRow = new T;
    if (!RefRow_Read(row, *pRow))
    {
        delete pRow;
        return 1;
    }
    if (map.find(pRow->*pKey) == map.end())
    {
        map.insert(std::make_pair(pRow->*pKey, pRow));
        return 1;
    }
    LogPrintf(0x2000000, pszFormat, (pRow->*pKey).c_str());
    delete pRow;
    return 0;
}

int RefData_LoadCollectionBookTheme(CReferenceData* pData, CDBRecord* pRecord)
{
    return RefData_LoadCollectionBookRow(pRecord, pData->m_mapCollectionBookTheme,
                                         &RefCollectionBookTheme::CodeName,
                                         "Already exists '_RefCollectionBook_Theme' CodeName : %s");
}

int RefData_LoadCollectionBookItem(CReferenceData* pData, CDBRecord* pRecord)
{
    return RefData_LoadCollectionBookRow(pRecord, pData->m_mapCollectionBookItem,
                                         &RefCollectionBookItem::ItemCodeName,
                                         "Already exists '_RefCollectionBook_Item' CodeName : %s");
}

int RefData_BuildCollectionBookItem(CReferenceData* pData, RefTableEntry* pTable, unsigned int)
{
    if (pTable->m_listRows.size() == 0)
        return 0;
    for (CDBRecordList::iterator it = pTable->m_listRows.begin(); it != pTable->m_listRows.end();
         ++it)
    {
        if (!RefData_LoadCollectionBookItem(pData, *it))
        {
            BSAssertFail();
            return 0;
        }
    }
    return RefData_ValidateCollectionBook(pData);
}

// TypeID 3/3/12/5 (not bionic): the collection book items.
static BOOL RefTID_IsCollectionBookItem(WORD wTypeID)
{
    return (wTypeID & 2) == 0 && (wTypeID & 0x1C) == 0xC && (wTypeID & 0x60) == 0x60 &&
           (wTypeID & 0x780) == 0x600 && (wTypeID & 0xF800) == 0x2800;
}

BOOL RefData_ValidateCollectionBook(CReferenceData* pData)
{
    BOOL bResult = TRUE;
    if (pData->m_mapCollectionBookTheme.size() == 0)
    {
        LogPrintf(0x2000000, "RefCollectionBook_Theme is empty");
        bResult = FALSE;
    }
    if (pData->m_mapCollectionBookItem.size() == 0)
    {
        LogPrintf(0x2000000, "RefCollectionBook_Item is empty");
        bResult = FALSE;
    }

    for (CReferenceData::RefCollectionBookItemMap::iterator it =
             pData->m_mapCollectionBookItem.begin();
         it != pData->m_mapCollectionBookItem.end(); ++it)
    {
        RefCollectionBookItem* pItem = it->second;
        if (!pItem)
        {
            LogPrintf(0x2000000, "pItem is NULL");
            bResult = FALSE;
            continue;
        }
        CReferenceData::RefCollectionBookThemeMap::iterator itTheme =
            pData->m_mapCollectionBookTheme.find(pItem->ThemeCodeName);
        RefCollectionBookTheme* pTheme =
            itTheme == pData->m_mapCollectionBookTheme.end() ? NULL : itTheme->second;
        const char* pszItem = pItem->ItemCodeName.c_str();

        CReferenceData::RefObjByCodeNameHashMap::iterator itObj =
            pData->m_hashRefObjByCodeName.find(pItem->ItemCodeName);
        const char* pszError = NULL;
        if (itObj == pData->m_hashRefObjByCodeName.end() || !itObj->second)
            pszError = "Ilegal '_RefCollectionBook_Item' ItemCodeName : %s";
        else if (!RefTID_IsCollectionBookItem(itObj->second->TypeID))
            pszError = "%s is not 'CollectionBookItem'";
        else if (!pItem->SlotIndex)
            pszError = "SlotIndex is 0. '_RefCollectionBook_Item' ItemCodeName : %s";
        if (pszError)
        {
            LogPrintf(0x2000000, pszError, pszItem);
            bResult = FALSE;
            continue;
        }

        if (!pTheme)
        {
            LogPrintf(0x2000000,
                      "Can't find '_RefCollectionBook_Theme'. ItemCodeName : %s  "
                      "ThemeCodeName : %s",
                      pszItem, pItem->ThemeCodeName.c_str());
            bResult = FALSE;
            continue;
        }
        DWORD dwSlot = pItem->SlotIndex;
        if (pTheme->CompleteNum < dwSlot)
        {
            LogPrintf(0x2000000,
                      "In 'CollectionBook_Item' %s. 'SlotIndex(%u)' is over %s->CompleteNum(%u)",
                      pszItem, dwSlot, pTheme->CodeName.c_str(), pTheme->CompleteNum);
            bResult = FALSE;
            continue;
        }
        std::vector<RefCollectionBookItem*>& vecItem = pTheme->vecItem;
        if (vecItem.size() >= dwSlot + 1 && vecItem.at(dwSlot))
        {
            // BUG(SM-070): the original passes two arguments for the three %s of the line; the
            // last one is whatever is on its stack.
            LogPrintf(0x2000000,
                      "Theme '%s' already has this index. ItemCodeName : %s  ThemeCodeName : %s",
                      pszItem, pItem->ThemeCodeName.c_str(), "");
            bResult = FALSE;
            continue;
        }
        if (vecItem.size() < pTheme->CompleteNum + 1)
        {
            vecItem.resize(pTheme->CompleteNum + 1);
            for (std::vector<RefCollectionBookItem*>::iterator itSlot = vecItem.begin();
                 itSlot != vecItem.end(); ++itSlot)
            {
                *itSlot = NULL;
            }
        }
        pItem->ThemeID = pTheme->ID;
        vecItem.at(pItem->SlotIndex) = pItem;
    }

    for (CReferenceData::RefCollectionBookThemeMap::iterator it =
             pData->m_mapCollectionBookTheme.begin();
         it != pData->m_mapCollectionBookTheme.end(); ++it)
    {
        RefCollectionBookTheme* pTheme = it->second;
        for (unsigned int i = 1; i < pTheme->vecItem.size(); ++i)
        {
            if (!pTheme->vecItem.at(i))
            {
                LogPrintf(0x2000000, "Theme '%s' has empty index : %u", pTheme->CodeName.c_str(),
                          i);
                bResult = FALSE;
            }
        }
        CReferenceData::RefCollectionBookThemeByIDMap::iterator itID =
            pData->m_mapCollectionBookThemeByID.find(pTheme->ID);
        if (itID != pData->m_mapCollectionBookThemeByID.end() && itID->second)
        {
            LogPrintf(0x2000000, "Theme '%s' already has this ID : %u", pTheme->CodeName.c_str(),
                      pTheme->ID);
            bResult = FALSE;
        }
        else
        {
            pData->m_mapCollectionBookThemeByID[pTheme->ID] = pTheme;
        }
    }
    return bResult;
}

RefRentItem* RefData_FindRentItem(CReferenceData* pData, const char* pszCodeName)
{
    CReferenceData::RefRentItemHashMap::iterator it =
        pData->m_hashRentItem.find(std::string(pszCodeName));
    if (it == pData->m_hashRentItem.end())
        return NULL;
    return it->second;
}

// ---------------------------------------------------------------------------------------------
// Monster lists by kind

// Monsters: TypeID 1/2/1 (bionic).
static BOOL RefTID_IsMonster(WORD wTypeID)
{
    return (wTypeID & 2) != 0 && (wTypeID & 0x1C) == 4 && (wTypeID & 0x60) == 0x40 &&
           (wTypeID & 0x780) == 0x80;
}

// 0 for psz1, 1 for psz2 (the start of the code name, case insensitive), 2 otherwise.
static unsigned int RefData_MonsterKind(const std::string& strCodeName, const char* psz1,
                                        const char* psz2)
{
    if (!_strnicmp(strCodeName.c_str(), psz1, lstrlenA(psz1)))
        return 0;
    return (_strnicmp(strCodeName.c_str(), psz2, lstrlenA(psz2)) != 0) + 1;
}

static void RefData_BuildMonsterKindList(CReferenceData* pData,
                                         CReferenceData::RefObjBandVector& vecKind, WORD wTID4,
                                         const char* psz1, const char* psz2)
{
    vecKind.resize(3);
    for (unsigned int i = 0; i < 3; ++i)
        vecKind.at(i).resize(140);
    for (CReferenceData::RefObjHashMap::iterator it = pData->m_hashRefObj.begin();
         it != pData->m_hashRefObj.end(); ++it)
    {
        RefObjCommonRow* pObj = it->second;
        WORD wTypeID = pObj->TypeID;
        if (!RefTID_IsMonster(wTypeID) || (wTypeID & 0xF800) != wTID4)
            continue;
        unsigned int nKind = RefData_MonsterKind(pObj->CodeName128, psz1, psz2);
        unsigned int nLevel = (unsigned int)static_cast<const RefObjChar*>(pObj)->Lvl - 1;
        if (nLevel > 0x8B)
        {
            BSAssertFail();
            continue;
        }
        vecKind.at(nKind).at(nLevel).push_back(pObj);
    }
}

void RefData_BuildHunterMonsterList(CReferenceData* pData)
{
    RefData_BuildMonsterKindList(pData, pData->m_vecHunterMonster, 0x1800, "MOB_HUNTER",
                                 "MOB_EU_HUNTER");
}

void RefData_BuildThiefMonsterList(CReferenceData* pData)
{
    RefData_BuildMonsterKindList(pData, pData->m_vecThiefMonster, 0x1000, "MOB_THIEF",
                                 "MOB_EU_THIEF");
}

void RefData_BuildTrapMonsterList(CReferenceData* pData, const std::string& strKind)
{
    std::string strPrefix = std::string("MOB_") + strKind;
    for (CReferenceData::RefObjHashMap::iterator it = pData->m_hashRefObj.begin();
         it != pData->m_hashRefObj.end(); ++it)
    {
        RefObjCommonRow* pObj = it->second;
        WORD wTypeID = pObj->TypeID;
        if (!RefTID_IsMonster(wTypeID) || (wTypeID & 0xF800) == 0x1000 ||
            (wTypeID & 0xF800) == 0x1800)
        {
            continue;
        }
        BYTE btRarity = pObj->Rarity;
        if (btRarity == 3 || btRarity == 8)
            continue;
        // The code name is compared over the length of the other string (as the original).
        const char* pszCode = pObj->CodeName128.c_str();
        if (CompareStringA(0x400, 0, pszCode, (int)strPrefix.size(), strPrefix.c_str(),
                           (int)strPrefix.size()) == 2 &&
            CompareStringA(0x400, 0, pszCode, 17, "MOB_TQ_QINSHITRAP", 17) != 2 &&
            CompareStringA(0x400, 0, pszCode, 16, "MOB_TQ_SNAKETRAP", 16) != 2)
        {
            pData->m_mapTrapMonster[strKind].push_back(pObj);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Special item / monster code lists

// The static objects 0x89aff4 / 0x89b008 and their singleton pointers.
static CSpecialCodeList s_SpecialItemCodeList;
static CSpecialCodeList s_SpecialMonsterCodeList;
static CSpecialCodeList* s_pSpecialItemCodeList = NULL;
static CSpecialCodeList* s_pSpecialMonsterCodeList = NULL;

CSpecialCodeList* CSpecialCodeList::CreateItemList()
{
    if (s_pSpecialItemCodeList)
        BSAssertFail();
    s_pSpecialItemCodeList = &s_SpecialItemCodeList;
    s_SpecialItemCodeList.m_dw00 = 0;
    s_SpecialItemCodeList.m_vecEntry.clear();
    // SpecialItemCodeList_RegisterDefaults (0x4a21b0).
    s_SpecialItemCodeList.Add("ITEM_COS_C_UNICORN", 1, 0);
    s_SpecialItemCodeList.Add("ITEM_ETC_E070919_TICKET", 2, 0);
    return &s_SpecialItemCodeList;
}

CSpecialCodeList* CSpecialCodeList::CreateMonsterList()
{
    if (s_pSpecialMonsterCodeList)
        BSAssertFail();
    s_pSpecialMonsterCodeList = &s_SpecialMonsterCodeList;
    s_SpecialMonsterCodeList.m_dw00 = 0;
    s_SpecialMonsterCodeList.m_vecEntry.clear();
    // SpecialMonsterCodeList_RegisterDefaults (0x4a25a0, through the wrapper 0x4a2590).
    s_SpecialMonsterCodeList.Add("MOB_TQ_", 0, 1);
    return &s_SpecialMonsterCodeList;
}

void CSpecialCodeList::Clear()
{
    m_dw00 = 0;
    for (std::vector<SpecialCodeEntry*>::iterator it = m_vecEntry.begin(); it != m_vecEntry.end();
         ++it)
    {
        if (*it)
        {
            delete *it;
            *it = NULL;
        }
    }
    m_vecEntry.clear();
    std::vector<SpecialCodeEntry*>().swap(m_vecEntry);
}

void CSpecialCodeList::DestroyItemList()
{
    s_SpecialItemCodeList.Clear();
    s_pSpecialItemCodeList = NULL;
}

void CSpecialCodeList::DestroyMonsterList()
{
    s_SpecialMonsterCodeList.Clear();
    s_pSpecialMonsterCodeList = NULL;
}

CSpecialCodeList* CSpecialCodeList::GetItemList()
{
    return s_pSpecialItemCodeList;
}

CSpecialCodeList* CSpecialCodeList::GetMonsterList()
{
    return s_pSpecialMonsterCodeList;
}

void CSpecialCodeList::Add(const char* pszCodeName, DWORD dwFlag1, DWORD dwFlag2)
{
    SpecialCodeEntry* pEntry = new SpecialCodeEntry;
    pEntry->strCodeName.assign(pszCodeName, strlen(pszCodeName));
    pEntry->dwFlag1 = dwFlag1;
    pEntry->dwFlag2 = dwFlag2;
    m_vecEntry.push_back(pEntry);
}

void CSpecialCodeList::ApplyToItem(RefObjCommonRow* pObj)
{
    if (!pObj)
        return;
    pObj->SpecialFlag1 = 0;
    pObj->SpecialFlag2 = 0;
    for (std::vector<SpecialCodeEntry*>::iterator it = m_vecEntry.begin(); it != m_vecEntry.end();
         ++it)
    {
        SpecialCodeEntry* pEntry = *it;
        if (!pEntry)
            continue;
        const char* pszCode = pObj->CodeName128.c_str();
        const char* pszEntry = pEntry->strCodeName.c_str();
        if (CompareStringA(0x400, 0, pszEntry, lstrlenA(pszEntry), pszCode,
                           lstrlenA(pszCode)) == 2)
        {
            pObj->SpecialFlag1 |= pEntry->dwFlag1;
            pObj->SpecialFlag2 |= pEntry->dwFlag2;
        }
    }
}

void CSpecialCodeList::ApplyToChar(RefObjCommonRow* pObj)
{
    if (!pObj)
        return;
    pObj->SpecialFlag1 = 0;
    pObj->SpecialFlag2 = 0;
    for (std::vector<SpecialCodeEntry*>::iterator it = m_vecEntry.begin(); it != m_vecEntry.end();
         ++it)
    {
        SpecialCodeEntry* pEntry = *it;
        if (!pEntry)
            continue;
        int nLength = (int)pEntry->strCodeName.size();
        if (CompareStringA(0x400, 0, pEntry->strCodeName.c_str(), nLength,
                           pObj->CodeName128.c_str(), nLength) == 2)
        {
            pObj->SpecialFlag1 |= pEntry->dwFlag1;
            pObj->SpecialFlag2 |= pEntry->dwFlag2;
            return;
        }
    }
}

stdext::hash_set<int>& RefObjItem_GetSpecialItemIDSet()
{
    // The atexit destructor of the original is ServerConfigExtraMap_AtExit (0x4a1100).
    static stdext::hash_set<int> s_setID;
    return s_setID;
}

// ---------------------------------------------------------------------------------------------
// TypeID tests

BOOL RefTID_Is_1_2_4(const WORD& wTypeID)
{
    WORD w = wTypeID;
    return (w & 2) != 0 && (w & 0x1C) == 4 && (w & 0x60) == 0x40 && (w & 0x780) == 0x200;
}

BOOL RefTID_Is_1_2_5(const WORD& wTypeID)
{
    WORD w = wTypeID;
    return (w & 2) != 0 && (w & 0x1C) == 4 && (w & 0x60) == 0x40 && (w & 0x780) == 0x280;
}

BOOL RefTID_Is_3_1_13_4(const WORD& wTypeID)
{
    WORD w = wTypeID;
    return (w & 2) == 0 && (w & 0x1C) == 0xC && (w & 0x60) == 0x20 && (w & 0x780) == 0x680 &&
           (w & 0xF800) == 0x2000;
}

BOOL RefTID_Is_3_3_11_3(const WORD& wTypeID)
{
    WORD w = wTypeID;
    return (w & 2) == 0 && (w & 0x1C) == 0xC && (w & 0x60) == 0x60 && (w & 0x780) == 0x580 &&
           (w & 0xF800) == 0x1800;
}

BOOL RefTID_Is_3_3_11_7(const WORD& wTypeID)
{
    WORD w = wTypeID;
    return (w & 2) == 0 && (w & 0x1C) == 0xC && (w & 0x60) == 0x60 && (w & 0x780) == 0x580 &&
           (w & 0xF800) == 0x3800;
}

// ---------------------------------------------------------------------------------------------
// sDataStream

// sDataStream_GetParsePos (0x6554f0): the first position at or after nPos that is not a space.
static unsigned int sDataStream_GetParsePos(const std::string& strData, unsigned int nPos)
{
    if (strData.size() <= nPos)
        return nPos;
    while (strData.c_str()[nPos] == ' ')
        ++nPos;
    return nPos;
}

int sDataStream_ExtractToken(const std::string& strData, int nPos, const char* pszPrefix,
                             const char* pszDelim, std::string& strOut)
{
    unsigned int nStart = sDataStream_GetParsePos(strData, (unsigned int)nPos);
    if (strData.size() < nStart)
        return -1;
    unsigned int nPrefix = 0;
    if (pszPrefix)
    {
        strData.find(pszPrefix, nStart);
        nPrefix = lstrlenA(pszPrefix);
    }
    std::string::size_type nDelim = strData.find(pszDelim, nStart + nPrefix);
    if (nDelim == std::string::npos)
        return -1;
    strOut = strData.substr(nStart + nPrefix, nDelim - nPrefix - nStart);
    return (int)nDelim + lstrlenA(pszDelim);
}

void sDataStream::ParseTarget(const std::string& strToken)
{
    if (strToken.compare("A") == 0)
        m_nTarget = TARGET_A;
    else if (strToken.compare("P") == 0)
        m_nTarget = TARGET_P;
    else
        BSAssertFail();
}

// The next token of the parameter, FALSE (after an assert) when there is none.
static BOOL sDataStream_Next(const std::string& strData, int* pnPos, const char* pszDelim,
                             std::string& strToken)
{
    *pnPos = sDataStream_ExtractToken(strData, *pnPos, NULL, pszDelim, strToken);
    if (*pnPos == -1)
    {
        BSAssertFail();
        return FALSE;
    }
    return TRUE;
}

BOOL UsedObjectUnLimit::Parse(const std::string& strData, int* pnPos)
{
    std::string strToken;
    if (!sDataStream_Next(strData, pnPos, "]", strToken))
        return FALSE;
    m_strName = strToken;
    m_nTarget = TARGET_P;
    return TRUE;
}

BOOL PSU::Parse(const std::string& strData, int* pnPos)
{
    std::string strToken;
    if (!sDataStream_Next(strData, pnPos, ",", strToken))
        return FALSE;
    m_strName = strToken;
    m_nTarget = TARGET_P;
    if (!sDataStream_Next(strData, pnPos, "]", strToken))
        return FALSE;
    m_nValue = atol(strToken.c_str());
    return TRUE;
}

BOOL UsedObjectLimit::Parse(const std::string& strData, int* pnPos)
{
    std::string strToken;
    if (!sDataStream_Next(strData, pnPos, ",", strToken))
        return FALSE;
    m_strName = strToken;
    if (!sDataStream_Next(strData, pnPos, ",", strToken))
        return FALSE;
    ParseTarget(strToken);
    if (!sDataStream_Next(strData, pnPos, ",", strToken))
        return FALSE;
    m_nValue1 = atol(strToken.c_str());
    if (!sDataStream_Next(strData, pnPos, "]", strToken))
        return FALSE;
    m_nValue2 = atol(strToken.c_str());
    return TRUE;
}

BOOL BUFF_ITEM::Parse(const std::string& strData, int* pnPos)
{
    std::string strToken;
    if (!sDataStream_Next(strData, pnPos, ",", strToken))
        return FALSE;
    m_strName = strToken;
    if (!sDataStream_Next(strData, pnPos, ",", strToken))
        return FALSE;
    ParseTarget(strToken);
    if (!sDataStream_Next(strData, pnPos, "]", strToken))
        return FALSE;
    m_nValue = atol(strToken.c_str());
    return TRUE;
}

BOOL BUF_CHARACTER_STATUS::Parse(const std::string& strData, int* pnPos)
{
    std::string strToken;
    if (!sDataStream_Next(strData, pnPos, ",", strToken))
        return FALSE;
    m_strName = strToken;
    if (!sDataStream_Next(strData, pnPos, ",", strToken))
        return FALSE;
    ParseTarget(strToken);
    if (!sDataStream_Next(strData, pnPos, ",", strToken))
        return FALSE;
    m_nValue = atol(strToken.c_str());
    for (int i = 0; i < 4; ++i)
    {
        if (!sDataStream_Next(strData, pnPos, ",", strToken))
            return FALSE;
        m_vecStatus.push_back(atol(strToken.c_str()));
    }
    if (!sDataStream_Next(strData, pnPos, "]", strToken))
        return FALSE;
    m_vecStatus.push_back(atol(strToken.c_str()));
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// Gacha free parameters

CGachaFreeParam::~CGachaFreeParam()
{
    for (unsigned int i = 0; i < m_vecEntry.size(); ++i)
        delete m_vecEntry.at(i);
    m_vecEntry.clear();
    std::vector<CGachaFreeParamEntry*>().swap(m_vecEntry);
}

DWORD Util_PackFourCC(const std::string& str)
{
    if (!str.size() || str.size() > 4)
        BSAssertFail();
    BYTE ab[4] = { 0, 0, 0, 0 };
    unsigned int nOffset = 4 - str.size();
    for (unsigned int i = 0; i < str.size(); ++i)
        ab[nOffset + i] = (BYTE)str.at(i);
    return (DWORD)ab[0] << 24 | (DWORD)ab[1] << 16 | (DWORD)ab[2] << 8 | ab[3];
}

void CGachaFreeParam_ParseToken(const std::string& strToken, CGachaFreeParamEntry& entry)
{
    entry.bValid = 1;
    entry.btType = (BYTE)strToken.substr(0, 1).c_str()[0];
    std::string::size_type nColon = strToken.find(":", 1);
    if (nColon == std::string::npos)
        BSAssertFail();
    std::string::size_type nPos = nColon + 1;
    switch (entry.btType)
    {
    case CGachaFreeParamEntry::TYPE_MAGIC_OPTION:
        {
            std::string::size_type nComma1 = strToken.find(",", nPos);
            if (nComma1 == std::string::npos)
                BSAssertFail();
            entry.dwCode = Util_PackFourCC(strToken.substr(nPos, nComma1 - nPos));
            std::string::size_type nComma2 = strToken.find(",", nComma1 + 1);
            entry.btLevel =
                (BYTE)atol(strToken.substr(nComma1 + 1, nComma2 - (nComma1 + 1)).c_str());
            std::string::size_type nComma3 = strToken.find(",", nComma2 + 1);
            entry.nValue = _atoi64(strToken.substr(nComma2 + 1, nComma3 - (nComma2 + 1)).c_str());
        }
        break;
    case CGachaFreeParamEntry::TYPE_DURABILITY:
    case CGachaFreeParamEntry::TYPE_OPT_LEVEL:
    case CGachaFreeParamEntry::TYPE_VALUE:
        entry.nValue = _atoi64(strToken.substr(nPos).c_str());
        break;
    default:
        BSAssertFail();
        break;
    }
}

void CGachaFreeParam::Parse(const std::string& strDesc)
{
    if (strDesc.find("[BIIV]", 0) == std::string::npos)
        return;
    std::string::size_type nPos = strDesc.find("<", 0);
    while (nPos != std::string::npos)
    {
        if (m_vecEntry.size() >= 5)
            BSAssertFail();
        ++nPos;
        // BUG(SM-071): without a '>' the search below starts over at 0 and never ends.
        std::string::size_type nEnd = strDesc.find(">", nPos);
        std::string strToken = strDesc.substr(nPos, nEnd - nPos);
        CGachaFreeParamEntry* pEntry = new CGachaFreeParamEntry;
        CGachaFreeParam_ParseToken(strToken, *pEntry);
        for (unsigned int i = 0; i < m_vecEntry.size(); ++i)
        {
            const CGachaFreeParamEntry* pOld = m_vecEntry.at(i);
            if (pOld->bValid == 1 && pOld->btType == pEntry->btType &&
                (pOld->btType != CGachaFreeParamEntry::TYPE_MAGIC_OPTION ||
                 pOld->dwCode == pEntry->dwCode))
            {
                BSAssertFail();
            }
        }
        m_vecEntry.push_back(pEntry);
        nPos = strDesc.find("<", nEnd + 1);
    }
}
