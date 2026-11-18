#pragma once

#include <winsock2.h>
#include <windows.h>

class CMsg;

// IBillingMgr: the billing manager of the main process (CGlobalManagerMainProcess +0x400C8),
// created from .\_billing.ini (sub_40E950): type 1 is CStandardBillingMgr (the external billing
// server), everything else CDummyBillingMgr.
struct IBillingMgr
{
    // Address and port of the billing server, pool size of the network unit.
    virtual BOOL Initialize(const char* pszAddress, WORD wPort, WORD wPoolSize) = 0;    // 0
    virtual void Cleanup() = 0;                                                         // 1
    virtual BOOL Start() = 0;                                                           // 2
    virtual void Stop() = 0;                                                            // 3
    // Deletes the object.
    virtual void Release() = 0;                                                         // 4
    virtual BOOL IsReady() = 0;                                                         // 5
    // A message of the main process (0x6201, 0x6202, 0x620C, 0x6900, 0x6904): 2 when it was
    // taken, 0 otherwise (CDummyBillingMgr: 1).
    virtual int ProcessMsg(CMsg* pMsg, DWORD dwSessionID, DWORD dwRelayID) = 0;          // 6
};

C_ASSERT(sizeof(IBillingMgr) == 4);

// CDummyBillingMgr (4 bytes): no billing server.
class CDummyBillingMgr : public IBillingMgr
{
public:
    // 0x40bb80: true.
    virtual BOOL Initialize(const char*, WORD, WORD)
    {
        return TRUE;
    }
    // 0x40bbb0
    virtual void Cleanup()
    {
    }
    // 0x40bb90: true.
    virtual BOOL Start()
    {
        return TRUE;
    }
    // 0x40bba0
    virtual void Stop()
    {
    }
    // 0x40bbc0
    virtual void Release()
    {
        delete this;
    }
    // 0x40bbe0: true.
    virtual BOOL IsReady()
    {
        return TRUE;
    }
    // 0x40bbf0: 1.
    virtual int ProcessMsg(CMsg*, DWORD, DWORD)
    {
        return 1;
    }
};
