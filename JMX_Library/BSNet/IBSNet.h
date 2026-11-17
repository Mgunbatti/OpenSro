#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>

#include "BSNet/Maintainer.h"
#include "BSNet/NetEngineLog.h"

class CMsg;
class CMsgPool;
class CTask;
struct CRuntimeClass;
struct SECURITY_HANDSHAKE_PARAM;
struct THREAD_AFFINITY;

// Interface ID checked by CNetEngine::QueryInterface: {A4AF69F3-C8B3-4242-9A96-078FF27EB1F8}.
extern const GUID IID_IBSNet;

// Receiver of CNetEngine::DumpMsgPool: one call per opcode of the messages handed out (the inner
// opcode for 0x2209 relay messages), then (0xFFFF, total).
typedef void (__cdecl* PFN_NETENGINE_DUMPMSGPOOL)(DWORD dwOpcode, DWORD dwCount);

// Bits of BSNET_INIT_PARAM::dwFlags.
enum
{
    BSNET_FLAG_SERVER           = 0x01,     // listener and its proactor (Listen works only then)
    BSNET_FLAG_LARGE            = 0x10,     // server: 5000 session slots instead of 50
};

// Per stream role settings (BSNET_INIT_PARAM::aSocket, CNetEngine +0x4C), indexed by
// CBaseSocket::m_nRole (0 accepted, 1 connected).
struct NETENGINE_SOCKET_CONFIG
{
    DWORD   dw0;                // +0   1
    DWORD   dw4;                // +4   1: the original calls an empty hook on connect/accept
    DWORD   dw8[4];             // +8   0, 0, 1, 1
    LONG    nSendQueueLimit;    // +18  -1: unlimited
    DWORD   dwSessionParam;     // +1C  task that receives the sessions' messages and events
    DWORD   dwSendMode;         // +20  0: one WSASend per message, 1: queued and coalesced
};

C_ASSERT(sizeof(NETENGINE_SOCKET_CONFIG) == 36);

// Default dump receiver (0x43ee70): does nothing.
void __cdecl NetEngine_DefaultDumpMsgPool(DWORD dwOpcode, DWORD dwCount);

// The 148-byte block passed by value to CNetEngine::Initialize and copied to CNetEngine +8. The
// constructor (0x43ed20) sets the defaults the engine starts with.
struct BSNET_INIT_PARAM
{
    BSNET_INIT_PARAM()
    {
        bSecurityMode = 1;
        bWinsockLow = 2;
        bWinsockHigh = 2;
        dwFlags = 0;
        nAcceptCount = 0;
        dwMaxConnectTime = 60;
        dwFileCacheLimit = 0;
        nSendFileType = -1;
        nRecvFileType = -1;
        pfnLog = NULL;
        pfnDumpMsgPool = NetEngine_DefaultDumpMsgPool;
        for (int i = 0; i < 2; ++i)
        {
            NETENGINE_SOCKET_CONFIG& c = aSocket[i];
            c.dw0 = 1;
            c.dw4 = 1;
            c.dw8[0] = 0;
            c.dw8[1] = 0;
            c.dw8[2] = 1;
            c.dw8[3] = 1;
            c.nSendQueueLimit = -1;
            c.dwSessionParam = 0;
            c.dwSendMode = 1;
        }
    }

    BYTE                        bSecurityMode;      // +0   SECURITY_MODE_*
    // Winsock version for WSAStartup. Initialize reads it from the engine before it copies the
    // new block, so only the constructor defaults (2.2) take effect (NET-001).
    BYTE                        bWinsockLow;        // +1
    BYTE                        bWinsockHigh;       // +2
    BYTE                        b3;                 // +3   not initialised
    DWORD                       dwFlags;            // +4   BSNET_FLAG_*
    int                         nAcceptCount;       // +8   pending AcceptEx calls (at least 10)
    DWORD                       dwMaxConnectTime;   // +C   seconds; -1 disables the check
    DWORD                       dwFileCacheLimit;   // +10  CFileSystem cache, at most half the
                                                    //      available physical memory
    int                         nSendFileType;      // +14  FILE_TYPE_* when SendFile gets -1
    int                         nRecvFileType;      // +18  FILE_TYPE_* when
                                                    //      BeginSessionFileTransfer gets -1
    PFN_NETENGINE_LOG           pfnLog;             // +1C  NetEngineLog receiver
    PFN_NETENGINE_DUMPMSGPOOL   pfnDumpMsgPool;     // +20  replaces the pool's receiver if set
    MAINTAINER_CONFIG           Maintainer;         // +24  maintainer only when dwFlags != 0
    NETENGINE_SOCKET_CONFIG     aSocket[2];         // +44
    DWORD                       dw8C;               // +8C  not initialised
    DWORD                       dw90;               // +90  not initialised
};

