#pragma once

#include <winsock2.h>
#include <windows.h>

#include "BSLib/ChunkAllocator.h"
#include "BSNet/IBSNet.h"

class CBaseSocket;
class CMsg;
class CSession;
class CSockDatagram;
class CSockListener;
class CSockStream;
class IFile;

// Minimal view of CNetEngine and CSession for the proactors, peer processes, maintainer and
// sockets. Replace with the real CNetEngine / CSession headers once they are ported (the
// globals then move there).

// The engine instance (0x4FAD50), set by the CNetEngine constructor.
extern IBSNet* g_pNetEngine;

// I/O model (0x4F8128) = OS_IsNotWinNT(), set by the CNetEngine constructor: 0 completion ports
// (NT), 1 event proactor (9x), -1 before the engine exists or when GetVersionEx failed.
extern int g_nNetIoModel;

// Set by CNetEngine::Shutdown (0x4FAD54); closing sockets then skip their state reset.
extern BOOL g_bNetEngineShutdown;

// Task ID of the datagram peer process (0x4F841C, 0x10000005).
extern DWORD g_dwNetDatagramTaskID;

// CNetEngine +0x338, published through the global pointer 0x53AD80 by
// NetEngine_RegisterGlobalInstance: the datagram socket and the task ID of its peer process.
struct NETENGINE_DATAGRAM
{
    CSockDatagram*  pSocket;    // +0
    DWORD           dwTaskID;   // +4  set by NetEngine_RegisterPeerProcessClass
};

extern NETENGINE_DATAGRAM* g_pNetEngineDatagram;

// GetGlobal_53AD80 (0x49be70).
inline NETENGINE_DATAGRAM* NetEngine_GetDatagram()
{
    return g_pNetEngineDatagram;
}

// Per stream role settings: an array of 36-byte blocks at CNetEngine +0x4C indexed by
// CBaseSocket::m_nRole (part of the init parameter copied to +8).
struct NETENGINE_SOCKET_CONFIG
{
    DWORD   dw0;                // +0
    DWORD   dw4;                // +4   1: the original calls an empty hook on connect/accept
    DWORD   dw8[4];             // +8
    LONG    nSendQueueLimit;    // +18  -1: unlimited
    DWORD   dwSessionParam;     // +1C  session parameter (accepted: +0x68, connected: +0x8C)
    DWORD   dwSendMode;         // +20  0: one WSASend per message, 1: queued and coalesced
};

C_ASSERT(sizeof(NETENGINE_SOCKET_CONFIG) == 36);

inline BYTE* NetEngine_Bytes(IBSNet* pEngine)
{
    return (BYTE*)pEngine;
}

// +0x8: SECURITY_MODE_* (CNetEngine::SetSecurityMode).
inline BYTE NetEngine_GetSecurityMode(IBSNet* pEngine)
{
    return *(NetEngine_Bytes(pEngine) + 0x8);
}

// +0x10: number of AcceptEx calls kept pending (at least 10 are used).
inline int NetEngine_GetAcceptCount(IBSNet* pEngine)
{
    return *(int*)(NetEngine_Bytes(pEngine) + 0x10);
}

// +0x14: an AcceptEx socket connected longer than this (seconds) is dropped; -1 disables the
// check.
inline DWORD NetEngine_GetMaxConnectTime(IBSNet* pEngine)
{
    return *(DWORD*)(NetEngine_Bytes(pEngine) + 0x14);
}

inline NETENGINE_SOCKET_CONFIG* NetEngine_GetSocketConfig(IBSNet* pEngine, int nRole)
{
    return (NETENGINE_SOCKET_CONFIG*)(NetEngine_Bytes(pEngine) + 0x4C) + nRole;
}

// +0x68 (= config[0].dwSessionParam): session parameter for accepted sockets.
inline DWORD NetEngine_GetAcceptSessionParam(IBSNet* pEngine)
{
    return *(DWORD*)(NetEngine_Bytes(pEngine) + 0x68);
}

// CNetEngine +0x114: the listener. When it exists, handler closes go through it.
inline CSockListener* NetEngine_GetListener(IBSNet* pEngine)
{
    return *(CSockListener**)(NetEngine_Bytes(pEngine) + 0x114);
}

// Socket pools: +0x118 connecting streams, +0x194 accepted streams, +0x210 datagrams.
inline CChunkAllocatorMT<CSockStream>* NetEngine_GetConnectPool(IBSNet* pEngine)
{
    return (CChunkAllocatorMT<CSockStream>*)(NetEngine_Bytes(pEngine) + 0x118);
}

inline CChunkAllocatorMT<CSockStream>* NetEngine_GetAcceptPool(IBSNet* pEngine)
{
    return (CChunkAllocatorMT<CSockStream>*)(NetEngine_Bytes(pEngine) + 0x194);
}

inline CChunkAllocatorMT<CSockDatagram>* NetEngine_GetDatagramPool(IBSNet* pEngine)
{
    return (CChunkAllocatorMT<CSockDatagram>*)(NetEngine_Bytes(pEngine) + 0x210);
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

// CSession fields the sockets read.
// +0x14: 1 = the session's packets bypass the security encoding (count, CRC, Blowfish).
inline DWORD Session_GetDword14(CSession* pSession)
{
    return *(DWORD*)((BYTE*)pSession + 0x14);
}

inline DWORD Session_GetID(CSession* pSession)
{
    return *(DWORD*)((BYTE*)pSession + 0x18);
}

// +0x24: the file being sent.
inline IFile* Session_GetWorkingFile(CSession* pSession)
{
    return *(IFile**)((BYTE*)pSession + 0x24);
}

// Defined with CNetEngine / CSession:
// 0x481cb0: SessionMgr_ReapExpiredSessions(engine +0x28C, dwParam).
void NetEngine_ReapExpiredSessions(IBSNet* pEngine, DWORD dwParam);
// KeepAlive_TryConnect (0x481a30).
void NetEngine_KeepAliveTryConnect(IBSNet* pEngine);
// SessionTable_CreateNewSession(engine +0x28C, pSock, dwParam) (0x4945e0): binds a new session
// to the socket (socket +0x13C), sets its task (CSockStream::SetTask), attaches the socket to
// its proactor (which starts receiving). Returns the session ID, 0 on failure.
DWORD NetEngine_CreateNewSession(IBSNet* pEngine, CSockStream* pSock, DWORD dwParam);
// Session_PostEvent_wrap (0x4924e0): tells the session's owner that the handshake completed.
void Session_PostHandshakeEvent(CSession* pSession);
// Session_HandleFileTransferData (0x470420): a received 0x1001 file block.
int Session_HandleFileTransferData(CMsg* pMsg, CSockStream* pSock);
// FileTransfer_AccumulateSent (0x492c40): adds dwBytes to the sent count; TRUE when the whole
// file was sent.
BOOL FileTransfer_AccumulateSent(CSession* pSession, DWORD dwBytes);
// FileTransfer_Finish (0x492ce0).
void FileTransfer_Finish(CSession* pSession, int nResult);
