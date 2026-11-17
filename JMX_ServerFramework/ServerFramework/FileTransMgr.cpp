#include "FileTransMgr.h"

#include <string.h>
#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "BSLib/file/IFile.h"
#include "BSNet/IBSNet.h"
#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerConfig.h"

CFileTransMgrDefault g_FileTransMgr;
FileTransEntryList g_FileTransPendingList;
std::string g_strFileTransDir;

// DynInit_CFileTransMgr_53B9AC (0x45aac0)
template <class TUncomp, class TPacker>
CFileTransMgr<TUncomp, TPacker>::CFileTransMgr()
{
    m_pEngine = NULL;
    m_dwServerSession = 0;
    m_dwFlags = 0;
}

// CFileTransMgr_CUncompDummy_CPackerDummy::dtor (0x45ab40), vf00 (0x45af30)
template <class TUncomp, class TPacker>
CFileTransMgr<TUncomp, TPacker>::~CFileTransMgr()
{
    Reset();
}

template <class TUncomp, class TPacker>
BOOL CFileTransMgr<TUncomp, TPacker>::Init()
{
    IBSNet* pEngine = g_pBSNet;
    if (!pEngine)
        BSAssertFail();
    m_pEngine = pEngine;
    m_dwFlags = FILETRANS_FLAG_SEND | FILETRANS_FLAG_RECV;
    return TRUE;
}

template <class TUncomp, class TPacker>
void CFileTransMgr<TUncomp, TPacker>::Reset()
{
    m_RecvQueue.clear();
    m_dwFlags = 0;
    m_dwServerSession = 0;
}

template <class TUncomp, class TPacker>
int CFileTransMgr<TUncomp, TPacker>::OnFileDone(IFile* pFile, DWORD dwSessionID)
{
    if (!pFile)
        BSAssertFail();
    int nResult = -1;
    DWORD dwFileHandle = pFile ? pFile->GetFileHandle() : 0;
    if (pFile->GetMode() == -1)
        BSAssertFail();

    switch (pFile->GetMode())
    {
    case 0:
        if (m_dwFlags & FILETRANS_FLAG_SEND)
        {
            if (m_pEngine->GetSessionSocketState(dwSessionID) != -1)
            {
                CMsg* pMsg = m_pEngine->AllocMsg(FALSE);
                pMsg->SetOpcode(FILETRANS_OPCODE_ACK);
                BYTE bDone = 1;
                pMsg->WriteBytes(&bDone, 1);
                m_pEngine->SendMsg(dwSessionID, pMsg);
                m_pEngine->ReleaseMsg(pMsg);
            }
            nResult = 0;
        }
        break;
    case 1:
        if ((m_dwFlags & FILETRANS_FLAG_RECV) && CheckQueueFront(pFile))
        {
            m_RecvQueue.pop_front();
            nResult = (int)m_RecvQueue.size();
        }
        break;
    }

    m_pEngine->CloseSessionTransferFile(0, dwFileHandle);
    return nResult;
}

template <class TUncomp, class TPacker>
int CFileTransMgr<TUncomp, TPacker>::OnFileFailed(DWORD dwSessionID)
{
    m_RecvQueue.clear();
    return m_pEngine->DisconnectSession(dwSessionID, 1);
}

