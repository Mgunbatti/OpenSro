#include "CertArch.h"

#include "BSLib/file/IFile.h"
#include "ServerFramework/CertData.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerDataExtern.h"

using ServerFramework::CMassiveMsg;

int ServerName_CompareCertification()
{
    int nCertLen = lstrlenA("Certification");
    int nNameLen = lstrlenA(SERVER_MODULE_NAME);
    return CompareStringA(0x400, 0, SERVER_MODULE_NAME, nNameLen, "Certification", nCertLen) - 2;
}

CERT_MODULE* CertModule_FindByName(const char* pszName)
{
    std::string strLower;
    StringToLowerMbcs(pszName, strLower);
    std::map<std::string, CERT_MODULE*>::iterator it = g_mapCertModuleByName.find(strLower);
    if (it == g_mapCertModuleByName.end())
        return NULL;
    return it->second;
}

CERT_MODULE* CertModule_FindById(BYTE byModuleID)
{
    if (!g_pCertModuleList)
        BSAssertFail();
    std::map<BYTE, CERT_MODULE*>::iterator it = g_mapCertModuleByID.find(byModuleID);
    if (it == g_mapCertModuleByID.end())
        return NULL;
    return it->second;
}

CERT_MACHINE* CertMachine_FindById(DWORD dwMachineID)
{
    std::map<DWORD, CERT_MACHINE*>::iterator it = g_mapCertMachine.find(dwMachineID);
    if (it == g_mapCertMachine.end())
        return NULL;
    return it->second;
}

bool CertData_BuildServerNameMap(CertModuleList* pList)
{
    if (g_mapCertModuleByID.size())
        BSAssertFail();
    if (!pList)
        BSAssertFail();
    if (pList->size() == 0)
        BSAssertFail();

    g_pCertModuleList = pList;
    std::string strLower;
    for (CertModuleList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        CERT_MODULE* pModule = *it;
        g_mapCertModuleByID.insert(std::make_pair(pModule->byID, pModule));
        std::string strName(pModule->szName);
        StringToLowerMbcs(strName.c_str(), strLower);
        strName = strLower;
        g_mapCertModuleByName.insert(std::make_pair(strName, pModule));
    }
    return true;
}

