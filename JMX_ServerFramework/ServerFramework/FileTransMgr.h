#pragma once

#include <windows.h>
#include <list>
#include <string>

#include "ServerFramework/ModuleVersion.h"

class IBSNet;
class IFile;

// Framework file transfer between server modules. The receiving side queues the files it wants
// (FILE_TRANS_ENTRY) and asks the serving session for them one after the other with 0x6004
// {DWORD file ID, DWORD offset}; the serving side answers through the engine's file transfer
// (ServerProcess_SendFileTo) and confirms the end of a file with 0xA004 {BYTE 1}.

enum
{
    FILETRANS_OPCODE_REQ        = 0x6004,
    FILETRANS_OPCODE_ACK        = 0xA004,

    FILETRANS_FLAG_SEND         = 0x01,     // serves files
    FILETRANS_FLAG_RECV         = 0x10,     // requests files
};

// A file to receive.
struct FILE_TRANS_ENTRY
{
    DWORD                       dwFileID;   // +0
    char*                       pszPath;    // +4   local path (new char[MAX_PATH]), owned by
                                            //      g_FileTransPendingList
    DWORD                       dwSize;     // +8
    BYTE                        bC;         // +C   always 0
    DWORD                       dwOffset;   // +10  always 0
    const MODULE_VERSION_FILE*  pSource;    // +14
};

C_ASSERT(sizeof(FILE_TRANS_ENTRY) == 0x18);

typedef std::list<FILE_TRANS_ENTRY> FileTransEntryList;

// Decompression and packing policies; both do nothing in this build.
class CUncompDummy
{
};

class CPackerDummy
{
};

template <class TUncomp, class TPacker>
class CFileTransMgr
{
public:
    CFileTransMgr();
    virtual ~CFileTransMgr();

    // 0x45abe0: takes the engine (g_pBSNet, asserted) and enables sending and receiving.
    BOOL Init();
    // CFileTransMgr_ResetQueueAndState (0x45aba0): empties the receive queue and clears the
    // flags and the serving session.
    void Reset();

    // ServerProcessMain_HandleFileXferEvent1005 (0x45ad70): a transfer of pFile on the session
    // finished. Read mode (a file was served): confirms with 0xA004 if the session still
    // exists, returns 0. Write mode (a file was received): drops the queue front, returns the
    // number of files left. Otherwise -1. The engine's file handle is closed in every case.
    int OnFileDone(IFile* pFile, DWORD dwSessionID);
    // FileXfer_ClearSendQueue_Notify (0x45ae70): a transfer failed; empties the receive queue
    // and disconnects the session.
    int OnFileFailed(DWORD dwSessionID);

    // FileXfer_BuildSendQueueAndStart / PathList53B9D0_Rebuild (0x45a490): replaces
    // g_FileTransPendingList with the files of pFiles, stored under
    // <module dir>\<g_strFileTransDir>\<pszSubDir>\<file path>\<file name> (empty parts left
    // out), and starts receiving them from the session.
    BOOL BuildRecvQueueAndStart(DWORD dwSessionID, const char* pszSubDir,
                                const ModuleVersionFileList* pFiles);
    // 0x45ac10: copies g_FileTransPendingList to the receive queue and requests the first
    // file from the session. -1 (asserted) without receive mode, session or pending files.
    int StartRecv(DWORD dwSessionID);
    // 0x45ac70: requests the queue front if the queue is not empty, else returns 0.
    int SendNextQueuedFile();
    // FileXfer_SendFrontQueuedFile (0x45b140): prepares the session to receive the queue front
    // and sends the 0x6004 request. Returns the queue length, 0 when empty, -1 when the engine
    // refused the transfer.
    int SendFrontQueuedFile();
    // FileXfer_CheckQueueFrontValid (0x45b2a0): reads the queue front for a received file
    // (throws when the queue is empty) and returns TRUE.
    BOOL CheckQueueFront(IFile* pFile);

public:
    IBSNet*             m_pEngine;          // +4
    DWORD               m_dwFlags;          // +8   FILETRANS_FLAG_*
    FileTransEntryList  m_RecvQueue;        // +C
    DWORD               m_dwServerSession;  // +18  session the files are requested from
    DWORD               m_dw1C;             // +1C  not used
};

typedef CFileTransMgr<CUncompDummy, CPackerDummy> CFileTransMgrDefault;

// 0x53B9AC
extern CFileTransMgrDefault g_FileTransMgr;
// 0x53B9CC: the files to receive, built by BuildRecvQueueAndStart.
extern FileTransEntryList g_FileTransPendingList;
// 0x4F8334: directory below the module directory that received files go to (also used by
// ServerProcess_SendFileTo for served files).
extern std::string g_strFileTransDir;

// FileXfer_InitEngineAndName (0x45a1e0): g_FileTransMgr.Init() and sets g_strFileTransDir.
BOOL FileXfer_InitEngineAndName(const char* pszDir);
// FileXfer_ResetQueueAndState_Global (0x45a230)
void FileXfer_ResetQueueAndState_Global();
