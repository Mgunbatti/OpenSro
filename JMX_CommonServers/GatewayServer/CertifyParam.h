#pragma once

#include <windows.h>
#include <string>

#include "ServerFramework/ServerBody.h"

// One login being certified (sCertifyParam, 0x290 bytes). Taken from g_CertifyParamPool by the
// certifier job of the login request (0x6102); it travels as the context of the relays to the
// GlobalManager (0x6111 / 0x6112) and the AgentServer (0x6200 / 0x6203) and of the captcha
// requests (0x6321), and goes back to the pool through CertParam_Release.
//
// The member names of the fields the main process uses are the ones it was written against.

// Second record of the contents 2 and 24 (the original's "dwFreeParam1", 0x28 bytes): the result
// of the Yahoo age check, read by the login hand-over (minimum ages 15 and 18).
struct sCertifyFreeParam
{
    std::string strName;        // +0   first field of the age check answer
    int         nAge;           // +1C
    int         n20;            // +20
    int         n24;            // +24
};

C_ASSERT(FIELD_OFFSET(sCertifyFreeParam, nAge) == 0x1C);
C_ASSERT(sizeof(sCertifyFreeParam) == 0x28);

struct sCertifyParam
{
    DWORD               dw0;            // +0    JID (account number) of the certification query
    DWORD               dwType;         // +4    content ID of the login request
    std::string         strID;          // +8    account name (lower case)
    std::string         str24;          // +24   password as typed (hashed for the queries)
    BYTE                b40;            // +40   security group, primary
    BYTE                b41;            // +41   security group, content (3: normal user)
    char                szBlockReason[0x200];   // +42  _QueryBlockGuide row: the reason text
    BYTE                abBlockEnd[0x10];       // +242 rest of that row, sent to the client
    WORD                w252;           // +252
    DWORD               dwSessionID;    // +254  client session
    DWORD               dw258;          // +258  client address (network order)
    WORD                wPort;          // +25C  client port (set before the AgentServer login)
    WORD                w25E;           // +25E
    SERVER_BODY*        pAgentBody;     // +260  AgentServer chosen for the login
    union
    {
        sCertifyFreeParam*  pFreeParam1;    // +264  contents 2 and 24 only
        void*               pIBUVData;      //       its name in the main process's test
    };
    DWORD               dw268;          // +268
    WORD                w26C;           // +26C  extra of the 15-byte _CertifyTB_User row
    WORD                w26E;           // +26E
    DWORD               dw270;          // +270  extra of the 15-byte _CertifyTB_User row
    std::wstring        wstrIBUVCode;   // +274  captcha code the client must type
};

C_ASSERT(FIELD_OFFSET(sCertifyParam, strID) == 0x8);
C_ASSERT(FIELD_OFFSET(sCertifyParam, str24) == 0x24);
C_ASSERT(FIELD_OFFSET(sCertifyParam, b40) == 0x40);
C_ASSERT(FIELD_OFFSET(sCertifyParam, szBlockReason) == 0x42);
C_ASSERT(FIELD_OFFSET(sCertifyParam, abBlockEnd) == 0x242);
C_ASSERT(FIELD_OFFSET(sCertifyParam, dwSessionID) == 0x254);
C_ASSERT(FIELD_OFFSET(sCertifyParam, pAgentBody) == 0x260);
C_ASSERT(FIELD_OFFSET(sCertifyParam, pFreeParam1) == 0x264);
C_ASSERT(FIELD_OFFSET(sCertifyParam, w26C) == 0x26C);
C_ASSERT(FIELD_OFFSET(sCertifyParam, dw270) == 0x270);
C_ASSERT(FIELD_OFFSET(sCertifyParam, wstrIBUVCode) == 0x274);
C_ASSERT(sizeof(sCertifyParam) == 0x290);

typedef sCertifyParam CERTIFY_PARAM;

// CertParam_Release (0x414410): frees the age check record (contents 2 and 24) and returns the
// record to g_CertifyParamPool; *ppParam becomes NULL. Nothing for NULL.
void CertParam_Release(sCertifyParam** ppParam);