C_ASSERT(FIELD_OFFSET(BSNET_INIT_PARAM, dwFlags) == 0x4);
C_ASSERT(FIELD_OFFSET(BSNET_INIT_PARAM, pfnLog) == 0x1C);
C_ASSERT(FIELD_OFFSET(BSNET_INIT_PARAM, Maintainer) == 0x24);
C_ASSERT(FIELD_OFFSET(BSNET_INIT_PARAM, aSocket) == 0x44);
C_ASSERT(sizeof(BSNET_INIT_PARAM) == 0x94);

// Bits of the GetStatistics flags.
enum
{
    BSNET_STAT_MSG_POOL         = 0x01,
    BSNET_STAT_SOCKET_POOL      = 0x02,
    BSNET_STAT_SESSIONS         = 0x04,
    BSNET_STAT_FILES            = 0x08,
    BSNET_STAT_ACCEPT           = 0x10,     // only with a listener
    BSNET_STAT_SESSION_LIST     = 0x20,
};

// Output of GetStatistics; only the groups selected by the flags are written.
struct NETENGINE_STATISTICS
{
    DWORD   dwMsgTotal;             // +0
    DWORD   dwMsgUsed;              // +4
    DWORD   dwMsgFree;              // +8
    DWORD   dw0C[2];                // +C
    DWORD   dwSocketTotal;          // +14  accepted socket pool only (NET-002)
    DWORD   dwSocketUsed;           // +18
    DWORD   dwSocketFree;           // +1C
    DWORD   dw20[2];                // +20
    DWORD   dwSessionTotal;         // +28  session pool capacity
    DWORD   dwSessionUsed;          // +2C
    DWORD   dwSessionFree;          // +30
    DWORD   dw34[2];                // +34
    DWORD   dwOpenFiles;            // +3C
    DWORD   dwFileUsers;            // +40
    DWORD   dwFileCacheLimit;       // +44
    DWORD   dwFileCacheUsed;        // +48
    float   fFileCacheRatio;        // +4C
    DWORD   dwAcceptCount;          // +50  configured pending accepts
    DWORD   dwPendingAccepts;       // +54
    DWORD   dw58[4];                // +58
    DWORD   dwLinkedSessions;       // +68
};

C_ASSERT(FIELD_OFFSET(NETENGINE_STATISTICS, dwSocketTotal) == 0x14);
C_ASSERT(FIELD_OFFSET(NETENGINE_STATISTICS, dwSessionTotal) == 0x28);
C_ASSERT(FIELD_OFFSET(NETENGINE_STATISTICS, dwOpenFiles) == 0x3C);
C_ASSERT(FIELD_OFFSET(NETENGINE_STATISTICS, dwAcceptCount) == 0x50);
C_ASSERT(FIELD_OFFSET(NETENGINE_STATISTICS, dwLinkedSessions) == 0x68);

// COM-style base of the network engine interface (methods are __stdcall).
class IUnknownInterface
{
public:
    virtual HRESULT __stdcall QueryInterface(const GUID& riid, void** ppv) = 0;    // 0
    virtual ULONG __stdcall AddRef() = 0;                                           // 1
};

