#pragma once

#include <windows.h>

// Base of the global singletons (CSystem, CFileSystem, ...): the constructor registers the
// instance, the destructor clears it. No virtuals and no data, so it adds nothing to the layout.
template <class T>
class CSingletonT
{
public:
    CSingletonT()
    {
        ms_pInstance = static_cast<T*>(this);
    }

    ~CSingletonT()
    {
        ms_pInstance = NULL;
    }

    static T* GetInstance()
    {
        return ms_pInstance;
    }

protected:
    static T* ms_pInstance;
};

template <class T>
T* CSingletonT<T>::ms_pInstance = NULL;
