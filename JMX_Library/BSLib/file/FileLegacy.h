#pragma once

#include "IFile.h"

// File accessed with ReadFile / WriteFile. Reads seek to the context's position and rewind
// afterwards; writes ignore the position and continue at the file pointer.
class CFileLegacy : public IFile
{
public:
    CFileLegacy();
    virtual ~CFileLegacy();

    virtual DWORD ReadAt(DWORD* pdwPos, void* pBuffer, DWORD dwSize);
    virtual DWORD WriteAt(DWORD* pdwPos, const void* pBuffer, DWORD dwSize);
    virtual void Close();
    virtual BOOL Flush();
    virtual HANDLE GetHandle();
    virtual BOOL IsOpen();
    // Write mode: CREATE_ALWAYS and set to dwSize bytes. Read mode: OPEN_EXISTING, must not
    // be empty.
    virtual int Open(const std::string& strPath, DWORD dwSize, DWORD dwContext);
    virtual void* GetMemory();
    virtual int GetType();

protected:
    BOOL SeekTo(DWORD* pdwPos);
    BOOL Rewind();

public:
    HANDLE m_hFile;     // +152
};

C_ASSERT(FIELD_OFFSET(CFileLegacy, m_hFile) == 152);
C_ASSERT(sizeof(CFileLegacy) == 0x9C);
