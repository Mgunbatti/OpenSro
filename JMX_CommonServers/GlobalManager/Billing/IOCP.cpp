#include "IOCP.h"

#include "BSLib/BSDebug.h"

static const char s_szIOCPFile[] =
    "D:\\WORK2005\\Source\\JMX_CommonServers\\GlobalManager\\Billing\\IOCP.cpp";

CIOCP::CIOCP(int nMaxConcurrency)
{
    m_hIOCP = NULL;
    if (!CreateIOCP(nMaxConcurrency) && !BSAssertDialog(8, s_szIOCPFile,
                                                         "CreateIOCP(nMaxConcurrency)"))
    {
        DebugBreak();
    }
}

CIOCP::~CIOCP()
{
    if (m_hIOCP && !CloseHandle(m_hIOCP)
        && !BSAssertDialog(14, s_szIOCPFile, "CloseHandle(m_hIOCP)"))
    {
        DebugBreak();
    }
}

bool CIOCP::CreateIOCP(int nMaxConcurrency)
{
    m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, nMaxConcurrency);
    if (!m_hIOCP)
        BSAssertFail();
    return m_hIOCP != NULL;
}

BOOL CIOCP::PostStatus(ULONG_PTR dwKey, DWORD dwBytes, LPOVERLAPPED pOverlapped)
{
    return PostQueuedCompletionStatus(m_hIOCP, dwBytes, dwKey, pOverlapped);
}

BOOL CIOCP::GetStatus(ULONG_PTR* pdwKey, LPDWORD pdwBytes, LPOVERLAPPED* ppOverlapped,
                      DWORD dwTimeout)
{
    return GetQueuedCompletionStatus(m_hIOCP, pdwBytes, pdwKey, ppOverlapped, dwTimeout);
}
