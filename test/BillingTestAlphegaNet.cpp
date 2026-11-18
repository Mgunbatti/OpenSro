// Stand-in for AlphegaNet.dll (GlobalManagerBillingTest): one client unit whose sessions are TCP
// connections with [WORD size][data] framing. Events go to the manager's completion port like
// the real library's (see CBaseBillingMgr::DoWork). The test reads the calls with the FakeNet_
// exports.

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>

#include "GlobalManager/Billing/BaseBillingMgr.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(linker, "/EXPORT:DllGetClassObject=_DllGetClassObject@12,PRIVATE")

static CRITICAL_SECTION s_cs;
static HANDLE s_hIOCP;
static ALPHEGA_NET_INIT s_Init;
static ALPHEGA_NET_UNIT s_Unit;
static std::string s_strCalls;
static SOCKET s_hSocket = INVALID_SOCKET;
static HANDLE s_hThread;
static DWORD s_dwSession;
static DWORD s_dwNextSession = 1;
static std::deque<std::vector<BYTE> > s_queRecv;
static char s_szAddress[64];
static DWORD s_dwPort;

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

static void Post(DWORD dwEvent, DWORD dwSession)
{
    ULONG_PTR dwKey = (BILLING_KEY_NET << 1) | (0 << 5) | (dwEvent << 10) | (dwSession << 16);
    PostQueuedCompletionStatus(s_hIOCP, 0, dwKey, NULL);
}

static bool RecvAll(SOCKET s, void* p, int n)
{
    char* pc = (char*)p;
    while (n > 0)
    {
        int r = recv(s, pc, n, 0);
        if (r <= 0)
            return false;
        pc += r;
        n -= r;
    }
    return true;
}

static DWORD WINAPI SessionThread(void* pParam)
{
    DWORD dwSession = (DWORD)(ULONG_PTR)pParam;
    SOCKET s = s_hSocket;
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(s_szAddress);
    addr.sin_port = htons((u_short)s_dwPort);
    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0)
    {
        Post(BILLING_NET_CLOSED, dwSession);
        return 0;
    }
    Post(BILLING_NET_CONNECTED, dwSession);
    for (;;)
    {
        WORD wSize;
        if (!RecvAll(s, &wSize, 2))
            break;
        std::vector<BYTE> data(wSize);
        if (wSize && !RecvAll(s, &data[0], wSize))
            break;
        EnterCriticalSection(&s_cs);
        s_queRecv.push_back(data);
        LeaveCriticalSection(&s_cs);
        Post(BILLING_NET_RECEIVED, dwSession);
    }
    Post(BILLING_NET_CLOSED, dwSession);
    return 0;
}

static void CloseSocketAndWait()
{
    if (s_hSocket != INVALID_SOCKET)
    {
        closesocket(s_hSocket);
        s_hSocket = INVALID_SOCKET;
    }
    if (s_hThread)
    {
        WaitForSingleObject(s_hThread, 5000);
        CloseHandle(s_hThread);
        s_hThread = NULL;
    }
}