bool CertArch_BuildServerNotifyTargets(CertContentList* pContent, CertDivisionList* pDivision,
                                       CertFarmList* pFarm, CertFarmContentList* pFarmContent,
                                       CertShardList* pShard, CertMachineList* pMachine,
                                       ServerBodyList* pBody, ServerCordList* pCord)
{
    if (!pBody)
        BSAssertFail();
    if (pBody->size() == 0)
        BSAssertFail();
    if (g_mapServerBody.size())
        BSAssertFail();

    g_pCertContentList = pContent;
    g_pCertShardList = pShard;
    g_pCertDivisionList = pDivision;
    g_pCertFarmList = pFarm;
    g_pCertFarmContentList = pFarmContent;
    g_pCertMachineList = pMachine;

    for (CertFarmContentList::iterator it = pFarmContent->begin(); it != pFarmContent->end();
         ++it)
    {
        CERT_FARM_CONTENT* pRec = *it;
        for (CertFarmList::iterator itFarm = pFarm->begin(); itFarm != pFarm->end(); ++itFarm)
        {
            if ((*itFarm)->byID == pRec->byFarmID)
            {
                pRec->pFarm = *itFarm;
                break;
            }
        }
    }
    for (CertShardList::iterator it = pShard->begin(); it != pShard->end(); ++it)
    {
        CERT_SHARD* pRec = *it;
        for (CertFarmList::iterator itFarm = pFarm->begin(); itFarm != pFarm->end(); ++itFarm)
        {
            if ((*itFarm)->byID == pRec->byFarmID)
            {
                pRec->pFarm = *itFarm;
                break;
            }
        }
    }

    g_pServerBodyList = pBody;
    g_pServerCordList = pCord;
    g_pSelfBody = pBody->front();

    for (CertMachineList::iterator it = pMachine->begin(); it != pMachine->end(); ++it)
        g_mapCertMachine.insert(std::make_pair((*it)->dwID, *it));

    ServerBodyList::iterator itBody;
    for (itBody = g_pServerBodyList->begin(); itBody != g_pServerBodyList->end(); ++itBody)
    {
        SERVER_BODY* p = *itBody;
        p->pModule = CertModule_FindById(p->byModuleID);
        p->pMachine = CertMachine_FindById(p->dwMachineID);

        p->pDivision = NULL;
        for (CertDivisionList::iterator it = g_pCertDivisionList->begin();
             it != g_pCertDivisionList->end(); ++it)
        {
            if ((*it)->byID == p->byDivisionID)
            {
                p->pDivision = *it;
                break;
            }
        }
        p->pFarm = NULL;
        for (CertFarmList::iterator it = g_pCertFarmList->begin(); it != g_pCertFarmList->end();
             ++it)
        {
            if ((*it)->byID == p->byFarmID)
            {
                p->pFarm = *it;
                break;
            }
        }
        p->pShard = NULL;
        for (CertShardList::iterator it = g_pCertShardList->begin();
             it != g_pCertShardList->end(); ++it)
        {
            if ((*it)->wID == p->wShardID)
            {
                p->pShard = *it;
                break;
            }
        }

        if (p->byDivisionID && !p->byFarmID && !p->wShardID &&
            p->pModule == CertModule_FindByName("GlobalManager"))
        {
            p->pDivision->wGlobalManagerID = p->wID;
        }
        if (p->pModule == CertModule_FindByName("MachineManager"))
            p->pMachine->wMachineManagerID = p->wID;
        g_mapServerBody.insert(std::make_pair(p->wID, p));
    }

    for (ServerCordList::iterator it = g_pServerCordList->begin();
         it != g_pServerCordList->end(); ++it)
    {
        SERVER_CORD* p = *it;
        p->dwSessionID = 0;
        g_mapServerCord.insert(std::make_pair(p->dwID, p));
    }

    ServerBody_BuildRouteTable();

    for (CertShardList::iterator it = g_pCertShardList->begin(); it != g_pCertShardList->end();
         ++it)
    {
        g_mapCertShard.insert(std::make_pair((*it)->wID, *it));
    }

    if (IsWindow(g_hServerArchView))
        PostMessageA(g_hServerArchView, WM_SERVER_ARCH_LOADED, 0, 0);

    ServerCordList lstCords;
    ServerCord_CollectForBody(g_pSelfBody->wID, &lstCords);
    for (ServerCordList::iterator it = lstCords.begin(); it != lstCords.end(); ++it)
    {
        SERVER_BODY* pPeer = ServerCord_FindPeerBody(*it, g_pSelfBody);
        if (!pPeer)
            continue;

        // Module IDs 2..5 are the common managers.
        BYTE byModuleID = pPeer->pModule->byID;
        if (byModuleID == 2 || byModuleID == 3 || byModuleID == 4 || byModuleID == 5)
        {
            if (byModuleID == g_pSelfBody->pModule->byID)
                BSAssertFail();
            if (pPeer->pShard)
            {
                BSMessageBoxPrintf("kek!!! why did you assign shard to common manager server? "
                                   "-_-+ plz check out server license table !!!");
                LogPrintf(0x2000000, "kek!!! why did you assign shard to common manager "
                                     "server? -_-+ plz check out server license table !!!");
                return false;
            }
        }

        if (g_pSelfBody->pShard && pPeer->pShard)
            continue;
        if (g_setNotifyBody.find(pPeer->wID) != g_setNotifyBody.end())
            continue;
        LogPrintf(0, "Add ServerNotify : (%d) - %s", pPeer->wID, pPeer->pModule->szName);
        g_setNotifyBody.insert(pPeer->wID);
    }

    if (g_setNotifyBody.size() == 0)
    {
        BSMessageBoxPrintf("There is no server notify target!!! (check whether Farm has "
                           "assigned shard or not...)");
        LogPrintf(0x2000000, "There is no server notify target!!! (check whether Farm has "
                             "assigned shard or not...)");
        return false;
    }
    return true;
}

// Reads one list: BYTE 0, then (BYTE flag, record) until the flag is 2.
template <class T>
static std::list<T*>* ReadCertList(CMassiveMsg* pMsg)
{
    std::list<T*>* pList = new std::list<T*>;
    BYTE bFlag;
    pMsg->Read(&bFlag, 1);
    pMsg->Read(&bFlag, 1);
    if (bFlag != 2)
    {
        do
        {
            T* pRec = (T*)operator new(sizeof(T));
            pMsg->Read(pRec, sizeof(T));
            pMsg->Read(&bFlag, 1);
            pList->push_back(pRec);
        } while (bFlag != 2);
    }
    return pList;
}

