#include "CertifyDB.h"

#include <stdlib.h>
#include <string.h>
#include <wininet.h>
#include <shlwapi.h>
#include <strsafe.h>

#include "BSLib/DB/DBQuery.h"
#include "BSNet/security/MD5.h"
#include "ServerFramework/ServerDataExtern.h"
#include "CertifyConstants.h"
#include "GatewayServer.h"
#include "UserCertifier.h"

#define CERTIFY_USER_KEY_SQL "exec _CertifyUser '%s','%s','" GATEWAY_CERTIFY_USER_KEY "'"

// Result rows of the certification procedures.
#pragma pack(push, 1)

struct CERTIFY_TB_ROW
{
    BYTE    byResult;           // +0   0 ok, 3 blocked
    DWORD   dwJID;              // +1
    BYTE    bySecPrimary;       // +5
    BYTE    bySecContent;       // +6
};
C_ASSERT(sizeof(CERTIFY_TB_ROW) == 7);

struct CERTIFY_TB_EXT_ROW
{
    BYTE    byResult;           // +0
    DWORD   dwJID;              // +1
    BYTE    bySecPrimary;       // +5
    BYTE    bySecContent;       // +6
    WORD    w7;                 // +7   -> w26C
    WORD    w9;                 // +9
    DWORD   dwB;                // +B   -> dw270
};
C_ASSERT(sizeof(CERTIFY_TB_EXT_ROW) == 15);

struct CERTIFY_USER_ROW
{
    DWORD   dwJID;              // +0   0: refused
    BYTE    bySecPrimary;       // +4
    BYTE    bySecContent;       // +5
};
C_ASSERT(sizeof(CERTIFY_USER_ROW) == 6);

struct CERTIFY_RESULT_ROW
{
    BYTE    byResult;           // +0
    DWORD   dwJID;              // +1
};
C_ASSERT(sizeof(CERTIFY_RESULT_ROW) == 5);

#pragma pack(pop)

static void InitQuery(CDBQuery* pQuery, CDBSessionMgr* pSession, void* pRow, int nRowSize,
                      int nSession)
{
    pQuery->m_pSession = pSession;
    pQuery->m_pBuffer = (BYTE*)pRow;
    pQuery->m_nBufferSize = nRowSize;
    pQuery->m_nRowSize = nRowSize;
    pQuery->m_pTrigger = NULL;
    pQuery->m_nSession = nSession;
}

// _QueryBlockGuide into the reason of the record (account database): 5, or 3 when it fails.
static int QueryBlockGuide(CDBQuery* pQuery, int nSession, sCertifyParam* pParam, DWORD dwJID)
{
    InitQuery(pQuery, g_pAccountDBSession, pParam->szBlockReason, 0x210, nSession);
    memset(pParam->szBlockReason, 0, 0x210);
    if (pQuery->ExecuteFormat("exec _QueryBlockGuide %d", dwJID) < 0)
        return CERTIFY_RESULT_ERROR;
    return CERTIFY_RESULT_BLOCKED;
}

// BUG(GWCERT-012): the row is not initialised; when the query finds no row (result 1) the
// handlers act on whatever the stack held.
static int CertifyWithRow7(int nSession, sCertifyParam* pParam, const char* pszFormat,
                           BOOL bLogIncorrect)
{
    CERTIFY_TB_ROW row;
    CDBQuery query;
    InitQuery(&query, g_pGatewayDBSession, &row, sizeof(row), nSession);
    CMD5 md5;
    md5.Compute(pParam->str24.c_str());
    if (query.ExecuteFormat(pszFormat, pParam->strID.c_str(), md5.GetHexDigest()) < 0)
        return CERTIFY_RESULT_ERROR;
    if (row.byResult == 0)
    {
        pParam->dw0 = row.dwJID;
        pParam->b40 = row.bySecPrimary;
        pParam->b41 = row.bySecContent;
        return CERTIFY_RESULT_OK;
    }
    if (row.byResult == 3)
        return QueryBlockGuide(&query, nSession, pParam, row.dwJID);
    if (bLogIncorrect)
        LogPrintf(0x2000000, "Incorrect ID or PWD: %s", pParam->strID.c_str());
    return CERTIFY_RESULT_WRONG_PASSWORD;
}

