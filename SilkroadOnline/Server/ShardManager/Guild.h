#pragma once

#include <winsock2.h>
#include <windows.h>

#include <string>
#include <utility>
#include <vector>

#include "BSNet/Msg.h"
#include "ServerCommon/GuildCommon.h"
#include "ShardManager/GuildExtern.h"

// The shard manager's guild: CGuild (the DB side of the guild operations), the alliance DB
// functions and CGuildManager (requests 0x78F7 / notifications 0x78FE of the game servers,
// answers 0xB8F7, notifications 0x38F5 to the game servers).
//
// SM_social1 ports the functions with an address comment in Guild.cpp; the other CGuildManager
// members declared here (the remaining handlers, the DB helpers and the Send* notifications)
// belong to the second guild group.

enum
{
    GUILD_MSG_REQUEST       = 0x78F7,   // game server -> shard: BYTE subtype, ...
    GUILD_MSG_ACK           = 0xB8F7,   // shard -> game server: DWORD request ID, BYTE result, ...
    GUILD_MSG_GS_NOTIFY     = 0x78FE,   // game server -> shard: BYTE subtype, ...
    GUILD_MSG_NOTIFY        = 0x38F5,   // shard -> game servers: BYTE subtype, ...
};

// Results of the guild operations (WORD; 1 is success, the others are the error codes the game
// server shows).
enum
{
    GUILD_OK                        = 1,
    GUILD_ERR_FAILED                = 2,
    GUILD_ERR_NO_RECORD             = 3,
    GUILD_ERR_ARGUMENT              = 5,
    GUILD_ERR_GUILD_NOT_FOUND       = 0x4C0D,   // 19469
    GUILD_ERR_MEMBER_NOT_FOUND      = 0x4C1D,   // 19485
    GUILD_ERR_NOT_MASTER            = 0x4C1E,   // 19486
    GUILD_ERR_MASTER_CANNOT_LEAVE   = 0x4C1F,   // 19487
    GUILD_ERR_MAX_LEVEL             = 0x4C21,   // 19489
    GUILD_ERR_NOTICE_TITLE          = 0x4C22,   // 19490
    GUILD_ERR_NOTICE_BODY           = 0x4C23,   // 19491
    GUILD_ERR_TARGET_NOT_MASTER     = 0x4C24,   // 19492
    GUILD_ERR_TARGET_NOT_FOUND      = 0x4C25,   // 19493
    GUILD_ERR_ALREADY_IN_UNION      = 0x4C28,   // 19496
    GUILD_ERR_UNION_FULL            = 0x4C29,   // 19497
    GUILD_ERR_UNION_NOT_FOUND       = 0x4C2C,   // 19500
    GUILD_ERR_DONATE_SILENT         = 0x4C2D,   // 19501 (OnDonateSP does not log it)
    GUILD_ERR_NOT_IN_UNION          = 0x4C2F,   // 19503
    GUILD_ERR_KICK_UNION_NOT_FOUND  = 0x4C30,   // 19504
    GUILD_ERR_NOT_ENOUGH_GP         = 0x4C32,   // 19506
    GUILD_ERR_VOTE_RUNNING          = 0x4C37,   // 19511
    GUILD_ERR_LOCK_HELD_BY_SELF     = 0x4C47,   // 19527
    GUILD_ERR_LOCK_HELD_BY_OTHER    = 0x4C48,   // 19528
    GUILD_ERR_LOCK_NOT_HELD         = 0x4C49,   // 19529
    GUILD_ERR_LEVEL_TOO_LOW         = 0x4C4A,   // 19530
    GUILD_ERR_ABILITY_SET           = 0x4C54,   // 19540
};

// dword_781A98: the GP a level-up from level n costs (index 1..4; index 0 is the last entry of
// the member limit table before it, index 5 the zero after it).
extern const int g_anGuildLevelUpGP[6];

// The (CharID, permission) list of a permission update.
typedef std::vector<std::pair<DWORD, DWORD> > GuildPermissionVector;

// The guild war request of the game server (GuildWarRequest_ReadFromMsg, second guild group).
struct GUILD_WAR_REQUEST
{
    BYTE    b00;                    // +0
    BYTE    bVictoryPointIndex;     // +1
    DWORD   dwDuration;             // +4  packed: hours 10-14, days 15-19, minutes 20-25,
                                    //     seconds 26-31; 0x7FFFFFFF and above: no end
    DWORD   dwLodgedGold;           // +8  half of the stake
};

