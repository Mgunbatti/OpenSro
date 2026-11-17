#include "FileSystem.h"

#include "BSNet/NetEngineLog.h"
#include "FileMem.h"
#include "FileLegacy.h"
#include "FileMM.h"

// The process-wide instance (CFileSystem::GetInstance()).
CFileSystem g_FileSystem;

CFileSystem::CFileSystem()
{
    Cleanup();
    m_dwCacheLimit = 10000000;
}

CFileSystem::~CFileSystem()
{
    Cleanup();
}

void CFileSystem::Cleanup()
{
    CSingleLockBS lock(&m_cs, TRUE);

    for (OpenFileMap::iterator it = m_mapOpenFile.begin(); it != m_mapOpenFile.end(); ++it)
    {
        IFile* pFile = it->second;
        if (pFile != NULL)
            delete pFile;
    }
    m_mapOpenFile.clear();
    m_mapCachedName.clear();

    m_dwCacheUsed = 0;
    m_nOpenCount = 0;
    m_nReadOpenCount = 0;
    m_dwA0 = 0;
    m_bInitialized = FALSE;
}

BOOL CFileSystem::Initialize(DWORD dwCacheLimit)
{
    m_dwCacheLimit = dwCacheLimit;
    m_bInitialized = TRUE;
    m_IDPool.m_dwGrowBy = 5000;
    m_IDPool.Grow();
    return TRUE;
}

int CFileSystem::OpenFile(DWORD* pdwHandle, int nType, std::string& strPath, DWORD dwSize,
                          DWORD dwContext)
{
    if (nType == -1)
        return 0;

    CSingleLockBS lock(&m_cs, TRUE);

    std::string strLower;
    StringToLowerMbcs(strPath.c_str(), strLower);
    strPath.assign(strLower, 0, std::string::npos);

    IFile* pFile = FindOrCreateFileObject(pdwHandle, strPath, nType);
    if (pFile == NULL)
    {
        // "... failed [FileName: %s]"
        NetEngineLog(0x2000000, "CFileSystem::_CreateFile() \xBD\xC7\xC6\xD0 [FileName: %s]",
                     strPath.c_str());
        return FILE_ERR_CREATE_OBJECT;
    }

    if (pFile->GetType() != nType)
    {
        // "... tried to open a cached file with a different file type"
        NetEngineLog(0x1000000,
                     "CFileSystem::OpenFile() \xC4\xB3\xBD\xAC\xB5\xC8 \xC8\xAD\xC0\xCF\xB0\xFA "
                     "\xB4\xD9\xB8\xA5 \xC8\xAD\xC0\xCF\xC5\xB8\xC0\xD4\xC0\xBB \xC1\xF6\xC1\xA4"
                     "\xC7\xD8\xBC\xAD Open\xC7\xCF\xB7\xC1 \xC7\xDF\xB4\xD9"
                     "[FileType: %d, %d][FileName: %s]",
                     pFile->GetType(), nType, pFile->m_strPath.c_str());
        return FILE_ERR_TYPE_MISMATCH;
    }

    int nResult = pFile->Open(strPath, dwSize, dwContext);
    if (nResult == 0)
    {
        BS_ASSERT(pFile->m_nMode != -1);
        if (pFile->m_nMode == 0)
            ++m_nReadOpenCount;

        *pdwHandle = pFile->m_dwHandle;
        if (*pdwHandle != 0 || (*pdwHandle = RegisterOpenFile(pFile)) != 0)
        {
            ++m_nOpenCount;
            return 0;
        }
        nResult = FILE_ERR_NO_HANDLE;
    }
    else
    {
        // "... file open failed [FileName: %s][ErrorCode: %d]"
        NetEngineLog(0x2000000,
                     "CFileSystem::OpenFile() - \xC8\xAD\xC0\xCF \xBF\xC0\xC7\xC2\xBD\xC7\xC6\xD0 "
                     "[FileName: %s][ErrorCode: %d]",
                     strPath.c_str(), nResult);
    }

    // A file object that was created for this call goes away again.
    if (pFile->m_dwHandle == 0)
        delete pFile;
    return nResult;
}

