#pragma once

#include <winsock2.h>
#include <windows.h>
#include <list>

#include "BSNet/BaseSocket.h"

class CSockStream;
class IBSNet;

// Listening socket. With completion ports it keeps a number of AcceptEx calls pending on
// pooled CSockStream objects; with the event proactor it accept()s on demand.
class CSockListener : public CBaseSocket
{
public:
    CSockListener();
    virtual ~CSockListener();                                                   // 0

    // AssociateHandle only (accepts are posted by ListenAndPostAccepts).
    virtual BOOL AttachToProactor(CServiceObjectSock* pProactor, HANDLE hHandle); // 1
    // An AcceptEx completed (completion ports) or a connection is waiting (event model).
    virtual int OnIOCompletion(DWORD dwBytes, LPOVERLAPPED pOverlapped);         // 2
    virtual int Close();                                                        // 4

    // TCP socket, SO_REUSEADDR, bind. Resets the pending accept count.
    BOOL CreateAndBind(const sockaddr* pAddr);
    // listen, attach to the proactor, post the initial accepts (completion ports).
    BOOL ListenAndPostAccepts(CServiceObjectSock* pProactor);
    // Takes one socket from the accept pool and posts AcceptEx on the listener; on failure the
    // socket goes back to the pool. TRUE when posted.
    static BOOL PostOneAccept(IBSNet* pEngine);
    // A closed accepted socket gets a new AcceptEx; FALSE when that fails.
    BOOL RepostAcceptForSocket(CSockStream* pSock);

protected:
    void ReplenishAcceptsLocked();
    int CreateSessionForAccepted(CSockStream* pSock);

public:
    int                     m_nBacklog;     // +50  0x7FFFFFFF
    std::list<CSockStream*> m_list54;       // +54  not used by the listener code
    CCriticalSectionBS      m_csAccept;     // +60  guards the pending accept count

    // Number of AcceptEx calls pending (0x4F9D0C).
    static int ms_nPendingAccepts;
};

C_ASSERT(FIELD_OFFSET(CSockListener, m_nBacklog) == 0x50);
C_ASSERT(FIELD_OFFSET(CSockListener, m_list54) == 0x54);
C_ASSERT(FIELD_OFFSET(CSockListener, m_csAccept) == 0x60);
C_ASSERT(sizeof(CSockListener) == 0x9C);

// The proactor's close path: sockets of kind SOCK_KIND_LISTENER are ignored (returns 1),
// otherwise the socket's session is closed with dwReason.
int Listener_ForwardHandlerClosed(CSockListener* pListener, CBaseSocket* pSock, DWORD dwReason);
