#pragma once

#include <windows.h>
#include <string>

#include "CertifyParam.h"

// The certification handlers of UserCertifier.cpp: one per division (the publisher's account
// system), run by the certifier processes. nSession is the index of the certifier process and
// selects the ODBC connection of the session pool; pParam carries the account name and password
// and receives JID, security groups and the block reason. They return CERTIFY_RESULT_*.
//
// Queries go to the gateway database (g_pGatewayDBSession) or the account database
// (g_pAccountDBSession), passwords as lower-case MD5 hex.

typedef int (__cdecl* PFN_CERTIFY_HANDLER)(int nSession, sCertifyParam* pParam);

// UserCertifier_GetHandler (0x4144f0): runs the handler of g_pSelfBody's division; 3 (logged)
// for divisions above 36.
int UserCertifier_GetHandler(int nSession, sCertifyParam* pParam);

// _CertifyTB_User on the gateway database (row {BYTE result, DWORD JID, BYTE, BYTE}); result 0:
// JID and groups, 3: blocked, _QueryBlockGuide (5, 3 when that fails); otherwise 1.
// Certify_TBUser (0x414530) also logs "Incorrect ID or PWD"; Certify_TBUser_3 (0x415ac0) and
// Certify_TBUser_4 (0x415c80) are separate copies.
int __cdecl Certify_TBUser(int nSession, sCertifyParam* pParam);
int __cdecl Certify_TBUser_3(int nSession, sCertifyParam* pParam);
int __cdecl Certify_TBUser_4(int nSession, sCertifyParam* pParam);
// The same with the 15-byte row that also fills w26C and dw270 (0x4158f0, 0x415e40).
int __cdecl Certify_TBUser_Ext(int nSession, sCertifyParam* pParam);
int __cdecl DB_CertifyTBUser_WithBlockGuide(int nSession, sCertifyParam* pParam);
// _CertifyUser with the publisher key on the gateway database, otherwise as Certify_TBUser_3
// (0x416010).
int __cdecl DB_CertifyUser_WithBlockGuide(int nSession, sCertifyParam* pParam);
// _CertifyUser with the publisher key on the account database (row {DWORD JID, BYTE, BYTE}):
// JID 0: 1; groups 127/127: blocked (5 / 3); otherwise 0 (0x414710).
int __cdecl Certify_CertifyUserWithKey(int nSession, sCertifyParam* pParam);
// The same without the block check (0x4161d0).
int __cdecl DB_CertifyUser(int nSession, sCertifyParam* pParam);
// Yahoo Korea: "#$" accounts through _CertifyUser_SU, the others through the Yahoo login check,
// _CertifyUser and the age check; then _GetUserSecurityGroup (0x415010).
int __cdecl Certify_YahooKoreaUDB(int nSession, sCertifyParam* pParam);
// Joymax web login (checkUser.cgi), then _CertifyTB_User_Portal (0x415360).
int __cdecl Certify_JoymaxCGI(int nSession, sCertifyParam* pParam);

// Certify_YahooKorea (0x414a90): the Yahoo login check of the account; 0 ok, 1 refused,
// 3 error.
int Certify_YahooKorea(const char* pszID, const char* pszPassword);
// Certify_YahooAgeCheck (0x414da0): fills the age check record of the account; 0 or 3.
int Certify_YahooAgeCheck(const char* pszID, sCertifyFreeParam* pFree);

// Http_GetUrlContent (0x4148c0): WinINet GET of pszURL into pBuffer (cbBuffer bytes, the answer
// must be shorter); *pdwElapsed gets the time taken. False on errors (logged).
BOOL Http_GetUrlContent(const char* pszURL, char* pBuffer, DWORD cbBuffer, DWORD* pdwElapsed);
// String_SplitAtDelimiter (0x414c90): the part of *pstr before the first '\x05' goes to
// *pstrHead, the rest after it to *pstrTail (may be pstr). False without a delimiter.
BOOL String_SplitAtDelimiter(const std::string* pstr, std::string* pstrHead,
                             std::string* pstrTail);
