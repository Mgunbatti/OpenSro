#include "MD5.h"

#include <stdio.h>
#include <string.h>

namespace
{

// Padding appended by Final: 0x80 followed by zeros.
const BYTE s_Padding[64] = { 0x80 };

inline DWORD RotateLeft(DWORD x, int n)
{
    return (x << n) | (x >> (32 - n));
}

inline DWORD FuncF(DWORD x, DWORD y, DWORD z) { return (x & y) | (~x & z); }
inline DWORD FuncG(DWORD x, DWORD y, DWORD z) { return (x & z) | (y & ~z); }
inline DWORD FuncH(DWORD x, DWORD y, DWORD z) { return x ^ y ^ z; }
inline DWORD FuncI(DWORD x, DWORD y, DWORD z) { return y ^ (x | ~z); }

#define MD5_STEP(func, a, b, c, d, x, s, t)                                 \
    (a) += func((b), (c), (d)) + (x) + (DWORD)(t);                          \
    (a) = RotateLeft((a), (s)) + (b)

} // namespace

BOOLEAN CMD5::ComputeHexDigest()
{
    CONTEXT ctx;
    Init(&ctx);
    Update(&ctx, (DWORD)strlen(m_pszInput), (const BYTE*)m_pszInput);
    Final(m_Digest, &ctx);

    for (int i = 0; i < 16; ++i)
    {
        if (sprintf_s(m_szHexDigest + 2 * i, 33 - 2 * i, "%02x", m_Digest[i]) < 0)
            return FALSE;
    }
    return TRUE;
}

void CMD5::Init(CONTEXT* pCtx)
{
    pCtx->Count[1] = 0;
    pCtx->Count[0] = 0;
    pCtx->State[0] = 0x67452301;
    pCtx->State[1] = 0xEFCDAB89;
    pCtx->State[2] = 0x98BADCFE;
    pCtx->State[3] = 0x10325476;
}

void CMD5::Update(CONTEXT* pCtx, DWORD dwLen, const BYTE* pInput)
{
    DWORD dwIndex = (pCtx->Count[0] >> 3) & 0x3F;
    pCtx->Count[0] += dwLen << 3;
    if (pCtx->Count[0] < (dwLen << 3))
        ++pCtx->Count[1];
    pCtx->Count[1] += dwLen >> 29;

    DWORD dwPart = 64 - dwIndex;
    DWORD i;
    if (dwLen >= dwPart)
    {
        MemCopy(pCtx->Buffer + dwIndex, dwPart, pInput);
        Transform(pCtx->Buffer, pCtx->State);
        for (i = dwPart; i + 63 < dwLen; i += 64)
            Transform(pInput + i, pCtx->State);
        dwIndex = 0;
    }
    else
    {
        i = 0;
    }
    MemCopy(pCtx->Buffer + dwIndex, dwLen - i, pInput + i);
}

void CMD5::Final(BYTE* pDigest, CONTEXT* pCtx)
{
    BYTE bits[8];
    Encode(pCtx->Count, 8, bits);

    DWORD dwIndex = (pCtx->Count[0] >> 3) & 0x3F;
    DWORD dwPadLen = (dwIndex < 56) ? (56 - dwIndex) : (120 - dwIndex);
    Update(pCtx, dwPadLen, s_Padding);
    Update(pCtx, 8, bits);
    Encode(pCtx->State, 16, pDigest);
    memset(pCtx, 0, sizeof(*pCtx));
}

