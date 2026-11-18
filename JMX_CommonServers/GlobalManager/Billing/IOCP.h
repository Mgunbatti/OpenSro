#pragma once

#include <winsock2.h>
#include <windows.h>

// CIOCP (Billing\IOCP.cpp, 4 bytes): the completion port of the billing manager. The billing
// network and timer libraries post their events to it; CThreadEx workers wait on it.
class CIOCP
{
public:
    // sub_453F90: creates the port (assert dialog when that fails).
    CIOCP(int nMaxConcurrency = 0);
    // sub_453FD0: closes the port (assert dialog when that fails).
    ~CIOCP();

    // sub_454000
    bool CreateIOCP(int nMaxConcurrency);
    // PostQueuedCompletionStatus_wrap_2 (0x454070)
    BOOL PostStatus(ULONG_PTR dwKey, DWORD dwBytes = 0, LPOVERLAPPED pOverlapped = NULL);
    // GetQueuedCompletionStatus_wrap (0x454080), waits without timeout.
    BOOL GetStatus(ULONG_PTR* pdwKey, LPDWORD pdwBytes, LPOVERLAPPED* ppOverlapped,
                   DWORD dwTimeout = INFINITE);

public:
    HANDLE  m_hIOCP;    // +0
};

C_ASSERT(sizeof(CIOCP) == 4);
