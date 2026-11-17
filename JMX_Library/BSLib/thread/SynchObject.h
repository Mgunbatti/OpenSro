#pragma once

#include <windows.h>
#include <string>

#include "BSLib/BSDebug.h"

// Same shape as MFC's CSyncObject / CCriticalSection / CSingleLock.

class CSynchObject
{
public:
    CSynchObject();
    virtual ~CSynchObject();

    virtual BOOL Lock(DWORD dwTimeout = INFINITE);
    // MSVC puts overloaded virtuals in reverse declaration order, so Unlock(LONG, LPLONG)
    // lands in slot 2 and Unlock() in slot 3, as in the originals.
    virtual BOOL Unlock() = 0;
    virtual BOOL Unlock(LONG lCount, LPLONG lpPrevCount = NULL);
    // 0 here, 1 in CCriticalSectionBS.
    virtual BOOL IsCriticalSection();

public:
    HANDLE      m_hObject;      // +4
    std::string m_strName;      // +8
};

class CCriticalSectionBS : public CSynchObject
{
public:
    CCriticalSectionBS();
    virtual ~CCriticalSectionBS();

    using CSynchObject::Unlock;
    virtual BOOL Lock(DWORD dwTimeout = INFINITE);
    virtual BOOL Unlock();
    virtual BOOL IsCriticalSection();

public:
    CRITICAL_SECTION m_cs;      // +36
};

// Scoped lock, inlined everywhere in the originals.
class CSingleLockBS
{
public:
    CSingleLockBS(CSynchObject* pObject, BOOL bInitialLock = FALSE)
    {
        BS_ASSERT(pObject != NULL);
        m_pObject = pObject;
        m_hObject = pObject->m_hObject;
        m_bAcquired = FALSE;
        if (bInitialLock)
            Lock();
    }

    ~CSingleLockBS()
    {
        Unlock();
    }

    BOOL Lock(DWORD dwTimeout = INFINITE)
    {
        m_bAcquired = m_pObject->Lock(dwTimeout);
        return m_bAcquired;
    }

    BOOL Unlock()
    {
        if (m_bAcquired)
            m_bAcquired = !m_pObject->Unlock();
        return !m_bAcquired;
    }

    BOOL IsLocked() const
    {
        return m_bAcquired;
    }

public:
    CSynchObject* m_pObject;
    HANDLE        m_hObject;
    BOOL          m_bAcquired;
};

C_ASSERT(FIELD_OFFSET(CSynchObject, m_strName) == 8);
C_ASSERT(FIELD_OFFSET(CCriticalSectionBS, m_cs) == 36);
C_ASSERT(sizeof(CCriticalSectionBS) == 60);
