#include "BSNet/Msg.h"
#include "BSNet/security/BlowFish.h"
#include "BSNet/security/KeyExchange.h"
#include "BSNet/security/MD5.h"
#include "BSNet/security/Security.h"
#include "BSNet/security/SecurityConstants.h"

#include <stdlib.h>
#include <string.h>

#include "TestCheck.h"

static void PutBE(BYTE* p, DWORD v)
{
    p[0] = (BYTE)(v >> 24);
    p[1] = (BYTE)(v >> 16);
    p[2] = (BYTE)(v >> 8);
    p[3] = (BYTE)v;
}

// Standard Blowfish vectors (key, plaintext, ciphertext as big-endian 64-bit values).
struct BF_VECTOR
{
    DWORD key[2];
    DWORD plain[2];
    DWORD cipher[2];
};

static const BF_VECTOR s_BlowFishVectors[] =
{
    { { 0x00000000, 0x00000000 }, { 0x00000000, 0x00000000 }, { 0x4EF99745, 0x6198DD78 } },
    { { 0xFFFFFFFF, 0xFFFFFFFF }, { 0xFFFFFFFF, 0xFFFFFFFF }, { 0x51866FD5, 0xB85ECB8A } },
    { { 0x30000000, 0x00000000 }, { 0x10000000, 0x00000001 }, { 0x7D856F9A, 0x613063F2 } },
    { { 0x11111111, 0x11111111 }, { 0x11111111, 0x11111111 }, { 0x2466DD87, 0x8B963C9D } },
    { { 0x01234567, 0x89ABCDEF }, { 0x11111111, 0x11111111 }, { 0x61F9C380, 0x2281B096 } },
    { { 0xFEDCBA98, 0x76543210 }, { 0x01234567, 0x89ABCDEF }, { 0x0ACEAB0F, 0xC6A0A28D } },
    { { 0x7CA11045, 0x4A1A6E57 }, { 0x01A1D6D0, 0x39776742 }, { 0x59C68245, 0xEB05282B } },
};

static void TestBlowFishVectors()
{
    for (int i = 0; i < sizeof(s_BlowFishVectors) / sizeof(s_BlowFishVectors[0]); ++i)
    {
        const BF_VECTOR& v = s_BlowFishVectors[i];
        BYTE key[8];
        PutBE(key, v.key[0]);
        PutBE(key + 4, v.key[1]);

        CBlowFish bf;
        bf.Initialize(key);
        DWORD L = v.plain[0];
        DWORD R = v.plain[1];
        bf.EncryptBlock(&L, &R);
        CHECK(L == v.cipher[0] && R == v.cipher[1]);
        bf.DecryptBlock(&L, &R);
        CHECK(L == v.plain[0] && R == v.plain[1]);
    }
}

static void TestBlowFishBuffers()
{
    BYTE key[8] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
    CBlowFish bf;
    bf.Initialize(key);

    // Words are taken in native byte order: the buffer 11 11 11 11 11 11 11 11 is the vector
    // plaintext, and the result is the vector ciphertext stored as two little-endian DWORDs.
    BYTE block[8];
    memset(block, 0x11, sizeof(block));
    CHECK(bf.Encrypt(block, block, 8) == 8);
    CHECK(((DWORD*)block)[0] == 0x61F9C380 && ((DWORD*)block)[1] == 0x2281B096);
    bf.Decrypt(block, block, 8);
    CHECK(((DWORD*)block)[0] == 0x11111111 && ((DWORD*)block)[1] == 0x11111111);

    CHECK(CBlowFish::AlignUp8(0) == 0);
    CHECK(CBlowFish::AlignUp8(1) == 8);
    CHECK(CBlowFish::AlignUp8(8) == 8);
    CHECK(CBlowFish::AlignUp8(13) == 16);

    // Copy mode: the 13-byte input is padded with zeros up to 16 bytes.
    BYTE src[32];
    BYTE dst[32];
    for (int i = 0; i < 32; ++i)
        src[i] = (BYTE)(i * 7 + 1);
    memset(dst, 0xCC, sizeof(dst));
    CHECK(bf.Encrypt(src, dst, 13) == 16);
    CHECK(dst[16] == 0xCC);
    BYTE plain[32];
    bf.Decrypt(dst, plain, 16);
    CHECK(memcmp(plain, src, 13) == 0);
    CHECK(plain[13] == 0 && plain[14] == 0 && plain[15] == 0);

    // In place gives the same bytes, and the tail of the buffer is zeroed first.
    BYTE inplace[32];
    memcpy(inplace, src, sizeof(inplace));
    CHECK(bf.Encrypt(inplace, inplace, 13) == 16);
    CHECK(memcmp(inplace, dst, 16) == 0);
    CHECK(inplace[16] == src[16]);
    bf.Decrypt(inplace, inplace, 13);
    CHECK(memcmp(inplace, src, 13) == 0);
    CHECK(inplace[13] == 0);

    // Length 7 still pads (the last-block test holds for dwLen >= 7).
    memset(dst, 0xCC, sizeof(dst));
    CHECK(bf.Encrypt(src, dst, 7) == 8);
    bf.Decrypt(dst, dst, 8);
    CHECK(memcmp(dst, src, 7) == 0 && dst[7] == 0);

    // SEC-001: below 7 bytes the block is copied whole from the source and not padded.
    memset(dst, 0xCC, sizeof(dst));
    CHECK(bf.Encrypt(src, dst, 5) == 8);
    bf.Decrypt(dst, dst, 8);
    CHECK(memcmp(dst, src, 8) == 0);

    memcpy(inplace, src, sizeof(inplace));
    CHECK(bf.Encrypt(inplace, inplace, 4) == 8);
    bf.Decrypt(inplace, inplace, 8);
    CHECK(memcmp(inplace, src, 8) == 0);

    // Zero length does nothing.
    memcpy(inplace, src, sizeof(inplace));
    CHECK(bf.Encrypt(inplace, inplace, 0) == 0);
    bf.Decrypt(inplace, inplace, 0);
    CHECK(memcmp(inplace, src, sizeof(src)) == 0);
}

