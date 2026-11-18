#pragma once

#include <windows.h>
#include <wininet.h>

// The FTP client of the crest uploader (August 2011 framework, SR_ShardManager 0x6be610..
// 0x6be8e0, vftable 0x781e2c). Server, user and password come from the ftp:// URL the caller
// passes to Connect; the remote folder and the upload size (a 256-byte crest) are fixed.
// Failures are reported through NetEngineLog with 0x2000000. Link wininet.lib.
class CSimpleFTP
{
public:
    enum { UPLOAD_SIZE = 0x100 };

    CSimpleFTP() : m_hInternet(NULL), m_hConnect(NULL) {}
    virtual ~CSimpleFTP() { Close(); }                                          // 0x40aaa0

    // InternetOpen ("SimpleFTPUploader"), InternetCrackUrl and a passive FTP connection.
    BOOL Connect(const char* pszURL);                                           // 0x6be610
    void Close();                                                               // 0x6be7c0
    // FtpSetCurrentDirectory "./SRO_CREST".
    BOOL ChangeToCrestDir();                                                    // 0x6be7f0
    BOOL DeleteRemoteFile(const char* pszFile);                                 // 0x6be8a0
    // Writes UPLOAD_SIZE bytes of pData to the file (binary, no cache).
    BOOL UploadBuffer(const char* pszFile, const void* pData);                  // 0x6be8e0

public:
    HINTERNET   m_hInternet;    // +4
    HINTERNET   m_hConnect;     // +8
};

C_ASSERT(sizeof(CSimpleFTP) == 0x0C);