bool CServerProcessMain_ParseA003CertArchAndBuildTargets(CMassiveMsg* pMsg)
{
    CertModuleList* pModule = ReadCertList<CERT_MODULE>(pMsg);
    CertContentList* pContent = ReadCertList<CERT_CONTENT>(pMsg);
    CertDivisionList* pDivision = ReadCertList<CERT_DIVISION>(pMsg);
    CertFarmList* pFarm = ReadCertList<CERT_FARM>(pMsg);
    CertFarmContentList* pFarmContent = ReadCertList<CERT_FARM_CONTENT>(pMsg);
    CertShardList* pShard = ReadCertList<CERT_SHARD>(pMsg);
    CertMachineList* pMachine = ReadCertList<CERT_MACHINE>(pMsg);
    ServerBodyList* pBody = ReadCertList<SERVER_BODY>(pMsg);
    ServerCordList* pCord = ReadCertList<SERVER_CORD>(pMsg);

    CertData_BuildServerNameMap(pModule);

    BYTE bHasCertData;
    pMsg->Read(&bHasCertData, 1);
    if (bHasCertData)
        CertData_ReadRecordLists(pMsg);

    return CertArch_BuildServerNotifyTargets(pContent, pDivision, pFarm, pFarmContent, pShard,
                                             pMachine, pBody, pCord);
}

void CertArch_CollectPeerBodies(SERVER_BODY* pBody, ServerBodyList* pList,
                                ServerBodySet* pBodySet, ServerCordSet* pCordSet)
{
    ServerCordList lstCords;
    ServerCord_CollectForBody(pBody->wID, &lstCords);
    for (ServerCordList::iterator it = lstCords.begin(); it != lstCords.end(); ++it)
    {
        SERVER_CORD* pCord = *it;
        SERVER_BODY* pPeer = ServerCord_FindPeerBody(pCord, pBody);
        if (!pPeer)
            continue;
        if (pBodySet->find(pPeer) == pBodySet->end())
        {
            pBodySet->insert(pPeer);
            pList->push_back(pPeer);
        }
        if (pCordSet->find(pCord) == pCordSet->end())
            pCordSet->insert(pCord);
    }
}

void CertArch_CollectBodiesRecursive(SERVER_BODY* pBody, ServerBodyList* pList,
                                     ServerBodySet* pBodySet, ServerCordSet* pCordSet)
{
    CertArch_CollectPeerBodies(pBody, pList, pBodySet, pCordSet);
    for (ServerBodyList::iterator it = g_pServerBodyList->begin();
         it != g_pServerBodyList->end(); ++it)
    {
        SERVER_BODY* pChild = *it;
        if (pChild->wParentID != pBody->wID)
            continue;
        if (pBodySet->find(pChild) == pBodySet->end())
        {
            pBodySet->insert(pChild);
            pList->push_back(pChild);
        }
        CertArch_CollectBodiesRecursive(pChild, pList, pBodySet, pCordSet);
    }
}

void CertArch_CollectFarmManagerPeers(ServerBodyList* pList, ServerBodySet* pBodySet,
                                      ServerCordSet* pCordSet)
{
    CERT_MODULE* pFarmManager = CertModule_FindByName("FarmManager");
    for (CertFarmContentList::iterator it = g_pCertFarmContentList->begin();
         it != g_pCertFarmContentList->end(); ++it)
    {
        // First FarmManager body of the farm only.
        for (ServerBodyList::iterator itBody = g_pServerBodyList->begin();
             itBody != g_pServerBodyList->end(); ++itBody)
        {
            SERVER_BODY* pBody = *itBody;
            if (pBody->pModule != pFarmManager || pBody->byFarmID != (*it)->byFarmID)
                continue;
            if (pBodySet->find(pBody) == pBodySet->end())
            {
                pBodySet->insert(pBody);
                pList->push_back(pBody);
            }
            CertArch_CollectBodiesRecursive(pBody, pList, pBodySet, pCordSet);
            break;
        }
    }
}

SERVER_BODY* ServerBody_FindChildByModuleAndAddr(const char* pszModule, const char* pszAddress)
{
    if (!strlen(pszAddress))
        BSAssertFail();
    for (ServerBodyList::iterator it = g_pServerBodyList->begin();
         it != g_pServerBodyList->end(); ++it)
    {
        SERVER_BODY* pBody = *it;
        if (pBody == g_pSelfBody || pBody->wParentID != g_pSelfBody->wID)
            continue;
        if (CompareNoCase(pBody->pModule->szName, pszModule) == CSTR_EQUAL &&
            CompareNoCase(pBody->pMachine->szPublicIP, pszAddress) == CSTR_EQUAL)
        {
            return pBody;
        }
    }
    return NULL;
}

