#pragma once

#include <windows.h>

#include <list>
#include <map>
#include <vector>

#include "BSNet/Msg.h"
#include "ServerCommon/ByteStream.h"
#include "ServerCommon/InstanceRecords.h"

// Guild data shared by the shard manager and the game server: the guild member, the guild with
// its members, GP history, master candidates and hostile guilds, the alliance and the guild
// manager base with the guild, alliance and guild war maps.
//
// Only the layouts and the functions of SM_social1 are complete here; the remaining CGuildCommon
// / CGuildMgrCommon functions (the declarations without a definition in GuildCommon.cpp) belong
// to the second guild group.

class CUser;
class CVote;

// One guild member (0x14 bytes, pooled; RTTI GuildMember).
class CGuildMember
{
public:
    // GuildMember::ctor (0x43fab0)
    CGuildMember()
    {
        m_dwState = (DWORD)-1;
        m_pRecord = NULL;
        m_pUser = NULL;
    }

    // GuildMember::vf00 (0x43fb40): back to the pool ("why do you about to delete already
    // deleted !!!" when it is not taken).
    virtual void Release();                                                 // 0

    // GuildMember_IsOffline (0x43fc10, misnamed): TRUE when the record's member class is 0,
    // i.e. the member is the guild master; undefined without a record.
    BOOL IsMaster() const;
    // GuildMember_GetField1C (0x43fc90): the CharID of the record.
    DWORD GetCharID() const;

public:
    DWORD                   m_dwState;      // +04  -1 after construction
    BYTE                    m_bAlive;       // +08  taken from the pool
    CInstanceGuildMember*   m_pRecord;      // +0C
    CUser*                  m_pUser;        // +10  set while the member is logged in
};

C_ASSERT(FIELD_OFFSET(CGuildMember, m_dwState) == 0x04);
C_ASSERT(FIELD_OFFSET(CGuildMember, m_pRecord) == 0x0C);
C_ASSERT(FIELD_OFFSET(CGuildMember, m_pUser) == 0x10);
C_ASSERT(sizeof(CGuildMember) == 0x14);

// GuildMember_FreePtr (0x43fb10): returns a taken member to the pool and clears the pointer.
void GuildMember_FreePtr(CGuildMember*& pMember);

// An entry of the hostile guild map: the guild war and the side of this guild.
struct GUILD_HOSTILE_ENTRY
{
    DWORD               dwSide;     // +0   1 when this guild is the second guild of the war
    CInstanceGuildWar*  pWar;       // +4
};

C_ASSERT(sizeof(GUILD_HOSTILE_ENTRY) == 8);

typedef std::map<DWORD, CGuildMember*> GuildMemberMap;           // CharID -> member
typedef std::map<DWORD, GUILD_HOSTILE_ENTRY> GuildHostileMap;     // guild ID -> war

// The guild (0x50 bytes with CGuild, which adds no data).
class CGuildCommon
{
public:
    // CGuildCommon::ctor (0x43fd40)
    CGuildCommon();
    // CGuildCommon::dtor (0x43fe90): Clear, then the containers.
    virtual ~CGuildCommon();                                                // 0

    // Slots 1..7 (0x4401f0, 0x43ff50, 0x440610, 0x440730, 0x4406a0, 0x4407d0, 0x440de0).
    virtual BOOL Initialize(CInstanceGuild* pRecord);                       // 1
    virtual void Clear();                                                   // 2
    virtual DWORD AddMember(DWORD dwState, CInstanceGuildMember* pRecord, CUser* pUser);  // 3
    virtual void WriteToMsg(CMsg* pMsg, BYTE bFlag);                        // 4
    virtual void Serialize(CByteStream* pStream, BYTE bFlag);              // 5
    virtual void WriteInfo(CMsg* pMsg, BYTE bMask);                        // 6
    virtual BOOL IsHostileGuild(DWORD dwGuildID);                          // 7

