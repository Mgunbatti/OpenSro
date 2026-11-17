#include "SockStream.h"

#include <string.h>

#include "BSLib/file/IFile.h"
#include "BSLib/thread/ServiceObject.h"
#include "BSLib/thread/Task.h"
#include "BSNet/Msg.h"
#include "BSNet/NetEngine.h"
#include "BSNet/NetEngineLog.h"
#include "BSNet/SockSystem.h"

namespace
{

// Clears the read position and hands a message back to the engine.
inline void ReleaseMsgToEngine(CMsg* pMsg)
{
    pMsg->m_wReadPos = pMsg->m_wWritePos;
    g_pNetEngine->ReleaseMsg(pMsg);
}

// Size of a packet on the wire: header + payload, encrypted packets padded to 8 bytes after
// the size word.
inline int WireSizeEncrypted(WORD wSizeField)
{
    return CBlowFish::AlignUp8((WORD)((wSizeField & MSG_SIZE_MASK) + MSG_HEADER_SIZE) - 2) + 2;
}

inline int WireSizePlain(WORD wSizeField)
{
    return (WORD)((wSizeField & MSG_SIZE_MASK) + MSG_HEADER_SIZE);
}

// Count and CRC bytes of a handshake packet the client sends.
inline void SignPacket(CMsg* pMsg, SECURITY_STATE* pSecurity)
{
    pMsg->SetCountByte(Security_NextCountByte(pSecurity));
    WORD wLen = (WORD)(pMsg->GetPayloadSize() + MSG_HEADER_SIZE);
    const BYTE* pData = pMsg->PtrAt(0);
    pMsg->SetCrcByte(Security_ComputeCrcByte(pData, &pSecurity->Crc, wLen));
}

} // namespace

CSockStream::CSockStream()
    : m_ovSend(SOCK_IO_SEND)
    , m_ovRecv(SOCK_IO_RECV)
    , m_ovDisconnect(SOCK_IO_DISCONNECT)
{
    m_nPendingSends = 0;
    // SocketHandle_Init: CRC seed 0, running value -1.
    m_Security.Crc.dwSeed = 0;
    m_Security.Crc.dwValue = 0xFFFFFFFF;
    m_Security.bCountAdd = 0;
    m_Security.bCountMul = 0;
    m_pSendMsg = NULL;
    m_pRecvMsg = NULL;
    m_pSendQueue = NULL;
    m_pSession = NULL;
    ResetState();
    m_dwCloseState = 0;
    m_ovSend.pOwner = this;
    m_ovRecv.pOwner = this;
    m_ovDisconnect.pOwner = this;
    m_ovRecv.AllocBuffer();
    m_ovSend.AllocBuffer();
    for (int i = 0; i < 10; ++i)
        m_apfnIo[i] = NULL;
    m_pfnOnMsg = NULL;
    m_pfnSend = NULL;
    m_nKind = SOCK_KIND_STREAM;
}

CSockStream::~CSockStream()
{
    Close();
    if (m_pSendQueue)
    {
        delete m_pSendQueue;
        m_pSendQueue = NULL;
    }
}

void CSockStream::ResetState()
{
    CSingleLockBS lock(&m_cs, TRUE);
    m_nSendMsgOffset = 0;
    m_nRecvMsgOffset = 0;
    m_ovSend.Reset();
    m_ovRecv.Reset();
    m_ovDisconnect.Reset();
    m_pTask = NULL;
    m_pSession = NULL;
    m_dwLastRecvTick = 0;
    m_nPendingSends = 0;
    m_dwCloseState = 0;
    m_bHandshaked = FALSE;
    m_Security.bOptions = 0;
    m_Security.bCountAdd = 0;
    m_Security.bCountMul = 0;
    m_Security.Crc.dwSeed = 0;
    m_Security.Crc.dwValue = 0xFFFFFFFF;
    memset(&m_PeerAddr, 0, sizeof(m_PeerAddr));
    if (m_pSendMsg)
    {
        ReleaseMsgToEngine(m_pSendMsg);
        m_pSendMsg = NULL;
    }
    if (m_pRecvMsg)
    {
        ReleaseMsgToEngine(m_pRecvMsg);
        m_pRecvMsg = NULL;
    }
}

BYTE CSockStream::DisconnectReuse(DWORD dwReason)
{
    CSingleLockBS lock(&m_cs, TRUE);
    BYTE bResult = 0;
    if (!m_pSession)
        bResult = 3;
    if (m_dwCloseState)
        bResult = 2;
    if (bResult && dwReason == 2)
    {
        CBaseSocket::Close();
        return bResult;
    }

    // BUG(NET-013): a socket that is already disconnecting (close state set) gets the new
    // reason and a second TransmitFile unless the reason is 2.
    m_dwCloseState = dwReason;
    // BUG(SOCK-004): TransmitFile returns a BOOL; the check for -1 never sees a failure, so a
    // failed disconnect leaves the socket open.
    if (g_pfnTransmitFile(m_hSocket, NULL, 0, 0, &m_ovDisconnect.ov, NULL,
                          TF_DISCONNECT | TF_REUSE_SOCKET) == -1
        && WSAGetLastError() != WSA_IO_PENDING)
    {
        CBaseSocket::Close();
    }
    return 1;
}

