#pragma once

#include <winsock2.h>
#include <windows.h>
#include <list>
#include <string>

#include "BSLib/ChunkAllocator.h"
#include "BSLib/thread/SynchObject.h"
#include "BSNet/IBSNet.h"
#include "BSNet/KeepAlive.h"
#include "BSNet/MsgPool.h"
#include "BSNet/SessionTable.h"
#include "BSNet/SockDatagram.h"
#include "BSNet/SockPool.h"
#include "BSNet/SockStream.h"

class CMsg;
class CSockListener;

// The engine instance (0x4FAD50): set by every CNetEngine constructor, cleared by Shutdown.
extern IBSNet* g_pNetEngine;

// Set by CNetEngine::Shutdown (0x4FAD54) and never cleared: closing sockets then skip their
// state reset, sessions are not closed any more and ReleaseMsg does nothing.
extern BOOL g_bNetEngineShutdown;

// I/O model (0x4F8128) = OS_IsNotWinNT(), set by the CNetEngine constructor: 0 completion ports
// (NT), 1 event proactor (9x), -1 before the engine exists or when GetVersionEx failed.
extern int g_nNetIoModel;

// Task ID of the datagram peer process (0x4F841C, NETENGINE_TASK_DATAGRAM).
extern DWORD g_dwNetDatagramTaskID;

// The datagram socket and the task ID of its received datagrams (CNetEngine +0x338, 2011
// +0x428).
struct NETENGINE_DATAGRAM
{
    CSockDatagram*  pSocket;    // +0
    DWORD           dwTaskID;   // +4  set by NetEngine_RegisterPeerProcessClass / Initialize
};

// Published through 0x53AD80 by the engine's datagram member.
extern NETENGINE_DATAGRAM* g_pNetEngineDatagram;

// GetGlobal_53AD80 (0x49be70).
inline NETENGINE_DATAGRAM* NetEngine_GetDatagram()
{
    return g_pNetEngineDatagram;
}

// The engine's datagram member: the constructor (NetEngine_RegisterGlobalInstance) publishes it
// and clears it, the destructor returns the socket to the datagram pool.
class CNetEngineDatagram : public NETENGINE_DATAGRAM
{
public:
    CNetEngineDatagram();
    ~CNetEngineDatagram();

    // NetEngine_RegisterPeerProcessClass: stores dwTaskIDArg and registers the peer process
    // class of the I/O model as task g_dwNetDatagramTaskID.
    BOOL RegisterPeerProcessClass(DWORD dwTaskIDArg);
};

C_ASSERT(sizeof(CNetEngineDatagram) == 8);

typedef std::list<KEEPALIVE_ENTRY> KeepAliveList;

// The network engine (IBSNet). One instance per process: sockets, sessions, proactors and the
// maintainer reach it through g_pNetEngine.
class CNetEngine : public IBSNet
{
public:
    CNetEngine();
    virtual ~CNetEngine();                                                              // 40

