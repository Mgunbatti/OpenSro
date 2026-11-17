#include "SockSystem.h"

#include <string.h>

LPFN_ACCEPTEX g_pfnAcceptEx = NULL;
LPFN_GETACCEPTEXSOCKADDRS g_pfnGetAcceptExSockaddrs = NULL;
LPFN_TRANSMITFILE g_pfnTransmitFile = NULL;

BOOL CSockSystem::Startup(BYTE bLow, BYTE bHigh)
{
    if (m_bStarted)
        return TRUE;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(bLow, bHigh), &wsa) == 0)
    {
        if (LOBYTE(wsa.wVersion) == bLow && HIBYTE(wsa.wVersion) == bHigh)
        {
            m_bStarted = TRUE;
            return TRUE;
        }
        WSACleanup();
    }
    return FALSE;
}

BOOL CSockSystem::Initialize(BYTE bLow, BYTE bHigh)
{
    if (!Startup(bLow, bHigh))
        return FALSE;
    return Winsock_LoadAcceptExFuncs();
}

BOOL Winsock_LoadAcceptExFuncs()
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    DWORD cbBytes = 0;
    HMODULE hModule = NULL;

    GUID guidAcceptEx = WSAID_ACCEPTEX;
    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx, sizeof(GUID),
                 &g_pfnAcceptEx, 4, &cbBytes, NULL, NULL) == SOCKET_ERROR)
    {
        hModule = LoadLibraryA("mswsock.dll");
        g_pfnAcceptEx = (LPFN_ACCEPTEX)GetProcAddress(hModule, "AcceptEx");
        if (!g_pfnAcceptEx)
            goto failed;
    }

    {
        GUID guidSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
        if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidSockaddrs, sizeof(GUID),
                     &g_pfnGetAcceptExSockaddrs, 4, &cbBytes, NULL, NULL) == SOCKET_ERROR)
        {
            g_pfnGetAcceptExSockaddrs = (LPFN_GETACCEPTEXSOCKADDRS)GetProcAddress(
                hModule, "GetAcceptExSockaddrs");
            if (!g_pfnGetAcceptExSockaddrs)
                goto failed;
        }
    }

    {
        GUID guidTransmitFile = WSAID_TRANSMITFILE;
        if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidTransmitFile, sizeof(GUID),
                     &g_pfnTransmitFile, 4, &cbBytes, NULL, NULL) == SOCKET_ERROR)
        {
            g_pfnTransmitFile = (LPFN_TRANSMITFILE)GetProcAddress(hModule, "TransmitFile");
            if (!g_pfnTransmitFile)
                goto failed;
        }
    }

    // BUG(SOCK-001): releases mswsock.dll after taking function pointers from it.
    if (hModule)
        FreeLibrary(hModule);
    closesocket(s);
    return TRUE;

failed:
    WSAGetLastError();
    closesocket(s);
    return FALSE;
}

int OS_IsNotWinNT()
{
    OSVERSIONINFOEXA vi;
    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXA);
    if (!GetVersionExA((LPOSVERSIONINFOA)&vi))
    {
        vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
        if (!GetVersionExA((LPOSVERSIONINFOA)&vi))
            return -1;
    }
    return vi.dwPlatformId != VER_PLATFORM_WIN32_NT;
}

ULONG ResolveHostIPv4(const char* pszHost)
{
    if (!pszHost || !strlen(pszHost))
        return 0;

    ULONG ulAddr = inet_addr(pszHost);
    if (ulAddr == INADDR_NONE)
    {
        hostent* pHost = gethostbyname(pszHost);
        if (!pHost || pHost->h_length != 4)
            return INADDR_NONE;
        return *(ULONG*)pHost->h_addr_list[0];
    }
    return ulAddr;
}

BOOL Net_GetLocalHostAddr(in_addr* pAddr, char* pszAddr)
{
    hostent* pHost = gethostbyname("localhost");
    if (!pHost)
        return FALSE;
    pHost = gethostbyname(pHost->h_name);
    if (!pHost)
        return FALSE;

    in_addr addr;
    memcpy(&addr, pHost->h_addr_list[0], pHost->h_length);
    *pAddr = addr;
    if (pszAddr && strcpy_s(pszAddr, 256, inet_ntoa(addr)))
        return FALSE;
    return TRUE;
}

BOOL Socket_GetPeerAddr(SOCKET s, DWORD* pdwAddr, char* pszAddr)
{
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    int nLen = sizeof(addr);
    if (getpeername(s, (sockaddr*)&addr, &nLen) == SOCKET_ERROR)
        return FALSE;

    *pdwAddr = addr.sin_addr.s_addr;
    if (pszAddr && strcpy_s(pszAddr, 256, inet_ntoa(addr.sin_addr)))
        return FALSE;
    return TRUE;
}

int Socket_SetBufferSizeFallback(SOCKET s, int nOption, int nSize)
{
    int nResult = 0;
    int nValue = nSize;
    int nLen = 0;
    if (nValue <= 1460)
        return nResult;

    // BUG(SOCK-002): keeps halving the size after every successful setsockopt instead of after
    // a failure, so it ends with the smallest size above 1460 and returns 0.
    for (;;)
    {
        nLen = setsockopt(s, SOL_SOCKET, nOption, (const char*)&nValue, 4);
        if (nLen == SOCKET_ERROR)
            break;
        nValue >>= 1;
        if (nValue <= 1460)
            return nResult;
    }

    int nError = WSAGetLastError();
    if (nError != WSAENOPROTOOPT && nError != WSAEINVAL)
    {
        nLen = 4;
        getsockopt(s, SOL_SOCKET, nOption, (char*)&nResult, &nLen);
    }
    return nResult;
}
