#include "ServerBody.h"

#include "BSNet/IBSNet.h"
#include "ServerDataExtern.h"

using ServerFramework::CMassiveMsg;

SERVER_BODY*                g_pSelfBody = NULL;
CertMachineList*            g_pCertMachineList = NULL;
CertContentList*            g_pCertContentList = NULL;
CertDivisionList*           g_pCertDivisionList = NULL;
CertFarmList*               g_pCertFarmList = NULL;
CertFarmContentList*        g_pCertFarmContentList = NULL;
CertShardList*              g_pCertShardList = NULL;
ServerBodyList*             g_pServerBodyList = NULL;
ServerCordList*             g_pServerCordList = NULL;
CertModuleList*             g_pCertModuleList = NULL;
IServerBodyStateObserver*   g_pServerBodyStateObserver = NULL;
HWND                        g_hServerArchView = NULL;

std::map<WORD, CERT_SHARD*>         g_mapCertShard;
std::map<BYTE, CERT_MODULE*>        g_mapCertModuleByID;
std::map<WORD, SERVER_BODY*>        g_mapServerBody;
std::map<DWORD, SERVER_CORD*>       g_mapServerCord;
std::map<DWORD, CERT_MACHINE*>      g_mapCertMachine;
std::map<std::string, CERT_MODULE*> g_mapCertModuleByName;
stdext::hash_map<WORD, ServerRouteListList*> g_hmapServerRoute2;
stdext::hash_map<WORD, SERVER_CORD*> g_hmapServerRoute;
std::map<DWORD, SERVER_CORD*>       g_mapSessionCord;
std::set<WORD>                      g_setNotifyBody;
std::set<DWORD>                     g_setNotifySession;

void ServerCord_CollectForBody(WORD wBodyID, ServerCordList* pList)
{
    for (ServerCordList::iterator it = g_pServerCordList->begin();
         it != g_pServerCordList->end(); ++it)
    {
        SERVER_CORD* pCord = *it;
        if (pCord->wToID == wBodyID || pCord->wFromID == wBodyID)
            pList->push_back(pCord);
    }
}

void ServerCord_CountForBody(WORD wBodyID, DWORD* pnCords, DWORD* pnConnected)
{
    *pnCords = 0;
    *pnConnected = 0;
    for (ServerCordList::iterator it = g_pServerCordList->begin();
         it != g_pServerCordList->end(); ++it)
    {
        SERVER_CORD* pCord = *it;
        if (pCord->wToID == wBodyID || pCord->wFromID == wBodyID)
        {
            ++*pnCords;
            if (pCord->dwSessionID)
                ++*pnConnected;
        }
    }
}

DWORD ServerCord_FindRouteRecursive(SERVER_BODY* pFrom, WORD wTargetID, DWORD dwSkipCordID,
                                    std::map<WORD, int>* pDepth, BOOL* pbFound)
{
    ServerCordList lstCords;
    ServerCord_CollectForBody(pFrom->wID, &lstCords);
    int nDepth = (*pDepth)[pFrom->wID];

    for (ServerCordList::iterator it = lstCords.begin(); it != lstCords.end(); ++it)
    {
        SERVER_CORD* pCord = *it;
        if (pCord->dwID == dwSkipCordID)
            continue;
        SERVER_BODY* pPeer = ServerCord_FindPeerBody(pCord, pFrom);
        if (!pPeer)
            continue;
        if (pPeer->wID == wTargetID)
        {
            (*pDepth)[pPeer->wID] = nDepth + 1;
            *pbFound = TRUE;
            return pCord->dwID;
        }
        if ((*pDepth)[pPeer->wID] == -1)
        {
            (*pDepth)[pPeer->wID] = nDepth + 1;
            ServerCord_FindRouteRecursive(pPeer, wTargetID, pCord->dwID, pDepth, pbFound);
            if (*pbFound)
                return pCord->dwID;
        }
    }
    return 0;
}

