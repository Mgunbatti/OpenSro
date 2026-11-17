#include "CertData.h"

#include "ServerDataExtern.h"

using ServerFramework::CMassiveMsg;

CertDataGroupList*      g_pCertDataGroupList = NULL;
CertDataSettingList*    g_pCertDataSettingList = NULL;
CertDataGroupIPList*    g_pCertDataGroupIPList = NULL;

stdext::hash_map<BYTE, PrivIPSet*>      g_hmapPrivIP;
std::map<DWORD, CERT_DATA_SETTING*>     g_mapCertDataSetting;
std::map<BYTE, CERT_DATA_GROUP*>        g_mapCertDataGroup;
CCriticalSectionBS                      g_csCertData;

template <class T>
static void FreeDataList(std::list<T*>*& pList)
{
    if (!pList)
        return;
    for (typename std::list<T*>::iterator it = pList->begin(); it != pList->end(); ++it)
        operator delete(*it);
    pList->clear();
    delete pList;
    pList = NULL;
}

void CertData_FreeRecordListsAndIPMaps()
{
    CSingleLockBS lock(&g_csCertData, TRUE);

    g_mapCertDataSetting.clear();
    g_mapCertDataGroup.clear();
    if (g_hmapPrivIP.size())
    {
        for (stdext::hash_map<BYTE, PrivIPSet*>::iterator it = g_hmapPrivIP.begin();
             it != g_hmapPrivIP.end(); ++it)
        {
            PrivIPSet* pSet = it->second;
            pSet->clear();
            if (pSet)
                PrivIPMap_DeleteIpRangeMap(pSet);
        }
        g_hmapPrivIP.clear();
    }
    FreeDataList(g_pCertDataGroupIPList);
    FreeDataList(g_pCertDataSettingList);
    FreeDataList(g_pCertDataGroupList);
}

void PrivIPMap_DeleteIpRangeMap(PrivIPSet* pSet)
{
    delete pSet;
}

bool CertData_BuildPrivilegedIPMaps(CertDataGroupList* pGroups, CertDataSettingList* pSettings,
                                    CertDataGroupIPList* pGroupIPs)
{
    CSingleLockBS lock(&g_csCertData, TRUE);

    g_pCertDataGroupList = pGroups;
    g_pCertDataSettingList = pSettings;
    g_pCertDataGroupIPList = pGroupIPs;
    if (g_pCertDataGroupList->size() == 0)
        BSAssertFail();
    if (g_pCertDataSettingList->size() == 0)
        BSAssertFail();
    if (g_pCertDataGroupIPList->size() == 0)
        BSAssertFail();

    for (CertDataGroupList::iterator it = g_pCertDataGroupList->begin();
         it != g_pCertDataGroupList->end(); ++it)
    {
        CERT_DATA_GROUP* pGroup = *it;
        PrivIPSet* pSet = new PrivIPSet;
        for (CertDataGroupIPList::iterator itIP = pGroupIPs->begin(); itIP != pGroupIPs->end();
             ++itIP)
        {
            CERT_DATA_GROUP_IP* pIP = *itIP;
            if (pIP->byGroupID != pGroup->byID)
                continue;
            DWORD dwIP = pIP->dwIP;
            if (pSet->find(dwIP) != pSet->end())
                BSAssertFail();
            pSet->insert(dwIP);
        }
        // BUG(FWDATA-003): a second group with the same ID is not inserted; its set leaks.
        g_hmapPrivIP.insert(std::make_pair(pGroup->byID, pSet));
    }

    for (CertDataSettingList::iterator it = g_pCertDataSettingList->begin();
         it != g_pCertDataSettingList->end(); ++it)
    {
        g_mapCertDataSetting.insert(std::make_pair((*it)->dwValue, *it));
    }
    for (CertDataGroupList::iterator it = g_pCertDataGroupList->begin();
         it != g_pCertDataGroupList->end(); ++it)
    {
        g_mapCertDataGroup.insert(std::make_pair((*it)->byID, *it));
    }
    return true;
}