int CSockStream::Close()
{
    if (this == NULL)
        return SOCK_ERR_NULL;

    CSingleLockBS lock(&m_cs, TRUE);
    if (m_pSendQueue && g_pNetEngine)
    {
        CMsg* pMsg = NULL;
        m_pSendQueue->Pop(&pMsg, 0, TRUE);
        while (pMsg)
        {
            g_pNetEngine->ReleaseMsg(pMsg);
            pMsg = NULL;
            m_pSendQueue->Pop(&pMsg, 0, TRUE);
        }
    }

    if (m_nRole == SOCK_ROLE_CONNECTED)
    {
        if (m_hSocket != INVALID_SOCKET)
        {
            if (m_bGraceful)
            {
                Sock_Hook();
                shutdown(m_hSocket, SD_SEND);
                DWORD cbBytes;
                WSAIoctl(m_hSocket, FIONREAD, NULL, 0, NULL, 4, &cbBytes, NULL, NULL);
            }
            else
            {
                linger lg;
                lg.l_onoff = 1;
                lg.l_linger = 0;
                setsockopt(m_hSocket, SOL_SOCKET, SO_LINGER, (const char*)&lg, sizeof(lg));
            }
            closesocket(m_hSocket);
            m_hSocket = INVALID_SOCKET;
        }
        m_nRole = SOCK_ROLE_NONE;
    }

    if (!g_bNetEngineShutdown)
        ResetState();
    return 0;
}

void CSockStream::SetupHandlersAndQueue()
{
    int nIoModel = g_nNetIoModel;
    DWORD dwMode = NetEngine_GetSocketConfig(g_pNetEngine, m_nRole)->dwSendMode;
    if (nIoModel == 1)
        dwMode = 1;

    if (dwMode == 0)
    {
        m_pfnSend = &CSockStream::SendQueued;
        m_apfnIo[2 * SOCK_IO_SEND] = &CSockStream::OnMsgSent;
        m_apfnIo[2 * SOCK_IO_RECV] = &CSockStream::ProcessRecvData;
    }
    else if (dwMode == 1)
    {
        m_pfnSend = &CSockStream::SendQueuedWithLimit;
        m_apfnIo[2 * SOCK_IO_SEND] = &CSockStream::FillSendBuffer;
        m_apfnIo[2 * SOCK_IO_RECV] = &CSockStream::ProcessRecvData;
    }
    m_apfnIo[2 * SOCK_IO_SEND + 1] = &CSockStream::OnFileSent;
    m_apfnIo[2 * SOCK_IO_RECV + 1] = &CSockStream::ProcessRecvData;
    m_pfnOnMsg = &CSockStream::OnMsgReceivedBeforeHandshake;

    if (dwMode == 1 && !m_pSendQueue)
    {
        if (nIoModel)
            m_pSendQueue = new CQueMT9x<CMsg>;
        else
            m_pSendQueue = new CQueMT<CMsg>;
    }
}

// Socket_CreateEvents: the event proactor waits on the events of the send and receive blocks.
BOOL CSockStream::CreateEvents()
{
    if (g_nNetIoModel == 1)
    {
        if (!m_ovRecv.ov.hEvent)
        {
            m_ovRecv.ov.hEvent = WSACreateEvent();
            if (!m_ovRecv.ov.hEvent)
                return FALSE;
        }
        if (!m_ovSend.ov.hEvent)
        {
            m_ovSend.ov.hEvent = WSACreateEvent();
            if (!m_ovSend.ov.hEvent)
                return FALSE;
        }
    }
    return TRUE;
}

DWORD CSockStream::GetOwnerSessionID()
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (!m_pSession)
        return 0;
    return Session_GetID(m_pSession);
}

BOOL CSockStream::Connect(const sockaddr* pBindAddr, const sockaddr* pAddr)
{
    NETENGINE_SOCKET_CONFIG* pConfig = NetEngine_GetSocketConfig(g_pNetEngine, 1);
    if (!CreateSocket(SOCK_ROLE_CONNECTED))
        return FALSE;
    if (pBindAddr)
        bind(m_hSocket, pBindAddr, sizeof(sockaddr_in));
    if (connect(m_hSocket, pAddr, sizeof(sockaddr_in)) == SOCKET_ERROR)
        return FALSE;
    if (pConfig->dw4 == 1 && !Sock_Hook())
        return FALSE;
    if (!CreateEvents())
        return FALSE;

    SetupHandlersAndQueue();
    m_dwLastRecvTick = GetTickCount();
    m_PeerAddr = *(const sockaddr_in*)pAddr;
    return TRUE;
}

BOOL CSockStream::PrepareAccept(SOCKET hListen)
{
    if (!CreateSocket(SOCK_ROLE_ACCEPTED))
        return FALSE;

    if (g_nNetIoModel)
    {
        if (!CreateEvents())
            return FALSE;
    }
    else
    {
        DWORD dwReceived = 0;
        if (!g_pfnAcceptEx(hListen, m_hSocket, m_ovRecv.pBuffer, 0, 32, 32, &dwReceived,
                           &m_ovRecv.ov)
            && WSAGetLastError() != WSA_IO_PENDING)
        {
            CBaseSocket::Close();
            return FALSE;
        }
    }

    SetupHandlersAndQueue();
    m_dwLastRecvTick = GetTickCount();
    return TRUE;
}

int CSockStream::AcceptPeer(SOCKET hListen)
{
    if (hListen == INVALID_SOCKET)
        return SOCK_ERR_FAILED;

    if (g_nNetIoModel == 1)
    {
        m_hSocket = accept(hListen, NULL, NULL);
        if (m_hSocket == INVALID_SOCKET)
            return SOCK_ERR_FAILED;
        int nLen = sizeof(m_PeerAddr);
        getpeername(m_hSocket, (sockaddr*)&m_PeerAddr, &nLen);
    }
    else if (g_nNetIoModel == 0)
    {
        DWORD dwSeconds = 0;
        if (!GetConnectTime(&dwSeconds))
            return SOCK_ERR_ACCEPT;
        DWORD dwMax = NetEngine_GetMaxConnectTime(g_pNetEngine);
        if (dwMax != 0xFFFFFFFF && dwSeconds > dwMax)
        {
            m_bGraceful = FALSE;
            return 1;
        }

        sockaddr* pLocal = NULL;
        sockaddr* pRemote = NULL;
        int nLocalLen = 0;
        int nRemoteLen = 0;
        g_pfnGetAcceptExSockaddrs(m_ovRecv.pBuffer, 0, 32, 32, &pLocal, &nLocalLen, &pRemote,
                                  &nRemoteLen);
        memcpy(&m_PeerAddr, pRemote, sizeof(m_PeerAddr));
    }

    if (NetEngine_GetSocketConfig(g_pNetEngine, 0)->dw4 == 1)
        Sock_Hook();
    return 0;
}

