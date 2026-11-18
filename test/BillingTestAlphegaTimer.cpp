// Stand-in for AlphegaTimer.dll (GlobalManagerBillingTest): timers only fire when the test calls
// FakeTimer_Fire (then the completion goes to the manager's port like the real library's).

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "GlobalManager/Billing/BaseBillingMgr.h"

#pragma comment(linker, "/EXPORT:DllGetClassObject=_DllGetClassObject@12,PRIVATE")

static CRITICAL_SECTION s_cs;
static HANDLE s_hIOCP;
static ALPHEGA_TIMER_INIT s_Init;
static DWORD s_adwInterval[8];
static volatile LONG s_abActive[8];
static DWORD s_dwCount;
static std::string s_strCalls;

static void Call(const char* pszFormat, ...)
{
    char sz[256];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf_s(sz, sizeof(sz), _TRUNCATE, pszFormat, args);
    va_end(args);
    EnterCriticalSection(&s_cs);
    s_strCalls += sz;
    s_strCalls += ';';
    LeaveCriticalSection(&s_cs);
}

class CFakeTimer : public IAlphegaTimer
{
public:
    CFakeTimer() : m_lRef(1) {}

    virtual HRESULT __stdcall QueryInterface(REFIID, void** ppv)
    {
        *ppv = this;
        AddRef();
        return S_OK;
    }
    virtual ULONG __stdcall AddRef()
    {
        return InterlockedIncrement(&m_lRef);
    }
    virtual ULONG __stdcall Release()
    {
        Call("Release");
        LONG l = InterlockedDecrement(&m_lRef);
        if (!l)
            delete this;
        return l;
    }
    virtual BOOL __stdcall Initialize(ALPHEGA_TIMER_INIT* pInit)
    {
        s_Init = *pInit;
        s_hIOCP = pInit->hIOCP;
        s_dwCount = 0;
        for (int i = 0; i < 8; ++i)
            s_abActive[i] = 0;
        Call("Initialize");
        return TRUE;
    }
    virtual DWORD __stdcall CreateTimer(ALPHEGA_TIMER_DESC* pDesc)
    {
        Call("CreateTimer %u %u %u", pDesc->dw0, pDesc->dw4, pDesc->dwInterval);
        if (s_dwCount >= 8)
            return (DWORD)-1;
        s_adwInterval[s_dwCount] = pDesc->dwInterval;
        return s_dwCount++;
    }
    virtual void __stdcall Shutdown(DWORD dwTimeout)
    {
        Call("Shutdown %d", (int)dwTimeout);
    }
    virtual BOOL __stdcall Start(DWORD dwParam)
    {
        Call("Start %u", dwParam);
        return TRUE;
    }
    virtual void __stdcall Slot7()
    {
    }
    virtual BOOL __stdcall ActivateTimer(DWORD dwIndex)
    {
        if (dwIndex >= s_dwCount)
            return FALSE;
        InterlockedExchange(&s_abActive[dwIndex], 1);
        return TRUE;
    }
    virtual BOOL __stdcall DeactivateTimer(DWORD dwIndex)
    {
        if (dwIndex >= s_dwCount)
            return FALSE;
        InterlockedExchange(&s_abActive[dwIndex], 0);
        return TRUE;
    }

private:
    volatile LONG m_lRef;
};

class CFakeFactory : public IClassFactory
{
public:
    virtual HRESULT __stdcall QueryInterface(REFIID, void** ppv)
    {
        *ppv = this;
        return S_OK;
    }
    virtual ULONG __stdcall AddRef()
    {
        return 2;
    }
    virtual ULONG __stdcall Release()
    {
        return 1;
    }
    virtual HRESULT __stdcall CreateInstance(IUnknown*, REFIID riid, void** ppv)
    {
        if (riid != IID_IAlphegaTimer)
        {
            *ppv = NULL;
            return E_NOINTERFACE;
        }
        *ppv = static_cast<IAlphegaTimer*>(new CFakeTimer);
        return S_OK;
    }
    virtual HRESULT __stdcall LockServer(BOOL)
    {
        return S_OK;
    }
};

static CFakeFactory s_Factory;

const GUID CLSID_AlphegaTimer =
    {0x3740C883, 0x79EE, 0x4F51, {0x92, 0xAD, 0xA3, 0xE8, 0xE8, 0x83, 0x04, 0x09}};
const GUID IID_IAlphegaTimer =
    {0xFB50AC1D, 0xB245, 0x48B7, {0x9B, 0xA9, 0x39, 0x01, 0x82, 0x29, 0x0E, 0x61}};

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (rclsid != CLSID_AlphegaTimer || riid != IID_IClassFactory)
        return CLASS_E_CLASSNOTAVAILABLE;
    *ppv = &s_Factory;
    return S_OK;
}

extern "C" __declspec(dllexport) void FakeTimer_GetInit(ALPHEGA_TIMER_INIT* pInit)
{
    *pInit = s_Init;
}

extern "C" __declspec(dllexport) BOOL FakeTimer_IsActive(DWORD dwIndex)
{
    return dwIndex < s_dwCount && s_abActive[dwIndex];
}

extern "C" __declspec(dllexport) DWORD FakeTimer_GetInterval(DWORD dwIndex)
{
    return dwIndex < s_dwCount ? s_adwInterval[dwIndex] : 0;
}

// Posts the timer's completion when it is active.
extern "C" __declspec(dllexport) BOOL FakeTimer_Fire(DWORD dwIndex)
{
    if (!FakeTimer_IsActive(dwIndex))
        return FALSE;
    ULONG_PTR dwKey = (BILLING_KEY_TIMER << 1) | (dwIndex << 5);
    return PostQueuedCompletionStatus(s_hIOCP, 0, dwKey, NULL);
}

extern "C" __declspec(dllexport) void FakeTimer_TakeCalls(char* psz, int nSize)
{
    EnterCriticalSection(&s_cs);
    strcpy_s(psz, nSize, s_strCalls.c_str());
    s_strCalls.clear();
    LeaveCriticalSection(&s_cs);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD dwReason, void*)
{
    if (dwReason == DLL_PROCESS_ATTACH)
        InitializeCriticalSection(&s_cs);
    return TRUE;
}
