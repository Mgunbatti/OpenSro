#pragma once

#include <winsock2.h>
#include <windows.h>

#include "BSLib/thread/ServiceObject.h"
#include "BSNet/BaseSocket.h"

// Peer (datagram) process for Windows NT: completion port with a fixed concurrency of 2. The
// worker loops ignore the stop flag and end on a packet with key 0.
class CPeerProcessIOCP : public CServiceObjectSock
{
    BS_DECLARE_DYNCREATE(CPeerProcessIOCP)              // 0

public:
    CPeerProcessIOCP();
    virtual ~CPeerProcessIOCP();                        // 1

    virtual BOOL Initialize(void* pOwner, void* pParam);    // 2
    virtual DWORD Run(volatile LONG* pbStop, int nIndex);   // 3
    // Posts one empty packet per running worker.
    virtual int SignalStop();                           // 4
    virtual BOOL AssociateHandle(HANDLE hHandle, CEventHandler* pHandler);     // 6
    virtual int CloseHandler(CEventHandler* pHandler, DWORD dwParam);          // 7
    virtual void Close();                               // 9

protected:
    BOOL PostStopPacket();
    BOOL PostStopPackets(int nCount);

public:
    HANDLE          m_hPort;        // +4C
    volatile LONG   m_nWorkers;     // +50  threads inside Run (m_nActive stays 0)
};

C_ASSERT(FIELD_OFFSET(CPeerProcessIOCP, m_hPort) == 0x4C);
C_ASSERT(sizeof(CPeerProcessIOCP) == 0x54);

// Peer process for Windows 9x: waits on the datagram socket's event and sends the queued
// datagrams with completion routines (run while the loop sleeps alertably). Wait index 0 is
// the stop event, 1 the wake-up event, 2 the socket.
class CPeerProcessOverlapped : public CServiceObjectSock
{
    BS_DECLARE_DYNCREATE(CPeerProcessOverlapped)        // 0

public:
    enum { MAX_EVENTS = 64 };

    CPeerProcessOverlapped();
    virtual ~CPeerProcessOverlapped();                  // 1

    // Also replaces the send queue with a new, started CQueMT9x.
    virtual BOOL Initialize(void* pOwner, void* pParam);    // 2
    virtual DWORD Run(volatile LONG* pbStop, int nIndex);   // 3
    // Stops the send queue, sets the stop event and returns the queued messages to the
    // engine.
    virtual int SignalStop();                           // 4
    // Takes pHandler's overlapped block 1 (CSockDatagram::GetOverlapped) as wait slot 2.
    virtual BOOL AssociateHandle(HANDLE hHandle, CEventHandler* pHandler);     // 6
    virtual int CloseHandler(CEventHandler* pHandler, DWORD dwParam);          // 7
    virtual IQue<CMsg>* GetSendQueue();                 // 8

    SOCK_OVERLAPPED* GetSlotOverlapped(int nIndex);

protected:
    BOOL RecountHandles();

public:
    IQue<CMsg>*         m_pSendQueue;               // +4C
    WSAEVENT            m_ahEvents[MAX_EVENTS];     // +50
    SOCK_OVERLAPPED*    m_apOverlapped[MAX_EVENTS]; // +150
    int                 m_nHandles;                 // +250
};

C_ASSERT(FIELD_OFFSET(CPeerProcessOverlapped, m_pSendQueue) == 0x4C);
C_ASSERT(FIELD_OFFSET(CPeerProcessOverlapped, m_ahEvents) == 0x50);
C_ASSERT(FIELD_OFFSET(CPeerProcessOverlapped, m_apOverlapped) == 0x150);
C_ASSERT(FIELD_OFFSET(CPeerProcessOverlapped, m_nHandles) == 0x250);
C_ASSERT(sizeof(CPeerProcessOverlapped) == 0x254);

// Completion routine of the datagram sends: hEvent carries the sending socket.
void CALLBACK PeerProcess_SendCompletion(DWORD dwError, DWORD cbTransferred,
                                         LPWSAOVERLAPPED lpOverlapped, DWORD dwFlags);
