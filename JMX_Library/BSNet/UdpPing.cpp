#include "UdpPing.h"

#include <process.h>
#include <string.h>
#include <strsafe.h>

namespace
{

// Host string to IPv4 address as the ping classes do it: empty -> INADDR_ANY, otherwise
// inet_addr with a gethostbyname fallback.
ULONG UdpPing_ResolveHost(const std::string& strHost)
{
    const char* pszHost = strHost.c_str();
    if (!pszHost || !strlen(pszHost))
        return 0;
    ULONG ulAddr = inet_addr(pszHost);
    if (ulAddr == INADDR_NONE)
    {
        // BUG(SOCK-007): no NULL check; an unknown host name crashes here.
        ulAddr = *(ULONG*)gethostbyname(pszHost)->h_addr_list[0];
    }
    return ulAddr;
}

int UdpPing_Send(SOCKET s, const sockaddr* pTo, char c0, char c1, char c2, DWORD dwLow,
                 DWORD dwHigh)
{
    char buf[UDPPING_PACKET_SIZE];
    buf[0] = c0;
    buf[1] = c1;
    buf[2] = c2;
    buf[3] = 0;
    // BUG(SOCK-006): bytes 4..7 are not initialised and go out with stack contents.
    *(DWORD*)(buf + 8) = dwLow;
    *(DWORD*)(buf + 12) = dwHigh;
    return sendto(s, buf, UDPPING_PACKET_SIZE, 0, pTo, sizeof(sockaddr_in));
}

// CUdpPingServer_BindPort15851
int UdpPing_BindServerPort(SOCKET s, ULONG ulAddr)
{
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDPPING_SERVER_PORT);
    addr.sin_addr.s_addr = ulAddr;
    return bind(s, (const sockaddr*)&addr, sizeof(addr));
}

// Logs a recvfrom failure.
void UdpPing_ReportRecvError(int nError)
{
    char szText[255];
    // BUG(SOCK-008): StringCchPrintfA returns S_OK (0) on success, so the message is only
    // printed when formatting fails.
    if (StringCchPrintfA(szText, 255, "recvfrom failed! [err: %d]\n", nError))
        OutputDebugStringA(szText);
}

} // namespace

int UdpPing_SendReq(SOCKET s, const sockaddr* pTo, DWORD dwLow, DWORD dwHigh)
{
    return UdpPing_Send(s, pTo, 'R', 'E', 'Q', dwLow, dwHigh);
}

int UdpPing_SendAck(SOCKET s, const sockaddr* pTo, DWORD dwLow, DWORD dwHigh)
{
    return UdpPing_Send(s, pTo, 'A', 'C', 'K', dwLow, dwHigh);
}

// ---------------------------------------------------------------------------------------------
// CUdpPingServer

CUdpPingServer::CUdpPingServer()
    : CUdpPingBase(0)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    m_hSocket = INVALID_SOCKET;
    m_hThread = NULL;
    m_bSuspended = 1;
}

CUdpPingServer::~CUdpPingServer()
{
    Stop();
    WSACleanup();
}

HANDLE CUdpPingServer::Open(const std::string& strHost)
{
    m_hSocket = socket(AF_INET, SOCK_DGRAM, 0);
    UdpPing_BindServerPort(m_hSocket, UdpPing_ResolveHost(strHost));
    unsigned uThreadID;
    m_hThread = (HANDLE)_beginthreadex(NULL, 0, ThreadProc, this, CREATE_SUSPENDED, &uThreadID);
    return m_hThread;
}

void CUdpPingServer::Stop()
{
    if (!m_hThread)
        return;
    if (m_bSuspended)
        Resume();
    closesocket(m_hSocket);
    m_hSocket = INVALID_SOCKET;
    WaitForSingleObject(m_hThread, INFINITE);
    CloseHandle(m_hThread);
    m_hThread = NULL;
}

BOOL CUdpPingServer::Resume()
{
    if (!m_hThread)
        return FALSE;
    if (m_bSuspended)
    {
        DWORD dwCount = ResumeThread(m_hThread);
        if (dwCount == (DWORD)-1)
            return FALSE;
        if (dwCount > 1)
        {
            for (DWORD i = dwCount - 1; i; --i)
                ResumeThread(m_hThread);
        }
        InterlockedExchange(&m_bSuspended, 0);
    }
    return TRUE;
}

BOOL CUdpPingServer::Suspend()
{
    if (!m_hThread)
        return FALSE;
    if (!m_bSuspended)
    {
        DWORD dwCount = SuspendThread(m_hThread);
        if (dwCount == (DWORD)-1)
            return FALSE;
        for (DWORD i = dwCount; i; --i)
            ResumeThread(m_hThread);
        InterlockedExchange(&m_bSuspended, 1);
    }
    return TRUE;
}

// CUdpPingServer::OnRecv_wrap
unsigned __stdcall CUdpPingServer::ThreadProc(void* pParam)
{
    return ((CUdpPingServer*)pParam)->OnRecv();
}

