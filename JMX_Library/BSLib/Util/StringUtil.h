#pragma once

#include <windows.h>
#include <string>

// UTF-8 and code page helpers of the 2011 library (AgentServer 0x475a10..0x4767cc). Only the
// display width (used by the truncation) and WideCharToMultiByte_wrap (used by
// BSAssertDialogW) are referenced in AgentServer.
//
// The loops step through the string with checked iterators like the original: a step past the
// end throws std::out_of_range. A lead byte of length 0 (0x80..0xBF, 0xF8..0xFF) does not advance
// the iterator, so UTF8_GetCharCount, UTF8_GetCharStartBefore and UTF8_TruncateToWidth never
// return for such strings (FW11-001).

// 0x475a10: bytes of the character with this lead byte: 1 (ASCII), 2, 3, 4, or 0.
int UTF8_GetCharLength(BYTE c);
// 0x475a50: number of characters.
int UTF8_GetCharCount(const std::string& str);
// 0x475b70: offset of the last character that starts before nMaxBytes.
int UTF8_GetCharStartBefore(const std::string& str, int nMaxBytes);
// 0x475d00: display width, 1 per ASCII character and 2 per multi-byte character; stops at a
// byte of length 0.
int UTF8_GetDisplayWidth(const std::string& str);
// 0x475e50: the string, or its start up to the first character that reaches nMaxWidth (a wide
// character can end one column beyond it).
std::string UTF8_TruncateToWidth(const std::string& str, int nMaxWidth);
// 0x4761e0: true when the string has at least one well-formed multi-byte sequence and no NUL or
// stray continuation byte (pure ASCII is false). A sequence cut off by the end still counts.
bool UTF8_IsUtf8(const std::string& str);

// 0x4764d0: ANSI code page to UTF-16. nLen -1 for a NUL-terminated string (not included).
std::wstring MultiByteToWString(const char* psz, int nLen);
// 0x476630: UTF-16 to the ANSI code page. nLen -1 for a NUL-terminated string.
std::string WideCharToString(const wchar_t* psz, int nLen);
// 0x4767b0: WideCharToMultiByte(CP_ACP) of cchSrc characters into pszDst (cbDst bytes).
bool WideCharToMultiByte_wrap(const wchar_t* pszSrc, int cchSrc, char* pszDst, int cbDst);