BOOL CSockStream::AttachToProactor(CServiceObjectSock* pProactor, HANDLE hHandle)
{
    if (!pProactor || !pProactor->AssociateHandle(hHandle, this))
        return FALSE;
    return PostRecv() == 0;
}

BOOL CSockStream::GetConnectTime(DWORD* pdwSeconds)
{
    int nLen = 4;
    return getsockopt(m_hSocket, SOL_SOCKET, SO_CONNECT_TIME, (char*)pdwSeconds, &nLen) == 0;
}

BOOL CSockStream::SendHandshakeRequest(SECURITY_HANDSHAKE_PARAM* pParam)
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (m_nRole == SOCK_ROLE_CONNECTED)
        return TRUE;

    Security_ApplyHandshakeFlags(&m_Security, pParam);
    if (NetEngine_GetSecurityMode(g_pNetEngine) & SECURITY_MODE_KEY_EXCHANGE)
        pParam->bOptions |= SECURITY_OPT_KEY_EXCHANGE;

    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(SOCK_OP_HANDSHAKE);
    pMsg->WriteBytes(&pParam->bOptions, 1);
    if (pParam->bOptions & SECURITY_OPT_BLOWFISH)
        pMsg->WriteBytes(pParam->BlowFishKey, 8);
    if (pParam->bOptions & SECURITY_OPT_COUNT_CRC)
    {
        pMsg->WriteBytes(&pParam->dwCountSeed, 4);
        pMsg->WriteBytes(&pParam->dwCrcSeed, 4);
    }

    m_bHandshakeState = SECURITY_HS_BLOWFISH | SECURITY_HS_COUNT_CRC;
    if (NetEngine_GetSecurityMode(g_pNetEngine) & SECURITY_MODE_KEY_EXCHANGE)
    {
        KEYEXCHANGE_SERVER_PARAM param;
        m_KeySender.GenerateServerParams(&param);
        pMsg->WriteBytes(&param, sizeof(param));
        m_bHandshakeState |= SECURITY_HS_KEY_EXCHANGE;
    }

    int nResult = (this->*m_pfnSend)(pMsg);
    ReleaseMsgToEngine(pMsg);
    if (nResult)
        return FALSE;
    return TRUE;
}

// Applies count/CRC and Blowfish to a message about to be sent. Encryption writes the packet
// into a new pooled message that replaces *ppMsg (returns 1); the caller keeps its reference to
// the original.
int CSockStream::EncodeOutgoingPacket(CMsg** ppMsg)
{
    if (Session_GetDword14(m_pSession) == 1)
        return 0;

    CMsg* pMsg = *ppMsg;
    WORD wLen = (WORD)((*pMsg->m_pSize & MSG_SIZE_MASK) + MSG_HEADER_SIZE);
    int nResult = 0;
    BYTE* pData = pMsg->PtrAt(0);

    if (m_Security.bOptions & SECURITY_OPT_COUNT_CRC)
    {
        if (m_nRole == SOCK_ROLE_CONNECTED)
        {
            BYTE bCount = Security_NextCountByte(&m_Security);
            *(*ppMsg)->m_pCount = bCount;
            *(*ppMsg)->m_pCrc = 0;
            *(*ppMsg)->m_pCrc = Security_ComputeCrcByte(pData, &m_Security.Crc, wLen);
        }
        else
        {
            *(*ppMsg)->m_pCount = 0;
            *(*ppMsg)->m_pCrc = 0;
        }
    }

    BYTE bBlowFish = m_Security.bOptions & SECURITY_OPT_BLOWFISH;
    if (bBlowFish && (*(*ppMsg)->m_pSize & MSG_ENCRYPTED))
    {
        WORD wPlain = (WORD)((*(*ppMsg)->m_pSize & MSG_SIZE_MASK) + 4);
        WORD wPadded = (WORD)CBlowFish::AlignUp8(wPlain);
        if (wPadded + 2 > 0x1000)
        {
            // "How did this happen? How big is this message that encryption fails? Don't
            // encrypt things like this!"
            NetEngineLog(0x1000000,
                         "\xBE\xEE\xB6\xBB\xB0\xD4 \xB5\xC8\xB0\xC5\xB3\xC4? \xB5\xB5\xB4\xEB"
                         "\xC3\xBC \xBE\xF3\xB8\xB6\xB3\xAA \xC5\xAB \xB8\xDE\xBD\xC3\xC1\xF6"
                         "\xC0\xCC\xB1\xE6\xB7\xA1 encryption fail\xC0\xCC \xB3\xAA\xB4\xC2"
                         "\xB0\xC5\xB3\xC4? \xC0\xCC\xB7\xB1\xB0\xC7 \xBE\xCF\xC8\xA3\xC8\xAD "
                         "\xC7\xCF\xC1\xF6 \xB8\xBB\xB6\xF5\xB8\xBB\xBE\xDF! [msgid: 0x%x]",
                         (*ppMsg)->GetOpcode());
            *(*ppMsg)->m_pSize &= MSG_SIZE_MASK;
            return nResult;
        }

        CMsg* pEncrypted = g_pNetEngine->AllocMsg(TRUE);
        BYTE* pDst = pEncrypted->PtrAt(2);
        BYTE* pSrc = (*ppMsg)->PtrAt(2);
        m_Security.BlowFish.Encrypt(pSrc, pDst, wPlain);
        pEncrypted->SetPayloadSize((WORD)(wPlain - 4));

        WORD wTotal = (WORD)(wPadded + 2);
        if (wTotal >= MSG_HEADER_SIZE && wTotal <= pEncrypted->m_nCapacity)
            pEncrypted->m_wWritePos = wTotal;
        if (wTotal >= MSG_HEADER_SIZE && wTotal <= pEncrypted->m_nCapacity)
            pEncrypted->m_wReadPos = wTotal;
        *ppMsg = pEncrypted;
        return 1;
    }

    if (!bBlowFish && (*(*ppMsg)->m_pSize & MSG_ENCRYPTED))
    {
        // "Why do you keep asking to encrypt on a session that does not support encryption?"
        NetEngineLog(0x1000000,
                     "Encryption\xC0\xBB \xC1\xF6\xBF\xF8\xC7\xCF\xC1\xF6 \xBE\xCA\xB4\xC2 "
                     "\xBC\xBC\xBC\xC7\xC0\xCE\xB5\xA5 \xBF\xD6 \xC0\xDA\xB2\xD9\xB8\xB8 "
                     "\xBE\xCF\xC8\xA3\xC8\xAD \xC7\xCF\xB6\xF3\xB1\xB8 \xB1\xD7\xB7\xAF"
                     "\xB4\xC2 \xB0\xC5\xB3\xC4 \xB4\xEB\xC3\xBC! [msgid: 0x%x]",
                     (*ppMsg)->GetOpcode());
        *(*ppMsg)->m_pSize &= MSG_SIZE_MASK;
    }
    return nResult;
}

