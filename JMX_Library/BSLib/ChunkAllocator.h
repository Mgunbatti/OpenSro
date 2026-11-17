#pragma once

#include <windows.h>
#include <list>
#ifdef JMX_FRAMEWORK_2011
#include <string.h>
#include <string>
#endif

#include "BSLib/BSDebug.h"
#include "BSLib/Que.h"
#include "BSLib/thread/SynchObject.h"

void NetEngineLog(DWORD dwFlags, const char* pszFormat, ...);

// Flags of the AllocItemBlock failure log.
#ifdef JMX_FRAMEWORK_2011
// AgentServer 0x468170 (and the other instances)
#define CHUNK_ALLOCATOR_LOG_FLAGS 0x2000000
#else
#define CHUNK_ALLOCATOR_LOG_FLAGS 0x2000001
#endif

// Pool that allocates T in arrays of m_nItemsPerBlock and hands them out from a free list.
// m_pfnInit (may be NULL) runs on every object taken from the free list.
template <class T>
class CChunkAllocatorST
{
public:
    typedef void (T::*PFN_INIT)(DWORD);

#ifdef JMX_FRAMEWORK_2011
    // AgentServer 0x46f8d0: the pool has a name (m_strName), "Unknown" when none is given.
    CChunkAllocatorST(PFN_INIT pfnInit = NULL, DWORD dwInitParam = 0, int nItemsPerBlock = 500,
                      const char* pszName = NULL)
    {
        m_nItemsPerBlock = nItemsPerBlock;
        m_nInUse = 0;
        m_pfnInit = pfnInit;
        m_dwInitParam = dwInitParam;
        m_dw4 = 0;
        m_nCapacity = 0;
        SetName(pszName);
    }

    // 0x46e980: NULL or empty -> "Unknown".
    void SetName(const char* pszName)
    {
        if (pszName && lstrlenA(pszName) > 0)
            m_strName.assign(pszName, strlen(pszName));
        else
            m_strName.assign("Unknown", 7);
    }
#else
    CChunkAllocatorST(PFN_INIT pfnInit = NULL, DWORD dwInitParam = 0, int nItemsPerBlock = 500)
    {
        m_pfnInit = pfnInit;
        m_dw4 = 0;
        m_nItemsPerBlock = nItemsPerBlock;
        m_dwInitParam = dwInitParam;
        m_nInUse = 0;
        m_nCapacity = 0;
    }
#endif

    ~CChunkAllocatorST()
    {
        FreeAllBlocks();
    }

    T* Alloc()
    {
        T* p = PopFree();
        if (!p)
        {
            AllocItemBlock();
            p = PopFree();
        }
        ++m_nInUse;
        return p;
    }

    void Free(T* p)
    {
        if (!p)
        {
            BSAssertFail();
            return;
        }
        m_FreeQue.Push(p);
        --m_nInUse;
    }

    T* PopFree()
    {
        T* p;
        m_FreeQue.Pop(p);
        if (p && m_pfnInit)
            (p->*m_pfnInit)(m_dwInitParam);
        return p;
    }

    // Adds one block when the free list is empty.
    BOOL AllocItemBlock()
    {
        if (m_FreeQue.GetSize())
            return FALSE;

        int n = m_nItemsPerBlock;
        T* pBlock = new T[n];
        if (!pBlock)
        {
            NetEngineLog(CHUNK_ALLOCATOR_LOG_FLAGS, "CChunkAllocatorST::AllocItemBlock() Failed");
            return FALSE;
        }
        m_Blocks.push_back(pBlock);
        for (int i = 0; i < m_nItemsPerBlock; ++i)
            m_FreeQue.Push(&pBlock[i]);
        m_nCapacity = m_nItemsPerBlock * (DWORD)m_Blocks.size();
        return TRUE;
    }

    void FreeAllBlocks()
    {
        for (typename std::list<T*>::iterator it = m_Blocks.begin(); it != m_Blocks.end(); ++it)
            delete[] *it;
        m_Blocks.clear();
    }

    void GetStats(DWORD& dwTotal, DWORD& dwUsed, DWORD& dwFree)
    {
        dwTotal = m_nCapacity;
        dwUsed = m_nInUse;
        if (m_nInUse > m_nCapacity)
            BSAssertFail();
        dwFree = dwTotal - dwUsed;
    }

public:
    PFN_INIT      m_pfnInit;            // +0x00
    DWORD         m_dw4;                // +0x04
    int           m_nItemsPerBlock;     // +0x08
    CQue<T>       m_FreeQue;            // +0x0C
    std::list<T*> m_Blocks;             // +0x28
    DWORD         m_dwInitParam;        // +0x34
    DWORD         m_nInUse;             // +0x38
    DWORD         m_nCapacity;          // +0x3C
#ifdef JMX_FRAMEWORK_2011
    std::string   m_strName;            // +0x40
#endif
};

// Same pool behind a critical section.
template <class T>
class CChunkAllocatorMT : public CChunkAllocatorST<T>
{
public:
    typedef typename CChunkAllocatorST<T>::PFN_INIT PFN_INIT;

#ifdef JMX_FRAMEWORK_2011
    // AgentServer 0x46dd20
    CChunkAllocatorMT(PFN_INIT pfnInit = NULL, DWORD dwInitParam = 0, int nItemsPerBlock = 500,
                      const char* pszName = NULL)
        : CChunkAllocatorST<T>(pfnInit, dwInitParam, nItemsPerBlock, pszName)
    {
    }
#else
    CChunkAllocatorMT(PFN_INIT pfnInit = NULL, DWORD dwInitParam = 0, int nItemsPerBlock = 500)
        : CChunkAllocatorST<T>(pfnInit, dwInitParam, nItemsPerBlock)
    {
    }
#endif

    ~CChunkAllocatorMT()
    {
        CSingleLockBS lock(&m_cs, TRUE);
        this->FreeAllBlocks();
    }

    T* Alloc()
    {
        CSingleLockBS lock(&m_cs, TRUE);
        return CChunkAllocatorST<T>::Alloc();
    }

    void Free(T* p)
    {
        CSingleLockBS lock(&m_cs, TRUE);
        CChunkAllocatorST<T>::Free(p);
    }

    void GetStats(DWORD& dwTotal, DWORD& dwUsed, DWORD& dwFree)
    {
        CSingleLockBS lock(&m_cs, TRUE);
        CChunkAllocatorST<T>::GetStats(dwTotal, dwUsed, dwFree);
    }

public:
    CCriticalSectionBS m_cs;            // +0x40 (+0x5C with JMX_FRAMEWORK_2011)
};