C_ASSERT(sizeof(GUILD_WAR_REQUEST) == 12);

// GuildWarRecord::Update (0x435a10): a new war of dwGuild1 against dwGuild2.
void GuildWarRecord_Update(CInstanceGuildWar* pWar, DWORD dwGuild1,
                           const GUILD_WAR_REQUEST& request, DWORD dwGuild2);
// GuildWarRecord::SetSlotValue (0x4359d0): PointGain[nIndex] (asserts for an index above 1).
void GuildWarRecord_SetPointGain(CInstanceGuildWar* pWar, DWORD nIndex, int nValue);
// GuildMember_WriteInfo (0x435c10, misnamed): ID, packed end time (0 when unset), victory
// point index, lodged gold, the two guilds and their points.
void GuildWarRecord_WriteToMsg(const CInstanceGuildWar* pWar, CMsg* pMsg);

// CCharacter_GetTrainingCampID (0x42c270): the camp of the character, 0 without one.
DWORD CCharacter_GetTrainingCampID(CCharacter* pCharacter);

class CGuild : public CGuildCommon
{
public:
    // CGuild::ctor (0x435d40) / CGuild::dtor (0x435dc0)
    CGuild() {}
    virtual ~CGuild() {}                                                    // 0

    // CGuild::RefreshMemberStatus (0x435fc0): type 1 stores the member's level (DB procedure
    // and record), type 2 logs the member in (dwValue 0, pUser) or out (1, releasing the
    // lock). The change goes to the game servers (0x38F5 subtype 6) in every case.
    int RefreshMemberStatus(DWORD dwCharID, BYTE bType, DWORD dwValue, CUser* pUser);
    // CGuild::OnMemberMsgRelay (0x435e10): BYTE, two strings -> 0x38F5 subtype 7.
    void OnMemberMsgRelay(CMsg* pMsg);
    // CGuild::DBDeleteMember (0x4361a0): _GUILD_DELMEMBER unless bReason is 3.
    WORD DBDeleteMember(DWORD dwGuildID, DWORD dwCharID, BYTE bReason);
    // CGuild::DBDeleteGuild (0x436320): _GUILD_DELETEGUILD, then the members are freed.
    WORD DBDeleteGuild(DWORD dwCharID);
    // CGuild::DonateGP (0x436470)
    WORD DonateGP(int nAmount, DWORD dwCharID);
    // CGuild::LevelUp (0x436600)
    WORD LevelUp();
    // CGuild::UpdateNotice (0x436730)
    WORD UpdateNotice(const std::string& strTitle, const std::string& strNotice);
    // CGuild::SetAllianceID_DBUpdate (0x4368a0): saved only with bSave 1.
    WORD SetAllianceID(CAlliance* pAlliance, BYTE bSave);
    // CGuild::LeaveUnion (0x4369f0): *pbResult becomes 3 when the alliance has to be
    // disbanded (the guild is its master or the last but one).
    WORD LeaveUnion(BYTE* pbResult);
    // CGuild::ClearAllianceID (0x436b90): record only.
    WORD ClearAllianceID();
    // CGuild::DelegateMaster (0x436bd0)
    WORD DelegateMaster(CInstanceGuildMember* pNewMaster, CInstanceGuildMember* pOldMaster);
    // CGuild::UpdateMemberPermissions (0x436ce0)
    WORD UpdateMemberPermissions(const GuildPermissionVector& vecPermission);
    // CGuild::DBIsMasterCandidates (0x436ea0): the candidates of a master vote.
    WORD DBIsMasterCandidates(CVote* pVote);
    // GuildRecord::AddGold (0x437040): Booty += nGold (record only).
    void AddBooty(int nGold);
    // CGuildManager::DBUpdateGuildWarRecord (0x437080, misnamed): Booty = 0 and saved.
    WORD DBClearBooty();
    // CGuild::Rename (0x437210)
    WORD Rename(const std::string& strName);
    // CGuild::MemberRename (0x4372c0)
    WORD MemberRename(DWORD dwCharID, const std::string& strName);
    // CGuild::SetMemberOnline (0x4373e0, misnamed): bType 1 takes the guild lock (members
    // with permission 8, guilds of level 2 and above), 2 releases it.
    WORD SetMemberOnline(DWORD dwCharID, BYTE bType);
    // CGuild::DBUpdateMember (0x437510): the member's nickname.
    WORD DBUpdateMember(DWORD dwCharID, const std::string& strNickname);
    // CGuild::SetCrestRev_DBUpdate (0x437680)
    WORD SetCrestRev(int nCrestRev);
    // CGuild::OffsetGP (0x437800)
    WORD OffsetGP(int nOffset, BOOL bSave);
    // CGuild::SetMercenaryAttr (0x437950): *pdwGold receives the gold cost.
    WORD SetMercenaryAttr(BYTE bAbility, DWORD* pdwGold, DWORD* pdwGP);
    // CGuild::BroadcastToOnlineMembers (0x437e70)
    void BroadcastToOnlineMembers(transaction* pTransaction);

