#pragma once

#include <windows.h>
#include <string>

#include "BSLib/SingletonT.h"
#include "ServerFramework/ServerBody.h"

// What the main process and the application object of GatewayServer use from the
// certification part (user certifier, captcha "IBUV" processes, privileged addresses). That
// part provides the definitions; the shims below stand for the member functions named in their
// comments.

class CTask;
struct CRuntimeClass;

// One login being certified (sCertifyParam, 0x290 bytes, pooled). Passed as the context of the
// relays to the GlobalManager and the AgentServer and of the IBUV requests. Only the fields the
// main process reads are named.
struct CERTIFY_PARAM
{
    DWORD           dw0;            // +0    user number, sent to the AgentServer
    DWORD           dwType;         // +4    certification type (2 and 24 carry captcha data)
    std::string     strID;          // +8    user ID
    std::string     str24;          // +24   second login string, sent to the AgentServer
    BYTE            b40;            // +40
    BYTE            b41;            // +41
    BYTE            ab42[0x212];    // +42
    DWORD           dwSessionID;    // +254  client session
    DWORD           dw258;          // +258  sent first in 0x6203
    DWORD           dw25C;          // +25C
    SERVER_BODY*    pAgentBody;     // +260  AgentServer chosen for the login
    void*           pIBUVData;      // +264
    DWORD           dw268;          // +268
    WORD            w26C;           // +26C
    WORD            w26E;           // +26E
    DWORD           dw270;          // +270
    BYTE            ab274[0x1C];    // +274
};

C_ASSERT(FIELD_OFFSET(CERTIFY_PARAM, strID) == 0x8);
C_ASSERT(FIELD_OFFSET(CERTIFY_PARAM, str24) == 0x24);
C_ASSERT(FIELD_OFFSET(CERTIFY_PARAM, b40) == 0x40);
C_ASSERT(FIELD_OFFSET(CERTIFY_PARAM, dwSessionID) == 0x254);
C_ASSERT(FIELD_OFFSET(CERTIFY_PARAM, pAgentBody) == 0x260);
C_ASSERT(FIELD_OFFSET(CERTIFY_PARAM, w26C) == 0x26C);
C_ASSERT(FIELD_OFFSET(CERTIFY_PARAM, dw270) == 0x270);
C_ASSERT(sizeof(CERTIFY_PARAM) == 0x290);

// IsPrivilegedIP (0x4100a0): the address (network order) is in the privileged address set.
BOOL IsPrivilegedIP(DWORD dwIP);
// CertParam_Release (0x414410): frees the captcha data and returns the record to its pool;
// *ppParam becomes NULL.
void CertParam_Release(CERTIFY_PARAM** ppParam);
// RequestIBUVImage (0x407300): asks the IBUV process for a captcha image for the login (0x6321).
void RequestIBUVImage(CERTIFY_PARAM* pParam);
// UserCertifier_InitProcessOverlapPool (0x40de10): the 30 certifier processes
// (GATEWAY_TASK_CERTIFIER_FIRST ...) and the cursor of g_CertifierProcessPool.
bool UserCertifier_InitProcessOverlapPool();

// The captcha classes; singletons of BSLib's CSingletonT.
class CIBUVProcess;                 // 0x53BAF4
class CIBUVIPBlockMgr;              // 0x53BAF0
class CIBUVTemporalCertParamMgr;    // 0x53BAF8

// &CIBUVProcess::classCIBUVProcess (0x4d8f04): the process of task GATEWAY_TASK_IBUV.
CRuntimeClass* IBUVProcess_GetRuntimeClass();
// CIBUVProcess::Initialize (0x4196b0): image queue of dwQueueReserveCount images, refilled at
// fQueuePrepareRatio, codes of nStringSize characters of pszCharSet; answers go to pOwnerTask.
bool IBUVProcess_Initialize(CIBUVProcess* pProcess, CTask* pOwnerTask, DWORD dwQueueReserveCount,
                            float fQueuePrepareRatio, int nStringSize, const char* pszCharSet);
// The setter 0x417cd0 (CIBUVIPBlockMgr +4): failed captchas tolerated per address.
void IBUVIPBlockMgr_SetFailureIPTolerance(CIBUVIPBlockMgr* pMgr, DWORD dwTolerance);
// CIBUVTemporalCertParamMgr::Remove (0x41b9a0): forgets the pending captcha of the session.
void IBUVTemporalCertParamMgr_Remove(CIBUVTemporalCertParamMgr* pMgr, DWORD dwSessionID);

// The CGatewayServer row triggers of the start-up queries (CGatewayServer::OnModuleVersionRow
// 0x404da0, OnPrivilegedIPRow 0x404eb0, OnPrivilegedIPRow2 0x404ef0) are declared in
// GatewayServer.h and also belong to the certification part.
