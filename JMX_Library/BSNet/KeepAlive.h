#pragma once

#include <winsock2.h>
#include <windows.h>

// A connection the engine keeps up (CNetEngine keep-alive list). The maintainer reconnects every
// entry without a session (KeepAlive_TryConnect); closing a connected session clears the ID.
struct KEEPALIVE_ENTRY
{
    // KeepAliveEntry_Init (0x4801a0). Without a bind address only its sin_addr is cleared (the
    // rest of the bind block is left as it was).
    void Init(const sockaddr* pTo, const sockaddr* pBind, DWORD dwTaskIDArg, DWORD dwSessionIDArg)
    {
        To = *(const sockaddr_in*)pTo;
        if (pBind)
        {
            Bind = *(const sockaddr_in*)pBind;
            dwTaskID = dwTaskIDArg;
        }
        else
        {
            dwTaskID = dwTaskIDArg;
            Bind.sin_addr.s_addr = 0;
        }
        dwSessionID = dwSessionIDArg;
        nFailCount = 0;
    }

    sockaddr_in To;             // +0
    sockaddr_in Bind;           // +10  used when sin_addr is set
    DWORD       dwTaskID;       // +20
    DWORD       dwSessionID;    // +24  0: not connected
    int         nFailCount;     // +28  failed reconnects in a row; logged after 15
};

C_ASSERT(FIELD_OFFSET(KEEPALIVE_ENTRY, dwTaskID) == 0x20);
C_ASSERT(sizeof(KEEPALIVE_ENTRY) == 0x2C);