    virtual HRESULT __stdcall QueryInterface(const GUID& riid, void** ppv);             // 0
    virtual ULONG __stdcall AddRef();                                                   // 1
    virtual int __stdcall Shutdown();                                                   // 2
    virtual BOOL __stdcall Initialize(BSNET_INIT_PARAM param);                          // 3
    virtual DWORD __stdcall ConnectSockAddr(const sockaddr* pTo, const sockaddr* pBind,
                                            DWORD dwTaskID, int nKeepAlive);            // 4
    virtual DWORD __stdcall Connect(const char* pszHost, u_short wPort, const char* pszBindIP,
                                    DWORD dwTaskID, int nKeepAlive);                    // 5
    virtual BOOL __stdcall ListenSockAddr(const sockaddr* pAddr);                       // 6
    virtual BOOL __stdcall Listen(const char* pszIP, u_short wPort);                    // 7
    virtual BOOL __stdcall RegisterProactor(DWORD dwID, CTask* pTask, CRuntimeClass* pClass,
                                            void* pParam);                              // 8
    virtual int __stdcall vf09(DWORD dw1, DWORD dw2, DWORD dw3, DWORD dw4);              // 9
    virtual BOOL __stdcall StartProactorWorkers(DWORD dwID, int nCount, int nPriority,
                                                unsigned nStackSize, DWORD dwCreateFlag,
                                                THREAD_AFFINITY* pAffinity);            // 10
    virtual BOOL __stdcall ProactorResume(DWORD dwID);                                  // 11
    virtual BOOL __stdcall ProactorSuspend(DWORD dwID);                                 // 12
    virtual BOOL __stdcall UnregisterProactor(DWORD dwID);                              // 13
    virtual void __stdcall GetStatistics(DWORD dwFlags, NETENGINE_STATISTICS* pStats);  // 14
    virtual BOOL __stdcall GetSessionPeerAddress(DWORD dwSessionID, std::string* pstrIP,
                                                 ULONG* pulIP, u_short* pwPort);       // 15
    virtual CTask* __stdcall GetProactor(DWORD dwID);                                   // 16
    virtual BOOL __stdcall StartHandshake(DWORD dwSessionID,
                                          SECURITY_HANDSHAKE_PARAM* pParam);            // 17
    virtual CMsg* __stdcall AllocMsg(BOOL bEncrypted);                                  // 18
    virtual LONG __stdcall ReleaseMsg(CMsg* pMsg);                                      // 19
    virtual int __stdcall SendMsg(DWORD dwSessionID, CMsg* pMsg);                       // 20
    virtual BOOL __stdcall BeginSessionFileTransfer(DWORD dwSessionID, std::string strPath,
                                                    DWORD dwSize, DWORD dwOffset,
                                                    int nFileType);                     // 21
    virtual BOOL __stdcall SendFile(DWORD dwSessionID, const std::string& strPath,
                                    DWORD dwOffset, int nFileType);                     // 22
    virtual int __stdcall DisconnectSession(DWORD dwSessionID, DWORD dwReason);         // 23
    virtual int __stdcall CloseSessionTransferFile(DWORD dwSessionID, DWORD dwFileHandle);  // 24
    virtual void __stdcall ExcludeSessionFromCleanup(DWORD dwSessionID);                // 25
    virtual int __stdcall GetSessionSocketState(DWORD dwSessionID);                     // 26
    virtual BOOL __stdcall RemoveKeepAliveTarget(DWORD dwSessionID);                    // 27
    virtual void __stdcall AddKeepAliveTarget(const char* pszHost, u_short wPort,
                                              const char* pszBindIP, DWORD dwTaskID,
                                              DWORD dwSessionID);                       // 28
    virtual void __stdcall CloseAllFiles();                                             // 29
    virtual void __stdcall DumpMsgPool(BYTE bFlags, DWORD dwReserved);                  // 30
    virtual void __stdcall SetDebugAsserted(BYTE bAsserted);                            // 31
    virtual void __stdcall CleanupDisconnectedSessions();                               // 32
    virtual CMsgPool* __stdcall GetMsgPool();                                           // 33
    virtual BOOL __stdcall GetSessionLocalMacAddress(DWORD dwSessionID, BYTE* pMac);    // 34
    virtual void __stdcall SetSecurityMode(BYTE bMode);                                 // 35
    virtual void __stdcall PrintVersion();                                              // 36
    virtual BOOL __stdcall RegisterDatagramProactor(DWORD dwTaskID);                    // 37
    virtual int __stdcall OpenDatagramSocket(WORD wPort);                               // 38
    virtual int __stdcall SendDatagram(const sockaddr* pTo, CMsg* pMsg);                // 39
#ifdef JMX_FRAMEWORK_2011
    // NetEngine_SetWinInetMaxConnections with the WinINet key; not called in AgentServer.
    virtual bool __stdcall SetWinInetMaxConnections(DWORD dwUnused);                    // 41
#endif

    // keep-alive list (KeepAlive.cpp), all under m_csKeepAlive
    void AddKeepAliveEntry(const KEEPALIVE_ENTRY* pEntry);
    // Connects every entry without a session (with the lock held); logs an entry once it
    // failed more than 15 times in a row.
    void TryKeepAliveConnect();
    // A connected session closed: its entry gets reconnected by the next TryKeepAliveConnect.
    void ClearKeepAliveSession(DWORD dwSessionID);

public:
    // Offsets of 2008; the 2011 framework (bigger engine parameters and chunk allocators) in
    // the C_ASSERTs below.
    DWORD                   m_dw4;              // +4    0
    BSNET_INIT_PARAM        m_Config;           // +8
    CCriticalSectionBS      m_csKeepAlive;      // +9C   keep-alive list, session cleanup
    CCriticalSectionBS      m_csMsg;            // +D8   AllocMsg / ReleaseMsg
    CSockListener*          m_pListener;        // +114
    CSockStreamPool         m_ConnectPool;      // +118
    CSockStreamPool         m_AcceptPool;       // +194
    CSockDatagramPool       m_DatagramPool;     // +210
    CSessionTable           m_SessionTable;     // +28C
    CNetEngineDatagram      m_Datagram;         // +338
    CMsgPool                m_MsgPool;          // +340
    KeepAliveList           m_listKeepAlive;    // +3C8
};

#ifdef JMX_FRAMEWORK_2011
// AgentServer 0x46b510
C_ASSERT(FIELD_OFFSET(CNetEngine, m_Config) == 0x8);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_csKeepAlive) == 0x11C);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_csMsg) == 0x158);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_pListener) == 0x194);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_ConnectPool) == 0x198);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_AcceptPool) == 0x230);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_DatagramPool) == 0x2C8);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_SessionTable) == 0x360);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_Datagram) == 0x428);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_MsgPool) == 0x430);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_listKeepAlive) == 0x4D4);
C_ASSERT(sizeof(CNetEngine) == 0x4E0);