int CSockStream::DecodeRecvPacket(WORD* pwSize)
{
    CMsg* pMsg = m_pRecvMsg;
    WORD wSizeField = *pMsg->m_pSize;
    *pwSize = wSizeField & MSG_SIZE_MASK;
    if (Session_GetDword14(m_pSession) == 1)
        return 0;

    WORD wEncrypted = wSizeField >> 15;
    if (!wEncrypted && pMsg->GetOpcode() == SOCK_OP_FILE_DATA)
        return 0;

    if (wEncrypted)
    {
        if (!(m_Security.bOptions & SECURITY_OPT_BLOWFISH))
            return SOCK_ERR_NOT_SUPPORTED;
        DWORD dwPadded = CBlowFish::AlignUp8((WORD)((wSizeField & MSG_SIZE_MASK) + 6) - 2);
        if (dwPadded + 2 > 0x1000)
            return SOCK_ERR_BAD_SIZE;
        BYTE* pData = pMsg->PtrAt(2);
        m_Security.BlowFish.Decrypt(pData, pData, dwPadded);
    }

    if (!(m_Security.bOptions & SECURITY_OPT_COUNT_CRC))
        return 0;

    if (m_nRole == SOCK_ROLE_ACCEPTED)
    {
        BYTE bCount = *m_pRecvMsg->m_pCount;
        if (bCount != Security_NextCountByte(&m_Security))
            return SOCK_ERR_BAD_COUNT;

        BYTE* pData = m_pRecvMsg->PtrAt(0);
        BYTE bCrc = *m_pRecvMsg->m_pCrc;
        *m_pRecvMsg->m_pCrc = 0;
        WORD wLen = (WORD)((*m_pRecvMsg->m_pSize & MSG_SIZE_MASK) + MSG_HEADER_SIZE);
        if (bCrc != Security_ComputeCrcByte(pData, &m_Security.Crc, wLen))
            return SOCK_ERR_BAD_CRC;
        return 0;
    }

    // Packets to the client carry no count/CRC.
    if (*m_pRecvMsg->m_pCount)
        return SOCK_ERR_BAD_COUNT;
    if (*m_pRecvMsg->m_pCrc)
        return SOCK_ERR_BAD_CRC;
    return 0;
}

int CSockStream::SendMsg(CMsg* pMsg)
{
    if (this == NULL)
        return SOCK_ERR_NULL;
    if (m_Security.bOptions && m_bHandshaked)
        return (this->*m_pfnSend)(pMsg);
    return SOCK_ERR_HANDSHAKE;
}

int CSockStream::PrepareOutgoingMsg(CMsg** ppMsg)
{
    if (m_dwCloseState == 6 || m_hSocket == INVALID_SOCKET)
        return SOCK_ERR_FAILED;

    int nEncoded = 0;
    if (m_bHandshaked == TRUE)
        nEncoded = EncodeOutgoingPacket(ppMsg);
    (*ppMsg)->m_dw24 = 0;
    if (!nEncoded)
        InterlockedIncrement(&(*ppMsg)->m_lRefCount);
    return 0;
}

int CSockStream::SendQueued(CMsg* pMsg)
{
    CSingleLockBS lock(&m_cs, TRUE);
    int nResult = PrepareOutgoingMsg(&pMsg);
    if (!nResult)
        nResult = PostWSASend((SOCK_OVERLAPPED*)pMsg);
    return nResult;
}

