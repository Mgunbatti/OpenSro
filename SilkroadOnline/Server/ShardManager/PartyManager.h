#pragma once

#include <map>
#include <string>
#include <vector>
#include <hash_map>

#include "BSLib/ChunkAllocator.h"
#include "ShardManager/SocialExtern.h"

// Party manager of SR_ShardManager (CPartyManager, static object 0x89a558, g_pPartyManager
// 0x8a892c): the parties of the shard and the party matching board. Requests come from the game
// servers with 0x7866 (answered with 0xB866) and 0x3864; changes go to every game server as
// 0x3864 notifications.

enum
{
    MSG_PARTY_NOTIFY            = 0x3864,
    MSG_PARTY_ACK               = 0xB866,
    MSG_PARTY_MATCH_JOIN_REQ    = 0x706D,
    MSG_PARTY_MATCH_INVITE_REQ  = 0x7D1B,
    MSG_PARTY_MATCH_EXPIRED     = 0x386F,

    // result codes
    PARTY_ERR_MATCH_PARTY_FULL  = 0x2C08,
    PARTY_ERR_ALREADY_IN_PARTY  = 0x2C09,
    PARTY_ERR_DISBANDED         = 0x2C0B,
    PARTY_ERR_NO_PARTY          = 0x2C0D,
    PARTY_ERR_NO_MASTER         = 0x2C0E,
    PARTY_ERR_NO_USER           = 0x2C0F,
    PARTY_ERR_ALREADY_MEMBER    = 0x2C12,
    PARTY_ERR_FULL_8            = 0x2C13,
    PARTY_ERR_FULL_4            = 0x2C14,
    PARTY_ERR_TARGET_IN_PARTY   = 0x2C18,
    PARTY_ERR_ENTRY_EXISTS      = 0x2C1B,
    PARTY_ERR_NO_MATCH_ENTRY    = 0x2C1C,
    PARTY_ERR_NOT_ENTRY_MASTER  = 0x2C1D,
    PARTY_ERR_LEVEL             = 0x2C1E,
    PARTY_ERR_JOIN_PENDING      = 0x2C1F,
    PARTY_ERR_MATCH_TYPE        = 0x2C20,
    PARTY_ERR_TYPE              = 0x2C23,
    PARTY_ERR_HAS_MATCH_ENTRY   = 0x2C25,
};

// A party (28 bytes, CChunkAllocatorST<Party>).
class Party
{
public:
    Party();                                    // 0x44e000
    ~Party();                                   // 0x44dda0

    // Resets every member's party data and empties the member list (0x44e070).
    void Clear();
    // 0x44de10
    BOOL HasMember(CUser* pUser);
    // 8 members with type bit 0, otherwise 4 (0x44de70).
    BOOL IsFull();
    // ID, master and type; adds the master and then pInvitee (0x44e100).
    WORD InitWithTwoMembers(CUser* pMaster, DWORD dwID, CUser* pInvitee, BYTE byType);
    // 1 or an error code; sets the user's party ID (0x44e140).
    WORD AddMember(CUser* pUser);
    // Removes pUser (more than two members); its match entry moves to the new master
    // (0x44e1a0). The member count.
    int RemoveMemberAndNotify(CUser* pUser);
    // ID, master, type, count and the members (0x44e3b0).
    void WriteToMsg(CMsg* pMsg);
    // 0x44e500
    CUser* FindMemberByJID(DWORD dwJID);

    DWORD GetMemberCount() const
    {
        return (DWORD)m_vecMember.size();
    }

public:
    DWORD               m_dwID;         // +0x00
    DWORD               m_dwMasterJID;  // +0x04
    BYTE                m_byType;       // +0x08
    std::vector<CUser*> m_vecMember;    // +0x0C
};

C_ASSERT(FIELD_OFFSET(Party, m_vecMember) == 0x0C);
C_ASSERT(sizeof(Party) == 28);

