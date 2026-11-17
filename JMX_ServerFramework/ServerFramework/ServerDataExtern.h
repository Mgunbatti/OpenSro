#pragma once

#include <windows.h>

// Functions of the server application layer used by the server data code.

// LogPrintf (0x41d790): formats into 8 KB and passes the line to the log / monitor window.
void LogPrintf(DWORD dwFlags, const char* pszFormat, ...);

// StrCmpI_Locale (0x41f430): CompareStringA(0x400, NORM_IGNORECASE | NORM_IGNOREKANATYPE)
// minus 2, so 0 means equal. The length of psz2 is taken first.
int StrCmpI_Locale(const char* psz1, const char* psz2);

// The same comparison inlined in many places: CSTR_EQUAL (2) when equal.
inline int CompareNoCase(const char* psz1, const char* psz2)
{
    int n2 = lstrlenA(psz2);
    int n1 = lstrlenA(psz1);
    return CompareStringA(0x400, NORM_IGNORECASE | NORM_IGNOREKANATYPE, psz1, n1, psz2, n2);
}