class CFakeNet : public IAlphegaNet
{
public:
    CFakeNet() : m_lRef(1) {}

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
    virtual BOOL __stdcall Initialize(ALPHEGA_NET_INIT* pInit)
    {
        s_Init = *pInit;
        s_hIOCP = pInit->hIOCP;
        Call("Initialize");
        return TRUE;
    }
    virtual DWORD __stdcall CreateUnit(ALPHEGA_NET_UNIT* pUnit)
    {
        s_Unit = *pUnit;
        Call("CreateUnit");
        return 0;
    }
    virtual void __stdcall Shutdown(DWORD dwTimeout)
    {
        Call("Shutdown %d", (int)dwTimeout);
        CloseSocketAndWait();
    }
    virtual BOOL __stdcall Start()
    {
        Call("Start");
        return TRUE;
    }
    virtual void __stdcall Cleanup()
    {
        Call("Cleanup");
    }
    virtual DWORD __stdcall Connect(DWORD dwUnit, const char* pszAddress, DWORD dwPort)
    {
        Call("Connect %u %s %u", dwUnit, pszAddress, dwPort);
        CloseSocketAndWait();
        strcpy_s(s_szAddress, pszAddress);
        s_dwPort = dwPort;
        s_hSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        s_dwSession = s_dwNextSession++;
        DWORD dwID;
        s_hThread = CreateThread(NULL, 0, SessionThread, (void*)(ULONG_PTR)s_dwSession, 0,
                                 &dwID);
        return s_dwSession;
    }
    virtual void __stdcall Slot9()
    {
    }
    virtual void __stdcall Slot10()
    {
    }
    virtual BOOL __stdcall Send(DWORD, DWORD dwSession, const void* pData, DWORD dwSize)
    {
        if (dwSession != s_dwSession || s_hSocket == INVALID_SOCKET)
            return FALSE;
        std::vector<BYTE> frame(2 + dwSize);
        *(WORD*)&frame[0] = (WORD)dwSize;
        memcpy(&frame[2], pData, dwSize);
        return send(s_hSocket, (const char*)&frame[0], (int)frame.size(), 0)
               == (int)frame.size();
    }
    virtual BOOL __stdcall Receive(DWORD, DWORD, void* pData, WORD* pwSize)
    {
        EnterCriticalSection(&s_cs);
        if (s_queRecv.empty())
        {
            LeaveCriticalSection(&s_cs);
            return FALSE;
        }
        std::vector<BYTE> data = s_queRecv.front();
        s_queRecv.pop_front();
        LeaveCriticalSection(&s_cs);
        if (!data.empty())
            memcpy(pData, &data[0], data.size());
        *pwSize = (WORD)data.size();
        return TRUE;
    }
    virtual void __stdcall Disconnect(DWORD dwUnit, DWORD dwSession)
    {
        Call("Disconnect %u %u", dwUnit, dwSession);
        if (s_hSocket != INVALID_SOCKET)
            shutdown(s_hSocket, SD_BOTH);
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
        if (riid != IID_IAlphegaNet)
        {
            *ppv = NULL;
            return E_NOINTERFACE;
        }
        *ppv = static_cast<IAlphegaNet*>(new CFakeNet);
        return S_OK;
    }
    virtual HRESULT __stdcall LockServer(BOOL)
    {
        return S_OK;
    }
};

static CFakeFactory s_Factory;

// The GUIDs as the manager passes them.
const GUID CLSID_AlphegaNet =
    {0xA3C74A37, 0x5825, 0x433C, {0xBB, 0xCB, 0xC0, 0xE6, 0xF4, 0xBD, 0xF7, 0x1A}};
const GUID IID_IAlphegaNet =
    {0x972C1115, 0xC82F, 0x4D52, {0xA6, 0xA2, 0x61, 0xE6, 0xC6, 0x31, 0x92, 0xE0}};

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (rclsid != CLSID_AlphegaNet || riid != IID_IClassFactory)
        return CLASS_E_CLASSNOTAVAILABLE;
    *ppv = &s_Factory;
    return S_OK;
}

extern "C" __declspec(dllexport) void FakeNet_GetInit(ALPHEGA_NET_INIT* pInit,
                                                      ALPHEGA_NET_UNIT* pUnit)
{
    *pInit = s_Init;
    *pUnit = s_Unit;
}

// The calls since the last call, separated by ';'.
extern "C" __declspec(dllexport) void FakeNet_TakeCalls(char* psz, int nSize)
{
    EnterCriticalSection(&s_cs);
    strcpy_s(psz, nSize, s_strCalls.c_str());
    s_strCalls.clear();
    LeaveCriticalSection(&s_cs);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD dwReason, void*)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        InitializeCriticalSection(&s_cs);
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    return TRUE;
}
