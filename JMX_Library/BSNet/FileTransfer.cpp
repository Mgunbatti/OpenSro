// File transfer over a session: one side sends a file (CSession::SendFile, the socket streams it
// as 0x1001 messages), the other prepares a file of the announced size
// (CSession::BeginFileTransfer) and writes every received block into it.

#include <stdlib.h>
#include <mbstring.h>
#include <string.h>

#include "BSLib/file/FileSystem.h"
#include "BSNet/Msg.h"
#include "BSNet/NetEngine.h"
#include "BSNet/Session.h"
#include "BSNet/SockStream.h"

// CreateDirectoryTreeForPath (0x492aa0).
BOOL CreateDirectoryTreeForPath(const std::string& strPath)
{
    std::string strDir;
    char szDrive[260];
    char szDir[260];
    char szBuffer[1024];
    _splitpath_s(strPath.c_str(), szDrive, 260, szDir, 260, NULL, 0, NULL, 0);
    _makepath_s(szBuffer, 0x400, szDrive, szDir, NULL, NULL);

    char szDelim[] = "\\";
    unsigned char* pContext = NULL;
    for (unsigned char* pToken = _mbstok_s((unsigned char*)szBuffer, (unsigned char*)szDelim,
                                           &pContext);
         pToken;
         pToken = _mbstok_s(NULL, (unsigned char*)szDelim, &pContext))
    {
        strDir.append((const char*)pToken, strlen((const char*)pToken));
        CreateDirectoryA(strDir.c_str(), NULL);
        strDir.append("\\", 1);
    }
    return TRUE;
}

// CSession::OpenSendFile (0x4929d0): opens strPath with the file system for this session (write
// mode with dwSize bytes when dwSize != 0, creating missing directories) and starts the transfer
// count at dwOffset.
BOOL CSession::OpenTransferFile(DWORD dwSize, int nFileType, std::string& strPath,
                                DWORD dwOffset)
{
    if ((DWORD)nFileType > FILE_TYPE_MAPPED)
        return FALSE;
    if (m_pFile)
        CloseTransferFile();

    CFileSystem* pFS = CFileSystem::GetInstance();
    DWORD dwHandle = 0;
    int nResult = pFS->OpenFile(&dwHandle, nFileType, strPath, dwSize, m_dwID);
    if (nResult == FILE_ERR_CREATE_FILE)
    {
        if (!dwSize || !CreateDirectoryTreeForPath(strPath))
            return FALSE;
        nResult = pFS->OpenFile(&dwHandle, nFileType, strPath, dwSize, m_dwID);
    }
    if (nResult)
        return FALSE;

    m_pFile = pFS->GetFile(dwHandle);
    if (!m_pFile)
        return FALSE;
    m_dwTransferred = 0;
    if ((int)dwOffset > 0)
    {
        if (dwOffset >= m_pFile->GetSize())
        {
            CloseTransferFile();
            return FALSE;
        }
        // BUG(NET-008): IFile::Seek only checks the context; the file position stays at 0
        // while the transfer count starts at dwOffset.
        m_pFile->Seek(m_dwID);
        m_dwTransferred = dwOffset;
    }
    return TRUE;
}

// CFileSystem::CloseFile_wrap (0x492d40).
int CSession::CloseTransferFile()
{
    int nResult = -1;
    if (m_pFile)
    {
        nResult = CFileSystem::GetInstance()->CloseFile(m_pFile->GetFileHandle(), m_dwID);
        m_pFile = NULL;
        m_dwTransferred = 0;
    }
    return nResult;
}

// Session_BeginFileSendTransfer (0x492750): receive side.
BOOL CSession::BeginFileTransfer(int nFileType, std::string strPath, DWORD dwSize,
                                 DWORD dwOffset)
{
    if (m_pFile || !m_pSocket || !dwSize)
        return FALSE;

    // The socket is checked again (Session_CheckPtrErr8009).
    if (!OpenTransferFile(dwSize, nFileType, strPath, dwOffset) || !m_pSocket)
    {
        FileTransfer_Finish(this, 0);
        return FALSE;
    }
    return TRUE;
}

