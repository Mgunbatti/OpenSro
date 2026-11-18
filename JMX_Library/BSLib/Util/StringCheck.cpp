#include "BSLib/Util/StringCheck.h"

#include <ctype.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------
// Separators and CStringProcess

bool StringSeparators_Add(STRING_SEPARATORS* pSeparators, char ch)
{
    int nCount = pSeparators->nCount;
    if (nCount >= 100)
        return false;
    for (int i = 0; i < nCount; ++i)
    {
        if (pSeparators->szChar[i] == ch)
        {
            pSeparators->chLast = ch;
            return false;
        }
    }
    pSeparators->szChar[nCount] = ch;
    pSeparators->byInclude[pSeparators->nCount] = 0;
    ++pSeparators->nCount;
    return true;
}

bool CStringProcess::IsBlank(char ch)
{
    return ch == ' ' || (ch > 0 && ch <= 0x10);
}

BOOL CStringProcess::IsDigit(char ch, bool bHex)
{
    if (isdigit(ch))
        return TRUE;
    if (bHex)
    {
        if (ch >= 'a' && ch <= 'f')
            return TRUE;
        if ((BYTE)(ch - 'A') <= 5)
            return TRUE;
    }
    return FALSE;
}

bool CStringProcess::SkipBlanks()
{
    bool bMoved = false;
    while (m_nPos < m_nLen)
    {
        char ch = m_pszLine[m_nPos];
        if (!ch || !IsBlank(ch))
            break;
        ++m_nPos;
        bMoved = true;
    }
    return bMoved;
}

bool CStringProcess::IsNumberAhead()
{
    int nSaved = m_nPos;
    bool bNumber = false;
    SkipBlanks();
    int nDigit;
    if (m_nPos < m_nLen)
    {
        char ch = m_pszLine[m_nPos++];
        if (ch == '-' || ch == '+')
        {
            char chNext = 0;
            if (m_nPos < m_nLen)
                chNext = m_pszLine[m_nPos++];
            nDigit = isdigit(chNext);
        }
        else
        {
            nDigit = isdigit((BYTE)ch);
        }
    }
    else
    {
        nDigit = isdigit(0);
    }
    if (nDigit)
        bNumber = true;
    m_nPos = nSaved > m_nLen ? 0 : nSaved;
    return bNumber;
}

int CStringProcess::ReadInt()
{
    m_bPositive = true;
    if (!IsNumberAhead())
        return 0;
    int nFirst = 0;
    bool bHex = false;
    int nChars = 0;
    int nValue = 0;
    SkipBlanks();
    const char* pszNumber = m_pszLine + m_nPos;
    while (m_nPos < m_nLen)
    {
        char ch = m_pszLine[m_nPos];
        if (!ch)
            break;
        if ((ch == 'x' || ch == 'X') && nChars == 1)
        {
            nFirst = nChars + 1;
            bHex = true;
        }
        else if (!IsDigit(ch, bHex))
        {
            if (nChars != 0)
                break;
            if (ch == '-')
                m_bPositive = false;
            else if (ch != '+')
                break;
            nFirst = 1;
        }
        if (m_nPos < m_nLen)
            ++m_nPos;
        ++nChars;
    }
    int nFactor = 1;
    if (bHex)
    {
        for (int i = nChars - 1; i >= nFirst; --i)
        {
            char ch = pszNumber[i];
            char chValue;
            if ((BYTE)(ch - 'a') <= 5)
                chValue = (char)(ch - 'a' + 10);
            else if ((BYTE)(ch - 'A') <= 5)
                chValue = (char)(ch - 'A' + 10);
            else
                chValue = (char)(ch - '0');
            nValue += chValue * nFactor;
            nFactor *= 16;
        }
    }
    else
    {
        for (int i = nChars - 1; i >= nFirst; --i)
        {
            nValue += (pszNumber[i] - '0') * nFactor;
            nFactor *= 10;
        }
    }
    if (!m_bPositive)
        nValue = -nValue;
    return nValue;
}

BOOL CStringProcess::ReadToken(std::string* pstrToken, STRING_SEPARATORS* pSeparators)
{
    if (pstrToken)
        pstrToken->erase();
    for (;;)
    {
        char ch = m_nPos < m_nLen ? m_pszLine[m_nPos] : 0;
        for (int i = 0; i < pSeparators->nCount; ++i)
        {
            if (pSeparators->szChar[i] == ch)
            {
                pSeparators->chLast = ch;
                if (!pSeparators->byInclude[i])
                    return TRUE;
                break;
            }
        }
        if (!ch)
            return TRUE;
        if (pstrToken)
            pstrToken->append(1, ch);
        if (m_nPos < m_nLen)
            ++m_nPos;
        for (int i = 0; i < pSeparators->nCount; ++i)
        {
            if (pSeparators->szChar[i] == ch)
            {
                pSeparators->chLast = ch;
                return TRUE;
            }
        }
    }
}

