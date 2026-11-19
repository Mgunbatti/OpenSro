#include "ShardManager/Vote.h"

#include "BSNet/NetEngine.h"

CVoteManager* g_pVoteManager = NULL;

CChunkAllocatorST<CVoteResult> CVoteResult::s_Pool(NULL, 0, 500, "CVoteResult");
CChunkAllocatorST<CVote> CVote::s_Pool(NULL, 0, 500, "CVote");

// The manager is a static object (0x89a884).
static CVoteManager s_VoteManager;

namespace
{

CMsg* NewNotifyMsg(BYTE bySubType)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(MSG_VOTE_NOTIFY);
    *pMsg << bySubType;
    return pMsg;
}

// BroadcastToGameServers releases the message.
void Broadcast(CMsg* pMsg)
{
    if (!g_pMainProcess)
        BSAssertFail();
    g_pMainProcess->BroadcastToGameServers(pMsg, 0);
}

CGuildCommon* FindGuild(DWORD dwGuildID)
{
    if (!g_pGuildManager)
        BSAssertFail();
    return g_pGuildManager->FindGuild(dwGuildID);
}

}

void Vote_NotifyBallotCast(DWORD dwVoteID, DWORD dwCharID, BYTE byChoice)
{
    CMsg* pMsg = NewNotifyMsg(2);
    *pMsg << dwVoteID << dwCharID << byChoice;
    Broadcast(pMsg);
}

void Vote_NotifyResult(DWORD dwVoteID, BYTE byResult, DWORD dwWinner)
{
    CMsg* pMsg = NewNotifyMsg(3);
    *pMsg << dwVoteID << byResult;
    if (byResult == 1)
        *pMsg << dwWinner;
    Broadcast(pMsg);
}

// ---------------------------------------------------------------------------------------------
// CVoteResult

void CVoteResult::Release()
{
    PoolObject_Release(this, "CVoteResult");
}

// ---------------------------------------------------------------------------------------------
// CVote

void CVote::Release()
{
    PoolObject_Release(this, "CVote");
}

void CVote::Reset()
{
    m_dwID = 0;
    m_byKind = 0;
    m_dwGuildID = 0;
    m_dw18 = 0;
    m_dwEndTick = 0;
    m_dwStartTick = 0;
    m_byWinner = 0xFF;
    m_mapResult.clear();
    // BUG(SM-277): the tally is kept, a reused vote starts with the counts of the last one.
    m_vecChoice.clear();
}

CVoteResult* CVote::FindVoter(DWORD dwCharID)
{
    VoteResultMap::iterator it = m_mapResult.find(dwCharID);
    if (it == m_mapResult.end())
        return NULL;
    return it->second;
}

void CVote::Start(DWORD dwID, BYTE byKind, DWORD dwGuildID, DWORD dwDuration)
{
    m_dwID = dwID;
    m_byKind = byKind;
    m_dwGuildID = dwGuildID;
    DWORD dwTick = GetTickCount();
    m_dwStartTick = dwTick;
    m_dwEndTick = dwDuration + dwTick;
}

void CVote::AddChoice(DWORD dwCharID)
{
    if (m_vecChoice.size() != m_vecTally.size())
        BSAssertFail();
    m_vecChoice.push_back(dwCharID);
    m_vecTally.push_back(0);
}

int CVote::Update(DWORD dwTick)
{
    // Once a minute has passed since the start, every tick sends the remaining time.
    if (dwTick - m_dwStartTick > 60000)
        SendProgress(dwTick);
    if (dwTick < m_dwEndTick)
        return 0;
    OnTimeout();
    return -1;
}

void CVote::SendProgress(DWORD dwTick)
{
    CMsg* pMsg = NewNotifyMsg(5);
    DWORD dwLeft = m_dwEndTick > dwTick ? m_dwEndTick - dwTick : 0;
    *pMsg << m_dwID << dwLeft;
    Broadcast(pMsg);
}

void CVote::OnTimeout()
{
    OnExpired();
}

void CVote::ClearResults()
{
    for (VoteResultMap::iterator it = m_mapResult.begin(); it != m_mapResult.end(); ++it)
    {
        CVoteResult* pResult = it->second;
        PoolObject_FreePtr(pResult);
    }
    m_mapResult.clear();
}

void CVote::OnDecided()
{
    if (m_byKind != VOTE_KIND_GUILD_MASTER)
    {
        BSAssertFail();
        return;
    }
    CGuild* pGuild = (CGuild*)FindGuild(m_dwGuildID);
    if (!pGuild)
        return;
    pGuild->RemoveMasterCandidate(m_dwID);
    DWORD dwOldMaster = pGuild->GetMasterCharID();
    DWORD dwWinner = m_vecChoice.at(m_byWinner);
    if (dwOldMaster == dwWinner)
        return;
    CGuildMember* pOld = pGuild->FindMember(dwOldMaster);
    CGuildMember* pNew = pGuild->FindMember(dwWinner);
    if (!pOld || !pNew)
        return;
    if (pGuild->DelegateMaster(pNew->GetRecord(), pOld->GetRecord()) == 1)
        GetSingleton_CGuildManager()->SendMasterDelegated(pGuild->GetID(), dwWinner, dwOldMaster);
}