bool ServerBody_BuildRouteTable()
{
    if (g_hmapServerRoute2.size())
        BSAssertFail();
    if (g_hmapServerRoute.size())
        BSAssertFail();

    std::map<WORD, int> mapDepth;
    ServerBodyList::iterator it;
    for (it = g_pServerBodyList->begin(); it != g_pServerBodyList->end(); ++it)
        mapDepth.insert(std::make_pair((*it)->wID, -1));

    for (it = g_pServerBodyList->begin(); it != g_pServerBodyList->end(); ++it)
    {
        SERVER_BODY* pTarget = *it;
        if (pTarget == g_pSelfBody)
            continue;

        ServerCordList lstCords;
        ServerCord_CollectForBody(g_pSelfBody->wID, &lstCords);

        bool bDirect = false;
        for (ServerCordList::iterator itCord = lstCords.begin(); itCord != lstCords.end();
             ++itCord)
        {
            if (ServerCord_FindPeerBody(*itCord, g_pSelfBody) == pTarget)
            {
                g_hmapServerRoute.insert(std::make_pair(pTarget->wID, *itCord));
                bDirect = true;
                break;
            }
        }
        if (bDirect)
            continue;

        for (std::map<WORD, int>::iterator itDepth = mapDepth.begin();
             itDepth != mapDepth.end(); ++itDepth)
        {
            itDepth->second = -1;
        }
        mapDepth[g_pSelfBody->wID] = 1;
        BOOL bFound = FALSE;
        DWORD dwCordID = ServerCord_FindRouteRecursive(g_pSelfBody, pTarget->wID, 0, &mapDepth,
                                                       &bFound);
        if (dwCordID)
        {
            SERVER_CORD* pCord = ServerCordMap_FindById(dwCordID);
            if (!pCord)
                BSAssertFail();
            g_hmapServerRoute.insert(std::make_pair(pTarget->wID, pCord));
        }
    }
    return true;
}

SERVER_BODY* ServerBodyInfo_Find(WORD wBodyID)
{
    std::map<WORD, SERVER_BODY*>::iterator it = g_mapServerBody.find(wBodyID);
    if (it == g_mapServerBody.end())
        return NULL;
    return it->second;
}

SERVER_BODY* ServerCord_FindPeerBody(SERVER_CORD* pCord, SERVER_BODY* pBody)
{
    if (pCord->wFromID != pBody->wID && pCord->wToID != pBody->wID)
        BSAssertFail();
    WORD wPeerID = pCord->wFromID;
    if (wPeerID == pBody->wID)
        wPeerID = pCord->wToID;
    return ServerBodyInfo_Find(wPeerID);
}

SERVER_BODY* ServerBody_FindSelf(SERVER_CORD* pCord)
{
    return ServerCord_FindPeerBody(pCord, g_pSelfBody);
}

SERVER_CORD* ServerCordMap_FindById(DWORD dwCordID)
{
    std::map<DWORD, SERVER_CORD*>::iterator it = g_mapServerCord.find(dwCordID);
    if (it == g_mapServerCord.end())
        return NULL;
    return it->second;
}

SERVER_CORD* ServerBody_Find(WORD wBodyID)
{
    stdext::hash_map<WORD, SERVER_CORD*>::iterator it = g_hmapServerRoute.find(wBodyID);
    if (it == g_hmapServerRoute.end())
        return NULL;
    return it->second;
}

// 0x2005 body record: BYTE 1, BYTE 0, BYTE 1, WORD body ID, DWORD state, BYTE 2.
bool ServerBody_SetStateAndBroadcast2005(int nState, WORD wBodyID, DWORD dwExcludeSession)
{
    SERVER_BODY* pBody = ServerBodyInfo_Find(wBodyID);
    if (!pBody)
        return false;

    int nOldState = pBody->nState;
    BOOL bChanged = nOldState != nState;
    pBody->nState = nState;
    if (g_pServerBodyStateObserver)
        g_pServerBodyStateObserver->OnTrigger((DWORD)pBody, (DWORD)nOldState);
    if (g_hServerArchView)
        PostMessageA(g_hServerArchView, WM_SERVER_BODY_CHANGED, 0, (LPARAM)pBody);

    if (bChanged || g_pSelfBody->wID == wBodyID)
    {
        CMassiveMsg* pMassive = CMassiveMsg::Alloc(ServerFramework::MASSIVE_MODE_WRITE);
        pMassive->SetOpcode(SERVER_STATUS_OPCODE);
        BYTE b = 1;
        pMassive->Write(&b, 1);
        BYTE ab[2] = { 0, 1 };
        pMassive->Write(&ab[0], 1);
        pMassive->Write(&ab[1], 1);
        pMassive->Write(&pBody->wID, 2);
        pMassive->Write(&pBody->nState, 4);
        ab[1] = 2;
        pMassive->Write(&ab[1], 1);
        ServerBody_BroadcastToNotifySetsAndFree(pMassive, dwExcludeSession);
    }
    return true;
}