void CStringProcess::SkipColumns(int nCount)
{
    if (nCount <= 0)
        return;
    int nSkipped = 0;
    for (;;)
    {
        while (m_nPos < m_nLen)
        {
            char ch = m_pszLine[m_nPos];
            if (!ch)
                break;
            ++m_nPos;
            if (ch == '\t')
                break;
        }
        if (m_nPos >= m_nLen)
            return;
        if (++nSkipped >= nCount)
            return;
    }
}

// ---------------------------------------------------------------------------------------------
// CStringLineReader

CStringLineReader::CStringLineReader()
{
    m_bSingleBreaks = FALSE;
    m_bSkipBlanks = FALSE;
    m_pszText = NULL;
    m_pszCursor = NULL;
    m_nSize = 0;
    m_bEnd = true;
    m_nRemain = 0;
    m_bInComment = false;
    m_nLine = 0;
    m_bCommentAtStart = false;
}

void CStringLineReader::Attach(char* pszText, int nSize)
{
    m_bEnd = pszText == NULL;
    m_pszText = pszText;
    m_pszCursor = pszText;
    m_nSize = nSize;
    m_nRemain = nSize;
    m_bInComment = false;
    m_nLine = 0;
    m_bCommentAtStart = false;
}

char* CStringLineReader::NextLineEnd(char* psz)
{
    if (m_nRemain <= 0)
    {
        m_bEnd = true;
        return psz;
    }
    for (;;)
    {
        --m_nRemain;
        char ch = *psz;
        if (!ch)
            return psz + 1;
        if (m_bSingleBreaks)
        {
            if (ch == '\r')
            {
                if (psz[1] != '\n')
                {
                    *psz = 0;
                    return psz + 1;
                }
                *psz = '\t';
            }
            else if (ch == '\n')
            {
                if (psz[1] != '\r')
                {
                    *psz = 0;
                    return psz + 1;
                }
                *psz = '\t';
            }
        }
        else if (ch == '\r' && psz[1] == '\n')
        {
            psz[0] = 0;
            psz[1] = 0;
            --m_nRemain;
            return psz + 2;
        }
        ++psz;
        if (m_nRemain <= 0)
        {
            *psz = 0;
            return psz;
        }
    }
}

CStringProcess* CStringLineReader::ReadLine()
{
    bool bDone = m_bInComment != true;
    char* pszLine = m_pszCursor;
    m_pszCursor = NextLineEnd(pszLine);
    if (m_bEnd != true)
    {
        for (;;)
        {
            ++m_nLine;
            m_Line.m_pszLine = pszLine;
            m_Line.m_nLen = pszLine ? (int)strlen(pszLine) : 0;
            m_Line.m_nPos = 0;
            if (!m_bInComment)
            {
                // Only the first '/' of the line is looked at.
                while (m_Line.m_nPos < m_Line.m_nLen && m_Line.m_pszLine[m_Line.m_nPos])
                {
                    if (m_Line.m_pszLine[m_Line.m_nPos] != '/')
                    {
                        ++m_Line.m_nPos;
                        continue;
                    }
                    int nSlash = m_Line.m_nPos;
                    BOOL bAtStart = TRUE;
                    if (m_bCommentAtStart)
                    {
                        m_Line.m_nPos = 0;
                        m_Line.SkipBlanks();
                        if (nSlash != m_Line.m_nPos)
                            bAtStart = FALSE;
                        m_Line.m_nPos = nSlash > m_Line.m_nLen ? 0 : nSlash;
                    }
                    if (m_Line.m_nPos < m_Line.m_nLen)
                        ++m_Line.m_nPos;
                    if (m_Line.m_nPos < m_Line.m_nLen)
                    {
                        char ch = m_Line.m_pszLine[m_Line.m_nPos++];
                        if (ch == '*')
                        {
                            m_Line.m_nPos = nSlash > m_Line.m_nLen ? 0 : nSlash;
                            if (m_Line.m_nPos < m_Line.m_nLen)
                                m_Line.m_pszLine[m_Line.m_nPos] = 0;
                            m_bInComment = true;
                        }
                        else if (ch == '/' && bAtStart)
                        {
                            m_Line.m_nPos = nSlash > m_Line.m_nLen ? 0 : nSlash;
                            if (m_Line.m_nPos < m_Line.m_nLen)
                                m_Line.m_pszLine[m_Line.m_nPos] = 0;
                        }
                    }
                    break;
                }
            }
            else
            {
                while (m_Line.m_nPos < m_Line.m_nLen && m_Line.m_pszLine[m_Line.m_nPos])
                {
                    if (m_Line.m_pszLine[m_Line.m_nPos] != '*')
                    {
                        ++m_Line.m_nPos;
                        continue;
                    }
                    if (m_Line.m_nPos < m_Line.m_nLen)
                        ++m_Line.m_nPos;
                    if (m_Line.m_nPos < m_Line.m_nLen)
                    {
                        char ch = m_Line.m_pszLine[m_Line.m_nPos++];
                        if (ch == '/')
                        {
                            m_bInComment = false;
                            bDone = true;
                            pszLine = m_Line.m_pszLine + m_Line.m_nPos;
                        }
                    }
                    break;
                }
            }
            m_Line.m_pszLine = pszLine;
            m_Line.m_nLen = pszLine ? (int)strlen(pszLine) : 0;
            m_Line.m_nPos = 0;
            if (bDone)
                break;
            pszLine = m_pszCursor;
            m_pszCursor = NextLineEnd(pszLine);
            if (m_bEnd == true)
                break;
        }
    }
    if (m_bEnd)
        return NULL;
    return &m_Line;
}

