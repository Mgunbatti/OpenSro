#pragma once

#include <winsock2.h>
#include <windows.h>
#include <map>
#include <string>

#include "BSLib/ChunkAllocator.h"
#include "BSLib/Util/TimeSpan.h"

class CServiceManagerProcess;

// Failed user validations (login, IBUV and MOTP checks reported by the GatewayServers with
// 0x6111, CGlobalManagerMainProcess::OnMsg_6111). An account that fails a check as often as the
// check's tolerance gets an automated punishment (COverlapJob_AutomatedPunishmentJob through
// the service manager); 0x6112 forgets the failures of an account, and the history is cleared
// when the day of the week changes (main process timer, sub_40E010).

enum
{
    UV_CHECK_LOGIN  = 0,
    UV_CHECK_IBUV   = 1,
    UV_CHECK_MOTP   = 2,
    UV_CHECK_COUNT  = 3,
};

// UVFailureHistory (0x28 bytes, from g_UVFailureHistoryPool).
struct UVFailureHistory
{
    // sub_464600 (the pool's element constructor)
    UVFailureHistory()
    {
        bInUse = 0;
    }

    // vf00 (0x464670): back to g_UVFailureHistoryPool (only once, see bInUse).
    virtual void __stdcall Release();                               // 0

    std::string strUserID;      // +4
    DWORD       dwCount;        // +20  failures so far
    BYTE        bInUse;         // +24  set by UVFailureHistoryPool_Alloc
};

C_ASSERT(FIELD_OFFSET(UVFailureHistory, strUserID) == 4);
C_ASSERT(FIELD_OFFSET(UVFailureHistory, dwCount) == 0x20);
C_ASSERT(FIELD_OFFSET(UVFailureHistory, bInUse) == 0x24);
C_ASSERT(sizeof(UVFailureHistory) == 0x28);

// The settings of one check (0x40 bytes; Server.cfg "LoginFailureTolerance" ... of
// sub_40F2A0).
struct UV_PUNISHMENT_CONFIG
{
    // sub_4647F0 / sub_464850 (vector constructor / destructor of the array)
    DWORD       dwTolerance;    // +0   failures that are punished
    DWORD       dwBlockTimeMin; // +4
    std::string strGuide;       // +8
    std::string strDescription; // +24
};

C_ASSERT(FIELD_OFFSET(UV_PUNISHMENT_CONFIG, strGuide) == 8);
C_ASSERT(FIELD_OFFSET(UV_PUNISHMENT_CONFIG, strDescription) == 0x24);
C_ASSERT(sizeof(UV_PUNISHMENT_CONFIG) == 0x40);

typedef std::map<std::string, UVFailureHistory*> UVFailureHistoryMap;

// Sets g_pUserValidationHistoryMgr first (inlined into the dynamic initializer 0x4646B0).
class CUserValidationHistoryMgrSingleton
{
public:
    CUserValidationHistoryMgrSingleton();
};

// CUserValidationHistoryMgr (0x130 bytes, the static object 0x59B0E8).
class CUserValidationHistoryMgr : public CUserValidationHistoryMgrSingleton
{
public:
    // dynamic initializer 0x4646B0: punisher "AutomatedPunisher", every check tolerance 3,
    // 10 minutes, "PunishmentGuide" / "PunishmentDesc"; the day is the current one.
    CUserValidationHistoryMgr();
    // CUserValidationHistoryMgr::dtor (0x464930, deleting dtor 0x4647d0): releases the
    // history and clears g_pUserValidationHistoryMgr.
    virtual ~CUserValidationHistoryMgr();                           // 0

    // sub_464A50: the service manager of the automated punishments and the punisher's name.
    bool SetPunisher(CServiceManagerProcess* pSM, const char* pszName);
    // sub_4649F0: the settings of a check.
    void SetConfig(int nCheck, const char* pszGuide, const char* pszDescription,
                   DWORD dwTolerance, DWORD dwBlockTimeMin);
    // sub_464AC0 (0x6111): counts a failure of the account in the check; *pdwCount gets the
    // failures. When the tolerance is reached (not on the first failure) the account is
    // punished and its history of the check removed: true.
    bool RecordFailure(const std::string* pstrUserID, int nCheck, DWORD* pdwCount);
    // sub_464A80: the automated punishment of the account for the check.
    bool Punish(int nCheck, std::string* pstrUserID);
    // sub_464C90 (0x6112): removes the account from the history of every check.
    void Clear(const std::string* pstrUserID);
    // sub_464DB0 (timer): clears the history when the day of the week has changed.
    void CheckDay();
    // sub_464E10: releases every history and empties the maps.
    void ClearAll();

public:
    CServiceManagerProcess* m_pSM;                      // +4
    std::string             m_strPunisher;              // +8
    UV_PUNISHMENT_CONFIG    m_aConfig[UV_CHECK_COUNT];  // +24
    CDateTime               m_dtLastCheck;              // +E4
    UVFailureHistoryMap     m_aMap[UV_CHECK_COUNT];     // +10C
};

C_ASSERT(FIELD_OFFSET(CUserValidationHistoryMgr, m_pSM) == 4);
C_ASSERT(FIELD_OFFSET(CUserValidationHistoryMgr, m_strPunisher) == 8);
C_ASSERT(FIELD_OFFSET(CUserValidationHistoryMgr, m_aConfig) == 0x24);
C_ASSERT(FIELD_OFFSET(CUserValidationHistoryMgr, m_dtLastCheck) == 0xE4);
C_ASSERT(FIELD_OFFSET(CUserValidationHistoryMgr, m_aMap) == 0x10C);
C_ASSERT(sizeof(CUserValidationHistoryMgr) == 0x130);

// 0x59B088: the pool of the histories (500 per block, unnamed).
extern CChunkAllocatorST<UVFailureHistory> g_UVFailureHistoryPool;
// 0x59B0E8 and its pointer 0x59B968.
extern CUserValidationHistoryMgr g_UserValidationHistoryMgr;
extern CUserValidationHistoryMgr* g_pUserValidationHistoryMgr;

// sub_464660: a history from the pool, marked as in use.
UVFailureHistory* UVFailureHistoryPool_Alloc();