int CSockStream::SendQueuedWithLimit(CMsg* pMsg)
{
    CSingleLockBS lock(&m_cs, TRUE);
    int nResult = PrepareOutgoingMsg(&pMsg);
    if (nResult)
        return nResult;

    NETENGINE_SOCKET_CONFIG* pConfig = NetEngine_GetSocketConfig(g_pNetEngine, m_nRole);
    if (pConfig->nSendQueueLimit != -1)
    {
        LONG nLimit = pConfig->nSendQueueLimit;
        if ((DWORD)m_pSendQueue->GetSize() > (DWORD)nLimit)
        {
            // BUG(SOCK-003): the reference taken by PrepareOutgoingMsg (or the encrypted copy)
            // is never released, so the message never returns to the pool.
            NetEngineLog(0x1000000, "NERR_PEER_DOESNT_RESPONSE - %d:%d", m_nRole, nLimit);
            return SOCK_ERR_PEER_NOT_RESPONDING;
        }
    }

    m_pSendQueue->Push(pMsg);
    if (m_ovSend.nUsed == m_ovSend.nRead)
        nResult = (this->*m_apfnIo[2 * SOCK_IO_SEND])(&m_ovSend.ov, 0);
    return nResult;
}

// SockStream_PostWSASend: a pooled CMsg is sent as it is, the send block only when it holds
// data.
int CSockStream::PostWSASend(SOCK_OVERLAPPED* pOverlapped)
{
    switch (pOverlapped->dwNotPooled)
    {
    case 0:
        pOverlapped->WsaBuf.len = ((CMsg*)pOverlapped)->m_wWritePos;
        break;
    case 1:
        if (m_ovSend.nUsed == m_ovSend.nRead)
            return SOCK_SEND_BUFFER_EMPTY;
        m_ovSend.SetupWSABuf();
        break;
    }

    int nResult = 0;
    InterlockedIncrement(&m_nPendingSends);
    DWORD dwSent = 0;
    if (WSASend(m_hSocket, &pOverlapped->WsaBuf, pOverlapped->dwBufferCount, &dwSent, 0,
                &pOverlapped->ov, NULL) == SOCKET_ERROR)
    {
        int nError = WSAGetLastError();
        if (nError == WSA_IO_PENDING)
            return nResult;
        if (nError != WSAEWOULDBLOCK && nError != ERROR_NOT_ENOUGH_MEMORY
            && nError != WSAENOBUFS)
        {
            InterlockedDecrement(&m_nPendingSends);
            return SOCK_ERR_FAILED;
        }
        NetEngineLog(0, "WSASend() - NERR_TOO_MANY_OUTSTANDING: %d", nError);
        nResult = SOCK_ERR_TOO_MANY;
        InterlockedDecrement(&m_nPendingSends);
    }
    return nResult;
}

int CSockStream::SendFile(IFile* pFile, BOOL bSeek)
{
    if (this == NULL)
        return SOCK_ERR_NULL;

    CSingleLockBS lock(&m_cs, TRUE);
    if (pFile && pFile->IsOpen())
    {
        if (bSeek)
            pFile->Seek(GetOwnerSessionID());
        return OnFileSent(NULL, 0);
    }

    // "... is not a valid file!"
    NetEngineLog(0x2000000, "CSockStream::SendFile() - Valid File \xC0\xCC \xBE\xC6\xB4\xCF"
                            "\xB4\xD9!");
    return SOCK_ERR_INVALID_FILE;
}

int CSockStream::OnIOCompletion(DWORD dwBytes, LPOVERLAPPED pOverlapped)
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (m_dwCloseState)
        return 0;
    if (!dwBytes)
        return SOCK_ERR_NO_DATA;
    if (m_hSocket == INVALID_SOCKET)
        return SOCK_ERR_CLOSED;

    BS_OVERLAPPED* pIo = (BS_OVERLAPPED*)pOverlapped;
    if (pIo->dwIoType == SOCK_IO_SEND)
    {
        LONG nPending = 0;
        InterlockedExchange(&nPending, m_nPendingSends);
        if (!nPending)
        {
            lock.Unlock();
            return SOCK_SEND_NOT_PENDING;
        }
        InterlockedDecrement(&m_nPendingSends);
    }

    int nResult = (this->*m_apfnIo[2 * pIo->dwIoType + pIo->dw18])(pOverlapped, dwBytes);
    lock.Unlock();
    return nResult;
}

BOOL CSockStream::PeekSendQueue()
{
    if (m_pSendMsg)
        return TRUE;
    if (m_pSendQueue->Pop(&m_pSendMsg, 0, FALSE) && m_pSendMsg)
        return TRUE;
    return FALSE;
}

int CSockStream::OnMsgSent(LPOVERLAPPED pOverlapped, DWORD)
{
    g_pNetEngine->ReleaseMsg((CMsg*)pOverlapped);
    return 0;
}

int CSockStream::FillSendBuffer(LPOVERLAPPED, DWORD dwBytes)
{
    if (dwBytes)
        m_ovSend.Consume(dwBytes);

    int nNeed = 0;
    while (PeekSendQueue())
    {
        CMsg* pMsg = m_pSendMsg;
        WORD wSizeField = *pMsg->m_pSize;
        if (wSizeField & MSG_ENCRYPTED)
            nNeed = WireSizeEncrypted(wSizeField) - m_nSendMsgOffset;
        else if (pMsg)
            nNeed = WireSizePlain(wSizeField) - m_nSendMsgOffset;

        const BYTE* pSrc = pMsg->PtrAt(m_nSendMsgOffset);
        int nCopy = nNeed;
        if (nNeed)
        {
            int nOver = m_ovSend.nUsed + nNeed - SOCK_IO_BUFFER_SIZE;
            if (nOver > 0)
                nCopy = nNeed - nOver;
            if (nCopy > 0)
            {
                memcpy(m_ovSend.pBuffer + m_ovSend.nUsed, pSrc, nCopy);
                m_ovSend.nUsed += nCopy;
            }
            else
            {
                nCopy = 0;
            }
        }
        m_nSendMsgOffset += nCopy;
        if (nCopy != nNeed)
            break;

        if (m_pSendMsg && m_pSendMsg->m_dw24 == 1)
            OnFileSent(&m_pSendMsg->m_Overlapped, WireSizePlain(*m_pSendMsg->m_pSize));
        else
            g_pNetEngine->ReleaseMsg(m_pSendMsg);
        m_pSendMsg = NULL;
        m_nSendMsgOffset = 0;
    }

    if (m_ovSend.nUsed == m_ovSend.nRead)
        return 0;
    return PostWSASend(&m_ovSend);
}