template <class T>
static void WriteDataList(CMassiveMsg* pMsg, std::list<T*>* pList)
{
    BYTE b = 0;
    pMsg->Write(&b, 1);
    for (typename std::list<T*>::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        b = 1;
        pMsg->Write(&b, 1);
        pMsg->Write(*it, sizeof(T));
    }
    b = 2;
    pMsg->Write(&b, 1);
}

bool CertData_SerializeRecordListsToMsg(CMassiveMsg* pMsg)
{
    CSingleLockBS lock(&g_csCertData, TRUE);
    WriteDataList(pMsg, g_pCertDataGroupList);
    WriteDataList(pMsg, g_pCertDataSettingList);
    WriteDataList(pMsg, g_pCertDataGroupIPList);
    return true;
}

// Reads BYTE 0, then (BYTE 1, record) while the flag is 1.
template <class T>
static std::list<T*>* ReadDataList(CMassiveMsg* pMsg)
{
    std::list<T*>* pList = new std::list<T*>;
    BYTE bFlag;
    pMsg->Read(&bFlag, 1);
    pMsg->Read(&bFlag, 1);
    if (bFlag == 1)
    {
        do
        {
            T* pRec = (T*)operator new(sizeof(T));
            pMsg->Read(pRec, sizeof(T));
            pMsg->Read(&bFlag, 1);
            pList->push_back(pRec);
        } while (bFlag == 1);
    }
    return pList;
}

bool CertData_ReadRecordLists(CMassiveMsg* pMsg)
{
    CSingleLockBS lock(&g_csCertData, TRUE);
    CertDataGroupList* pGroups = ReadDataList<CERT_DATA_GROUP>(pMsg);
    CertDataSettingList* pSettings = ReadDataList<CERT_DATA_SETTING>(pMsg);
    CertDataGroupIPList* pGroupIPs = ReadDataList<CERT_DATA_GROUP_IP>(pMsg);
    CertData_BuildPrivilegedIPMaps(pGroups, pSettings, pGroupIPs);
    return true;
}

bool PrivIPMap_ContainsIP(BYTE byGroupID, DWORD dwIP)
{
    CSingleLockBS lock(&g_csCertData, TRUE);
    stdext::hash_map<BYTE, PrivIPSet*>::iterator it = g_hmapPrivIP.find(byGroupID);
    if (it == g_hmapPrivIP.end())
        return false;
    PrivIPSet* pSet = it->second;
    return pSet->find(dwIP) != pSet->end();
}

DWORD CertData_GetIgnoreShardMaxUserValue()
{
    CSingleLockBS lock(&g_csCertData, TRUE);
    for (CertDataSettingList::iterator it = g_pCertDataSettingList->begin();
         it != g_pCertDataSettingList->end(); ++it)
    {
        CERT_DATA_SETTING* pSetting = *it;
        if (CompareNoCase(pSetting->szName, "Ignore_Shard_MaxUser") == CSTR_EQUAL)
            return pSetting->dwValue;
    }
    return 0;
}

// Map53B958_FindValueLocked (0x44d040). Unreferenced in GatewayServer.
CERT_DATA_SETTING* CertData_FindSettingLocked(DWORD dwID)
{
    CSingleLockBS lock(&g_csCertData, TRUE);
    std::map<DWORD, CERT_DATA_SETTING*>::iterator it = g_mapCertDataSetting.find(dwID);
    if (it == g_mapCertDataSetting.end())
        return NULL;
    return it->second;
}

// Map53B964_FindValueLocked (0x44d160). Unreferenced in GatewayServer.
CERT_DATA_GROUP* CertData_FindGroupLocked(BYTE byID)
{
    CSingleLockBS lock(&g_csCertData, TRUE);
    std::map<BYTE, CERT_DATA_GROUP*>::iterator it = g_mapCertDataGroup.find(byID);
    if (it == g_mapCertDataGroup.end())
        return NULL;
    return it->second;
}
