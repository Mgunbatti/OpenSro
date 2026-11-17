#pragma once

#include <windows.h>

class CServiceObjectSock;

// Interface of the objects a proactor dispatches to (CBaseSocket and the socket classes).
// Only the destructor has code here.
class CEventHandler
{
public:
    CEventHandler() {}
    virtual ~CEventHandler() {}                                     // 0

    // Registers hHandle with the proactor (pProactor->AssociateHandle(hHandle, this)) and
    // starts the first I/O.
    virtual BOOL AttachToProactor(CServiceObjectSock* pProactor, HANDLE hHandle) = 0;  // 1
    // Called by the proactor worker loops when an overlapped operation of this handler
    // completed. A non-zero result means the operation is finished (the IOCP loops then return
    // a pooled CMsg to the engine); bit 0x8000 asks the proactor to close the handler.
    virtual int OnIOCompletion(DWORD dwBytes, LPOVERLAPPED pOverlapped) = 0;            // 2
    virtual void OnIdle() = 0;                                      // 3
};

C_ASSERT(sizeof(CEventHandler) == 4);