void CVote::OnExpired()
{
    if (m_byKind != VOTE_KIND_GUILD_MASTER)
    {
        BSAssertFail();
        return;
    }
    CGuildCommon* pGuild = FindGuild(m_dwGuildID);
    if (!pGuild)
        return;
    pGuild->RemoveMasterCandidate(m_dwID);
    if (!g_pVoteManager)
        BSAssertFail();
    Vote_NotifyResult(m_dwID, 2, 0);
}

void CVote::WriteToMsg(CMsg* pMsg)
{
    *pMsg << m_dwID << m_byKind << m_dwGuildID << (BYTE)m_vecChoice.size();
    for (std::vector<DWORD>::iterator it = m_vecChoice.begin(); it != m_vecChoice.end(); ++it)
        *pMsg << *it;
}

WORD CVote::CastBallot(DWORD dwCharID, BYTE byChoice)
{
    CVoteResult* pResult = FindVoter(dwCharID);
    if (pResult)
    {
        BYTE byOld = pResult->m_byChoice;
        pResult->m_byChoice = byChoice;
        if (m_vecTally.at(byOld) == 0)
            BSAssertFail();
        if (m_vecTally.at(byChoice) >= 0xFF)
            BSAssertFail();
        --m_vecTally.at(byOld);
    }
    else
    {
        pResult = PoolObject_Alloc<CVoteResult>();
        pResult->m_dwVoteID = m_dwID;
        pResult->m_dwCharID = dwCharID;
        pResult->m_byChoice = byChoice;
        m_mapResult.insert(VoteResultMap::value_type(dwCharID, pResult));
    }
    ++m_vecTally.at(byChoice);

    if (m_byKind != VOTE_KIND_GUILD_MASTER || !FindGuild(m_dwGuildID))
    {
        BSAssertFail();
        return 2;
    }
    // The share is taken of the number of choices, not of the ballots.
    int nVotes = m_vecTally.at(byChoice);
    DWORD dwChoices = (DWORD)m_vecChoice.size();
    float fPercent = (float)((double)nVotes * 100.0 / (double)dwChoices);
    if (fPercent < 60.0)
        return 1;
    m_byWinner = byChoice;
    return VOTE_RESULT_DECIDED;
}

// ---------------------------------------------------------------------------------------------
// CVoteManager

CVoteManager::CVoteManager()
{
    if (g_pVoteManager)
        BSAssertFail();
    g_pVoteManager = this;
}

CVoteManager::~CVoteManager()
{
    g_pVoteManager = NULL;
}

CVote* CVoteManager::CreateVote(DWORD dwGuildID)
{
    CVote* pVote = PoolObject_Alloc<CVote>();
    pVote->Reset();
    DWORD dwID = m_IDPool.Alloc();
    if (!dwID)
    {
        BSAssertFail();
        return NULL;
    }
    pVote->Start(dwID, VOTE_KIND_GUILD_MASTER, dwGuildID, 600000);
    if (!InsertVote(pVote))
    {
        BSAssertFail();
        PoolObject_FreePtr(pVote);
        return NULL;
    }
    return pVote;
}

CVote* CVoteManager::FindVote(DWORD dwVoteID, BYTE bErase)
{
    VoteMap::iterator it = m_mapVote.find(dwVoteID);
    if (it == m_mapVote.end())
        return NULL;
    CVote* pVote = it->second;
    if (bErase == 1)
        m_mapVote.erase(it);
    return pVote;
}

BOOL CVoteManager::InsertVote(CVote* pVote)
{
    if (m_mapVote.find(pVote->m_dwID) != m_mapVote.end())
    {
        BSAssertFail();
        return FALSE;
    }
    m_mapVote.insert(VoteMap::value_type(pVote->m_dwID, pVote));
    return TRUE;
}

void CVoteManager::RemoveVote(CVote* pVote)
{
    FindVote(pVote->m_dwID, 1);
    PoolObject_FreePtr(pVote);
}

void CVoteManager::OnBallot(CMsg* pMsg)
{
    BYTE bySubType;
    *pMsg >> bySubType;
    if (bySubType != 2)
    {
        BSAssertFail();
        return;
    }
    DWORD dwVoteID, dwCharID;
    BYTE byChoice;
    *pMsg >> dwVoteID >> byChoice >> dwCharID >> byChoice;
    CVote* pVote = FindVote(dwVoteID, 0);
    if (!pVote)
    {
        BSAssertFail();
        return;
    }
    if (pVote->CastBallot(dwCharID, byChoice) != VOTE_RESULT_DECIDED)
    {
        Vote_NotifyBallotCast(pVote->m_dwID, dwCharID, byChoice);
        return;
    }
    Vote_NotifyResult(pVote->m_dwID, 1, pVote->m_vecChoice.at(byChoice));
    pVote->OnDecided();
    RemoveVote(pVote);
}

void CVoteManager::Update()
{
    DWORD dwTick = GetTickCount();
    VoteMap::iterator it = m_mapVote.begin();
    while (it != m_mapVote.end())
    {
        CVote* pVote = it->second;
        if (pVote->Update(dwTick) >= 0)
        {
            ++it;
            continue;
        }
        ++it;
        if (pVote->m_dwID)
            m_IDPool.Free(pVote->m_dwID);
        RemoveVote(pVote);
    }
}
