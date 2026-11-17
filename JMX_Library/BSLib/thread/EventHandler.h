#pragma once

#include <windows.h>

// Interface of the objects a proactor dispatches to (CBaseSocket and the socket classes).
// Only the destructor has code; the parameter types of slots 1 and 2 follow the socket
// overrides and are refined with them.
class CEventHandler
{
public:
    CEventHandler() {}
    virtual ~CEventHandler() {}                                     // 0

    // Associates the handle with the proactor and starts I/O.
    virtual BOOL AttachToProactor(void* pProactor, DWORD dwParam) = 0;     // 1
    // Called by the proactor worker loop with the completion result.
    virtual int OnIOCompletion(DWORD dwBytes, LPOVERLAPPED pOverlapped) = 0; // 2
    virtual void OnIdle() = 0;                                      // 3
};

C_ASSERT(sizeof(CEventHandler) == 4);
