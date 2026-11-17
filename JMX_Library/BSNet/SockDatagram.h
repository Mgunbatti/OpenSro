#pragma once

#include <winsock2.h>
#include <windows.h>

#include "BSNet/BaseSocket.h"
#include "BSNet/QueMT.h"
#include "BSNet/security/BlowFish.h"

class CMsg;
class CTask;
struct NETENGINE_DATAGRAM;

class __single_inheritance CSockDatagram;

// UDP socket of the engine. Received datagrams are framed like stream packets (no security);
// sends go through WSASendTo (completion ports) or the peer process's send queue (event model).
class CSockDatagram : public CBaseSocket
{
public:
    typedef int (CSockDatagram::*PFN_IO)(LPOVERLAPPED pOverlapped, DWORD dwBytes);
    typedef int (CSockDatagram::*PFN_SEND)(const sockaddr* pTo, CMsg* pMsg);

    CSockDatagram();
    virtual ~CSockDatagram();                                                   // 0

    // Takes the proactor's send queue, AssociateHandle, first WSARecvFrom.
    virtual BOOL AttachToProactor(CServiceObjectSock* pProactor, HANDLE hHandle); // 1
    virtual int OnIOCompletion(DWORD dwBytes, LPOVERLAPPED pOverlapped);         // 2
    // Returns the queued sends, closes the socket and resets the object. 0x8009 for NULL.
    virtual int Close();                                                        // 4
    // Overloads in reverse slot order: GetOverlapped() is slot 6, GetOverlapped(int) slot 5.
    virtual SOCK_OVERLAPPED* GetOverlapped();                                    // 6
    // The receive block for type 1, NULL otherwise.
    virtual SOCK_OVERLAPPED* GetOverlapped(int nType);                           // 5

    void ResetState();

    // CSockDatagram_OpenAndBind: UDP socket bound to INADDR_ANY:wPort (SO_REUSEADDR for a
    // non-zero port), event (event model) and callbacks.
    BOOL Open(WORD wPort);
    void SetTask(CTask* pTask);

    // Session_InvokeCallback248: sends through the installed send function. The socket owns
    // the message afterwards (released when the send completed or failed).
    int Send(const sockaddr* pTo, CMsg* pMsg);

    // send functions (+0xF8)
    int SendTo(const sockaddr* pTo, CMsg* pMsg);
    int PostWSASendTo(const sockaddr* pTo, CMsg* pMsg);

    // completion callbacks (+0xCC)
    int OnRecvComplete(LPOVERLAPPED pOverlapped, DWORD dwBytes);
    int ReleaseSentMsg(LPOVERLAPPED pOverlapped, DWORD dwBytes);

protected:
    BOOL CreateEventIfOverlapped();
    void InitCallbacks();
    int ParseRecvHeader(WORD* pwSize);
    int DispatchRecvMsg(CMsg* pMsg);
    int PostRecvFrom();

public:
    CSockIoBlock        m_ovRecv;           // +50  SOCK_IO_RECV
    CMsg*               m_pRecvMsg;         // +94
    CTask*              m_pTask;            // +98  receives the datagrams
    IQue<CMsg>*         m_pSendQueue;       // +9C  the peer process's queue (event model)
    int                 m_nRecvMsgOffset;   // +A0
    volatile LONG       m_nPendingSends;    // +A4
    BYTE                m_bOpened;          // +A8
    CBlowFish           m_BlowFish;         // +AC  not used
    sockaddr_in         m_FromAddr;         // +B8  bind address, then the last sender
    int                 m_nFromLen;         // +C8
    PFN_IO              m_apfnIo[10];       // +CC  indexed by 2 * type + dw18
    void*               m_pfnF4;            // +F4  not used
    PFN_SEND            m_pfnSend;          // +F8
};

C_ASSERT(sizeof(CSockDatagram::PFN_IO) == 4);
C_ASSERT(FIELD_OFFSET(CSockDatagram, m_ovRecv) == 0x50);
C_ASSERT(FIELD_OFFSET(CSockDatagram, m_pRecvMsg) == 0x94);
C_ASSERT(FIELD_OFFSET(CSockDatagram, m_pSendQueue) == 0x9C);
C_ASSERT(FIELD_OFFSET(CSockDatagram, m_nPendingSends) == 0xA4);
C_ASSERT(FIELD_OFFSET(CSockDatagram, m_BlowFish) == 0xAC);
C_ASSERT(FIELD_OFFSET(CSockDatagram, m_FromAddr) == 0xB8);
C_ASSERT(FIELD_OFFSET(CSockDatagram, m_apfnIo) == 0xCC);
C_ASSERT(FIELD_OFFSET(CSockDatagram, m_pfnSend) == 0xF8);
C_ASSERT(sizeof(CSockDatagram) == 0xFC);

// CNetEngine_CreateDatagramSocket: takes a datagram socket from the engine pool, opens it on
// wPort, sets the message task dwTaskID of pDatagram, attaches it to the datagram peer process
// (g_dwNetDatagramTaskID) and starts one worker. FALSE on failure.
BOOL NetEngine_CreateDatagramSocket(NETENGINE_DATAGRAM* pDatagram, WORD wPort);

// Session_QueueDatagramAndNotify: sends pMsg to pTo on the engine's datagram socket. Returns
// the send result; when the send failed but left an extra reference, it is released.
int NetEngine_SendDatagram(NETENGINE_DATAGRAM* pDatagram, const sockaddr* pTo, CMsg* pMsg);