static void CheckMD5(const char* psz, const char* pszExpected)
{
    CMD5 md5;
    CHECK(md5.GetHexDigest() == NULL);
    CHECK(md5.Compute(psz));
    CHECK(md5.GetHexDigest() != NULL && strcmp(md5.GetHexDigest(), pszExpected) == 0);
    CHECK(md5.m_pszInput == psz);
}

static void TestMD5()
{
    CheckMD5("", "d41d8cd98f00b204e9800998ecf8427e");
    CheckMD5("a", "0cc175b9c0f1b6a831c399e269772661");
    CheckMD5("abc", "900150983cd24fb0d6963f7d28e17f72");
    CheckMD5("message digest", "f96b697d7cb7938d525a2f31aaf161d0");
    CheckMD5("abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b");
    CheckMD5("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
             "d174ab98d277d9f5a5611c2c9f419d9f");
    CheckMD5("1234567890123456789012345678901234567890"
             "1234567890123456789012345678901234567890",
             "57edf4a22be3c955ac49da2e2107b67a");

    CMD5 md5;
    md5.Compute("abc");
    CHECK(md5.m_Digest[0] == 0x90 && md5.m_Digest[15] == 0x72);

    // Incremental updates across block boundaries give the same digest.
    char sz[200];
    for (int i = 0; i < 199; ++i)
        sz[i] = (char)('a' + i % 26);
    sz[199] = '\0';
    CMD5::CONTEXT ctx;
    CMD5::Init(&ctx);
    CMD5::Update(&ctx, 3, (const BYTE*)sz);
    CMD5::Update(&ctx, 70, (const BYTE*)sz + 3);
    CMD5::Update(&ctx, 126, (const BYTE*)sz + 73);
    BYTE digest[16];
    CMD5::Final(digest, &ctx);
    md5.Compute(sz);
    CHECK(memcmp(digest, md5.m_Digest, 16) == 0);
    CHECK(ctx.State[0] == 0 && ctx.Count[0] == 0);
}

// Bit-by-bit reference of the count seed generator.
static DWORD RefLFSR(DWORD v)
{
    for (int i = 0; i < 32; ++i)
    {
        DWORD bit = ((v >> 0) ^ (v >> 1) ^ (v >> 2) ^ (v >> 3) ^ (v >> 5) ^ (v >> 7)) & 1;
        DWORD rot = (v >> 1) | (v << 31);
        v = (rot & ~1u) | bit;
    }
    return v;
}

static void TestLFSR()
{
    DWORD seeds[] = { 0, 1, 0x80000000, 0x12345678, 0xDEADBEEF, 0xFFFFFFFF };
    for (int i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i)
    {
        DWORD v = seeds[i];
        Security_LFSR32(&v);
        CHECK(v == RefLFSR(seeds[i]));
    }
    DWORD z = 0;
    Security_LFSR32(&z);
    CHECK(z == 0);
}