// _OnFileSent: releases the sent block and sends the next block of the session's file.
int CSockStream::OnFileSent(LPOVERLAPPED pOverlapped, DWORD dwBytes)
{
    if (pOverlapped)
        g_pNetEngine->ReleaseMsg((CMsg*)pOverlapped);

    CSession* pSession = m_pSession;
    if (!pSession)
    {
        // "... the bound session is NULL!"
        NetEngineLog(0x2000000, "CSockStream::_OnFileSent() - Bind\xB5\xC8 \xBC\xBC\xBC\xC7"
                                "\xC0\xCC NULL\xC0\xCC\xB4\xD9!");
        return SOCK_ERR_NULL;
    }

    IFile* pFile = Session_GetWorkingFile(pSession);
    if (!pFile)
    {
        // "... the session's WorkingFile is NULL"
        NetEngineLog(0x2000000, "CSockStream::_OnFileSent() - Session\xC0\xC7 WorkingFile"
                                "\xC0\xCC NULL\xC0\xCC\xB4\xD9");
        return SOCK_ERR_NO_FILE;
    }

    DWORD dwData = dwBytes ? dwBytes - MSG_HEADER_SIZE : 0;
    if (FileTransfer_AccumulateSent(pSession, dwData) == TRUE)
    {
        FileTransfer_Finish(pSession, 1);
        return 0;
    }

    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(SOCK_OP_FILE_DATA);
    BYTE* pDst = pMsg->m_pData + pMsg->m_wWritePos;
    WORD wRead = (WORD)pFile->Read(GetOwnerSessionID(), pDst);
    if (wRead > MSG_SIZE_MASK)
        BSAssertFail();
    *pMsg->m_pSize = (*pMsg->m_pSize & MSG_ENCRYPTED) | wRead;
    pMsg->m_dw24 = 1;
    if (wRead + MSG_HEADER_SIZE <= pMsg->m_nCapacity)
    {
        pMsg->m_wReadPos = (WORD)(wRead + MSG_HEADER_SIZE);
        pMsg->m_wWritePos = (WORD)(wRead + MSG_HEADER_SIZE);
    }

    if (NetEngine_GetSocketConfig(g_pNetEngine, m_nRole)->dwSendMode == 1)
    {
        m_pSendQueue->Push(pMsg);
        if (m_ovSend.nUsed != m_ovSend.nRead)
            return 0;
        return FillSendBuffer(&m_ovSend.ov, 0);
    }
    return PostWSASend((SOCK_OVERLAPPED*)pMsg);
}

int CSockStream::PostRecv()
{
    SOCK_OVERLAPPED& o = m_ovRecv;
    if (o.nUsed == o.nRead)
    {
        o.nUsed = 0;
    }
    else
    {
        int nRest = o.nUsed - o.nRead;
        memmove(o.pBuffer, o.pBuffer + o.nRead, nRest);
        o.nUsed = nRest;
    }
    o.nRead = 0;
    o.SetupWSABuf();

    DWORD dwFlags = 0;
    DWORD dwReceived = 0;
    if (WSARecv(m_hSocket, &o.WsaBuf, o.dwBufferCount, &dwReceived, &dwFlags, &o.ov, NULL)
        == SOCKET_ERROR)
    {
        int nError = WSAGetLastError();
        if (nError == WSA_IO_PENDING)
            return 0;
        if (nError == WSAEWOULDBLOCK || nError == ERROR_NOT_ENOUGH_MEMORY
            || nError == WSAENOBUFS)
        {
            return SOCK_ERR_TOO_MANY;
        }
        return SOCK_ERR_FAILED;
    }
    return 0;
}

