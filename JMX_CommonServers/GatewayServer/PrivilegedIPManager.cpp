#include "PrivilegedIPManager.h"

#include "ServerFramework/ServerDataExtern.h"

CPrivilegedIPManager g_PrivilegedIPManager;
CPrivilegedIPManager g_PrivilegedIPManager2;

static DWORD ByteSwap(DWORD dw)
{
    return (dw >> 24) | ((dw >> 8) & 0xFF00) | ((dw & 0xFF00) << 8) | (dw << 24);
}

CPrivilegedIPManager::~CPrivilegedIPManager()
{
    m_setIP.clear();
}

// BUG(GWCERT-007): a range ending at 255.255.255.255 never ends (the counter wraps to 0).
void CPrivilegedIPManager::AddRange(const DWORD* pRange)
{
    DWORD dwFirst = ByteSwap(pRange[0]);
    DWORD dwLast = ByteSwap(pRange[1]);
    for (DWORD dw = dwFirst; dw <= dwLast; ++dw)
        m_setIP.insert(ByteSwap(dw));
    LogPrintf(0, "%s ip : %d.%d.%d.%d - %d.%d.%d.%d", "previleged", dwFirst >> 24,
              (BYTE)(dwFirst >> 16), (BYTE)(dwFirst >> 8), (BYTE)dwFirst, dwLast >> 24,
              (BYTE)(dwLast >> 16), (BYTE)(dwLast >> 8), (BYTE)dwLast);
}

BOOL CPrivilegedIPManager::Contains(DWORD dwIP)
{
    return m_setIP.find(dwIP) != m_setIP.end();
}

BOOL IsPrivilegedIP(DWORD dwIP)
{
    return g_PrivilegedIPManager.Contains(dwIP);
}