static int CertifyWithRow15(int nSession, sCertifyParam* pParam)
{
    CERTIFY_TB_EXT_ROW row;
    CDBQuery query;
    InitQuery(&query, g_pGatewayDBSession, &row, sizeof(row), nSession);
    CMD5 md5;
    md5.Compute(pParam->str24.c_str());
    if (query.ExecuteFormat("exec _CertifyTB_User '%s','%s'", pParam->strID.c_str(),
                            md5.GetHexDigest()) < 0)
    {
        return CERTIFY_RESULT_ERROR;
    }
    if (row.byResult == 0)
    {
        pParam->dw0 = row.dwJID;
        pParam->b40 = row.bySecPrimary;
        pParam->b41 = row.bySecContent;
        pParam->dw270 = row.dwB;
        pParam->w26C = row.w7;
        return CERTIFY_RESULT_OK;
    }
    if (row.byResult == 3)
        return QueryBlockGuide(&query, nSession, pParam, row.dwJID);
    return CERTIFY_RESULT_WRONG_PASSWORD;
}

int __cdecl Certify_TBUser(int nSession, sCertifyParam* pParam)
{
    return CertifyWithRow7(nSession, pParam, "exec _CertifyTB_User '%s','%s'", TRUE);
}

int __cdecl Certify_TBUser_3(int nSession, sCertifyParam* pParam)
{
    return CertifyWithRow7(nSession, pParam, "exec _CertifyTB_User '%s','%s'", FALSE);
}

int __cdecl Certify_TBUser_4(int nSession, sCertifyParam* pParam)
{
    return CertifyWithRow7(nSession, pParam, "exec _CertifyTB_User '%s','%s'", FALSE);
}

int __cdecl DB_CertifyUser_WithBlockGuide(int nSession, sCertifyParam* pParam)
{
    return CertifyWithRow7(nSession, pParam, CERTIFY_USER_KEY_SQL, FALSE);
}

int __cdecl Certify_TBUser_Ext(int nSession, sCertifyParam* pParam)
{
    return CertifyWithRow15(nSession, pParam);
}

int __cdecl DB_CertifyTBUser_WithBlockGuide(int nSession, sCertifyParam* pParam)
{
    return CertifyWithRow15(nSession, pParam);
}

int __cdecl Certify_CertifyUserWithKey(int nSession, sCertifyParam* pParam)
{
    CERTIFY_USER_ROW row;
    CDBQuery query;
    InitQuery(&query, g_pAccountDBSession, &row, sizeof(row), nSession);
    CMD5 md5;
    md5.Compute(pParam->str24.c_str());
    if (query.ExecuteFormat(CERTIFY_USER_KEY_SQL, pParam->strID.c_str(), md5.GetHexDigest()) < 0)
        return CERTIFY_RESULT_ERROR;
    if (!row.dwJID)
        return CERTIFY_RESULT_WRONG_PASSWORD;
    if (row.bySecPrimary == 127 && row.bySecContent == 127)
        return QueryBlockGuide(&query, nSession, pParam, row.dwJID);
    pParam->dw0 = row.dwJID;
    pParam->b40 = row.bySecPrimary;
    pParam->b41 = row.bySecContent;
    return CERTIFY_RESULT_OK;
}

int __cdecl DB_CertifyUser(int nSession, sCertifyParam* pParam)
{
    CERTIFY_USER_ROW row;
    CDBQuery query;
    InitQuery(&query, g_pAccountDBSession, &row, sizeof(row), nSession);
    CMD5 md5;
    md5.Compute(pParam->str24.c_str());
    if (query.ExecuteFormat(CERTIFY_USER_KEY_SQL, pParam->strID.c_str(), md5.GetHexDigest()) < 0)
        return CERTIFY_RESULT_ERROR;
    if (!row.dwJID)
        return CERTIFY_RESULT_WRONG_PASSWORD;
    pParam->dw0 = row.dwJID;
    pParam->b40 = row.bySecPrimary;
    pParam->b41 = row.bySecContent;
    return CERTIFY_RESULT_OK;
}

// ---------------------------------------------------------------------------------------------
// Web certification