    // CGuild::GetMercenaryAbilityCost (0x440940, second guild group)
    static void GetMercenaryAbilityCost(DWORD* pdwGP, BYTE bAbility, DWORD* pdwGold);
};

C_ASSERT(sizeof(CGuild) == 0x50);

class CGuildManager : public CGuildMgrCommon
{
public:
    // CGuildManager::CreateSingleton (0x437f10) / CGuildManager::dtor (0x437fb0)
    CGuildManager();
    virtual ~CGuildManager();                                               // 0

    virtual BOOL Load();                                                    // 1 (0x438050)
    // CGuildManager::vf03 (0x438060): reload (exits the process when it fails) and tell the
    // game servers (0x38F5 subtype 32).
    virtual void Reset();                                                   // 3
    // CGuildManager::vf04 (0x4380c0)
    virtual CGuildCommon* CreateGuild(CInstanceGuild* pRecord);             // 4
    virtual BOOL AddHostileGuilds(CInstanceGuildWar* pWar);                 // 5 (0x43c260)
    virtual BOOL RemoveHostileGuilds(CInstanceGuildWar* pWar);              // 6 (0x43c2c0)
    virtual WORD OnCombatResult(CInstanceGuildWar* pWar, CGuildMember* pKiller,
                                CGuildMember* pVictim, int nPoint, int n5, int n6);  // 7

    CGuild* FindGuildT(DWORD dwGuildID)
    {
        return static_cast<CGuild*>(FindGuild(dwGuildID));
    }

    // CGuildManager::SendErrorB8F7 (0x438010, misnamed): a new 0xB8F7 {DWORD dwRequestID};
    // the caller appends the result and sends it.
    static CMsg* AllocAck(DWORD dwRequestID);
    // CGuildManager::DeleteGuild (0x4381c0)
    WORD DeleteGuild(DWORD dwGuildID, DWORD dwCharID);
    // CGuildManager::OnGuildRequest (0x438260): 0x78F7 by subtype.
    void OnGuildRequest(CMsg* pMsg);
    // CGuildManager::CheckGuildWarTimeouts (0x438430)
    void CheckGuildWarTimeouts();
    // CGuildManager::BroadcastGuildWarState (0x4385e0): 0x38F5 subtype 31 with the time.
    static int BroadcastGuildWarState();
    // CGuildManager::OnGuildNotify (0x438660): 0x78FE by subtype.
    void OnGuildNotify(CMsg* pMsg);
    // CGuildManager::OnRefreshMemberStatus (0x4386c0) / OnMemberRelay (0x438770)
    void OnRefreshMemberStatus(CMsg* pMsg);
    void OnMemberRelay(CMsg* pMsg);

    // Request handlers (subtype in brackets).
    void OnDisbandGuild(CMsg* pMsg);            // 1  0x4387a0
    void OnLeaveGuild(CMsg* pMsg);              // 3  0x438980
    void OnDonateSP(CMsg* pMsg);                // 8  0x438ad0
    void OnLevelUpGuild(CMsg* pMsg);            // 9  0x438bb0
    void OnUpdateNotice(CMsg* pMsg);            // 10 0x438d80
    void OnUnionInvite(CMsg* pMsg);             // 11 0x438f70
    void OnUnionLeave(CMsg* pMsg);              // 16 0x4391d0
    void OnUnionKick(CMsg* pMsg);               // 17 0x439350
    void OnDelegateMaster(CMsg* pMsg);          // 19 0x439550

