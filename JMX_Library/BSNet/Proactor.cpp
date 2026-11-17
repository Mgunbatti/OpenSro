#include "Proactor.h"

#include <string.h>

#include "BSLib/thread/Task.h"
#include "BSNet/NetEngine.h"
#include "BSNet/NetEngineLog.h"
#include "BSNet/QueMT.h"
#include "BSNet/SockListener.h"
#include "BSNet/SockStream.h"

// ---------------------------------------------------------------------------------------------
// Close path shared by the proactors

int Proactor_NotifyHandlerClosed(IBSNet* pEngine, CEventHandler* pHandler, DWORD dwReason)
{
    CSockListener* pListener = NetEngine_GetListener(pEngine);
    if (pListener)
        return Listener_ForwardHandlerClosed(pListener, static_cast<CBaseSocket*>(pHandler),
                                             dwReason);
    return NetEngine_CloseSession(static_cast<CSockStream*>(pHandler)->GetOwnerSessionID(),
                                  dwReason);
}

// ---------------------------------------------------------------------------------------------
// CProactorIOCP

BS_IMPLEMENT_DYNCREATE(CProactorIOCP, &CServiceObjectSock::classCServiceObjectSock)

CProactorIOCP::CProactorIOCP()
{
    m_hPort = NULL;
    m_nActive = 0;
    m_dwConcurrency = 0;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    m_dwConcurrency = 2 * si.dwNumberOfProcessors;
    m_hPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, m_dwConcurrency);
}

CProactorIOCP::~CProactorIOCP()
{
    Close();
}

void CProactorIOCP::Close()
{
    SignalStop();
    if (m_hPort)
    {
        CloseHandle(m_hPort);
        m_hPort = NULL;
    }
}

BOOL CProactorIOCP::AssociateHandle(HANDLE hHandle, CEventHandler* pHandler)
{
    if (CreateIoCompletionPort(hHandle, m_hPort, (ULONG_PTR)pHandler, m_dwConcurrency)
        || GetLastError() == ERROR_INVALID_PARAMETER)
    {
        return TRUE;
    }
    NetEngineLog(0x2000000,
                 "\xC4\xC9\xBF\xA2! CreateIoCompletionPort(%d, %d, %d, %d) \xBD\xC7\xC6\xD0!!! "
                 "[LastError: %d]",
                 hHandle, m_hPort, pHandler, m_dwConcurrency, GetLastError());
    return FALSE;
}

DWORD CProactorIOCP::Run(volatile LONG* pbStop, int /*nIndex*/)
{
    if (*pbStop)
        return 0;

    InterlockedIncrement(&m_nActive);
    DWORD dwBytes = 0;
    ULONG_PTR ulKey = 0;
    LPOVERLAPPED pOverlapped = NULL;
    while (!*pbStop)
    {
        BOOL bOk = GetQueuedCompletionStatus(m_hPort, &dwBytes, &ulKey, &pOverlapped, INFINITE);
        CBaseSocket* pHandler = (CBaseSocket*)ulKey;
        if (pHandler == NULL)
            break;

        BS_OVERLAPPED* pIo = (BS_OVERLAPPED*)pOverlapped;
        if (pIo->dwIoType == 3)
        {
            CloseHandler(pHandler, 6);
            continue;
        }

        if (!bOk)
        {
            // BUG(PROACT-001): a failed pooled CMsg is released here and still dispatched; a
            // non-zero result below releases it a second time.
            if (pIo->dwNotPooled == 0)
                NetEngine_ReleaseMsg(pOverlapped);
            if (pHandler->m_nKind == SOCK_KIND_STREAM)
            {
                CloseHandler(pHandler, 3);
                continue;
            }
        }

        int nResult = pHandler->OnIOCompletion(dwBytes, pOverlapped);
        if (nResult)
        {
            if (pOverlapped && ((BS_OVERLAPPED*)pOverlapped)->dwNotPooled == 0)
                NetEngine_ReleaseMsg(pOverlapped);
            if (nResult & 0x8000)
                CloseHandler(pHandler, (DWORD)(WORD)nResult << 16);
        }
    }
    InterlockedDecrement(&m_nActive);
    return 0;
}

BOOL CProactorIOCP::PostStopPacket()
{
    if (m_hPort == NULL)
        return FALSE;
    return PostQueuedCompletionStatus(m_hPort, 0x44, 0, NULL) != 0;
}

