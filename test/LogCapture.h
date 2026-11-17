#pragma once

// Reads back the lines LogPrintf posts to g_hServerIOCP.

#include <windows.h>
#include <string>
#include <vector>

#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerFramework.h"

struct CAPTURED_LOG
{
    DWORD       dwFlags;
    std::string strText;
};

// Gives LogPrintf a completion port to post to (it needs g_pBSNet as well).
inline void LogCapture_Start()
{
    if (!g_hServerIOCP)
        g_hServerIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
}

// Every queued line; the messages go back to g_pBSNet.
inline std::vector<CAPTURED_LOG> LogCapture_Drain()
{
    std::vector<CAPTURED_LOG> v;
    for (;;)
    {
        DWORD dwBytes = 0;
        ULONG_PTR ulKey = 0;
        LPOVERLAPPED pOverlapped = NULL;
        if (!GetQueuedCompletionStatus(g_hServerIOCP, &dwBytes, &ulKey, &pOverlapped, 0)
            || !pOverlapped)
        {
            break;
        }
        CMsg* pMsg = (CMsg*)pOverlapped;
        CAPTURED_LOG log;
        *pMsg >> log.dwFlags;
        *pMsg >> log.strText;
        g_pBSNet->ReleaseMsg(pMsg);
        v.push_back(log);
    }
    return v;
}

inline void LogCapture_Stop()
{
    LogCapture_Drain();
    CloseHandle(g_hServerIOCP);
    g_hServerIOCP = NULL;
}
