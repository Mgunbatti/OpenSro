#include "SynchObject.h"

CSynchObject::CSynchObject()
{
    m_hObject = NULL;
}

CSynchObject::~CSynchObject()
{
    if (m_hObject)
    {
        CloseHandle(m_hObject);
        m_hObject = NULL;
    }
}

BOOL CSynchObject::Lock(DWORD dwTimeout)
{
    return WaitForSingleObject(m_hObject, dwTimeout) == WAIT_OBJECT_0;
}

BOOL CSynchObject::Unlock(LONG, LPLONG)
{
    return TRUE;
}

BOOL CSynchObject::IsCriticalSection()
{
    return FALSE;
}

CCriticalSectionBS::CCriticalSectionBS()
{
    InitializeCriticalSection(&m_cs);
}

CCriticalSectionBS::~CCriticalSectionBS()
{
    DeleteCriticalSection(&m_cs);
}

BOOL CCriticalSectionBS::Lock(DWORD)
{
    EnterCriticalSection(&m_cs);
    return TRUE;
}

BOOL CCriticalSectionBS::Unlock()
{
    LeaveCriticalSection(&m_cs);
    return TRUE;
}

BOOL CCriticalSectionBS::IsCriticalSection()
{
    return TRUE;
}
