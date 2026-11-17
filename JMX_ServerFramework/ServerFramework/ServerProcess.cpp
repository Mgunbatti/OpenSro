#include "ServerProcess.h"

#include "BSNet/QueMT.h"
#include "ServerFramework/CrashDump.h"
#include "ServerFramework/ServerApp.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerDataExtern.h"

namespace ServerFramework
{

BS_IMPLEMENT_DYNCREATE(CServerProcessBase, &CServiceObject::classCServiceObject)
BS_IMPLEMENT_DYNCREATE(CServerProcessOverlap, &CServerProcessBase::classCServerProcessBase)

static SERVER_CORD* FindSessionCord(DWORD dwSessionID)
{
    std::map<DWORD, SERVER_CORD*>::iterator it = g_mapSessionCord.find(dwSessionID);
    if (it == g_mapSessionCord.end())
        return NULL;
    return it->second;
}

// The body at the other end of a cord seen from this server.
static WORD CordPeerBody(SERVER_CORD* pCord)
{
    if (pCord->wToID == g_pSelfBody->wID)
        return pCord->wFromID;
    return pCord->wToID;
}

void CheckMsgFullyConsumed(CMsg* pMsg)
{
    if (pMsg->m_lRefCount == 1 && pMsg->m_wWritePos != pMsg->m_wReadPos)
    {
        char szDesc[256];
        LogPrintf(0x1000000, "msg is not completely used : %s", pMsg->FormatDebugString(szDesc));
        pMsg->SkipRemaining();
    }
}

// ---------------------------------------------------------------------------------------------
// CServerProcessBase

CServerProcessBase::CServerProcessBase()
{
    m_bUseTimer = FALSE;
    m_bUseOverlapJob = FALSE;
    m_pOverlapTask = NULL;
    m_bRunAfterStop = FALSE;
}

CServerProcessBase::~CServerProcessBase()
{
}

BOOL CServerProcessBase::Initialize(void* pOwner, void* pParam)
{
    m_pParam = pParam;
    m_pOwner = pOwner;
    PFN_MSG_HANDLER* pHandler = m_aHandler;
    for (int i = 0; i < 0x10000; ++i)
        pHandler[i] = &CServerProcessBase::OnUnhandledMsg;
    InitMsgHandlers();

    if (m_bUseTimer)
    {
        CreateTimerQueueTimer(&m_hTimer, NULL, &CServerProcessBase::TimerCallback, this, 1000,
                              1000, 0);
    }
    if (m_bUseOverlapJob)
    {
        m_OverlapJobManager.SetOwner((CTask*)pOwner);
        RegisterOverlapTimer();
        m_pOverlapTask = g_pBSNet->GetProactor(SERVER_TASK_OVERLAP);
        if (!m_pOverlapTask)
            BSAssertFail();
    }
    return TRUE;
}

// The structured exception handler cannot live in a function with C++ objects to unwind.
static void DispatchProtected(CServerProcessBase* pProcess, CMsg* pMsg, int nIndex)
{
    __try
    {
        if (!nIndex)
            BSAssertFail();
        pMsg->m_pOwner = (void*)(INT_PTR)nIndex;
        pProcess->Dispatch(pMsg, 0, 0, NULL);
        CheckMsgFullyConsumed(pMsg);
        g_pBSNet->ReleaseMsg(pMsg);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (g_pBSNet && pMsg->m_dwSessionID)
            g_pBSNet->DisconnectSession(pMsg->m_dwSessionID, 1);
        pMsg->SkipRemaining();
        g_pBSNet->ReleaseMsg(pMsg);
    }
}

DWORD CServerProcessBase::Run(volatile LONG* pbStop, int nIndex)
{
    if (*pbStop)
        return 0;

    CTask* pTask = (CTask*)m_pOwner;
    while (!*pbStop || m_bRunAfterStop == 1)
    {
        CMsg* pMsg = NULL;
        if (!pTask->m_pQueue->Pop(&pMsg, INFINITE, FALSE))
            break;
        if (pMsg)
            DispatchProtected(this, pMsg, nIndex);
    }
    BS_THREAD_TRACE(GetRuntimeClass()->m_lpszClassName);
    return 0;
}

int CServerProcessBase::SignalStop()
{
    if (m_bUseTimer)
        ClearTimers();
    if (m_bUseOverlapJob)
    {
        m_OverlapJobManager.ReleaseAll();
        m_bUseOverlapJob = FALSE;
    }
    return 0;
}

void CServerProcessBase::InitMsgHandlers()
{
    RegisterHandler(SERVER_MSG_TIMER_TICK, &CServerProcessBase::OnTimerTick);
    RegisterHandler(SERVER_MSG_RELAY_ACK, &CServerProcessBase::OnMsg_A008);
    RegisterHandler(SERVER_MSG_RELAY_REQ, &CServerProcessBase::OnMsg_6008);
    RegisterHandler(SERVER_MSG_OVERLAP_JOB_ACK, &CServerProcessBase::OnMsg_A009);
}

void CServerProcessBase::RegisterHandler(WORD wOpcode, PFN_MSG_HANDLER pfnHandler)
{
    m_aHandler[wOpcode] = pfnHandler;
}

void CServerProcessBase::Dispatch(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                  CMassiveMsg* pMassive)
{
    if (pMassive)
        (this->*m_aHandler[pMassive->m_wOpcode])(NULL, dwRelayID, dwContext, pMassive);
    else
        (this->*m_aHandler[pMsg->GetOpcode()])(pMsg, dwRelayID, dwContext, NULL);
}

void CServerProcessBase::OnUnhandledMsg(CMsg* pMsg, DWORD, DWORD, CMassiveMsg* pMassive)
{
    char szDesc[256];
    if (pMsg)
    {
        LogPrintf(0x1000000, "unhandled msg received %s : %s",
                  GetRuntimeClass()->m_lpszClassName, pMsg->FormatDebugString(szDesc));
        pMsg->SkipRemaining();
    }
    if (pMassive)
    {
        LogPrintf(0x1000000, "unhandled msg received %s : %s",
                  GetRuntimeClass()->m_lpszClassName, pMassive->FormatDesc(szDesc));
        pMassive->ReleaseMsgs();
    }
}

VOID CALLBACK CServerProcessBase::TimerCallback(PVOID pParam, BOOLEAN)
{
    static_cast<CServerProcessBase*>(pParam)->PostTimerTick();
}

BOOL CServerProcessBase::PostTimerTick()
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(SERVER_MSG_TIMER_TICK);
    return ((CTask*)m_pOwner)->m_pQueue->Push(pMsg);
}

