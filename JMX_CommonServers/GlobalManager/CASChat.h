#pragma once

#include <winsock2.h>
#include <windows.h>
#include <map>
#include <string>

#include "ServerFramework/OverlapJob.h"

namespace ServerFramework
{
class CServerProcessBase;
}

// The CAS chat sessions (a chat between a GM and a user, 0x6315 relayed to the user's body and
// 0x3417 sent to the user's session). Only the static manager is built at start and destroyed
// at exit: none of the functions that open, use or close a session (0x455B20..0x4560B0, and the
// session's constructor 0x4553A0 and send functions 0x4554A0..0x4557F0) is referenced anywhere
// in the executable, so they are not part of the rewrite.

// A chat session (0x34 bytes, operator new).
struct CASChatSession
{
    ServerFramework::CServerProcessBase* pProcess;  // +0   the manager's process (relays)
    DWORD       dwState;        // +4   2 created or closed, 0 open relayed, 1 open sent
    DWORD       dwChatID;       // +8   from the manager's ID pool
    DWORD       dwOwnerKey;     // +C   key of the manager's owner map, relay context
    WORD        wTargetBody;    // +10
    DWORD       dwSessionID;    // +14  the user's session
    std::string strText;        // +18
    // CASChatSession_dtor (0x455430) is the string's destruction.
};

C_ASSERT(FIELD_OFFSET(CASChatSession, wTargetBody) == 0x10);
C_ASSERT(FIELD_OFFSET(CASChatSession, strText) == 0x18);
C_ASSERT(sizeof(CASChatSession) == 0x34);

// The manager (0x48 bytes, the static object 0x59AFD0).
class CCASChatManager
{
public:
    // CASChatMgr_DynInit (0x455870): IDs 0..999.
    CCASChatManager();
    // CASChatMgr_AtExit (0x455990): deletes the sessions.
    ~CCASChatManager();

public:
    ServerFramework::COverlapJobIDPool      m_IDPool;       // +0
    ServerFramework::CServerProcessBase*    m_pProcess;     // +2C  never set
    std::map<DWORD, DWORD>                  m_mapOwner;     // +30  owner key -> chat ID
    std::map<DWORD, CASChatSession*>        m_mapSession;   // +3C  chat ID -> session
};

C_ASSERT(FIELD_OFFSET(CCASChatManager, m_pProcess) == 0x2C);
C_ASSERT(FIELD_OFFSET(CCASChatManager, m_mapOwner) == 0x30);
C_ASSERT(FIELD_OFFSET(CCASChatManager, m_mapSession) == 0x3C);
C_ASSERT(sizeof(CCASChatManager) == 0x48);

// 0x59AFD0
extern CCASChatManager g_CASChatManager;
