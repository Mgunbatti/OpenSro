#pragma once

#include <winsock2.h>
#include <windows.h>

#include "BSNet/IBSNet.h"

class CBaseSocket;
class CMsg;

// Minimal view of CNetEngine for the proactors, peer processes and the maintainer. Replace
// with the real CNetEngine header once it is ported (the globals then move there).

// The engine instance (0x4FAD50), set by the CNetEngine constructor.
extern IBSNet* g_pNetEngine;

// CNetEngine +0x338, published through the global pointer 0x53AD80 by
// NetEngine_RegisterGlobalInstance: the datagram socket CPeerProcessOverlapped sends from.
struct NETENGINE_DATAGRAM
{
    CBaseSocket*    pSocket;    // +0
    DWORD           dwParam;    // +4  set by NetEngine_RegisterPeerProcessClass
};

extern NETENGINE_DATAGRAM* g_pNetEngineDatagram;

// GetGlobal_53AD80 (0x49be70).
inline NETENGINE_DATAGRAM* NetEngine_GetDatagram()
{
    return g_pNetEngineDatagram;
}

// CNetEngine +0x114: the listener. When it exists, handler closes go through it.
inline CBaseSocket* NetEngine_GetListener(IBSNet* pEngine)
{
    return *(CBaseSocket**)((BYTE*)pEngine + 0x114);
}

// Engine slot 23 (+0x5C) takes (session ID, reason); IBSNet still declares it with the
// placeholder types (DWORD*, SOCKET).
inline int NetEngine_CloseSession(DWORD dwSessionID, DWORD dwReason)
{
    return g_pNetEngine->DisconnectSession((DWORD*)(DWORD_PTR)dwSessionID, (SOCKET)dwReason);
}

// Engine slot 19 (+0x4C): returns a CMsg to the pool.
inline void NetEngine_ReleaseMsg(LPOVERLAPPED pOverlapped)
{
    g_pNetEngine->ReleaseMsg((CMsg*)pOverlapped);
}

// Defined with CNetEngine:
// 0x481cb0: SessionMgr_ReapExpiredSessions(engine +0x28C, dwParam).
void NetEngine_ReapExpiredSessions(IBSNet* pEngine, DWORD dwParam);
// KeepAlive_TryConnect (0x481a30).
void NetEngine_KeepAliveTryConnect(IBSNet* pEngine);
