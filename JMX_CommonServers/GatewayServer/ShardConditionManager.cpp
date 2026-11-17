#include "ShardConditionManager.h"

#include "BSLib/DB/DBQuery.h"
#include "GatewayServer.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/Trigger.h"

using namespace ServerFramework;

CShardConditionManager g_ShardConditionManager;

// ---------------------------------------------------------------------------------------------
// SHARD_CONDITION

SHARD_CONDITION::SHARD_CONDITION()
{
    itNextAgent = listAgent.end();
}

SHARD_CONDITION::~SHARD_CONDITION()
{
    listAgent.clear();
    listBody.clear();
}

// ---------------------------------------------------------------------------------------------
// CShardConditionManager

CShardConditionManager::~CShardConditionManager()
{
    if (size())
        BSAssertFail();
}

int CShardConditionManager::OnServiceTypeRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    BYTE* pRow = (BYTE*)pQuery->m_pBuffer;
    if (!pQuery->m_bRowFetched)
    {
        *(WORD*)pRow = 0;
        pRow[2] = 0;
        return 1;
    }
    iterator it = find(*(WORD*)pRow);
    if (it != end())
    {
        SHARD_CONDITION* pCondition = it->second;
        BYTE byType = pRow[2];
        pCondition->byServiceType = byType;
        const char* pszType = "Unknown";
        switch (byType)
        {
        case SHARD_SERVICE_OPERATING:   pszType = "Operating";   break;
        case SHARD_SERVICE_EXAMINATION: pszType = "Examination"; break;
        case SHARD_SERVICE_HIDDEN:      pszType = "Hidden";      break;
        }
        LogPrintf(0, "shard service type refreshed : [%d]%s -> %s", pCondition->pShard->wID,
                  (const char*)pCondition->pShard->ab3 + 1, pszType);
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------
// Shard map

// BUG(GW-002): a shard without an AgentServer ends the build; the shards after it are not
// offered and the ready flags are not computed (the caller ignores the result).
bool ShardService_BuildMap()
{
    CertShardList* pShards = g_pCertShardList;
    ServerBodyList* pBodies = g_pServerBodyList;
    if (!pShards->size())
    {
        LogPrintf(0x2000000, "there is no shard to service");
        return false;
    }
    CERT_MODULE* pAgentModule = CertModule_FindByName("AgentServer");
    for (CertShardList::iterator it = pShards->begin(); it != pShards->end(); ++it)
    {
        CERT_SHARD* pShard = *it;
        LogPrintf(0, "shard service : %d,%d", pShard->wID, pShard->ab3[0]);
        SHARD_CONDITION* pCondition = new SHARD_CONDITION;
        for (ServerBodyList::iterator itBody = pBodies->begin(); itBody != pBodies->end();
             ++itBody)
        {
            SERVER_BODY* pBody = *itBody;
            if (pBody->wShardID == pShard->wID)
                pCondition->listBody.push_back(pBody);
            if (pBody->pModule == pAgentModule && pBody->wShardID == pShard->wID)
                pCondition->listAgent.push_back(pBody);
        }
        if (!pCondition->listAgent.size())
        {
            delete pCondition;
            LogPrintf(0x2000000,
                      "there is unfurnished shard does not have any agent server : %d - %s",
                      pShard->wID, (const char*)pShard->ab3 + 1);
            return false;
        }
        pCondition->pShard = pShard;
        pCondition->byServiceType = SHARD_SERVICE_EXAMINATION;
        LogPrintf(0, "shard %s is ready for gateway service", (const char*)pShard->ab3 + 1);
        g_ShardConditionManager.insert(std::make_pair(pShard->wID, pCondition));
    }
    ShardMap_RecomputeReadyFlags();
    return true;
}

void ShardServiceMap_DestroyAll()
{
    for (CShardConditionManager::iterator it = g_ShardConditionManager.begin();
         it != g_ShardConditionManager.end(); ++it)
    {
        if (it->second)
            delete it->second;
    }
    g_ShardConditionManager.clear();
}

void ShardMap_RecomputeReadyFlags()
{
    for (CShardConditionManager::iterator it = g_ShardConditionManager.begin();
         it != g_ShardConditionManager.end(); ++it)
    {
        SHARD_CONDITION* pCondition = it->second;
        pCondition->pShard->b556 = 1;
        for (ServerBodyList::iterator itBody = pCondition->listBody.begin();
             itBody != pCondition->listBody.end(); ++itBody)
        {
            if ((*itBody)->nState != 5)
            {
                pCondition->pShard->b556 = 0;
                break;
            }
        }
    }
}

SHARD_CONDITION* ShardMap_Find(WORD wShardID)
{
    CShardConditionManager::iterator it = g_ShardConditionManager.find(wShardID);
    if (it == g_ShardConditionManager.end())
        return NULL;
    return it->second;
}

SERVER_BODY* ShardService_PickAgentServerRR(WORD wShardID)
{
    CShardConditionManager::iterator itShard = g_ShardConditionManager.find(wShardID);
    if (itShard == g_ShardConditionManager.end())
        return NULL;
    SHARD_CONDITION* pCondition = itShard->second;
    ServerBodyList& list = pCondition->listAgent;
    if (pCondition->itNextAgent == list.end())
        pCondition->itNextAgent = list.begin();
    ServerBodyList::iterator it = pCondition->itNextAgent;
    int nRounds = 0;
    do
    {
        ++it;
        if (it == list.end())
        {
            it = list.begin();
            ++nRounds;
        }
        SERVER_BODY* pBody = *it;
        if (pBody->nState == 5)
        {
            pCondition->itNextAgent = it;
            return pBody;
        }
    } while (nRounds < 3);
    return NULL;
}

void ShardList_WriteA101(CMsg* pMsg, BOOL bPrivileged)
{
    for (CShardConditionManager::iterator it = g_ShardConditionManager.begin();
         it != g_ShardConditionManager.end(); ++it)
    {
        SHARD_CONDITION* pCondition = it->second;
        if (pCondition->byServiceType == SHARD_SERVICE_HIDDEN && !bPrivileged)
            continue;
        BYTE byMore = 1;
        pMsg->WriteBytes(&byMore, 1);
        CERT_SHARD* pShard = pCondition->pShard;
        pMsg->WriteBytes(&pShard->wID, 2);
        *pMsg << (const char*)pShard->ab3 + 1;
        pMsg->WriteBytes(&pShard->w557, 2);
        pMsg->WriteBytes(&pShard->w548, 2);
        BYTE byNotReady = 0;
        const BYTE* pReady = &pShard->b556;
        if (*pReady == 1 && pCondition->byServiceType == SHARD_SERVICE_EXAMINATION && !bPrivileged)
            pReady = &byNotReady;
        pMsg->WriteBytes(pReady, 1);
        switch (g_pSelfBody->byDivisionID)
        {
        case 3: case 5: case 9: case 10: case 26: case 28: case 21: case 22: case 23: case 34:
            pMsg->WriteBytes(&pShard->byFarmID, 1);
            break;
        }
    }
    BYTE byEnd = 0;
    pMsg->WriteBytes(&byEnd, 1);
}

bool DB_RefreshShardServiceType(WORD wShardID)
{
    CClassTrigger<CShardConditionManager> trigger(&g_ShardConditionManager);
    trigger.m_pfnTrigger2 = &CShardConditionManager::OnServiceTypeRow;
    BYTE abRow[3];
    CDBQuery query;
    query.m_pSession = g_pGatewayDBSession;
    query.m_pTrigger = &trigger;
    query.m_pBuffer = abRow;
    query.m_nBufferSize = 3;
    query.m_nRowSize = 3;
    query.m_nSession = 0;
    int nResult;
    if (!wShardID)
        nResult = query.ExecuteFormat("select * from _ShardService");
    else
        nResult = query.ExecuteFormat("select * from _ShardService where ShardID = %d",
                                           wShardID);
    if (nResult < 0)
    {
        LogPrintf(0x2000000, "refresh shard service type failed!!!");
        return false;
    }
    return true;
}
