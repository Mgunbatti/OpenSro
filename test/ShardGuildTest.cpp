// SR_ShardManager social part 1: party info serialisation.

#include <winsock2.h>
#include <windows.h>

#include "BSNet/Msg.h"
#include "ServerCommon/PartyInfo.h"

#include "TestCheck.h"

static void ResetForRead(CMsg& msg)
{
    msg.m_wReadPos = MSG_HEADER_SIZE;
    msg.m_bReadForward = TRUE;
}

static void TestPartyInfo()
{
    sPartyMemberInfo info;
    CHECK(info.m_bLevel == 0 && info.m_wRegion == 0 && info.m_strName.empty());
    info.m_dwID = 0x11223344;
    info.m_strName = "Hero";
    info.m_dwRefObjID = 1907;
    info.m_bLevel = 90;
    info.m_b41 = 7;
    info.m_bHPMP = 0xAA;
    info.m_wRegion = 25000;
    info.m_dwX = 0x12345;          // world region: only the low WORDs are sent
    info.m_dwY = 0x20;
    info.m_dwZ = 0x30;
    info.m_dwAngle = 0x4000;
    info.m_strGuildName = "Guild";
    info.m_dwMastery1 = 257;
    info.m_dwMastery2 = 258;

    CMsg msg;
    msg.Reset();
    info.Write(&msg, PARTY_MEMBER_ALL);
    // mask 1, ID 4, name 2+4, RefObjID 4, level 1, HP/MP 1, region 2 + 3 WORDs, angle 4,
    // guild 2+5, byte 1, masteries 8
    CHECK(msg.GetPayloadSize() == 1 + 4 + 6 + 4 + 1 + 1 + 8 + 4 + 7 + 1 + 8);
    BYTE* p = msg.m_pData + MSG_HEADER_SIZE;
    CHECK(p[0] == 0xFF);
    CHECK(*(DWORD*)(p + 1) == 0x11223344);
    CHECK(*(WORD*)(p + 5) == 4 && memcmp(p + 7, "Hero", 4) == 0);
    CHECK(*(WORD*)(p + 17) == 25000 && *(WORD*)(p + 19) == 0x2345);

    ResetForRead(msg);
    sPartyMemberInfo copy;
    copy.m_dwX = 0xFFFFFFFF;
    copy.Read(&msg);
    CHECK(copy.m_dwID == 0x11223344 && copy.m_strName == "Hero" && copy.m_dwRefObjID == 1907);
    CHECK(copy.m_bLevel == 90 && copy.m_b41 == 7 && copy.m_bHPMP == 0xAA);
    CHECK(copy.m_wRegion == 25000 && copy.m_dwX == 0x2345 && copy.m_dwZ == 0x30);
    CHECK(copy.m_dwAngle == 0x4000 && copy.m_strGuildName == "Guild");
    CHECK(copy.m_dwMastery1 == 257 && copy.m_dwMastery2 == 258);
    CHECK(msg.GetUnreadSize() == 0);

    // dungeon region: DWORD coordinates, only the masked parts
    CMsg msg2;
    msg2.Reset();
    info.m_wRegion = -32000;
    info.Write(&msg2, PARTY_MEMBER_POSITION);
    CHECK(msg2.GetPayloadSize() == 1 + 2 + 12 + 4);
    ResetForRead(msg2);
    sPartyMemberInfo copy2;
    copy2.Read(&msg2);
    CHECK(copy2.m_wRegion == -32000 && copy2.m_dwX == 0x12345 && copy2.m_strName.empty());

    sPartyInfo party;
    CHECK(party.m_bSetting == 4 && party.m_dwPartyID == 0);
    party.m_dwPartyID = 9;
    party.m_Member.m_strName = "x";
    party.m_Member.m_strGuildName = "g";
    party.Reset();
    CHECK(party.m_dwPartyID == 0 && party.m_Member.m_strName.empty());
    CHECK(party.m_Member.m_strGuildName == "g");     // Reset keeps the guild name
    CMsg msg3;
    msg3.Reset();
    party.Write(&msg3);
    CHECK(msg3.GetPayloadSize() == 13 + 1 + 4 + 2 + 4 + 1 + 1 + 8 + 4 + 3 + 1 + 8);

    CMsg msg4;
    msg4.Reset();
    DWORD dw = 5;
    BYTE b1 = 1, b2 = 2;
    WORD w = 0x300;
    msg4.WriteBytes(&dw, 4);
    msg4.WriteBytes(&b1, 1);
    msg4.WriteBytes(&b2, 1);
    msg4.WriteBytes(&w, 2);
    ResetForRead(msg4);
    sPartySetting setting;
    setting.Read(&msg4);
    CHECK(setting.m_dw00 == 5 && setting.m_b04 == 1 && setting.m_b05 == 2 &&
          setting.m_w06 == 0x300);
}

int main()
{
    TestPartyInfo();
    return TEST_RESULT("ShardGuildTest");
}