static void ExpectedCounts(DWORD dwSeed, BYTE* pCount, BYTE* pMul, BYTE* pAdd)
{
    DWORD r1 = RefLFSR(dwSeed);
    DWORD r2 = RefLFSR(r1);
    DWORD r3 = RefLFSR(r2);
    DWORD r4 = RefLFSR(r3);
    BYTE bMul = (BYTE)(r1 ^ r2);
    BYTE bAdd = (BYTE)(r3 ^ r4);
    if (!bMul)
        bMul = 1;
    if (!bAdd)
        bAdd = 1;
    *pMul = bMul;
    *pAdd = bAdd;
    *pCount = bMul ^ bAdd;
}

static void TestHandshakeFlags()
{
    SECURITY_STATE st;
    st.bCount = 0xAA;
    st.bCountMul = 0xBB;
    st.bCountAdd = 0xCC;
    st.Crc.dwSeed = 77;
    st.Crc.dwValue = 88;

    // Option 1 only records the options.
    SECURITY_HANDSHAKE_PARAM hp;
    memset(&hp, 0, sizeof(hp));
    hp.bOptions = SECURITY_OPT_NONE | SECURITY_OPT_COUNT_CRC;
    hp.dwCountSeed = 0x1234;
    Security_ApplyHandshakeFlags(&st, &hp);
    CHECK(st.bOptions == (SECURITY_OPT_NONE | SECURITY_OPT_COUNT_CRC));
    CHECK(st.bCount == 0xAA && st.bCountMul == 0xBB && st.Crc.dwSeed == 77);

    hp.bOptions = SECURITY_OPT_COUNT_CRC;
    hp.dwCountSeed = 0x0BADF00D;
    hp.dwCrcSeed = 0x1234;
    Security_ApplyHandshakeFlags(&st, &hp);
    BYTE c, m, a;
    ExpectedCounts(0x0BADF00D, &c, &m, &a);
    CHECK(st.bCount == c && st.bCountMul == m && st.bCountAdd == a);
    CHECK(st.Crc.dwSeed == 0x34 && st.Crc.dwValue == 0xFFFFFFFF);
    CHECK(st.bOptions == SECURITY_OPT_COUNT_CRC);

    // CRC seed byte 0 becomes 1; count seed 0 takes the built-in default.
    hp.dwCrcSeed = 0x100;
    hp.dwCountSeed = 0;
    Security_ApplyHandshakeFlags(&st, &hp);
    CHECK(st.Crc.dwSeed == 1);
    SECURITY_STATE st2;
    hp.dwCountSeed = g_dwSecurityDefaultCountSeed;
    Security_ApplyHandshakeFlags(&st2, &hp);
    CHECK(st.bCount == st2.bCount && st.bCountMul == st2.bCountMul
          && st.bCountAdd == st2.bCountAdd);
    CHECK(st.bCountMul != 0 && st.bCountAdd != 0);

    // Blowfish option keys the session cipher.
    hp.bOptions = SECURITY_OPT_BLOWFISH;
    for (int i = 0; i < 8; ++i)
        hp.BlowFishKey[i] = (BYTE)(0x10 * i + 3);
    Security_ApplyHandshakeFlags(&st, &hp);
    CBlowFish ref;
    ref.Initialize(hp.BlowFishKey);
    DWORD L1 = 1, R1 = 2, L2 = 1, R2 = 2;
    st.BlowFish.EncryptBlock(&L1, &R1);
    ref.EncryptBlock(&L2, &R2);
    CHECK(L1 == L2 && R1 == R2);
    CHECK(memcmp(st.BlowFish.m_pS, ref.m_pS, CBlowFish::S_COUNT * sizeof(DWORD)) == 0);
}

static void TestCountByte()
{
    SECURITY_STATE a;
    SECURITY_STATE b;
    a.bCount = b.bCount = 0x5A;
    a.bCountMul = b.bCountMul = 0x33;
    a.bCountAdd = b.bCountAdd = 0x91;

    BYTE x = 0x5A;
    for (int i = 0; i < 1000; ++i)
    {
        BYTE y = (BYTE)(((BYTE)~x + 0x33) * 0x91);
        y = (BYTE)(y ^ (y >> 4));
        x = y;
        CHECK(Security_NextCountByte(&a) == x);
        CHECK(a.bCount == x);
        CHECK(Security_NextCountByte(&b) == x);
    }
}

static DWORD s_TestSeeds[256];