// An entry of the party matching board (56 bytes, CChunkAllocatorST<PartyMatchEntry>).
struct PartyMatchEntry
{
    // PartyMatchEntry::ctor (0x454910, PartyMatchEntry::Init 0x455b60): empty title, settings
    // and IDs zero; the party, master, time and pending flag are left alone. The destructor
    // (0x4535f0 / 0x450920) only frees the title.
    PartyMatchEntry()
    {
        m_strTitle.clear();
        m_byMaxLevel = 0;
        m_byMinLevel = 0;
        m_byPurpose = 0;
        m_byPartyType = 0;
        m_dwPartyID = 0;
        m_dwID = 0;
    }

    // PartyMatchEntry::Reset (0x44dec0)
    void Reset();
    // PartyMatchEntry_ReadFromMsg (0x455970): ID, party ID, type, purpose, levels, title.
    void ReadFromMsg(CMsg* pMsg);
    // ID, registration time, the master's name, the member count and the settings (0x44f640). FALSE when the
    // master is not online (nothing written) or the party has lost its master (partly written).
    BOOL WriteToMsg(CMsg* pMsg);

    DWORD       m_dwID;             // +0x00
    DWORD       m_dwPartyID;        // +0x04
    BYTE        m_byPartyType;      // +0x08
    BYTE        m_byPurpose;        // +0x09
    BYTE        m_byMinLevel;       // +0x0A
    BYTE        m_byMaxLevel;       // +0x0B
    std::string m_strTitle;         // +0x0C
    Party*      m_pParty;           // +0x28
    DWORD       m_dwMasterJID;      // +0x2C
    DWORD       m_dwRegTime;        // +0x30 time() of the registration / last change
    DWORD       m_bJoinPending;     // +0x34
};

C_ASSERT(FIELD_OFFSET(PartyMatchEntry, m_pParty) == 0x28);
C_ASSERT(sizeof(PartyMatchEntry) == 56);

// 0x44ed20: party types 1 and 3 go together, 2 and 4 only with themselves.
BOOL Party_IsCompatibleType(BYTE byType, BYTE byOther);
// 0x44f7f0: whether a request of type byType may join an entry of purpose byPurpose.
BOOL PartyMatch_IsJoinAllowed(BYTE byType, BYTE byPurpose);
// PartyMemberInfo::Copy (0x44df70)
void PartyMemberInfo_Copy(sPartyMemberInfo* pDest, const sPartyMemberInfo* pSrc);

typedef stdext::hash_map<DWORD, Party*> PartyHash;
typedef std::map<DWORD, PartyMatchEntry*> PartyMatchMap;

class CPartyManager
{
public:
    CPartyManager();                                                    // 0x44e560
    virtual ~CPartyManager();                                           // 0 (0x44e6f0)

    // 0x44df10
    PartyMatchEntry* FindMatchEntry(DWORD dwEntryID);
    // 0x44e380: BroadcastToGameServers, then the message is released.
    static void SendToShardAndFree(CMsg* pMsg, DWORD dwParam);
    // 0x44e460
    void SendPartyInfo(CMsg* pMsg, Party* pParty);
    static CMsg* MakeNotify3864(BYTE bySubType);                        // 0x44e7c0
    static CMsg* MakeReplyB866(DWORD dwRequestID);                      // 0x44e800
    void OnPartyNotifyRelay(CMsg* pMsg);                                // 0x44e840
    // A new party of pMaster and pInvitee (0x44e9c0); NULL with *pwResult set on failure.
    Party* CreateParty(CUser* pMaster, CUser* pInvitee, BYTE byType, WORD* pwResult);
    // 0x44ead0
    Party* FindParty(DWORD dwPartyID);
    // 0x44eb30
    void DisbandParty(DWORD dwPartyID);
    // OnMsg_7866 (0x44ec50)
    void OnPartyRequest(CMsg* pMsg);
    void OnCreateParty(CMsg* pMsg);                                     // 0x44ed70
    void OnJoinParty(CMsg* pMsg);                                       // 0x44f130
    // OnMsg_3864 (0x44f3f0)
    void OnPartyNotify(CMsg* pMsg);
    void OnPartyInfoRequest(CMsg* pMsg);                                // 0x44f490
    void OnLeaveParty(CMsg* pMsg);                                      // 0x44f4f0
    // Leave / kick / logout (0x44f580): a party of two is disbanded.
    void RemoveMember(DWORD dwPartyID, DWORD dwJID, BYTE byReason);
    // OnMsg_7C00 opcodes 0x706D / 0x7D1B (0x44f850)
    WORD OnPartyMatchJoinRequest(DWORD dwRequesterJID, DWORD dwOpcode, CMsg* pMsg);
    // The answer of the entry's master (trs_party_job, 0x44fbe0).
    void OnPartyMatchJoinAnswer(trs_party_job* pJob, BYTE byAnswer);

