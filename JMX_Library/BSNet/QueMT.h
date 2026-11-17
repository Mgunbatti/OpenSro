#pragma once

#include <windows.h>
#include <deque>

#include "BSLib/thread/SynchObject.h"

// Thread message queues. Pop returns FALSE only when the queue was stopped; on a timeout it
// returns TRUE with *pp == NULL.
template <class T>
class IQue
{
public:
    virtual ~IQue() {}

    virtual BOOL Close() = 0;
    virtual LONG GetSize() = 0;
    virtual BOOL IsEmpty() = 0;
    virtual BOOL IsFull() = 0;
    virtual BOOL Push(T* p) = 0;
    virtual BOOL Pop(T** pp, DWORD dwTimeout, BOOL bEvenIfStopped) = 0;
    virtual BOOL Stop() = 0;
    virtual LONG Start() = 0;
    virtual void Reserved() = 0;
};

// I/O completion port queue. T must start with an OVERLAPPED (CMsg does).
template <class T>
class CQueMT : public IQue<T>
{
public:
    CQueMT()
    {
        m_nConcurrency = 4;
        m_nWaiters = 0;
        m_nCount = 0;
        m_hIOCP = NULL;
        m_bActive = 1;
        m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 1, 4);
    }

    virtual ~CQueMT()
    {
        Close();
    }

    virtual BOOL Close()
    {
        CSingleLockBS lock(&m_cs, TRUE);
        Stop();
        if (m_hIOCP)
        {
            CloseHandle(m_hIOCP);
            m_hIOCP = NULL;
        }
        return TRUE;
    }

    virtual LONG GetSize()
    {
        LONG n = 0;
        InterlockedExchange(&n, m_nCount);
        return n;
    }

    virtual BOOL IsEmpty()
    {
        LONG n = 0;
        InterlockedExchange(&n, m_nCount);
        return n == 0;
    }

    virtual BOOL IsFull()
    {
        return FALSE;
    }

    virtual BOOL Push(T* p)
    {
        CSingleLockBS lock(&m_cs, TRUE);
        ULONG_PTR key = m_bActive;
        if (key && PostQueuedCompletionStatus(m_hIOCP, 4, key, (LPOVERLAPPED)p))
        {
            ++m_nCount;
            return TRUE;
        }
        return FALSE;
    }

    virtual BOOL Pop(T** pp, DWORD dwTimeout, BOOL bEvenIfStopped)
    {
        {
            CSingleLockBS lock(&m_cs, TRUE);
            if (m_bActive)
                ++m_nWaiters;
            else if (!bEvenIfStopped)
                return FALSE;
        }

        DWORD dwBytes;
        ULONG_PTR key;
        BOOL bOk = GetQueuedCompletionStatus(m_hIOCP, &dwBytes, &key, (LPOVERLAPPED*)pp, dwTimeout);

        CSingleLockBS lock(&m_cs, TRUE);
        if (m_nWaiters > 0)
            --m_nWaiters;
        if (bOk)
        {
            if (!key)
                return FALSE;           // wake-up posted by Stop
            --m_nCount;
            if (!m_bActive)
                return FALSE;
        }
        return TRUE;
    }

    // Wakes every waiting thread with an empty completion (key 0).
    virtual BOOL Stop()
    {
        CSingleLockBS lock(&m_cs, TRUE);
        if (m_bActive)
        {
            m_bActive = 0;
            for (int i = 0; i < m_nConcurrency; ++i)
                PostQueuedCompletionStatus(m_hIOCP, 0, 0, NULL);
        }
        return TRUE;
    }

    virtual LONG Start()
    {
        return InterlockedExchange(&m_bActive, 1);
    }

    virtual void Reserved()
    {
    }

    // Replaces the port; m_nConcurrency (used by Stop) is left unchanged.
    virtual BOOL RecreateCompletionPort(DWORD dwConcurrency)
    {
        if (m_hIOCP)
            CloseHandle(m_hIOCP);
        m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 1, dwConcurrency);
        return m_hIOCP != NULL;
    }

public:
    CCriticalSectionBS m_cs;            // +4
    int                m_nConcurrency;  // +64
    int                m_nWaiters;      // +68
    LONG               m_nCount;        // +72
    volatile LONG      m_bActive;       // +76
    HANDLE             m_hIOCP;         // +80
};

// Event + deque queue (Win9x has no completion ports).
template <class T>
class CQueMT9x : public IQue<T>
{
public:
    CQueMT9x()
    {
        m_bActive = 1;
        m_hEvent = NULL;
        m_nMaxSize = -1;
        Close();
        m_hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    }

    virtual ~CQueMT9x()
    {
        if (m_hEvent)
        {
            CloseHandle(m_hEvent);
            m_hEvent = NULL;
        }
    }

    virtual BOOL Close()
    {
        if (m_hEvent)
        {
            CloseHandle(m_hEvent);
            m_hEvent = NULL;
        }
        return TRUE;
    }

    virtual LONG GetSize()
    {
        CSingleLockBS lock(&m_cs, TRUE);
        return (LONG)m_Que.size();
    }

    virtual BOOL IsEmpty()
    {
        CSingleLockBS lock(&m_cs, TRUE);
        return m_Que.size() == 0;
    }

    virtual BOOL IsFull()
    {
        CSingleLockBS lock(&m_cs, TRUE);
        if (m_nMaxSize == -1)
            return FALSE;
        return m_Que.size() >= (unsigned)m_nMaxSize;
    }

    virtual BOOL Push(T* p)
    {
        CSingleLockBS lock(&m_cs, TRUE);
        if (m_nMaxSize == -1 || m_Que.size() < (unsigned)m_nMaxSize)
        {
            m_Que.push_back(p);
            if (m_Que.size() == 1)
                SetEvent(m_hEvent);
            return TRUE;
        }
        return FALSE;
    }

    virtual BOOL Pop(T** pp, DWORD dwTimeout, BOOL bEvenIfStopped)
    {
        *pp = NULL;
        if (WaitForSingleObject(m_hEvent, dwTimeout) != WAIT_OBJECT_0)
            return TRUE;

        CSingleLockBS lock(&m_cs, TRUE);
        size_t n = m_Que.size();
        if (m_bActive)
        {
            if (n)
            {
                if (n == 1)
                    ResetEvent(m_hEvent);
                *pp = m_Que.front();
                m_Que.pop_front();
            }
            else
            {
                ResetEvent(m_hEvent);
            }
            return TRUE;
        }

        if (!bEvenIfStopped)
            return FALSE;
        if (n)
        {
            *pp = m_Que.front();
            m_Que.pop_front();
        }
        return FALSE;
    }

    virtual BOOL Stop()
    {
        CSingleLockBS lock(&m_cs, TRUE);
        m_bActive = 0;
        if (m_hEvent)
            SetEvent(m_hEvent);
        return TRUE;
    }

    virtual LONG Start()
    {
        CSingleLockBS lock(&m_cs, TRUE);
        m_bActive = 1;
        return FALSE;
    }

    virtual void Reserved()
    {
    }

public:
    CCriticalSectionBS m_cs;            // +4
    HANDLE             m_hEvent;        // +64 manual reset, set while not empty
    std::deque<T*>     m_Que;           // +68
    LONG               m_nMaxSize;      // +88
    LONG               m_bActive;       // +92
};
