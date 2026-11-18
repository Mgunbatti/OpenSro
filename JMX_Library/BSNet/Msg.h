#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>

#include "BSLib/BSDebug.h"

// Network message. One object is both the overlapped I/O request and the packet buffer.
//
// Packet header (6 bytes at the start of the data buffer):
//   +0 WORD  payload size (bits 0-14) | 0x8000 encrypted
//   +2 WORD  opcode (0x4000 request, 0x8000 ack, high nibble = sub type)
//   +4 BYTE  security count
//   +5 BYTE  security crc
//
// Reads normally advance the read cursor. With m_bReadForward == 0 the fixed-size reads take
// their value from the end of the payload instead and shrink the size field.

enum
{
    MSG_HEADER_SIZE      = 6,
    MSG_INLINE_BUF_SIZE  = 4096,
    MSG_SIZE_MASK        = 0x7FFF,
    MSG_ENCRYPTED        = 0x8000,

    MSG_OP_REQ           = 0x4000,
    MSG_OP_ACK           = 0x8000,
};

#ifdef JMX_FRAMEWORK_2011
// Exceptions of the 2011 library. Nothing catches them by type: a short read in a packet handler
// ends in the __except of the server process message loop like the 2008 access violation (the
// session is closed), but destructors run on the way.
class CInternalException
{
public:
    CInternalException() : m_dw4(0) {}
    virtual ~CInternalException() {}                                            // 0x408120
    virtual const char* GetName() { return "CInternalException"; }              // 0x408190

public:
    DWORD   m_dw4;      // +4
};

C_ASSERT(sizeof(CInternalException) == 8);

// Thrown by the CMsg readers on a short read (AgentServer 0x4693f0..0x469630).
class CMsgException : public CInternalException
{
public:
    CMsgException()                                                             // 0x4081f0
    {
        m_dw4 = 1;
        m_dw8 = 1;
    }
    virtual const char* GetName() { return "CMsgException"; }                   // 0x408150

public:
    DWORD   m_dw8;      // +8
};

C_ASSERT(sizeof(CMsgException) == 0xC);

#define MSG_READ_FAILED() throw CMsgException()
#else
#define MSG_READ_FAILED() BS_CRASH()
#endif

class CMsg
{
public:
    CMsg();
    ~CMsg();

    // Also the pool's init callback, hence the unused parameter.
    void Reset(DWORD dwUnused = 0);

    // header
    WORD GetOpcode() const                { return *m_pOpcode; }
    void SetOpcode(WORD wOpcode)          { *m_pOpcode = wOpcode; }
    BYTE GetCountByte() const             { return *m_pCount; }
    void SetCountByte(BYTE b)             { *m_pCount = b; }
    BYTE GetCrcByte() const               { return *m_pCrc; }
    void SetCrcByte(BYTE b)               { *m_pCrc = b; }
    void SetEncryptFlag()                 { *m_pSize |= MSG_ENCRYPTED; }
    void ClearEncryptFlag()               { *m_pSize &= ~MSG_ENCRYPTED; }
    WORD GetPayloadSize() const           { return *m_pSize & MSG_SIZE_MASK; }
    void SetPayloadSize(WORD wSize);

    WORD GetUnreadSize() const            { return (WORD)(m_wWritePos - m_wReadPos); }
    void SkipRemaining()                  { m_wReadPos = m_wWritePos; }
    BYTE* PtrAt(int nOffset);

    // buffers
    void AttachBuffer(BYTE* pBuf, int nCapacity);
    void AllocLargeBuffer(WORD wSize);
    void FreeExtBuffer();

    // raw access; both throw (int)0 on overrun
    void WriteBytes(const void* pSrc, WORD wLen);
    void ReadBytes(void* pDst, WORD wLen);

    // CMsg::CopyPayloadFrom (SR_ShardManager 0x41b5a0, not linked by the earlier modules):
    // appends the unread part of pSrc (nMode 1) or its whole payload (other values) and marks
    // pSrc as read. Throws (int)0 when this message has no room.
    void CopyPayloadFrom(CMsg* pSrc, int nMode);

