#pragma once

#include <set>

#include "BSLib/ChunkAllocator.h"
#include "BSNet/Msg.h"

// CMsg pool owned by the network engine. On top of the locked chunk allocator it tracks every
// handed-out message in a set and uses CMsg::m_lInUse to catch double allocation and double free.
class CMsgPool : public CChunkAllocatorMT<CMsg>
{
public:
    CMsgPool() : CChunkAllocatorMT<CMsg>(&CMsg::Reset, 0, 500) {}

    CMsg* Alloc();
    void Free(CMsg* pMsg);

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
