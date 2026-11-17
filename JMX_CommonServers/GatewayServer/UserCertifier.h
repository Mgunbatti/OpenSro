#pragma once

#include <winsock2.h>
#include <windows.h>

#include "BSLib/ChunkAllocator.h"
#include "ServerFramework/OverlapJob.h"
#include "CertifyParam.h"

// The multiprocess user certifier (D:\WORK2005\JMX_CommonServers\GatewayServer\UserCertifier.cpp).
//
// A login request (0x6102) becomes a CUserCertifier_OverlapJob that the main process hands to one
// of the 30 certifier processes (CServerProcessOverlap, round-robin). There Prepare checks the
// client session and Execute runs the certification handler of this server's division (a
// database or web query, CertifyDB.h). Back on the main process OnComplete (handler result 0)
// hands the login to an AgentServer of the shard through the GlobalManager (0x6200), OnFail
// reports a wrong password to the GlobalManager (result 1, 0x6111) or answers the client with
// 0xA102 {2, ...}.

// Results of the certification handlers (CUserCertifier_OverlapJob::m_nResult).
enum
{
    CERTIFY_RESULT_OK               = 0,
    CERTIFY_RESULT_WRONG_PASSWORD   = 1,    // counted by the GlobalManager (0x6111 {ID, 0})
    CERTIFY_RESULT_2                = 2,    // 0xA102 {2, 2, 2}
    CERTIFY_RESULT_ERROR            = 3,    // query failed, no handler; 0xA102 {2, 6}
    CERTIFY_RESULT_NO_ACCOUNT       = 4,    // 0xA102 {2, 2, 3}
    CERTIFY_RESULT_BLOCKED          = 5,    // 0xA102 {2, 2, 1, reason, end}
    CERTIFY_RESULT_WEB_FAILED       = 7,    // Joymax login CGI; 0xA102 {2, 6}
};

enum
{
    // Relayed to the GlobalManager: {DWORD client address, DWORD JID, BYTE content}; its answer
    // is 0xA203 (CGatewayMainProcess::OnMsg_A203).
    GATEWAY_MSG_LOGIN_GRANT_REQ     = 0x6200,
};

class CUserCertifier_OverlapJob : public ServerFramework::COverlapJob_Local
{
public:
    // 0x40d3a0
    CUserCertifier_OverlapJob()
    {
        m_pCertParam = NULL;
    }

    // 0x40d350: releases the ID and goes back to g_UserCertifierJobPool (jobs with an ID only).
    virtual void ReturnToPool();                                // 3
    // vf05 (0x40d650): UserCertifier_IsSessionStillValid.
    virtual bool Prepare();                                     // 5
    // RunCertify (0x40d6e0): the certification handler; true for CERTIFY_RESULT_OK.
    virtual bool Execute();                                     // 6
    // ForwardLoginToAgentServer (0x40d710)
    virtual bool OnComplete();                                  // 7
    // OnCertifyComplete (0x40daf0)
    virtual bool OnFail();                                      // 8

    // Setup (0x40d4a0): Init, then a new certification record for the session. False when the
    // client address cannot be read.
    bool Setup(CTask* pOwner, DWORD dwProcessIndex, DWORD dwSessionID, const char* pszID,
               const char* pszPassword, BYTE byContentID, WORD wShardID);
    // UserCertifier_IsSessionStillValid (0x40d660): the session is open and still has the
    // address of the request; a session with another address is closed.
    bool IsSessionStillValid();
    // UserCertifier_SendLoginFailure (0x40dc40)
    void SendLoginFailure();
    // The checks of OnComplete (inlined there).
    BYTE CheckAgentLogin(const BYTE** ppbyContent);

public:
    sCertifyParam*  m_pCertParam;       // +20
    DWORD           m_dwProcessIndex;   // +24  certifier process (0..29) = DB connection index
    WORD            m_wShardID;         // +28
    int             m_nResult;          // +2C  CERTIFY_RESULT_*, set by Execute
    DWORD           m_dwCertifyStart;   // +30
    DWORD           m_dwCertifyElapsed; // +34
};

C_ASSERT(FIELD_OFFSET(CUserCertifier_OverlapJob, m_pCertParam) == 0x20);
C_ASSERT(FIELD_OFFSET(CUserCertifier_OverlapJob, m_wShardID) == 0x28);
C_ASSERT(FIELD_OFFSET(CUserCertifier_OverlapJob, m_nResult) == 0x2C);
C_ASSERT(FIELD_OFFSET(CUserCertifier_OverlapJob, m_dwCertifyElapsed) == 0x34);
C_ASSERT(sizeof(CUserCertifier_OverlapJob) == 0x38);

// 0x53B318 and 0x53B358: the job and record pools (500 per block, single threaded: both are
// only used by the main process).
extern CChunkAllocatorST<CUserCertifier_OverlapJob> g_UserCertifierJobPool;
extern CChunkAllocatorST<sCertifyParam> g_CertifyParamPool;

// UserCertifier_AllocJob (0x40d330): a pooled job with an overlap job ID.
CUserCertifier_OverlapJob* UserCertifier_AllocJob();
// CUserCertifier_OverlapJob_AllocInit (0x4143b0): a pooled record, cleared.
sCertifyParam* CertParam_Alloc();
// CUserCertifier_OverlapJob_ClearData (0x40d2a0)
void CertParam_Clear(sCertifyParam* pParam);

// UserCertifier_InitProcessOverlapPool (0x40de10): registers and starts the 30 certifier
// processes (GATEWAY_TASK_CERTIFIER_FIRST + i, CServerProcessOverlap) and fills
// g_CertifierProcessPool. False when one cannot be started.
bool UserCertifier_InitProcessOverlapPool();
// UserCertifierMap_NextCyclic (0x40dee0): the next certifier process (wrapping) and its task ID.
CTask* UserCertifier_NextProcess(DWORD* pdwTaskID);
// UserCertifier_StartJob (0x40df50): certifies the login on the next certifier process; the
// answer goes to the main process of pOwnerTask. 0 when the job was sent, 3 otherwise.
int UserCertifier_StartJob(CTask* pOwnerTask, DWORD dwSessionID, const char* pszID,
                           const char* pszPassword, BYTE byContentID, WORD wShardID);
// UserCertifier_AddTimingStat (0x40e0b0): adds one certification time to g_CertifierProcessPool.
void UserCertifier_AddTimingStat(DWORD dwElapsed);
