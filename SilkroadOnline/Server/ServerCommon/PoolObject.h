#pragma once

#include "BSLib/ChunkAllocator.h"

// The pooled objects of the shard (CMessenger, Friend, Memo, TrainingCampMember, ...): a static
// CChunkAllocatorST<T> (T::s_Pool) hands the objects out with an in-use byte set; FreePtr and the
// Release slot clear it, reset the object (T::ResetForPool) and give it back.

template <class T>
T* PoolObject_Alloc()
{
    T* p = T::s_Pool.Alloc();
    p->m_bInUse = 1;
    return p;
}

// e.g. CMessenger_FreePtr (0x445f30), TrainingCampMember::Release (0x45d990)
template <class T>
void PoolObject_FreePtr(T*& p)
{
    if (p)
    {
        if (p->m_bInUse)
        {
            p->m_bInUse = 0;
            p->ResetForPool();
            T::s_Pool.Free(p);
        }
        p = NULL;
    }
}

// "why do you about to delete already deleted !!! [%s]" and an assert (Messenger.cpp).
void PoolObject_LogDoubleRelease(const char* pszName);

// The Release slot, e.g. CMessenger::vf01 (0x445f60), TrainingCampMember::vf00 (0x45d9c0)
template <class T>
void PoolObject_Release(T* p, const char* pszName)
{
    if (p->m_bInUse)
    {
        p->m_bInUse = 0;
        p->ResetForPool();
        T::s_Pool.Free(p);
    }
    else
    {
        PoolObject_LogDoubleRelease(pszName);
    }
}
