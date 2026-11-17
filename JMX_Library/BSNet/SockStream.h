#pragma once

#include <winsock2.h>
#include <windows.h>

#include "BSNet/BaseSocket.h"
#include "BSNet/QueMT.h"
#include "BSNet/security/KeyExchange.h"
#include "BSNet/security/Security.h"

class CMsg;
class CSession;
class CTask;
class IFile;

// The callback slots hold plain code pointers in the original.
class __single_inheritance CSockStream;

// Error codes returned by the socket functions; bit 0x8000 makes the proactor close the socket.
enum
{
    SOCK_ERR_FAILED             = 0x8002,   // socket invalid / Winsock error
    SOCK_ERR_NO_DATA            = 0x8003,   // completion with 0 bytes (peer closed)
    SOCK_ERR_TOO_MANY           = 0x8005,   // WSAEWOULDBLOCK / ERROR_NOT_ENOUGH_MEMORY / WSAENOBUFS
    SOCK_ERR_NO_TASK            = 0x8007,   // receive without a message task
    SOCK_ERR_CLOSED             = 0x8008,   // completion on a closed socket
    SOCK_ERR_NULL               = 0x8009,   // no socket / no session
    SOCK_ERR_INVALID_FILE       = 0x800A,
    SOCK_ERR_BAD_COUNT          = 0x800B,
    SOCK_ERR_BAD_CRC            = 0x800C,
    SOCK_ERR_BAD_SIZE           = 0x800D,
    SOCK_ERR_ACCEPT             = 0x800E,
    SOCK_ERR_NO_FILE            = 0x800F,
    SOCK_ERR_HANDSHAKE          = 0x8010,   // also: send before the handshake completed
    SOCK_ERR_NOT_SUPPORTED      = 0x8011,   // encrypted packet on a session without Blowfish
    SOCK_ERR_NO_HANDLER         = 0x8013,
    SOCK_ERR_PEER_NOT_RESPONDING = 0x8015,  // send queue over the configured limit

    SOCK_SEND_NOT_PENDING       = 0x12,     // send completion without an outstanding send
    SOCK_SEND_BUFFER_EMPTY      = 6,        // PostWSASend on an empty send buffer
};

// Opcodes of the handshake.
enum
{
    SOCK_OP_FILE_DATA           = 0x1001,
    SOCK_OP_HANDSHAKE           = 0x5000,
    SOCK_OP_HANDSHAKE_ACCEPT    = 0x9000,
};

// TCP connection: framed CMsg packets, packet security and the handshake. The objects come from
// the engine's pools and are reused (CloseAndDrainQueue / ResetState).
//
// Receiving: WSARecv into the receive block; complete packets (6-byte header, size & 0x7FFF
// payload, encrypted packets padded to 8 bytes after the size word) are reassembled into pooled
// CMsg objects and passed to the message callback (+0x22C).
// Sending (send mode from the engine's per-role config): mode 0 issues one WSASend per CMsg;
// mode 1 queues the messages and copies them into the 8 KB send block, one WSASend at a time.
class CSockStream : public CBaseSocket
{
public:
    typedef int (CSockStream::*PFN_IO)(LPOVERLAPPED pOverlapped, DWORD dwBytes);
    typedef int (CSockStream::*PFN_MSG)(CMsg* pMsg);

    CSockStream();
    virtual ~CSockStream();                                                     // 0

    // AssociateHandle with the proactor, then the first WSARecv.
    virtual BOOL AttachToProactor(CServiceObjectSock* pProactor, HANDLE hHandle); // 1
    // Dispatches to the completion callback 2 * type + dw18 of the block.
    virtual int OnIOCompletion(DWORD dwBytes, LPOVERLAPPED pOverlapped);         // 2
    // Returns the queued messages, closes a connected socket (graceful or abortive) and resets
    // the object. 0x8009 for a NULL object.
    virtual int Close();                                                        // 4

    // Clears the counters, the I/O blocks and the security state and returns the current
    // send/receive messages to the engine.
    void ResetState();

    // Client side: optional bind, connect, events, callbacks; stores the peer address.
    BOOL Connect(const sockaddr* pBindAddr, const sockaddr* pAddr);
    // Server side: new socket, AcceptEx on hListen (completion port model) or events.
    BOOL PrepareAccept(SOCKET hListen);
    // Takes the accepted connection from hListen (accept() or the AcceptEx result) and stores
    // the peer address. 0 on success.
    int AcceptPeer(SOCKET hListen);
    // getsockopt(SO_CONNECT_TIME).
    BOOL GetConnectTime(DWORD* pdwSeconds);
    BOOL CreateEvents();
    void SetupHandlersAndQueue();

    // TransmitFile(TF_DISCONNECT | TF_REUSE_SOCKET) on the disconnect block. Returns 1 when
    // the disconnect was issued, 2/3 when the socket was closed instead (dwReason 2 on an
    // unbound or already closing socket).
    BYTE DisconnectReuse(DWORD dwReason);

