#pragma once

#include <windows.h>

// Blowfish with a fixed 8-byte key. Blocks are two native (little-endian) DWORDs, so a buffer
// encrypts differently from the big-endian reference byte order; the cipher itself and the
// key schedule are standard.
class CBlowFish
{
public:
    enum
    {
        KEY_SIZE    = 8,
        BLOCK_SIZE  = 8,
        P_COUNT     = 18,
        S_COUNT     = 4 * 256,
    };

    CBlowFish();
    virtual ~CBlowFish();

    // Key schedule. Always reads KEY_SIZE bytes.
    void Initialize(const BYTE* pKey);

    void EncryptBlock(DWORD* pL, DWORD* pR) const;
    void DecryptBlock(DWORD* pL, DWORD* pR) const;

    // Encrypts dwLen bytes in 8-byte blocks, zero-padding the last one, and returns the padded
    // length. In place when pSrc == pDst; otherwise pDst needs room for the padded length.
    DWORD Encrypt(const void* pSrc, void* pDst, DWORD dwLen) const;

    // Decrypts (dwLen - 1) / 8 + 1 whole blocks, in place when pSrc == pDst.
    void Decrypt(const void* pSrc, void* pDst, DWORD dwLen) const;

    // Length rounded up to the block size.
    static DWORD AlignUp8(DWORD dwLen)
    {
        if (dwLen & 7)
            return dwLen - (dwLen & 7) + 8;
        return dwLen;
    }

public:
    DWORD*  m_pP;   // +4  P-array, 18 DWORDs (heap)
    DWORD*  m_pS;   // +8  four S-boxes, 1024 DWORDs (heap)

private:
    DWORD F(DWORD x) const
    {
        return ((m_pS[x >> 24] + m_pS[256 + ((x >> 16) & 0xFF)])
                ^ m_pS[512 + ((x >> 8) & 0xFF)])
               + m_pS[768 + (x & 0xFF)];
    }

    CBlowFish(const CBlowFish&);
    CBlowFish& operator=(const CBlowFish&);
};

C_ASSERT(sizeof(CBlowFish) == 12);