// Network engine interface, vtable order of CNetEngine in the originals. Several slots return
// whatever the original left in eax; they are void here.
class IBSNet : public IUnknownInterface
{
public:
    virtual int __stdcall Shutdown() = 0;                                               // 2
    virtual BOOL __stdcall Initialize(BSNET_INIT_PARAM param) = 0;                      // 3
    // Returns the new session ID, 0 on failure. nKeepAlive == 1 also adds a keep-alive target.
    virtual DWORD __stdcall ConnectSockAddr(const sockaddr* pTo, const sockaddr* pBind,
                                            DWORD dwTaskID, int nKeepAlive) = 0;        // 4
    virtual DWORD __stdcall Connect(const char* pszHost, u_short wPort, const char* pszBindIP,
                                    DWORD dwTaskID, int nKeepAlive) = 0;                // 5
    virtual BOOL __stdcall ListenSockAddr(const sockaddr* pAddr) = 0;                   // 6
    virtual BOOL __stdcall Listen(const char* pszIP, u_short wPort) = 0;                // 7
    // Registers pTask, or a new task running a job created from pClass, under dwID.
    virtual BOOL __stdcall RegisterProactor(DWORD dwID, CTask* pTask, CRuntimeClass* pClass,
                                            void* pParam) = 0;                          // 8
    virtual int __stdcall vf09(DWORD dw1, DWORD dw2, DWORD dw3, DWORD dw4) = 0;          // 9
    virtual BOOL __stdcall StartProactorWorkers(DWORD dwID, int nCount, int nPriority,
                                                unsigned nStackSize, DWORD dwCreateFlag,
                                                THREAD_AFFINITY* pAffinity) = 0;        // 10
    virtual BOOL __stdcall ProactorResume(DWORD dwID) = 0;                              // 11
    virtual BOOL __stdcall ProactorSuspend(DWORD dwID) = 0;                             // 12
    virtual BOOL __stdcall UnregisterProactor(DWORD dwID) = 0;                          // 13
    virtual void __stdcall GetStatistics(DWORD dwFlags, NETENGINE_STATISTICS* pStats) = 0;  // 14
    virtual BOOL __stdcall GetSessionPeerAddress(DWORD dwSessionID, std::string* pstrIP,
                                                 ULONG* pulIP, u_short* pwPort) = 0;   // 15
    virtual CTask* __stdcall GetProactor(DWORD dwID) = 0;                               // 16
    virtual BOOL __stdcall StartHandshake(DWORD dwSessionID,
                                          SECURITY_HANDSHAKE_PARAM* pParam) = 0;        // 17
    virtual CMsg* __stdcall AllocMsg(BOOL bEncrypted) = 0;                              // 18
    virtual LONG __stdcall ReleaseMsg(CMsg* pMsg) = 0;                                  // 19
    virtual int __stdcall SendMsg(DWORD dwSessionID, CMsg* pMsg) = 0;                   // 20
    // Prepares the session to receive a file of dwSize bytes into strPath.
    virtual BOOL __stdcall BeginSessionFileTransfer(DWORD dwSessionID, std::string strPath,
                                                    DWORD dwSize, DWORD dwOffset,
                                                    int nFileType) = 0;                 // 21
    virtual BOOL __stdcall SendFile(DWORD dwSessionID, const std::string& strPath,
                                    DWORD dwOffset, int nFileType) = 0;                 // 22
    virtual int __stdcall DisconnectSession(DWORD dwSessionID, DWORD dwReason) = 0;     // 23
    virtual int __stdcall CloseSessionTransferFile(DWORD dwSessionID,
                                                   DWORD dwFileHandle) = 0;             // 24
    virtual void __stdcall ExcludeSessionFromCleanup(DWORD dwSessionID) = 0;            // 25
    virtual int __stdcall GetSessionSocketState(DWORD dwSessionID) = 0;                 // 26
    virtual BOOL __stdcall RemoveKeepAliveTarget(DWORD dwSessionID) = 0;                // 27
    virtual void __stdcall AddKeepAliveTarget(const char* pszHost, u_short wPort,
                                              const char* pszBindIP, DWORD dwTaskID,
                                              DWORD dwSessionID) = 0;                   // 28
    virtual void __stdcall CloseAllFiles() = 0;                                         // 29
    virtual void __stdcall DumpMsgPool(BYTE bFlags, DWORD dwReserved) = 0;              // 30
    virtual void __stdcall SetDebugAsserted(BYTE bAsserted) = 0;                        // 31
    virtual void __stdcall CleanupDisconnectedSessions() = 0;                           // 32
    virtual CMsgPool* __stdcall GetMsgPool() = 0;                                       // 33
    virtual BOOL __stdcall GetSessionLocalMacAddress(DWORD dwSessionID, BYTE* pMac) = 0;   // 34
    virtual void __stdcall SetSecurityMode(BYTE bMode) = 0;                             // 35
    virtual void __stdcall PrintVersion() = 0;                                          // 36
    virtual BOOL __stdcall RegisterDatagramProactor(DWORD dwTaskID) = 0;                // 37
    virtual int __stdcall OpenDatagramSocket(WORD wPort) = 0;                           // 38
    virtual int __stdcall SendDatagram(const sockaddr* pTo, CMsg* pMsg) = 0;            // 39
    virtual ~IBSNet() {}                                                                // 40
};
