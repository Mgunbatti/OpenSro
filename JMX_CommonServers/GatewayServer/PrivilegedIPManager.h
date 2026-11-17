#pragma once

#include <windows.h>
#include <hash_set>

// Address sets loaded from address ranges (rows of _PrivilegedIP). Privileged clients see hidden
// shards, may log in to shards in examination and as GM accounts.
class CPrivilegedIPManager
{
public:
    // 0x40fdf0
    CPrivilegedIPManager() {}
    // 0x40fec0
    virtual ~CPrivilegedIPManager();                            // 0

    // IpRange_AddToSet (0x40ff30): every address of the range {DWORD first, DWORD last} (both in
    // network order) and the log line "previleged ip : a.b.c.d - a.b.c.d".
    void AddRange(const DWORD* pRange);
    // The address (network order) is in the set.
    BOOL Contains(DWORD dwIP);

public:
    stdext::hash_set<DWORD> m_setIP;                            // +4
};

C_ASSERT(sizeof(CPrivilegedIPManager) == 0x2C);

// 0x53AE10: _PrivilegedIP. 0x53AE3C: a second set with its own row trigger
// (CGatewayServer::OnPrivilegedIPRow2) that no query uses.
extern CPrivilegedIPManager g_PrivilegedIPManager;
extern CPrivilegedIPManager g_PrivilegedIPManager2;

// IsPrivilegedIP (0x4100a0): the address (network order) is in g_PrivilegedIPManager.
BOOL IsPrivilegedIP(DWORD dwIP);
