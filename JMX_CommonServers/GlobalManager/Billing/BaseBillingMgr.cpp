#include "BaseBillingMgr.h"

#include <stdarg.h>
#include <stdio.h>
#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerDataExtern.h"

// {A3C74A37-5825-433C-BBCB-C0E6F4BDF71A}
const GUID CLSID_AlphegaNet =
    {0xA3C74A37, 0x5825, 0x433C, {0xBB, 0xCB, 0xC0, 0xE6, 0xF4, 0xBD, 0xF7, 0x1A}};
// {972C1115-C82F-4D52-A6A2-61E6C63192E0}
const GUID IID_IAlphegaNet =
    {0x972C1115, 0xC82F, 0x4D52, {0xA6, 0xA2, 0x61, 0xE6, 0xC6, 0x31, 0x92, 0xE0}};
// {3740C883-79EE-4F51-92AD-A3E8E8830409}
const GUID CLSID_AlphegaTimer =
    {0x3740C883, 0x79EE, 0x4F51, {0x92, 0xAD, 0xA3, 0xE8, 0xE8, 0x83, 0x04, 0x09}};
// {FB50AC1D-B245-48B7-9BA9-390182290E61}
const GUID IID_IAlphegaTimer =
    {0xFB50AC1D, 0xB245, 0x48B7, {0x9B, 0xA9, 0x39, 0x01, 0x82, 0x29, 0x0E, 0x61}};

