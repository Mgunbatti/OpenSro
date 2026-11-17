#pragma once

#include <windows.h>

#include "BSLib/thread/SynchObject.h"

// Minimal view of CBase / CServiceObject: the worker object a thread runs (CMaintainer, the
// proactors, the server processes). Only what the thread code needs is declared; the vtable
// order matches the originals. Replace this header with the real one once CServiceObject is
// ported.

struct CRuntimeClass
{
    LPCSTR m_lpszClassName;     // +0, the only field the thread code reads
};

class CBase
{
public:
    virtual CRuntimeClass* GetRuntimeClass() const              // 0
    {
        static CRuntimeClass s_rtc = { "CBase" };
        return &s_rtc;
    }
    virtual ~CBase() {}                                         // 1
};

class CServiceObject : public CBase
{
public:
    CServiceObject()
    {
        m_nActive = 0;
    }
    virtual ~CServiceObject() {}

    virtual CRuntimeClass* GetRuntimeClass() const              // 0
    {
        static CRuntimeClass s_rtc = { "CServiceObject" };
        return &s_rtc;
    }
    virtual BOOL Initialize(void* pOwner, void* pParam)         // 2
    {
        m_pOwner = pOwner;
        m_pParam = pParam;
        return TRUE;
    }
    // Worker loop, run by CThreadControl until *pbStop is set; the result is the thread's exit
    // code. nIndex is the thread key minus one.
    virtual DWORD Run(volatile LONG* pbStop, int nIndex) = 0;   // 3
    virtual int SignalStop() = 0;                               // 4
    virtual void* GetOwner()                                    // 5
    {
        return m_pOwner;
    }

public:
    volatile LONG       m_nActive;      // +4, threads currently inside Run
    void*               m_pOwner;       // +8
    void*               m_pParam;       // +C
    CCriticalSectionBS  m_cs;           // +10
};

C_ASSERT(FIELD_OFFSET(CServiceObject, m_cs) == 0x10);
C_ASSERT(sizeof(CServiceObject) == 0x4C);
