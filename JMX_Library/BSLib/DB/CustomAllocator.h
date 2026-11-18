#pragma once

#include "BSLib/ChunkAllocator.h"
#include "BSLib/DB/Database.h"

// Record allocator of a table (RTTI names CCustomAllocator<T, CChunkAllocatorMT<T>> and
// CCustomAllocator<T, CChunkAllocatorST<T>>, base _record_allocator). The application passes one
// to CDBTable::Open; records then come from a chunk pool of 1000 records per block named after
// the table instead of the record class' CreateObject. SR_ShardManager has one MT and one ST
// instance per record type (the MT one is used, e.g. CTableKeeper::RegisterTable_CRefClimate at
// 0x532c60 creates CCustomAllocator<CRefClimate, CChunkAllocatorMT<CRefClimate> > at 0x5494a0);
// the earlier modules contain no record classes.
//
// Layout: vftable +0, m_pTable +4, the pool +8 (0xA0 bytes with CChunkAllocatorMT, 0x64 with
// CChunkAllocatorST in the 2011 framework). Slots: 0 destructor, 1 AllocRecord, 2 FreeRecord,
// 3 the pool counts.
template <class T, class TPool>
class CCustomAllocator : public _record_allocator
{
public:
    enum
    {
        ITEMS_PER_BLOCK = 1000,
    };

    // 0x5494a0 (MT) / 0x5495e0 (ST): the pool is named after pInfo's table, "Unknown DB Table"
    // without one.
    CCustomAllocator(CDBTable* pTable, CDBTableInfo* pInfo)
#ifdef JMX_FRAMEWORK_2011
        : _record_allocator(pTable),
          m_Pool(NULL, 0, ITEMS_PER_BLOCK, pInfo ? pInfo->m_strTable.c_str() : "Unknown DB Table")
#else
        : _record_allocator(pTable), m_Pool(NULL, 0, ITEMS_PER_BLOCK)
#endif
    {
#ifndef JMX_FRAMEWORK_2011
        (void)pInfo;
#endif
    }

    // 0x565060 (MT) / 0x5650c0 (ST): the pool frees its blocks.
    virtual ~CCustomAllocator()                                             // 0
    {
    }

    // 0x549520 (MT, locked) / 0x549660 (ST).
    virtual CDBRecord* AllocRecord()                                        // 1
    {
        return m_Pool.Alloc();
    }

    // 0x549530 (MT, locked) / 0x549670 (ST): NULL asserts; a full free list drops the record.
    virtual void FreeRecord(CDBRecord* pRecord)                             // 2
    {
        m_Pool.Free(static_cast<T*>(pRecord));
    }

    // 0x549550 (MT, locked) / 0x55ca00 (ST, shared by every type): the records the pool holds
    // and the records in use; asserts when more are in use than allocated.
    virtual void vf03(DWORD* pdwTotal, DWORD* pdwUsed)                      // 3
    {
        DWORD dwFree;
        m_Pool.GetStats(*pdwTotal, *pdwUsed, dwFree);
    }

public:
    TPool m_Pool;       // +8
};