void CServerProcessBase::OnTimerTick(CMsg*, DWORD, DWORD, CMassiveMsg*)
{
    ServerProcessTimerMap::iterator it = m_mapTimer.begin();
    while (it != m_mapTimer.end())
    {
        SERVER_PROCESS_TIMER& timer = it->second;
        if (GetTickCount() - timer.dwLastTick >= timer.dwPeriod)
        {
            timer.dwLastTick = GetTickCount();
            if (!(this->*timer.pfnTimer)(it->first))
            {
                m_mapTimer.erase(it++);
                continue;
            }
        }
        ++it;
    }
}

void CServerProcessBase::RegisterOverlapTimer()
{
    SERVER_PROCESS_TIMER timer;
    timer.dwLastTick = GetTickCount();
    timer.dwPeriod = 5000;
    timer.pfnTimer = &CServerProcessBase::OnOverlapTimer;
    m_mapTimer.insert(ServerProcessTimerMap::value_type(SERVER_PROCESS_TIMER_OVERLAP, timer));
}

void CServerProcessBase::SetTimerPeriod(BYTE byTimerID, DWORD dwPeriod)
{
    ServerProcessTimerMap::iterator it = m_mapTimer.find(byTimerID);
    if (it != m_mapTimer.end())
        it->second.dwPeriod = dwPeriod;
}

void CServerProcessBase::EraseTimer(BYTE byTimerID)
{
    ServerProcessTimerMap::iterator it = m_mapTimer.find(byTimerID);
    if (it != m_mapTimer.end())
        m_mapTimer.erase(it);
}

void CServerProcessBase::ClearTimers()
{
    if (m_mapTimer.size())
    {
        m_mapTimer.clear();
        DeleteTimerQueueTimer(NULL, m_hTimer, NULL);
    }
}