BOOL CProactorIOCP::PostStopPackets(int nCount)
{
    for (int i = 0; i < nCount; ++i)
    {
        // BUG(PROACT-003): PostStopPacket returns 0 or 1, so a failed post is never detected.
        if (PostStopPacket() == -1)
            return FALSE;
    }
    return TRUE;
}

int CProactorIOCP::SignalStop()
{
    CSingleLockBS lock(&m_cs, TRUE);
    int nActive = m_nActive;
    if (nActive)
        return PostStopPackets(nActive);
    return 0;
}

int CProactorIOCP::CloseHandler(CEventHandler* pHandler, DWORD dwReason)
{
    return Proactor_NotifyHandlerClosed(g_pNetEngine, pHandler, dwReason);
}

// ---------------------------------------------------------------------------------------------
// CProactorEvent

BS_IMPLEMENT_DYNCREATE(CProactorEvent, &CServiceObjectSock::classCServiceObjectSock)

CProactorEvent::CProactorEvent()
{
    memset(m_ahEvents, 0, sizeof(m_ahEvents));
    memset(m_apOverlapped, 0, sizeof(m_apOverlapped));
    for (int i = MAX_EVENTS; i; --i)
    {
        WSAEVENT hEvent = WSACreateEvent();
        m_listFreeEvents.push_back(hEvent);
    }
    m_ahEvents[0] = TakeFreeEvent();
    m_ahEvents[1] = TakeFreeEvent();
    m_nActive = 0;
    m_nHandles = 0;
    RecountHandles();
}

CProactorEvent::~CProactorEvent()
{
    CSingleLockBS lock(&m_cs, TRUE);
    // Events of slots without a socket are the pool's own; the sockets close theirs.
    for (int i = 0; i < MAX_EVENTS; i += 2)
    {
        if (m_apOverlapped[i] == NULL)
        {
            WSAEVENT hEvent = m_ahEvents[i];
            if (hEvent)
                m_listFreeEvents.push_back(hEvent);
            hEvent = m_ahEvents[i + 1];
            if (hEvent)
                m_listFreeEvents.push_back(hEvent);
        }
    }
    for (std::list<WSAEVENT>::iterator it = m_listFreeEvents.begin();
         it != m_listFreeEvents.end(); ++it)
    {
        WSACloseEvent(*it);
    }
    m_listFreeEvents.clear();
}

WSAEVENT CProactorEvent::TakeFreeEvent()
{
    WSAEVENT hEvent = m_listFreeEvents.front();
    m_listFreeEvents.pop_front();
    return hEvent;
}

BOOL CProactorEvent::RecountHandles()
{
    m_nHandles = 0;
    for (int i = 0; i < MAX_EVENTS; ++i)
    {
        if (m_ahEvents[i])
            ++m_nHandles;
    }
    return SetEvent(m_ahEvents[1]);
}

BOOL CProactorEvent::ClearSlot(int nIndex)
{
    if ((unsigned)(nIndex - 2) > 0x3D)
        return FALSE;

    int nHandles = m_nHandles;
    m_ahEvents[nIndex] = NULL;
    m_ahEvents[nIndex + 1] = NULL;
    m_apOverlapped[nIndex] = NULL;
    m_apOverlapped[nIndex + 1] = NULL;
    RecountHandles();
    // BUG(PROACT-002): placeholders are put back only when nothing was cleared; a cleared
    // socket slot stays NULL inside the waited range.
    if (nHandles == m_nHandles)
    {
        m_ahEvents[nIndex] = TakeFreeEvent();
        m_ahEvents[nIndex + 1] = TakeFreeEvent();
    }
    return TRUE;
}

