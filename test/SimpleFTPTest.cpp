// CSimpleFTP (August 2011 framework) without any network: a URL WinInet cannot crack, and the
// calls that need a connection. Nothing connects anywhere.
#include <windows.h>
#include <stdio.h>

#include "ServerFramework/SimpleFTP.h"
#include "TestCheck.h"

int main()
{
    {
        CSimpleFTP ftp;
        CHECK(ftp.m_hInternet == NULL && ftp.m_hConnect == NULL);
        // InternetOpen works offline; the URL has no scheme, so cracking it fails.
        CHECK(ftp.Connect("not a url") == FALSE);
        CHECK(ftp.m_hInternet != NULL);
        CHECK(ftp.m_hConnect == NULL);
        // Without a connection nothing is sent.
        CHECK(ftp.ChangeToCrestDir() == FALSE);
        CHECK(ftp.DeleteRemoteFile("crest.bin") == FALSE);
        BYTE byCrest[CSimpleFTP::UPLOAD_SIZE] = {0};
        CHECK(ftp.UploadBuffer("crest.bin", byCrest) == FALSE);
        ftp.Close();
        CHECK(ftp.m_hInternet == NULL && ftp.m_hConnect == NULL);
        ftp.Close();
    }
    {
        // The destructor closes what Connect opened.
        CSimpleFTP* pFtp = new CSimpleFTP;
        CHECK(pFtp->Connect("") == FALSE);
        delete pFtp;
    }
    return TEST_RESULT("SimpleFTPTest");
}
