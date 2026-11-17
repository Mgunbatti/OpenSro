#pragma once

#include <windows.h>
#include <deque>

// Pool of non-zero 32-bit ids. Free ids wait in a FIFO; the pool refills itself in steps of
// m_dwGrowBy fresh ids when it runs low. Id 0 is never handed out.
class CIDPool
{
public:
    CIDPool()
    {
        m_dwNextID = 1;
        m_dwGrowBy = 5000;
    }

    // Not virtual in the original (the vtable holds Grow only).
    ~CIDPool()
    {
    }

    // Queues the next m_dwGrowBy ids.
    virtual void Grow()
    {
        for (DWORD dwID = m_dwNextID; dwID < m_dwNextID + m_dwGrowBy; ++dwID)
        {
            if (dwID != 0)
                m_deqFree.push_back(dwID);
        }
        m_dwNextID += m_dwGrowBy;
    }

    // Grows when the queue is empty or holds fewer than a fifth of the step.
    DWORD Alloc()
    {
        if (m_deqFree.size() == 0 ||
            (double)m_dwGrowBy * (double)0.2f > (double)m_deqFree.size())
        {
            Grow();
        }
        DWORD dwID = m_deqFree.front();
        m_deqFree.pop_front();
        return dwID;
    }

    void Free(DWORD dwID)
    {
        m_deqFree.push_back(dwID);
    }

public:
    DWORD             m_dwGrowBy;   // +4
    DWORD             m_dwNextID;   // +8
    std::deque<DWORD> m_deqFree;    // +12
};

C_ASSERT(sizeof(CIDPool) == 32);
