#pragma once

#include "BaseBillingMgr.h"

// CStandardBillingMgr (0x80050 bytes, Billing\StandardBillingMgr.cpp): the billing server
// protocol. The client certifies itself (0x2001), the server answers with 0x4001; then the
// list of logged-in users is sent (0x8007, unless the server said it does not want it) and a
// heartbeat (0x2003, answer 0x4003) runs every 30 s. Logins, logouts and the other user events
// of the AgentServers (main process messages, ProcessMsg) are forwarded; the server's answers
// go back to the AgentServers and shard managers.
//
// Billing opcodes: 0x2001 certify, 0x2002 login, 0x2003 heartbeat, 0x2006 (0x6900), 0x8007 user
// list, 0x8008 logout (0x6202), 0x800A (0x6904), 0x400B online status answer; from the server
// 0x4001 certify answer, 0x4002 login answer, 0x4003 heartbeat answer, 0x8004 / 0x8005 / 0x4006
// / 0x8009 user events, 0x200B online status request.
class CStandardBillingMgr : public CBaseBillingMgr
{
public:
    // CStandardBillingMgr::ctor (0x451e80)
    explicit CStandardBillingMgr(ServerFramework::CServerProcessBase* pOwner);
    // sub_451F10 (deleting dtor 0x451ef0)
    virtual ~CStandardBillingMgr();                                 // 1

    // ActivateNetwork-named 0x4521b0: heartbeat, heartbeat timeout, reconnect.
    virtual void OnTimer(DWORD dwIndex);                            // 2
    // sub_452250: "Session Established!", then the certification request.
    virtual void OnConnected(DWORD dwUnit, DWORD dwSession);        // 3
    // sub_452290: logged; unless stopping the reconnect timer runs.
    virtual void OnDisconnected(DWORD dwUnit, DWORD dwSession);     // 4
    // CStandardBillingMgr::vf07 (0x451f60): the eight opcodes of the server.
    virtual void InitHandlers();                                    // 7

    // vf09 (0x4523c0) 0x4001 {BYTE result, BYTE no user list}: activated (user list unless
    // refused, heartbeat, heartbeat timer). A failure stops the manager and breaks (int 3).
    virtual void OnCertifyAck(CBillingPacket* pPacket);             // 9
    // vf10 (0x4524f0) 0x4002 {JID, BYTE result[, BYTE]}: 0xA201 to the AgentServer of the
    // login; a refused user is unregistered.
    virtual void OnLoginAck(CBillingPacket* pPacket);               // 10
    // vf11 (0x452690) 0x4003: the heartbeat answer timer stops.
    virtual void OnHeartBeatAck(CBillingPacket* pPacket);           // 11
    // vf12 (0x4526b0) 0x8004 {JID, BYTE, DWORD}: stored, 0x6902 {JID, 2, BYTE, DWORD} to the
    // user's AgentServer.
    virtual void On8004(CBillingPacket* pPacket);                   // 12
    // vf13 (0x452800) 0x8005 {JID, BYTE, DWORD}: 0x6902 {JID, 1, BYTE, DWORD}.
    virtual void On8005(CBillingPacket* pPacket);                   // 13
    // vf14 (0x452940) 0x4006: 0xA900 to the AgentServer of the request, then 0x6905 or
    // 0x6902 and 0x6903 (shard manager).
    virtual void On4006(CBillingPacket* pPacket);                   // 14
    // vf15 (0x452d30) 0x8009 {JID, BYTE}: 0x6905 {JID} to the user's AgentServer.
    virtual void On8009(CBillingPacket* pPacket);                   // 15
    // vf16 (0x452df0) 0x200B {JID}: 0x400B with the user's data.
    virtual void OnOnlineStatusReq(CBillingPacket* pPacket);        // 16
    // vf17 (0x452fb0): 0x2001 {DWORD 103}.
    virtual void SendCertifyReq();                                  // 17
    // vf18 (0x4536b0): 0x2003 {BYTE 0}, then the answer timer.
    virtual void SendHeartBeat();                                   // 18
    // vf19 (0x453770, 0x620C): the logged-in users in 0x8007 packets of up to 90 users.
    virtual void RefreshActiveUserList();                           // 19
    // vf20 (0x453050, 0x6201): 0x2002 login.
    virtual void OnUserLogin(CMsg* pMsg, DWORD dwSessionID, DWORD dwRelayID);       // 20
    // vf21 (0x4532b0, 0x6900): 0x2006 {JID} of a logged-in user.
    virtual void On6900(CMsg* pMsg, DWORD dwSessionID, DWORD dwRelayID);            // 21
    // vf22 (0x453590, 0x6904): 0x800A {DWORD, BYTE, DWORD}.
    virtual void On6904(CMsg* pMsg, DWORD dwSessionID, DWORD dwRelayID);            // 22
    // vf23 (0x453380, 0x6202): 0x8008 logout {BYTE type, JID or WORD count and JIDs}.
    virtual void OnUserLogout(CMsg* pMsg, DWORD dwSessionID, DWORD dwRelayID);      // 23

    // CStandardBillingMgr::vf00 (0x452040): base initialisation and the three timers.
    virtual BOOL Initialize(const char* pszAddress, WORD wPort, WORD wPoolSize);
    // CStandardBillingMgr::vf01 (0x4520d0)
    virtual void Cleanup();
    // CStandardBillingMgr::vf02 (0x4520e0): worker and connection.
    virtual BOOL Start();
    // CStandardBillingMgr::vf03 (0x452140): timers stop, session closed, worker suspended.
    virtual void Stop();
    // CStandardBillingMgr::vf04 (0x4521a0)
    virtual void Release();
    // CStandardBillingMgr::vf06 (0x4522f0)
    virtual int ProcessMsg(CMsg* pMsg, DWORD dwSessionID, DWORD dwRelayID);

    // sub_452F40: sends the packet while connected or activated.
    void SendPacket(CBillingPacket* pPacket);

public:
    BOOL    m_bNoActiveUserList;    // +80048  the server does not want the user list
    DWORD   m_dw8004C;              // +8004C
};

C_ASSERT(FIELD_OFFSET(CStandardBillingMgr, m_bNoActiveUserList) == 0x80048);
C_ASSERT(sizeof(CStandardBillingMgr) == 0x80050);
