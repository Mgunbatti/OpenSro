#pragma once

#include <windows.h>
#include <list>
#include <map>

#include "BSNet/Msg.h"
#include "ServerFramework/ServerBody.h"

// The shards the gateway offers to clients: per shard its server bodies, its AgentServers
// (logins are spread over them round-robin) and its service type from _ShardService.

enum
{
    SHARD_SERVICE_OPERATING     = 0,
    SHARD_SERVICE_EXAMINATION   = 1,    // shown, but as not ready to non-privileged clients
    SHARD_SERVICE_HIDDEN        = 2,    // shown to privileged clients only
};

// sShardCondition (0x28 bytes).
struct SHARD_CONDITION
{
    // ShardService_InitEntry (0x4122c0): the cursor starts at the end of the AgentServers.
    SHARD_CONDITION();
    // ShardService_DestroyEntry (0x412340)
    ~SHARD_CONDITION();

    CERT_SHARD*                 pShard;         // +0
    ServerBodyList              listAgent;      // +4   AgentServer bodies of the shard
    ServerBodyList              listBody;       // +10  every body of the shard
    BYTE                        byServiceType;  // +1C  SHARD_SERVICE_*
    ServerBodyList::iterator    itNextAgent;    // +20
};

C_ASSERT(FIELD_OFFSET(SHARD_CONDITION, listBody) == 0x10);
C_ASSERT(FIELD_OFFSET(SHARD_CONDITION, byServiceType) == 0x1C);
C_ASSERT(FIELD_OFFSET(SHARD_CONDITION, itNextAgent) == 0x20);
C_ASSERT(sizeof(SHARD_CONDITION) == 0x28);

// The shards by ID.
class CShardConditionManager : public std::map<WORD, SHARD_CONDITION*>
{
public:
    // DynInit_CShardConditionManager_53AD84 (0x412400)
    CShardConditionManager() {}
    // 0x4124e0: asserts the map was emptied (ShardServiceMap_DestroyAll).
    virtual ~CShardConditionManager();                                      // 0

    // ShardService_SetServiceType (0x412b60): DB row trigger of _ShardService {WORD shard,
    // BYTE type}. 1.
    int OnServiceTypeRow(DWORD dwQuery, DWORD dwUnused);
};

C_ASSERT(sizeof(CShardConditionManager) == 0x10);

// 0x53AD84
extern CShardConditionManager g_ShardConditionManager;

// ShardService_BuildMap (0x412550): an entry per certified shard; false (and the remaining
// shards left out) when there is no shard or a shard has no AgentServer.
bool ShardService_BuildMap();
// ShardServiceMap_DestroyAll (0x4127e0)
void ShardServiceMap_DestroyAll();
// ShardMap_RecomputeReadyFlags (0x412990): CERT_SHARD::b556 is 1 when every body of the shard is
// in state 5.
void ShardMap_RecomputeReadyFlags();
// ShardMap_Find (0x412c20): NULL for unknown shards.
SHARD_CONDITION* ShardMap_Find(WORD wShardID);
// ShardService_PickAgentServerRR (0x4128b0): the next AgentServer of the shard in state 5,
// NULL for unknown shards or when three rounds found none.
SERVER_BODY* ShardService_PickAgentServerRR(WORD wShardID);
// ShardList_WriteA101 (0x412c80): per shard (hidden ones only for privileged clients) BYTE 1,
// WORD ID, string name, WORD users, WORD user limit, BYTE ready (0 for examination shards and
// non-privileged clients), BYTE farm in some divisions; then BYTE 0.
void ShardList_WriteA101(CMsg* pMsg, BOOL bPrivileged);
// DB_RefreshShardServiceType (0x412a30): service types of _ShardService (one shard, 0: all).
// False when the query fails.
bool DB_RefreshShardServiceType(WORD wShardID);
