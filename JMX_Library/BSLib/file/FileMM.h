#pragma once

#include "IFile.h"

// Memory-mapped file. The whole file is mapped on open.
class CFileMM : public IFile
{
public:
    CFileMM();
    virtual ~CFileMM();

    virtual DWORD ReadAt(DWORD* pdwPos, void* pBuffer, DWORD dwSize);
    virtual DWORD WriteAt(DWORD* pdwPos, const void* pBuffer, DWORD dwSize);
    virtual void Close();
    virtual BOOL Flush();
    virtual HANDLE GetHandle();
    virtual BOOL IsOpen();
    // OPEN_ALWAYS in both modes. Write mode maps dwSize bytes (the file grows to that size,
    // existing data is kept). Read mode maps the whole file, which must not be empty.
    virtual int Open(const std::string& strPath, DWORD dwSize, DWORD dwContext);
    // Returns the mapped view.
    virtual void* GetMemory();
    virtual int GetType();

public:
    HANDLE m_hFile;     // +152
    HANDLE m_hMapping;  // +156
    BYTE*  m_pView;     // +160
};

C_ASSERT(FIELD_OFFSET(CFileMM, m_hFile) == 152);
C_ASSERT(FIELD_OFFSET(CFileMM, m_pView) == 160);
C_ASSERT(sizeof(CFileMM) == 0xA4);
