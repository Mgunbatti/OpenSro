#include "NetEngine.h"

#include <iphlpapi.h>
#include <string.h>

#include "BSLib/BSDebug.h"
#include "BSLib/file/FileSystem.h"
#include "BSLib/thread/System.h"
#include "BSLib/thread/Task.h"
#include "BSNet/Maintainer.h"
#include "BSNet/PeerProcess.h"
#include "BSNet/Proactor.h"
#include "BSNet/SockListener.h"
#include "BSNet/SockPool.h"
#include "BSNet/SockSystem.h"
#include "BSNet/security/Security.h"

const GUID IID_IBSNet =
    { 0xA4AF69F3, 0xC8B3, 0x4242, { 0x9A, 0x96, 0x07, 0x8F, 0xF2, 0x7E, 0xB1, 0xF8 } };

// GetPhysicalMemory (0x481930).
static void GetPhysicalMemory(SIZE_T* pdwTotal, SIZE_T* pdwAvailable)
{
    MEMORYSTATUS ms;
    GlobalMemoryStatus(&ms);
    *pdwTotal = ms.dwTotalPhys;
    *pdwAvailable = ms.dwAvailPhys;
}

// NetStat_GetCountersAndRatio (0x47fd20): open files, open users, cache limit and use, and the
// ratio of the counter at +0xA0 to the read opens, under the file system lock.
static void FileSystem_GetCounters(CFileSystem* pFS, DWORD* pdwFiles, DWORD* pdwUsers,
                                   DWORD* pdwCacheLimit, DWORD* pdwCacheUsed, float* pfRatio)
{
    CSingleLockBS lock(&pFS->m_cs, TRUE);
    *pdwFiles = (DWORD)pFS->m_mapOpenFile.size();
    *pdwUsers = pFS->m_nOpenCount;
    *pdwCacheLimit = pFS->m_dwCacheLimit;
    *pdwCacheUsed = pFS->m_dwCacheUsed;
    DWORD dwReads = (DWORD)pFS->m_nReadOpenCount;
    if (dwReads)
        *pfRatio = (float)((double)pFS->m_dwA0 / (double)dwReads);
    else
        *pfRatio = 0.0f;
}

// ---------------------------------------------------------------------------------------------
// CNetEngineDatagram

// NetEngine_RegisterGlobalInstance (0x4961d0).
CNetEngineDatagram::CNetEngineDatagram()
{
    g_pNetEngineDatagram = this;
    pSocket = NULL;
    dwTaskID = 0;
}

// j_MsgObj_CallVfn16_PushQueue_wrap (0x4961f0).
CNetEngineDatagram::~CNetEngineDatagram()
{
    Sock_ReleaseToDatagramPoolAndClear(&pSocket);
}

BOOL CNetEngineDatagram::RegisterPeerProcessClass(DWORD dwTaskIDArg)
{
    dwTaskID = dwTaskIDArg;
    CRuntimeClass* pClass = NULL;
    if (g_nNetIoModel == 0)
        pClass = &CPeerProcessIOCP::classCPeerProcessIOCP;
    else if (g_nNetIoModel == 1)
        pClass = &CPeerProcessOverlapped::classCPeerProcessOverlapped;
    return g_pNetEngine->RegisterProactor(g_dwNetDatagramTaskID, NULL, pClass, NULL) != FALSE;
}

// ---------------------------------------------------------------------------------------------
// CNetEngine

CNetEngine::CNetEngine()
{
    g_pNetEngine = this;
    m_dw4 = 0;
    m_pListener = NULL;
    g_nNetIoModel = OS_IsNotWinNT();
    Security_BuildCrcTable();
}

CNetEngine::~CNetEngine()
{
    if (m_pListener)
    {
        delete m_pListener;
        m_pListener = NULL;
    }
}

