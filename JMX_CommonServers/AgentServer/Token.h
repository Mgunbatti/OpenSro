#pragma once

#include <winsock2.h>
#include <windows.h>
#include <map>
#include <string>

#include "ServerFramework/OverlapJob.h"

// Login tokens. The GatewayServer hands a certified login over with 0x6203 (relayed): the
// AgentServer keeps it as a token and answers 0xA203 with the token ID, which the gateway gives
// the client. The client then logs in here (0x6103) with that ID, its account name and password;
// the token is used up by the first attempt, or dropped after 10 s.

enum
{
    AGENT_TOKEN_TIMEOUT     = 10000,    // ms
    AGENT_TOKEN_TIMER_ID    = 2,        // process timer of the timeout check
    AGENT_TOKEN_TIMER_PERIOD = 3000,
};

// CToken (0x58 bytes).
class CToken
{
public:
    // sub_409A40: an ID from g_TokenIDPool, the creation tick.
    CToken();
    // 0x409ae0: gives the ID back to g_TokenIDPool.
    virtual ~CToken();                                              // 0

public:
    DWORD           m_dwTokenID;        // +4
    DWORD           m_dwClientIP;       // +8   the 0x6103 must come from this address
    std::string     m_strID;            // +C   account name
    std::string     m_strPassword;      // +28
    DWORD           m_dwJID;            // +44
    BYTE            m_bySecPrimary;     // +48
    BYTE            m_bySecContent;     // +49
    WORD            m_w4A;              // +4A
    DWORD           m_dwCreateTick;     // +4C
    WORD            m_w50;              // +50  extras of the gateway's certification row,
    WORD            m_w52;              // +52  passed on to the GlobalManager in 0x6201
    DWORD           m_dw54;             // +54
};

C_ASSERT(FIELD_OFFSET(CToken, m_strID) == 0xC);
C_ASSERT(FIELD_OFFSET(CToken, m_strPassword) == 0x28);
C_ASSERT(FIELD_OFFSET(CToken, m_dwJID) == 0x44);
C_ASSERT(FIELD_OFFSET(CToken, m_dwCreateTick) == 0x4C);
C_ASSERT(FIELD_OFFSET(CToken, m_dw54) == 0x54);
C_ASSERT(sizeof(CToken) == 0x58);

// 0x56B2D4: the token IDs (the same ID pool class as the framework's overlap job IDs).
extern ServerFramework::COverlapJobIDPool g_TokenIDPool;

typedef std::map<DWORD, CToken*> AgentTokenMap;

// CTokenManager (0x56B300): the tokens by ID.
class CTokenManager
{
public:
    // sub_409BB0 (dynamic initializer)
    CTokenManager() {}
    // sub_409C20: asserts that the tokens were removed.
    virtual ~CTokenManager();                                       // 0

    // sub_409D50: false when the ID is taken (the token is not stored).
    bool Add(CToken* pToken);
    // sub_409DC0: the extras of a token (both 0 first). FALSE for unknown IDs.
    BOOL GetExtra(DWORD dwTokenID, WORD* pw50, DWORD* pdw54);
    // sub_409E40: removes the token and checks it: the address and the name and password
    // (CompareStringA with flags 0, i.e. case-sensitive). On a match the JID and the security
    // groups are returned and the result is true. The token is deleted either way; false for
    // unknown IDs.
    bool Consume(DWORD dwTokenID, DWORD dwClientIP, const char* pszID, const char* pszPassword,
                 DWORD* pdwJID, BYTE* pbySecPrimary, BYTE* pbySecContent);
    // sub_409C90: deletes every token.
    void RemoveAll();
    // sub_409F60: deletes (and logs) the tokens older than AGENT_TOKEN_TIMEOUT.
    void CheckTimeout();

public:
    AgentTokenMap   m_mapToken;         // +4
};

C_ASSERT(sizeof(CTokenManager) == 0x10);

extern CTokenManager g_TokenManager;