// Writes BYTE 0, then (BYTE 1, record) for each record, then BYTE 2.
static void WriteListBegin(CMassiveMsg* pReply)
{
    BYTE b = 0;
    pReply->Write(&b, 1);
}

static void WriteListRecord(CMassiveMsg* pReply, const void* pRec, int nSize)
{
    BYTE b = 1;
    pReply->Write(&b, 1);
    pReply->Write(pRec, nSize);
}

static void WriteListEnd(CMassiveMsg* pReply)
{
    BYTE b = 2;
    pReply->Write(&b, 1);
}

bool CertArch_OnCertificationRequest(CMsg* pMsg, CMassiveMsg* pReply)
{
    std::string strModule;
    std::string strAddress;
    *pMsg >> strModule >> strAddress;
    LogPrintf(0, "Certification request from : %s(%s)", strModule.c_str(), strAddress.c_str());

    SERVER_BODY* pBody = ServerBody_FindChildByModuleAndAddr(strModule.c_str(),
                                                             strAddress.c_str());
    BYTE bResult;
    if (!pBody)
    {
        bResult = 2;
        pReply->Write(&bResult, 1);
        LogPrintf(0x1000000, "cannot certify server body : [%s][%s]", strModule.c_str(),
                  strAddress.c_str());
        return false;
    }
    bResult = 1;
    pReply->Write(&bResult, 1);

    ServerBodySet setBody;
    ServerCordSet setCord;
    ServerBodyList lstBody;
    setBody.insert(pBody);
    setBody.insert(g_pSelfBody);
    lstBody.push_back(pBody);
    lstBody.push_back(g_pSelfBody);
    CertArch_CollectBodiesRecursive(pBody, &lstBody, &setBody, &setCord);

    if (pBody->pModule == CertModule_FindByName("GatewayServer"))
    {
        CertArch_CollectFarmManagerPeers(&lstBody, &setBody, &setCord);
        for (ServerBodyList::iterator it = g_pServerBodyList->begin();
             it != g_pServerBodyList->end(); ++it)
        {
            SERVER_BODY* p = *it;
            if (p->pModule != CertModule_FindByName("DownloadServer"))
                continue;
            if (setBody.find(p) == setBody.end())
            {
                lstBody.push_back(p);
                setBody.insert(p);
            }
        }
    }
    else if (pBody->pModule == CertModule_FindByName("AgentServer") ||
             (pBody->pShard && pBody->pShard->wManagerID == pBody->wID))
    {
        // The division's GlobalManager goes right after the requesting body.
        SERVER_BODY* pGM = ServerBodyInfo_Find(g_pSelfBody->pDivision->wGlobalManagerID);
        if (setBody.find(pGM) == setBody.end())
        {
            setBody.insert(pGM);
            lstBody.insert(++lstBody.begin(), pGM);
        }
        CertArch_CollectPeerBodies(pGM, &lstBody, &setBody, &setCord);
    }

    if (pBody->pShard)
    {
        for (ServerBodyList::iterator it = g_pServerBodyList->begin();
             it != g_pServerBodyList->end(); ++it)
        {
            SERVER_BODY* p = *it;
            if (p->pShard != pBody->pShard)
                continue;
            if (setBody.find(p) == setBody.end())
            {
                setBody.insert(p);
                lstBody.push_back(p);
                CertArch_CollectPeerBodies(p, &lstBody, &setBody, &setCord);
            }
        }
    }

    WriteListBegin(pReply);
    for (CertModuleList::iterator it = g_pCertModuleList->begin();
         it != g_pCertModuleList->end(); ++it)
    {
        WriteListRecord(pReply, *it, sizeof(CERT_MODULE));
    }
    WriteListEnd(pReply);

    WriteListBegin(pReply);
    for (CertContentList::iterator it = g_pCertContentList->begin();
         it != g_pCertContentList->end(); ++it)
    {
        WriteListRecord(pReply, *it, sizeof(CERT_CONTENT));
    }
    WriteListEnd(pReply);

    WriteListBegin(pReply);
    for (CertDivisionList::iterator it = g_pCertDivisionList->begin();
         it != g_pCertDivisionList->end(); ++it)
    {
        if ((*it)->byID == pBody->byDivisionID)
            WriteListRecord(pReply, *it, sizeof(CERT_DIVISION));
    }
    WriteListEnd(pReply);

    WriteListBegin(pReply);
    for (CertFarmList::iterator it = g_pCertFarmList->begin(); it != g_pCertFarmList->end();
         ++it)
    {
        if ((*it)->byDivisionID == pBody->byDivisionID)
            WriteListRecord(pReply, *it, sizeof(CERT_FARM));
    }
    WriteListEnd(pReply);

    WriteListBegin(pReply);
    for (CertFarmContentList::iterator it = g_pCertFarmContentList->begin();
         it != g_pCertFarmContentList->end(); ++it)
    {
        if ((*it)->pFarm->byDivisionID == pBody->byDivisionID)
            WriteListRecord(pReply, *it, sizeof(CERT_FARM_CONTENT));
    }
    WriteListEnd(pReply);

    WriteListBegin(pReply);
    for (CertShardList::iterator it = g_pCertShardList->begin(); it != g_pCertShardList->end();
         ++it)
    {
        if ((*it)->pFarm->byDivisionID == pBody->byDivisionID)
            WriteListRecord(pReply, *it, sizeof(CERT_SHARD));
    }
    WriteListEnd(pReply);

    WriteListBegin(pReply);
    {
        std::set<CERT_MACHINE*> setMachine;
        for (CertMachineList::iterator it = g_pCertMachineList->begin();
             it != g_pCertMachineList->end(); ++it)
        {
            if ((*it)->byDivisionID == pBody->byDivisionID)
            {
                WriteListRecord(pReply, *it, sizeof(CERT_MACHINE));
                setMachine.insert(*it);
            }
        }
        // This server's machine is always sent.
        if (setMachine.find(g_pSelfBody->pMachine) == setMachine.end())
            WriteListRecord(pReply, g_pSelfBody->pMachine, sizeof(CERT_MACHINE));
        WriteListEnd(pReply);
    }

    WriteListBegin(pReply);
    for (ServerBodyList::iterator it = lstBody.begin(); it != lstBody.end(); ++it)
        WriteListRecord(pReply, *it, sizeof(SERVER_BODY));
    WriteListEnd(pReply);

    WriteListBegin(pReply);
    for (ServerCordSet::iterator it = setCord.begin(); it != setCord.end(); ++it)
        WriteListRecord(pReply, *it, sizeof(SERVER_CORD));
    WriteListEnd(pReply);

    BYTE bCertData;
    if (g_pSelfBody->byDivisionID > 0)
    {
        bCertData = 1;
        pReply->Write(&bCertData, 1);
        CertData_SerializeRecordListsToMsg(pReply);
    }
    else
    {
        bCertData = 0;
        pReply->Write(&bCertData, 1);
    }
    return true;
}