static void InitTestCrcTable()
{
    // Row 1 is the standard reflected CRC-32 polynomial, the others are arbitrary.
    for (int i = 0; i < 256; ++i)
        s_TestSeeds[i] = 0x9E3779B9u * (DWORD)(i + 1) | 0x80000001u;
    s_TestSeeds[1] = 0xEDB88320;
    Security_BuildCrcTableFrom(s_TestSeeds);
}

static void TestCrc()
{
    g_bSecurityCrcTableBuilt = FALSE;
    InitTestCrcTable();
    CHECK(g_bSecurityCrcTableBuilt);
    CHECK(g_SecurityCrcTable[256 + 1] == 0x77073096);
    CHECK(g_SecurityCrcTable[256 + 255] == 0x2D02EF8D);
    CHECK(g_SecurityCrcTable[0] == 0 && g_SecurityCrcTable[256 * 5] == 0);

    // With seed 1 the running value is the complement of CRC-32("123456789") = 0xCBF43926.
    SECURITY_CRC crc;
    Security_InitCrcSeed(1, &crc);
    CHECK(crc.dwSeed == 1 && crc.dwValue == 0xFFFFFFFF);
    BYTE b = Security_ComputeCrcByte((const BYTE*)"123456789", &crc, 9);
    CHECK(crc.dwValue == 0x340BC6D9);
    CHECK(b == (BYTE)(0xD9 + 0xC6 + 0x0B + 0x34));

    // Each call starts over.
    CHECK(Security_ComputeCrcByte((const BYTE*)"123456789", &crc, 9) == b);

    // NULL returns 0 without touching the value; length 0 leaves 0xFFFFFFFF.
    CHECK(Security_ComputeCrcByte(NULL, &crc, 9) == 0);
    CHECK(crc.dwValue == 0x340BC6D9);
    CHECK(Security_ComputeCrcByte((const BYTE*)"x", &crc, 0) == (BYTE)(4 * 0xFF));
    CHECK(crc.dwValue == 0xFFFFFFFF);

    // Another seed selects another row.
    Security_InitCrcSeed(2, &crc);
    Security_ComputeCrcByte((const BYTE*)"123456789", &crc, 9);
    CHECK(crc.dwValue != 0x340BC6D9);
}

static void TestModeCheck()
{
    const BYTE B = SECURITY_HS_BLOWFISH, C = SECURITY_HS_COUNT_CRC;
    const BYTE K = SECURITY_HS_KEY_EXCHANGE, H = SECURITY_HS_CHALLENGE;

    CHECK(Security_ModeCheck(SECURITY_MODE_BASIC, B | C));
    CHECK(Security_ModeCheck(SECURITY_MODE_BASIC, B | C | K));
    CHECK(!Security_ModeCheck(SECURITY_MODE_BASIC, B | C | K | H));
    CHECK(!Security_ModeCheck(SECURITY_MODE_BASIC, B));
    CHECK(!Security_ModeCheck(SECURITY_MODE_BASIC, C));

    CHECK(Security_ModeCheck(SECURITY_MODE_KEY_EXCHANGE, B | C | K | H));
    CHECK(!Security_ModeCheck(SECURITY_MODE_KEY_EXCHANGE, B | C | K));
    CHECK(!Security_ModeCheck(SECURITY_MODE_KEY_EXCHANGE, B | C));

    CHECK(Security_ModeCheck(SECURITY_MODE_BASIC | SECURITY_MODE_KEY_EXCHANGE, B | C));
    CHECK(Security_ModeCheck(SECURITY_MODE_BASIC | SECURITY_MODE_KEY_EXCHANGE, B | C | K | H));
    CHECK(!Security_ModeCheck(0, B | C | K | H));
}

static void TestModPowAndPrimes()
{
    CHECK(KeyExchange_ModPow(4, 13, 497) == 445);
    CHECK(KeyExchange_ModPow(7, 0, 13) == 1);
    CHECK(KeyExchange_ModPow(0, 5, 13) == 0);
    CHECK(KeyExchange_ModPow(2, 10, 1000) == 24);
    // Large operands need the 64-bit products: 3^(p-1) mod p for p = 2^31 - 1.
    CHECK(KeyExchange_ModPow(3, 0x7FFFFFFE, 0x7FFFFFFF) == 1);
    CHECK(KeyExchange_ModPow(0xFFFFFFFF, 2, 0xFFFFFFFB) == 16);

    CHECK(KeyExchange_FermatWitness(13, 2));
    CHECK(!KeyExchange_FermatWitness(15, 2));

    CHECK(KeyExchange_IsPrime(0x7FFFFFFF));
    CHECK(KeyExchange_IsPrime(1000000007));
    CHECK(!KeyExchange_IsPrime(1000000005));
    CHECK(!KeyExchange_IsPrime(0x7FFFFFFD));
    // SEC-002: 1 is reported as prime.
    CHECK(KeyExchange_IsPrime(1));

    for (int i = 0; i < 4; ++i)
    {
        DWORD p = KeyExchange_RandomPrime();
        CHECK((p & 1) == 1);
        CHECK(KeyExchange_IsPrime(p));
    }
}