void CMD5::Transform(const BYTE* pBlock, DWORD* pState)
{
    DWORD a = pState[0];
    DWORD b = pState[1];
    DWORD c = pState[2];
    DWORD d = pState[3];
    DWORD x[16];
    Decode(pBlock, x);

    MD5_STEP(FuncF, a, b, c, d, x[ 0],  7, 0xD76AA478);
    MD5_STEP(FuncF, d, a, b, c, x[ 1], 12, 0xE8C7B756);
    MD5_STEP(FuncF, c, d, a, b, x[ 2], 17, 0x242070DB);
    MD5_STEP(FuncF, b, c, d, a, x[ 3], 22, 0xC1BDCEEE);
    MD5_STEP(FuncF, a, b, c, d, x[ 4],  7, 0xF57C0FAF);
    MD5_STEP(FuncF, d, a, b, c, x[ 5], 12, 0x4787C62A);
    MD5_STEP(FuncF, c, d, a, b, x[ 6], 17, 0xA8304613);
    MD5_STEP(FuncF, b, c, d, a, x[ 7], 22, 0xFD469501);
    MD5_STEP(FuncF, a, b, c, d, x[ 8],  7, 0x698098D8);
    MD5_STEP(FuncF, d, a, b, c, x[ 9], 12, 0x8B44F7AF);
    MD5_STEP(FuncF, c, d, a, b, x[10], 17, 0xFFFF5BB1);
    MD5_STEP(FuncF, b, c, d, a, x[11], 22, 0x895CD7BE);
    MD5_STEP(FuncF, a, b, c, d, x[12],  7, 0x6B901122);
    MD5_STEP(FuncF, d, a, b, c, x[13], 12, 0xFD987193);
    MD5_STEP(FuncF, c, d, a, b, x[14], 17, 0xA679438E);
    MD5_STEP(FuncF, b, c, d, a, x[15], 22, 0x49B40821);

    MD5_STEP(FuncG, a, b, c, d, x[ 1],  5, 0xF61E2562);
    MD5_STEP(FuncG, d, a, b, c, x[ 6],  9, 0xC040B340);
    MD5_STEP(FuncG, c, d, a, b, x[11], 14, 0x265E5A51);
    MD5_STEP(FuncG, b, c, d, a, x[ 0], 20, 0xE9B6C7AA);
    MD5_STEP(FuncG, a, b, c, d, x[ 5],  5, 0xD62F105D);
    MD5_STEP(FuncG, d, a, b, c, x[10],  9, 0x02441453);
    MD5_STEP(FuncG, c, d, a, b, x[15], 14, 0xD8A1E681);
    MD5_STEP(FuncG, b, c, d, a, x[ 4], 20, 0xE7D3FBC8);
    MD5_STEP(FuncG, a, b, c, d, x[ 9],  5, 0x21E1CDE6);
    MD5_STEP(FuncG, d, a, b, c, x[14],  9, 0xC33707D6);
    MD5_STEP(FuncG, c, d, a, b, x[ 3], 14, 0xF4D50D87);
    MD5_STEP(FuncG, b, c, d, a, x[ 8], 20, 0x455A14ED);
    MD5_STEP(FuncG, a, b, c, d, x[13],  5, 0xA9E3E905);
    MD5_STEP(FuncG, d, a, b, c, x[ 2],  9, 0xFCEFA3F8);
    MD5_STEP(FuncG, c, d, a, b, x[ 7], 14, 0x676F02D9);
    MD5_STEP(FuncG, b, c, d, a, x[12], 20, 0x8D2A4C8A);

    MD5_STEP(FuncH, a, b, c, d, x[ 5],  4, 0xFFFA3942);
    MD5_STEP(FuncH, d, a, b, c, x[ 8], 11, 0x8771F681);
    MD5_STEP(FuncH, c, d, a, b, x[11], 16, 0x6D9D6122);
    MD5_STEP(FuncH, b, c, d, a, x[14], 23, 0xFDE5380C);
    MD5_STEP(FuncH, a, b, c, d, x[ 1],  4, 0xA4BEEA44);
    MD5_STEP(FuncH, d, a, b, c, x[ 4], 11, 0x4BDECFA9);
    MD5_STEP(FuncH, c, d, a, b, x[ 7], 16, 0xF6BB4B60);
    MD5_STEP(FuncH, b, c, d, a, x[10], 23, 0xBEBFBC70);
    MD5_STEP(FuncH, a, b, c, d, x[13],  4, 0x289B7EC6);
    MD5_STEP(FuncH, d, a, b, c, x[ 0], 11, 0xEAA127FA);
    MD5_STEP(FuncH, c, d, a, b, x[ 3], 16, 0xD4EF3085);
    MD5_STEP(FuncH, b, c, d, a, x[ 6], 23, 0x04881D05);
    MD5_STEP(FuncH, a, b, c, d, x[ 9],  4, 0xD9D4D039);
    MD5_STEP(FuncH, d, a, b, c, x[12], 11, 0xE6DB99E5);
    MD5_STEP(FuncH, c, d, a, b, x[15], 16, 0x1FA27CF8);
    MD5_STEP(FuncH, b, c, d, a, x[ 2], 23, 0xC4AC5665);

    MD5_STEP(FuncI, a, b, c, d, x[ 0],  6, 0xF4292244);
    MD5_STEP(FuncI, d, a, b, c, x[ 7], 10, 0x432AFF97);
    MD5_STEP(FuncI, c, d, a, b, x[14], 15, 0xAB9423A7);
    MD5_STEP(FuncI, b, c, d, a, x[ 5], 21, 0xFC93A039);
    MD5_STEP(FuncI, a, b, c, d, x[12],  6, 0x655B59C3);
    MD5_STEP(FuncI, d, a, b, c, x[ 3], 10, 0x8F0CCC92);
    MD5_STEP(FuncI, c, d, a, b, x[10], 15, 0xFFEFF47D);
    MD5_STEP(FuncI, b, c, d, a, x[ 1], 21, 0x85845DD1);
    MD5_STEP(FuncI, a, b, c, d, x[ 8],  6, 0x6FA87E4F);
    MD5_STEP(FuncI, d, a, b, c, x[15], 10, 0xFE2CE6E0);
    MD5_STEP(FuncI, c, d, a, b, x[ 6], 15, 0xA3014314);
    MD5_STEP(FuncI, b, c, d, a, x[13], 21, 0x4E0811A1);
    MD5_STEP(FuncI, a, b, c, d, x[ 4],  6, 0xF7537E82);
    MD5_STEP(FuncI, d, a, b, c, x[11], 10, 0xBD3AF235);
    MD5_STEP(FuncI, c, d, a, b, x[ 2], 15, 0x2AD7D2BB);
    MD5_STEP(FuncI, b, c, d, a, x[ 9], 21, 0xEB86D391);

    pState[0] += a;
    pState[1] += b;
    pState[2] += c;
    pState[3] += d;
}

// dwLen is a byte count and a multiple of 4.
void CMD5::Encode(const DWORD* pIn, DWORD dwLen, BYTE* pOut)
{
    for (DWORD i = 0, j = 0; j < dwLen; ++i, j += 4)
    {
        pOut[j]     = (BYTE)pIn[i];
        pOut[j + 1] = (BYTE)(pIn[i] >> 8);
        pOut[j + 2] = (BYTE)(pIn[i] >> 16);
        pOut[j + 3] = (BYTE)(pIn[i] >> 24);
    }
}

void CMD5::Decode(const BYTE* pIn, DWORD* pOut)
{
    for (int i = 0; i < 16; ++i, pIn += 4)
        pOut[i] = pIn[0] | ((DWORD)pIn[1] << 8) | ((DWORD)pIn[2] << 16) | ((DWORD)pIn[3] << 24);
}

void CMD5::MemCopy(BYTE* pDst, DWORD dwLen, const BYTE* pSrc)
{
    for (DWORD i = 0; i < dwLen; ++i)
        pDst[i] = pSrc[i];
}
