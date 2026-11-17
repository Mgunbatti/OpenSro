#include "Msg.h"

#include <stdio.h>

CMsg::CMsg()
{
    ZeroMemory(&m_Overlapped, sizeof(m_Overlapped));
    m_dw32 = 1;
    m_bReadForward = 1;
    m_dw28 = 0;
    m_dwIoType = 2;
    m_dw24 = 0;
    m_wReadPos = 0;
    m_wWritePos = 0;
    m_dw4160 = 0;
    m_lRefCount = 0;
    // BUG(BSNET-004): the original copies m_pData into m_WsaBuf.buf before it is set; not
    // reproduced (AttachBuffer overwrites it anyway).
    m_pData = NULL;
    AttachBuffer(m_InlineBuf, MSG_INLINE_BUF_SIZE);
    m_pExtBuf = NULL;
    m_lInUse = 0;
}

CMsg::~CMsg()
{
    FreeExtBuffer();
}

void CMsg::Reset(int)
{
    m_wWritePos = MSG_HEADER_SIZE;
    m_wReadPos = MSG_HEADER_SIZE;
    m_bReadForward = 1;
    m_lRefCount = 0;
    m_dwSessionID = 0;
    m_pOwner = NULL;
    m_dw4160 = 0;
    AttachBuffer(m_InlineBuf, MSG_INLINE_BUF_SIZE);
    *(DWORD*)m_pData = 0;
    *(WORD*)(m_pData + 4) = 0;
    FreeExtBuffer();
}

// Points the header fields at a new buffer and carries the 6 header bytes over.
void CMsg::AttachBuffer(BYTE* pBuf, int nCapacity)
{
    BYTE* pOld = m_pData;
    if (pOld == pBuf)
        return;

    m_nCapacity = nCapacity;
    m_pOpcode = (WORD*)(pBuf + 2);
    m_pCount = pBuf + 4;
    m_pData = pBuf;
    m_pSize = (WORD*)pBuf;
    m_pCrc = pBuf + 5;
    if (pOld)
    {
        *(DWORD*)pBuf = *(DWORD*)pOld;
        *(WORD*)(pBuf + 4) = *(WORD*)(pOld + 4);
    }
    m_WsaBuf.buf = (char*)pBuf;
}

void CMsg::AllocLargeBuffer(WORD wSize)
{
    FreeExtBuffer();
    m_pExtBuf = (BYTE*)operator new(wSize);
    *(DWORD*)m_pExtBuf = *(DWORD*)m_pData;
    *(WORD*)(m_pExtBuf + 4) = *(WORD*)(m_pData + 4);
    AttachBuffer(m_pExtBuf, wSize);
}

void CMsg::FreeExtBuffer()
{
    if (m_pExtBuf)
    {
        operator delete(m_pExtBuf);
        m_pExtBuf = NULL;
    }
}

void CMsg::SetPayloadSize(WORD wSize)
{
    if (wSize > MSG_SIZE_MASK)
        BSAssertFail();
    *m_pSize = wSize | (*m_pSize & MSG_ENCRYPTED);
}

BYTE* CMsg::PtrAt(int nOffset)
{
    if (nOffset >= m_nCapacity)
        BSAssertFail();
    return m_pData + nOffset;
}

void CMsg::WriteBytes(const void* pSrc, WORD wLen)
{
    if (wLen + m_wWritePos > m_nCapacity)
        throw 0;
    memcpy(m_pData + m_wWritePos, pSrc, wLen);
    m_wWritePos += wLen;
    SetPayloadSize((WORD)(m_wWritePos - MSG_HEADER_SIZE));
}

void CMsg::ReadBytes(void* pDst, WORD wLen)
{
    if (wLen + m_wReadPos > m_wWritePos)
        throw 0;
    memcpy(pDst, m_pData + m_wReadPos, wLen);
    m_wReadPos += wLen;
}

// BUG(BSNET-001): the three string writers clamp without counting the 2-byte length word, so a
// string near the end of the buffer makes WriteBytes throw instead of being truncated.
CMsg& CMsg::operator<<(const char* psz)
{
    WORD wLen = psz ? (WORD)lstrlenA(psz) : 0;
    if (wLen + m_wWritePos >= m_nCapacity)
        wLen = (WORD)(m_nCapacity - m_wWritePos - 1);
    WriteBytes(&wLen, 2);
    if (wLen)
        WriteBytes(psz, wLen);
    return *this;
}

CMsg& CMsg::operator<<(const wchar_t* psz)
{
    WORD wLen = psz ? (WORD)lstrlenW(psz) : 0;
    if (m_wWritePos + 2 * wLen >= m_nCapacity)
        wLen = (WORD)((unsigned)(m_nCapacity - m_wWritePos - 1) >> 1);
    WriteBytes(&wLen, 2);
    if (wLen)
        WriteBytes(psz, 2 * wLen);
    return *this;
}

CMsg& CMsg::operator<<(const std::string& s)
{
    WORD wLen = (WORD)s.size();
    if (wLen + m_wWritePos >= m_nCapacity)
        wLen = (WORD)(m_nCapacity - m_wWritePos - 1);
    WriteBytes(&wLen, 2);
    if (wLen)
        WriteBytes(s.data(), wLen);
    return *this;
}

// BUG(BSNET-003): in pop-back mode both string readers return without reading.
CMsg& CMsg::operator>>(std::string& s)
{
    WORD wLen = 0;
    if (!m_bReadForward)
    {
        BSAssertFail();
        return *this;
    }
    ReadBytes(&wLen, 2);
    if (!wLen)
    {
        s.clear();
        return *this;
    }
    if (wLen >= GetUnreadSize())
        wLen = GetUnreadSize();
    s.resize(wLen);
    ReadBytes(&s[0], wLen);
    return *this;
}

CMsg& CMsg::operator>>(std::wstring& s)
{
    WORD wLen = 0;
    if (!m_bReadForward)
    {
        BSAssertFail();
        return *this;
    }
    ReadBytes(&wLen, 2);
    if (!wLen)
    {
        s.clear();
        return *this;
    }
    WORD wMax = (WORD)(GetUnreadSize() >> 1);
    if (wLen >= wMax)
        wLen = wMax;
    s.resize(wLen);
    ReadBytes(&s[0], 2 * wLen);
    return *this;
}

char* CMsg::FormatDebugString(char* pszOut) const
{
    *pszOut = 0;
    WORD wOpcode = *m_pOpcode;
    const char* pszDir;
    if (wOpcode & MSG_OP_REQ)
        pszDir = "Req";
    else if (wOpcode & MSG_OP_ACK)
        pszDir = "Ack";
    else
        pszDir = "NoDir";

    int n = _snprintf_s(pszOut, 0xFF, 0xFE, "%s:0x%x (%d), %d - %d", pszDir, wOpcode & 0xFFF,
                        wOpcode >> 12, *m_pSize & MSG_SIZE_MASK,
                        (short)(m_wWritePos - m_wReadPos));
    if (n < 0)
    {
        pszOut[0xFE] = 0;
        BSAssertFail();
        return NULL;
    }
    return pszOut;
}
