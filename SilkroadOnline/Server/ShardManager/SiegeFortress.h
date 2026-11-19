#pragma once

#include <windows.h>

class CMsg;

// The siege fortress manager of SR_ShardManager: it only relays the fortress messages of a game
// server to the others and fills the siege times of the main process at start.

// CSiegeFortressMgr (8 bytes; static object 0x8A7B60, g_pSiegeFortressMgr = dword_8A8954)
class CSiegeFortressMgr
{
public:
    CSiegeFortressMgr();                                // CSiegeFortressMgr::StaticInit 0x668810
    virtual ~CSiegeFortressMgr();                       // 0x668880

    // CSiegeFortressMgr::InitSiegeScheduleTimes (0x668920, CShard_ScheduleManager::Init): the
    // previous siege begin, the next siege (begin + 30 minutes, end) and the next siege request
    // period into the main process's siege times.
    static void InitSiegeScheduleTimes();
    // CSiegeFortressMgr::OnMsg_385F (0x668e80, CMainProcess::OnMsg_385F): relays the message to
    // the game servers by its first byte.
    static void OnMsg_385F(CMsg* pMsg);

    DWORD           m_dw4;              // +4   not used
};

C_ASSERT(sizeof(CSiegeFortressMgr) == 8);

extern CSiegeFortressMgr* g_pSiegeFortressMgr;
extern CSiegeFortressMgr g_SiegeFortressMgr;
