#include "IFile.h"

#include "BSNet/NetEngineLog.h"

// The largest block IFile::Read hands out (fits one network message).
#define IFILE_READ_BLOCK    4090

void StringToLowerMbcs(const char* pszSrc, std::string& strDst)
{
    strDst.clear();
    char ch = *pszSrc;
    while (ch != 0)
    {
        ++pszSrc;
        if (ch < 0)
        {
            strDst.append(1, ch);
            // BUG(FILE-004): a lead byte right before the terminator copies the terminator
            // and keeps reading behind it.
            strDst.append(1, *pszSrc);
            ++pszSrc;
        }
        else
        {
            if ((BYTE)(ch - 'A') <= 'Z' - 'A')
                ch += 'a' - 'A';
            strDst.append(1, ch);
        }
        ch = *pszSrc;
    }
}

IFile::IFile()
{
    m_dwHandle = 0;
    m_dwSize = 0;
    m_nOpenCount = 0;
    m_nMode = -1;
    m_nKind = -1;
}

IFile::~IFile()
{
    Reset();
}

void IFile::Reset()
{
    CSingleLockBS lock(&m_cs, TRUE);

    for (PositionMap::iterator it = m_mapPosition.begin(); it != m_mapPosition.end(); ++it)
        delete it->second;
    m_mapPosition.clear();
    m_strPath.clear();

    m_dwHandle = 0;
    m_dwSize = 0;
    m_nOpenCount = 0;
    m_nMode = -1;
    m_nKind = -1;
}

int IFile::GetUserCount()
{
    CSingleLockBS lock(&m_cs, TRUE);
    return (int)m_mapPosition.size();
}

int IFile::ReleaseHandle(DWORD dwContext)
{
    CSingleLockBS lock(&m_cs, TRUE);
    return RemoveUser(dwContext);
}

DWORD IFile::GetRemainingSize(DWORD* pdwPos)
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (m_dwSize > *pdwPos)
        return m_dwSize - *pdwPos;
    return 0;
}

int IFile::AddUser(DWORD dwContext)
{
    CSingleLockBS lock(&m_cs, TRUE);

    if (m_mapPosition.find(dwContext) != m_mapPosition.end())
        return FILE_ERR_ALREADY_OPENED_BY_USER;

    DWORD* pdwPos = new DWORD(0);
    m_mapPosition.insert(PositionMap::value_type(dwContext, pdwPos));
    ++m_nOpenCount;
    return 0;
}

DWORD* IFile::FindPosition(DWORD dwContext)
{
    CSingleLockBS lock(&m_cs, TRUE);

    PositionMap::iterator it = m_mapPosition.find(dwContext);
    if (it == m_mapPosition.end())
        return NULL;
    return it->second;
}

DWORD IFile::GetPosition(DWORD dwContext)
{
    CSingleLockBS lock(&m_cs, TRUE);

    DWORD* pdwPos = FindPosition(dwContext);
    if (pdwPos == NULL)
        return 0;
    return *pdwPos;
}

int IFile::RemoveUser(DWORD dwContext)
{
    CSingleLockBS lock(&m_cs, TRUE);

    PositionMap::iterator it = m_mapPosition.find(dwContext);
    if (it != m_mapPosition.end())
    {
        delete it->second;
        m_mapPosition.erase(it);
    }
    return (int)m_mapPosition.size();
}

int IFile::Open(const std::string& strPath, DWORD dwSize, DWORD dwContext)
{
    CSingleLockBS lock(&m_cs, TRUE);

    int nMode = (dwSize != 0) ? 1 : 0;
    if (IsOpen() != TRUE)
    {
        m_nMode = nMode;
        StringToLowerMbcs(strPath.c_str(), m_strPath);
        return 0;
    }

    BS_ASSERT(m_nMode != -1);
    if (m_nMode == 1 && dwContext != 0)
        return FILE_ERR_WRITE_SHARED;
    BS_ASSERT(m_nMode != -1);
    if (m_nMode != nMode)
        return FILE_ERR_MODE_MISMATCH;
    if (FindPosition(dwContext) != NULL)
        return FILE_ERR_ALREADY_OPENED_BY_USER;
    return 0;
}

void* IFile::GetMemory()
{
    return NULL;
}

void* IFile::AttachMemory(void* /*pMemory*/, DWORD /*dwSize*/)
{
    return NULL;
}

int IFile::GetType()
{
    return -1;
}

int IFile::Read(DWORD dwContext, void* pBuffer)
{
    CSingleLockBS lock(&m_cs, TRUE);

    DWORD* pdwPos = FindPosition(dwContext);
    if (pdwPos == NULL)
    {
        // "... User Context lookup failed!"
        NetEngineLog(0x2000000, "IFile::Read() - User Context \xBE\xF2\xB1\xE2 \xBD\xC7\xC6\xD0!");
        return 0;
    }

    DWORD dwRemain = GetRemainingSize(pdwPos);
    if (dwRemain == 0)
    {
        // "... (requested byte count: %d)"
        // BUG(FILE-003): logs the block limit instead of a requested size.
        NetEngineLog(0x2000000,
                     "IFile::Read() - RemainBytes == 0 (\xBF\xE4\xC3\xBB\xC7\xD1 \xB9\xD9\xC0\xCC"
                     "\xC6\xAE \xBC\xF6: %d)",
                     IFILE_READ_BLOCK);
        return 0;
    }

    if (dwRemain >= IFILE_READ_BLOCK)
        dwRemain = IFILE_READ_BLOCK;
    DWORD dwRead = ReadAt(pdwPos, pBuffer, dwRemain);
    *pdwPos += dwRead;
    return (int)dwRead;
}

int IFile::Write(DWORD dwContext, const void* pBuffer, int nSize)
{
    CSingleLockBS lock(&m_cs, TRUE);

    if (nSize <= 0)
        return 0;

    DWORD* pdwPos = FindPosition(dwContext);
    if (pdwPos == NULL)
    {
        NetEngineLog(0x2000000, "IFile::Write() - User Context \xBE\xF2\xB1\xE2 \xBD\xC7\xC6\xD0!");
        return 0;
    }

    DWORD dwRemain = GetRemainingSize(pdwPos);
    if (dwRemain == 0)
    {
        // The original reuses the Read() message here.
        NetEngineLog(0x2000000,
                     "IFile::Read() - RemainBytes == 0 (\xBF\xE4\xC3\xBB\xC7\xD1 \xB9\xD9\xC0\xCC"
                     "\xC6\xAE \xBC\xF6: %d)",
                     nSize);
        return 0;
    }

    DWORD dwSize = (DWORD)nSize;
    if (dwSize > dwRemain)
        dwSize = dwRemain;
    DWORD dwWritten = WriteAt(pdwPos, pBuffer, dwSize);
    *pdwPos += dwWritten;
    return (int)dwWritten;
}

int IFile::Seek(DWORD dwContext)
{
    CSingleLockBS lock(&m_cs, TRUE);

    if (FindPosition(dwContext) == NULL)
    {
        NetEngineLog(0x2000000, "IFile::Seek() - User Context \xBE\xF2\xB1\xE2 \xBD\xC7\xC6\xD0!");
        return FILE_ERR_UNKNOWN_USER;
    }
    return 0;
}