// CSession::SendFile (0x492850).
BOOL CSession::SendFile(DWORD dwOffset, int nFileType, std::string strPath)
{
    if (m_pFile)
    {
        // "m_pFile is not NULL! Is the file of an earlier send still there?"
        NetEngineLog(0x2000000,
                     "CSession::SendFile() - \xC4\xCA m_pFile\xC0\xCC NULL\xC0\xCC "
                     "\xBE\xC6\xB4\xCF\xB4\xD9! \xC0\xCC\xC0\xFC\xBF\xA1 Send\xC7\xCF\xB4\xF8 "
                     "\xC8\xAD\xC0\xCF\xC0\xCC \xBE\xC6\xC1\xF7 \xB3\xB2\xBE\xC6\xC0\xD6\xB4\xC2 "
                     "\xB0\xC5\xB3\xC4?");
        return FALSE;
    }

    if (!OpenTransferFile(0, nFileType, strPath, dwOffset))
    {
        // "... failed!"
        NetEngineLog(0x2000000,
                     "CSession::SendFile() - _PrepareFileTransfer() \xBD\xC7\xC6\xD0!");
        FileTransfer_Finish(this, 0);
        return FALSE;
    }

    if (!m_pSocket)
    {
        // "Socket Pointer is NULL!"; the file stays open.
        NetEngineLog(0x2000000,
                     "CSession::SendFile() - \xC4\xCA Socket Pointer\xB0\xA1 NULL "
                     "\xC0\xCC\xB4\xD9!");
        return FALSE;
    }

    if (m_pSocket->SendFile(m_pFile, dwOffset))
    {
        NetEngineLog(0x2000000, "CSession::SendFile() - m_pSocket->SendFile() \xBD\xC7\xC6\xD0");
        FileTransfer_Finish(this, 0);
        return FALSE;
    }
    return TRUE;
}

// FileTransfer_GetPercent (0x492c60).
DWORD CSession::GetTransferPercent()
{
    DWORD dwStep = m_pFile->GetSize() / 100;
    if (dwStep)
        return m_dwTransferred / dwStep;
    return m_dwTransferred ? 100 : 0;
}

// FileTransfer_AddReceived (0x492c90): posts SESSION_EVENT_FILE_PROGRESS when the percentage
// changed; TRUE once the whole file arrived.
BOOL CSession::AddReceived(DWORD dwBytes)
{
    DWORD dwPercent = GetTransferPercent();
    m_dwTransferred += dwBytes;
    DWORD dwTransferred = m_dwTransferred;
    if (dwPercent != GetTransferPercent())
        PostEvent(SESSION_EVENT_FILE_PROGRESS, m_dwID, dwTransferred);
    return m_dwTransferred >= m_pFile->GetSize();
}

// FileTransfer_AccumulateSent (0x492c40).
BOOL FileTransfer_AccumulateSent(CSession* pSession, DWORD dwBytes)
{
    pSession->m_dwTransferred += dwBytes;
    return pSession->m_dwTransferred >= pSession->m_pFile->GetSize();
}

// FileTransfer_Finish (0x492ce0).
void FileTransfer_Finish(CSession* pSession, int nResult)
{
    if (nResult)
    {
        // Context 0 keeps the file registered after the session's context is gone.
        pSession->m_pFile->AddUser(0);
        IFile* pFile = pSession->m_pFile;
        pSession->CloseTransferFile();
        pSession->PostEvent(SESSION_EVENT_FILE_DONE, pSession->m_dwID, (DWORD)(DWORD_PTR)pFile);
    }
    else
    {
        pSession->CloseTransferFile();
        pSession->PostEvent(SESSION_EVENT_FILE_FAILED, pSession->m_dwID, 0);
    }
}

// Session_HandleFileTransferData (0x470420).
int Session_HandleFileTransferData(CMsg* pMsg, CSockStream* pSock)
{
    CSession* pSession = pSock->m_pSession;
    int nResult = 0;
    if (!pSession)
    {
        nResult = SOCK_ERR_NULL;
    }
    else
    {
        IFile* pFile = pSession->m_pFile;
        if (pFile && pFile->IsOpen())
        {
            int nWritten = pFile->Write(pSock->GetOwnerSessionID(),
                                        pMsg->m_pData + pMsg->m_wReadPos,
                                        pMsg->GetPayloadSize());
            if (pSession->AddReceived(nWritten) == TRUE)
                FileTransfer_Finish(pSession, 1);
        }
        else
        {
            nResult = SOCK_ERR_NO_FILE;
        }
    }
    pMsg->SkipRemaining();
    g_pNetEngine->ReleaseMsg(pMsg);
    return nResult;
}
