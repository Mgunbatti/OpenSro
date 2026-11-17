#pragma once

#include <windows.h>
#include <hash_map>
#include <hash_set>
#include <list>
#include <map>

#include "BSLib/thread/SynchObject.h"
#include "ServerFramework/MassiveMsg.h"

// Certification data sent with the 0xA003 architecture when the certification server belongs to
// a division: groups of privileged addresses and named settings. Guarded by g_csCertData.

#pragma pack(push, 1)

// Address group.
struct CERT_DATA_GROUP
{
    BYTE    byID;               // +0
    BYTE    ab1[288];           // +1
};
C_ASSERT(sizeof(CERT_DATA_GROUP) == 289);

// Named setting, e.g. "Ignore_Shard_MaxUser".
struct CERT_DATA_SETTING
{
    DWORD   dwValue;            // +0
    char    szName[320];        // +4
};
C_ASSERT(sizeof(CERT_DATA_SETTING) == 324);

// One address of a group.
struct CERT_DATA_GROUP_IP
{
    BYTE    byGroupID;          // +0
    DWORD   dwIP;               // +1
};
C_ASSERT(sizeof(CERT_DATA_GROUP_IP) == 5);

#pragma pack(pop)

typedef std::list<CERT_DATA_GROUP*>     CertDataGroupList;
typedef std::list<CERT_DATA_SETTING*>   CertDataSettingList;
typedef std::list<CERT_DATA_GROUP_IP*>  CertDataGroupIPList;
typedef stdext::hash_set<DWORD>         PrivIPSet;

extern CertDataGroupList*       g_pCertDataGroupList;       // 0x4F9CE8
extern CertDataSettingList*     g_pCertDataSettingList;     // 0x4F9CEC
extern CertDataGroupIPList*     g_pCertDataGroupIPList;     // 0x4F9CF0

extern stdext::hash_map<BYTE, PrivIPSet*>       g_hmapPrivIP;           // 0x53B934
extern std::map<DWORD, CERT_DATA_SETTING*>      g_mapCertDataSetting;   // 0x53B958
extern std::map<BYTE, CERT_DATA_GROUP*>         g_mapCertDataGroup;     // 0x53B964
extern CCriticalSectionBS                       g_csCertData;           // 0x53B970

// Frees the three lists, the maps and the address sets.
void CertData_FreeRecordListsAndIPMaps();
void PrivIPMap_DeleteIpRangeMap(PrivIPSet* pSet);
// Takes ownership of the lists and builds the address sets and the maps.
bool CertData_BuildPrivilegedIPMaps(CertDataGroupList* pGroups, CertDataSettingList* pSettings,
                                    CertDataGroupIPList* pGroupIPs);
// Each list as BYTE 0, (BYTE 1, record)..., BYTE 2.
bool CertData_SerializeRecordListsToMsg(ServerFramework::CMassiveMsg* pMsg);
bool CertData_ReadRecordLists(ServerFramework::CMassiveMsg* pMsg);
bool PrivIPMap_ContainsIP(BYTE byGroupID, DWORD dwIP);
// Value of the "Ignore_Shard_MaxUser" setting, 0 without one.
DWORD CertData_GetIgnoreShardMaxUserValue();
