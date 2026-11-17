#pragma once

#include <winsock2.h>
#include <windows.h>
#include <map>
#include <string>

// UDP round-trip measurement. The client sends 16-byte "REQ" datagrams carrying a timestamp,
// the server (port 15851) echoes them as "ACK"; the client posts the round-trip time to a
// window. Both own a Winsock start-up and a receive thread.

enum
{
#ifdef JMX_FRAMEWORK_2007
    UDPPING_SERVER_PORT = 15850,    // FarmManager 0x459820
#else
    UDPPING_SERVER_PORT = 15851,
#endif
    UDPPING_PACKET_SIZE = 16,
    UDPPING_ID_ANY      = 0xFFFF,
    // High word of wParam of the client's window message when the target is unreachable
    // (WSAECONNRESET).
    UDPPING_UNREACHABLE = 0x27460000,
};

class CUdpPingBase
{
public:
    explicit CUdpPingBase(DWORD dwType)
        : m_dwType(dwType)
    {
    }

    virtual ~CUdpPingBase()                                         // 0
    {
    }

public:
    DWORD   m_dwType;   // +4  0 server, 1 client
};

C_ASSERT(sizeof(CUdpPingBase) == 8);

class CUdpPingServer : public CUdpPingBase
{
public:
    CUdpPingServer();
    virtual ~CUdpPingServer();                                      // 0

    // UDP socket bound to strHost:15851 and a suspended receive thread. Returns the thread
    // handle.
    HANDLE Open(const std::string& strHost);
    // Closes the socket and waits for the receive thread (resumed first if suspended).
    void Stop();
    // WorkerThread_ResumeFully: resumes until the suspend count is 0. FALSE without a thread
    // or when ResumeThread fails.
    BOOL Resume();
    // WorkerThread_SuspendExactlyOnce: leaves the thread suspended exactly once.
    BOOL Suspend();

protected:
    static unsigned __stdcall ThreadProc(void* pParam);
    int OnRecv();

public:
    SOCKET          m_hSocket;      // +8
    HANDLE          m_hThread;      // +C
    volatile LONG   m_bSuspended;   // +10
};

C_ASSERT(FIELD_OFFSET(CUdpPingServer, m_hSocket) == 8);
C_ASSERT(FIELD_OFFSET(CUdpPingServer, m_bSuspended) == 0x10);
C_ASSERT(sizeof(CUdpPingServer) == 0x14);

// A ping target of the client.
struct UDPPING_TARGET
{
    WORD        wID;        // +0
    sockaddr_in Addr;       // +4
};

C_ASSERT(sizeof(UDPPING_TARGET) == 20);

class CUdpPingClient : public CUdpPingBase
{
public:
    typedef std::map<WORD, UDPPING_TARGET> TargetMap;

    CUdpPingClient();
    virtual ~CUdpPingClient();                                      // 0

    // Closes the socket, waits for the receive thread and forgets all targets.
    void Close();
    // Adds a target (host name or dotted address); returns wID, 0xFFFF when wID exists.
    WORD AddTarget(WORD wID, const std::string& strHost, u_short wPort);
    void RemoveTarget(WORD wID);
    // Pings one target, or every target for UDPPING_ID_ANY.
    void PingTarget(WORD wID);

protected:
    void SendPing(UDPPING_TARGET* pTarget);
    // The target ID of a sender address, 0xFFFF when unknown.
    WORD FindIdByAddr(const sockaddr* pFrom);
    static unsigned __stdcall ThreadProc(void* pParam);
    int OnRecv();

public:
    HWND            m_hWnd;         // +8   receives the results
    UINT            m_uMsg;         // +C
    SOCKET          m_hSocket;      // +10
    HANDLE          m_hThread;      // +14
    BOOL            m_bHighRes;     // +18  QueryPerformanceFrequency succeeded
    LARGE_INTEGER   m_liFrequency;  // +20
    TargetMap       m_mapTargets;   // +28
};

C_ASSERT(FIELD_OFFSET(CUdpPingClient, m_hWnd) == 8);
C_ASSERT(FIELD_OFFSET(CUdpPingClient, m_hSocket) == 0x10);
C_ASSERT(FIELD_OFFSET(CUdpPingClient, m_bHighRes) == 0x18);
C_ASSERT(FIELD_OFFSET(CUdpPingClient, m_liFrequency) == 0x20);
C_ASSERT(FIELD_OFFSET(CUdpPingClient, m_mapTargets) == 0x28);

// UdpPing_SendReq / CUdpPingServer::SendAck: 16-byte "REQ" / "ACK" datagram with two DWORDs
// at +8 and +12.
int UdpPing_SendReq(SOCKET s, const sockaddr* pTo, DWORD dwLow, DWORD dwHigh);
int UdpPing_SendAck(SOCKET s, const sockaddr* pTo, DWORD dwLow, DWORD dwHigh);