    // Under the socket lock: the ID of the owning session, 0 without one.
    DWORD GetOwnerSessionID();
    void SetTask(CTask* pTask);
    void ClearCloseStateLocked();

    // Server side of the handshake: 0x5000 with the options, seeds and key exchange
    // parameters. TRUE when sent (and for client sockets, which do nothing).
    BOOL SendHandshakeRequest(SECURITY_HANDSHAKE_PARAM* pParam);
    // Sends through the installed send function once the handshake completed.
    int SendMsg(CMsg* pMsg);
    int SendFile(IFile* pFile, BOOL bSeek);

    // send functions (+0x230)
    int SendQueued(CMsg* pMsg);
    int SendQueuedWithLimit(CMsg* pMsg);

    // completion callbacks (+0x204)
    int OnMsgSent(LPOVERLAPPED pOverlapped, DWORD dwBytes);
    int FillSendBuffer(LPOVERLAPPED pOverlapped, DWORD dwBytes);
    int OnFileSent(LPOVERLAPPED pOverlapped, DWORD dwBytes);
    int ProcessRecvData(LPOVERLAPPED pOverlapped, DWORD dwBytes);

    // message callbacks (+0x22C)
    int OnMsgReceivedBeforeHandshake(CMsg* pMsg);
    int OnMsgReceivedAfterHandshake(CMsg* pMsg);

protected:
    int PrepareOutgoingMsg(CMsg** ppMsg);
    int EncodeOutgoingPacket(CMsg** ppMsg);
    int DecodeRecvPacket(WORD* pwSize);
    int PostWSASend(SOCK_OVERLAPPED* pOverlapped);
    int PostRecv();
    BOOL PeekSendQueue();
    void SetHandshaked();

public:
    sockaddr_in         m_PeerAddr;             // +50
    CSockIoBlock        m_ovSend;               // +60  SOCK_IO_SEND
    CSockIoBlock        m_ovRecv;               // +A4  SOCK_IO_RECV
    CSockIoBlock        m_ovDisconnect;         // +E8  SOCK_IO_DISCONNECT (TransmitFile)
    DWORD               m_dwLastRecvTick;       // +12C GetTickCount of the last receive
    DWORD               m_dwCloseState;         // +130 disconnect reason, 6 = no more sends
    CMsg*               m_pSendMsg;             // +134 message being copied to the send block
    CMsg*               m_pRecvMsg;             // +138 message being reassembled
    CSession*           m_pSession;             // +13C
    IQue<CMsg>*         m_pSendQueue;           // +140 send mode 1
    int                 m_nSendMsgOffset;       // +144
    int                 m_nRecvMsgOffset;       // +148
    volatile LONG       m_nPendingSends;        // +14C
    CTask*              m_pTask;                // +150 receives the messages after the handshake
    BOOL                m_bHandshaked;          // +154
    SECURITY_STATE      m_Security;             // +158
    CKeySender          m_KeySender;            // +170 server role
    CKeyRepicient       m_KeyRecipient;         // +1B8 client role
    // BUG(SOCK-010): never initialised by the constructor or ResetState.
    BYTE                m_bHandshakeState;      // +0x200 SECURITY_HS_* (server role)
    PFN_IO              m_apfnIo[10];           // +204 indexed by 2 * type + dw18
    PFN_MSG             m_pfnOnMsg;             // +22C
    PFN_MSG             m_pfnSend;              // +230
};

C_ASSERT(sizeof(CSockStream::PFN_IO) == 4);
C_ASSERT(FIELD_OFFSET(CSockStream, m_PeerAddr) == 0x50);
C_ASSERT(FIELD_OFFSET(CSockStream, m_ovSend) == 0x60);
C_ASSERT(FIELD_OFFSET(CSockStream, m_ovRecv) == 0xA4);
C_ASSERT(FIELD_OFFSET(CSockStream, m_ovDisconnect) == 0xE8);
C_ASSERT(FIELD_OFFSET(CSockStream, m_dwLastRecvTick) == 0x12C);
C_ASSERT(FIELD_OFFSET(CSockStream, m_pSession) == 0x13C);
C_ASSERT(FIELD_OFFSET(CSockStream, m_nPendingSends) == 0x14C);
C_ASSERT(FIELD_OFFSET(CSockStream, m_pTask) == 0x150);
C_ASSERT(FIELD_OFFSET(CSockStream, m_Security) == 0x158);
C_ASSERT(FIELD_OFFSET(CSockStream, m_KeySender) == 0x170);
C_ASSERT(FIELD_OFFSET(CSockStream, m_KeyRecipient) == 0x1B8);
C_ASSERT(FIELD_OFFSET(CSockStream, m_bHandshakeState) == 0x200);
C_ASSERT(FIELD_OFFSET(CSockStream, m_apfnIo) == 0x204);
C_ASSERT(FIELD_OFFSET(CSockStream, m_pfnOnMsg) == 0x22C);
C_ASSERT(FIELD_OFFSET(CSockStream, m_pfnSend) == 0x230);
C_ASSERT(sizeof(CSockStream) == 0x234);