int CSockStream::ProcessRecvData(LPOVERLAPPED, DWORD dwBytes)
{
    m_dwLastRecvTick = GetTickCount();
    if (!m_pTask)
        return SOCK_ERR_NO_TASK;
    if (dwBytes)
        m_ovRecv.Consume(dwBytes);

    SOCK_OVERLAPPED& o = m_ovRecv;
    int nResult;
    for (;;)
    {
        CMsg* pMsg = m_pRecvMsg;
        if (pMsg)
        {
            int nNeed;
            if ((m_Security.bOptions & SECURITY_OPT_BLOWFISH) && (*pMsg->m_pSize & MSG_ENCRYPTED))
                nNeed = WireSizeEncrypted(*pMsg->m_pSize) - m_nRecvMsgOffset;
            else
                nNeed = WireSizePlain(*pMsg->m_pSize) - m_nRecvMsgOffset;

            if (pMsg->m_wWritePos > pMsg->m_nCapacity)
                BSAssertFail();
            if (nNeed > (WORD)(pMsg->m_nCapacity - pMsg->m_wWritePos))
            {
                nResult = SOCK_ERR_BAD_SIZE;
                goto failed;
            }

            BYTE* pDst = m_pRecvMsg->PtrAt(m_nRecvMsgOffset);
            int nCopy = nNeed;
            if (nNeed)
            {
                if (o.nUsed == o.nRead)
                {
                    nCopy = 0;
                }
                else
                {
                    int nAvail = o.nUsed - o.nRead;
                    if (nNeed > nAvail)
                        nCopy = nAvail;
                    memcpy(pDst, o.pBuffer + o.nRead, nCopy);
                    o.nRead += nCopy;
                }
            }
            m_nRecvMsgOffset += nCopy;
            if (nCopy != nNeed)
                return PostRecv();

            WORD wSize = 0;
            nResult = DecodeRecvPacket(&wSize);
            if (nResult)
                goto failed;

            if (m_pRecvMsg->m_nCapacity < MSG_HEADER_SIZE)
            {
                nResult = SOCK_ERR_BAD_SIZE;
                goto failed;
            }
            m_pRecvMsg->m_wReadPos = MSG_HEADER_SIZE;
            if (wSize + MSG_HEADER_SIZE > m_pRecvMsg->m_nCapacity)
            {
                nResult = SOCK_ERR_BAD_SIZE;
                goto failed;
            }
            m_pRecvMsg->m_wWritePos = (WORD)(wSize + MSG_HEADER_SIZE);
            m_pRecvMsg->m_dwSessionID = GetOwnerSessionID();
            nResult = (this->*m_pfnOnMsg)(m_pRecvMsg);
            if (nResult)
                goto failed;
            m_pRecvMsg = NULL;
            m_nRecvMsgOffset = 0;
        }

        if (o.nUsed - o.nRead < MSG_HEADER_SIZE)
            return PostRecv();

        // Next packet: its header first.
        pMsg = g_pNetEngine->AllocMsg(FALSE);
        m_pRecvMsg = pMsg;
        BYTE* pDst = pMsg->PtrAt(0);
        int nCopy = MSG_HEADER_SIZE;
        if (o.nUsed == o.nRead)
        {
            nCopy = 0;
        }
        else
        {
            int nAvail = o.nUsed - o.nRead;
            if (nAvail < MSG_HEADER_SIZE)
                nCopy = nAvail;
            memcpy(pDst, o.pBuffer + o.nRead, nCopy);
            o.nRead += nCopy;
        }
        m_nRecvMsgOffset = nCopy;

        WORD wTotal = (WORD)((*m_pRecvMsg->m_pSize & MSG_SIZE_MASK) + MSG_HEADER_SIZE);
        if (wTotal > MSG_INLINE_BUF_SIZE)
        {
            if (wTotal > MSG_SIZE_MASK)
            {
                nResult = SOCK_ERR_BAD_SIZE;
                goto failed;
            }
            m_pRecvMsg->AllocLargeBuffer(wTotal);
        }
    }

failed:
    ReleaseMsgToEngine(m_pRecvMsg);
    m_pRecvMsg = NULL;
    m_nRecvMsgOffset = 0;
    return nResult;
}

