#pragma once

// Test-only bind redirect: keeps every socket of the test executable on the loopback address,
// so no test opens a port on the network (and Windows Firewall never asks for permission).
// The servers' code is unchanged; the import address table of this executable is patched:
//
// - bind() with any IPv4 address outside 127.0.0.0/8 (INADDR_ANY included) binds 127.0.0.1
//   with the same port instead;
// - sendto() / WSASendTo() on a socket that is not bound yet binds it to 127.0.0.1 first
//   (the system would bind it to INADDR_ANY).
//
// Set LOOPBACK_BIND_TRACE=1 to print every bind (the requested address) and implicit bind.

#include <winsock2.h>
#include <stdio.h>
#include <string.h>

typedef int (WSAAPI* PFN_LOOPBACK_BIND)(SOCKET s, const sockaddr* pAddr, int nLen);
typedef int (WSAAPI* PFN_LOOPBACK_SENDTO)(SOCKET s, const char* pBuf, int nLen, int nFlags,
                                          const sockaddr* pTo, int nToLen);
typedef int (WSAAPI* PFN_LOOPBACK_WSASENDTO)(SOCKET s, LPWSABUF pBuffers, DWORD dwCount,
                                             LPDWORD pdwSent, DWORD dwFlags, const sockaddr* pTo,
                                             int nToLen, LPWSAOVERLAPPED pOverlapped,
                                             LPWSAOVERLAPPED_COMPLETION_ROUTINE pfnCompletion);
// Called after every bind of an IPv4 address with the address actually bound and the result.
typedef void (*PFN_LOOPBACK_BOUND)(const sockaddr_in& addr, int nResult);

static PFN_LOOPBACK_BIND      s_pfnLoopbackRealBind;
static PFN_LOOPBACK_SENDTO    s_pfnLoopbackRealSendTo;
static PFN_LOOPBACK_WSASENDTO s_pfnLoopbackRealWSASendTo;
static PFN_LOOPBACK_BOUND     s_pfnLoopbackBound;
static bool                   s_bLoopbackTrace;

static bool LoopbackBind_IsLoopback(ULONG ulAddr)
{
    return (ntohl(ulAddr) >> 24) == 127;
}

static int WSAAPI LoopbackBind_Bind(SOCKET s, const sockaddr* pAddr, int nLen)
{
    sockaddr_in addr;
    if (!pAddr || nLen < (int)sizeof(addr) || pAddr->sa_family != AF_INET)
        return s_pfnLoopbackRealBind(s, pAddr, nLen);
    memcpy(&addr, pAddr, sizeof(addr));
    if (s_bLoopbackTrace)
        printf("LoopbackBind: bind %s:%u\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    if (!LoopbackBind_IsLoopback(addr.sin_addr.s_addr))
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int nResult = s_pfnLoopbackRealBind(s, (const sockaddr*)&addr, sizeof(addr));
    if (s_pfnLoopbackBound)
    {
        int nError = WSAGetLastError();
        s_pfnLoopbackBound(addr, nResult);
        WSASetLastError(nError);
    }
    return nResult;
}

static void LoopbackBind_BindIfUnbound(SOCKET s)
{
    sockaddr_in addr;
    int nLen = sizeof(addr);
    if (getsockname(s, (sockaddr*)&addr, &nLen) == 0 || WSAGetLastError() != WSAEINVAL)
        return;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (s_bLoopbackTrace)
        printf("LoopbackBind: implicit bind before a send\n");
    s_pfnLoopbackRealBind(s, (const sockaddr*)&addr, sizeof(addr));
}

static int WSAAPI LoopbackBind_SendTo(SOCKET s, const char* pBuf, int nLen, int nFlags,
                                      const sockaddr* pTo, int nToLen)
{
    LoopbackBind_BindIfUnbound(s);
    return s_pfnLoopbackRealSendTo(s, pBuf, nLen, nFlags, pTo, nToLen);
}

static int WSAAPI LoopbackBind_WSASendTo(SOCKET s, LPWSABUF pBuffers, DWORD dwCount,
                                         LPDWORD pdwSent, DWORD dwFlags, const sockaddr* pTo,
                                         int nToLen, LPWSAOVERLAPPED pOverlapped,
                                         LPWSAOVERLAPPED_COMPLETION_ROUTINE pfnCompletion)
{
    LoopbackBind_BindIfUnbound(s);
    return s_pfnLoopbackRealWSASendTo(s, pBuffers, dwCount, pdwSent, dwFlags, pTo, nToLen,
                                      pOverlapped, pfnCompletion);
}

// Replaces the import address table entries of pfnOld in this executable; returns their count.
static int LoopbackBind_Patch(void* pfnOld, void* pfnNew)
{
    BYTE* pBase = (BYTE*)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS* pNt = (IMAGE_NT_HEADERS*)(pBase + ((IMAGE_DOS_HEADER*)pBase)->e_lfanew);
    IMAGE_DATA_DIRECTORY& dir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    void** ppEntry = (void**)(pBase + dir.VirtualAddress);
    int nPatched = 0;
    for (DWORD i = 0; i < dir.Size / sizeof(void*); ++i)
    {
        if (ppEntry[i] != pfnOld)
            continue;
        DWORD dwOld;
        VirtualProtect(&ppEntry[i], sizeof(void*), PAGE_READWRITE, &dwOld);
        ppEntry[i] = pfnNew;
        VirtualProtect(&ppEntry[i], sizeof(void*), dwOld, &dwOld);
        ++nPatched;
    }
    return nPatched;
}

// Installs the redirect (once, before the first socket). Returns the number of patched bind()
// imports: 1 for every executable that can bind.
static int LoopbackBind_Install(PFN_LOOPBACK_BOUND pfnBound = NULL)
{
    s_pfnLoopbackBound = pfnBound;
    if (s_pfnLoopbackRealBind)
        return 1;
    char szTrace[8];
    s_bLoopbackTrace = GetEnvironmentVariableA("LOOPBACK_BIND_TRACE", szTrace, sizeof(szTrace)) > 0;
    HMODULE hWs2 = LoadLibraryA("ws2_32.dll");
    s_pfnLoopbackRealBind = (PFN_LOOPBACK_BIND)GetProcAddress(hWs2, "bind");
    s_pfnLoopbackRealSendTo = (PFN_LOOPBACK_SENDTO)GetProcAddress(hWs2, "sendto");
    s_pfnLoopbackRealWSASendTo = (PFN_LOOPBACK_WSASENDTO)GetProcAddress(hWs2, "WSASendTo");
    LoopbackBind_Patch((void*)s_pfnLoopbackRealSendTo, (void*)LoopbackBind_SendTo);
    LoopbackBind_Patch((void*)s_pfnLoopbackRealWSASendTo, (void*)LoopbackBind_WSASendTo);
    return LoopbackBind_Patch((void*)s_pfnLoopbackRealBind, (void*)LoopbackBind_Bind);
}
