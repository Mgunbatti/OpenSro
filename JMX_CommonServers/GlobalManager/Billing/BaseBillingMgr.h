#pragma once

#include <winsock2.h>
#include <windows.h>
#include <unknwn.h>
#include <string>

#include "IBillingMgr.h"
#include "IOCP.h"
#include "ThreadEx.h"

namespace ServerFramework
{
class CServerProcessBase;
}

// The billing manager talks to the billing server through two COM-style libraries loaded at
// run time: AlphegaNet.dll (TCP sessions, framing) and AlphegaTimer.dll (timers). Both are
// initialised with the manager's completion port and post their events to it; the key of a
// completion says what happened (see CBaseBillingMgr::DoWork). Only the calls the manager
// makes are known; the slots it never calls are placeholders.

// The interface and class IDs of the libraries (read from the binary).
extern const GUID CLSID_AlphegaNet;     // 0x529D10
extern const GUID IID_IAlphegaNet;      // 0x529D20
extern const GUID CLSID_AlphegaTimer;   // 0x529D30
extern const GUID IID_IAlphegaTimer;    // 0x529D40

#pragma pack(push, 1)
// IAlphegaNet::Initialize
struct ALPHEGA_NET_INIT
{
    HANDLE  hIOCP;          // +0
    BYTE    by4;            // +4   2
    BYTE    by5;            // +5   1
    BYTE    by6;            // +6   0
    BYTE    by7;            // +7   not set
};

// IAlphegaNet::CreateUnit (one client unit)
struct ALPHEGA_NET_UNIT
{
    WORD    w0;             // +0   50
    WORD    w2;             // +2   0x1000
    WORD    w4;             // +4   0x8000
    WORD    wPoolSize;      // +6
    WORD    w8;             // +8   0x4000
    WORD    wA;             // +A   0x4000
    WORD    wC;             // +C   pool size / 2
    WORD    wE;             // +E   pool size / 2
    BYTE    by10;           // +10  0
    BYTE    by11;           // +11  0
    BYTE    ab12[2];        // +12  not set
    DWORD   dw14;           // +14  0
    DWORD   dw18;           // +18  0
};

// IAlphegaTimer::Initialize
struct ALPHEGA_TIMER_INIT
{
    HANDLE  hIOCP;          // +0
    BYTE    by4;            // +4   1
    BYTE    by5;            // +5   3
    BYTE    ab6[2];         // +6   not set
};

// IAlphegaTimer::CreateTimer
struct ALPHEGA_TIMER_DESC
{
    DWORD   dw0;            // +0   0
    DWORD   dw4;            // +4   0
    DWORD   dwInterval;     // +8   ms
};
#pragma pack(pop)

C_ASSERT(sizeof(ALPHEGA_NET_INIT) == 8);
C_ASSERT(sizeof(ALPHEGA_NET_UNIT) == 0x1C);
C_ASSERT(sizeof(ALPHEGA_TIMER_INIT) == 8);
C_ASSERT(sizeof(ALPHEGA_TIMER_DESC) == 0xC);

struct IAlphegaNet : public IUnknown
{
    virtual BOOL __stdcall Initialize(ALPHEGA_NET_INIT* pInit) = 0;                    // 3
    // The unit index, -1 on failure.
    virtual DWORD __stdcall CreateUnit(ALPHEGA_NET_UNIT* pUnit) = 0;                   // 4
    virtual void __stdcall Shutdown(DWORD dwTimeout) = 0;                              // 5
    virtual BOOL __stdcall Start() = 0;                                                // 6
    virtual void __stdcall Cleanup() = 0;                                              // 7
    // Connects a session of the unit; the session ID, -1 on failure. The result comes as a
    // completion (connected / closed).
    virtual DWORD __stdcall Connect(DWORD dwUnit, const char* pszAddress, DWORD dwPort) = 0;    // 8
    virtual void __stdcall Slot9() = 0;                                                // 9
    virtual void __stdcall Slot10() = 0;                                               // 10
    // Sends one packet (the library frames it).
    virtual BOOL __stdcall Send(DWORD dwUnit, DWORD dwSession, const void* pData,
                                DWORD dwSize) = 0;                                     // 11
    // Takes the next received packet: the data to pData, its size to *pwSize.
    virtual BOOL __stdcall Receive(DWORD dwUnit, DWORD dwSession, void* pData,
                                   WORD* pwSize) = 0;                                  // 12
    virtual void __stdcall Disconnect(DWORD dwUnit, DWORD dwSession) = 0;              // 13
};