bool CServerProcessBase::OnOverlapTimer(BYTE)
{
    return true;
}

static void SendRelayFailure(DWORD dwSessionID, DWORD dwRelayID, WORD wOpcode)
{
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(SERVER_MSG_RELAY_ACK);
    BYTE bResult = SERVER_RELAY_RESULT_FAILED;
    pReply->WriteBytes(&bResult, 1);
    pReply->WriteBytes(&dwRelayID, 4);
    pReply->WriteBytes(&wOpcode, 2);
    g_pBSNet->SendMsg(dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
}

void CServerProcessBase::OnMsg_6008(CMsg* pMsg, DWORD, DWORD, CMassiveMsg* pMassive)
{
    DWORD dwRelayID;
    WORD wTarget;
    WORD wOpcode;

    if (pMsg)
    {
        *pMsg >> dwRelayID >> wTarget >> wOpcode;
        pMsg->SetOpcode(wOpcode);
        SERVER_CORD* pCord = FindSessionCord(pMsg->m_dwSessionID);
        if (!pCord)
        {
            pMsg->SkipRemaining();
            return;
        }
        WORD wSource = CordPeerBody(pCord);
        if (wTarget == g_pSelfBody->wID)
        {
            Dispatch(pMsg, dwRelayID, 0, NULL);
            return;
        }
        InterlockedIncrement(&pMsg->m_lRefCount);
        if (RelayMsgToBody_6008(this, pMsg, wTarget, wSource, dwRelayID, 0))
            return;
        SendRelayFailure(pMsg->m_dwSessionID, dwRelayID, wOpcode);
        return;
    }

    if (!pMassive)
    {
        BSAssertFail();
        return;
    }
    pMassive->Read(&dwRelayID, 4);
    pMassive->Read(&wTarget, 2);
    pMassive->Read(&wOpcode, 2);
    pMassive->SetOpcode(wOpcode);
    SERVER_CORD* pCord = FindSessionCord(pMassive->m_dw36);
    if (!pCord)
        return;
    WORD wSource = CordPeerBody(pCord);
    if (wTarget == g_pSelfBody->wID)
    {
        Dispatch(NULL, dwRelayID, 0, pMassive);
        return;
    }
    CMassiveMsg* pCopy = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pCopy->SetOpcode(wOpcode);
    if (pCopy->AppendFrom(pMassive, TRUE))
    {
        if (RelayMassiveMsgToBody_6008(pCopy, this, wTarget, wSource, dwRelayID))
            return;
    }
    else
    {
        pCopy->Free();
    }
    SendRelayFailure(pMassive->m_dw36, dwRelayID, wOpcode);
}

void CServerProcessBase::OnMsg_A008(CMsg* pMsg, DWORD, DWORD, CMassiveMsg* pMassive)
{
    BYTE bResult;
    DWORD dwRelayID;
    WORD wOpcode;

    if (pMsg)
    {
        *pMsg >> bResult >> dwRelayID >> wOpcode;
        COverlapJob_RelayMsg* pJob =
            static_cast<COverlapJob_RelayMsg*>(m_OverlapJobManager.Remove(dwRelayID));
        if (!pJob)
        {
            pMsg->SkipRemaining();
            return;
        }
        WORD wSource = pJob->m_wSourceBody;
        if (wSource == g_pSelfBody->wID || !wSource)
        {
            if (bResult == SERVER_RELAY_RESULT_OK)
            {
                pMsg->SetOpcode(wOpcode);
                Dispatch(pMsg, dwRelayID, pJob->m_dwContext, NULL);
            }
            else
            {
                OnRelayFailed(wOpcode, pJob->m_dwContext);
            }
        }
        else
        {
            CMsg* pForward = g_pBSNet->AllocMsg(FALSE);
            pForward->SetOpcode(SERVER_MSG_RELAY_ACK);
            DWORD dwSourceJobID = pJob->m_dwSourceJobID;
            pForward->WriteBytes(&bResult, 1);
            pForward->WriteBytes(&dwSourceJobID, 4);
            pForward->WriteBytes(&wOpcode, 2);
            pForward->WriteBytes(pMsg->m_pData + pMsg->m_wReadPos, pMsg->GetUnreadSize());
            pMsg->SkipRemaining();
            if (!ServerBody_SendMsg(wSource, pForward))
                g_pBSNet->ReleaseMsg(pForward);
        }
        pJob->ReturnToPool();
        return;
    }

    if (!pMassive)
    {
        BSAssertFail();
        return;
    }
    pMassive->Read(&bResult, 1);
    pMassive->Read(&dwRelayID, 4);
    pMassive->Read(&wOpcode, 2);
    COverlapJob_RelayMsg* pJob =
        static_cast<COverlapJob_RelayMsg*>(m_OverlapJobManager.Remove(dwRelayID));
    if (!pJob)
        return;
    WORD wSource = pJob->m_wSourceBody;
    if (wSource == g_pSelfBody->wID || !wSource)
    {
        if (bResult == SERVER_RELAY_RESULT_OK)
        {
            pMassive->SetOpcode(wOpcode);
            Dispatch(NULL, dwRelayID, pJob->m_dwContext, pMassive);
        }
        else
        {
            OnRelayFailed(wOpcode, pJob->m_dwContext);
        }
        pJob->ReturnToPool();
        return;
    }

    CMassiveMsg* pForward = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pForward->SetOpcode(SERVER_MSG_RELAY_ACK);
    DWORD dwSourceJobID = pJob->m_dwSourceJobID;
    pForward->Write(&bResult, 1);
    pForward->Write(&dwSourceJobID, 4);
    pForward->Write(&wOpcode, 2);
    if (pForward->AppendFrom(pMassive, TRUE))
    {
        SERVER_CORD* pCord = ServerBody_Find(wSource);
        if (pCord && pCord->dwSessionID)
            pForward->SendTo(pCord->dwSessionID, FALSE);
        else
            pForward->Free();
        pJob->ReturnToPool();
        return;
    }
    pForward->Free();
    SERVER_CORD* pCord = ServerBody_Find(wSource);
    if (pCord && pCord->dwSessionID)
    {
        CMsg* pFail = g_pBSNet->AllocMsg(FALSE);
        pFail->SetOpcode(SERVER_MSG_RELAY_ACK);
        bResult = SERVER_RELAY_RESULT_FAILED;
        pFail->WriteBytes(&bResult, 1);
        pFail->WriteBytes(&dwSourceJobID, 4);
        pFail->WriteBytes(&wOpcode, 2);
        g_pBSNet->SendMsg(pCord->dwSessionID, pFail);
        g_pBSNet->ReleaseMsg(pFail);
    }
    pJob->ReturnToPool();
}

void CServerProcessBase::OnMsg_A009(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwID;
    *pMsg >> dwID;
    COverlapJob_Local* pJob = static_cast<COverlapJob_Local*>(m_OverlapJobManager.Remove(dwID));
    if (!pJob)
    {
        LogPrintf(0x2000000, "overlapjob local ack : pOverlapJob is NULL");
        return;
    }
    if (pJob->m_bTimedOut || pJob->m_nState != OVERLAP_JOB_STATE_DONE)
        pJob->OnFail();
    else
        pJob->OnComplete();
    pJob->ReturnToPool();
}

void CServerProcessBase::SendRelayReplyMassive(DWORD dwSessionID, DWORD dwRelayID,
                                               CMassiveMsg* pReply)
{
    CMassiveMsg* pAck = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pAck->SetOpcode(SERVER_MSG_RELAY_ACK);
    WORD wOpcode = pReply->m_wOpcode;
    BYTE bResult = SERVER_RELAY_RESULT_OK;
    pAck->Write(&bResult, 1);
    pAck->Write(&dwRelayID, 4);
    pAck->Write(&wOpcode, 2);
    if (pAck->AppendFrom(pReply, FALSE))
    {
        pAck->SendTo(dwSessionID, FALSE);
        pReply->Free();
        return;
    }
    pAck->Free();
    pReply->Free();
    CMsg* pFail = g_pBSNet->AllocMsg(FALSE);
    pFail->SetOpcode(SERVER_MSG_RELAY_ACK);
    wOpcode = pReply->m_wOpcode;
    bResult = SERVER_RELAY_RESULT_FAILED;
    pFail->WriteBytes(&bResult, 1);
    pFail->WriteBytes(&dwRelayID, 4);
    pFail->WriteBytes(&wOpcode, 2);
    g_pBSNet->SendMsg(dwSessionID, pFail);
    g_pBSNet->ReleaseMsg(pFail);
}

LONG CServerProcessBase::SendRelayReply(DWORD dwSessionID, DWORD dwRelayID, CMsg* pReply)
{
    CMsg* pAck = g_pBSNet->AllocMsg(FALSE);
    pAck->SetOpcode(SERVER_MSG_RELAY_ACK);
    WORD wOpcode = pReply->GetOpcode();
    BYTE bResult = SERVER_RELAY_RESULT_OK;
    pAck->WriteBytes(&bResult, 1);
    pAck->WriteBytes(&dwRelayID, 4);
    pAck->WriteBytes(&wOpcode, 2);
    pAck->WriteBytes(pReply->m_pData + pReply->m_wReadPos, pReply->GetUnreadSize());
    pReply->SkipRemaining();
    g_pBSNet->ReleaseMsg(pReply);
    g_pBSNet->SendMsg(dwSessionID, pAck);
    return g_pBSNet->ReleaseMsg(pAck);
}

void CServerProcessBase::OnRelayFailed(WORD, DWORD)
{
}

int CServerProcessBase::BroadcastMsg(CMsg* pMsg, std::list<DWORD>* pSessions)
{
    int nFailed = 0;
    for (std::list<DWORD>::iterator it = pSessions->begin(); it != pSessions->end(); ++it)
    {
        if (!g_pBSNet->SendMsg(*it, pMsg))
            ++nFailed;
    }
    g_pBSNet->ReleaseMsg(pMsg);
    return nFailed;
}

// BUG(FWAPP-006): when the queue does not take the message, the job is released but the 0x6009
// message is never returned to the pool.
bool CServerProcessBase::SendOverlapJob(COverlapJob_Local* pJob, CTask* pTask)
{
    if (!pJob->m_pOwner)
    {
        BSAssertFail();
        return false;
    }
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(SERVER_MSG_OVERLAP_JOB_REQ);
    COverlapJob_Local* pPointer = pJob;
    pMsg->WriteBytes(&pPointer, 4);
    m_OverlapJobManager.Insert(pJob);
    if (!pTask)
        pTask = m_pOverlapTask;
    if (!pTask->m_pQueue->Push(pMsg))
    {
        m_OverlapJobManager.RemoveAndRelease(pJob);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Relays

bool RelayMsgToBody_6008(CServerProcessBase* pProcess, CMsg* pMsg, WORD wTargetBody,
                         WORD wSourceBody, DWORD dwSourceJobID, DWORD dwContext)
{
    SERVER_CORD* pCord = ServerBody_Find(wTargetBody);
    if (!pCord || !pCord->dwSessionID)
    {
        pMsg->SkipRemaining();
        g_pBSNet->ReleaseMsg(pMsg);
        return false;
    }

    COverlapJob_RelayMsg* pJob = OverlapJob_AllocRelayMsg();
    pJob->Setup((CTask*)pProcess->m_pOwner, wTargetBody, pMsg->GetOpcode(), wSourceBody,
                dwSourceJobID, dwContext);
    COverlapJobManager* pManager = &pProcess->m_OverlapJobManager;
    pManager->Insert(pJob);

    CMsg* pRelay = g_pBSNet->AllocMsg(FALSE);
    pRelay->SetOpcode(SERVER_MSG_RELAY_REQ);
    DWORD dwRelayID = pJob->m_dwID;
    WORD wOpcode = pMsg->GetOpcode();
    pRelay->WriteBytes(&dwRelayID, 4);
    pRelay->WriteBytes(&wTargetBody, 2);
    pRelay->WriteBytes(&wOpcode, 2);
    pRelay->WriteBytes(pMsg->m_pData + pMsg->m_wReadPos, pMsg->GetUnreadSize());
    if (!ServerBody_SendMsg(wTargetBody, pRelay))
    {
        g_pBSNet->ReleaseMsg(pRelay);
        pMsg->SkipRemaining();
        g_pBSNet->ReleaseMsg(pMsg);
        pManager->RemoveAndRelease(pJob);
        return false;
    }
    pMsg->SkipRemaining();
    g_pBSNet->ReleaseMsg(pMsg);
    return true;
}

// BUG(FWAPP-007): when the data cannot be copied, the relay job stays registered.
bool RelayMassiveMsgToBody_6008(CMassiveMsg* pMassive, CServerProcessBase* pProcess,
                                WORD wTargetBody, WORD wSourceBody, DWORD dwSourceJobID)
{
    SERVER_CORD* pCord = ServerBody_Find(wTargetBody);
    if (!pCord || !pCord->dwSessionID)
    {
        LogPrintf(0x2000000, "FindServerCordToRouteServer Failed !!! (%d -> %d)", wSourceBody,
                  wTargetBody);
        pMassive->Free();
        return false;
    }

    COverlapJob_RelayMsg* pJob = OverlapJob_AllocRelayMsg();
    pJob->Setup((CTask*)pProcess->m_pOwner, wTargetBody, pMassive->m_wOpcode, wSourceBody,
                dwSourceJobID, 0);
    pProcess->m_OverlapJobManager.Insert(pJob);

    CMassiveMsg* pRelay = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pRelay->SetOpcode(SERVER_MSG_RELAY_REQ);
    DWORD dwRelayID = pJob->m_dwID;
    WORD wOpcode = pMassive->m_wOpcode;
    pRelay->Write(&dwRelayID, 4);
    pRelay->Write(&wTargetBody, 2);
    pRelay->Write(&wOpcode, 2);
    if (pRelay->AppendFrom(pMassive, FALSE))
    {
        pRelay->SendTo(pCord->dwSessionID, FALSE);
        pMassive->Free();
        return true;
    }
    pMassive->Free();
    pRelay->Free();
    return false;
}

// ---------------------------------------------------------------------------------------------
// CServerProcessOverlap

void CServerProcessOverlap::InitMsgHandlers()
{
    CServerProcessBase::InitMsgHandlers();
    RegisterHandler(SERVER_MSG_OVERLAP_JOB_REQ, &CServerProcessOverlap::OnMsg_6009);
    m_bRunAfterStop = TRUE;
}

static void PostOverlapJobAck(COverlapJob_Local* pJob)
{
    CMsg* pAck = g_pBSNet->AllocMsg(FALSE);
    pAck->SetOpcode(SERVER_MSG_OVERLAP_JOB_ACK);
    DWORD dwID = pJob->m_dwID;
    pAck->WriteBytes(&dwID, 4);
    pJob->m_pOwner->m_pQueue->Push(pAck);
}

static int OverlapJobExceptionFilter(EXCEPTION_POINTERS* pExceptionInfo)
{
    CrashDump_TopLevelExceptionFilter(pExceptionInfo);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RunOverlapJob(CServerProcessOverlap* pProcess, COverlapJob_Local* pJob)
{
    __try
    {
        InterlockedExchange(&pJob->m_nState, OVERLAP_JOB_STATE_PREPARE);
        if (pJob->Prepare())
        {
            InterlockedExchange(&pJob->m_nState, OVERLAP_JOB_STATE_EXECUTE);
            if (pJob->Execute())
                InterlockedExchange(&pJob->m_nState, OVERLAP_JOB_STATE_DONE);
        }
        PostOverlapJobAck(pJob);
    }
    __except (OverlapJobExceptionFilter(GetExceptionInformation()))
    {
        LogPrintf(0x2000000, "local overlap job exception!! : %s",
                  pProcess->GetRuntimeClass()->m_lpszClassName);
        if (pJob->m_pOwner)
            PostOverlapJobAck(pJob);
        else
            LogPrintf(0x2000000, "local overlap job's owner task is null !!");
    }
}

void CServerProcessOverlap::OnMsg_6009(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    COverlapJob_Local* pJob;
    *pMsg >> pJob;
    if (!pJob)
    {
        LogPrintf(0x2000000, "local overlap job pointer is null !!!");
        return;
    }
    if (!pJob->m_pOwner)
    {
        LogPrintf(0x2000000,
                  "local overlap job's owner task is null !!!! overlap job may be leak");
        return;
    }
    RunOverlapJob(this, pJob);
}

} // namespace ServerFramework
