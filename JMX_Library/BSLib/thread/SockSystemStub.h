#pragma once

#include <winsock2.h>
#include <windows.h>

// Minimal stand-in for CSockSystem (Winsock start-up owner, 8 bytes), limited to what CSystem
// uses. Replace the include in System.cpp with the real header once CSockSystem is ported.
class CSockSystem
{
public:
    CSockSystem()
    {
        m_bStarted = FALSE;
    }

    virtual ~CSockSystem()
    {
        Cleanup();
    }

    void Cleanup()
    {
        if (m_bStarted)
        {
            if (WSACleanup())
                WSAGetLastError();
            m_bStarted = FALSE;
        }
    }

public:
    BOOL m_bStarted;    // +4, set after WSAStartup succeeded
};

C_ASSERT(sizeof(CSockSystem) == 8);
