#pragma once

#include <winsock2.h>
#include <windows.h>

#include "BSLib/thread/EventHandler.h"
#include "BSLib/thread/SynchObject.h"

// Minimal view of the socket classes (CBaseSocket, CSockStream, CSockDatagram), limited to
// what the proactors and peer processes read. Offsets are those of the originals. Replace this
// header with the real socket headers once they are ported.

class CBaseSocket;

// Head shared by every OVERLAPPED the engine issues: CMsg (m_Overlapped, m_dwIoType, m_dw24,
// m_dw28) and the blocks embedded in the sockets.
struct BS_OVERLAPPED
{
    OVERLAPPED  ov;             // +0
    DWORD       dwIoType;       // +14  3: the IOCP proactor closes the handler (reason 6)
    DWORD       dw18;           // +18
    DWORD       dwNotPooled;    // +1C  0: a pooled CMsg, handed back to the engine
};

C_ASSERT(sizeof(BS_OVERLAPPED) == 0x20);

// Overlapped block embedded in a socket (CSockStream +0x60 and +0xA4, CSockDatagram +0x50).
struct SOCK_OVERLAPPED : public BS_OVERLAPPED
{
    DWORD           dw20[8];    // +20
    CBaseSocket*    pOwner;     // +40
};

C_ASSERT(FIELD_OFFSET(SOCK_OVERLAPPED, pOwner) == 0x40);
C_ASSERT(sizeof(SOCK_OVERLAPPED) == 0x44);

class CBaseSocket : public CEventHandler
{
public:
    CBaseSocket()
    {
        m_hSocket = INVALID_SOCKET;
        m_nState = 0;
    }

    virtual ~CBaseSocket()
    {
        Close();
    }

    virtual int Close()                                             // 4
    {
        if (m_hSocket != INVALID_SOCKET)
        {
            closesocket(m_hSocket);
            m_hSocket = INVALID_SOCKET;
        }
        return 0;
    }

    // Overlapped block of the given kind (1: the one the peer process waits on).
    virtual SOCK_OVERLAPPED* GetOverlapped(int nType) = 0;          // 5

public:
    SOCKET              m_hSocket;      // +4
    DWORD               m_dw8;          // +8
    DWORD               m_dwC;          // +C
    CCriticalSectionBS  m_cs;           // +10
    int                 m_nState;       // +4C  2: the listener ignores closes of this socket
};

C_ASSERT(FIELD_OFFSET(CBaseSocket, m_hSocket) == 4);
C_ASSERT(FIELD_OFFSET(CBaseSocket, m_cs) == 0x10);
C_ASSERT(FIELD_OFFSET(CBaseSocket, m_nState) == 0x4C);

// CSockStream's two overlapped blocks, registered by CProactorEvent::AssociateHandle.
inline SOCK_OVERLAPPED* SockStream_GetOverlapped60(CBaseSocket* pSock)
{
    return (SOCK_OVERLAPPED*)((BYTE*)pSock + 0x60);
}

inline SOCK_OVERLAPPED* SockStream_GetOverlappedA4(CBaseSocket* pSock)
{
    return (SOCK_OVERLAPPED*)((BYTE*)pSock + 0xA4);
}

// CSockStream::GetOwnerField18_Locked (0x46e710): under the socket lock, the ID (+0x18) of the
// owning session (+0x13C), 0 without a session.
inline DWORD SockStream_GetOwnerSessionID(CBaseSocket* pSock)
{
    CSingleLockBS lock(&pSock->m_cs, TRUE);
    BYTE* pSession = *(BYTE**)((BYTE*)pSock + 0x13C);
    if (pSession)
        return *(DWORD*)(pSession + 0x18);
    return 0;
}
