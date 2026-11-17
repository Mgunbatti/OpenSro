#pragma once

#include <windows.h>

namespace ServerFramework
{

// Callback object with two entry points. CClassTrigger<T> forwards them to member functions of
// a T object; the server data code keeps one as g_pServerBodyStateObserver and calls OnTrigger
// with (SERVER_BODY*, previous state) when a body changes its state.
class CTrigger
{
public:
    CTrigger() {}
    virtual ~CTrigger() {}                                      // 0

    virtual int OnTrigger(DWORD dwParam1, DWORD dwParam2) = 0;  // 1
    virtual int OnTrigger2(DWORD dwParam1, DWORD dwParam2) = 0; // 2
};

C_ASSERT(sizeof(CTrigger) == 4);

// The member function pointers are as wide as T needs (4 bytes for single inheritance, 8 for
// multiple inheritance); the constructors set the object and leave the pointers to the owner.
template <class T>
class CClassTrigger : public CTrigger
{
public:
    typedef int (T::*PFN_TRIGGER)(DWORD dwParam1, DWORD dwParam2);

    explicit CClassTrigger(T* pObject)
    {
        m_pObject = pObject;
    }

    virtual int OnTrigger(DWORD dwParam1, DWORD dwParam2)
    {
        return (m_pObject->*m_pfnTrigger)(dwParam1, dwParam2);
    }

    virtual int OnTrigger2(DWORD dwParam1, DWORD dwParam2)
    {
        return (m_pObject->*m_pfnTrigger2)(dwParam1, dwParam2);
    }

public:
    T*          m_pObject;          // +4
    PFN_TRIGGER m_pfnTrigger2;      // +8   slot 2
    PFN_TRIGGER m_pfnTrigger;       // +8 + sizeof(PFN_TRIGGER)   slot 1
};

} // namespace ServerFramework