// 0x2005 cord record: BYTE 2, BYTE 0, BYTE 1, DWORD cord ID, DWORD status, BYTE 2.
bool ServerBody_UpdateStatusAndNotify(DWORD dwCordID, DWORD dwStatus, DWORD dwExcludeSession)
{
    SERVER_CORD* pCord = ServerCordMap_FindById(dwCordID);
    if (!pCord)
        return false;

    BOOL bChanged = pCord->dwStatus != dwStatus;
    pCord->dwStatus = dwStatus;
    if (g_hServerArchView)
        PostMessageA(g_hServerArchView, WM_SERVER_CORD_CHANGED, 0, (LPARAM)pCord);

    if (bChanged)
    {
        CMassiveMsg* pMassive = CMassiveMsg::Alloc(ServerFramework::MASSIVE_MODE_WRITE);
        pMassive->SetOpcode(SERVER_STATUS_OPCODE);
        BYTE b = 2;
        pMassive->Write(&b, 1);
        b = 0;
        pMassive->Write(&b, 1);
        b = 1;
        pMassive->Write(&b, 1);
        pMassive->Write(&pCord->dwID, 4);
        pMassive->Write(&pCord->dwStatus, 4);
        b = 2;
        pMassive->Write(&b, 1);
        ServerBody_BroadcastToNotifySetsAndFree(pMassive, dwExcludeSession);
    }
    return true;
}

void ServerBody_BroadcastToNotifySetsAndFree(CMassiveMsg* pMassive, DWORD dwExcludeSession)
{
    for (std::set<WORD>::iterator it = g_setNotifyBody.begin(); it != g_setNotifyBody.end();
         ++it)
    {
        SERVER_CORD* pRoute = ServerBody_Find(*it);
        if (!pRoute)
            continue;
        // BUG(FWDATA-002): a route without a session is not skipped; the parts go to session 0.
        if (pRoute->dwSessionID != dwExcludeSession)
            pMassive->SendTo(pRoute->dwSessionID, TRUE);
    }
    for (std::set<DWORD>::iterator it = g_setNotifySession.begin();
         it != g_setNotifySession.end(); ++it)
    {
        if (*it != dwExcludeSession)
            pMassive->SendTo(*it, TRUE);
    }
    pMassive->ReleaseMsgs();
    pMassive->Free();
}

bool ServerBody_SendMsg(WORD wBodyID, CMsg* pMsg)
{
    SERVER_CORD* pRoute = ServerBody_Find(wBodyID);
    if (!pRoute)
        return false;
    DWORD dwSessionID = pRoute->dwSessionID;
    if (!dwSessionID)
        return false;
    int nResult = g_pBSNet->SendMsg(dwSessionID, pMsg);
    g_pBSNet->ReleaseMsg(pMsg);
    return (nResult & 0x8000) == 0;
}