template <class T>
static void FreeCertList(std::list<T*>*& pList)
{
    if (!pList)
        return;
    for (typename std::list<T*>::iterator it = pList->begin(); it != pList->end(); ++it)
        operator delete(*it);
    pList->clear();
    delete pList;
    pList = NULL;
}

void ServerApp_FreeCertArchData()
{
    FreeCertList(g_pCertContentList);
    FreeCertList(g_pCertDivisionList);
    FreeCertList(g_pCertFarmList);
    FreeCertList(g_pCertFarmContentList);
    FreeCertList(g_pCertShardList);
    FreeCertList(g_pCertMachineList);
    FreeCertList(g_pServerBodyList);
    FreeCertList(g_pServerCordList);
    FreeCertList(g_pCertModuleList);
    // g_pSelfBody keeps pointing into the freed body list.

    g_hmapServerRoute.clear();

    // BUG(FWDATA-001): the test is inverted; the nested lists are freed only when the map is
    // empty, so a filled map is neither freed nor cleared.
    if (g_hmapServerRoute2.size() == 0)
    {
        for (stdext::hash_map<WORD, ServerRouteListList*>::iterator it =
                 g_hmapServerRoute2.begin();
             it != g_hmapServerRoute2.end(); ++it)
        {
            ServerRouteListList* pOuter = it->second;
            if (!pOuter)
                continue;
            for (ServerRouteListList::iterator itInner = pOuter->begin();
                 itInner != pOuter->end(); ++itInner)
            {
                (*itInner)->clear();
                delete *itInner;
            }
            pOuter->clear();
            delete pOuter;
        }
        g_hmapServerRoute2.clear();
    }

    g_mapServerBody.clear();
    g_mapServerCord.clear();
    g_mapCertModuleByID.clear();
    g_mapCertModuleByName.clear();
    g_mapCertMachine.clear();
    g_mapSessionCord.clear();
    g_mapCertShard.clear();
    g_setNotifyBody.clear();
    g_setNotifySession.clear();
    ModuleVersion_FreeAllData();
}