static void TestTransformKey()
{
    BYTE key[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    CKeyExchangeBase::TransformKey(key, 3, 0x40302010);
    BYTE expected[8];
    const BYTE add[4] = { 0x10, 0x20, 0x30, 0x40 };
    for (int i = 0; i < 8; ++i)
        expected[i] = (BYTE)(i ^ (BYTE)(i + add[i % 4] + 3));
    CHECK(memcmp(key, expected, 8) == 0);
}

static void TestKeyExchange()
{
    CKeySender server;
    CKeyRepicient client;
    CHECK(server.m_dwSecret == 0 && server.m_DH.dwPrime == 0);

    KEYEXCHANGE_SERVER_PARAM sp;
    server.GenerateServerParams(&sp);
    CHECK(sp.dwGenerator == server.m_dwGenerator && sp.dwPrime == server.m_dwPrime);
    CHECK(sp.dwGenerator <= sp.dwPrime);
    CHECK(KeyExchange_IsPrime(sp.dwGenerator) && KeyExchange_IsPrime(sp.dwPrime));
    CHECK(memcmp(sp.InitialKey, server.m_InitialKey, 8) == 0);
    CHECK(sp.dwServerPublic == KeyExchange_ModPow(sp.dwGenerator, server.m_DH.dwServerPrivate,
                                                  sp.dwPrime));

    client.AcceptServerParams(&sp);
    CHECK(client.m_dwServerPublic == sp.dwServerPublic);
    CHECK(client.m_DH.dwGenerator == sp.dwGenerator && client.m_DH.dwPrime == sp.dwPrime);

    KEYEXCHANGE_CHALLENGE ch;
    client.MakeResponse(&ch);
    CHECK(ch.dwClientPublic == client.m_dwClientPublic);

    KEYEXCHANGE_CHALLENGE bad = ch;
    bad.Proof[3] ^= 0x40;
    CHECK(server.VerifyChallenge(&ch));
    CHECK(server.m_dwSecret == client.m_dwSecret);
    CHECK(server.m_DH.dwSecret == client.m_DH.dwSecret);
    CHECK(!server.VerifyChallenge(&bad));
    CHECK(server.VerifyChallenge(&ch));

    BYTE proof[8];
    server.MakeChallenge(proof);
    CHECK(client.CheckPeerValue(proof));
    proof[0] ^= 1;
    CHECK(!client.CheckPeerValue(proof));

    BYTE k1[8], k2[8], k3[8];
    server.GetFinalKey(k1);
    client.GetFinalKey(k2);
    CHECK(memcmp(k1, k2, 8) == 0);
    memcpy(k3, server.m_InitialKey, 8);
    CKeyExchangeBase::TransformKey(k3, 3, server.m_dwSecret);
    CHECK(memcmp(k1, k3, 8) == 0);
}

// Header stamping and payload encryption as the stream socket does it on send and receive.
static void SendStamp(SECURITY_STATE* pState, CMsg* pMsg, CMsg* pOut)
{
    // The encrypted flag is set when the message is allocated, so the CRC covers it.
    pMsg->SetEncryptFlag();
    WORD wTotal = (WORD)(pMsg->GetPayloadSize() + MSG_HEADER_SIZE);
    pMsg->SetCountByte(Security_NextCountByte(pState));
    pMsg->SetCrcByte(0);
    pMsg->SetCrcByte(Security_ComputeCrcByte(pMsg->m_pData, &pState->Crc, wTotal));

    DWORD dwLen = pMsg->GetPayloadSize() + 4;
    pState->BlowFish.Encrypt(pMsg->m_pData + 2, pOut->m_pData + 2, dwLen);
    *(WORD*)pOut->m_pData = *(WORD*)pMsg->m_pData;
}

static bool ReceiveCheck(SECURITY_STATE* pState, CMsg* pMsg)
{
    DWORD dwLen = CBlowFish::AlignUp8(pMsg->GetPayloadSize() + 4);
    pState->BlowFish.Decrypt(pMsg->m_pData + 2, pMsg->m_pData + 2, dwLen);
    if (pMsg->GetCountByte() != Security_NextCountByte(pState))
        return false;
    BYTE bCrc = pMsg->GetCrcByte();
    pMsg->SetCrcByte(0);
    WORD wTotal = (WORD)(pMsg->GetPayloadSize() + MSG_HEADER_SIZE);
    return bCrc == Security_ComputeCrcByte(pMsg->m_pData, &pState->Crc, wTotal);
}

static void TestMsgRoundTrip()
{
    InitTestCrcTable();

    SECURITY_HANDSHAKE_PARAM hp;
    memset(&hp, 0, sizeof(hp));
    hp.bOptions = SECURITY_OPT_BLOWFISH | SECURITY_OPT_COUNT_CRC;
    hp.dwCountSeed = 0x2468ACE1;
    hp.dwCrcSeed = 0x77;
    for (int i = 0; i < 8; ++i)
        hp.BlowFishKey[i] = (BYTE)(0xA0 + i);

    SECURITY_STATE sender;
    SECURITY_STATE receiver;
    Security_ApplyHandshakeFlags(&sender, &hp);
    Security_ApplyHandshakeFlags(&receiver, &hp);

    for (int nMsg = 0; nMsg < 20; ++nMsg)
    {
        CMsg msg;
        msg.Reset();
        msg.SetOpcode((WORD)(0x7000 + nMsg));
        for (int i = 0; i < nMsg * 3; ++i)
            msg << (BYTE)(i * 13 + nMsg);

        CMsg out;
        out.Reset();
        SendStamp(&sender, &msg, &out);
        WORD wPayload = msg.GetPayloadSize();
        CHECK(out.GetPayloadSize() == wPayload);
        CHECK((*(WORD*)out.m_pData & MSG_ENCRYPTED) != 0);
        if (wPayload >= 4)
            CHECK(memcmp(out.m_pData + 6, msg.m_pData + 6, wPayload) != 0);

        CHECK(ReceiveCheck(&receiver, &out));
        CHECK(out.GetOpcode() == 0x7000 + nMsg);
        CHECK(memcmp(out.m_pData + 6, msg.m_pData + 6, wPayload) == 0);
    }

    // A changed byte fails the CRC; a replayed packet fails the count.
    CMsg msg;
    msg.Reset();
    msg.SetOpcode(0x7100);
    msg << (DWORD)0x01020304 << (DWORD)0x05060708;
    CMsg out;
    out.Reset();
    SendStamp(&sender, &msg, &out);
    BYTE wire[64];
    memcpy(wire, out.m_pData, sizeof(wire));
    CMsg copy;
    copy.Reset();
    memcpy(copy.m_pData, wire, sizeof(wire));
    copy.m_pData[6] ^= 0x01;

    SECURITY_STATE saved;
    saved.bCount = receiver.bCount;
    saved.bCountMul = receiver.bCountMul;
    saved.bCountAdd = receiver.bCountAdd;
    saved.Crc = receiver.Crc;
    memcpy(saved.BlowFish.m_pP, receiver.BlowFish.m_pP, CBlowFish::P_COUNT * sizeof(DWORD));
    memcpy(saved.BlowFish.m_pS, receiver.BlowFish.m_pS, CBlowFish::S_COUNT * sizeof(DWORD));

    CHECK(!ReceiveCheck(&saved, &copy));
    CHECK(ReceiveCheck(&receiver, &out));
    memcpy(copy.m_pData, wire, sizeof(wire));
    CHECK(!ReceiveCheck(&receiver, &copy));
}

static void TestLayouts()
{
    CHECK(sizeof(CBlowFish) == 12);
    CHECK(sizeof(CKeyExchangeBase) == 72);
    CHECK(sizeof(SECURITY_STATE) == 24);
    CHECK(sizeof(CMD5) == 60);

    CMD5 md5;
    CHECK(md5.m_bValid == FALSE && md5.m_szHexDigest[32] == '\0');
}

int main()
{
    TestBlowFishVectors();
    TestBlowFishBuffers();
    TestMD5();
    TestLFSR();
    TestHandshakeFlags();
    TestCountByte();
    TestCrc();
    TestModeCheck();
    TestModPowAndPrimes();
    TestTransformKey();
    TestKeyExchange();
    TestMsgRoundTrip();
    TestLayouts();
    return TEST_RESULT("SecurityTest");
}
