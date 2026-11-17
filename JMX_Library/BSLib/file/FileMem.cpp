#include "FileMem.h"

#include <string.h>

CFileMem::CFileMem()
{
    m_pBuffer = NULL;
    m_nKind = 0;
}

CFileMem::~CFileMem()
{
    Close();
}

void CFileMem::Close()
{
    Reset();
    if (m_pBuffer != NULL)
    {
        delete[] m_pBuffer;
        m_pBuffer = NULL;
    }
}

BOOL CFileMem::Flush()
{
    return TRUE;
}

HANDLE CFileMem::GetHandle()
{
    return NULL;
}

BOOL CFileMem::IsOpen()
{
    return m_pBuffer != NULL;
}

int CFileMem::Open(const std::string& strPath, DWORD dwSize, DWORD dwContext)
{
    CSingleLockBS lock(&m_cs, TRUE);

    int nResult = IFile::Open(strPath, dwSize, dwContext);
    if (nResult == 0)
    {
        if (dwContext != 0)
        {
            // BUG(FILE-001): allocates a new buffer on every open, also when the file is
            // already open (the old buffer leaks, a read-mode open leaves a 0-byte buffer).
            m_dwSize = dwSize;
            m_pBuffer = new BYTE[dwSize];
        }
        nResult = AddUser(dwContext);
    }
    return nResult;
}

DWORD CFileMem::ReadAt(DWORD* pdwPos, void* pBuffer, DWORD dwSize)
{
    if (m_pBuffer == NULL)
        return 0;
    if (*pdwPos + dwSize > m_dwSize)
        dwSize = m_dwSize - *pdwPos;
    memcpy(pBuffer, m_pBuffer + *pdwPos, dwSize);
    return dwSize;
}

DWORD CFileMem::WriteAt(DWORD* pdwPos, const void* pBuffer, DWORD dwSize)
{
    if (m_pBuffer == NULL)
        return 0;
    if (*pdwPos + dwSize > m_dwSize)
        dwSize = m_dwSize - *pdwPos;
    memcpy(m_pBuffer + *pdwPos, pBuffer, dwSize);
    return dwSize;
}

void* CFileMem::GetMemory()
{
    return m_pBuffer;
}

void* CFileMem::AttachMemory(void* pMemory, DWORD dwSize)
{
    void* pOld = m_pBuffer;
    m_pBuffer = (BYTE*)pMemory;
    m_dwSize = dwSize;
    return pOld;
}

int CFileMem::GetType()
{
    return FILE_TYPE_MEMORY;
}
