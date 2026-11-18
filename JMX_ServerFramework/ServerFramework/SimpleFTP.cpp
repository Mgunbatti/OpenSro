#include "ServerFramework/SimpleFTP.h"

#include <string.h>

#include "BSNet/NetEngineLog.h"

static const char CREST_DIR[] = "./SRO_CREST";

BOOL CSimpleFTP::Connect(const char* pszURL)
{
    m_hInternet = InternetOpenA("SimpleFTPUploader", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!m_hInternet)
    {
        NetEngineLog(0x2000000, "InternetOpen Failed (%d)\n", GetLastError());
        return FALSE;
    }
    URL_COMPONENTSA components;
    char szUserName[256];
    char szPassword[256];
    char szHostName[256];
    char szUrlPath[256];
    char szScheme[256];
    char szExtraInfo[256];
    memset(&components, 0, sizeof(components));
    components.dwStructSize = sizeof(components);
    components.lpszScheme = szScheme;
    components.dwSchemeLength = 256;
    components.nScheme = INTERNET_SCHEME_FTP;
    components.lpszHostName = szHostName;
    components.dwHostNameLength = 256;
    components.nPort = 0;
    components.lpszUserName = szUserName;
    components.dwUserNameLength = 256;
    components.lpszPassword = szPassword;
    components.dwPasswordLength = 256;
    components.lpszUrlPath = szUrlPath;
    components.dwUrlPathLength = 256;
    components.lpszExtraInfo = szExtraInfo;
    components.dwExtraInfoLength = 256;
    if (!InternetCrackUrlA(pszURL, lstrlenA(pszURL), 0, &components))
    {
        NetEngineLog(0x2000000, "InternetCrackUrl Failed (%d)\n", GetLastError());
        return FALSE;
    }
    m_hConnect = InternetConnectA(m_hInternet, components.lpszHostName, components.nPort,
                                  components.lpszUserName, components.lpszPassword,
                                  INTERNET_SERVICE_FTP, INTERNET_FLAG_PASSIVE, 0);
    if (!m_hConnect)
    {
        // BUG(FW11-027): the whole URL, password included, goes into the log.
        NetEngineLog(0x2000000, "InternetConnect[%s] Failed (%d)\n", pszURL, GetLastError());
        return FALSE;
    }
    return TRUE;
}

void CSimpleFTP::Close()
{
    if (m_hConnect)
    {
        InternetCloseHandle(m_hConnect);
        m_hConnect = NULL;
    }
    if (m_hInternet)
    {
        InternetCloseHandle(m_hInternet);
        m_hInternet = NULL;
    }
}

BOOL CSimpleFTP::ChangeToCrestDir()
{
    if (!m_hConnect)
        return FALSE;
    if (!FtpSetCurrentDirectoryA(m_hConnect, CREST_DIR))
    {
        DWORD dwError = GetLastError();
        // The response buffer is not initialised; it stays so when no response is available.
        char szResponse[256];
        DWORD dwLength = 256;
        InternetGetLastResponseInfoA(&dwError, szResponse, &dwLength);
        NetEngineLog(0x2000000, "FtpSetCurrentDirectory[Dir: %s] Failed (%d, %s)\n", CREST_DIR,
                     dwError, szResponse);
        return FALSE;
    }
    return TRUE;
}

BOOL CSimpleFTP::DeleteRemoteFile(const char* pszFile)
{
    if (!m_hConnect)
        return FALSE;
    if (!FtpDeleteFileA(m_hConnect, pszFile))
    {
        NetEngineLog(0x2000000, "FtpDeleteFile[File: %s] Failed (%d)\n", pszFile, GetLastError());
        return FALSE;
    }
    return TRUE;
}

BOOL CSimpleFTP::UploadBuffer(const char* pszFile, const void* pData)
{
    const BYTE* pbyData = (const BYTE*)pData;
    DWORD dwLeft = UPLOAD_SIZE;
    HINTERNET hFile = NULL;
    BOOL bResult = FALSE;
    __try
    {
        if (!m_hConnect)
            __leave;
        hFile = FtpOpenFileA(m_hConnect, pszFile, GENERIC_WRITE,
                             INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                             FTP_TRANSFER_TYPE_BINARY, 0);
        if (!hFile)
        {
            NetEngineLog(0x2000000, "FtpOpenFile[file: %s] Failed (%d)\n", pszFile,
                         GetLastError());
            __leave;
        }
        DWORD dwWritten = 0;
        char szResponse[256];
        memset(szResponse, 0, sizeof(szResponse));
        DWORD dwLength = 256;
        for (;;)
        {
            if (!InternetWriteFile(hFile, pbyData, dwLeft, &dwWritten))
            {
                DWORD dwError = GetLastError();
                if (dwError == ERROR_INTERNET_EXTENDED_ERROR)
                    InternetGetLastResponseInfoA(&dwError, szResponse, &dwLength);
                NetEngineLog(0x2000000, "InternetWriteFile[file: %s] Failed (%d, %s)\n", pszFile,
                             dwError, szResponse);
                __leave;
            }
            pbyData += dwWritten;
            dwLeft -= dwWritten;
            if (dwLeft == 0)
                break;
        }
        bResult = TRUE;
    }
    __finally
    {
        if (hFile)
            InternetCloseHandle(hFile);
    }
    return bResult;
}
