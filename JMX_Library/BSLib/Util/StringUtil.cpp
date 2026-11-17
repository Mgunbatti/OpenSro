#include "BSLib/Util/StringUtil.h"

int UTF8_GetCharLength(BYTE c)
{
    if (c < 0x80)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 0;
}

// BUG(FW11-001): a lead byte of length 0 does not advance the loops below.
int UTF8_GetCharCount(const std::string& str)
{
    int nCount = 0;
    std::string::const_iterator itEnd = str.end();
    for (std::string::const_iterator it = str.begin(); it < itEnd;)
    {
        it += UTF8_GetCharLength((BYTE)*it);
        ++nCount;
    }
    return nCount;
}

int UTF8_GetCharStartBefore(const std::string& str, int nMaxBytes)
{
    std::string::const_iterator itBegin = str.begin();
    std::string::const_iterator itEnd = str.end();
    std::string::const_iterator it = itBegin;
    std::string::const_iterator itLast = itBegin;
    while (it < itEnd && it - itBegin < nMaxBytes)
    {
        itLast = it;
        it += UTF8_GetCharLength((BYTE)*it);
    }
    return (int)(itLast - itBegin);
}

int UTF8_GetDisplayWidth(const std::string& str)
{
    int nWidth = 0;
    std::string::const_iterator itEnd = str.end();
    for (std::string::const_iterator it = str.begin(); it < itEnd;)
    {
        int n = UTF8_GetCharLength((BYTE)*it);
        it += n;
        switch (n)
        {
        case 0:
            return nWidth;
        case 1:
            nWidth += 1;
            break;
        case 2:
        case 3:
        case 4:
            nWidth += 2;
            break;
        }
    }
    return nWidth;
}

std::string UTF8_TruncateToWidth(const std::string& str, int nMaxWidth)
{
    if (UTF8_GetDisplayWidth(str) <= nMaxWidth)
        return std::string(str, 0, std::string::npos);

    int nWidth = 0;
    std::string::const_iterator itBegin = str.begin();
    std::string::const_iterator itEnd = str.end();
    std::string::const_iterator it = itBegin;
    while (it < itEnd && nWidth < nMaxWidth)
    {
        int n = UTF8_GetCharLength((BYTE)*it);
        it += n;
        if (n == 1)
            nWidth += 1;
        else if (n > 1 && n <= 4)
            nWidth += 2;
    }
    return std::string(str, 0, it - itBegin);
}

bool UTF8_IsUtf8(const std::string& str)
{
    bool bUtf8 = false;
    std::string::const_iterator itEnd = str.end();
    for (std::string::const_iterator it = str.begin(); it < itEnd; ++it)
    {
        char c = *it;
        if (!(c & 0x80))
        {
            if (c < 1)
                return false;
            continue;
        }
        if (!(c & 0x40))
            return false;

        int nFollow;
        if (!(c & 0x20))
            nFollow = 1;
        else if (!(c & 0x10))
            nFollow = 2;
        else if (!(c & 0x08))
            nFollow = 3;
        else if (!(c & 0x04))
            nFollow = 4;
        else if (!(c & 0x02))
            nFollow = 5;
        else
            return false;

        for (int i = 0; i < nFollow; ++i)
        {
            ++it;
            if (it >= itEnd)
                return bUtf8;
            if (!(*it & 0x80))
                return false;
            if (*it & 0x40)
                return false;
        }
        bUtf8 = true;
    }
    return bUtf8;
}

std::wstring MultiByteToWString(const char* psz, int nLen)
{
    int n = MultiByteToWideChar(CP_ACP, 0, psz, nLen, NULL, 0);
    if (nLen == -1)
        --n;
    if (!n)
        return std::wstring(L"");

    std::wstring strTemp;
    strTemp.append(n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, psz, nLen, &strTemp[0], n);
    return std::wstring(strTemp, 0, std::wstring::npos);
}

std::string WideCharToString(const wchar_t* psz, int nLen)
{
    int n = WideCharToMultiByte(CP_ACP, 0, psz, nLen, NULL, 0, NULL, NULL);
    if (nLen == -1)
        --n;
    if (!n)
        return std::string("");

    std::string strTemp;
    strTemp.append(n, '\0');
    WideCharToMultiByte(CP_ACP, 0, psz, nLen, &strTemp[0], n, NULL, NULL);
    return std::string(strTemp, 0, std::string::npos);
}

bool WideCharToMultiByte_wrap(const wchar_t* pszSrc, int cchSrc, char* pszDst, int cbDst)
{
    return WideCharToMultiByte(CP_ACP, 0, pszSrc, cchSrc, pszDst, cbDst, NULL, NULL) != 0;
}