BOOL Http_GetUrlContent(const char* pszURL, char* pBuffer, DWORD cbBuffer, DWORD* pdwElapsed)
{
    if (lstrlenA(pszURL) < 1 || !pBuffer)
        return FALSE;
    if (pdwElapsed)
        *pdwElapsed = 0;
    DWORD dwStart = GetTickCount();
    HINTERNET hInternet = InternetOpenA("GatewayServer", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet)
    {
        LogPrintf(0x2000001, "(%d), (%d)", 456, GetLastError());
        return FALSE;
    }
    DWORD dwFlags = 0xA0000000;
    if (strstr(pszURL, "https"))
        dwFlags = 0xA0800000;
    HINTERNET hURL = InternetOpenUrlA(hInternet, pszURL, NULL, 0, dwFlags, 0);
    if (!hURL)
    {
        LogPrintf(0x2000001, "(%d), (%d) - %s, 0x%X", 468, GetLastError(), pszURL, dwFlags);
        InternetCloseHandle(hInternet);
        return FALSE;
    }
    DWORD dwAvailable = 0;
    DWORD dwRead = 0;
    DWORD dwTotal = 0;
    do
    {
        InternetQueryDataAvailable(hURL, &dwAvailable, 0, 0);
        if (dwTotal + dwAvailable >= cbBuffer)
        {
            LogPrintf(0x2000001, "(%d), (%d)", 485, GetLastError(), dwAvailable);
            InternetCloseHandle(hURL);
            InternetCloseHandle(hInternet);
            return FALSE;
        }
        InternetReadFile(hURL, pBuffer + dwTotal, dwAvailable, &dwRead);
        dwTotal += dwRead;
    } while (dwRead);
    InternetCloseHandle(hURL);
    InternetCloseHandle(hInternet);
    pBuffer[dwTotal] = 0;
    if (!dwTotal)
    {
        LogPrintf(0x2000001, "(%d), (%d)", 506, GetLastError());
        return FALSE;
    }
    if (pdwElapsed)
        *pdwElapsed = GetTickCount() - dwStart;
    return TRUE;
}

BOOL String_SplitAtDelimiter(const std::string* pstr, std::string* pstrHead,
                             std::string* pstrTail)
{
    std::string::size_type nPos = pstr->find('\x05', 0);
    if (nPos == std::string::npos)
        return FALSE;
    pstrHead->assign(pstr->substr(0, nPos), 0, std::string::npos);
    pstrTail->assign(pstr->substr(nPos + 1, pstr->size()), 0, std::string::npos);
    return TRUE;
}

// BUG(GWCERT-005): the password is logged in clear text (escape errors and every answer).
// BUG(GWCERT-006): the answer is read into 256 bytes with a limit of 1024; a longer answer
// overwrites the escaped password and the URL.
int Certify_YahooKorea(const char* pszID, const char* pszPassword)
{
    // The stack frame of the original: answer, escaped password, URL, escaped account name.
    char abFrame[0xA00];
    char* pszAnswer = abFrame;
    char* pszEscapedPassword = abFrame + 0x100;
    char* pszURL = abFrame + 0x200;
    char* pszEscapedID = abFrame + 0x600;

    DWORD cch = 1024;
    if (UrlEscapeA(pszID, pszEscapedID, &cch, URL_ESCAPE_SEGMENT_ONLY) < 0
        || (cch = 1024, UrlEscapeA(pszPassword, pszEscapedPassword, &cch,
                                   URL_ESCAPE_SEGMENT_ONLY) < 0))
    {
        LogPrintf(0x2000000, "UrlEscapeError : %s, %s", pszID, pszPassword);
        return CERTIFY_RESULT_ERROR;
    }
    if (FAILED(StringCchPrintfA(pszURL, 0x400,
                                "http://login.korea.yahoo.com/config/isp_verify_user?.src=mmo&l="
                                "%s&p=%s",
                                pszEscapedID, pszEscapedPassword)))
    {
        BSAssertFail();
        LogPrintf(0x2000001, "UrlEscapeError : %s, %s", pszID, pszPassword);
        return CERTIFY_RESULT_ERROR;
    }
    DWORD dwElapsed = 0;
    if (!Http_GetUrlContent(pszURL, pszAnswer, 0x400, &dwElapsed))
    {
        LogPrintf(0x2000001, "GetCGIResultString Failed!! (%s)", pszURL);
        return CERTIFY_RESULT_ERROR;
    }
    LogPrintf(0x2000001, "[Yahoo] Certification elapsed time is %d ms !! (%s, %s)", dwElapsed,
              pszEscapedID, pszEscapedPassword);
    if (strstr(pszAnswer, "OK:0") || strstr(pszAnswer, "NOMAIL:0"))
        return CERTIFY_RESULT_OK;
    if (!strstr(pszAnswer, "Invalid Password"))
        LogPrintf(0x1000000, "unconditional error code : %s", pszAnswer);
    return CERTIFY_RESULT_WRONG_PASSWORD;
}

