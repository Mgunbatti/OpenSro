#pragma once

#include <windows.h>

#include <list>
#include <map>
#include <vector>

#include "BSLib/Util/TimeSpan.h"
#include "ServerCommon/RefRecords.h"

class CMsg;

// The battle arena of SR_ShardManager. Four arena kinds (random, party, guild, job), each played
// in three sizes (index 0..2: 8, 16 and 32 players) and one of three match types (0 occupy,
// 1 score, 2 flag). The game servers register the players (0x74D0); the schedule jobs open and
// close the participation, check the registered players (0x74D1 / 0xB4D1), build the teams, open
// the arena layers and tell the game servers (0x34D2).

enum
{
    BATTLEARENA_KIND_COUNT  = 4,    // 0 random, 1 party, 2 guild, 3 job
    BATTLEARENA_SIZE_COUNT  = 3     // 8, 16, 32 players
};

// A player of the team building: the JID (or party / guild ID) and the level. The list nodes
// hold the two dwords like the original's 64-bit value.
struct sArenaLevelEntry
{
    DWORD   dwID;
    DWORD   dwLevel;
};

// ---------------------------------------------------------------------------------------------
// sBattleArenaTeam (28 bytes): ctor 0x66d970, copy ctor 0x677390, Assign 0x672cc0, dtor 0x672c60

struct sBattleArenaTeam
{
    sBattleArenaTeam();

    // sBattleArenaTeam::AddMember (0x66d9e0): the member and 0x34D2 {7, world, layer, JID}.
    void AddMember(DWORD dwJID);
    // sBattleArenaTeam::PopMember (0x66daa0): the first member and 0x34D2 {11, world, layer, JID}.
    DWORD PopMember();

    BYTE                m_byCount;      // +0
    BYTE                m_by1;          // +1
    WORD                m_wWorldID;     // +2
    WORD                m_wLayerID;     // +4
    std::list<DWORD>    m_listMember;   // +8   JIDs
    DWORD               m_dw20;         // +14  0
    BYTE                m_by24;         // +18  not set by the constructor
};

C_ASSERT(FIELD_OFFSET(sBattleArenaTeam, m_listMember) == 8);
C_ASSERT(FIELD_OFFSET(sBattleArenaTeam, m_dw20) == 20);
C_ASSERT(sizeof(sBattleArenaTeam) == 28);

// ---------------------------------------------------------------------------------------------
// CBattleArenaMgr (0x338 bytes; static object 0x8A7BC0, g_pBattleArenaMgr = dword_8A895C)

class CBattleArenaMgr
{
public:
    typedef std::vector<sBattleArenaTeam>       TeamVector;
    typedef std::map<DWORD, BYTE>               ByteMap;
    typedef std::map<DWORD, DWORD>              DwordMap;
    typedef std::map<DWORD, ByteMap>            GroupMap;
    typedef std::map<DWORD, std::list<DWORD> >  MemberListMap;
    typedef std::list<sArenaLevelEntry>         LevelList;

    CBattleArenaMgr();                                  // CBattleArenaMgr::StaticInit 0x66db70
    virtual ~CBattleArenaMgr();                         // 0x66dda0
    // CBattleArenaMgr::Reset (0x66e050, slot 1): no participation, no match, every kind closed.
    virtual void Reset();
    // CBattleArenaMgr::Clear (0x66e0a0, slot 2): no participation, no match.
    virtual void Clear();

    // Requests of the game servers.
    void OnMsg_74D0(CMsg* pMsg);                                        // 0x66e5e0
    // CBattleArenaMgr::OnRegisterReq (0x66e990): byAction 1 register, 2 unregister.
    void OnRegisterReq(CMsg* pMsg, BYTE byAction);
    // CBattleArenaMgr::IsUserRegistered (0x66ee50): TRUE (and the error 2 sent) when the JID is
    // registered for the kind and size already.
    BOOL IsUserRegistered(BYTE bySize, BYTE byKind, DWORD dwJID);
    BOOL OnUnregisterReq(BYTE byKind, DWORD dwJID, BYTE bySize);       // 0x66ef90
    // CBattleArenaMgr::OnScheduleEvent (0x66f270): the GM commands of 0x74D0 type 3.
    void OnScheduleEvent(BYTE byKind, BYTE byEvent);

    // 0xB4D0 / 0x34D2 to the game servers.
    static void SendRegisterAck_OK(BYTE byAction, DWORD dwJID, BYTE byKind, BYTE bySize); // 0x66e800
    static void SendRegisterAck_Error(BYTE byAction, DWORD dwJID, BYTE byError);         // 0x66e8a0
    static void SendKickUser(DWORD dwJID, BYTE byReason);                                 // 0x66e910
    void SendParticipationStarted(BYTE byKind);                         // 0x66f2d0
    void SendParticipationEnded(BYTE byKind);                           // 0x66f3c0
    static void SendEndPlay(BYTE byKind);                               // 0x66f450

    // The schedule jobs write into their notice.
    void WriteParticipationStarted(CMsg* pMsg, BYTE byKind);           // 0x66f4e0
    void WriteParticipationEnded(CMsg* pMsg, BYTE byKind);             // 0x66f5c0
    static void WriteLetsParticipate(CMsg* pMsg, BYTE byKind);         // 0x66f630
    static void Write5MinRemain(CMsg* pMsg, BYTE byKind);              // 0x66f680
    static void Write1MinRemain(CMsg* pMsg, BYTE byKind);              // 0x66f6d0
    static void WriteEndPlay(CMsg* pMsg, BYTE byKind);                 // 0x66f710
    static void WriteStopResurrect(CMsg* pMsg, BYTE byKind);           // 0x66fb80
    // CBattleArenaMgr::OnScheduledGameEnd (0x66f750): the participation ends, the registrations
    // of the kind are dropped, {5, kind} into the notice.
    void OnScheduledGameEnd(CMsg* pMsg, BYTE byKind);