CStringProcess* CStringLineReader::NextNonEmptyLine()
{
    CStringProcess* pLine = ReadLine();
    while (pLine)
    {
        if (m_bSkipBlanks)
            pLine->SkipBlanks();
        if (pLine->m_nPos < pLine->m_nLen && pLine->m_pszLine[pLine->m_nPos])
            return pLine;
        pLine = ReadLine();
    }
    return NULL;
}

// ---------------------------------------------------------------------------------------------
// CStringCheck

void String_ToLowerMbcs(const char* psz, std::string* pstrOut)
{
    pstrOut->erase();
    char ch = *psz;
    while (ch)
    {
        ++psz;
        if (ch < 0)
        {
            // BUG(FW11-028): the byte after a lead byte is taken even when it is the
            // terminator; the loop then reads past the end.
            pstrOut->append(1, ch);
            pstrOut->append(1, *psz);
            ++psz;
        }
        else
        {
            if ((BYTE)(ch - 'A') <= 25)
                ch += 0x20;
            pstrOut->append(1, ch);
        }
        ch = *psz;
    }
}

void CStringCheck::CWordList::Clear()
{
    m_nCount = 0;
    m_listWord.clear();
}

CStringCheck::CStringCheck()
{
    m_nLanguage = 0;
    Reset();
}

CStringCheck::~CStringCheck()
{
    Reset();
}

void CStringCheck::Reset()
{
    m_WordList.Clear();
    m_listForbiddenPart[0].clear();
    m_listForbiddenPart[1].clear();
    m_mapForbiddenWord[0].clear();
    m_mapForbiddenWord[1].clear();
}