int CUdpPingServer::OnRecv()
{
    char buf[0x1000];
    sockaddr from;
    int nFromLen;
    for (;;)
    {
        nFromLen = sizeof(from);
        memset(buf, 0, sizeof(buf));
        if (recvfrom(m_hSocket, buf, sizeof(buf), 0, &from, &nFromLen) == SOCKET_ERROR)
        {
            int nError = WSAGetLastError();
            if (nError != WSAECONNRESET)
            {
                UdpPing_ReportRecvError(nError);
                return 0;
            }
            OutputDebugStringA("unreachable host!\n");
            continue;
        }

        if (buf[0] == 'R' && buf[1] == 'E' && buf[2] == 'Q')
        {
            OutputDebugStringA("REQ received!\n");
            if (UdpPing_SendAck(m_hSocket, &from, *(DWORD*)(buf + 8), *(DWORD*)(buf + 12))
                == SOCKET_ERROR)
            {
                OutputDebugStringA("failed to send ack!\n");
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// CUdpPingClient

CUdpPingClient::CUdpPingClient()
    : CUdpPingBase(1)
{
    m_hWnd = NULL;
    m_uMsg = (UINT)-1;
    m_hSocket = INVALID_SOCKET;
    m_hThread = NULL;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    m_bHighRes = QueryPerformanceFrequency(&m_liFrequency);
}

CUdpPingClient::~CUdpPingClient()
{
    Close();
    WSACleanup();
}

void CUdpPingClient::Close()
{
    if (m_hSocket != INVALID_SOCKET)
    {
        closesocket(m_hSocket);
        m_hSocket = INVALID_SOCKET;
    }
    if (m_hThread)
    {
        WaitForSingleObject(m_hThread, INFINITE);
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }
    m_mapTargets.clear();
}

WORD CUdpPingClient::AddTarget(WORD wID, const std::string& strHost, u_short wPort)
{
    if (m_mapTargets.find(wID) != m_mapTargets.end())
        return UDPPING_ID_ANY;

    // SockAddr_FromHostPort
    UDPPING_TARGET target;
    memset(&target.Addr, 0, sizeof(target.Addr));
    target.wID = wID;
    target.Addr.sin_family = AF_INET;
    target.Addr.sin_addr.s_addr = UdpPing_ResolveHost(strHost);
    target.Addr.sin_port = htons(wPort);
    m_mapTargets.insert(TargetMap::value_type(wID, target));
    return wID;
}

void CUdpPingClient::RemoveTarget(WORD wID)
{
    TargetMap::iterator it = m_mapTargets.find(wID);
    // BUG(SOCK-009): inverted test: an existing target is kept, a missing one is erased
    // (end()), which throws std::out_of_range("invalid map/set<T> iterator").
    if (it == m_mapTargets.end())
        m_mapTargets.erase(it);
}

void CUdpPingClient::SendPing(UDPPING_TARGET* pTarget)
{
    LARGE_INTEGER liNow;
    if (m_bHighRes)
    {
        QueryPerformanceCounter(&liNow);
    }
    else
    {
        liNow.LowPart = GetTickCount();
        liNow.HighPart = 0;
    }
    UdpPing_SendReq(m_hSocket, (const sockaddr*)&pTarget->Addr, liNow.LowPart,
                    (DWORD)liNow.HighPart);
    if (!m_hThread)
    {
        unsigned uThreadID;
        m_hThread = (HANDLE)_beginthreadex(NULL, 0, ThreadProc, this, 0, &uThreadID);
    }
}

void CUdpPingClient::PingTarget(WORD wID)
{
    if (wID == UDPPING_ID_ANY)
    {
        for (TargetMap::iterator it = m_mapTargets.begin(); it != m_mapTargets.end(); ++it)
            SendPing(&it->second);
        return;
    }

    TargetMap::iterator it = m_mapTargets.find(wID);
    if (it != m_mapTargets.end())
        SendPing(&it->second);
}

WORD CUdpPingClient::FindIdByAddr(const sockaddr* pFrom)
{
    for (TargetMap::iterator it = m_mapTargets.begin(); it != m_mapTargets.end(); ++it)
    {
        if (memcmp(pFrom, &it->second.Addr, sizeof(sockaddr_in)) == 0)
            return it->second.wID;
    }
    return UDPPING_ID_ANY;
}

// CUdpPingClient::OnRecv_wrap
unsigned __stdcall CUdpPingClient::ThreadProc(void* pParam)
{
    return ((CUdpPingClient*)pParam)->OnRecv();
}

int CUdpPingClient::OnRecv()
{
    char buf[0x1000];
    sockaddr from;
    int nFromLen;
    for (;;)
    {
        memset(buf, 0, sizeof(buf));
        nFromLen = sizeof(from);
        if (recvfrom(m_hSocket, buf, sizeof(buf), 0, &from, &nFromLen) == SOCKET_ERROR)
        {
            int nError = WSAGetLastError();
            if (nError != WSAECONNRESET)
            {
                UdpPing_ReportRecvError(nError);
                return 0;
            }
            WORD wID = FindIdByAddr(&from);
            PostMessageA(m_hWnd, m_uMsg, wID | UDPPING_UNREACHABLE, 0);
            OutputDebugStringA("unreachable host!\n");
            continue;
        }

        if (buf[0] == 'A' && buf[1] == 'C' && buf[2] == 'K')
        {
            LARGE_INTEGER liNow;
            if (m_bHighRes)
            {
                QueryPerformanceCounter(&liNow);
            }
            else
            {
                liNow.LowPart = GetTickCount();
                liNow.HighPart = 0;
            }
            __int64 nElapsed = liNow.QuadPart - *(__int64*)(buf + 8);
            if (m_bHighRes)
                nElapsed = nElapsed * 1000 / m_liFrequency.QuadPart;
            WORD wID = FindIdByAddr(&from);
            PostMessageA(m_hWnd, m_uMsg, wID, (LPARAM)(DWORD)nElapsed);
            OutputDebugStringA("ACK received!\n");
        }
        else
        {
            OutputDebugStringA("unknown datagram received!\n");
        }
    }
}
