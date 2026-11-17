#pragma once

#include <windows.h>

#include "BSLib/ChunkAllocator.h"

class CBaseSocket;
class CSockDatagram;
class CSockStream;
class IBSNet;

// The engine's socket pools are CChunkAllocatorMT<CSockStream> (connecting and accepted
// streams, CNetEngine +0x118 / +0x194) and CChunkAllocatorMT<CSockDatagram> (+0x210), 500
// objects per block.
typedef CChunkAllocatorMT<CSockStream> CSockStreamPool;
typedef CChunkAllocatorMT<CSockDatagram> CSockDatagramPool;

// Close the socket (virtual Close) and return it to the engine's pool; NULL is ignored.
void Sock_ReleaseToConnectPool(CSockStream* pSock, IBSNet* pEngine);
void Sock_ReleaseToAcceptPool(CSockStream* pSock, IBSNet* pEngine);
void Sock_ReleaseToDatagramPool(CSockDatagram* pSock, IBSNet* pEngine);

// Sock_ReleaseToPool528AndClear: releases *ppSock to the datagram pool of the global engine
// (when there is one) and clears the pointer.
void Sock_ReleaseToDatagramPoolAndClear(CSockDatagram** ppSock);

// Session_CloseSocketByType: closes a session's socket; an accepted socket gets a new AcceptEx
// on the listener (back to the accept pool when that fails), a connecting socket goes back to
// the connect pool.
void SockPool_CloseSocket(CSockStream* pSock, IBSNet* pEngine);

// NetStat_GetPoolCounters_B: capacity, objects in use and free objects under the pool lock.
template <class T>
void SockPool_GetCounters(CChunkAllocatorMT<T>* pPool, DWORD* pdwTotal, DWORD* pdwUsed,
                          DWORD* pdwFree)
{
    CSingleLockBS lock(&pPool->m_cs, TRUE);
    *pdwTotal = pPool->m_nCapacity;
    DWORD dwUsed = pPool->m_nInUse;
    *pdwUsed = dwUsed;
    *pdwFree = *pdwTotal - dwUsed;
}
