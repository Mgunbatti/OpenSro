#pragma once

#include <map>
#include <vector>

#include "BSLib/IDPool.h"
#include "ServerCommon/PoolObject.h"
#include "ShardManager/SocialExtern.h"

// Votes of SR_ShardManager (CVoteManager, static object 0x89a884, g_pVoteManager 0x8a8934): the
// guild master votes. Ballots come from the game servers with 0x7908; progress and results go
// to every game server as 0x3908.

enum
{
    MSG_VOTE_NOTIFY     = 0x3908,
    VOTE_RESULT_DECIDED = 0x5007,
    VOTE_KIND_GUILD_MASTER = 0,
};

// ---------------------------------------------------------------------------------------------
// Votes

// The ballot of one voter (20 bytes, pooled).
class CVoteResult
{
public:
    // CVoteResult::ctor (0x4612b0)
    CVoteResult() {}
    // CVoteResult::vf00 (0x461470)
    virtual void Release();                                                 // 0
    // 0x4643e0
    virtual ~CVoteResult() {}                                               // 1

    // The inlined reset of CVoteResult::Release (0x461440) and vf00.
    void ResetForPool()
    {
        m_dwVoteID = 0;
        m_dwCharID = 0;
        m_byChoice = 0;
    }

public:
    BYTE    m_bInUse;       // +04
    DWORD   m_dwVoteID;     // +08
    DWORD   m_dwCharID;     // +0C
    BYTE    m_byChoice;     // +10

    static CChunkAllocatorST<CVoteResult> s_Pool;
};

C_ASSERT(sizeof(CVoteResult) == 0x14);

typedef std::map<DWORD, CVoteResult*> VoteResultMap;

// A vote (0x50 bytes, pooled).
class CVote
{
public:
    CVote() {}                                                              // 0x4614b0
    // CVote::vf00 (0x4613f0)
    virtual void Release();                                                 // 0
    // 0x461570 (deleting 0x464380)
    virtual ~CVote() {}                                                     // 1
    // CVote::vf02 (0x461610): ID, kind, guild and the end tick.
    virtual void Start(DWORD dwID, BYTE byKind, DWORD dwGuildID, DWORD dwDuration);   // 2
    // CVote::vf03 (0x4616a0): -1 when the vote is over.
    virtual int Update(DWORD dwTick);                                       // 3
    // CVote::vf04 (0x4617a0)
    virtual void OnTimeout();                                               // 4
    // CVote::vf05 (0x4618e0): the ballots back to their pool.
    virtual void ClearResults();                                            // 5
    // CVote::vf06 (0x4617b0): the winner becomes guild master.
    virtual void OnDecided();                                               // 6
    // CVote::vf07 (0x461870): no winner, 0x3908 subtype 3 result 2.
    virtual void OnExpired();                                               // 7
    // CVote::vf08 (0x461990): ID, kind, guild, choice count and the choices.
    virtual void WriteToMsg(CMsg* pMsg);                                    // 8

    // CVote::Reset (0x4612d0): the ballot map is emptied without releasing the ballots.
    void Reset();
    void ResetForPool()
    {
        Reset();
    }
    // 0x461350
    CVoteResult* FindVoter(DWORD dwCharID);
    // 0x461640: a candidate with a tally of 0.
    void AddChoice(DWORD dwCharID);
    BYTE GetKind() const
    {
        return m_byKind;
    }
    // CVote::SendNotify3908 (0x4616e0): subtype 5, the remaining milliseconds.
    void SendProgress(DWORD dwTick);
    // 0x461a50: VOTE_RESULT_DECIDED once a choice has 60 percent, 1 otherwise, 2 on errors.
    WORD CastBallot(DWORD dwCharID, BYTE byChoice);

public:
    BYTE                    m_bInUse;       // +04
    DWORD                   m_dwID;         // +08
    BYTE                    m_byKind;       // +0C
    DWORD                   m_dwGuildID;    // +10
    BYTE                    m_byWinner;     // +14 0xFF while open
    DWORD                   m_dw18;         // +18
    DWORD                   m_dwEndTick;    // +1C
    DWORD                   m_dwStartTick;  // +20
    VoteResultMap           m_mapResult;    // +24 voter CharID -> ballot
    std::vector<BYTE>       m_vecTally;     // +30 votes per choice
    std::vector<DWORD>      m_vecChoice;    // +40 candidate CharIDs

    static CChunkAllocatorST<CVote> s_Pool;
};

C_ASSERT(FIELD_OFFSET(CVote, m_mapResult) == 0x24);
C_ASSERT(FIELD_OFFSET(CVote, m_vecTally) == 0x30);
C_ASSERT(sizeof(CVote) == 0x50);

typedef std::map<DWORD, CVote*> VoteMap;

class CVoteManager
{
public:
    CVoteManager();                                                         // 0x461be0
    virtual ~CVoteManager();                                                // 0 (0x461ce0)

    // 0x461d60: a guild master vote of ten minutes, NULL on failure.
    CVote* CreateVote(DWORD dwGuildID);
    // CVoteManager::FindOrEraseVote (0x461dd0)
    CVote* FindVote(DWORD dwVoteID, BYTE bErase);
    // 0x461fc0
    BOOL InsertVote(CVote* pVote);
    // 0x462120
    void RemoveVote(CVote* pVote);
    // 0x461e40 (CMainProcess::OnMsg_7908)
    void OnBallot(CMsg* pMsg);
    // 0x462030 (CMainProcess_TickVoteManager)
    void Update();

public:
    CIDPool     m_IDPool;       // +04
    VoteMap     m_mapVote;      // +24
};

C_ASSERT(FIELD_OFFSET(CVoteManager, m_mapVote) == 0x24);

// Vote_Notify3908_BallotCast (0x462150, subtype 2) / Vote_Notify3908_Result (0x4621e0, 3; the
// winner only with result 1)
void Vote_NotifyBallotCast(DWORD dwVoteID, DWORD dwCharID, BYTE byChoice);
void Vote_NotifyResult(DWORD dwVoteID, BYTE byResult, DWORD dwWinner);

// g_pVoteManager (0x8a8934)
extern CVoteManager* g_pVoteManager;
