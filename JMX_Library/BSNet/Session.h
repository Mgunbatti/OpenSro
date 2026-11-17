#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>

#include "BSLib/thread/SynchObject.h"

class CMsg;
class CSockStream;
class CTask;
class IFile;
struct SECURITY_HANDSHAKE_PARAM;

// Intrusive list of all objects of class T (one list per class, kept in statics). An object is
// linked by Link() (or by the constructor when ms_bAutoLink is set, which it never is). Removing
// the node the current iteration stands on moves the iterator to the next node and sets bit 0
// of ms_dwIterFlags, so loops that close objects while walking keep their place.
template <class T>
class CClassLink
{
public:
    CClassLink()
    {
        m_bLinked = FALSE;
        if (ms_bAutoLink)
            Link();
    }

    virtual ~CClassLink()                                               // 0
    {
        Unlink();
    }

    // SessionList_Append / SessionList_PushBack: appends unless already linked.
    void Link()
    {
        if (m_bLinked)
            return;
        m_bLinked = TRUE;
        ++ms_nCount;
        if (ms_pHead)
        {
            CClassLink* pTail = ms_pTail;
            pTail->m_pNext = this;
            ms_pTail = this;
            m_pPrev = pTail;
        }
        else
        {
            ms_pHead = this;
            ms_pTail = this;
            m_pPrev = NULL;
        }
        m_pNext = NULL;
    }

    // SessionList_Unlink.
    void Unlink()
    {
        if (!m_bLinked)
            return;
        m_bLinked = FALSE;
        if (this == ms_pIter)
        {
            ms_dwIterFlags |= 1;
            ms_pIter = m_pNext;
        }
        --ms_nCount;
        if (m_pPrev)
            m_pPrev->m_pNext = m_pNext;
        else
            ms_pHead = m_pNext;
        if (m_pNext)
            m_pNext->m_pPrev = m_pPrev;
        else
            ms_pTail = m_pPrev;
        m_pNext = NULL;
        m_pPrev = NULL;
    }

    // Iteration over the list as the loops of the original do it (not thread safe by itself).
    static CClassLink* IterFirst()
    {
        ms_dwIterFlags &= ~1;
        ms_pIter = ms_pHead;
        return ms_pIter;
    }

    static CClassLink* IterNext()
    {
        if (ms_dwIterFlags & 1)
        {
            ms_dwIterFlags &= ~1;
            return ms_pIter;
        }
        ms_pIter = ms_pIter->m_pNext;
        return ms_pIter;
    }

public:
    BOOL        m_bLinked;      // +4
    CClassLink* m_pPrev;        // +8   not initialised by the constructor
    CClassLink* m_pNext;        // +C   not initialised by the constructor

    static BOOL         ms_bAutoLink;       // 0x4FAD58, never set
    static int          ms_nCount;          // 0x4FAD5C
    static CClassLink*  ms_pHead;           // 0x4FAD60
    static CClassLink*  ms_pTail;           // 0x4FAD64
    static CClassLink*  ms_pIter;           // 0x4FAD68
    static DWORD        ms_dwIterFlags;     // 0x4FAD6C
};

template <class T> BOOL CClassLink<T>::ms_bAutoLink = FALSE;
template <class T> int CClassLink<T>::ms_nCount = 0;
template <class T> CClassLink<T>* CClassLink<T>::ms_pHead = NULL;
template <class T> CClassLink<T>* CClassLink<T>::ms_pTail = NULL;
template <class T> CClassLink<T>* CClassLink<T>::ms_pIter = NULL;
template <class T> DWORD CClassLink<T>::ms_dwIterFlags = 0;

// Events a session posts to its task's queue: a CMsg with the event as opcode, the session ID
// in CMsg::m_dwSessionID and the payload DWORD session ID, DWORD parameter.
enum
{
    SESSION_EVENT_CONNECTED     = 0x1001,   // parameter: socket role (0 accepted, 1 connected)
    SESSION_EVENT_CLOSED        = 0x1002,   // parameter: the socket's close state or the reason
    SESSION_EVENT_HANDSHAKED    = 0x1003,   // parameter: 6
    SESSION_EVENT_FILE_PROGRESS = 0x1004,   // parameter: bytes received so far
    SESSION_EVENT_FILE_DONE     = 0x1005,   // parameter: the IFile*
    SESSION_EVENT_FILE_FAILED   = 0x1006,   // parameter: 0
};

// Task IDs registered by the engine. Sessions only accept task IDs with one of the bits
// 0x11000000.
enum
{
    NETENGINE_TASK_LISTEN       = 0x10000001,   // proactor of the listener
    NETENGINE_TASK_IO           = 0x10000002,   // proactor of the session sockets
    NETENGINE_TASK_MAINTAINER   = 0x10000003,
    NETENGINE_TASK_DATAGRAM     = 0x10000005,   // peer process (g_dwNetDatagramTaskID)
    NETENGINE_TASK_MASK         = 0x11000000,
};

