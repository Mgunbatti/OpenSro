#pragma once

#include <winsock2.h>
#include <windows.h>
#include <sql.h>

// Functions of other parts of GlobalManager that the overlap jobs call (signatures from the
// binary). They are defined with the main process (CGlobalManagerMainProcess) and the
// punishment manager; the tests have stand-ins.

// sub_4129C0: CGlobalManagerMainProcess +0x80264, the time of the last daily play time reset
// (COverlapJob_ResetPlayTimeOfGlobalServer::OnComplete stores it the same way, inlined).
void GlobalManagerMain_SetLastPlayTimeReset(DWORD dwTime);

// 0x59B950: the punishment manager (created by the main process).
class CPunishmentManager;
extern CPunishmentManager* g_pPunishmentManager;

// sub_459400 (__stdcall): registers a punishment of the account (type 1 and 2 are refused) for
// the period; false when it was not added.
bool __stdcall PunishmentManager_Add(CPunishmentManager* pManager, DWORD dwJID, DWORD dwType,
                                     const TIMESTAMP_STRUCT* pBegin,
                                     const TIMESTAMP_STRUCT* pEnd);