// BUG(GWCERT-008): the last field is read from the rest of the answer at the length of the token
// instead of from the token.
int Certify_YahooAgeCheck(const char* pszID, sCertifyFreeParam* pFree)
{
    char szAnswer[0x400];
    char szURL[0x400];
    szAnswer[0] = 0;
    if (FAILED(StringCchPrintfA(szURL, 0x400,
                                "http://kr.udbp.yahoo.com/udbproxy.html?_login=%s&_src=silkroad",
                                pszID)))
    {
        BSAssertFail();
        return CERTIFY_RESULT_ERROR;
    }
    DWORD dwElapsed = 0;
    if (!Http_GetUrlContent(szURL, szAnswer, 0x400, &dwElapsed))
        return CERTIFY_RESULT_ERROR;
    if (dwElapsed > 500)
        LogPrintf(0x2000001, "elapsed time for yahoo age checking is %d ms !!", dwElapsed);
    if (!lstrlenA(szAnswer))
    {
        LogPrintf(0, "failed to check age: result string empty(%s)", pszID);
        return CERTIFY_RESULT_ERROR;
    }
    if (strstr(szAnswer, "error:"))
    {
        LogPrintf(0, "failed to check age: %s", szAnswer);
        return CERTIFY_RESULT_ERROR;
    }
    std::string strRest(szAnswer);
    if (!String_SplitAtDelimiter(&strRest, &pFree->strName, &strRest))
        return CERTIFY_RESULT_ERROR;
    std::string strToken;
    if (!String_SplitAtDelimiter(&strRest, &strToken, &strRest)
        || !String_SplitAtDelimiter(&strRest, &strToken, &strRest))
    {
        return CERTIFY_RESULT_ERROR;
    }
    pFree->nAge = atol(strToken.c_str());
    if (!String_SplitAtDelimiter(&strRest, &strToken, &strRest))
        return CERTIFY_RESULT_ERROR;
    pFree->n20 = atol(strToken.c_str());
    std::string::size_type nLen = strToken.size();
    if (nLen < 1 || nLen > strRest.size())
        return CERTIFY_RESULT_ERROR;
    pFree->n24 = strRest[nLen - 1];
    return CERTIFY_RESULT_OK;
}

