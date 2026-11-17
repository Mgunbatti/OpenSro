#pragma once

#include <windows.h>
#include <string>
#include <hash_map>

#include "BSLib/thread/SynchObject.h"

// Copies pszSrc into strDst with 'A'..'Z' lowered. A byte with the high bit set is taken as a
// DBCS lead byte and copied together with the following byte unchanged.
void StringToLowerMbcs(const char* pszSrc, std::string& strDst);

// Error codes returned by the Open functions.
enum
{
    FILE_ERR_ALREADY_OPENED_BY_USER = -1,   // the context already has the file open
    FILE_ERR_EMPTY_FILE             = -3,   // read mode and the file has no data
    FILE_ERR_CREATE_MAPPING         = -4,   // CreateFileMappingA failed
    FILE_ERR_MAP_VIEW               = -5,   // MapViewOfFile failed
    FILE_ERR_UNKNOWN_USER           = -6,   // Seek: no such context
    FILE_ERR_WRITE_SHARED           = -10,  // a file open for writing is not shared
    FILE_ERR_MODE_MISMATCH          = -11,  // already open in the other mode
    FILE_ERR_CREATE_FILE            = -13,  // CreateFileA failed
    FILE_ERR_TYPE_MISMATCH          = -14,  // cached file has a different implementation
    FILE_ERR_NO_HANDLE              = -18,  // no file handle could be assigned
    FILE_ERR_CREATE_OBJECT          = -19,  // unknown file type
};

// Implementations returned by GetType() (CFileSystem::OpenFile's nType).
enum
{
    FILE_TYPE_MEMORY = 0,   // CFileMem
    FILE_TYPE_LEGACY = 1,   // CFileLegacy (ReadFile / WriteFile)
    FILE_TYPE_MAPPED = 2,   // CFileMM (memory-mapped file)
};

// File shared by several users (contexts). Each context that opened the file has its own read
// or write position; the file is in one mode for everybody: read (0) or write (1). A file is
// opened for writing when Open gets a non-zero size and is then truncated or grown to it.
class IFile
{
public:
    IFile();
    virtual ~IFile();

    // Raw transfer at *pdwPos (not advanced). Return the byte count.
    virtual DWORD ReadAt(DWORD* pdwPos, void* pBuffer, DWORD dwSize) = 0;
    virtual DWORD WriteAt(DWORD* pdwPos, const void* pBuffer, DWORD dwSize) = 0;
    // Drops the context; returns how many contexts still have the file open.
    virtual int ReleaseHandle(DWORD dwContext);
    virtual void Close() = 0;
    virtual BOOL Flush() = 0;
    virtual HANDLE GetHandle() = 0;
    virtual BOOL IsOpen() = 0;
    // Base part: sets the mode and path on first open, checks a further open against them.
    virtual int Open(const std::string& strPath, DWORD dwSize, DWORD dwContext);
    virtual void* GetMemory();
    virtual void* AttachMemory(void* pMemory, DWORD dwSize);
    virtual int GetType();

    // Reads the next block (at most 4090 bytes) for the context and advances its position.
    int Read(DWORD dwContext, void* pBuffer);
    // Writes up to nSize bytes (clamped to the file size) and advances the context's position.
    int Write(DWORD dwContext, const void* pBuffer, int nSize);
    // Only checks that the context has the file open: 0 or FILE_ERR_UNKNOWN_USER.
    int Seek(DWORD dwContext);
    // Position of the context, 0 if it has not opened the file.
    DWORD GetPosition(DWORD dwContext);
    // Number of contexts that have the file open.
    int GetUserCount();
    DWORD GetRemainingSize(DWORD* pdwPos);

    DWORD GetFileHandle() const { return m_dwHandle; }
    DWORD GetSize() const { return m_dwSize; }
    int GetMode() const { return m_nMode; }
    const std::string& GetPath() const { return m_strPath; }

    // Registers the context with position 0; FILE_ERR_ALREADY_OPENED_BY_USER if present. Also
    // called by the session file transfer (FileTransfer_Finish, context 0).
    int AddUser(DWORD dwContext);

protected:
    // Frees the positions and returns the object to the closed state.
    void Reset();
    DWORD* FindPosition(DWORD dwContext);
    int RemoveUser(DWORD dwContext);

    typedef stdext::hash_map<DWORD, DWORD*> PositionMap;

public:
    int                m_nMode;         // +4    -1 closed, 0 read, 1 write
    int                m_nKind;         // +8    0 CFileMem, 1 CFileLegacy, -1 otherwise
    DWORD              m_dwHandle;      // +12   CFileSystem handle, 0 if not registered
    DWORD              m_dwSize;        // +16
    std::string        m_strPath;       // +20   lower case
    PositionMap        m_mapPosition;   // +48   context -> position
    CCriticalSectionBS m_cs;            // +88
    int                m_nOpenCount;    // +148  successful AddUser calls since the last Reset
};

C_ASSERT(FIELD_OFFSET(IFile, m_strPath) == 20);
C_ASSERT(FIELD_OFFSET(IFile, m_mapPosition) == 48);
C_ASSERT(FIELD_OFFSET(IFile, m_cs) == 88);
C_ASSERT(FIELD_OFFSET(IFile, m_nOpenCount) == 148);
C_ASSERT(sizeof(IFile) == 152);
