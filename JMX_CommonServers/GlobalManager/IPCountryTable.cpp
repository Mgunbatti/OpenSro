#include "IPCountryTable.h"

#include <stdlib.h>
#include <string.h>

#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerDataExtern.h"

CIPCountryTable* g_pIPCountryTable = NULL;

// The byte order swaps of FindInBlock and ParseRIRRecord (inlined shifts in the original).
static inline DWORD SwapBytes(DWORD dw)
{
    return (dw << 24) | ((dw & 0xFF00) << 8) | ((dw >> 8) & 0xFF00) | (dw >> 24);
}

CIPCountryTable* CIPCountryTable::GetInstance()
{
    if (!g_pIPCountryTable)
        g_pIPCountryTable = new CIPCountryTable;
    return g_pIPCountryTable;
}

const char* CIPCountryTable::NextToken(const char* pszDelimiter, const char* pszText, char* pszOut,
                                       DWORD* pbEnd)
{
    int i = 0;
    if (lstrlenA(pszText) > 0)
    {
        do
        {
            if (pszText[i] == *pszDelimiter)
            {
                int nLen = lstrlenA(pszDelimiter);
                if (CompareStringA(0x400, 0, pszText + i, nLen, pszDelimiter, nLen) == CSTR_EQUAL)
                {
                    lstrcpynA(pszOut, pszText, i + 1);
                    return pszText + lstrlenA(pszDelimiter) + i;
                }
            }
            ++i;
        }
        while (i < lstrlenA(pszText));
    }
    if (i == lstrlenA(pszText))
    {
        *pbEnd = 1;
        return pszText;
    }
    return NULL;
}

void CIPCountryTable::Reset()
{
    m_hRIRFile = NULL;
    m_hCountryFile = NULL;
    m_pRIRBuffer = NULL;
    m_pCountryBuffer = NULL;
    m_pCursor = NULL;
    m_liRIRSize.QuadPart = 0;
    m_liCountrySize.QuadPart = 0;
    m_nASNCount = 0;
    m_nIPv4Count = 0;
    m_nIPv6Count = 0;
    m_mapBlock.clear();
    m_mapCountryName.clear();
    m_vecSummaryType.clear();
    memset(m_szLine, 0, sizeof(m_szLine));
}

void CIPCountryTable::Clear()
{
    for (IPCountryBlockMap::iterator it = m_mapBlock.begin(); it != m_mapBlock.end(); ++it)
    {
        if (it->second)
            operator delete(it->second);
    }
    m_mapBlock.clear();
    m_mapCountryName.clear();
    m_vecSummaryType.clear();
}