struct IAlphegaTimer : public IUnknown
{
    virtual BOOL __stdcall Initialize(ALPHEGA_TIMER_INIT* pInit) = 0;                  // 3
    // The timer index (part of the completion key), -1 on failure.
    virtual DWORD __stdcall CreateTimer(ALPHEGA_TIMER_DESC* pDesc) = 0;                // 4
    virtual void __stdcall Shutdown(DWORD dwTimeout) = 0;                              // 5
    virtual BOOL __stdcall Start(DWORD dwParam) = 0;                                   // 6
    virtual void __stdcall Slot7() = 0;                                                // 7
    virtual BOOL __stdcall ActivateTimer(DWORD dwIndex) = 0;                           // 8
    virtual BOOL __stdcall DeactivateTimer(DWORD dwIndex) = 0;                         // 9
};

// The completion keys the libraries post: bits 1..4 the source, then for timers bits 5..9 the
// timer index; for the network bits 5..9 the unit, bits 10..15 the event, bits 16..31 the
// session.
enum
{
    BILLING_KEY_TIMER       = 1,
    BILLING_KEY_NET         = 2,

    BILLING_NET_CONNECTED   = 1,
    BILLING_NET_CLOSED      = 2,
    BILLING_NET_RECEIVED    = 3,
    BILLING_NET_CLOSED2     = 6,
};

// The packet buffer of the billing protocol (0x100C bytes, on the stack): WORD size of the
// data, WORD opcode, data. The size word is not sent; the network library frames the opcode
// and the data. Reading past the end writes to address 0 (a deliberate crash); writing past
// 4096 bytes throws an int.
class CBillingPacket
{
public:
    // sub_4510C0 (inlined everywhere)
    CBillingPacket()
    {
        Reset();
    }

    void Reset()
    {
        m_wWritePos = 4;
        m_wReadPos = 4;
        *(DWORD*)m_abData = 0;
        m_pwSize = (WORD*)m_abData;
        m_pwOpcode = (WORD*)(m_abData + 2);
    }

    WORD GetOpcode() const          { return *m_pwOpcode; }
    void SetOpcode(WORD wOpcode)    { *m_pwOpcode = wOpcode; }

    // BillingPacket_AdjustHeader_451120: after a receive the size counted the opcode.
    void AdjustReceived()
    {
        *m_pwSize -= 2;
        m_wWritePos = (WORD)(m_wWritePos + *m_pwSize);
    }

    // sub_451D20
    void Write(const void* pData, WORD wSize);
    // sub_451D80: WORD length and text (cut at 4095 bytes of packet).
    void WriteString(const char* psz);
    // sub_451DF0
    void WriteString(const std::string& str);

    template <typename T> void Read(T& v)
    {
        if (m_wReadPos + (int)sizeof(T) > m_wWritePos)
            *(volatile int*)0 = 2000;
        v = *(T*)(m_abData + m_wReadPos);
        m_wReadPos = (WORD)(m_wReadPos + sizeof(T));
    }
    void Skip(int nSize)
    {
        if (m_wReadPos + nSize > m_wWritePos)
            *(volatile int*)0 = 2000;
        m_wReadPos = (WORD)(m_wReadPos + nSize);
    }

public:
    BYTE    m_abData[4096];     // +0
    WORD    m_wReadPos;         // +1000
    WORD    m_wWritePos;        // +1002
    WORD*   m_pwSize;           // +1004
    WORD*   m_pwOpcode;         // +1008
};

C_ASSERT(FIELD_OFFSET(CBillingPacket, m_wReadPos) == 0x1000);
C_ASSERT(FIELD_OFFSET(CBillingPacket, m_pwOpcode) == 0x1008);
C_ASSERT(sizeof(CBillingPacket) == 0x100C);

// sub_451BA0: "BillingMgr- %s" of the formatted text (1024 characters) through LogPrintf.
void BillingMgr_Log(DWORD dwLevel, const char* pszFormat, ...);
// BillingMgr_LoadComDll_451240 / sub_4511D0: loads the library (never freed) and creates the
// object through DllGetClassObject and the class factory (which is not released).
BOOL BillingMgr_LoadComDll(const char* pszDll, const GUID& clsid, const GUID& iid,
                           void** ppObject);

// Declared with the inheritance model so that the handler pointers have their final size
// (pointer and this adjustment, 8 bytes) before the class is complete.
class __multiple_inheritance CBaseBillingMgr;
typedef void (CBaseBillingMgr::*PFN_BILLING_HANDLER)(CBillingPacket* pPacket);

enum
{
    eHeartBeatTimer     = 0,
    eHeartBeatAckTimer  = 1,
    eReconnectTimer     = 2,
};

enum
{
    BILLING_STATE_IDLE          = 0,
    BILLING_STATE_CONNECTED     = 1,
    BILLING_STATE_ACTIVATED     = 2,
    BILLING_STATE_STOPPING      = 3,
};

