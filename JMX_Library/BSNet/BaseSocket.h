#pragma once

#include <winsock2.h>
#include <windows.h>

#include "BSLib/thread/EventHandler.h"
#include "BSLib/thread/SynchObject.h"

class CBaseSocket;

// Head shared by every OVERLAPPED the engine issues: CMsg (m_Overlapped, m_dwIoType, m_dw24,
// m_dw28) and the I/O blocks embedded in the sockets.
struct BS_OVERLAPPED
{
    OVERLAPPED  ov;             // +0
    DWORD       dwIoType;       // +14  SOCK_IO_*; 3: the IOCP proactor closes the handler
    DWORD       dw18;           // +18  0, 1 for a file block (second completion callback)
    DWORD       dwNotPooled;    // +1C  0: a pooled CMsg, handed back to the engine
};

C_ASSERT(sizeof(BS_OVERLAPPED) == 0x20);

// I/O types; a socket picks its completion callback with 2 * type + dw18.
enum
{
    SOCK_IO_ACCEPT      = 0,
    SOCK_IO_RECV        = 1,
    SOCK_IO_SEND        = 2,
    SOCK_IO_DISCONNECT  = 3,

    SOCK_IO_BUFFER_SIZE = 0x2000,
};

// Overlapped block embedded in a socket (CSockStream +0x60/+0xA4/+0xE8, CSockDatagram +0x50)
// with its 8 KB buffer. Receive blocks fill the buffer from nUsed on and consume from nRead;
// the send block is filled from the front and drained after each completed send.
struct SOCK_OVERLAPPED : public BS_OVERLAPPED
{
    DWORD           dwBufferCount;  // +20  always 1
    WSABUF          WsaBuf;         // +24
    DWORD           dw2C;           // +2C
    DWORD           dw30;           // +30
    BYTE*           pBuffer;        // +34  SOCK_IO_BUFFER_SIZE bytes
    int             nUsed;          // +38
    int             nRead;          // +3C
    CBaseSocket*    pOwner;         // +40

    // SockIoBuf_Init: everything zero except the type, the "not pooled" flag and the buffer
    // count.
    void Init(DWORD dwType)
    {
        pBuffer = NULL;
        nUsed = 0;
        nRead = 0;
        dwNotPooled = 1;
        pOwner = NULL;
        dwIoType = dwType;
        ov.Internal = 0;
        ov.InternalHigh = 0;
        ov.Offset = 0;
        ov.OffsetHigh = 0;
        ov.hEvent = NULL;
        dw18 = 0;
        dwBufferCount = 1;
    }

    // Closes the event and frees the buffer.
    void Release()
    {
        if (ov.hEvent)
        {
            WSACloseEvent(ov.hEvent);
            ov.hEvent = NULL;
        }
        if (pBuffer)
        {
            operator delete(pBuffer);
            pBuffer = NULL;
        }
    }

    void AllocBuffer()
    {
        if (pBuffer)
        {
            operator delete(pBuffer);
            pBuffer = NULL;
        }
        pBuffer = (BYTE*)operator new(SOCK_IO_BUFFER_SIZE);
    }

    // IOContext_Reset.
    void Reset()
    {
        dw18 = 0;
        if (ov.hEvent)
            WSAResetEvent(ov.hEvent);
        nUsed = 0;
        nRead = 0;
    }

    // IOContext_SetupWSABuf: the single WSABUF for the block's type.
    void SetupWSABuf()
    {
        switch (dwIoType)
        {
        case SOCK_IO_ACCEPT:
            WsaBuf.buf = (char*)pBuffer;
            WsaBuf.len = SOCK_IO_BUFFER_SIZE - 1;
            dwBufferCount = 1;
            break;
        case SOCK_IO_RECV:
            WsaBuf.len = SOCK_IO_BUFFER_SIZE - nUsed;
            WsaBuf.buf = (char*)pBuffer + nUsed;
            dwBufferCount = 1;
            break;
        case SOCK_IO_SEND:
            WsaBuf.len = nUsed;
            WsaBuf.buf = (char*)pBuffer;
            dwBufferCount = 1;
            break;
        }
    }