int CIPCountryTable::ReadWholeFile(char** ppBuffer, HANDLE* phFile, const char* pszPath,
                                   LARGE_INTEGER* pliSize)
{
    *phFile = CreateFileA(pszPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (*phFile == INVALID_HANDLE_VALUE)
        BSAssertFail();
    if (*phFile == INVALID_HANDLE_VALUE)
        return 0;
    GetFileSizeEx(*phFile, pliSize);
    // BUG(GM-024): the buffer has exactly the file's size; the parser runs lstrlenA over it.
    *ppBuffer = (char*)operator new(pliSize->LowPart);
    if (!*ppBuffer)
        BSAssertFail();
    if (!*ppBuffer)
        return 0;
    char szBuffer[0x1000] = { 0 };
    DWORD dwRead = 0;
    ULONGLONG qwPos = 0;
    for (;;)
    {
        if (!ReadFile(*phFile, szBuffer, sizeof(szBuffer), &dwRead, NULL))
        {
            BSAssertFail();
            break;
        }
        memcpy(*ppBuffer + (DWORD)qwPos, szBuffer, dwRead);
        qwPos += dwRead;
        if (!dwRead)
            break;
    }
    CloseHandle(*phFile);
    *phFile = NULL;
    return 1;
}

int CIPCountryTable::OpenCountryCodeFile(const char* pszPath)
{
    return ReadWholeFile(&m_pCountryBuffer, &m_hCountryFile, pszPath, &m_liCountrySize);
}

int CIPCountryTable::OpenRIRFile(const char* pszPath)
{
    return ReadWholeFile(&m_pRIRBuffer, &m_hRIRFile, pszPath, &m_liRIRSize);
}

char* CIPCountryTable::NextLine(int bSkip, int nSkip)
{
    DWORD bEnd = 0;
    if (!m_pCursor)
        BSAssertFail();
    if (bSkip == 1)
        m_pCursor += nSkip;
    m_pCursor = (char*)NextToken("\r\n", m_pCursor, m_szLine, &bEnd);
    if (!bSkip)
        m_szLine[lstrlenA(m_szLine)] = 0;
    return bEnd != 1 ? m_szLine : NULL;
}

int CIPCountryTable::LoadCountryCodeFile(const char* pszPath)
{
    if (!OpenCountryCodeFile(pszPath))
        return 0;
    m_pCursor = m_pCountryBuffer;
    NextLine(0, 0);
    DWORD dwCount = atol(m_szLine);
    if (dwCount < 0xF0)
        MessageBoxA(NULL, "Probably You have Invalid CountryCodeFile..", NULL, 0);
    for (; dwCount; --dwCount)
        ParseCountryLine(NextLine(0, 0));
    return 1;
}

int CIPCountryTable::LoadRIRFileTimed(const char* pszPath)
{
    DWORD dwStart = GetTickCount();
    int nResult = LoadRIRFile(pszPath);
    LogPrintf(0, "File Loading Time : (%s) %d tick\r\n", pszPath, GetTickCount() - dwStart);
    return nResult;
}

int CIPCountryTable::LoadRIRFile(const char* pszPath)
{
    if (!OpenRIRFile(pszPath))
        return 0;
    m_pCursor = m_pRIRBuffer;
    ParseRIRHeader();
    for (int i = 0; i < 3; ++i)
    {
        // Throws (std::out_of_range) when fewer than three summary types were found.
        if (!lstrcmpiA(m_vecSummaryType[0].c_str(), "asn"))
        {
            for (int n = 0; n < m_nASNCount; ++n)
                NextLine(1, 30);
        }
        else if (!lstrcmpiA(m_vecSummaryType[0].c_str(), "ipv4"))
        {
            for (int n = 0; n < m_nIPv4Count; ++n)
                ParseRIRRecord(NextLine(0, 0), NULL);
        }
        else if (!lstrcmpiA(m_vecSummaryType[0].c_str(), "ipv6"))
        {
            for (int n = 0; n < m_nIPv6Count; ++n)
                NextLine(1, 40);
        }
        m_vecSummaryType.erase(m_vecSummaryType.begin());
    }
    return 1;
}

void CIPCountryTable::CloseFile()
{
    if (m_pRIRBuffer)
    {
        operator delete(m_pRIRBuffer);
        m_pRIRBuffer = NULL;
    }
    if (m_pCountryBuffer)
    {
        operator delete(m_pCountryBuffer);
        m_pCountryBuffer = NULL;
    }
    if (m_hRIRFile)
    {
        CloseHandle(m_hRIRFile);
        m_hRIRFile = NULL;
    }
    if (m_hCountryFile)
    {
        CloseHandle(m_hCountryFile);
        m_hCountryFile = NULL;
    }
    m_vecSummaryType.clear();
}

void CIPCountryTable::ParseRIRHeader()
{
    IP_COUNTRY_RECORD summary = { 0 };
    while (*m_pCursor == '#')
        NextLine(0, 0);
    NextLine(0, 0);
    for (int i = 3; i; --i)
    {
        ParseRIRRecord(NextLine(0, 0), &summary);
        ParseRIRSummary(&summary);
    }
}

void CIPCountryTable::ParseRIRSummary(const IP_COUNTRY_RECORD* pRecord)
{
    if (!lstrcmpiA(pRecord->szCode, "ipv4"))
    {
        m_nIPv4Count = pRecord->dwCount;
        m_vecSummaryType.push_back(std::string("ipv4"));
    }
    else if (!lstrcmpiA(pRecord->szCode, "asn"))
    {
        m_nASNCount = pRecord->dwCount;
        m_vecSummaryType.push_back(std::string("asn"));
    }
    else if (!lstrcmpiA(pRecord->szCode, "ipv6"))
    {
        m_nIPv6Count = pRecord->dwCount;
        m_vecSummaryType.push_back(std::string("ipv6"));
    }
}

void CIPCountryTable::ParseCountryLine(const char* pszLine)
{
    char szToken[0x1000] = { 0 };
    DWORD bEnd = 0;
    std::string strCode;
    std::string strName;
    const char* p = NextToken("\t", pszLine, szToken, &bEnd);
    strCode.assign(szToken, strlen(szToken));
    p = NextToken("\t", p, szToken, &bEnd);
    if (bEnd == 1)
        strName.assign(p, strlen(p));
    m_mapCountryName.insert(std::pair<std::string, std::string>(strCode, strName));
}

void CIPCountryTable::ParseRIRRecord(const char* pszLine, IP_COUNTRY_RECORD* pRecord)
{
    char szToken[0x1000] = { 0 };
    DWORD bEnd;
    IP_COUNTRY_RECORD local = { 0 };
    if (!pRecord)
        pRecord = &local;
    const char* p = NextToken("|", pszLine, szToken, &bEnd);       // registry
    p = NextToken("|", p, szToken, &bEnd);                          // country code
    BOOL bSummary = lstrcmpA(szToken, "*") == 0;
    if (!bSummary)
        strcpy_s(pRecord->szCode, sizeof(pRecord->szCode), szToken);
    p = NextToken("|", p, szToken, &bEnd);                          // type
    // BUG(GM-020): "ipv4" and "ipv6" do not fit the code; strcpy_s fails.
    if (bSummary == 1)
        strcpy_s(pRecord->szCode, sizeof(pRecord->szCode), szToken);
    p = NextToken("|", p, szToken, &bEnd);                          // first address
    if (!bSummary)
        ParseDottedIP(szToken, &pRecord->dwIP);
    NextToken("|", p, szToken, &bEnd);                              // count
    DWORD dwCount = atol(szToken);
    pRecord->dwCount = dwCount;
    if (IsClassfulBlock(dwCount) == 1)
    {
        if (!bSummary)
        {
            IP_COUNTRY_RECORD* pBlock = new IP_COUNTRY_RECORD;
            memset(pBlock, 0, sizeof(*pBlock));
            *pBlock = *pRecord;
            // An address that is already in the map keeps its block; the new one is lost.
            m_mapBlock.insert(IPCountryBlockMap::value_type(pRecord->dwIP, pBlock));
        }
        return;
    }
    // BUG(GM-021): summary lines are split into blocks as well (from address 0).
    for (int nShift = 8; nShift < 32; nShift += 8)
    {
        DWORD dwBlock = 1 << nShift;
        if (dwCount <= dwBlock || dwCount >= dwBlock << 8 || !dwCount)
            continue;
        DWORD dwOffset = 0;
        do
        {
            IP_COUNTRY_RECORD* pBlock = new IP_COUNTRY_RECORD;
            memset(pBlock, 0, sizeof(*pBlock));
            *pBlock = *pRecord;
            DWORD dwIP = SwapBytes(SwapBytes(pRecord->dwIP) + dwOffset);
            pBlock->dwCount = dwBlock;
            pBlock->dwIP = dwIP;
            m_mapBlock.insert(IPCountryBlockMap::value_type(dwIP, pBlock));
            dwOffset += dwBlock;
        }
        while (dwOffset < dwCount);
    }
}

BOOL CIPCountryTable::IsClassfulBlock(DWORD dwCount)
{
    return dwCount == 0x100 || dwCount == 0x10000 || dwCount == 0x1000000;
}

void CIPCountryTable::ParseDottedIP(const char* pszText, DWORD* pdwIP)
{
    char szToken[16] = { 0 };
    DWORD bEnd = 0;
    const char* p = NextToken(".", pszText, szToken, &bEnd);
    BYTE by0 = (BYTE)atol(szToken);
    p = NextToken(".", p, szToken, &bEnd);
    BYTE by1 = (BYTE)atol(szToken);
    p = NextToken(".", p, szToken, &bEnd);
    BYTE by2 = (BYTE)atol(szToken);
    p = NextToken(".", p, szToken, &bEnd);
    if (bEnd == 1)
        strcpy_s(szToken, sizeof(szToken), p);
    BYTE by3 = (BYTE)atol(szToken);
    *pdwIP = by0 | ((DWORD)by1 << 8) | ((DWORD)by2 << 16) | ((DWORD)by3 << 24);
}

int CIPCountryTable::FindInBlock(DWORD dwKey, DWORD dwIP, char* pszOut)
{
    IPCountryBlockMap::iterator it = m_mapBlock.lower_bound(dwKey);
    if (it != m_mapBlock.end())
    {
        IP_COUNTRY_RECORD* pBlock = it->second;
        DWORD dwHost = SwapBytes(dwIP);
        DWORD dwFirst = SwapBytes(pBlock->dwIP);
        if (dwFirst <= dwHost && dwHost <= dwFirst + pBlock->dwCount)
        {
            strcpy_s(pszOut, 8, pBlock->szCode);
            return 1;
        }
    }
    strcpy_s(pszOut, 8, "**");
    return 0;
}

void CIPCountryTable::FindCountryCode(char* pszOut, DWORD dwIP)
{
    for (int nShift = 8; nShift < 32; nShift += 8)
    {
        if (FindInBlock((dwIP << nShift) >> nShift, dwIP, pszOut) == 1)
            break;
    }
}

void CIPCountryTable::GetCountryName(std::string strCode, std::string* pstrOut)
{
    CountryNameMap::iterator it = m_mapCountryName.find(strCode);
    if (it == m_mapCountryName.end())
        pstrOut->assign("**", 2);
    else
        pstrOut->assign(it->second);
}
