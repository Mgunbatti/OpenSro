#pragma once

#include <windows.h>

#include "BSLib/thread/SynchObject.h"

class CBase;
class CEventHandler;
class CMsg;
template <class T> class IQue;

// MFC-style runtime class record (20 bytes). The network engine creates its worker objects
// (proactors, peer processes, the maintainer) from these records.
struct CRuntimeClass
{
    // Calls m_pfnCreateObject inside a structured exception handler; NULL when there is no
    // factory or the factory faulted.
    CBase* CreateObject();
    // TRUE when pBaseClass is this record or one of its m_pBaseClass ancestors.
    BOOL IsDerivedFrom(const CRuntimeClass* pBaseClass) const;

    LPCSTR          m_lpszClassName;                    // +0
    int             m_nObjectSize;                      // +4
    CBase*          (__cdecl* m_pfnCreateObject)();     // +8
    void            (__stdcall* m_pfnDeleteObject)(CBase* pObject);    // +C
    CRuntimeClass*  m_pBaseClass;                       // +10
};

C_ASSERT(sizeof(CRuntimeClass) == 20);

// Declares the static record, GetRuntimeClass and (DYNCREATE) the factory / deleter pair.
#define BS_DECLARE_RUNTIME_CLASS(class_name)                                                \
public:                                                                                     \
    static CRuntimeClass class##class_name;                                                 \
    virtual CRuntimeClass* GetRuntimeClass() const;

#define BS_DECLARE_DYNCREATE(class_name)                                                    \
    BS_DECLARE_RUNTIME_CLASS(class_name)                                                    \
    static CBase* __cdecl CreateObject();                                                   \
    static void __stdcall DeleteObject(CBase* pObject);

#define BS_IMPLEMENT_RUNTIME_CLASS(class_name, pfnCreate, pfnDelete, pBaseClass)            \
    CRuntimeClass class_name::class##class_name =                                           \
        { #class_name, sizeof(class_name), pfnCreate, pfnDelete, pBaseClass };              \
    CRuntimeClass* class_name::GetRuntimeClass() const                                      \
    {                                                                                       \
        return &class_name::class##class_name;                                              \
    }

#define BS_IMPLEMENT_DYNCREATE(class_name, pBaseClass)                                      \
    BS_IMPLEMENT_RUNTIME_CLASS(class_name, class_name::CreateObject,                        \
                               class_name::DeleteObject, pBaseClass)                        \
    CBase* __cdecl class_name::CreateObject()                                               \
    {                                                                                       \
        return new class_name;                                                              \
    }                                                                                       \
    void __stdcall class_name::DeleteObject(CBase* pObject)                                 \
    {                                                                                       \
        if (pObject)                                                                        \
            delete pObject;                                                                 \
    }

class CBase
{
    BS_DECLARE_RUNTIME_CLASS(CBase)                     // 0

public:
    virtual ~CBase() {}                                 // 1
};

C_ASSERT(sizeof(CBase) == 4);

// The worker object a task's threads run (CThreadControl calls Run). Initialize is called once
// by Proactor_Create with the owning CTask.
class CServiceObject : public CBase
{
    BS_DECLARE_RUNTIME_CLASS(CServiceObject)            // 0

public:
    CServiceObject();
    virtual ~CServiceObject();                          // 1

    virtual BOOL Initialize(void* pOwner, void* pParam);    // 2
    // Worker loop; returns the thread's exit code. nIndex is the thread key minus one.
    virtual DWORD Run(volatile LONG* pbStop, int nIndex) = 0;   // 3
    // Makes the running loops return.
    virtual int SignalStop() = 0;                       // 4
    virtual void* GetOwner();                           // 5

public:
    volatile LONG       m_nActive;      // +4   threads inside Run
    void*               m_pOwner;       // +8   not initialised by the constructor
    void*               m_pParam;       // +C   not initialised by the constructor
    CCriticalSectionBS  m_cs;           // +10
};

C_ASSERT(FIELD_OFFSET(CServiceObject, m_nActive) == 4);
C_ASSERT(FIELD_OFFSET(CServiceObject, m_cs) == 0x10);
C_ASSERT(sizeof(CServiceObject) == 0x4C);

// Base of the objects that dispatch socket I/O (proactors and peer processes).
class CServiceObjectSock : public CServiceObject
{
    BS_DECLARE_RUNTIME_CLASS(CServiceObjectSock)        // 0

public:
    CServiceObjectSock();
    virtual ~CServiceObjectSock();                      // 1

    // Registers hHandle so that its completions reach pHandler.
    virtual BOOL AssociateHandle(HANDLE hHandle, CEventHandler* pHandler) = 0;  // 6
    // Called when a handler has to be closed. dwParam is a reason code (IOCP proactor) or the
    // wait index of the handler's slot (event proactors).
    virtual int CloseHandler(CEventHandler* pHandler, DWORD dwParam) = 0;      // 7
    // Queue the datagram sockets put their outgoing messages in; NULL here.
    virtual IQue<CMsg>* GetSendQueue();                 // 8
};

C_ASSERT(sizeof(CServiceObjectSock) == 0x4C);
