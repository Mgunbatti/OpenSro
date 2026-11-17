#pragma once

#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>

// Owner of the Winsock start-up (8 bytes), created by CSystem.
class CSockSystem
{
public:
    CSockSystem()
    {
        m_bStarted = FALSE;
    }

    virtual ~CSockSystem()                                              // 0
    {
        Cleanup();
    }

    // WSACleanup once WSAStartup succeeded.
    void Cleanup()
    {
        if (m_bStarted)
        {
            if (WSACleanup())
                WSAGetLastError();
            m_bStarted = FALSE;
        }
    }

    // NetIO_WSAStartupVersion: WSAStartup(MAKEWORD(bLow, bHigh)); TRUE (and started) only when
    // that exact version was granted, otherwise WSACleanup. TRUE right away when started.
    BOOL Startup(BYTE bLow, BYTE bHigh);

    // NetIO_InitAndLoadAcceptEx: Startup, then Winsock_LoadAcceptExFuncs.
    BOOL Initialize(BYTE bLow, BYTE bHigh);

public:
    BOOL m_bStarted;    // +4
};

C_ASSERT(sizeof(CSockSystem) == 8);

// Extension functions (0x53AD74 / 0x53AD78 / 0x53AD7C).
extern LPFN_ACCEPTEX g_pfnAcceptEx;
extern LPFN_GETACCEPTEXSOCKADDRS g_pfnGetAcceptExSockaddrs;
extern LPFN_TRANSMITFILE g_pfnTransmitFile;

// Looks the three functions up with SIO_GET_EXTENSION_FUNCTION_POINTER on a temporary TCP
// socket, falling back to mswsock.dll exports. FALSE when one is missing.
BOOL Winsock_LoadAcceptExFuncs();

// 0 on Windows NT (completion ports), 1 on 9x, -1 when GetVersionEx fails.
int OS_IsNotWinNT();

// inet_addr, then gethostbyname (IPv4 only). 0 for an empty name, INADDR_NONE when the name
// does not resolve.
ULONG ResolveHostIPv4(const char* pszHost);

// Address of the host's own name (gethostbyname("localhost"), then its h_name); optionally as
// dotted text (256 bytes).
BOOL Net_GetLocalHostAddr(in_addr* pAddr, char* pszAddr);

// Peer IPv4 address of a connected socket; optionally as dotted text (256 bytes).
BOOL Socket_GetPeerAddr(SOCKET s, DWORD* pdwAddr, char* pszAddr);

// SO_SNDBUF / SO_RCVBUF helper of the original (unused there).
int Socket_SetBufferSizeFallback(SOCKET s, int nOption, int nSize);
