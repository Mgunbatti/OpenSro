#pragma once

#include <windows.h>
#include <string>
#include <hash_map>

#include "BSLib/SingletonT.h"
#include "BSLib/IDPool.h"
#include "BSLib/thread/SynchObject.h"
#include "IFile.h"

// Registry of the open files, addressed by handle ids from an id pool. With a cache limit set,
// memory-mapped files opened for reading stay mapped after the last user closed them and are
// found again by path; unused files are dropped once the mapped bytes reach the limit.
class CFileSystem : public CSingletonT<CFileSystem>
{
public:
    CFileSystem();
    virtual ~CFileSystem();

    // Sets the cache limit (bytes, 0 = no caching) and fills the handle id pool.
    BOOL Initialize(DWORD dwCacheLimit);
    // Deletes every file object and resets the counters.
    void Cleanup();

    // Lower-cases strPath in place, then opens it for dwContext with the implementation nType
    // (FILE_TYPE_*). dwSize != 0 opens for writing. Returns 0 or a FILE_ERR_* / IFile code.
    int OpenFile(DWORD* pdwHandle, int nType, std::string& strPath, DWORD dwSize,
                 DWORD dwContext);
    // Removes dwContext from the file; returns the number of remaining users.
    int CloseFile(DWORD dwHandle, DWORD dwContext);
    IFile* GetFile(DWORD dwHandle);

protected:
    IFile* FindOrCreateFileObject(DWORD* pdwHandle, const std::string& strPath, int nType);
    DWORD RegisterOpenFile(IFile* pFile);
    DWORD PurgeUnusedFileEntries();
    void ReleaseFileEntry(IFile* pFile);

    typedef stdext::hash_map<DWORD, IFile*>      OpenFileMap;
    typedef stdext::hash_map<std::string, DWORD> FileNameMap;

public:
    CCriticalSectionBS m_cs;                // +4
    OpenFileMap        m_mapOpenFile;       // +64   handle -> file
    FileNameMap        m_mapCachedName;     // +104  path -> handle (cached mapped files)
    DWORD              m_dwCacheLimit;      // +144
    DWORD              m_dwCacheUsed;       // +148
    int                m_nOpenCount;        // +152  open users over all files
    int                m_nReadOpenCount;    // +156  read-mode opens (never decremented)
    DWORD              m_dwA0;              // +160
    CIDPool            m_IDPool;            // +164
    BOOL               m_bInitialized;      // +196
};

C_ASSERT(FIELD_OFFSET(CFileSystem, m_cs) == 4);
C_ASSERT(FIELD_OFFSET(CFileSystem, m_mapOpenFile) == 64);
C_ASSERT(FIELD_OFFSET(CFileSystem, m_mapCachedName) == 104);
C_ASSERT(FIELD_OFFSET(CFileSystem, m_dwCacheLimit) == 144);
C_ASSERT(FIELD_OFFSET(CFileSystem, m_IDPool) == 164);
C_ASSERT(FIELD_OFFSET(CFileSystem, m_bInitialized) == 196);
C_ASSERT(sizeof(CFileSystem) == 200);
