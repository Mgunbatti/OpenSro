#pragma once

#include <windows.h>
#include <vector>

#include "BSLib/ChunkAllocator.h"
#include "BSLib/IDPool.h"
#include "BSLib/thread/SynchObject.h"
#include "BSNet/Session.h"

class CSockStream;

// The engine's sessions (CNetEngine +0x28C): pooled CSession objects, a pool of session IDs and
// the ID -> session slot vector. Closed sessions go back to the pool and their IDs back to the
// ID pool.
class CSessionTable
{
public:
    CSessionTable();
    ~CSessionTable();

    // SessionTable_InitLocked: dwMaxSessions IDs (1..n) and as many empty slots. TRUE.
    BOOL Initialize(DWORD dwMaxSessions);
    // Closes (reason 4) and unlinks every session in a slot and empties the slot vector. The
    // sessions, their IDs and the pool counters are left alone.
    void CloseAll();
    // The session in slot dwID. IDs past the slot vector throw std::out_of_range.
    CSession* FindSession(DWORD dwID);

    // Takes a session and an ID, grows the slot vector when needed and binds the socket
    // (CSession::Bind). Returns the ID, 0 on failure.
    DWORD CreateNewSession(CSockStream* pSock, DWORD dwTaskID);
    // DisconnectSession: an accepted socket first gets a disconnect (the session is closed when
    // it completes, with reason 6); otherwise the session is released and closed. Always 1.
    int Close(DWORD dwSessionID, DWORD dwReason);
    // Maintainer job: closes the linked sessions that are no longer alive (idle longer than
    // dwTimeout, trusted sessions never). Returns the number of linked sessions.
    int ReapExpiredSessions(DWORD dwTimeout);
    // CleanupDisconnectedSessions: closes every linked untrusted session with an accepted
    // socket.
    void ReapClosedSessions();
    // Clears the message task of every linked session's socket (shutdown).
    void DisconnectAll();

    // NetStat_GetPoolCounters_A: session pool capacity, sessions in use and free.
    void GetCounters(DWORD* pdwTotal, DWORD* pdwUsed, DWORD* pdwFree);

public:
    CCriticalSectionBS              m_cs;               // +0
    CIDPool                         m_IDPool;           // +3C
    CChunkAllocatorST<CSession>     m_SessionPool;      // +5C
    std::vector<CSession*>          m_vecSessions;      // +9C  indexed by session ID
};

C_ASSERT(FIELD_OFFSET(CSessionTable, m_IDPool) == 0x3C);
C_ASSERT(FIELD_OFFSET(CSessionTable, m_SessionPool) == 0x5C);
#ifdef JMX_FRAMEWORK_2011
// AgentServer 0x46b080: the session pool is 0x5C bytes.
C_ASSERT(FIELD_OFFSET(CSessionTable, m_vecSessions) == 0xB8);
C_ASSERT(sizeof(CSessionTable) == 0xC8);
#else
C_ASSERT(FIELD_OFFSET(CSessionTable, m_vecSessions) == 0x9C);
C_ASSERT(sizeof(CSessionTable) == 0xAC);
#endif
