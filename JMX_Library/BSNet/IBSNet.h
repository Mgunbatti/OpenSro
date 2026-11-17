#pragma once

#include <winsock2.h>
#include <windows.h>

class CMsg;

// 148-byte parameter block passed by value to Initialize; its fields are filled in when
// CNetEngine is ported.
struct BSNET_INIT_PARAM
{
    DWORD dw[37];
};

// COM-style base of the network engine interface (methods are __stdcall).
class IUnknownInterface
{
public:
    virtual int __stdcall QueryInterface(DWORD dwIID, void** ppv) = 0;
    virtual int __stdcall AddRef() = 0;
};

// Network engine interface, vtable order of CNetEngine in the originals. Names come from the
// analysis; parameter types that are only known by count are DWORD for now.
class IBSNet : public IUnknownInterface
{
public:
    virtual int __stdcall Shutdown() = 0;                                               // 2
    virtual BOOL __stdcall Initialize(BSNET_INIT_PARAM param) = 0;                      // 3
    virtual DWORD __stdcall ConnectSockAddr(DWORD* pdwSessionID, const sockaddr* pAddr,
                                            int nAddrLen, DWORD dwParam) = 0;           // 4
    virtual int __stdcall Connect(const char* pszHost, u_short wPort, const char* pszBindIP,
                                  DWORD dwParam1, DWORD dwParam2) = 0;                  // 5
    virtual BOOL __stdcall ListenSockAddr(const sockaddr* pAddr) = 0;                   // 6
    virtual int __stdcall Listen(const char* pszIP, u_short wPort) = 0;                 // 7
    virtual int __stdcall RegisterProactor(DWORD dw1, void* pHandler, DWORD dw3, DWORD dw4) = 0;
    virtual int __stdcall vf09(DWORD dw1, DWORD dw2, DWORD dw3, DWORD dw4) = 0;          // 9
    virtual void* __stdcall StartProactorWorkers(DWORD dw1, DWORD dw2, DWORD dw3, DWORD dw4,
                                                 DWORD dw5, DWORD* pdw6) = 0;           // 10
    virtual void* __stdcall ProactorResume(DWORD dwProactor) = 0;                       // 11
    virtual void* __stdcall ProactorSuspend(DWORD dwProactor) = 0;                      // 12
    virtual int __stdcall UnregisterProactor(DWORD dwProactor) = 0;                     // 13
    virtual int __stdcall GetStatistics(DWORD dw1, DWORD dw2) = 0;                      // 14
    virtual int __stdcall GetSessionPeerAddress(DWORD dwSessionID, DWORD* pdw, ULONG* pIP,
                                                u_short* pwPort) = 0;                   // 15
    virtual int __stdcall GetProactor(DWORD dwProactor) = 0;                            // 16
    virtual char __stdcall StartHandshake(DWORD dwSessionID, BYTE* pb) = 0;             // 17
    virtual CMsg* __stdcall AllocMsg(BOOL bEncrypted) = 0;                              // 18
    virtual LONG __stdcall ReleaseMsg(CMsg* pMsg) = 0;                                  // 19
    virtual int __stdcall SendMsg(DWORD dwSessionID, CMsg* pMsg) = 0;                   // 20
    virtual int __stdcall BeginSessionFileTransfer(DWORD dw1, DWORD dw2, DWORD dw3, DWORD dw4,
                                                   DWORD dw5, DWORD dw6, DWORD dw7, DWORD dw8,
                                                   DWORD dw9, DWORD dw10, DWORD dw11) = 0;  // 21
    virtual int __stdcall SendFile(DWORD dwSessionID, void* p, DWORD dw3, DWORD dw4) = 0;  // 22
    virtual int __stdcall DisconnectSession(DWORD* pdwSessionID, SOCKET s) = 0;         // 23
    virtual int __stdcall CloseSessionTransferFile(DWORD dwSessionID, DWORD dw) = 0;    // 24
    virtual int __stdcall ExcludeSessionFromCleanup(DWORD dwSessionID) = 0;             // 25
    virtual int __stdcall GetSessionSocketState(DWORD dwSessionID) = 0;                 // 26
    virtual int __stdcall RemoveKeepAliveTarget(DWORD dw) = 0;                          // 27
    virtual void* __stdcall AddKeepAliveTarget(DWORD dw1, DWORD dw2, DWORD dw3, DWORD dw4,
                                               DWORD dw5) = 0;                          // 28
    virtual void __stdcall CloseAllFiles() = 0;                                         // 29
    virtual int __stdcall DumpMsgPool(BYTE b, DWORD dw) = 0;                            // 30
    virtual char __stdcall vf31(BYTE b) = 0;                                            // 31
    virtual DWORD __stdcall CleanupDisconnectedSessions() = 0;                          // 32
    virtual void* __stdcall GetMsgPool() = 0;                                           // 33
    virtual int __stdcall GetSessionLocalMacAddress(DWORD dwSessionID, DWORD* pdw) = 0; // 34
    virtual int __stdcall SetSecurityMode(BYTE bMode) = 0;                              // 35
    virtual int __stdcall PrintVersion() = 0;                                           // 36
    virtual BOOL __stdcall RegisterDatagramProactor(DWORD dw) = 0;                      // 37
    virtual int __stdcall OpenDatagramSocket(int nOpt) = 0;                             // 38
    virtual int __stdcall SendDatagram(DWORD dw1, DWORD dw2) = 0;                       // 39
    virtual ~IBSNet() {}                                                                // 40
};
