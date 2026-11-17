#include "SockPool.h"

#include "BSNet/NetEngineFwd.h"
#include "BSNet/SockDatagram.h"
#include "BSNet/SockListener.h"
#include "BSNet/SockStream.h"

void Sock_ReleaseToConnectPool(CSockStream* pSock, IBSNet* pEngine)
{
    if (pSock)
    {
        pSock->Close();
        NetEngine_GetConnectPool(pEngine)->Free(pSock);
    }
}

void Sock_ReleaseToAcceptPool(CSockStream* pSock, IBSNet* pEngine)
{
    if (pSock)
    {
        pSock->Close();
        NetEngine_GetAcceptPool(pEngine)->Free(pSock);
    }
}

void Sock_ReleaseToDatagramPool(CSockDatagram* pSock, IBSNet* pEngine)
{
    if (pSock)
    {
        pSock->Close();
        NetEngine_GetDatagramPool(pEngine)->Free(pSock);
    }
}

void Sock_ReleaseToDatagramPoolAndClear(CSockDatagram** ppSock)
{
    if (*ppSock)
    {
        if (g_pNetEngine)
            Sock_ReleaseToDatagramPool(*ppSock, g_pNetEngine);
        *ppSock = NULL;
    }
}

void SockPool_CloseSocket(CSockStream* pSock, IBSNet* pEngine)
{
    if (!pSock)
        return;

    int nRole = pSock->m_nRole;
    pSock->Close();
    if (nRole == SOCK_ROLE_ACCEPTED)
    {
        if (!NetEngine_GetListener(pEngine)->RepostAcceptForSocket(pSock))
            NetEngine_GetAcceptPool(pEngine)->Free(pSock);
    }
    else if (nRole == SOCK_ROLE_CONNECTED)
    {
        NetEngine_GetConnectPool(pEngine)->Free(pSock);
    }
}