    // BUG(BSNET-002): a short packet crashes the process here and in PopBack (2011: throws
    // CMsgException).
    template <typename T> CMsg& Read(T& v)
    {
        if (m_bReadForward)
        {
            if (m_wReadPos + sizeof(T) > m_wWritePos)
                MSG_READ_FAILED();
            v = *(T*)(m_pData + m_wReadPos);
            m_wReadPos += sizeof(T);
        }
        else
        {
            PopBack(v);
        }
        return *this;
    }

    // Reads from the end of the payload and shrinks the size field.
    template <typename T> void PopBack(T& v)
    {
        if (m_wWritePos - MSG_HEADER_SIZE < (int)sizeof(T))
            MSG_READ_FAILED();
        m_wWritePos -= sizeof(T);
        v = *(T*)(m_pData + m_wWritePos);
        SetPayloadSize((WORD)(m_wWritePos - MSG_HEADER_SIZE));
    }

    template <typename T> CMsg& Write(const T& v)
    {
        WriteBytes(&v, sizeof(T));
        return *this;
    }

    template <typename T> CMsg& operator>>(T& v)        { return Read(v); }
    template <typename T> CMsg& operator<<(const T& v)  { return Write(v); }

    // length-prefixed strings (WORD count, no terminator)
    CMsg& operator<<(const char* psz);
    CMsg& operator<<(const wchar_t* psz);
    CMsg& operator<<(const std::string& s);
    CMsg& operator>>(std::string& s);
    CMsg& operator>>(std::wstring& s);

    // "Req:0x%x (%d), %d - %d" description used in logs
    char* FormatDebugString(char* pszOut) const;

public:
    OVERLAPPED      m_Overlapped;       // +0
    DWORD           m_dwIoType;         // +20, 2 after construction
    DWORD           m_dw24;             // +24
    DWORD           m_dw28;             // +28
    DWORD           m_dw32;             // +32, 1 after construction
    WSABUF          m_WsaBuf;           // +36, len set before a send, buf = m_pData
    DWORD           m_dw44;             // +44
    DWORD           m_dw48;             // +48
    BYTE            m_InlineBuf[MSG_INLINE_BUF_SIZE];   // +52
    BYTE*           m_pData;            // +4148 header + payload
    BYTE*           m_pExtBuf;          // +4152 heap buffer for oversized messages
    WORD            m_wReadPos;         // +4156
    WORD            m_wWritePos;        // +4158
    DWORD           m_dw4160;           // +4160
    volatile LONG   m_lRefCount;        // +4164
    DWORD           m_bReadForward;     // +4168
    int             m_nCapacity;        // +4172
    WORD*           m_pOpcode;          // +4176
    WORD*           m_pSize;            // +4180
    BYTE*           m_pCount;           // +4184
    BYTE*           m_pCrc;             // +4188
    DWORD           m_dwSessionID;      // +4192
    void*           m_pOwner;           // +4196
    volatile LONG   m_lInUse;           // +4200 set while handed out by the pool
    sockaddr_in     m_Addr;             // +4204 datagram peer
};

C_ASSERT(sizeof(CMsg) == 0x107C);
C_ASSERT(FIELD_OFFSET(CMsg, m_InlineBuf) == 52);
C_ASSERT(FIELD_OFFSET(CMsg, m_pData) == 4148);
C_ASSERT(FIELD_OFFSET(CMsg, m_wReadPos) == 4156);
C_ASSERT(FIELD_OFFSET(CMsg, m_bReadForward) == 4168);
C_ASSERT(FIELD_OFFSET(CMsg, m_pOpcode) == 4176);
C_ASSERT(FIELD_OFFSET(CMsg, m_dwSessionID) == 4192);
C_ASSERT(FIELD_OFFSET(CMsg, m_Addr) == 4204);
