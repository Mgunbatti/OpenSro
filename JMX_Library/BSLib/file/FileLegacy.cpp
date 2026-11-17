#include "FileLegacy.h"

CFileLegacy::CFileLegacy()
{
    m_hFile = INVALID_HANDLE_VALUE;
    m_nKind = 1;
}

CFileLegacy::~CFileLegacy()
{
    Close();
}

void CFileLegacy::Close()
{
    CSingleLockBS lock(&m_cs, TRUE);

    Flush();
    if (m_hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
    }
    Reset();
}

BOOL CFileLegacy::Flush()
{
    CSingleLockBS lock(&m_cs, TRUE);

    if (IsOpen() == TRUE)
    {
        BS_ASSERT(m_nMode != -1);
        if (m_nMode == 1)
            return FlushFileBuffers(m_hFile);
    }
    return TRUE;
}

HANDLE CFileLegacy::GetHandle()
{
    return m_hFile;
}

BOOL CFileLegacy::IsOpen()
{
    return m_hFile != INVALID_HANDLE_VALUE;
}

int CFileLegacy::Open(const std::string& strPath, DWORD dwSize, DWORD dwContext)
{
    CSingleLockBS lock(&m_cs, TRUE);

    int nResult = IFile::Open(strPath, dwSize, dwContext);
    if (nResult != 0)
        return nResult;

    if (!IsOpen())
    {
        DWORD dwAccess;
        DWORD dwDisposition;
        DWORD dwFlags;
        BS_ASSERT(m_nMode != -1);
        if (m_nMode == 1)
        {
            dwAccess = GENERIC_WRITE;
            dwDisposition = CREATE_ALWAYS;
            dwFlags = FILE_ATTRIBUTE_NORMAL;
        }
        else
        {
            dwAccess = GENERIC_READ;
            dwDisposition = OPEN_EXISTING;
            dwFlags = FILE_FLAG_SEQUENTIAL_SCAN;
        }

        m_hFile = CreateFileA(strPath.c_str(), dwAccess, FILE_SHARE_READ, NULL, dwDisposition,
                              dwFlags, NULL);
        if (m_hFile == INVALID_HANDLE_VALUE)
            return FILE_ERR_CREATE_FILE;

        BS_ASSERT(m_nMode != -1);
        if (m_nMode == 1)
        {
            m_dwSize = dwSize;
            SetFilePointer(m_hFile, (LONG)dwSize, NULL, FILE_BEGIN);
            SetEndOfFile(m_hFile);
            SetFilePointer(m_hFile, 0, NULL, FILE_BEGIN);
        }
        else
        {
            m_dwSize = GetFileSize(m_hFile, NULL);
            if (m_dwSize == 0)
                return FILE_ERR_EMPTY_FILE;     // the handle stays open
        }
    }

    return AddUser(dwContext);
}

BOOL CFileLegacy::SeekTo(DWORD* pdwPos)
{
    if (*pdwPos == 0)
        return TRUE;
    return SetFilePointer(m_hFile, (LONG)*pdwPos, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER;
}

BOOL CFileLegacy::Rewind()
{
    return SetFilePointer(m_hFile, 0, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER;
}

DWORD CFileLegacy::ReadAt(DWORD* pdwPos, void* pBuffer, DWORD dwSize)
{
    if (dwSize == 0 || !SeekTo(pdwPos))
        return 0;

    DWORD dwRead = 0;
    ReadFile(m_hFile, pBuffer, dwSize, &dwRead, NULL);
    Rewind();
    return dwRead;
}

DWORD CFileLegacy::WriteAt(DWORD* /*pdwPos*/, const void* pBuffer, DWORD dwSize)
{
    if (dwSize == 0)
        return 0;

    DWORD dwWritten;
    if (!WriteFile(m_hFile, pBuffer, dwSize, &dwWritten, NULL))
        return 0;
    return dwWritten;
}

void* CFileLegacy::GetMemory()
{
    return NULL;
}

int CFileLegacy::GetType()
{
    return FILE_TYPE_LEGACY;
}