int CSockStream::OnMsgReceivedBeforeHandshake(CMsg* pMsg)
{
    int nResult = 0;
    BYTE bDone = 0;

    if (m_nRole == SOCK_ROLE_CONNECTED)
    {
        // client: the server's options (first 0x5000) or its proof (second 0x5000)
        WORD wOpcode = pMsg->GetOpcode();
        if (wOpcode != SOCK_OP_HANDSHAKE)
        {
            NetEngineLog(0x1000000, "_OnMsgReceivedBeforeHandshake() - Handshake \xC0\xCC\xC0\xFC"
                                    "\xBF\xA1 \xC0\xCC\xBB\xF3\xC7\xD1 \xB8\xDE\xBD\xC3\xC1\xF6"
                                    "\xB0\xA1 \xB3\xAF\xBE\xC6\xBF\xD4\xB4\xD9[MsgID: 0x%x]",
                         wOpcode);
            goto failed;
        }

        SECURITY_HANDSHAKE_PARAM param;
        param.bOptions = 0;
        param.dwCountSeed = 0;
        param.dwCrcSeed = 0;
        memset(param.BlowFishKey, 0, sizeof(param.BlowFishKey));
        pMsg->Read(param.bOptions);
        if (param.bOptions & SECURITY_OPT_BLOWFISH)
            pMsg->ReadBytes(param.BlowFishKey, 8);
        if (param.bOptions & SECURITY_OPT_COUNT_CRC)
        {
            pMsg->Read(param.dwCountSeed);
            pMsg->Read(param.dwCrcSeed);
        }

        if (param.bOptions & SECURITY_OPT_CHALLENGE)
        {
            unsigned __int64 qwProof = 0;
            pMsg->Read(qwProof);
            if (!m_KeyRecipient.CheckPeerValue(&qwProof))
            {
                NetEngineLog(0x1000000, "ACTIVE_SESSION::_OnMsgReceivedBeforeHandshake() - "
                                        "Handshake ServerSignature Error[MsgID: 0x%x]",
                             pMsg->GetOpcode());
                goto failed;
            }

            BYTE key[8];
            m_KeyRecipient.GetFinalKey(key);
            m_Security.BlowFish.Initialize(key);

            CMsg* pAccept = g_pNetEngine->AllocMsg(FALSE);
            pAccept->SetOpcode(SOCK_OP_HANDSHAKE_ACCEPT);
            SignPacket(pAccept, &m_Security);
            nResult = (this->*m_pfnSend)(pAccept);
            ReleaseMsgToEngine(pAccept);
            bDone = 1;
        }
        else
        {
            Security_ApplyHandshakeFlags(&m_Security, &param);
            if (param.bOptions & SECURITY_OPT_KEY_EXCHANGE)
            {
                KEYEXCHANGE_SERVER_PARAM server;
                pMsg->Read(server);
                m_KeyRecipient.AcceptServerParams(&server);
                KEYEXCHANGE_CHALLENGE challenge;
                m_KeyRecipient.MakeResponse(&challenge);

                CMsg* pReply = g_pNetEngine->AllocMsg(FALSE);
                pReply->SetOpcode(SOCK_OP_HANDSHAKE);
                pReply->WriteBytes(&challenge, sizeof(challenge));
                SignPacket(pReply, &m_Security);
                nResult = (this->*m_pfnSend)(pReply);
                ReleaseMsgToEngine(pReply);
            }
            else
            {
                CMsg* pAccept = g_pNetEngine->AllocMsg(FALSE);
                pAccept->SetOpcode(SOCK_OP_HANDSHAKE_ACCEPT);
                if (param.bOptions & SECURITY_OPT_COUNT_CRC)
                {
                    SignPacket(pAccept, &m_Security);
                }
                else
                {
                    pAccept->SetCountByte(0);
                    pAccept->SetCrcByte(0);
                }
                nResult = (this->*m_pfnSend)(pAccept);
                ReleaseMsgToEngine(pAccept);
                bDone = 1;
            }
        }
    }
    else
    {
        // server: the client's key exchange answer or its final 0x9000
        BYTE bState = m_bHandshakeState;
        if (!bState)
        {
            NetEngineLog(0x1000000, "_OnMsgReceivedBeforeHandshake() - Initialize [MsgID: 0x%x]",
                         pMsg->GetOpcode());
            goto failed;
        }

        WORD wOpcode = pMsg->GetOpcode();
        if (wOpcode == SOCK_OP_HANDSHAKE)
        {
            if (!(bState & SECURITY_HS_BLOWFISH) || !(bState & SECURITY_HS_COUNT_CRC)
                || !(bState & SECURITY_HS_KEY_EXCHANGE))
            {
                NetEngineLog(0x1000000,
                             "_OnMsgReceivedBeforeHandshake() - Handshake Failed:%d [MsgID: 0x%x]",
                             bState, SOCK_OP_HANDSHAKE);
                goto failed;
            }
            WORD wSize = pMsg->GetPayloadSize();
            if (wSize != sizeof(KEYEXCHANGE_CHALLENGE))
            {
                NetEngineLog(0x1000000, "_OnMsgReceivedBeforeHandshake() - Handshake Recipient "
                                        "Size:%d [MsgID: 0x%x]",
                             wSize, SOCK_OP_HANDSHAKE);
                goto failed;
            }

            KEYEXCHANGE_CHALLENGE challenge;
            pMsg->Read(challenge);
            if (!m_KeySender.VerifyChallenge(&challenge))
            {
                NetEngineLog(0x1000000, "_OnMsgReceivedBeforeHandshake() - Handshake "
                                        "RecipientInfo Error [MsgID: 0x%x]",
                             pMsg->GetOpcode());
                goto failed;
            }

            CMsg* pReply = g_pNetEngine->AllocMsg(FALSE);
            pReply->SetOpcode(SOCK_OP_HANDSHAKE);
            BYTE bOptions = SECURITY_OPT_CHALLENGE;
            pReply->WriteBytes(&bOptions, 1);
            BYTE proof[8];
            m_KeySender.MakeChallenge(proof);
            pReply->WriteBytes(proof, 8);
            nResult = (this->*m_pfnSend)(pReply);
            ReleaseMsgToEngine(pReply);

            BYTE key[8];
            m_KeySender.GetFinalKey(key);
            m_Security.BlowFish.Initialize(key);
            m_bHandshakeState |= SECURITY_HS_CHALLENGE;
        }
        else if (wOpcode == SOCK_OP_HANDSHAKE_ACCEPT)
        {
            if (!Security_ModeCheck(NetEngine_GetSecurityMode(g_pNetEngine), m_bHandshakeState))
            {
                NetEngineLog(0x1000000, "SecurityModeCheck - Mode:%d, Handshake:%d",
                             NetEngine_GetSecurityMode(g_pNetEngine), m_bHandshakeState);
                goto failed;
            }
            bDone = 1;
        }
        else
        {
            NetEngineLog(0x1000000, "_OnMsgReceivedBeforeHandshake() - Handshake \xC0\xCC\xC0\xFC"
                                    "\xBF\xA1 \xC0\xCC\xBB\xF3\xC7\xD1 \xB8\xDE\xBD\xC3\xC1\xF6"
                                    "\xB0\xA1 \xB3\xAF\xBE\xC6\xBF\xD4\xB4\xD9[MsgID: 0x%x]",
                         wOpcode);
            goto failed;
        }
    }

    if (nResult)
        return nResult;
    ReleaseMsgToEngine(pMsg);
    if (bDone)
        SetHandshaked();
    return nResult;

failed:
    m_bHandshakeState = 0;
    return SOCK_ERR_HANDSHAKE;
}

// Session_OnMsgReceivedAfterHandshake
int CSockStream::OnMsgReceivedAfterHandshake(CMsg* pMsg)
{
    if (pMsg->GetOpcode() == SOCK_OP_FILE_DATA)
        return Session_HandleFileTransferData(pMsg, this);
    if (!m_pTask)
        return SOCK_ERR_NO_HANDLER;
    m_pTask->m_pQueue->Push(pMsg);
    return 0;
}

// Session_SetHandshakedAndNotify
void CSockStream::SetHandshaked()
{
    m_bHandshaked = TRUE;
    m_pfnOnMsg = &CSockStream::OnMsgReceivedAfterHandshake;
    Session_PostHandshakeEvent(m_pSession);
}

// SockStream_ClearField130Locked
void CSockStream::ClearCloseStateLocked()
{
    CSingleLockBS lock(&m_cs, TRUE);
    m_dwCloseState = 0;
}

// Session_SetProactorLocked
void CSockStream::SetTask(CTask* pTask)
{
    CSingleLockBS lock(&m_cs, TRUE);
    m_pTask = pTask;
}