// CBaseBillingMgr: the connection to the billing server and the handler table of its
// opcodes. The IBillingMgr part is at +4.
class CBaseBillingMgr : public sThreadWork, public IBillingMgr
{
public:
    // sub_451250
    explicit CBaseBillingMgr(ServerFramework::CServerProcessBase* pOwner);
    // sub_451320 (deleting dtor 0x4512f0): Cleanup, then the port is closed.
    virtual ~CBaseBillingMgr();                                     // 1

    // sub_451C30: a completion of the port.
    virtual void DoWork(ULONG_PTR dwKey, DWORD dwBytes, LPOVERLAPPED pOverlapped);  // 0
    virtual void OnTimer(DWORD dwIndex) = 0;                        // 2
    virtual void OnConnected(DWORD dwUnit, DWORD dwSession) = 0;    // 3
    virtual void OnDisconnected(DWORD dwUnit, DWORD dwSession) = 0; // 4
    // sub_451A70: takes the packet from the network library and calls its handler.
    virtual void OnReceived(DWORD dwUnit, DWORD dwSession);         // 5
    // nullsub_880 (0x451b90): other network events.
    virtual void OnNetEvent(DWORD dwUnit, DWORD dwSession, DWORD dwEvent);  // 6
    // CBaseBillingMgr::vf07 (0x451a00): every opcode gets OnUnhandledPacket.
    virtual void InitHandlers();                                    // 7
    // CBaseBillingMgr::vf08 (0x451a40): "Unhandled Billing Msg [0x%0x]!".
    virtual void OnUnhandledPacket(CBillingPacket* pPacket);        // 8

    // CBaseBillingMgr::vf00 (0x451390): keeps the address (StringCchCopy with 256 characters
    // into the 16-byte field; the main process reads at most 15) and the port, creates the
    // worker, then the network unit and the timers. Message box and log on every failure.
    virtual BOOL Initialize(const char* pszAddress, WORD wPort, WORD wPoolSize);
    // CBaseBillingMgr::vf01 (0x4515c0): timers, network, worker.
    virtual void Cleanup();
    // CBaseBillingMgr::vf05 (0x4511b0): the billing server accepted the client.
    virtual BOOL IsReady();

    // CBaseBillingMgr_SetHandler_451A20
    void SetHandler(WORD wOpcode, PFN_BILLING_HANDLER pfnHandler)
    {
        m_aHandler[wOpcode] = pfnHandler;
    }

    // sub_4514C0: the event notifier with one worker.
    BOOL CreateEventNotifier();
    // sub_451580
    void DeleteEventNotifier();
    // sub_4517E0: AlphegaNet.dll, its initialisation, the unit and the start.
    BOOL InitNetwork(WORD wPoolSize);
    // sub_451930
    void CleanupNetwork();
    // sub_4515E0: AlphegaTimer.dll, its initialisation and start.
    BOOL InitTimer();
    // sub_4516B0
    void CleanupTimer();
    // sub_4516E0: a timer with the interval; -1 (and the timer library released) on failure.
    DWORD CreateTimer(DWORD dwInterval);
    // sub_451780 / sub_4517B0
    BOOL ActivateTimer(DWORD dwIndex);
    BOOL DeactivateTimer(DWORD dwIndex);
    // sub_451960: connects the session to the billing server.
    BOOL ActivateNetwork();
    // sub_4519B0: disconnects the session (only while stopping).
    void CloseSession();

public:
    IAlphegaNet*        m_pINet;                // +8
    IAlphegaTimer*      m_pITimer;              // +C
    CIOCP               m_IOCP;                 // +10
    CThreadExMgr*       m_pEventNotifier;       // +14
    ServerFramework::CServerProcessBase* m_pOwner;  // +18  the main process
    volatile LONG       m_nState;               // +1C
    DWORD               m_dwUnit;               // +20
    DWORD               m_dwSession;            // +24
    DWORD               m_dwTimerIndex[4];      // +28  set by CStandardBillingMgr
    PFN_BILLING_HANDLER m_aHandler[0xFFFF];     // +38
    char                m_szAddress[16];        // +80030
    WORD                m_wPort;                // +80040
    DWORD               m_dw80044;              // +80044
};

C_ASSERT(sizeof(PFN_BILLING_HANDLER) == 8);
C_ASSERT(FIELD_OFFSET(CBaseBillingMgr, m_pINet) == 8);
C_ASSERT(FIELD_OFFSET(CBaseBillingMgr, m_pOwner) == 0x18);
C_ASSERT(FIELD_OFFSET(CBaseBillingMgr, m_dwTimerIndex) == 0x28);
C_ASSERT(FIELD_OFFSET(CBaseBillingMgr, m_aHandler) == 0x38);
C_ASSERT(FIELD_OFFSET(CBaseBillingMgr, m_szAddress) == 0x80030);
C_ASSERT(FIELD_OFFSET(CBaseBillingMgr, m_wPort) == 0x80040);
C_ASSERT(sizeof(CBaseBillingMgr) == 0x80048);