// Body of CNetEngine::SetWinInetMaxConnections (AgentServer 0x46d380) with the key as parameter:
// under HKEY_CURRENT_USER\pszSubKey "MaxConnectionsPerServer" and "MaxConnectionsPer1_OServer"
// (sic, letter O) are set to 100 unless they are 100 already. False when the key cannot be
// opened or a value cannot be written.
bool NetEngine_SetWinInetMaxConnections(const char* pszSubKey);
#else
C_ASSERT(FIELD_OFFSET(CNetEngine, m_Config) == 0x8);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_csKeepAlive) == 0x9C);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_csMsg) == 0xD8);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_pListener) == 0x114);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_ConnectPool) == 0x118);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_AcceptPool) == 0x194);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_DatagramPool) == 0x210);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_SessionTable) == 0x28C);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_Datagram) == 0x338);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_MsgPool) == 0x340);
C_ASSERT(FIELD_OFFSET(CNetEngine, m_listKeepAlive) == 0x3C8);
C_ASSERT(sizeof(CNetEngine) == 0x3D4);
#endif

// ---------------------------------------------------------------------------------------------
// What the sockets, proactors and the maintainer read from the engine. They take the IBSNet
// pointer and rely only on CNetEngine's layout.

inline CNetEngine* NetEngine_Cast(IBSNet* pEngine)
{
    return static_cast<CNetEngine*>(pEngine);
}

// +0x8: SECURITY_MODE_* (SetSecurityMode).
inline BYTE NetEngine_GetSecurityMode(IBSNet* pEngine)
{
    return NetEngine_Cast(pEngine)->m_Config.bSecurityMode;
}

// +0x10: number of AcceptEx calls kept pending (at least 10 are used).
inline int NetEngine_GetAcceptCount(IBSNet* pEngine)
{
    return NetEngine_Cast(pEngine)->m_Config.nAcceptCount;
}

// +0x14: an AcceptEx socket connected longer than this (seconds) is dropped; -1 disables the
// check.
inline DWORD NetEngine_GetMaxConnectTime(IBSNet* pEngine)
{
    return NetEngine_Cast(pEngine)->m_Config.dwMaxConnectTime;
}

inline NETENGINE_SOCKET_CONFIG* NetEngine_GetSocketConfig(IBSNet* pEngine, int nRole)
{
    return &NetEngine_Cast(pEngine)->m_Config.aSocket[nRole];
}

// +0x68: task of the accepted sockets' sessions.
inline DWORD NetEngine_GetAcceptSessionParam(IBSNet* pEngine)
{
    return NetEngine_Cast(pEngine)->m_Config.aSocket[0].dwSessionParam;
}

// +0x114: the listener. When it exists, handler closes go through it.
inline CSockListener* NetEngine_GetListener(IBSNet* pEngine)
{
    return NetEngine_Cast(pEngine)->m_pListener;
}

inline CSockStreamPool* NetEngine_GetConnectPool(IBSNet* pEngine)
{
    return &NetEngine_Cast(pEngine)->m_ConnectPool;
}

inline CSockStreamPool* NetEngine_GetAcceptPool(IBSNet* pEngine)
{
    return &NetEngine_Cast(pEngine)->m_AcceptPool;
}

inline CSockDatagramPool* NetEngine_GetDatagramPool(IBSNet* pEngine)
{
    return &NetEngine_Cast(pEngine)->m_DatagramPool;
}

// Engine slot 23 (+0x5C).
inline int NetEngine_CloseSession(DWORD dwSessionID, DWORD dwReason)
{
    return g_pNetEngine->DisconnectSession(dwSessionID, dwReason);
}

// Engine slot 19 (+0x4C): returns a CMsg to the pool.
inline void NetEngine_ReleaseMsg(LPOVERLAPPED pOverlapped)
{
    g_pNetEngine->ReleaseMsg((CMsg*)pOverlapped);
}

// Engine jobs and session creation as the sockets and the maintainer call them.
// 0x481cb0: CSessionTable::ReapExpiredSessions of the engine.
void NetEngine_ReapExpiredSessions(IBSNet* pEngine, DWORD dwTimeout);
// KeepAlive_TryConnect (0x481a30).
void NetEngine_KeepAliveTryConnect(IBSNet* pEngine);
// CreateNewSession_wrap (0x46dce0): CSessionTable::CreateNewSession of the engine.
DWORD NetEngine_CreateNewSession(IBSNet* pEngine, CSockStream* pSock, DWORD dwTaskID);
