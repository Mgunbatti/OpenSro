#include "BaseSocket.h"

#include <string.h>

#include "BSNet/NetEngineLog.h"

// IOBuffer_Consume
void SOCK_OVERLAPPED::Consume(int nBytes)
{
    if (!nBytes)
        return;

    if (dwIoType == SOCK_IO_RECV)
    {
        nUsed += nBytes;
    }
    else if (dwIoType == SOCK_IO_SEND)
    {
        int nRest = nUsed - nBytes;
        if (nRest > 0)
        {
            // "!!!! oh! so this does happen?" - a send completed only partially.
            NetEngineLog(0x1000000, "!!!! \xBF\xC0\xC8\xAA! \xC0\xCC\xB7\xB1 \xB0\xE6\xBF\xEC"
                                    "\xB0\xA1 \xC0\xD6\xB1\xE4 \xC0\xD6\xB3\xD7?");
            memmove(pBuffer, pBuffer + nBytes, nRest);
        }
        nUsed = nRest;
    }
}

CBaseSocket::CBaseSocket()
{
    m_hSocket = INVALID_SOCKET;
    m_nKind = SOCK_KIND_NONE;
    m_bGraceful = FALSE;
    m_nRole = SOCK_ROLE_NONE;
}

CBaseSocket::~CBaseSocket()
{
    CBaseSocket::Close();
}

// nullsub_991
void CBaseSocket::OnIdle()
{
}

int CBaseSocket::Close()
{
    if (m_hSocket != INVALID_SOCKET)
    {
        closesocket(m_hSocket);
        m_hSocket = INVALID_SOCKET;
    }
    return 0;
}

BOOL CBaseSocket::CreateSocket(int nRole)
{
    m_nRole = nRole;
    if (m_hSocket != INVALID_SOCKET)
        return TRUE;

    m_hSocket = WSASocketA(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (m_hSocket != INVALID_SOCKET)
        return TRUE;

    WSAGetLastError();
    CBaseSocket::Close();
    return FALSE;
}

sockaddr_in* CBaseSocket::GetLocalAddr(sockaddr_in* pAddr)
{
    memset(pAddr, 0, sizeof(*pAddr));
    int nLen = sizeof(*pAddr);
    getsockname(m_hSocket, (sockaddr*)pAddr, &nLen);
    return pAddr;
}
