#include "BSNet/Msg.h"

#include <stdio.h>
#include <string.h>

static int g_nFailed = 0;

#define CHECK(expr)                                                         \
    do                                                                      \
    {                                                                       \
        if (!(expr))                                                        \
        {                                                                   \
            printf("FAILED line %d: %s\n", __LINE__, #expr);                \
            ++g_nFailed;                                                    \
        }                                                                   \
    } while (0)

static void TestHeader()
{
    CMsg msg;
    msg.Reset();
    CHECK(msg.m_wReadPos == 6 && msg.m_wWritePos == 6);
    CHECK(msg.m_pData == msg.m_InlineBuf && msg.m_nCapacity == 4096);

    msg.SetOpcode(0x6301);
    msg.SetCountByte(0x12);
    msg.SetCrcByte(0x34);
    msg.SetEncryptFlag();
    CHECK(*(WORD*)(msg.m_pData + 2) == 0x6301);
    CHECK(msg.m_pData[4] == 0x12 && msg.m_pData[5] == 0x34);
    CHECK(*(WORD*)msg.m_pData == 0x8000);

    msg << (DWORD)0xAABBCCDD;
    CHECK(*(WORD*)msg.m_pData == (0x8000 | 4));
    msg.ClearEncryptFlag();
    CHECK(msg.GetPayloadSize() == 4);
}

static void TestForwardRead()
{
    CMsg msg;
    msg.Reset();
    msg << (BYTE)1 << (WORD)0x0203 << (DWORD)0x04050607 << (unsigned __int64)0x08090A0B0C0D0E0FULL;
    CHECK(msg.GetPayloadSize() == 15);

    BYTE b;
    WORD w;
    DWORD dw;
    unsigned __int64 q;
    msg >> b >> w >> dw >> q;
    CHECK(b == 1 && w == 0x0203 && dw == 0x04050607 && q == 0x08090A0B0C0D0E0FULL);
    CHECK(msg.GetUnreadSize() == 0);
}

static void TestPopBack()
{
    CMsg msg;
    msg.Reset();
    msg << (DWORD)0x11111111 << (WORD)0x2222 << (BYTE)0x33;
    msg.m_bReadForward = 0;

    BYTE b;
    WORD w;
    DWORD dw;
    msg >> b;
    CHECK(b == 0x33 && msg.GetPayloadSize() == 6);
    msg >> w;
    CHECK(w == 0x2222 && msg.GetPayloadSize() == 4);
    msg >> dw;
    CHECK(dw == 0x11111111 && msg.GetPayloadSize() == 0);
}

static void TestStrings()
{
    CMsg msg;
    msg.Reset();
    msg << "SR_GameServer" << std::string("abc") << L"wide";

    std::string s1, s2;
    std::wstring ws;
    msg >> s1 >> s2 >> ws;
    CHECK(s1 == "SR_GameServer" && s2 == "abc" && ws == L"wide");

    // a length larger than what is left is clamped to the unread bytes
    CMsg bad;
    bad.Reset();
    WORD wFakeLen = 100;
    bad.WriteBytes(&wFakeLen, 2);
    bad.WriteBytes("xyz", 3);
    std::string s3;
    bad >> s3;
    CHECK(s3 == "xyz");
}

static void TestOverflowThrows()
{
    CMsg msg;
    msg.Reset();
    static BYTE big[5000];
    int nThrown = -1;
    try
    {
        msg.WriteBytes(big, 4091);
    }
    catch (int n)
    {
        nThrown = n;
    }
    CHECK(nThrown == 0);
    CHECK(msg.m_wWritePos == 6);

    nThrown = -1;
    try
    {
        msg.WriteBytes(big, 4090);
    }
    catch (int n)
    {
        nThrown = n;
    }
    CHECK(nThrown == -1 && msg.m_wWritePos == 4096);
}

static void TestLargeBuffer()
{
    CMsg msg;
    msg.Reset();
    msg.SetOpcode(0x600D);
    msg << (DWORD)0x12345678;
    msg.AllocLargeBuffer(8000);
    CHECK(msg.m_pData == msg.m_pExtBuf && msg.m_nCapacity == 8000);
    CHECK(msg.GetOpcode() == 0x600D && msg.GetPayloadSize() == 4);

    msg.Reset();
    CHECK(msg.m_pData == msg.m_InlineBuf && msg.m_pExtBuf == NULL && msg.GetOpcode() == 0);
}

static void TestDebugString()
{
    CMsg msg;
    msg.Reset();
    msg.SetOpcode(0x6301 | MSG_OP_REQ);
    msg << (DWORD)1;
    char sz[256];
    msg.FormatDebugString(sz);
    CHECK(strcmp(sz, "Req:0x301 (6), 4 - 4") == 0);
}

int main()
{
    TestHeader();
    TestForwardRead();
    TestPopBack();
    TestStrings();
    TestOverflowThrows();
    TestLargeBuffer();
    TestDebugString();
    printf(g_nFailed ? "MsgTest: %d failed\n" : "MsgTest: all passed\n", g_nFailed);
    return g_nFailed ? 1 : 0;
}