    // Party matching board (0x4504c0..0x451630)

    // Every tick (CMainProcess_TickPartyMatchExpiry): entries without a pending join that are
    // older than an hour are removed, their master gets 0x386F (0x4504c0).
    void ExpireMatchEntries();
    // Subtypes 10..13 of 0x7866 (0x450680, 0x450990, 0x450ba0, 0x450d20)
    void OnRegisterPartyMatch(CMsg* pMsg);
    void OnModifyPartyMatch(CMsg* pMsg);
    void OnDeletePartyMatch(CMsg* pMsg);
    void OnPartyMatchList(CMsg* pMsg);
    // The entry of pUser's party; pUser must be its master (0x451110). 1 or an error code.
    WORD RegisterMatchEntryForParty(DWORD* pdwEntryID, CUser* pUser,
                                    const PartyMatchEntry* pSettings);
    // The entry of a user without party; strName goes to the user (0x451290).
    WORD AddMatchEntry(DWORD* pdwEntryID, const PartyMatchEntry* pSettings, CUser* pUser,
                       const std::string& strName);
    // 0x4513c0
    WORD ModifyMatchEntry(const PartyMatchEntry* pSettings, CUser* pUser);
    // 0x451490
    WORD RemoveMatchEntry(CUser* pUser, DWORD dwEntryID);
    // CUser::Reset (0x451590)
    void OnCharLogout(CUser* pUser, DWORD dwEntryID);
    // The list pages of the board, 23 entries per message; the entry count of a page is kept
    // in its opcode field (0x451630).
    void RebuildMatchListCache();
    // CPartyManager_NextMatchSerial (0x44def0)
    DWORD NextMatchSerial()
    {
        if (++m_dwMatchSerial == 0)
            m_dwMatchSerial = 1;
        return m_dwMatchSerial;
    }

public:
    DWORD                               m_dwPartyIDCounter;     // +0x04
    PartyHash                           m_hashParty;            // +0x08
    CChunkAllocatorST<Party>            m_PartyPool;            // +0x30
    DWORD                               m_dwMatchSerial;        // +0x8C (not initialised)
    PartyMatchMap                       m_mapMatchEntry;        // +0x90
    CChunkAllocatorST<PartyMatchEntry>  m_MatchEntryPool;       // +0x9C
    std::vector<CMsg*>                  m_vecMatchListCache;    // +0xF8 list pages
};

C_ASSERT(FIELD_OFFSET(CPartyManager, m_hashParty) == 0x08);
C_ASSERT(FIELD_OFFSET(CPartyManager, m_PartyPool) == 0x30);
C_ASSERT(FIELD_OFFSET(CPartyManager, m_dwMatchSerial) == 0x8C);
C_ASSERT(FIELD_OFFSET(CPartyManager, m_mapMatchEntry) == 0x90);
C_ASSERT(FIELD_OFFSET(CPartyManager, m_MatchEntryPool) == 0x9C);
C_ASSERT(sizeof(CPartyManager) == 0x108);

// 0x4558e0: entry ID, two DWORDs, the party type and the member info; returns the member mask.
BYTE PartyMatchJoinRequest_ReadFromMsg(CMsg* pMsg, sPartyInfo* pInfo);

// g_pPartyManager (0x8a892c); GetSingleton_CPartyManager (0x418440) asserts it.
extern CPartyManager* g_pPartyManager;

inline CPartyManager* GetSingleton_CPartyManager()
{
    if (!g_pPartyManager)
        BSAssertFail();
    return g_pPartyManager;
}
