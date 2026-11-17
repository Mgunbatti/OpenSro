#include "BSLib/Util/Registry.h"

#include <string.h>
#include <strsafe.h>

CRegistry::CRegistry()
{
    m_hKey = NULL;
}

CRegistry::~CRegistry()
{
    Close();
}

bool CRegistry::Open(const char* pszSubKey)
{
    return RegOpenKeyA(HKEY_CURRENT_USER, pszSubKey, &m_hKey) == ERROR_SUCCESS;
}

bool CRegistry::Close()
{
    if (m_hKey)
    {
        RegCloseKey(m_hKey);
        m_hKey = NULL;
    }
    return true;
}

bool CRegistry::SetDword(const char* pszName, DWORD dwValue)
{
    if (!m_hKey)
        return false;
    return RegSetValueExA(m_hKey, pszName, 0, REG_DWORD, (const BYTE*)&dwValue, 4) == 0;
}

bool CRegistry::SetString(const char* pszName, const char* pszValue)
{
    if (!m_hKey)
        return false;
    return RegSetValueExA(m_hKey, pszName, 0, REG_SZ, (const BYTE*)pszValue,
                          (DWORD)strlen(pszValue)) == 0;
}

bool CRegistry::SetBinary(const char* pszName, const void* pData, DWORD cbData)
{
    if (!m_hKey)
        return false;
    return RegSetValueExA(m_hKey, pszName, 0, REG_BINARY, (const BYTE*)pData, cbData) == 0;
}

bool CRegistry::QueryDword(const char* pszName, DWORD* pdwValue)
{
    if (!m_hKey)
        return false;
    DWORD dwType = REG_DWORD;
    DWORD cbData = 4;
    return RegQueryValueExA(m_hKey, pszName, NULL, &dwType, (BYTE*)pdwValue, &cbData) == 0;
}

// BUG(FW11-002): no terminator is added; a value of 255 bytes without NUL is read past the
// buffer.
bool CRegistry::QueryString(const char* pszName, std::string* pstrValue)
{
    BYTE abBuf[256];
    if (!m_hKey)
        return false;
    DWORD dwType = REG_SZ;
    DWORD cbData = 255;
    if (RegQueryValueExA(m_hKey, pszName, NULL, &dwType, abBuf, &cbData))
        return false;
    pstrValue->assign((const char*)abBuf, strlen((const char*)abBuf));
    return true;
}

bool CRegistry::QueryBinary(const char* pszName, void* pData, DWORD cbData)
{
    if (!m_hKey)
        return false;
    DWORD dwType = REG_BINARY;
    DWORD cb = cbData;
    return RegQueryValueExA(m_hKey, pszName, NULL, &dwType, (BYTE*)pData, &cb) == 0;
}

bool CRegistry::QueryString(const char* pszName, char* pszValue)
{
    BYTE abBuf[256];
    if (!m_hKey)
        return false;
    DWORD dwType = REG_SZ;
    DWORD cbData = 255;
    if (RegQueryValueExA(m_hKey, pszName, NULL, &dwType, abBuf, &cbData))
        return false;
    return StringCchCopyA(pszValue, 255, (const char*)abBuf) == S_OK;
}