// BUG(GWCERT-005): "there is no additional account info" logs the password.
// BUG(GWCERT-013): "#$" accounts put the typed password into the SQL text unfiltered.
int __cdecl Certify_YahooKoreaUDB(int nSession, sCertifyParam* pParam)
{
    CERTIFY_RESULT_ROW row;
    CDBQuery query;
    if (strstr(pParam->strID.c_str(), "#$"))
    {
        InitQuery(&query, g_pAccountDBSession, &row, sizeof(row), nSession);
        if (query.ExecuteFormat("exec _CertifyUser_SU '%s','%s'", pParam->strID.c_str(),
                                pParam->str24.c_str()) < 0)
        {
            return CERTIFY_RESULT_ERROR;
        }
        if (row.byResult)
        {
            if (row.byResult == 2)
                return CERTIFY_RESULT_2;
            return CERTIFY_RESULT_WRONG_PASSWORD;
        }
        if (!row.dwJID)
            return CERTIFY_RESULT_WRONG_PASSWORD;
        pParam->dw0 = row.dwJID;
    }
    else
    {
        int nResult = Certify_YahooKorea(pParam->strID.c_str(), pParam->str24.c_str());
        if (nResult)
        {
            LogPrintf(0x2000001, "UDB Processing Failed!!! (%d)", nResult);
            return nResult;
        }
        if (!g_pAccountDBSession)
        {
            LogPrintf(0x2000001, "g_DBHandle_Web == NULL");
            return CERTIFY_RESULT_ERROR;
        }
        InitQuery(&query, g_pAccountDBSession, &row, sizeof(row), nSession);
        if (query.ExecuteFormat("exec _CertifyUser '%s'", pParam->strID.c_str()) < 0)
        {
            LogPrintf(0x2000001, "_CertifyUser Query Failed (%s)", query.m_szQuery);
            return CERTIFY_RESULT_ERROR;
        }
        switch (row.byResult)
        {
        case 0:
            break;
        case 1:
            LogPrintf(0x1000000, "there is no additional account info! [%s/%s]",
                      pParam->strID.c_str(), pParam->str24.c_str());
            return CERTIFY_RESULT_NO_ACCOUNT;
        case 2:
            return CERTIFY_RESULT_2;
        case 3:
            return QueryBlockGuide(&query, nSession, pParam, row.dwJID);
        default:
            return CERTIFY_RESULT_WRONG_PASSWORD;
        }
        if (!row.dwJID)
            return CERTIFY_RESULT_WRONG_PASSWORD;
        pParam->dw0 = row.dwJID;
        if (!pParam->pFreeParam1)
        {
            LogPrintf(0x2000001, "pParam->dwFreeParam1 == NULL (%s, %d)",
                      "D:\\WORK2005\\JMX_CommonServers\\GatewayServer\\UserCertifier.cpp", 1131);
            return CERTIFY_RESULT_ERROR;
        }
        nResult = Certify_YahooAgeCheck(pParam->strID.c_str(), pParam->pFreeParam1);
        if (nResult)
            return nResult;
    }

    InitQuery(&query, g_pGatewayDBSession, &pParam->b40, 2, nSession);
    if (query.ExecuteFormat("exec _GetUserSecurityGroup '%s'", pParam->strID.c_str()) < 0)
    {
        pParam->b40 = 3;
        pParam->b41 = 3;
    }
    return CERTIFY_RESULT_OK;
}

// BUG(GWCERT-009): when the failure answer cannot be parsed, the login fail notice requests the
// checkUser URL (with the password hash) a second time.
int __cdecl Certify_JoymaxCGI(int nSession, sCertifyParam* pParam)
{
    char szURL[0x1000];
    char szAnswer[0x1000];
    DWORD dwElapsed = 60000;
    CMD5 md5;
    md5.Compute(pParam->str24.c_str());
    if (FAILED(StringCchPrintfA(szURL, 0x1000,
                                "http://cgi.joymax.com/property/silkroadonline/checkUser.cgi?"
                                "userid=%s&pwd=%s&userip=%u",
                                pParam->strID.c_str(), md5.GetHexDigest(), pParam->dw258)))
    {
        LogPrintf(0x2000000, "Failed to make Login CGI URL.");
        return CERTIFY_RESULT_WEB_FAILED;
    }
    if (!Http_GetUrlContent(szURL, szAnswer, 0x1000, &dwElapsed))
    {
        LogPrintf(0x2000000,
                  "Failed to request to property Login CGI: can't open URL!. URL(%s), Elapsed "
                  "time(%u)",
                  szURL, dwElapsed);
        return CERTIFY_RESULT_WEB_FAILED;
    }
    LogPrintf(0, "Login CGI result(%s)", szAnswer);

    std::string strAnswer;
    strAnswer.assign(szAnswer, strlen(szAnswer));
    std::string::size_type nPos = strAnswer.find('|', 0);
    bool bFailed = true;
    if (nPos > 0)
    {
        if (strAnswer.substr(0, nPos).compare("0") == 0)
            bFailed = false;
    }
    if (bFailed)
    {
        LogPrintf(0, "Fail to certify from Session Server. UID(%s)", pParam->strID.c_str());
        return CERTIFY_RESULT_WEB_FAILED;
    }
    LogPrintf(0, "Success to Login from Session Server. UID(%s)", pParam->strID.c_str());

    CERTIFY_TB_ROW row;
    CDBQuery query;
    InitQuery(&query, g_pGatewayDBSession, &row, sizeof(row), nSession);
    if (query.ExecuteFormat("exec _CertifyTB_User_Portal '%s'", pParam->strID.c_str()) < 0)
    {
        LogPrintf(0x2000000, "_CertifyTB_User : Error : %d", 1267);
        return CERTIFY_RESULT_ERROR;
    }
    if (row.byResult == 0)
    {
        LogPrintf(0, "Login success at sro");
        pParam->dw0 = row.dwJID;
        pParam->b40 = row.bySecPrimary;
        pParam->b41 = row.bySecContent;
        return CERTIFY_RESULT_OK;
    }

    LogPrintf(0, "Login fail at sro. send Login fail msg to session server");
    std::string::size_type nStart = nPos + 1;
    std::string::size_type nEnd = strAnswer.find('|', nStart);
    if (nEnd == std::string::npos || nEnd == nStart)
    {
        LogPrintf(0x2000000, "Fail to parsing at result of Login CGI. CGI result(%s)",
                  strAnswer.c_str());
    }
    else
    {
        std::string strJID = strAnswer.substr(nStart, nEnd - nStart);
        bool bError = FAILED(StringCchPrintfA(
            szURL, 0x1000, "http://cgi.joymax.com/property/silkroadonline/LoginFail.cgi?jid=%s",
            strJID.c_str()));
        if (bError)
            LogPrintf(0x2000000, "Failed to make Login fail CGI URL.");
    }
    if (!Http_GetUrlContent(szURL, szAnswer, 0x1000, &dwElapsed))
    {
        LogPrintf(0x2000000,
                  "Failed to request to property Login fail CGI: can't open URL!. URL(%s), "
                  "Elapsed time(%u)",
                  szURL, dwElapsed);
    }
    if (row.byResult != 3)
        return CERTIFY_RESULT_WRONG_PASSWORD;
    return QueryBlockGuide(&query, nSession, pParam, row.dwJID);
}

