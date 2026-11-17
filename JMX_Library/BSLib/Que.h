#pragma once

#include <windows.h>
#include <deque>

// Free list of pooled objects. m_nMaxSize == -1 means unlimited.
template <class T>
class CQue
{
public:
    CQue() : m_nMaxSize(-1) {}
    virtual ~CQue() { m_nMaxSize = 0; }

    // Returns FALSE and sets p to NULL when empty.
    BOOL Pop(T*& p)
    {
        if (m_Que.empty())
        {
            p = NULL;
            return FALSE;
        }
        p = m_Que.front();
        m_Que.pop_front();
        return TRUE;
    }

    // Drops the object when the queue is full.
    void Push(T* p)
    {
        if (m_nMaxSize == -1 || m_Que.size() < (unsigned)m_nMaxSize)
            m_Que.push_back(p);
    }

    size_t GetSize() const { return m_Que.size(); }

public:
    LONG           m_nMaxSize;  // +4
    std::deque<T*> m_Que;       // +8
};
