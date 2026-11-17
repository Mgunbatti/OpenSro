#pragma once

#include <windows.h>
#include <vector>

#include "BSLib/ChunkAllocator.h"
#include "BSNet/IBSNet.h"
#include "BSNet/Msg.h"

// Engine the framework talks to.
extern IBSNet* g_pBSNet;

namespace ServerFramework
{

enum
{
    MASSIVE_MSG_OPCODE  = 0x600D,
    MASSIVE_MODE_READ   = 0,
    MASSIVE_MODE_WRITE  = 1,
};

// Message larger than one CMsg, sent as a header packet plus parts, all with opcode 0x600D.
// Header packet: BYTE 1, WORD part count, WORD inner opcode. Each part: BYTE 0, then data.
class CMassiveMsg
{
public:
    CMassiveMsg();
    virtual ~CMassiveMsg() {}

    static CMassiveMsg* Alloc(int nMode);
    void Free();
    void ReleaseMsgs();

    void SetOpcode(WORD wOpcode)    { m_wOpcode = wOpcode; }
    void BeginNewPacket();
    void Write(const void* pSrc, int nLen);
    void WriteString(const char* psz);
    void Read(void* pDst, int nLen);

    template <typename T> CMassiveMsg& operator<<(const T& v) { Write(&v, sizeof(T)); return *this; }
    template <typename T> CMassiveMsg& operator>>(T& v)       { Read(&v, sizeof(T)); return *this; }

    // Sends the header and every part to the session; unless bKeep, releases the parts and
    // returns the object to the pool.
    void SendTo(DWORD dwSessionID, BOOL bKeep);

    // receive side
    void SetRecvHeader(WORD wOpcode, WORD wPartCount);
    bool AddRecvPart(CMsg* pMsg);
    void BeginRead();
    int GetUnreadSize();

    // Appends the parts of another massive message (unread bytes, or whole payloads without
    // the part byte).
    bool AppendFrom(CMassiveMsg* pSrc, BOOL bUnreadOnly);

    char* FormatDesc(char* pszOut);

public:
    std::vector<CMsg*> m_MsgList;          // +4
    DWORD              m_nCurrentReadMsg;  // +20
    DWORD              m_nMode;            // +24, 2 = idle
    BYTE               m_bFromPool;        // +28
    BYTE               m_bEncrypted;       // +29, passed to AllocMsg
    WORD               m_wOpcode;          // +30
    WORD               m_wPendingParts;    // +32
    DWORD              m_dw36;             // +36
    CMsg*              m_pCurMsg;          // +40
};

C_ASSERT(sizeof(CMassiveMsg) == 0x2C);
C_ASSERT(FIELD_OFFSET(CMassiveMsg, m_nCurrentReadMsg) == 20);
C_ASSERT(FIELD_OFFSET(CMassiveMsg, m_wOpcode) == 30);
C_ASSERT(FIELD_OFFSET(CMassiveMsg, m_pCurMsg) == 40);

extern CChunkAllocatorMT<CMassiveMsg> g_MassiveMsgPool;
// Constructor arguments of g_MassiveMsgPool (a macro so that MassiveMsg.cpp keeps its lines).
#ifdef JMX_FRAMEWORK_2011
// AgentServer 0x438e10: the pool is named.
#define MASSIVE_MSG_POOL_ARGS (NULL, 0, 500, "CMassiveMsg")
#else
#define MASSIVE_MSG_POOL_ARGS
#endif

// Inner opcode of a 0x600D packet: from the header packet itself, or from pMassive for a part.
WORD GetMassiveOpcode(CMsg* pMsg, CMassiveMsg* pMassive);

} // namespace ServerFramework