// ---------------------------------------------------------------------------------------------
// Handler table

// BUG(GWCERT-001): division 0 calls the address 0x400 and division 27 a NULL pointer.
static PFN_CERTIFY_HANDLER const s_apfnCertifyHandler[37] = {
    (PFN_CERTIFY_HANDLER)0x400,         // 0
    Certify_CertifyUserWithKey,         // 1
    Certify_YahooKoreaUDB,              // 2
    Certify_TBUser_Ext,                 // 3
    Certify_JoymaxCGI,                  // 4
    Certify_TBUser_Ext,                 // 5
    Certify_TBUser,                     // 6
    Certify_TBUser,                     // 7
    Certify_TBUser,                     // 8
    Certify_TBUser_Ext,                 // 9
    Certify_TBUser_Ext,                 // 10
    Certify_CertifyUserWithKey,         // 11
    Certify_TBUser,                     // 12
    Certify_TBUser_3,                   // 13
    Certify_TBUser_3,                   // 14
    Certify_TBUser_4,                   // 15
    Certify_TBUser_4,                   // 16
    Certify_TBUser_3,                   // 17
    Certify_CertifyUserWithKey,         // 18
    Certify_CertifyUserWithKey,         // 19
    Certify_YahooKoreaUDB,              // 20
    DB_CertifyTBUser_WithBlockGuide,    // 21
    DB_CertifyTBUser_WithBlockGuide,    // 22
    DB_CertifyTBUser_WithBlockGuide,    // 23
    DB_CertifyUser_WithBlockGuide,      // 24
    DB_CertifyUser_WithBlockGuide,      // 25
    Certify_TBUser_Ext,                 // 26
    NULL,                               // 27
    Certify_TBUser_Ext,                 // 28
    DB_CertifyUser_WithBlockGuide,      // 29
    DB_CertifyUser,                     // 30
    Certify_CertifyUserWithKey,         // 31
    Certify_CertifyUserWithKey,         // 32
    DB_CertifyTBUser_WithBlockGuide,    // 33
    Certify_TBUser_Ext,                 // 34
    Certify_TBUser,                     // 35
    Certify_TBUser_3,                   // 36
};

int UserCertifier_GetHandler(int nSession, sCertifyParam* pParam)
{
    BYTE byDivision = g_pSelfBody->byDivisionID;
    if (byDivision > 36)
    {
        LogPrintf(0x2000000,
                  "There is no certification handler!!! you must register it first!!! [Div ID: "
                  "%d]",
                  byDivision);
        return CERTIFY_RESULT_ERROR;
    }
    return s_apfnCertifyHandler[byDivision](nSession, pParam);
}