    // Guild_GetID_45E4F0 (0x45e4f0): the ID of the record.
    DWORD GetID() const
    {
        return m_pRecord->ID;
    }
    // Guild_GetLevel_4409C0 (0x4409c0)
    BYTE GetLevel() const
    {
        return m_pRecord->Lvl;
    }
    // Guild_GetAllianceID_4409D0 (0x4409d0)
    DWORD GetAllianceID() const
    {
        return m_pRecord->Alliance;
    }

    // CGuildCommon::FindMember (0x4408a0): NULL for CharID 0 or an unknown member.
    CGuildMember* FindMember(DWORD dwCharID);
    // CGuildCommon::GetMemberSession (0x440a70, misnamed): TRUE when the member is the guild
    // master (FALSE when there is no such member; asserts without a record).
    BOOL IsMemberMaster(DWORD dwCharID);
    // CGuildCommon::MemberHasPermission8 (0x440aa0)
    BOOL MemberHasPermission8(DWORD dwCharID);
    // CGuildCommon::HasMasterCandidate (0x440c80)
    BOOL HasMasterCandidate(BYTE bKey);
    // Guild_PushCandidate_440C60 (0x440c60)
    void PushCandidate(CVote* pVote)
    {
        m_vecCandidate.push_back(pVote);
    }
    // GuildMemberCursor_Find (0x440910) / GuildMemberCursor_Reset (0x440930): the member that
    // holds the guild lock (permission 8).
    void SetLockMember(DWORD dwCharID);
    void ResetLockMember()
    {
        m_pLockMember = NULL;
        m_dwLockCharID = 0;
    }

    // CGuildCommon::FindMemberEntry (0x435ce0): the hostile entry of the guild, NULL when
    // there is none.
    GUILD_HOSTILE_ENTRY* FindHostileEntry(DWORD dwGuildID);

public:
    CInstanceGuild*                 m_pRecord;          // +04
    CInstanceGuildMember*           m_pMasterRecord;    // +08
    CGuildMember*                   m_pLockMember;      // +0C
    std::list<CInstanceGPHistory*>  m_listGPHistory;    // +10
    GuildMemberMap                  m_mapMember;        // +1C
    DWORD                           m_dwLockCharID;     // +28
    std::vector<CVote*>             m_vecCandidate;     // +2C
    GuildHostileMap                 m_mapHostile;       // +3C
    DWORD                           m_dwMaxLevel;       // +48  CountMembers
    DWORD                           m_dwAvgLevel;       // +4C
};

C_ASSERT(FIELD_OFFSET(CGuildCommon, m_pRecord) == 0x04);
C_ASSERT(FIELD_OFFSET(CGuildCommon, m_listGPHistory) == 0x10);
C_ASSERT(FIELD_OFFSET(CGuildCommon, m_mapMember) == 0x1C);
C_ASSERT(FIELD_OFFSET(CGuildCommon, m_dwLockCharID) == 0x28);
C_ASSERT(FIELD_OFFSET(CGuildCommon, m_vecCandidate) == 0x2C);
C_ASSERT(FIELD_OFFSET(CGuildCommon, m_mapHostile) == 0x3C);
C_ASSERT(FIELD_OFFSET(CGuildCommon, m_dwMaxLevel) == 0x48);
C_ASSERT(sizeof(CGuildCommon) == 0x50);

// An alliance (union) of up to eight guilds (8 bytes, stored by value in the alliance map).
// The DB functions are the shard manager's (ShardManager\Guild.cpp).
class CAlliance
{
public:
    // CAlliance::ctor (0x433f70)
    CAlliance()
    {
        m_pRecord = NULL;
    }
    // CAlliance::dtor (0x433fa0): nothing but the vtable.
    virtual ~CAlliance() {}                                                 // 0