HRESULT CNetEngine::QueryInterface(const GUID& riid, void** ppv)
{
    if (memcmp(&IID_IBSNet, &riid, sizeof(GUID)) == 0)
    {
        *ppv = this;
        AddRef();
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

ULONG CNetEngine::AddRef()
{
    return 0;
}

int CNetEngine::Shutdown()
{
    if (g_pNetEngine)
    {
        g_bNetEngineShutdown = TRUE;
        if (m_pListener)
            m_pListener->Close();
        m_SessionTable.DisconnectAll();
        if (CSystem::GetInstance())
            CSystem::GetInstance()->ReleaseSubsystems();
        m_SessionTable.CloseAll();
        g_pNetEngine = NULL;
    }
    return 0;
}

BOOL CNetEngine::Initialize(BSNET_INIT_PARAM param)
{
    // BUG(NET-001): the Winsock version comes from the engine's current block, not from param.
    if (!CSystem::GetInstance()->m_pSockSystem->Initialize(m_Config.bWinsockLow,
                                                           m_Config.bWinsockHigh))
    {
        return FALSE;
    }

    memcpy(&m_Config, &param, sizeof(BSNET_INIT_PARAM));
    if (param.pfnDumpMsgPool)
        m_MsgPool.m_dw4 = (DWORD)(DWORD_PTR)param.pfnDumpMsgPool;

    CRuntimeClass* pListenClass = NULL;
    CRuntimeClass* pIoClass = NULL;
    if (g_nNetIoModel == 0)
    {
        pIoClass = &CProactorIOCP::classCProactorIOCP;
        pListenClass = &CProactorIOCP::classCProactorIOCP;
    }
    else if (g_nNetIoModel == 1)
    {
        pIoClass = &CProactorEvent::classCProactorEvent;
        pListenClass = &CProactorEvent::classCProactorEvent;
    }

    if ((param.dwFlags & BSNET_FLAG_SERVER)
        && !RegisterProactor(NETENGINE_TASK_LISTEN, NULL, pListenClass, NULL))
    {
        return FALSE;
    }
    if (!RegisterProactor(NETENGINE_TASK_IO, NULL, pIoClass, NULL))
        return FALSE;
    // The maintainer copies the configuration in its Initialize, so the address of the
    // parameter copy is enough.
    if (param.Maintainer.dwFlags
        && !RegisterProactor(NETENGINE_TASK_MAINTAINER, NULL, &CMaintainer::classCMaintainer,
                             &param.Maintainer))
    {
        return FALSE;
    }

    if (param.dwFlags & BSNET_FLAG_SERVER)
    {
        if (!m_SessionTable.Initialize((param.dwFlags & BSNET_FLAG_LARGE) ? 5000 : 50))
            return FALSE;
        InterlockedExchange((volatile LONG*)&m_AcceptPool.m_nItemsPerBlock, 5000);
        InterlockedExchange((volatile LONG*)&m_ConnectPool.m_nItemsPerBlock, 50);
        InterlockedExchange((volatile LONG*)&m_MsgPool.m_nItemsPerBlock, 3000);
    }
    else
    {
        if (!m_SessionTable.Initialize(50))
            return FALSE;
        InterlockedExchange((volatile LONG*)&m_AcceptPool.m_nItemsPerBlock, 5000);
        InterlockedExchange((volatile LONG*)&m_ConnectPool.m_nItemsPerBlock, 50);
        InterlockedExchange((volatile LONG*)&m_MsgPool.m_nItemsPerBlock, 500);
    }

    SIZE_T dwTotal = 0;
    SIZE_T dwAvailable = 0;
    GetPhysicalMemory(&dwTotal, &dwAvailable);
    if (param.dwFileCacheLimit > dwAvailable >> 1)
        param.dwFileCacheLimit = dwAvailable >> 1;
    return CFileSystem::GetInstance()->Initialize(param.dwFileCacheLimit) != FALSE;
}

DWORD CNetEngine::ConnectSockAddr(const sockaddr* pTo, const sockaddr* pBind, DWORD dwTaskID,
                                  int nKeepAlive)
{
    DWORD dwSessionID = 0;
    CSockStream* pSock = m_ConnectPool.Alloc();
    if (pSock)
    {
        if (!pSock->Connect(pBind, pTo))
        {
            Sock_ReleaseToConnectPool(pSock, this);
            dwSessionID = 0;
        }
        else
        {
            // The default task only reaches the keep-alive entry on this path.
            if (!dwTaskID)
                dwTaskID = m_Config.aSocket[SOCK_ROLE_CONNECTED].dwSessionParam;
            dwSessionID = m_SessionTable.CreateNewSession(pSock, dwTaskID);
        }
    }

    if (nKeepAlive == 1)
    {
        KEEPALIVE_ENTRY entry;
        entry.Init(pTo, pBind, dwTaskID, dwSessionID);
        AddKeepAliveEntry(&entry);
    }
    return dwSessionID;
}

DWORD CNetEngine::Connect(const char* pszHost, u_short wPort, const char* pszBindIP,
                          DWORD dwTaskID, int nKeepAlive)
{
    ULONG ulBind = ResolveHostIPv4(pszBindIP);
    ULONG ulHost = ResolveHostIPv4(pszHost);
    if (ulHost == INADDR_NONE || ulBind == INADDR_NONE)
        return 0;

    sockaddr_in to;
    sockaddr_in bind;
    memset(&to, 0, sizeof(to));
    memset(&bind, 0, sizeof(bind));
    to.sin_family = AF_INET;
    to.sin_port = htons(wPort);
    to.sin_addr.s_addr = ulHost;
    // BUG(NET-003): the resolved bind address is never stored, so no bind address is passed.
    return ConnectSockAddr((const sockaddr*)&to,
                           bind.sin_addr.s_addr ? (const sockaddr*)&bind : NULL, dwTaskID,
                           nKeepAlive);
}

BOOL CNetEngine::ListenSockAddr(const sockaddr* pAddr)
{
    if (!(m_Config.dwFlags & BSNET_FLAG_SERVER))
        return FALSE;

    if (m_pListener)
    {
        delete m_pListener;
        m_pListener = NULL;
    }
    m_pListener = new CSockListener;
    if (!m_pListener->CreateAndBind(pAddr))
    {
        if (m_pListener)
            delete m_pListener;
        m_pListener = NULL;
        return FALSE;
    }

    CTask* pTask = CSystem::GetInstance()->m_pTaskManager->FindTask(NETENGINE_TASK_LISTEN);
    return m_pListener->ListenAndPostAccepts(static_cast<CServiceObjectSock*>(pTask->GetJob()))
        != FALSE;
}

BOOL CNetEngine::Listen(const char* pszIP, u_short wPort)
{
    if (!(m_Config.dwFlags & BSNET_FLAG_SERVER))
        return FALSE;

    ULONG ulAddr = 0;
    if (pszIP)
    {
        ulAddr = ResolveHostIPv4(pszIP);
        if (ulAddr == INADDR_NONE)
            return FALSE;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(wPort);
    addr.sin_addr.s_addr = ulAddr;
    return ListenSockAddr((const sockaddr*)&addr);
}

BOOL CNetEngine::RegisterProactor(DWORD dwID, CTask* pTask, CRuntimeClass* pClass, void* pParam)
{
    BOOL bCreated = FALSE;
    if (!pTask)
    {
        bCreated = TRUE;
        pTask = new CTask;
        if (!Proactor_Create(pClass, pTask, CSystem::GetInstance()->m_pThreadManager, pParam))
        {
            if (pTask)
                delete pTask;
            return FALSE;
        }
    }

    if (!CSystem::GetInstance()->m_pTaskManager->AddTask(dwID, pTask))
    {
        if (bCreated == TRUE && pTask)
            delete pTask;
        return FALSE;
    }
    return TRUE;
}

int CNetEngine::vf09(DWORD, DWORD, DWORD, DWORD)
{
    return 0;
}

BOOL CNetEngine::StartProactorWorkers(DWORD dwID, int nCount, int nPriority,
                                      unsigned nStackSize, DWORD dwCreateFlag,
                                      THREAD_AFFINITY* pAffinity)
{
    CTask* pTask = CSystem::GetInstance()->m_pTaskManager->FindTask(dwID);
    if (!pTask)
        return FALSE;

    if (nCount <= 0)
    {
        if (g_nNetIoModel)
        {
            nCount = 1;
        }
        else
        {
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            nCount = 2 * si.dwNumberOfProcessors;
        }
    }
    if (!pTask->GetJob())
        return FALSE;
    return pTask->StartThreads(nCount, nPriority, nStackSize, dwCreateFlag, pAffinity);
}

BOOL CNetEngine::ProactorResume(DWORD dwID)
{
    CTask* pTask = CSystem::GetInstance()->m_pTaskManager->FindTask(dwID);
    if (!pTask)
        return FALSE;
    return pTask->ResumeThreads() == TRUE;
}

BOOL CNetEngine::ProactorSuspend(DWORD dwID)
{
    CTask* pTask = CSystem::GetInstance()->m_pTaskManager->FindTask(dwID);
    if (!pTask)
        return FALSE;
    return pTask->SuspendThreads() == TRUE;
}

BOOL CNetEngine::UnregisterProactor(DWORD dwID)
{
    if (!GetProactor(dwID))
        return FALSE;
    return CSystem::GetInstance()->m_pTaskManager->RemoveAndDeleteTask(dwID) != FALSE;
}

void CNetEngine::GetStatistics(DWORD dwFlags, NETENGINE_STATISTICS* pStats)
{
    DWORD dwTotal;
    DWORD dwUsed;
    DWORD dwFree;

    if (dwFlags & BSNET_STAT_MSG_POOL)
    {
        SockPool_GetCounters(&m_MsgPool, &dwTotal, &dwUsed, &dwFree);
        pStats->dwMsgTotal = dwTotal;
        pStats->dwMsgUsed = dwUsed;
        pStats->dwMsgFree = dwFree;
    }
    if (dwFlags & BSNET_STAT_SOCKET_POOL)
    {
        // BUG(NET-002): the connecting socket pool is read into the same variables and then
        // overwritten by the accepted socket pool.
        SockPool_GetCounters(&m_ConnectPool, &dwTotal, &dwUsed, &dwFree);
        SockPool_GetCounters(&m_AcceptPool, &dwTotal, &dwUsed, &dwFree);
        pStats->dwSocketTotal = dwTotal;
        pStats->dwSocketUsed = dwUsed;
        pStats->dwSocketFree = dwFree;
    }
    if (dwFlags & BSNET_STAT_SESSIONS)
    {
        m_SessionTable.GetCounters(&dwTotal, &dwUsed, &dwFree);
        pStats->dwSessionTotal = dwTotal;
        pStats->dwSessionUsed = dwUsed;
        pStats->dwSessionFree = dwFree;
    }
    if (dwFlags & BSNET_STAT_FILES)
    {
        DWORD dwFiles;
        DWORD dwUsers;
        DWORD dwLimit;
        DWORD dwCacheUsed;
        float fRatio;
        FileSystem_GetCounters(CFileSystem::GetInstance(), &dwFiles, &dwUsers, &dwLimit,
                               &dwCacheUsed, &fRatio);
        pStats->fFileCacheRatio = fRatio;
        pStats->dwOpenFiles = dwFiles;
        pStats->dwFileUsers = dwUsers;
        pStats->dwFileCacheLimit = dwLimit;
        pStats->dwFileCacheUsed = dwCacheUsed;
    }
    if ((dwFlags & BSNET_STAT_ACCEPT) && m_pListener)
    {
        pStats->dwAcceptCount = m_Config.nAcceptCount;
        pStats->dwPendingAccepts = CSockListener::ms_nPendingAccepts;
    }
    if (dwFlags & BSNET_STAT_SESSION_LIST)
    {
        // The original constructs an unused function-local CSession here (destroyed at exit).
        static CSession s_Session;
        pStats->dwLinkedSessions = CClassLink<CSession>::ms_nCount;
    }
}

BOOL CNetEngine::GetSessionPeerAddress(DWORD dwSessionID, std::string* pstrIP, ULONG* pulIP,
                                       u_short* pwPort)
{
    CSession* pSession = m_SessionTable.FindSession(dwSessionID);
    if (!pSession)
        return FALSE;
    CSockStream* pSock = pSession->m_pSocket;
    if (!pSock)
        return FALSE;

    sockaddr_in addr = pSock->m_PeerAddr;
    if (pulIP)
        *pulIP = addr.sin_addr.s_addr;
    if (pstrIP)
        pstrIP->assign(inet_ntoa(pSock->m_PeerAddr.sin_addr));
    if (pwPort)
        *pwPort = ntohs(addr.sin_port);
    return TRUE;
}

CTask* CNetEngine::GetProactor(DWORD dwID)
{
    return CSystem::GetInstance()->m_pTaskManager->FindTask(dwID);
}

BOOL CNetEngine::StartHandshake(DWORD dwSessionID, SECURITY_HANDSHAKE_PARAM* pParam)
{
    CSession* pSession = m_SessionTable.FindSession(dwSessionID);
    if (!pSession)
        return FALSE;

    BOOL bResult = pSession->StartHandshake(pParam);
    if (bResult)
        return bResult;

    CSockStream* pSock = pSession->m_pSocket;
    if (!pSock)
        return FALSE;
    BYTE bDisconnect = pSock->DisconnectReuse(6);
    if (bDisconnect)
        return bDisconnect;
    DWORD dwCloseState = pSock->m_dwCloseState;
    return DisconnectSession(pSock->GetOwnerSessionID(), dwCloseState);
}

CMsg* CNetEngine::AllocMsg(BOOL bEncrypted)
{
    CSingleLockBS lock(&m_csMsg, TRUE);
    CMsg* pMsg = m_MsgPool.Alloc();
    if (bEncrypted == TRUE)
        pMsg->SetEncryptFlag();
    InterlockedIncrement(&pMsg->m_lRefCount);
    return pMsg;
}

LONG CNetEngine::ReleaseMsg(CMsg* pMsg)
{
    CSingleLockBS lock(&m_csMsg, TRUE);
    if (!g_bNetEngineShutdown && pMsg->m_lRefCount > 0)
    {
        InterlockedDecrement(&pMsg->m_lRefCount);
        if (pMsg->m_lRefCount == 0)
            m_MsgPool.Free(pMsg);
    }
    return 0;
}

int CNetEngine::SendMsg(DWORD dwSessionID, CMsg* pMsg)
{
    pMsg->SkipRemaining();
    CSession* pSession = m_SessionTable.FindSession(dwSessionID);
    if (!pSession)
        return SOCK_ERR_NULL;

    LONG lRefCount = pMsg->m_lRefCount;
    int nResult = pSession->SendMsg(pMsg);
    if (nResult)
    {
        // The socket took a reference it will not give back.
        if (pMsg->m_lRefCount > lRefCount)
            ReleaseMsg(pMsg);
        if (nResult & 0x8000)
            DisconnectSession(pSession->m_dwID, 1);
    }
    return nResult;
}

BOOL CNetEngine::BeginSessionFileTransfer(DWORD dwSessionID, std::string strPath, DWORD dwSize,
                                          DWORD dwOffset, int nFileType)
{
    CSession* pSession = m_SessionTable.FindSession(dwSessionID);
    if (!pSession)
        return FALSE;
    if (nFileType == -1)
        nFileType = m_Config.nRecvFileType;
    return pSession->BeginFileTransfer(nFileType, strPath, dwSize, dwOffset);
}

BOOL CNetEngine::SendFile(DWORD dwSessionID, const std::string& strPath, DWORD dwOffset,
                          int nFileType)
{
    CSession* pSession = m_SessionTable.FindSession(dwSessionID);
    if (!pSession)
    {
        // "Tried to send a file but the session was not found!"
        NetEngineLog(0x2000000,
                     "\xC8\xAD\xC0\xCF\xC0\xBB \xBA\xB8\xB3\xBB\xB7\xC1 \xC7\xDF\xC1\xF6\xB8\xB8 "
                     "\xC7\xD8\xB4\xE7 Session\xC0\xBB \xC3\xA3\xC0\xBB \xBC\xF6 "
                     "\xBE\xF8\xB4\xD9!");
        return FALSE;
    }
    if (nFileType == -1)
        nFileType = m_Config.nSendFileType;
    return pSession->SendFile(dwOffset, nFileType, strPath);
}

int CNetEngine::DisconnectSession(DWORD dwSessionID, DWORD dwReason)
{
    return m_SessionTable.Close(dwSessionID, dwReason);
}

int CNetEngine::CloseSessionTransferFile(DWORD dwSessionID, DWORD dwFileHandle)
{
    if (dwSessionID)
    {
        CSession* pSession = m_SessionTable.FindSession(dwSessionID);
        if (pSession)
            return pSession->CloseTransferFile();
    }
    return CFileSystem::GetInstance()->CloseFile(dwFileHandle, dwSessionID);
}

void CNetEngine::ExcludeSessionFromCleanup(DWORD dwSessionID)
{
    CSession* pSession = m_SessionTable.FindSession(dwSessionID);
    if (pSession)
        pSession->m_bTrusted = TRUE;
}

int CNetEngine::GetSessionSocketState(DWORD dwSessionID)
{
    CSession* pSession = m_SessionTable.FindSession(dwSessionID);
    if (!pSession)
        return -1;
    if (pSession->m_pSocket)
        return pSession->m_pSocket->m_nRole;
    return -1;
}

void CNetEngine::CloseAllFiles()
{
    CFileSystem::GetInstance()->Cleanup();
}

#ifdef JMX_FRAMEWORK_2007
// FarmManager 0x464800: empty.
void CNetEngine::DumpMsgPool(BYTE, DWORD)
{
}
#else
void CNetEngine::DumpMsgPool(BYTE bFlags, DWORD)
{
    if (bFlags & 1)
        m_MsgPool.DumpEntries();
}
#endif

// The flag makes CSimpleThread::Close wait only 100 ms for a thread.
void CNetEngine::SetDebugAsserted(BYTE bAsserted)
{
    g_bBSDebugAsserted = bAsserted;
}

void CNetEngine::CleanupDisconnectedSessions()
{
    CSingleLockBS lock(&m_csKeepAlive, TRUE);
    m_SessionTable.ReapClosedSessions();
}

CMsgPool* CNetEngine::GetMsgPool()
{
    return &m_MsgPool;
}

// MAC address of the Ethernet adapter that has the session socket's local IP address.
BOOL CNetEngine::GetSessionLocalMacAddress(DWORD dwSessionID, BYTE* pMac)
{
    CSession* pSession = m_SessionTable.FindSession(dwSessionID);
    if (!pSession || !pMac || !pSession->m_pSocket)
        return FALSE;

    sockaddr_in local;
    std::string strLocalIP(inet_ntoa(pSession->m_pSocket->GetLocalAddr(&local)->sin_addr));

    IP_ADAPTER_INFO* pAdapters = NULL;
    ULONG ulSize = 0;
    if (GetAdaptersInfo(NULL, &ulSize) == ERROR_BUFFER_OVERFLOW)
    {
        // BUG(NET-004): allocates ulSize adapter records although ulSize is a byte count.
        pAdapters = new IP_ADAPTER_INFO[ulSize];
        if (!pAdapters)
            return FALSE;
        GetAdaptersInfo(pAdapters, &ulSize);
    }

    for (IP_ADAPTER_INFO* p = pAdapters; p; p = p->Next)
    {
        if (p->AddressLength != 6 || p->Type != MIB_IF_TYPE_ETHERNET)
            continue;
        for (IP_ADDR_STRING* pIP = &p->IpAddressList; pIP; pIP = pIP->Next)
        {
            if (strcmp(strLocalIP.c_str(), pIP->IpAddress.String) == 0)
            {
                memcpy(pMac, p->Address, 6);
                delete[] pAdapters;
                return TRUE;
            }
        }
    }
    delete[] pAdapters;
    return FALSE;
}

void CNetEngine::SetSecurityMode(BYTE bMode)
{
    m_Config.bSecurityMode = bMode;
}

void CNetEngine::PrintVersion()
{
    NetEngineLog(0, "netengine(ver %0.2f)", 1.54);
}

BOOL CNetEngine::RegisterDatagramProactor(DWORD dwTaskID)
{
    return m_Datagram.RegisterPeerProcessClass(dwTaskID);
}

int CNetEngine::OpenDatagramSocket(WORD wPort)
{
    return NetEngine_CreateDatagramSocket(&m_Datagram, wPort) == TRUE ? 0 : SOCK_ERR_FAILED;
}

int CNetEngine::SendDatagram(const sockaddr* pTo, CMsg* pMsg)
{
    return NetEngine_SendDatagram(&m_Datagram, pTo, pMsg);
}

// ---------------------------------------------------------------------------------------------
// Entry points of the sockets and the maintainer

void NetEngine_ReapExpiredSessions(IBSNet* pEngine, DWORD dwTimeout)
{
    NetEngine_Cast(pEngine)->m_SessionTable.ReapExpiredSessions(dwTimeout);
}

DWORD NetEngine_CreateNewSession(IBSNet* pEngine, CSockStream* pSock, DWORD dwTaskID)
{
    return NetEngine_Cast(pEngine)->m_SessionTable.CreateNewSession(pSock, dwTaskID);
}