    // Second guild group: the other handlers.
    void OnCreateGuild(CMsg* pMsg);             // 0
    void OnJoinGuild(CMsg* pMsg);               // 2
    void OnUpdatePermissions(CMsg* pMsg);       // 20
    void OnMasterVoteStart(CMsg* pMsg);         // 21
    void OnGuildWarRecordUpdate(CMsg* pMsg);    // 30
    void OnMemberOnlineState(CMsg* pMsg);       // 36
    void OnMemberInfoUpdate(CMsg* pMsg);        // 37
    void OnGuildCrestRequest38(CMsg* pMsg);     // 38
    void OnGuildCrestUpdate(CMsg* pMsg);        // 39
    void OnUnionCrestRequest40(CMsg* pMsg);     // 40
    void OnUnionCrestUpdate(CMsg* pMsg);        // 41
    void OnDonateSP42(CMsg* pMsg);              // 42
    void OnGiveMercenaryAbility(CMsg* pMsg);    // 43
    void OnUpdateGuildSP(CMsg* pMsg);           // 44
    void OnOffsetGP(CMsg* pMsg);                // 45
    void OnSetSiegeAuthority(CMsg* pMsg);       // 46
    void OnGuildWarBegin(CMsg* pMsg);           // 50
    void OnOffsetGPAndLevel(CMsg* pMsg);        // 53
    void OnGuildWarCombatResult(CMsg* pMsg);    // notify 29

    // Second guild group: DB helpers and notifications used here.
    WORD RemoveMember(DWORD dwGuildID, DWORD dwCharID, BYTE bReason);
    WORD DBCreateUnion(CGuild* pMaster, CGuild* pMember, DWORD* pdwAllianceID);
    void DisbandUnion(DWORD dwAllianceID);
    WORD DBEndGuildWar(DWORD dwWinnerGuildID, DWORD dwWarID);
    WORD DBAddGPHistory(DWORD dwGuildID, DWORD dwJID, int nGP, BYTE bReason);
    BYTE DBDeleteGPHistory(CGuild* pGuild);
    void SendGuildInfo53(DWORD dwGuildID);
    void SendGPHistory54(DWORD dwGuildID, BYTE bCount);
    static void SendDisbandResult(DWORD dwGuildID);
    static void SendDonateSPResult(DWORD dwGuildID, DWORD dwCharID, int nAmount);
    static void SendLevelUpResult(DWORD dwGuildID, BYTE bLevel, int nGatheredSP);
    static void SendNoticeUpdated(DWORD dwGuildID, const std::string& strTitle,
                                  const std::string& strNotice);
    static void SendUnionCreated(DWORD dwAllianceID, DWORD dwMasterGuildID,
                                 DWORD dwMemberGuildID);
    static void SendUnionGuildAdded(DWORD dwAllianceID, DWORD dwGuildID, BYTE bSlot);
    static void SendUnionGuildRemoved(DWORD dwAllianceID, DWORD dwGuildID, BYTE bReason);
    static void SendUnionDisbanded(DWORD dwAllianceID);
    static void SendMasterDelegated(DWORD dwGuildID, DWORD dwNewMasterCharID,
                                    DWORD dwOldMasterCharID);
    static void SendGuildWarEnded(DWORD dwWarID, DWORD dwWinnerGuildID);
    static void SendGuildRenamed(const std::string& strName, DWORD dwGuildID);
    static void SendMemberRenamed(const std::string& strName, DWORD dwGuildID,
                                  DWORD dwCharID);
    static void SendMemberOnlineState(DWORD dwGuildID, DWORD dwCharID, BYTE bState);
};

C_ASSERT(sizeof(CGuildManager) == 0x28);

// 0x8A8924 (the object itself is the static at 0x89A398)
extern CGuildManager* g_pGuildManager;

// GetSingleton_CGuildManager (0x40bab0): g_pGuildManager, asserted.
inline CGuildManager* GetSingleton_CGuildManager()
{
    if (!g_pGuildManager)
        BSAssertFail();
    return g_pGuildManager;
}
