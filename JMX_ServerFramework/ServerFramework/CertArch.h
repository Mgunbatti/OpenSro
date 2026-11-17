#pragma once

#include "ServerFramework/ServerBody.h"

// Certification architecture: parsing the 0xA003 reply of the certification server into the
// global lists and maps, answering 0x6003 certification requests (certification server side) and
// freeing everything at shutdown.

#ifndef SERVER_MODULE_NAME
#define SERVER_MODULE_NAME "GatewayServer"
#endif

typedef std::set<SERVER_BODY*>  ServerBodySet;
typedef std::set<SERVER_CORD*>  ServerCordSet;

// 0 when this module is the certification server (compares SERVER_MODULE_NAME).
int ServerName_CompareCertification();

CERT_MODULE* CertModule_FindByName(const char* pszName);
CERT_MODULE* CertModule_FindById(BYTE byModuleID);
CERT_MACHINE* CertMachine_FindById(DWORD dwMachineID);

// Stores the module list and indexes it by ID and lower-case name.
bool CertData_BuildServerNameMap(CertModuleList* pList);

// Takes ownership of the parsed lists, links the records, builds the maps, the route table and the
// set of bodies to notify. False when the architecture is unusable.
bool CertArch_BuildServerNotifyTargets(CertContentList* pContent, CertDivisionList* pDivision,
                                       CertFarmList* pFarm, CertFarmContentList* pFarmContent,
                                       CertShardList* pShard, CertMachineList* pMachine,
                                       ServerBodyList* pBody, ServerCordList* pCord);

// Parses the 0xA003 architecture (nine record lists, optional certification data).
bool CServerProcessMain_ParseA003CertArchAndBuildTargets(ServerFramework::CMassiveMsg* pMsg);

void CertArch_CollectPeerBodies(SERVER_BODY* pBody, ServerBodyList* pList,
                                ServerBodySet* pBodySet, ServerCordSet* pCordSet);
void CertArch_CollectBodiesRecursive(SERVER_BODY* pBody, ServerBodyList* pList,
                                     ServerBodySet* pBodySet, ServerCordSet* pCordSet);
void CertArch_CollectFarmManagerPeers(ServerBodyList* pList, ServerBodySet* pBodySet,
                                      ServerCordSet* pCordSet);
// Body certified by this server with the given module name and public address.
SERVER_BODY* ServerBody_FindChildByModuleAndAddr(const char* pszModule, const char* pszAddress);

// 0x6003 request (module name, public address): writes the part of the architecture the
// requesting body needs to pReply.
bool CertArch_OnCertificationRequest(CMsg* pMsg, ServerFramework::CMassiveMsg* pReply);

// Frees the architecture, all maps and the module version data.
void ServerApp_FreeCertArchData();