template <class TUncomp, class TPacker>
BOOL CFileTransMgr<TUncomp, TPacker>::BuildRecvQueueAndStart(DWORD dwSessionID,
                                                             const char* pszSubDir,
                                                             const ModuleVersionFileList* pFiles)
{
    if (g_FileTransPendingList.size())
    {
        for (FileTransEntryList::iterator it = g_FileTransPendingList.begin();
             it != g_FileTransPendingList.end(); ++it)
        {
            delete[] it->pszPath;
        }
        g_FileTransPendingList.clear();
    }

    for (ModuleVersionFileList::const_iterator it = pFiles->begin(); it != pFiles->end(); ++it)
    {
        const MODULE_VERSION_FILE* pSource = *it;
        char* pszPath = new char[MAX_PATH];
        strcpy_s(pszPath, MAX_PATH, g_ServerGlobals.szModuleDir);

        // On a failed append the path buffer is lost and the pending list stays half built.
        if (g_strFileTransDir.size() > 0)
        {
            if (strlen(pszPath) && StringCchCatA(pszPath, MAX_PATH, "\\") != S_OK)
            {
                BSAssertFail();
                return FALSE;
            }
            if (StringCchCatA(pszPath, MAX_PATH, g_strFileTransDir.c_str()) != S_OK)
            {
                BSAssertFail();
                return FALSE;
            }
        }
        if (pszSubDir && strlen(pszSubDir))
        {
            if (strlen(pszPath) && StringCchCatA(pszPath, MAX_PATH, "\\") != S_OK)
            {
                BSAssertFail();
                return FALSE;
            }
            if (StringCchCatA(pszPath, MAX_PATH, pszSubDir) != S_OK)
            {
                BSAssertFail();
                return FALSE;
            }
        }
        if (strlen(pSource->szPath))
        {
            if (strlen(pszPath) && StringCchCatA(pszPath, MAX_PATH, "\\") != S_OK)
            {
                BSAssertFail();
                return FALSE;
            }
            if (StringCchCatA(pszPath, MAX_PATH, pSource->szPath) != S_OK)
            {
                BSAssertFail();
                return FALSE;
            }
        }
        if (strlen(pszPath) && StringCchCatA(pszPath, MAX_PATH, "\\") != S_OK)
        {
            BSAssertFail();
            return FALSE;
        }
        if (StringCchCatA(pszPath, MAX_PATH, pSource->szName) != S_OK)
        {
            BSAssertFail();
            return FALSE;
        }

        FILE_TRANS_ENTRY entry;
        entry.dwFileID = pSource->dwID;
        entry.pszPath = pszPath;
        entry.dwSize = pSource->dwSize;
        entry.bC = 0;
        entry.dwOffset = 0;
        entry.pSource = pSource;
        g_FileTransPendingList.push_back(entry);
    }

    return StartRecv(dwSessionID) != -1;
}

template <class TUncomp, class TPacker>
int CFileTransMgr<TUncomp, TPacker>::StartRecv(DWORD dwSessionID)
{
    m_dwServerSession = 0;
    if (m_RecvQueue.size())
        m_RecvQueue.clear();
    if (!(m_dwFlags & FILETRANS_FLAG_RECV) || !dwSessionID || !g_FileTransPendingList.size())
    {
        BSAssertFail();
        return -1;
    }
    m_dwServerSession = dwSessionID;
    // FileXfer_CopyPendingListTo (0x45af70) is this assignment
    m_RecvQueue = g_FileTransPendingList;
    return SendFrontQueuedFile();
}

template <class TUncomp, class TPacker>
int CFileTransMgr<TUncomp, TPacker>::SendNextQueuedFile()
{
    if (m_RecvQueue.size())
        return SendFrontQueuedFile();
    return 0;
}

template <class TUncomp, class TPacker>
int CFileTransMgr<TUncomp, TPacker>::SendFrontQueuedFile()
{
    int nCount = (int)m_RecvQueue.size();
    if (!nCount)
        return 0;

    FILE_TRANS_ENTRY& entry = m_RecvQueue.front();
    if (m_pEngine->BeginSessionFileTransfer(m_dwServerSession, std::string(entry.pszPath),
                                            entry.dwSize, entry.dwOffset, -1) != 1)
    {
        return -1;
    }

    CMsg* pMsg = m_pEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(FILETRANS_OPCODE_REQ);
    pMsg->WriteBytes(&entry.dwFileID, 4);
    pMsg->WriteBytes(&entry.dwOffset, 4);
    m_pEngine->SendMsg(m_dwServerSession, pMsg);
    m_pEngine->ReleaseMsg(pMsg);
    return nCount;
}

template <class TUncomp, class TPacker>
BOOL CFileTransMgr<TUncomp, TPacker>::CheckQueueFront(IFile* pFile)
{
    if (pFile->GetMode() == -1)
        BSAssertFail();
    if (pFile->GetMode() == 1)
        m_RecvQueue.front();
    return TRUE;
}

template class CFileTransMgr<CUncompDummy, CPackerDummy>;

C_ASSERT(FIELD_OFFSET(CFileTransMgrDefault, m_pEngine) == 0x4);
C_ASSERT(FIELD_OFFSET(CFileTransMgrDefault, m_RecvQueue) == 0xC);
C_ASSERT(FIELD_OFFSET(CFileTransMgrDefault, m_dwServerSession) == 0x18);
C_ASSERT(sizeof(CFileTransMgrDefault) == 0x20);

BOOL FileXfer_InitEngineAndName(const char* pszDir)
{
    g_FileTransMgr.Init();
    g_strFileTransDir.assign(pszDir, strlen(pszDir));
    return TRUE;
}

void FileXfer_ResetQueueAndState_Global()
{
    g_FileTransMgr.Reset();
}
