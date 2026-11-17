#include "ServiceObject.h"

// Discarded trace output of the originals (empty function); the argument is still evaluated.
#define BS_RTC_TRACE(x) ((void)(x))

// ---------------------------------------------------------------------------------------------
// CRuntimeClass

CBase* CRuntimeClass::CreateObject()
{
    if (m_pfnCreateObject == NULL)
    {
        BS_RTC_TRACE(m_lpszClassName);
        return NULL;
    }

    __try
    {
        return (*m_pfnCreateObject)();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        BS_RTC_TRACE(m_lpszClassName);
    }
    return NULL;
}

BOOL CRuntimeClass::IsDerivedFrom(const CRuntimeClass* pBaseClass) const
{
    const CRuntimeClass* pClass = this;
    while (pClass)
    {
        if (pClass == pBaseClass)
            return TRUE;
        pClass = pClass->m_pBaseClass;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------------------------
// CBase, CServiceObject, CServiceObjectSock
//
// The abstract classes have no factory, and their records do not link to a base record.

BS_IMPLEMENT_RUNTIME_CLASS(CBase, NULL, NULL, NULL)
BS_IMPLEMENT_RUNTIME_CLASS(CServiceObject, NULL, NULL, NULL)
BS_IMPLEMENT_RUNTIME_CLASS(CServiceObjectSock, NULL, NULL, NULL)

CServiceObject::CServiceObject()
{
    m_nActive = 0;
}

CServiceObject::~CServiceObject()
{
}

BOOL CServiceObject::Initialize(void* pOwner, void* pParam)
{
    m_pOwner = pOwner;
    m_pParam = pParam;
    return TRUE;
}

void* CServiceObject::GetOwner()
{
    return m_pOwner;
}

CServiceObjectSock::CServiceObjectSock()
{
}

CServiceObjectSock::~CServiceObjectSock()
{
}

IQue<CMsg>* CServiceObjectSock::GetSendQueue()
{
    return NULL;
}
