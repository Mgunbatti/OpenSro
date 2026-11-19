#pragma once

#include <string.h>

#include <string>

#include "BSLib/BSDebug.h"

// The bounded byte stream the instance records are written to / read from (the ByteStream_*
// functions of the analysis; the owner of the buffer is not part of the records). Writes past
// the capacity assert and throw (int)0; reads past the written part assert and read anyway.
class CByteStream
{
public:
    // 0x445af0 (BYTE), 0x653d30 (WORD), 0x445b40 (DWORD), 0x532c10 (__int64)
    template <class T>
    CByteStream& Write(T value)
    {
        if (m_pBuffer == NULL)
            BSAssertFail();
        if (m_nWritePos + (int)sizeof(T) > m_nCapacity)
            BSAssertFailAndThrow();
        *(T*)(m_pBuffer + m_nWritePos) = value;
        m_nWritePos += sizeof(T);
        return *this;
    }

    // 0x445ed0
    CByteStream& WriteBytes(const void* pData, WORD wSize)
    {
        if (m_pBuffer == NULL)
            BSAssertFail();
        if (m_nWritePos + wSize > m_nCapacity)
            BSAssertFailAndThrow();
        memcpy(m_pBuffer + m_nWritePos, pData, wSize);
        m_nWritePos += wSize;
        return *this;
    }

    // 0x445ea0: WORD length (lstrlenA), then the characters.
    CByteStream& WriteString(const char* psz)
    {
        WORD wLength = (WORD)lstrlenA(psz);
        Write<WORD>(wLength);
        if (wLength >= 1)
            WriteBytes(psz, wLength);
        return *this;
    }

    template <class T>
    CByteStream& Read(T& value)
    {
        if (m_nReadPos + (int)sizeof(T) > m_nWritePos)
            BSAssertFail();
        value = *(T*)(m_pBuffer + m_nReadPos);
        m_nReadPos += sizeof(T);
        return *this;
    }

    // A read whose value is not used.
    CByteStream& Skip(int nSize)
    {
        if (m_nReadPos + nSize > m_nWritePos)
            BSAssertFail();
        m_nReadPos += nSize;
        return *this;
    }

    // 0x61b6e0: WORD length, then the characters.
    CByteStream& ReadString16(std::string& str)
    {
        if (m_nReadPos + 2 > m_nWritePos)
            BSAssertFail();
        WORD wLength = *(WORD*)(m_pBuffer + m_nReadPos);
        m_nReadPos += 2;
        if (wLength != 0)
        {
            if (wLength + m_nReadPos > m_nWritePos)
                BSAssertFail();
            str.resize(wLength);
            if ((int)(wLength + m_nReadPos) > m_nWritePos)
                BSAssertFail();
            memcpy(&str[0], m_pBuffer + m_nReadPos, wLength);
            m_nReadPos += wLength;
        }
        return *this;
    }

public:
    int     m_nReadPos;     // +0
    int     m_nWritePos;    // +4  end of the data
    DWORD   m_dw08;         // +8
    int     m_nCapacity;    // +C
    BYTE*   m_pBuffer;      // +10
};

C_ASSERT(sizeof(CByteStream) == 0x14);