int CFileSystem::CloseFile(DWORD dwHandle, DWORD dwContext)
{
    CSingleLockBS lock(&m_cs, TRUE);

    OpenFileMap::iterator it = m_mapOpenFile.find(dwHandle);
    if (it == m_mapOpenFile.end())
        return 0;

    IFile* pFile = it->second;
    int nUsers = pFile->ReleaseHandle(dwContext);
    --m_nOpenCount;
    BS_ASSERT(pFile->m_nMode != -1);
    if (nUsers == 0 && (m_dwCacheLimit == 0 || pFile->m_nMode == 1))
    {
        ReleaseFileEntry(pFile);
        m_mapOpenFile.erase(it);
    }
    return nUsers;
}

IFile* CFileSystem::GetFile(DWORD dwHandle)
{
    CSingleLockBS lock(&m_cs, TRUE);

    OpenFileMap::iterator it = m_mapOpenFile.find(dwHandle);
    if (it == m_mapOpenFile.end())
    {
        // "... cannot get the given file [FileHandle: %d]"
        NetEngineLog(0x1000000,
                     "CFileSystem::GetFile() - \xC1\xF6\xC1\xA4\xC7\xD1 \xC8\xAD\xC0\xCF\xC0\xBB "
                     "\xBE\xF2\xC0\xBB \xBC\xF6 \xBE\xF8\xB4\xD9 [FileHandle: %d]",
                     dwHandle);
        return NULL;
    }
    return it->second;
}

DWORD CFileSystem::RegisterOpenFile(IFile* pFile)
{
    DWORD dwHandle = m_IDPool.Alloc();
    pFile->m_dwHandle = dwHandle;
    m_mapOpenFile.insert(OpenFileMap::value_type(dwHandle, pFile));

    if (pFile->GetType() == FILE_TYPE_MAPPED)
    {
        BS_ASSERT(pFile->m_nMode != -1);
        if (pFile->m_nMode == 0 && m_dwCacheLimit != 0)
        {
            DWORD dwUsed = pFile->m_dwSize + m_dwCacheUsed;
            while (dwUsed >= m_dwCacheLimit)
            {
                DWORD dwFreed = PurgeUnusedFileEntries();
                if (dwFreed == 0)
                    break;
                dwUsed -= dwFreed;
            }
            // Clamped to [0, INT_MAX] as a signed value.
            if ((int)dwUsed < 0)
                dwUsed = 0;
            m_dwCacheUsed = dwUsed;

            m_mapCachedName.insert(
                FileNameMap::value_type(std::string(pFile->m_strPath.c_str()), dwHandle));
        }
    }
    return dwHandle;
}

DWORD CFileSystem::PurgeUnusedFileEntries()
{
    for (OpenFileMap::iterator it = m_mapOpenFile.begin(); it != m_mapOpenFile.end(); ++it)
    {
        IFile* pFile = it->second;
        if (pFile->GetUserCount() == 0)
        {
            // BUG(FILE-005): every idle file is purged and its size returned, also files that
            // never counted towards the cache.
            DWORD dwSize = pFile->m_dwSize;
            ReleaseFileEntry(pFile);
            m_mapOpenFile.erase(it);
            return dwSize;
        }
    }
    return 0;
}

void CFileSystem::ReleaseFileEntry(IFile* pFile)
{
    if (pFile == NULL)
        return;

    FileNameMap::iterator it;
    {
        std::string strName(pFile->m_strPath.c_str());
        it = m_mapCachedName.find(strName);
    }
    if (it != m_mapCachedName.end())
        m_mapCachedName.erase(it);

    DWORD dwHandle = pFile->m_dwHandle;
    if (dwHandle != 0)
        m_IDPool.Free(dwHandle);
    delete pFile;
}

IFile* CFileSystem::FindOrCreateFileObject(DWORD* pdwHandle, const std::string& strPath,
                                           int nType)
{
    *pdwHandle = 0;

    FileNameMap::iterator it = m_mapCachedName.find(strPath);
    if (it == m_mapCachedName.end())
    {
        switch (nType)
        {
        case FILE_TYPE_MEMORY:
            return new CFileMem;
        case FILE_TYPE_LEGACY:
            return new CFileLegacy;
        case FILE_TYPE_MAPPED:
            return new CFileMM;
        }
        return NULL;
    }

    *pdwHandle = it->second;
    return m_mapOpenFile.find(*pdwHandle)->second;
}