    // CShard_BattleArenaSchedule::SetField_0x2BC (0x66f4d0).
    void SetParticipationTime(DWORD dwTime) { m_dwParticipationTime = dwTime; }
    // BattleArenaSchedule_SetArenaSlot (0x66e5a0): the match schedule jobs.
    void SetMatchActive(BYTE bySize, DWORD dwActive) { m_adwMatchActive[bySize] = dwActive; }
    // CBattleArenaMgr::GetActiveMatchFlags (0x66e5b0): 0x10 << size for each active size.
    WORD GetActiveMatchFlags() const;

    // CBattleArenaMgr::GetRefGameWorld (0x66e0e0): INS_ARENA_OCCUPY / SCORE / FLAG _8 / _16 / _32
    // of the match type (0..2) and size (1..3); NULL for other values.
    static RefGameWorld* GetRefGameWorld(BYTE byMatchType, BYTE bySize);
    // CBattleArenaMgr::OpenArenaLayer (0x66e2f0): FALSE when no layer could be opened.
    static BOOL OpenArenaLayer(sBattleArenaTeam* pTeam, BYTE byMatchType, BYTE bySize);
    // CBattleArenaMgr::CloseArenaLayers (0x66e490): 0x3C15 "closed" for the open team layers of
    // the kind, then its teams are dropped.
    void CloseArenaLayers(BYTE byKind);
    // CBattleArenaMgr_CloseArenaOnce (0x6750e0): CloseArenaLayers unless the kind is closed.
    void CloseArenaOnce(BYTE byKind);

    // CBattleArenaMgr::CheckAndGo (0x66fbc0) and the rest of the game flow: provided by the
    // battle arena port of net_SM_world_5 (pending).
    void CheckAndGo(BYTE byKind);
    void CheckAndGoRandom();                                            // 0x66fc30
    void CheckAndGoParty();                                             // 0x670040
    void CheckAndGoGuild();                                             // 0x670600
    void CheckAndGoJob();                                               // 0x670bd0
    void OnGameEnded(BYTE byKind);                                      // 0x674c70

    TeamVector      m_avecTeam[BATTLEARENA_KIND_COUNT][BATTLEARENA_SIZE_COUNT];    // +4
    ByteMap         m_amapRandom[BATTLEARENA_SIZE_COUNT];           // +C4  JID -> value
    DwordMap        m_amapParty[BATTLEARENA_SIZE_COUNT];            // +E8  JID -> party ID
    DwordMap        m_amapGuild[BATTLEARENA_SIZE_COUNT];            // +10C JID -> guild ID
    ByteMap         m_amapJobA[BATTLEARENA_SIZE_COUNT];             // +130 JID -> value (job 0, 2)
    ByteMap         m_amapJobB[BATTLEARENA_SIZE_COUNT];             // +154 JID -> value (job 1, 3)
    GroupMap        m_amapPartyMember[BATTLEARENA_SIZE_COUNT];      // +178 party -> JID -> level
    DwordMap        m_amapParty19C[BATTLEARENA_SIZE_COUNT];         // +19C
    ByteMap         m_amapPartyLevel[BATTLEARENA_SIZE_COUNT];       // +1C0 party -> highest level
    LevelList       m_alistParty[BATTLEARENA_SIZE_COUNT];           // +1E4 parties by level
    MemberListMap   m_amapGuildMemberList[BATTLEARENA_SIZE_COUNT];  // +208 guild -> listed JIDs
    GroupMap        m_amapGuildMember[BATTLEARENA_SIZE_COUNT];      // +22C guild -> JID -> level
    DwordMap        m_amapGuild250[BATTLEARENA_SIZE_COUNT];         // +250
    ByteMap         m_amapGuildLevel[BATTLEARENA_SIZE_COUNT];       // +274 guild -> highest level
    LevelList       m_alistGuild[BATTLEARENA_SIZE_COUNT];           // +298 guilds by level
    DWORD           m_dwParticipationTime;      // +2BC ms (0x74D0 type 8)
    DWORD           m_adwCheckCount[BATTLEARENA_KIND_COUNT][BATTLEARENA_SIZE_COUNT]; // +2C0
    DWORD           m_adwParticipation[BATTLEARENA_KIND_COUNT];     // +2F0 1 while it runs
    DWORD           m_adwMatchActive[BATTLEARENA_SIZE_COUNT];       // +300 set by the match jobs
    CDateTime       m_dtParticipationBegin;     // +30C
    BYTE            m_abClosed[BATTLEARENA_KIND_COUNT];             // +334 layers closed
};

C_ASSERT(FIELD_OFFSET(CBattleArenaMgr, m_amapRandom) == 0xC4);
C_ASSERT(FIELD_OFFSET(CBattleArenaMgr, m_amapJobB) == 0x154);
C_ASSERT(FIELD_OFFSET(CBattleArenaMgr, m_amapGuildMemberList) == 0x208);
C_ASSERT(FIELD_OFFSET(CBattleArenaMgr, m_dwParticipationTime) == 0x2BC);
C_ASSERT(FIELD_OFFSET(CBattleArenaMgr, m_adwParticipation) == 0x2F0);
C_ASSERT(FIELD_OFFSET(CBattleArenaMgr, m_dtParticipationBegin) == 0x30C);
C_ASSERT(FIELD_OFFSET(CBattleArenaMgr, m_abClosed) == 0x334);
C_ASSERT(sizeof(CBattleArenaMgr) == 0x338);

extern CBattleArenaMgr* g_pBattleArenaMgr;
extern CBattleArenaMgr g_BattleArenaMgr;