// A connection of the engine: binds a stream socket to an ID and to the task that receives its
// messages and events. Sessions come from the session table's pool and are reused.
class CSession
{
public:
    CSession();
    virtual ~CSession();                                                // 0

    static CSession* FromLink(CClassLink<CSession>* pLink)
    {
        return (CSession*)((BYTE*)pLink - FIELD_OFFSET(CSession, m_Link));
    }

    // Under the session lock: binds the socket, the ID and the task, links the session, posts
    // SESSION_EVENT_CONNECTED and attaches the socket to the I/O proactor (which starts
    // receiving). FALSE when the task is unknown or the socket could not be attached.
    BOOL Bind(CSockStream* pSock, DWORD dwID, DWORD dwTaskID);
    // Sets the socket's message task and remembers its ID; FALSE for unknown IDs.
    BOOL AttachTask(DWORD dwTaskID);
    // Unless the engine is shutting down: SESSION_EVENT_CLOSED, keep-alive bookkeeping, socket
    // back to its pool, transfer file closed, fields cleared.
    void CloseInternal(DWORD dwReason);
    // Clears the socket's message task (engine shutdown).
    void DisconnectSocketIfAny();
    void ResetIoFields();
    // Allocates the event message and pushes it to the session's task (if it has a job).
    void PostEvent(WORD wEvent, DWORD dwID, DWORD dwParam);
    // Under the session lock: FALSE when there is no usable socket, the socket is closing
    // (close state 6) or nothing was received for more than dwTimeout ms (-1: no limit).
    BOOL IsAlive(DWORD dwTimeout);
    BOOL StartHandshake(SECURITY_HANDSHAKE_PARAM* pParam);
    // 0x8008 when there is no open socket, otherwise CSockStream::SendMsg.
    int SendMsg(CMsg* pMsg);

    // file transfer (FileTransfer.cpp)
    BOOL BeginFileTransfer(int nFileType, std::string strPath, DWORD dwSize, DWORD dwOffset);
    BOOL SendFile(DWORD dwOffset, int nFileType, std::string strPath);
    BOOL OpenTransferFile(DWORD dwSize, int nFileType, std::string& strPath, DWORD dwOffset);
    int CloseTransferFile();
    DWORD GetTransferPercent();
    BOOL AddReceived(DWORD dwBytes);

public:
    CClassLink<CSession>    m_Link;             // +4
    // 1: no packet security, never reaped, kept by CleanupDisconnectedSessions
    // (ExcludeSessionFromCleanup).
    BOOL                    m_bTrusted;         // +14
    DWORD                   m_dwID;             // +18
    DWORD                   m_dwTaskID;         // +1C
    DWORD                   m_dwTransferred;    // +20  bytes of the file sent / received
    IFile*                  m_pFile;            // +24  file being transferred
    CSockStream*            m_pSocket;          // +28
    CCriticalSectionBS      m_cs;               // +2C
};

C_ASSERT(sizeof(CClassLink<CSession>) == 0x10);
C_ASSERT(FIELD_OFFSET(CSession, m_Link) == 4);
C_ASSERT(FIELD_OFFSET(CSession, m_bTrusted) == 0x14);
C_ASSERT(FIELD_OFFSET(CSession, m_dwID) == 0x18);
C_ASSERT(FIELD_OFFSET(CSession, m_pFile) == 0x24);
C_ASSERT(FIELD_OFFSET(CSession, m_pSocket) == 0x28);
C_ASSERT(FIELD_OFFSET(CSession, m_cs) == 0x2C);
C_ASSERT(sizeof(CSession) == 0x68);

// ProactorMap_Find_wrap: the task registered under dwTaskID, NULL for IDs without the
// NETENGINE_TASK_MASK bits.
CTask* Session_FindTask(DWORD dwTaskID);

// Session_PostEvent_wrap (0x4924e0): SESSION_EVENT_HANDSHAKED.
void Session_PostHandshakeEvent(CSession* pSession);

// Opcode 0x1001 on a session: the payload is written to the file being received; the message
// is released. 0x8009 without a session, 0x800F without an open file.
int Session_HandleFileTransferData(CMsg* pMsg, CSockStream* pSock);
// Adds dwBytes to the bytes sent; TRUE when the whole file was sent.
BOOL FileTransfer_AccumulateSent(CSession* pSession, DWORD dwBytes);
// Ends the transfer: nResult != 0 keeps the file registered (context 0), closes it and posts
// SESSION_EVENT_FILE_DONE; otherwise closes it and posts SESSION_EVENT_FILE_FAILED.
void FileTransfer_Finish(CSession* pSession, int nResult);

// Creates every directory of the path's directory part. Always TRUE.
BOOL CreateDirectoryTreeForPath(const std::string& strPath);

// CSession fields the sockets read (they also work on objects with the same layout).
inline BOOL Session_GetDword14(CSession* pSession)
{
    return pSession->m_bTrusted;
}

inline DWORD Session_GetID(CSession* pSession)
{
    return pSession->m_dwID;
}

inline IFile* Session_GetWorkingFile(CSession* pSession)
{
    return pSession->m_pFile;
}
