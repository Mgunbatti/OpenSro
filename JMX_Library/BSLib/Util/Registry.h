#pragma once

#include <windows.h>
#include <string>

// Values of one key under HKEY_CURRENT_USER (2011 library, AgentServer 0x489760..0x4899d0; used
// by CNetEngine::SetWinInetMaxConnections). Every access returns false while no key is open.
class CRegistry
{
public:
    CRegistry();                                                                // 0x489760
    virtual ~CRegistry();                                                       // 0x489790

    // RegOpenKeyA(HKEY_CURRENT_USER, pszSubKey). An open key is not closed first.
    bool Open(const char* pszSubKey);                                           // 0x4897a0
    // Closes the key if one is open. Always true.
    bool Close();                                                               // 0x4897c0

    bool SetDword(const char* pszName, DWORD dwValue);                          // 0x4897e0
    // REG_SZ; the size passed is strlen (without the terminator).
    bool SetString(const char* pszName, const char* pszValue);                  // 0x489810
    bool SetBinary(const char* pszName, const void* pData, DWORD cbData);       // 0x489850
    bool QueryDword(const char* pszName, DWORD* pdwValue);                      // 0x489880
    // Reads at most 255 bytes into a local buffer and assigns it up to the first NUL.
    bool QueryString(const char* pszName, std::string* pstrValue);              // 0x4898d0
    bool QueryBinary(const char* pszName, void* pData, DWORD cbData);           // 0x489980
    // The same into a buffer of 255 characters (StringCchCopyA); false when truncated.
    bool QueryString(const char* pszName, char* pszValue);                      // 0x4899d0

public:
    HKEY    m_hKey;     // +4
};

C_ASSERT(sizeof(CRegistry) == 8);
