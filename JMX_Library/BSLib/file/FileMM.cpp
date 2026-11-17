#include "FileMM.h"

#include <string.h>

CFileMM::CFileMM()
{
    m_hFile = INVALID_HANDLE_VALUE;
    m_hMapping = NULL;
    m_pView = NULL;
}

CFileMM::~CFileMM()
{
    Close();
}

void CFileMM::Close()
{
    CSingleLockBS lock(&m_cs, TRUE);

    Flush();
    if (m_pView != NULL)
    {
        UnmapViewOfFile(m_pView);
        m_pView = NULL;
    }
    if (m_hMapping != NULL)
    {
        CloseHandle(m_hMapping);
        m_hMapping = NULL;
    }
    if (m_hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
    }
    Reset();
}

BOOL CFileMM::Flush()
{
    CSingleLockBS lock(&m_cs, TRUE);

    if (m_pView != NULL)
    {
        BS_ASSERT(m_nMode != -1);
        if (m_nMode == 1)
            return FlushViewOfFile(m_pView, 0);
    }
    return TRUE;
}

HANDLE CFileMM::GetHandle()
{
    return m_hFile;
}

BOOL CFileMM::IsOpen()
{
    return m_hFile != INVALID_HANDLE_VALUE && m_hMapping != NULL;
}

int CFileMM::Open(const std::string& strPath, DWORD dwSize, DWORD dwContext)
{
    CSingleLockBS lock(&m_cs, TRUE);

    int nResult = IFile::Open(strPath, dwSize, dwContext);
    if (nResult != 0)
        return nResult;

    if (!IsOpen())
    {
        DWORD dwAccess;
        DWORD dwProtect;
        DWORD dwMapAccess;
        DWORD dwFlags;
        BS_ASSERT(m_nMode != -1);
        if (m_nMode == 1)
        {
            dwProtect = PAGE_READWRITE;
            dwAccess = GENERIC_ALL;
            dwMapAccess = FILE_MAP_WRITE;
            dwFlags = FILE_ATTRIBUTE_NORMAL;
        }
        else
        {
            dwAccess = GENERIC_READ;
            dwProtect = PAGE_READONLY;
            dwMapAccess = FILE_MAP_READ;
            dwFlags = FILE_FLAG_SEQUENTIAL_SCAN;
        }

        // BUG(FILE-002): OPEN_ALWAYS also in read mode, so opening a missing file for reading
        // creates an empty file and fails with FILE_ERR_EMPTY_FILE.
        m_hFile = CreateFileA(strPath.c_str(), dwAccess, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                              dwFlags, NULL);
        if (m_hFile == INVALID_HANDLE_VALUE)
            return FILE_ERR_CREATE_FILE;

        // The failures below leave the handles open until Close().
        BS_ASSERT(m_nMode != -1);
        if (m_nMode == 1)
        {
            m_dwSize = dwSize;
        }
        else
        {
            m_dwSize = GetFileSize(m_hFile, NULL);
            if (m_dwSize == 0)
                return FILE_ERR_EMPTY_FILE;
        }

        m_hMapping = CreateFileMappingA(m_hFile, NULL, dwProtect, 0, m_dwSize, NULL);
        if (m_hMapping == NULL)
            return FILE_ERR_CREATE_MAPPING;

        void* pView = MapViewOfFile(m_hMapping, dwMapAccess, 0, 0, 0);
        if (pView == NULL)
            return FILE_ERR_MAP_VIEW;
        m_pView = (BYTE*)pView;
    }

    return AddUser(dwContext);
}

DWORD CFileMM::ReadAt(DWORD* pdwPos, void* pBuffer, DWORD dwSize)
{
    if (m_pView == NULL)
        return 0;
    if (*pdwPos + dwSize > m_dwSize)
        dwSize = m_dwSize - *pdwPos;
    memcpy(pBuffer, m_pView + *pdwPos, dwSize);
    return dwSize;
}

DWORD CFileMM::WriteAt(DWORD* pdwPos, const void* pBuffer, DWORD dwSize)
{
    if (m_pView == NULL)
        return 0;
    if (*pdwPos + dwSize > m_dwSize)
        dwSize = m_dwSize - *pdwPos;
    memcpy(m_pView + *pdwPos, pBuffer, dwSize);
    return dwSize;
}

void* CFileMM::GetMemory()
{
    return m_pView;
}

int CFileMM::GetType()
{
    return FILE_TYPE_MAPPED;
}