BOOL CProactorEvent::AssociateHandle(HANDLE /*hHandle*/, CEventHandler* pHandler)
{
    CSingleLockBS lock(&m_cs, TRUE);
    int nIndex = 2;
    while (m_apOverlapped[nIndex])
    {
        nIndex += 2;
        if (nIndex >= MAX_EVENTS)
            return FALSE;
    }

    WSAEVENT hEvent = m_ahEvents[nIndex];
    if (hEvent)
        m_listFreeEvents.push_back(hEvent);
    hEvent = m_ahEvents[nIndex + 1];
    if (hEvent)
        m_listFreeEvents.push_back(hEvent);

    CSockStream* pSock = static_cast<CSockStream*>(pHandler);
    SOCK_OVERLAPPED* pOverlappedA4 = &pSock->m_ovRecv;
    SOCK_OVERLAPPED* pOverlapped60 = &pSock->m_ovSend;
    m_ahEvents[nIndex] = pOverlappedA4->ov.hEvent;
    m_apOverlapped[nIndex] = pOverlappedA4;
    m_ahEvents[nIndex + 1] = pOverlapped60->ov.hEvent;
    m_apOverlapped[nIndex + 1] = pOverlapped60;
    RecountHandles();
    return TRUE;
}

SOCK_OVERLAPPED* CProactorEvent::GetSlotOverlapped(int nIndex)
{
    CSingleLockBS lock(&m_cs, TRUE);
    return m_apOverlapped[nIndex];
}

int CProactorEvent::CloseHandler(CEventHandler* pHandler, DWORD dwIndex)
{
    CSingleLockBS lock(&m_cs, TRUE);
    ClearSlot(dwIndex & ~1);
    if (pHandler)
        return NetEngine_CloseSession(
            static_cast<CSockStream*>(pHandler)->GetOwnerSessionID(), 3);
    return 0;
}

int CProactorEvent::SignalStop()
{
    WSASetEvent(m_ahEvents[0]);
    return 1;
}

DWORD CProactorEvent::Run(volatile LONG* pbStop, int /*nIndex*/)
{
    if (*pbStop)
        return 0;

    InterlockedIncrement(&m_nActive);
    DWORD cbTransfer = 0;
    DWORD dwFlags = 0;
    while (!*pbStop)
    {
        DWORD nCount = m_nHandles;
        if (nCount == 0)
            break;

        DWORD dwIndex = WSAWaitForMultipleEvents(nCount, m_ahEvents, FALSE, WSA_INFINITE, FALSE);
        if (dwIndex == WSA_WAIT_FAILED || dwIndex == 0)
            break;
        if (dwIndex == 1)
        {
            WSAResetEvent(m_ahEvents[1]);
            continue;
        }
        if (dwIndex == WSA_WAIT_TIMEOUT)
            continue;

        WSAResetEvent(m_ahEvents[dwIndex]);
        SOCK_OVERLAPPED* pOverlapped = GetSlotOverlapped(dwIndex);
        if (pOverlapped == NULL)
            continue;

        CBaseSocket* pSock = pOverlapped->pOwner;
        cbTransfer = 0;
        if (!WSAGetOverlappedResult(pSock->m_hSocket, &pOverlapped->ov, &cbTransfer, FALSE,
                                    &dwFlags))
        {
            if (WSAGetLastError() != WSA_IO_INCOMPLETE)
                CloseHandler(pSock, dwIndex);
        }
        else if (cbTransfer == 0)
        {
            CloseHandler(pSock, dwIndex);
        }
        else
        {
            int nResult = pSock->OnIOCompletion(cbTransfer, &pOverlapped->ov);
            if (nResult && (nResult & 0x8000))
                CloseHandler(pSock, dwIndex);
        }
    }
    InterlockedDecrement(&m_nActive);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Proactor_Create

BOOL Proactor_Create(CRuntimeClass* pClass, CTask* pTask, CThreadManager* pThreadManager,
                     void* pParam)
{
    pTask->m_pThreadManager = pThreadManager;

    OSVERSIONINFOEXA vi;
    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXA);
    if (!GetVersionExA((LPOSVERSIONINFOA)&vi))
    {
        vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
        if (!GetVersionExA((LPOSVERSIONINFOA)&vi))
            return FALSE;
    }

    IQue<CMsg>* pQueue;
    if (vi.dwPlatformId == VER_PLATFORM_WIN32_NT)
        pQueue = new CQueMT<CMsg>;
    else
        pQueue = new CQueMT9x<CMsg>;
    pTask->m_pQueue = pQueue;
    pTask->m_desc.pOwner = pTask;

    CServiceObject* pJob = static_cast<CServiceObject*>(pClass->CreateObject());
    pTask->m_desc.pJob = pJob;
    // BUG(PROACT-004): no NULL check; a class without factory (or a faulting one) crashes here.
    return pJob->Initialize(pTask, pParam);
}