BOOL CStringCheck::LoadFromFile(const char* pszFile)
{
    memset(m_byCharTable, 0, sizeof(m_byCharTable));
    HANDLE hFile = CreateFileA(pszFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD dwSize = GetFileSize(hFile, NULL);
    WCHAR* pwszText = (WCHAR*)::operator new(dwSize);
    DWORD dwRead;
    ReadFile(hFile, pwszText, dwSize, &dwRead, NULL);
    // Skips the byte order mark.
    int nChars = (int)(dwSize >> 1) - 1;
    int nLen = WideCharToMultiByte(CP_ACP, 0, pwszText + 1, nChars, NULL, 0, NULL, NULL);
    char* pszText = (char*)::operator new(nLen + 1);
    WideCharToMultiByte(CP_ACP, 0, pwszText + 1, nChars, pszText, nLen, NULL, NULL);
    pszText[nLen] = 0;
    ::operator delete(pwszText);

    CStringLineReader reader;
    reader.Attach(pszText, nLen);
    STRING_SEPARATORS separators;
    separators.chLast = 0;
    separators.nCount = 0;
    StringSeparators_Add(&separators, 0);
    StringSeparators_Add(&separators, '\t');
    StringSeparators_Add(&separators, '\r');

    int nSkip = 0;
    switch (m_nLanguage)
    {
    case 0:
        nSkip = 0;
        break;
    case 1:
        nSkip = 6;
        break;
    case 2:
        nSkip = 12;
        break;
    case 3:
        nSkip = 18;
        break;
    case 4:
        nSkip = 24;
        break;
    case 5:
        nSkip = 30;
        break;
    }

    for (CStringProcess* pLine = reader.NextNonEmptyLine(); pLine;
         pLine = reader.NextNonEmptyLine())
    {
        pLine->SkipColumns(nSkip);
        std::string strWord;
        std::string strToken;
        if (pLine->m_nPos >= pLine->m_nLen)
            continue;
        pLine->ReadToken(&strToken, &separators);
        String_ToLowerMbcs(strToken.c_str(), &strWord);
        if (strWord == "#allow_id_table")
        {
            int nFirst = (WORD)pLine->ReadInt();
            int nLast = (WORD)pLine->ReadInt();
            for (int nCode = nFirst; nCode <= nLast; ++nCode)
                m_byCharTable[nCode / 8] |= (BYTE)(1 << (nCode % 8));
            continue;
        }
        if (strWord.size() == 0)
            continue;
        int nColumn[4];
        nColumn[0] = pLine->ReadInt();
        nColumn[1] = pLine->ReadInt();
        nColumn[2] = pLine->ReadInt();
        nColumn[3] = pLine->ReadInt();
        for (int i = 0; i < 4; ++i)
        {
            if (nColumn[i] == 1)
            {
                bool bForbidden = true;
                m_mapForbiddenWord[i].insert(std::pair<std::string, bool>(strWord, bForbidden));
            }
            else if (nColumn[i] == 2)
            {
                m_listForbiddenPart[i].push_back(strWord);
            }
        }
        // The rest of the line goes into the word and is not used.
        pLine->ReadToken(&strWord, &separators);
    }

    ::operator delete(pszText);
    CloseHandle(hFile);
    for (int i = 0; i < (int)sizeof(m_byCharTable); ++i)
        m_byCharTable[i] = (BYTE)~m_byCharTable[i];
    return TRUE;
}

bool CStringCheck::IsAllowedName(const std::string& strName)
{
    std::string strLower;
    String_ToLowerMbcs(strName.c_str(), &strLower);
    if (!AreCharsAllowed(strLower.c_str()))
        return false;
    if (ContainsForbiddenPart(m_listForbiddenPart[0], strLower))
        return false;
    if (ContainsForbiddenWord(strLower, m_mapForbiddenWord[0]))
        return false;
    return true;
}

BOOL CStringCheck::AreCharsAllowed(const char* psz)
{
    const BYTE* p = (const BYTE*)psz;
    BYTE ch = *p;
    while (ch)
    {
        ++p;
        DWORD dwCode;
        if (ch & 0x80)
        {
            // BUG(FW11-028): a lead byte at the end takes the terminator as trail byte.
            dwCode = (ch << 8) | *p;
            ++p;
        }
        else
        {
            dwCode = ch;
        }
        if (m_byCharTable[dwCode >> 3] & (1 << (dwCode % 8)))
            return FALSE;
        ch = *p;
    }
    return TRUE;
}

bool CStringCheck::ContainsForbiddenPart(std::list<std::string>& listPart,
                                         const std::string& strName)
{
    if (strName.size() == 0)
        return false;
    for (std::list<std::string>::iterator it = listPart.begin(); it != listPart.end(); ++it)
    {
        size_t nLeft = strName.size();
        if (nLeft < it->size())
            continue;
        char szPart[0x100];
        strcpy_s(szPart, sizeof(szPart), it->c_str());
        const char* p = strName.c_str();
        if (!*p)
            continue;
        do
        {
            size_t nPart = it->size();
            if (nLeft < nPart || nLeft == 0)
                break;
            char szSub[0x100];
            memcpy(szSub, p, nPart);
            szSub[nPart] = 0;
            if (_stricmp(szSub, szPart) == 0)
                return true;
            if (*p & 0x80)
            {
                p += 2;
                nLeft -= 2;
            }
            else
            {
                p += 1;
                nLeft -= 1;
            }
        } while (nLeft != 0 && *p);
    }
    return false;
}

bool CStringCheck::ContainsForbiddenWord(const std::string& strName,
                                         stdext::hash_map<std::string, bool>& mapWord)
{
    if (strName.size() == 0)
        return false;
    SetWords(strName);
    for (std::list<std::string>::iterator it = m_WordList.m_listWord.begin();
         it != m_WordList.m_listWord.end(); ++it)
    {
        if (mapWord.find(*it) != mapWord.end())
            return true;
    }
    return false;
}

CStringCheck::CWordList* CStringCheck::SetWords(const std::string& strText)
{
    m_WordList.Clear();
    ParseWords(strText.c_str(), NULL);
    return &m_WordList;
}

BOOL CStringCheck::ParseWords(const char* psz, CWordList* pList)
{
    if (!pList)
        pList = &m_WordList;
    char szWord[0x400];
    memset(szWord, 0, sizeof(szWord));
    if (psz)
    {
        // BUG(FW11-029): a word of 1024 characters or more overruns szWord.
        char* pszOut = szWord;
        while (*psz != ' ' && *psz)
            *pszOut++ = *psz++;
    }
    std::string strWord;
    strWord.assign(szWord, strlen(szWord));
    pList->m_listWord.push_back(strWord);
    ++pList->m_nCount;
    while (psz && *psz == ' ')
        ++psz;
    if (!psz || !*psz)
        return TRUE;
    return ParseWords(psz, pList);
}
