#pragma once

#include <windows.h>

#include <string>

#include "BSNet/Msg.h"

// Party data exchanged between the shard manager and the game servers: the member info with
// its field mask, the party info (party, leader, setting and the member) and the party match
// settings. The field mask selects the parts that are read and written:
//   0x10 ID, 0x01 name and RefObjID, 0x02 level, 0x04 HP/MP, 0x20 position and angle,
//   0x40 guild name, 0x80 the byte at +41, 0x08 the two masteries.

enum
{
    PARTY_MEMBER_ID         = 0x10,
    PARTY_MEMBER_NAME       = 0x01,
    PARTY_MEMBER_LEVEL      = 0x02,
    PARTY_MEMBER_HPMP       = 0x04,
    PARTY_MEMBER_POSITION   = 0x20,
    PARTY_MEMBER_GUILD      = 0x40,
    PARTY_MEMBER_B41        = 0x80,
    PARTY_MEMBER_MASTERY    = 0x08,
    PARTY_MEMBER_ALL        = 0xFF,
};

// One party member (0x64 bytes).
struct sPartyMemberInfo
{
    // sPartyMemberInfo_ctor (0x428470)
    sPartyMemberInfo()
    {
        Clear();
    }

    // sPartyMemberInfo_dtor (0x428870) is the destructor of the two strings.

    // Inlined in sPartyInfo_ctor / sPartyInfo_Reset: clears the name and every value; the guild
    // name stays.
    void Clear()
    {
        m_strName.clear();
        m_dwID = 0;
        m_dwRefObjID = 0;
        m_bLevel = 0;
        m_bHPMP = 0;
        m_wRegion = 0;
        m_dwX = 0;
        m_dwY = 0;
        m_dwZ = 0;
        m_dw60 = 0;
        m_b41 = 0;
        m_dwAngle = 0;
        m_dwMastery1 = 0;
        m_dwMastery2 = 0;
    }

    // sPartyMemberInfo_Read (0x427b50): the mask byte, then the masked parts. With a region
    // of 0 or more (a world region) the coordinates are WORDs (stored in the low halves of the
    // cleared values), otherwise DWORDs.
    void Read(CMsg* pMsg);
    // sPartyMemberInfo_Write (0x427f10): the mirror of Read with the mask bMask.
    void Write(CMsg* pMsg, BYTE bMask) const;

    DWORD       m_dwID;             // +00  mask 0x10
    std::string m_strName;          // +04  mask 0x01
    std::string m_strGuildName;     // +20  mask 0x40
    DWORD       m_dwRefObjID;       // +3C  mask 0x01
    BYTE        m_bLevel;           // +40  mask 0x02
    BYTE        m_b41;              // +41  mask 0x80
    BYTE        m_bHPMP;            // +42  mask 0x04
    short       m_wRegion;          // +44  mask 0x20
    DWORD       m_dwX;              // +48  mask 0x20
    DWORD       m_dwY;              // +4C
    DWORD       m_dwZ;              // +50
    DWORD       m_dwAngle;          // +54  mask 0x20
    DWORD       m_dwMastery1;       // +58  mask 0x08
    DWORD       m_dwMastery2;       // +5C
    DWORD       m_dw60;             // +60  not serialised
};

C_ASSERT(FIELD_OFFSET(sPartyMemberInfo, m_strName) == 0x04);
C_ASSERT(FIELD_OFFSET(sPartyMemberInfo, m_strGuildName) == 0x20);
C_ASSERT(FIELD_OFFSET(sPartyMemberInfo, m_dwRefObjID) == 0x3C);
C_ASSERT(FIELD_OFFSET(sPartyMemberInfo, m_b41) == 0x41);
C_ASSERT(FIELD_OFFSET(sPartyMemberInfo, m_wRegion) == 0x44);
C_ASSERT(FIELD_OFFSET(sPartyMemberInfo, m_dwAngle) == 0x54);
C_ASSERT(FIELD_OFFSET(sPartyMemberInfo, m_dw60) == 0x60);
C_ASSERT(sizeof(sPartyMemberInfo) == 0x64);

// A party with its leader (0x74 bytes).
struct sPartyInfo
{
    // sPartyInfo_ctor (0x428510)
    sPartyInfo()
    {
        Reset();
    }

    // sPartyInfo_Reset (0x428820): setting 4, every value 0, the member cleared.
    void Reset()
    {
        m_dwPartyID = 0;
        m_dw04 = 0;
        m_dw08 = 0;
        m_bSetting = 4;
        m_Member.Clear();
    }

    // sPartyInfo_Write (0x427e00): the three DWORDs, the setting byte and the whole member.
    void Write(CMsg* pMsg) const;

    DWORD               m_dwPartyID;    // +00
    DWORD               m_dw04;         // +04
    DWORD               m_dw08;         // +08
    BYTE                m_bSetting;     // +0C
    sPartyMemberInfo    m_Member;       // +10
};

C_ASSERT(FIELD_OFFSET(sPartyInfo, m_Member) == 0x10);
C_ASSERT(sizeof(sPartyInfo) == 0x74);

// Party match settings (8 bytes).
struct sPartySetting
{
    // sPartySetting_Read (0x427d90)
    void Read(CMsg* pMsg);

    DWORD   m_dw00;     // +0
    BYTE    m_b04;      // +4
    BYTE    m_b05;      // +5
    WORD    m_w06;      // +6
};

C_ASSERT(sizeof(sPartySetting) == 8);