SERVER_CORD* ServerCord_OnEstablished(DWORD dwSessionID, const char* pszModuleName)
{
    if (!g_pSelfBody)
        return NULL;

    std::string strIP;
    u_short wPort;
    if (!g_pBSNet->GetSessionPeerAddress(dwSessionID, &strIP, NULL, &wPort))
        return NULL;

    // Collected but not used.
    ServerCordList lstCords;
    ServerCord_CollectForBody(g_pSelfBody->wID, &lstCords);

    // 1: this server opened the connection.
    bool bOutgoing = g_pBSNet->GetSessionSocketState(dwSessionID) == 1;

    for (ServerCordList::iterator it = g_pServerCordList->begin();
         it != g_pServerCordList->end(); ++it)
    {
        SERVER_CORD* pCord = *it;
        SERVER_BODY* pPeer;
        if (pCord->wFromID == g_pSelfBody->wID && bOutgoing)
        {
            pPeer = ServerBodyInfo_Find(pCord->wToID);
            if (!pPeer)
                continue;
            if (CompareNoCase(pPeer->pModule->szName, pszModuleName) != CSTR_EQUAL)
                continue;
            if (pPeer->wPort != wPort)
                continue;
        }
        else if (pCord->wToID == g_pSelfBody->wID && g_pSelfBody->nState >= 3 && !bOutgoing)
        {
            pPeer = ServerBodyInfo_Find(pCord->wFromID);
            if (!pPeer)
                continue;
            if (StrCmpI_Locale(pPeer->pModule->szName, pszModuleName) != 0)
                continue;
        }
        else
        {
            continue;
        }

        bool bAddressMatch = false;
        if (pCord->byPrivate == 1 &&
            StrCmpI_Locale(pPeer->pMachine->szPrivateIP, strIP.c_str()) == 0)
        {
            bAddressMatch = true;
        }
        else if (pCord->byPrivate == 0 &&
                 StrCmpI_Locale(pPeer->pMachine->szPublicIP, strIP.c_str()) == 0)
        {
            bAddressMatch = true;
        }
        if (!bAddressMatch)
            continue;

        if (pCord->dwSessionID && pCord->dwSessionID != dwSessionID)
        {
            LogPrintf(0x1000000, "server cord already established : discarding session: %d",
                      pCord->dwSessionID);
            g_pBSNet->DisconnectSession(pCord->dwSessionID, 1);
            std::map<DWORD, SERVER_CORD*>::iterator itSession =
                g_mapSessionCord.find(pCord->dwSessionID);
            if (itSession != g_mapSessionCord.end())
                g_mapSessionCord.erase(itSession);
            pCord->dwSessionID = 0;
        }
        if (pCord->dwSessionID != dwSessionID)
        {
            pCord->dwSessionID = dwSessionID;
            g_mapSessionCord.insert(std::make_pair(dwSessionID, pCord));
            LogPrintf(0, "server cord established : %d (%s:%d)", dwSessionID, strIP.c_str(),
                      (int)wPort);
            g_pBSNet->ExcludeSessionFromCleanup(dwSessionID);
        }
        return pCord;
    }
    return NULL;
}

SERVER_CORD* ServerCord_OnClosed(DWORD dwSessionID)
{
    std::map<DWORD, SERVER_CORD*>::iterator it = g_mapSessionCord.find(dwSessionID);
    if (it == g_mapSessionCord.end())
        return NULL;

    SERVER_CORD* pCord = it->second;
    pCord->dwSessionID = 0;
    ServerBody_UpdateStatusAndNotify(pCord->dwID, 0, 0);
    if (g_pSelfBody)
    {
        WORD wPeerID = pCord->wFromID;
        if (wPeerID == g_pSelfBody->wID)
            wPeerID = pCord->wToID;
        ServerBody_SetStateAndBroadcast2005(0, wPeerID, 0);
        {
            ServerCordList lstCords;
            ServerCord_CollectForBody(wPeerID, &lstCords);
            for (ServerCordList::iterator itCord = lstCords.begin(); itCord != lstCords.end();
                 ++itCord)
            {
                ServerBody_UpdateStatusAndNotify((*itCord)->dwID, 0, 0);
            }
        }
        g_mapSessionCord.erase(it);
        LogPrintf(0, "server cord closed : %d", dwSessionID);
    }
    return pCord;
}

bool ServerCord_ConnectOutgoingCords()
{
    ServerCordList lstCords;
    ServerCord_CollectForBody(g_pSelfBody->wID, &lstCords);
    for (ServerCordList::iterator it = lstCords.begin(); it != lstCords.end(); ++it)
    {
        SERVER_CORD* pCord = *it;
        if (pCord->wFromID != g_pSelfBody->wID || pCord->dwSessionID != 0)
            continue;
        SERVER_BODY* pPeer = ServerBodyInfo_Find(pCord->wToID);
        if (!pPeer)
        {
            LogPrintf(0x2000000, "invalid server cord : cannot find server body : %d",
                      pCord->wToID);
            continue;
        }
        if (pCord->byPrivate == 1)
        {
            g_pBSNet->AddKeepAliveTarget(pPeer->pMachine->szPrivateIP, pPeer->wPort,
                                         g_pSelfBody->pMachine->szPrivateIP, 0, 0);
        }
        else
        {
            g_pBSNet->AddKeepAliveTarget(pPeer->pMachine->szPublicIP, pPeer->wPort, NULL, 0,
                                         0);
        }
    }
    return false;
}