    // IOBuffer_Consume: a receive block gains nBytes, a send block drops its first nBytes.
    void Consume(int nBytes);

    // Buffer8K_GetWritePtr.
    BYTE* GetWritePtr(int* pnFree)
    {
        *pnFree = SOCK_IO_BUFFER_SIZE - nUsed;
        return pBuffer + nUsed;
    }

    // Obj_GetPair52_56.
    BYTE* GetBuffer(int* pnUsed)
    {
        *pnUsed = nUsed;
        return pBuffer;
    }

    // Obj_DiffField38_3C.
    int GetUnread() const
    {
        return nUsed - nRead;
    }
};

C_ASSERT(FIELD_OFFSET(SOCK_OVERLAPPED, WsaBuf) == 0x24);
C_ASSERT(FIELD_OFFSET(SOCK_OVERLAPPED, pBuffer) == 0x34);
C_ASSERT(FIELD_OFFSET(SOCK_OVERLAPPED, pOwner) == 0x40);
C_ASSERT(sizeof(SOCK_OVERLAPPED) == 0x44);

// An I/O block as a socket member: initialised with its type, released with the socket.
struct CSockIoBlock : public SOCK_OVERLAPPED
{
    explicit CSockIoBlock(DWORD dwType)
    {
        Init(dwType);
    }

    ~CSockIoBlock()
    {
        Release();
    }

private:
    CSockIoBlock(const CSockIoBlock&);
    CSockIoBlock& operator=(const CSockIoBlock&);
};

C_ASSERT(sizeof(CSockIoBlock) == 0x44);

// Kind of a socket object (+0x4C).
enum
{
    SOCK_KIND_NONE      = -1,
    SOCK_KIND_STREAM    = 0,
    SOCK_KIND_DATAGRAM  = 1,
    SOCK_KIND_LISTENER  = 2,
};

// Role of a stream socket (+0x8).
enum
{
    SOCK_ROLE_NONE      = -1,
    SOCK_ROLE_ACCEPTED  = 0,    // server side, created for AcceptEx / accept
    SOCK_ROLE_CONNECTED = 1,    // client side, created by connect
};

// Base of the socket handlers the proactors dispatch to.
class CBaseSocket : public CEventHandler
{
public:
    CBaseSocket();
    virtual ~CBaseSocket();                                         // 0

    virtual void OnIdle();                                          // 3
    // closesocket and invalidate the handle; returns 0.
    virtual int Close();                                            // 4

    // SockStream_CreateSocket: sets the role and creates an overlapped TCP socket unless one
    // exists. FALSE on failure.
    BOOL CreateSocket(int nRole);

    // getsockname_wrap: the local address of the socket (zeroed first).
    sockaddr_in* GetLocalAddr(sockaddr_in* pAddr);

public:
    SOCKET              m_hSocket;      // +4
    int                 m_nRole;        // +8   SOCK_ROLE_*
    BOOL                m_bGraceful;    // +C   stream close: shutdown(SD_SEND) instead of an
                                        //      abortive linger
    CCriticalSectionBS  m_cs;           // +10
    int                 m_nKind;        // +4C  SOCK_KIND_*; listeners ignore closes of kind 2
};

C_ASSERT(FIELD_OFFSET(CBaseSocket, m_hSocket) == 4);
C_ASSERT(FIELD_OFFSET(CBaseSocket, m_nRole) == 8);
C_ASSERT(FIELD_OFFSET(CBaseSocket, m_cs) == 0x10);
C_ASSERT(FIELD_OFFSET(CBaseSocket, m_nKind) == 0x4C);
C_ASSERT(sizeof(CBaseSocket) == 0x50);

// ReturnConst_1_16: an empty hook of the original (always TRUE). Kept where it is called.
inline BOOL Sock_Hook()
{
    return TRUE;
}
