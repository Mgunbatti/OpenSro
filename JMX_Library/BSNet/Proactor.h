#pragma once

#include <winsock2.h>
#include <windows.h>
#include <list>

#include "BSLib/thread/ServiceObject.h"
#include "BSNet/BaseSocketFwd.h"

class CTask;
class CThreadManager;
class IBSNet;

// Proactor for Windows NT: one I/O completion port, the completion key is the CEventHandler.
class CProactorIOCP : public CServiceObjectSock
{
    BS_DECLARE_DYNCREATE(CProactorIOCP)                 // 0

public:
    CProactorIOCP();
    virtual ~CProactorIOCP();                           // 1

    virtual DWORD Run(volatile LONG* pbStop, int nIndex);   // 3
    // Posts one empty packet per running worker; a packet with key 0 ends a worker loop.
    virtual int SignalStop();                           // 4
    virtual BOOL AssociateHandle(HANDLE hHandle, CEventHandler* pHandler);     // 6
    virtual int CloseHandler(CEventHandler* pHandler, DWORD dwReason);         // 7
    // SignalStop, then closes the completion port.
    virtual void Close();                               // 9

protected:
    BOOL PostStopPacket();
    BOOL PostStopPackets(int nCount);

public:
    HANDLE  m_hPort;            // +4C
    DWORD   m_dwConcurrency;    // +50  twice the processor count
};

C_ASSERT(FIELD_OFFSET(CProactorIOCP, m_hPort) == 0x4C);
C_ASSERT(sizeof(CProactorIOCP) == 0x54);

// Proactor for Windows 9x: WSAWaitForMultipleEvents over up to 64 events. Wait index 0 is the
// stop event, 1 the wake-up event (set whenever the handle set changes); every socket takes a
// pair of slots (2/3, 4/5, ...) for its two overlapped blocks. Unused events are pooled.
class CProactorEvent : public CServiceObjectSock
{
    BS_DECLARE_DYNCREATE(CProactorEvent)                // 0

public:
    enum { MAX_EVENTS = 64 };

    CProactorEvent();
    virtual ~CProactorEvent();                          // 1

    virtual DWORD Run(volatile LONG* pbStop, int nIndex);   // 3
    virtual int SignalStop();                           // 4
    // pHandler must be a CSockStream; the handle itself is not used.
    virtual BOOL AssociateHandle(HANDLE hHandle, CEventHandler* pHandler);     // 6
    // Frees the slot pair of wait index dwIndex and closes the socket's session (reason 3).
    virtual int CloseHandler(CEventHandler* pHandler, DWORD dwIndex);          // 7

    SOCK_OVERLAPPED* GetSlotOverlapped(int nIndex);

protected:
    BOOL ClearSlot(int nIndex);
    BOOL RecountHandles();
    WSAEVENT TakeFreeEvent();

public:
    SOCK_OVERLAPPED*        m_apOverlapped[MAX_EVENTS]; // +4C
    WSAEVENT                m_ahEvents[MAX_EVENTS];     // +14C
    int                     m_nHandles;                 // +24C  non-NULL entries of m_ahEvents
    std::list<WSAEVENT>     m_listFreeEvents;           // +250
};

C_ASSERT(FIELD_OFFSET(CProactorEvent, m_apOverlapped) == 0x4C);
C_ASSERT(FIELD_OFFSET(CProactorEvent, m_ahEvents) == 0x14C);
C_ASSERT(FIELD_OFFSET(CProactorEvent, m_nHandles) == 0x24C);
C_ASSERT(FIELD_OFFSET(CProactorEvent, m_listFreeEvents) == 0x250);
C_ASSERT(sizeof(CProactorEvent) == 0x25C);

// Sets up a task registered by CNetEngine::RegisterProactor: message queue (completion port
// queue on NT, event queue otherwise), job created from pClass and initialised with
// (pTask, pParam). Returns the job's Initialize result, FALSE when GetVersionEx fails.
BOOL Proactor_Create(CRuntimeClass* pClass, CTask* pTask, CThreadManager* pThreadManager,
                     void* pParam);

// CProactorIOCP's close path: through the engine's listener when there is one, otherwise
// straight to the engine (session of the handler, dwReason).
int Proactor_NotifyHandlerClosed(IBSNet* pEngine, CEventHandler* pHandler, DWORD dwReason);

// Listener side of the close path: ignores sockets in state 2, otherwise closes the socket's
// session with dwReason. Returns 1 for ignored sockets, else the engine's result.
int Listener_ForwardHandlerClosed(CBaseSocket* pListener, CBaseSocket* pSock, DWORD dwReason);
