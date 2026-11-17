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

// One login being certified: sCertifyParam (CertifyParam.h, the certification part), with the
// member names the main process uses.
#include "CertifyParam.h"

// IsPrivilegedIP (0x4100a0): the address (network order) is in the privileged address set.
BOOL IsPrivilegedIP(DWORD dwIP);
// CertParam_Release (0x414410): declared in CertifyParam.h.
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
