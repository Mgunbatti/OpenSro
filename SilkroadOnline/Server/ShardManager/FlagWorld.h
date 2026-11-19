#pragma once

#include <windows.h>

#include <list>
#include <map>
#include <vector>

#include "BSLib/Util/TimeSpan.h"

class CMsg;

// The flag world event of SR_ShardManager. The game servers register participants (0x74B0)
// while the participation phase runs; at the game start the shard asks the game servers for the
// participants' levels (0x74B7 / 0xB4B7), sorts the participants by level into teams of up to
// 16 members, opens an INS_EVENT_RANDOMMATCH layer per team and tells the game servers the
// sides (0x34B1). The schedule jobs CShard_FlagWorldParticipation and CSahrd_FlagWorld drive the
// phases.

// ---------------------------------------------------------------------------------------------
// sFlagWorldTeam (20 bytes): ctor 0x669230, copy ctor 0x66c5a0, dtor 0x66a5a0

struct sFlagWorldTeam
{
    sFlagWorldTeam();

    // sFlagWorldTeam::AddMember (0x669290): the member and 0x34B1 {7, world, layer, JID}.
    void AddMember(DWORD dwJID);
    // sFlagWorldTeam::PopMember (0x669350): the first member and 0x34B1 {11, world, layer, JID}.
    DWORD PopMember();

    BYTE                m_byCount;      // +0
    BYTE                m_byGrade;      // +1   highest level bracket - 1 of the members
    WORD                m_wWorldID;     // +2
    WORD                m_wLayerID;     // +4
    std::list<DWORD>    m_listMember;   // +8   JIDs
};

C_ASSERT(FIELD_OFFSET(sFlagWorldTeam, m_listMember) == 8);
C_ASSERT(sizeof(sFlagWorldTeam) == 20);

// Arena_GetLevelBracket (0x66aa40): 0 below level 20, then 1 (20-35), 2 (36-50), 3 (51-65),
// 4 (66-80), 5 (81-90) and 6 (91 and above).
BYTE Arena_GetLevelBracket(BYTE byLevel);

// ---------------------------------------------------------------------------------------------
// CFlagWorldMgr (0x58 bytes; static object 0x8A7B68, g_pFlagWorldMgr = dword_8A8958)

class CFlagWorldMgr
{
public:
    typedef std::map<DWORD, BYTE> ParticipantMap;

    CFlagWorldMgr();                                    // CFlagWorldMgr::StaticInit 0x669420
    virtual ~CFlagWorldMgr();                           // 0x6694d0
    // CFlagWorldMgr::vf01 (0x669580): the participation state back to 0.
    virtual void ResetState();

    // Requests of the game servers.
    void OnMsg_74B0(CMsg* pMsg);                        // 0x669780
    void OnParticipantLevels(CMsg* pMsg);               // OnParticipantLevels_B4B7 0x66b090
    // CFlagWorldMgr::ForceState (0x66aaa0): the GM commands of 0x74B0 type 3.
    void ForceState(int nState);

    // The schedule jobs write into their notice; the *AndNotify forms broadcast their own.
    void BeginParticipation(CMsg* pMsg);                // 0x66a790
    void EndParticipation(CMsg* pMsg);                  // 0x66a810
    void Notify5MinBeforeGame(CMsg* pMsg);              // 0x66a850
    void Notify1MinBeforeGame(CMsg* pMsg);              // 0x66a880
    void EndGame(CMsg* pMsg);                           // 0x66a8b0
    void BeginParticipationAndNotify();                 // 0x66a920
    void EndParticipationAndNotify();                   // 0x66a9d0
    void EndGameAndNotify();                            // 0x66a600

    // CFlagWorldMgr::RequestParticipantLevels (0x66ab50): 0x74B7 with the JIDs of the online
    // participants (up to 100 a message) to every game server one of them is on.
    void RequestParticipantLevels();
    // CFlagWorldMgr::StartGame (0x669c20): teams, layers and sides.
    void StartGame();
    // CFlagWorldMgr::OpenTeamLayer (0x669590): FALSE when no layer could be opened.
    BOOL OpenTeamLayer(sFlagWorldTeam* pTeam);
    // CFlagWorldMgr::CloseTeamLayers (0x66a6a0): 0x3C15 "closed" for the open team layers, then
    // the teams are dropped.
    void CloseTeamLayers();

    std::vector<sFlagWorldTeam> m_vecTeam;              // +4
    ParticipantMap              m_mapParticipant;       // +14  JID -> value of the 0x74B0 request
    ParticipantMap              m_mapPending;           // +20  JID -> game servers yet to answer
    int                         m_nState;               // +2C  1 while the participation runs
    CDateTime                   m_dtParticipationBegin; // +30
};

C_ASSERT(FIELD_OFFSET(CFlagWorldMgr, m_mapParticipant) == 0x14);
C_ASSERT(FIELD_OFFSET(CFlagWorldMgr, m_nState) == 0x2C);
C_ASSERT(sizeof(CFlagWorldMgr) == 0x58);

extern CFlagWorldMgr* g_pFlagWorldMgr;
extern CFlagWorldMgr g_FlagWorldMgr;