// IID_IClassFactory {00000001-0000-0000-C000-000000000046} (0x52768C)
static const GUID s_IID_IClassFactory =
    {0x00000001, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

typedef HRESULT (__stdcall* PFN_DLLGETCLASSOBJECT)(REFCLSID rclsid, REFIID riid, void** ppv);

// ---------------------------------------------------------------------------------------------
// Helpers

void BillingMgr_Log(DWORD dwLevel, const char* pszFormat, ...)
{
    char szText[0x400];
    va_list args;
    va_start(args, pszFormat);
    int nLen = _vsnprintf_s(szText, 0x400, 0x3FF, pszFormat, args);
    va_end(args);
    if (nLen < 0 || nLen > 0x3FF)
    {
        szText[0x3FF] = 0;
        BSAssertFail();
    }
    else if (nLen == 0x3FF)
    {
        szText[0x3FF] = 0;
    }
    LogPrintf(dwLevel, "BillingMgr- %s", szText);
}

BOOL BillingMgr_LoadComDll(const char* pszDll, const GUID& clsid, const GUID& iid,
                           void** ppObject)
{
    HMODULE hModule = LoadLibraryA(pszDll);
    if (!hModule)
        return FALSE;
    PFN_DLLGETCLASSOBJECT pfnGetClassObject =
        (PFN_DLLGETCLASSOBJECT)GetProcAddress(hModule, "DllGetClassObject");
    if (!pfnGetClassObject)
        return FALSE;
    IClassFactory* pFactory = NULL;
    if (pfnGetClassObject(clsid, s_IID_IClassFactory, (void**)&pFactory) < 0)
        return FALSE;
    HRESULT hr = pFactory->CreateInstance(NULL, iid, ppObject);
    if (!*ppObject)
        BSAssertFail();
    return hr >= 0;
}

void CBillingPacket::Write(const void* pData, WORD wSize)
{
    if (wSize + m_wWritePos > 4096)
        throw 0;
    memcpy(m_abData + m_wWritePos, pData, wSize);
    m_wWritePos = (WORD)(m_wWritePos + wSize);
    *m_pwSize = (WORD)(m_wWritePos - 4);
}

void CBillingPacket::WriteString(const char* psz)
{
    DWORD dwLen = psz ? (WORD)lstrlenA(psz) : 0;
    // BUG(GM-019): the cut does not count the length word, so a cut text never fits.
    if ((WORD)dwLen + m_wWritePos >= 4096)
        dwLen = (WORD)(4095 - m_wWritePos);
    Write(&dwLen, 2);
    if ((WORD)dwLen)
        Write(psz, (WORD)dwLen);
}

void CBillingPacket::WriteString(const std::string& str)
{
    DWORD dwLen = (WORD)str.size();
    // BUG(GM-019): as above.
    if ((WORD)dwLen + m_wWritePos >= 4096)
        dwLen = (WORD)(4095 - m_wWritePos);
    Write(&dwLen, 2);
    if ((WORD)dwLen)
        Write(str.c_str(), (WORD)dwLen);
}

// ---------------------------------------------------------------------------------------------
// CBaseBillingMgr

CBaseBillingMgr::CBaseBillingMgr(ServerFramework::CServerProcessBase* pOwner)
{
    m_pINet = NULL;
    m_pITimer = NULL;
    m_pOwner = pOwner;
    m_pEventNotifier = NULL;
    m_nState = BILLING_STATE_IDLE;
    m_dwUnit = (DWORD)-1;
    m_dwSession = (DWORD)-1;
    memset(m_szAddress, 0, sizeof(m_szAddress));
    m_wPort = 0;
}

CBaseBillingMgr::~CBaseBillingMgr()
{
    CBaseBillingMgr::Cleanup();
}

void CBaseBillingMgr::DoWork(ULONG_PTR dwKey, DWORD, LPOVERLAPPED)
{
    switch ((dwKey >> 1) & 0xF)
    {
    case BILLING_KEY_TIMER:
        OnTimer((dwKey >> 5) & 0x1F);
        break;
    case BILLING_KEY_NET:
        {
            DWORD dwUnit = (dwKey >> 5) & 0x1F;
            DWORD dwSession = dwKey >> 16;
            DWORD dwEvent = (dwKey >> 10) & 0x3F;
            switch (dwEvent)
            {
            case BILLING_NET_CONNECTED:
                OnConnected(dwUnit, dwSession);
                break;
            case BILLING_NET_CLOSED:
            case BILLING_NET_CLOSED2:
                OnDisconnected(dwUnit, dwSession);
                break;
            case BILLING_NET_RECEIVED:
                OnReceived(dwUnit, dwSession);
                break;
            default:
                OnNetEvent(dwUnit, dwSession, dwEvent);
                break;
            }
        }
        break;
    }
}

void CBaseBillingMgr::OnReceived(DWORD dwUnit, DWORD dwSession)
{
    if (!m_pINet)
        BSAssertFail();
    CBillingPacket packet;
    if (m_pINet->Receive(dwUnit, dwSession, packet.m_abData + 2, packet.m_pwSize))
    {
        packet.AdjustReceived();
        PFN_BILLING_HANDLER pfnHandler = m_aHandler[packet.GetOpcode()];
        (this->*pfnHandler)(&packet);
    }
}

void CBaseBillingMgr::OnNetEvent(DWORD, DWORD, DWORD)
{
}

void CBaseBillingMgr::InitHandlers()
{
    for (int i = 0; i < 0xFFFF; ++i)
        m_aHandler[i] = &CBaseBillingMgr::OnUnhandledPacket;
}

void CBaseBillingMgr::OnUnhandledPacket(CBillingPacket* pPacket)
{
    BillingMgr_Log(0x1000000, "Unhandled Billing Msg [0x%0x]!", pPacket->GetOpcode());
}

BOOL CBaseBillingMgr::Initialize(const char* pszAddress, WORD wPort, WORD wPoolSize)
{
    // BUG(GM-016): the 16-byte field is filled with a limit of 256 characters.
    if (FAILED(StringCchCopyA(m_szAddress, 0x100, pszAddress)))
    {
        BSAssertFail();
        return FALSE;
    }
    m_wPort = wPort;
    if (!CreateEventNotifier())
    {
        MessageBoxA(NULL, "BillingMgr- Failed to Create Notifier!!", "Critical", MB_ICONHAND);
        BillingMgr_Log(0x2000000, "Failed to Create Notifier!");
        return FALSE;
    }
    if (!InitNetwork(wPoolSize))
    {
        MessageBoxA(NULL, "BillingMgr- Failed to Initialize Network!!", "Critical",
                    MB_ICONHAND);
        BillingMgr_Log(0x2000000, "Failed to Initialize Network!");
        return FALSE;
    }
    if (!InitTimer())
    {
        MessageBoxA(NULL, "BillingMgr- Failed to Initialize Timer!", "Critical", MB_ICONHAND);
        BillingMgr_Log(0x2000000, "Failed to Initialize Timer!");
        return FALSE;
    }
    return TRUE;
}

void CBaseBillingMgr::Cleanup()
{
    CleanupTimer();
    CleanupNetwork();
    DeleteEventNotifier();
}

BOOL CBaseBillingMgr::IsReady()
{
    return m_nState == BILLING_STATE_ACTIVATED;
}

BOOL CBaseBillingMgr::CreateEventNotifier()
{
    m_pEventNotifier = new CThreadExMgr(this, &m_IOCP);
    if (!m_pEventNotifier)
    {
        MessageBoxA(NULL, "BillingMgr- Failed to create ThreadExMgr!", "Critical", MB_ICONHAND);
        BillingMgr_Log(0x2000000, "Failed to create ThreadExMgr!");
        return FALSE;
    }
    for (unsigned i = 0; i < 1; ++i)
    {
        if (!m_pEventNotifier->CreateThread())
            return FALSE;
    }
    return TRUE;
}

void CBaseBillingMgr::DeleteEventNotifier()
{
    if (m_pEventNotifier)
    {
        delete m_pEventNotifier;
        m_pEventNotifier = NULL;
    }
}

BOOL CBaseBillingMgr::InitNetwork(WORD wPoolSize)
{
    if (!BillingMgr_LoadComDll("AlphegaNet.dll", CLSID_AlphegaNet, IID_IAlphegaNet,
                               (void**)&m_pINet))
    {
        MessageBoxA(NULL, "BillingMgr- Initializing AlphegaNet.dll Failed!!", "Critical",
                    MB_ICONHAND);
        OutputDebugStringA("BillingMgr- Initializing AlphegaNet.dll Failed!!\n");
        return FALSE;
    }
    // The bytes the original leaves unset are 0 here.
    ALPHEGA_NET_INIT init;
    memset(&init, 0, sizeof(init));
    init.hIOCP = m_IOCP.m_hIOCP;
    init.by4 = 2;
    init.by5 = 1;
    init.by6 = 0;
    if (!m_pINet->Initialize(&init))
    {
        MessageBoxA(NULL, "AlphegaNet: Cannot Initialize AlphegaNet", "Critical", MB_ICONHAND);
        BillingMgr_Log(0x2000000, "AlphegaNet: Cannot Initialize AlphegaNet");
        m_pINet->Release();
        m_pINet = NULL;
        return FALSE;
    }
    ALPHEGA_NET_UNIT unit;
    memset(&unit, 0, sizeof(unit));
    unit.w0 = 50;
    unit.w2 = 0x1000;
    unit.w4 = 0x8000;
    unit.wPoolSize = wPoolSize;
    unit.w8 = 0x4000;
    unit.wA = 0x4000;
    unit.wC = (WORD)(wPoolSize >> 1);
    unit.wE = (WORD)(wPoolSize >> 1);
    unit.by10 = 0;
    unit.by11 = 0;
    unit.dw14 = 0;
    unit.dw18 = 0;
    m_dwUnit = m_pINet->CreateUnit(&unit);
    if (m_dwUnit == (DWORD)-1)
    {
        MessageBoxA(NULL, "AlphegaNet: Failed to create net unit!", "Critical", MB_ICONHAND);
        BillingMgr_Log(0x2000000, "AlphegaNet: Failed to create net unit!");
        m_pINet->Release();
        m_pINet = NULL;
        return FALSE;
    }
    return m_pINet->Start();
}

void CBaseBillingMgr::CleanupNetwork()
{
    if (m_pINet)
    {
        m_pINet->Shutdown((DWORD)-1);
        m_pINet->Cleanup();
        m_pINet->Release();
        m_pINet = NULL;
    }
}

BOOL CBaseBillingMgr::InitTimer()
{
    if (!BillingMgr_LoadComDll("AlphegaTimer.dll", CLSID_AlphegaTimer, IID_IAlphegaTimer,
                               (void**)&m_pITimer))
    {
        MessageBoxA(NULL, "BillingMgr- Initializing AlphegaTimer.dll Failed!!", "Critical",
                    MB_ICONHAND);
        OutputDebugStringA("BillingMgr- Initializing AlphegaTimer.dll Failed!!\n");
        return FALSE;
    }
    ALPHEGA_TIMER_INIT init;
    memset(&init, 0, sizeof(init));
    init.hIOCP = m_IOCP.m_hIOCP;
    init.by4 = 1;
    init.by5 = 3;
    if (m_pITimer->Initialize(&init) && m_pITimer->Start(0))
        return TRUE;
    m_pITimer->Release();
    m_pITimer = NULL;
    return FALSE;
}

void CBaseBillingMgr::CleanupTimer()
{
    if (m_pITimer)
    {
        m_pITimer->Shutdown((DWORD)-1);
        m_pITimer->Release();
        m_pITimer = NULL;
    }
}

DWORD CBaseBillingMgr::CreateTimer(DWORD dwInterval)
{
    if (!m_pITimer)
        BSAssertFail();
    ALPHEGA_TIMER_DESC desc;
    desc.dw0 = 0;
    desc.dw4 = 0;
    desc.dwInterval = dwInterval;
    DWORD dwIndex = m_pITimer->CreateTimer(&desc);
    if (dwIndex == (DWORD)-1)
    {
        MessageBoxA(NULL, "BillingMgr- Failed To Create Timer!!!", "Critical", MB_ICONHAND);
        OutputDebugStringA("BillingMgr- Failed To Create Timer!\n");
        m_pITimer->Release();
        m_pITimer = NULL;
    }
    return dwIndex;
}

BOOL CBaseBillingMgr::ActivateTimer(DWORD dwIndex)
{
    if (!m_pITimer)
        return TRUE;
    if (dwIndex == (DWORD)-1)
        BSAssertFail();
    return m_pITimer->ActivateTimer(dwIndex);
}

BOOL CBaseBillingMgr::DeactivateTimer(DWORD dwIndex)
{
    if (!m_pITimer)
        return TRUE;
    if (dwIndex == (DWORD)-1)
        BSAssertFail();
    return m_pITimer->DeactivateTimer(dwIndex);
}

BOOL CBaseBillingMgr::ActivateNetwork()
{
    if (!m_pINet)
        BSAssertFail();
    if (m_dwUnit == (DWORD)-1)
        BSAssertFail();
    m_dwSession = m_pINet->Connect(m_dwUnit, m_szAddress, m_wPort);
    return m_dwSession != (DWORD)-1;
}

void CBaseBillingMgr::CloseSession()
{
    if (m_nState != BILLING_STATE_STOPPING)
        return;
    if (!m_pINet)
        BSAssertFail();
    if (m_dwUnit == (DWORD)-1)
        BSAssertFail();
    if (m_dwSession == (DWORD)-1)
        BSAssertFail();
    m_pINet->Disconnect(m_dwUnit, m_dwSession);
    BillingMgr_Log(0x1000000, "Closing Session!");
}
