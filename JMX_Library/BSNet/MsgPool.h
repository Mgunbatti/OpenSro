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

    CMsgPool() : CChunkAllocatorMT<CMsg>(&CMsg::Reset, 0, 500) {}

    CMsg* Alloc();
    void Free(CMsg* pMsg);

    // CNetEngine_MsgPool_DumpEntries (0x482c60): counts the handed-out messages per opcode (the
    // inner opcode at data +10 for 0x2209 relay messages) and passes every count, then
    // (0xFFFF, total), to the receiver. Nothing without a receiver.
    void DumpEntries();

private:
    CMsg* AllocFromFreeQueue();

public:
    std::set<CMsg*> m_setInUse;        // +0x7C
};

C_ASSERT(FIELD_OFFSET(CMsgPool, m_FreeQue) == 0x0C);
C_ASSERT(FIELD_OFFSET(CMsgPool, m_Blocks) == 0x28);
C_ASSERT(FIELD_OFFSET(CMsgPool, m_nInUse) == 0x38);
C_ASSERT(FIELD_OFFSET(CMsgPool, m_cs) == 0x40);
C_ASSERT(FIELD_OFFSET(CMsgPool, m_setInUse) == 0x7C);
