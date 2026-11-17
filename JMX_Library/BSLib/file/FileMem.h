#pragma once

#include "IFile.h"

// File held in a heap buffer. Nothing touches the disk.
class CFileMem : public IFile
{
public:
    CFileMem();
    virtual ~CFileMem();

    virtual DWORD ReadAt(DWORD* pdwPos, void* pBuffer, DWORD dwSize);
    virtual DWORD WriteAt(DWORD* pdwPos, const void* pBuffer, DWORD dwSize);
    virtual void Close();
    virtual BOOL Flush();
    virtual HANDLE GetHandle();
    virtual BOOL IsOpen();
    virtual int Open(const std::string& strPath, DWORD dwSize, DWORD dwContext);
    // Returns the buffer.
    virtual void* GetMemory();
    // Replaces buffer and size without freeing anything; returns the previous buffer.
    virtual void* AttachMemory(void* pMemory, DWORD dwSize);
    virtual int GetType();

public:
    BYTE* m_pBuffer;    // +152  new[]
};

C_ASSERT(FIELD_OFFSET(CFileMem, m_pBuffer) == 152);
C_ASSERT(sizeof(CFileMem) == 0x9C);
