#pragma once

#include <set>

#include "BSLib/ChunkAllocator.h"
#include "BSNet/Msg.h"

// CMsg pool owned by the network engine. On top of the locked chunk allocator it tracks every
// handed-out message in a set and uses CMsg::m_lInUse to catch double allocation and double free.
// m_dw4 holds the DumpMsgPool receiver (set by CNetEngine::Initialize, 0 before).
class CMsgPool : public CChunkAllocatorMT<CMsg>
{
public:
    typedef void (__cdecl* PFN_DUMP)(DWORD dwOpcode, DWORD dwCount);

#ifdef JMX_FRAMEWORK_2011
    // AgentServer 0x46e1d0
    CMsgPool() : CChunkAllocatorMT<CMsg>(&CMsg::Reset, 0, 500, "NetEngine::MsgPool") {}
#else
    CMsgPool() : CChunkAllocatorMT<CMsg>(&CMsg::Reset, 0, 500) {}
#endif

    CMsg* Alloc();
    void Free(CMsg* pMsg);

#ifdef JMX_FRAMEWORK_2011
    // AgentServer 0x46e470: as below, and while counting, with nThreshold above 10000, the text
    // of every 0x200A log message whose opcode count is already above nThreshold / 2 (the first
    // message of an opcode never is) is appended to <MM-DD-hh-mm>_LogContent.txt in the current
    // directory, at most 1000 lines of up to 500 characters.
    void DumpEntries(int nThreshold);
#else
    // CNetEngine_MsgPool_DumpEntries (0x482c60): counts the handed-out messages per opcode (the
    // inner opcode at data +10 for 0x2209 relay messages) and passes every count, then
    // (0xFFFF, total), to the receiver. Nothing without a receiver.
    void DumpEntries();
#endif

private:
    CMsg* AllocFromFreeQueue();
#ifdef JMX_FRAMEWORK_2011
public:
    // AgentServer 0x471650: the chunk allocator's AllocItemBlock with its own failure text; when
    // the allocation throws, the handed-out messages per opcode are appended to
    // MsgAllocatorException.log (WriteAllocatorExceptionLog) and FALSE is returned.
    BOOL AllocItemBlock();
    void WriteAllocatorExceptionLog();
#endif

public:
    std::set<CMsg*> m_setInUse;        // +0x7C (present but never filled with JMX_FRAMEWORK_2007)
};

C_ASSERT(FIELD_OFFSET(CMsgPool, m_FreeQue) == 0x0C);
C_ASSERT(FIELD_OFFSET(CMsgPool, m_Blocks) == 0x28);
C_ASSERT(FIELD_OFFSET(CMsgPool, m_nInUse) == 0x38);
#ifdef JMX_FRAMEWORK_2011
C_ASSERT(FIELD_OFFSET(CMsgPool, m_strName) == 0x40);
C_ASSERT(FIELD_OFFSET(CMsgPool, m_cs) == 0x5C);
C_ASSERT(FIELD_OFFSET(CMsgPool, m_setInUse) == 0x98);
C_ASSERT(sizeof(CMsgPool) == 0xA4);
#else
C_ASSERT(FIELD_OFFSET(CMsgPool, m_cs) == 0x40);
C_ASSERT(FIELD_OFFSET(CMsgPool, m_setInUse) == 0x7C);
#endif
