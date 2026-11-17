#include "MassiveMsg.h"

#include <strsafe.h>

IBSNet* g_pBSNet = NULL;

namespace ServerFramework
{

CChunkAllocatorMT<CMassiveMsg> g_MassiveMsgPool MASSIVE_MSG_POOL_ARGS;

CMassiveMsg::CMassiveMsg()
{
    m_bFromPool = 0;
    m_nMode = 2;
    m_pCurMsg = NULL;
    m_nCurrentReadMsg = 0;
    m_wOpcode = 0;
}

CMassiveMsg* CMassiveMsg::Alloc(int nMode)
{
    CMassiveMsg* p = g_MassiveMsgPool.Alloc();
    p->m_nMode = nMode;
    p->m_bFromPool = 1;
    p->m_bEncrypted = 0;
    p->m_pCurMsg = NULL;
    return p;
}

void CMassiveMsg::Free()
{
    ReleaseMsgs();
    if (m_bFromPool)
    {
        m_bFromPool = 0;
        g_MassiveMsgPool.Free(this);
    }
}

void CMassiveMsg::ReleaseMsgs()
{
    for (std::vector<CMsg*>::iterator it = m_MsgList.begin(); it != m_MsgList.end(); ++it)
    {
        CMsg* pMsg = *it;
        pMsg->SkipRemaining();
        g_pBSNet->ReleaseMsg(pMsg);
    }
    m_MsgList.clear();
}

void CMassiveMsg::BeginNewPacket()
{
    CMsg* pMsg = g_pBSNet->AllocMsg(m_bEncrypted);
    pMsg->SetOpcode(MASSIVE_MSG_OPCODE);
    BYTE bPart = 0;
    pMsg->WriteBytes(&bPart, 1);
    m_MsgList.push_back(pMsg);
    m_pCurMsg = pMsg;
}

static int RemainingSpace(CMsg* pMsg)
{
    if (pMsg->m_wWritePos > pMsg->m_nCapacity)
        BSAssertFail();
    return pMsg->m_nCapacity - pMsg->m_wWritePos;
}

// BUG(SRVFW-002): a write that does not fit is split over exactly one new packet; more than
// one packet's worth at once makes CMsg::WriteBytes throw.
void CMassiveMsg::Write(const void* pSrc, int nLen)
{
    if (m_nMode != MASSIVE_MODE_WRITE)
        BSAssertFail();

    CMsg* pCur = m_pCurMsg;
    if (!pCur)
    {
        BeginNewPacket();
    }
    else
    {
        if (pCur->m_wWritePos > pCur->m_nCapacity)
            BSAssertFail();
        if ((WORD)pCur->m_nCapacity == pCur->m_wWritePos)
        {
            BeginNewPacket();
        }
        else
        {
            if (pCur->m_wWritePos > pCur->m_nCapacity)
                BSAssertFail();
            if (nLen > (WORD)(pCur->m_nCapacity - pCur->m_wWritePos))
            {
                WORD wFirst = (WORD)RemainingSpace(pCur);
                BeginNewPacket();
                pCur->WriteBytes(pSrc, wFirst);
                m_pCurMsg->WriteBytes((const BYTE*)pSrc + wFirst, (WORD)(nLen - wFirst));
                return;
            }
        }
    }
    m_pCurMsg->WriteBytes(pSrc, (WORD)nLen);
}

void CMassiveMsg::WriteString(const char* psz)
{
    WORD wLen = (WORD)lstrlenA(psz);
    Write(&wLen, 2);
    if (wLen)
        Write(psz, wLen);
}

// BUG(SRVFW-002): like Write, crosses at most one part boundary; CMsg::ReadBytes throws for more.
void CMassiveMsg::Read(void* pDst, int nLen)
{
    if (m_nMode != MASSIVE_MODE_READ)
        BSAssertFail();

    CMsg* pCur = m_pCurMsg;
    if (nLen <= pCur->GetUnreadSize())
    {
        pCur->ReadBytes(pDst, (WORD)nLen);
        return;
    }

    if (pCur->m_wWritePos == pCur->m_wReadPos)
    {
        ++m_nCurrentReadMsg;
        BS_ASSERT_DIALOG((size_t)m_nCurrentReadMsg < m_MsgList.size());
        m_pCurMsg = m_MsgList[m_nCurrentReadMsg];
        m_pCurMsg->ReadBytes(pDst, (WORD)nLen);
        return;
    }

    WORD wFirst = pCur->GetUnreadSize();
    pCur->ReadBytes(pDst, wFirst);
    ++m_nCurrentReadMsg;
    BS_ASSERT_DIALOG((size_t)m_nCurrentReadMsg < m_MsgList.size());
    m_pCurMsg = m_MsgList[m_nCurrentReadMsg];
    m_pCurMsg->ReadBytes((BYTE*)pDst + wFirst, (WORD)(nLen - wFirst));
}

void CMassiveMsg::SendTo(DWORD dwSessionID, BOOL bKeep)
{
    if (m_MsgList.size())
    {
        CMsg* pHeader = g_pBSNet->AllocMsg(m_bEncrypted);
        pHeader->SetOpcode(MASSIVE_MSG_OPCODE);
        BYTE bHeader = 1;
        WORD wCount = (WORD)m_MsgList.size();
        pHeader->WriteBytes(&bHeader, 1);
        pHeader->WriteBytes(&wCount, 2);
        pHeader->WriteBytes(&m_wOpcode, 2);
        g_pBSNet->SendMsg(dwSessionID, pHeader);
        g_pBSNet->ReleaseMsg(pHeader);

        for (std::vector<CMsg*>::iterator it = m_MsgList.begin(); it != m_MsgList.end(); ++it)
            g_pBSNet->SendMsg(dwSessionID, *it);
    }

    if (!bKeep)
    {
        ReleaseMsgs();
        Free();
    }
}

void CMassiveMsg::SetRecvHeader(WORD wOpcode, WORD wPartCount)
{
    m_wPendingParts = wPartCount;
    m_wOpcode = wOpcode;
    if (m_MsgList.size())
        BSAssertFail();
}

// Returns true once the last part arrived.
bool CMassiveMsg::AddRecvPart(CMsg* pMsg)
{
    if (!m_wPendingParts)
        BSAssertFail();
    InterlockedIncrement(&pMsg->m_lRefCount);
    m_MsgList.push_back(pMsg);
    m_nCurrentReadMsg = 0;
    m_pCurMsg = *m_MsgList.begin();
    return --m_wPendingParts == 0;
}

// Switches a filled message to reading and skips the part byte of every packet.
void CMassiveMsg::BeginRead()
{
    if (m_nMode != MASSIVE_MODE_WRITE)
        BSAssertFail();
    m_nCurrentReadMsg = 0;
    m_pCurMsg = m_MsgList[0];
    for (std::vector<CMsg*>::iterator it = m_MsgList.begin(); it != m_MsgList.end(); ++it)
    {
        BYTE bPart;
        (*it)->Read(bPart);
    }
    m_nMode = MASSIVE_MODE_READ;
}

int CMassiveMsg::GetUnreadSize()
{
    int n = 0;
    for (std::vector<CMsg*>::iterator it = m_MsgList.begin(); it != m_MsgList.end(); ++it)
        n += (*it)->GetUnreadSize();
    return n;
}

bool CMassiveMsg::AppendFrom(CMassiveMsg* pSrc, BOOL bUnreadOnly)
{
    if (!pSrc->m_MsgList.size())
        return false;

    for (std::vector<CMsg*>::iterator it = pSrc->m_MsgList.begin(); it != pSrc->m_MsgList.end(); ++it)
    {
        CMsg* pMsg = *it;
        if (!pMsg->GetPayloadSize())
            return false;
        if (bUnreadOnly)
        {
            Write(pMsg->m_pData + pMsg->m_wReadPos, pMsg->GetUnreadSize());
        }
        else
        {
            WORD wPayload = pMsg->GetPayloadSize();
            if (pMsg->m_nCapacity <= MSG_HEADER_SIZE)
                BSAssertFail();
            Write(pMsg->m_pData + MSG_HEADER_SIZE + 1, wPayload - 1);
        }
    }
    return true;
}

char* CMassiveMsg::FormatDesc(char* pszOut)
{
    *pszOut = 0;
    const char* pszDir;
    if (m_wOpcode & MSG_OP_REQ)
        pszDir = "Req";
    else if (m_wOpcode & MSG_OP_ACK)
        pszDir = "Ack";
    else
        pszDir = "NoDir";

    if (StringCchPrintfA(pszOut, 0xFF, "(Massive)%s:0x%x (%d), %d", pszDir, m_wOpcode & 0xFFF,
                         m_wOpcode >> 12, GetUnreadSize()))
    {
        BSAssertFail();
        return NULL;
    }
    return pszOut;
}

// BUG(SRVFW-003): two early returns leave the read cursor advanced by the type byte.
WORD GetMassiveOpcode(CMsg* pMsg, CMassiveMsg* pMassive)
{
    if (!pMsg)
        return 0;

    WORD wSavedPos = pMsg->m_wReadPos;
    WORD wUnread = pMsg->GetUnreadSize();
    WORD wOpcode = 0;
    if (!wUnread)
        return 0;

    BYTE bType;
    pMsg->Read(bType);
    if (bType == 1)
    {
        if (pMassive)
            return 0;
        if (wUnread >= 5)
        {
            WORD wCount;
            pMsg->Read(wCount);
            pMsg->Read(wOpcode);
        }
    }
    else
    {
        if (!pMassive)
            return 0;
        wOpcode = pMassive->m_wOpcode;
    }

    if (wSavedPos >= MSG_HEADER_SIZE && wSavedPos <= pMsg->m_nCapacity)
        pMsg->m_wReadPos = wSavedPos;
    return wOpcode;
}

} // namespace ServerFramework