    // CAlliance_HasGuild (0x4342e0)
    BOOL HasGuild(DWORD dwGuildID) const
    {
        for (BYTE i = 0; i < 8; ++i)
        {
            if (m_pRecord->Ally[i] == (int)dwGuildID)
                return TRUE;
        }
        return FALSE;
    }
    // CAlliance_CountGuilds (0x434300, misnamed): the first empty slot, 0 when all eight are
    // used.
    BYTE FindEmptySlot() const
    {
        for (BYTE i = 0; i < 8; ++i)
        {
            if (!m_pRecord->Ally[i])
                return i;
        }
        return 0;
    }
    // CAlliance::CountGuilds (0x434320): all used slots.
    BYTE CountGuilds() const
    {
        BYTE n = 0;
        for (int i = 0; i < 8; ++i)
        {
            if (m_pRecord->Ally[i])
                ++n;
        }
        return n;
    }
    // CAlliance_GetMasterGuildID (0x434340): slot 0, asserted.
    DWORD GetMasterGuildID() const
    {
        DWORD dwGuildID = m_pRecord->Ally[0];
        if (!dwGuildID)
            BSAssertFail();
        return dwGuildID;
    }
    // CAlliance_GetCrestField3C / 40 (0x434360 / 0x434370)
    int GetLastCrestRev() const
    {
        return m_pRecord->LastCrestRev;
    }
    int GetCurCrestRev() const
    {
        return m_pRecord->CurCrestRev;
    }

    // Shard manager (ShardManager\Guild.cpp).
    void ReleaseRecord();                                   // 0x433fc0
    WORD RemoveGuildSlot(DWORD dwGuildID);                  // 0x434040
    WORD DBRemove();                                        // 0x4341e0
    WORD SetCrestRev(int nCrestRev);                        // 0x434380
    WORD AddGuild(class CGuild* pGuild, BYTE bSlot);        // 0x43bca0 (second guild group)

public:
    CInstanceAlliedClan*    m_pRecord;      // +4
};

C_ASSERT(sizeof(CAlliance) == 8);

typedef std::map<DWORD, CGuildCommon*> GuildMap;
typedef std::map<DWORD, CAlliance> AllianceMap;
typedef std::map<DWORD, CInstanceGuildWar*> GuildWarMap;

// The guild manager base (0x28 bytes).
class CGuildMgrCommon
{
public:
    // CGuildMgrCommon::ctor (0x441060)
    CGuildMgrCommon() {}
    // CGuildMgrCommon::dtor (0x4411f0)
    virtual ~CGuildMgrCommon() {}                                           // 0

    // Slots 1..11 (CGuildMgrCommon::vf01.. / CGuildManager overrides).
    virtual BOOL Load();                                                    // 1
    virtual void Clear();                                                   // 2
    virtual void Reset() = 0;                                               // 3
    virtual CGuildCommon* CreateGuild(CInstanceGuild* pRecord) = 0;         // 4
    virtual BOOL AddHostileGuilds(CInstanceGuildWar* pWar) = 0;             // 5
    virtual BOOL RemoveHostileGuilds(CInstanceGuildWar* pWar) = 0;          // 6
    virtual WORD OnCombatResult(CInstanceGuildWar* pWar, CGuildMember* pKiller,
                                CGuildMember* pVictim, int nPoint, int n5, int n6) = 0;   // 7
    virtual CAlliance* AddAlliance(CInstanceAlliedClan* pRecord);           // 8
    virtual CAlliance* FindAlliance(DWORD dwAllianceID);                    // 9
    virtual void RemoveAlliance(DWORD dwAllianceID);                        // 10
    virtual CAlliance* FindAllianceOfGuild(DWORD dwGuildID);               // 11

    // CGuildMgrCommon::FindGuild (0x40bf10): NULL for ID 0 or an unknown guild.
    CGuildCommon* FindGuild(DWORD dwGuildID);

public:
    GuildMap        m_mapGuild;         // +04
    AllianceMap     m_mapAlliance;      // +10
    GuildWarMap     m_mapGuildWar;      // +1C
};

C_ASSERT(FIELD_OFFSET(CGuildMgrCommon, m_mapGuild) == 0x04);
C_ASSERT(FIELD_OFFSET(CGuildMgrCommon, m_mapAlliance) == 0x10);
C_ASSERT(FIELD_OFFSET(CGuildMgrCommon, m_mapGuildWar) == 0x1C);
C_ASSERT(sizeof(CGuildMgrCommon) == 0x28);
