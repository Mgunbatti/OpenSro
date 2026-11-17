#pragma once

#include <windows.h>

// MD5 of a zero-terminated string, kept as 16 raw bytes and as a lower-case hex string.
// Used by the certification code to hash passwords.
class CMD5
{
public:
    // Working state of one digest computation (lives on the stack while hashing).
    struct CONTEXT
    {
        DWORD   State[4];       // +0  A, B, C, D
        DWORD   Count[2];       // +16 message length in bits, low / high
        BYTE    Buffer[64];     // +24 partial input block
    };

    CMD5()
    {
        m_bValid = FALSE;
        m_szHexDigest[32] = '\0';
    }

    virtual ~CMD5()
    {
    }

    // Hashes psz (kept by pointer) and returns whether the hex string could be formatted.
    BOOLEAN Compute(const char* psz)
    {
        m_pszInput = psz;
        m_bValid = ComputeHexDigest();
        return m_bValid;
    }

    // 32 hex digits, or NULL when Compute has not succeeded.
    const char* GetHexDigest() const
    {
        if (m_bValid)
            return m_szHexDigest;
        return NULL;
    }

    static void Init(CONTEXT* pCtx);
    static void Update(CONTEXT* pCtx, DWORD dwLen, const BYTE* pInput);
    static void Final(BYTE* pDigest, CONTEXT* pCtx);

private:
    BOOLEAN ComputeHexDigest();

    static void Transform(const BYTE* pBlock, DWORD* pState);
    static void Encode(const DWORD* pIn, DWORD dwLen, BYTE* pOut);
    static void Decode(const BYTE* pIn, DWORD* pOut);
    static void MemCopy(BYTE* pDst, DWORD dwLen, const BYTE* pSrc);

public:
    BOOLEAN     m_bValid;           // +4
    BYTE        m_Digest[16];       // +5
    char        m_szHexDigest[33];  // +21
    const char* m_pszInput;         // +56
};

C_ASSERT(FIELD_OFFSET(CMD5, m_bValid) == 4);
C_ASSERT(FIELD_OFFSET(CMD5, m_Digest) == 5);
C_ASSERT(FIELD_OFFSET(CMD5, m_szHexDigest) == 21);
C_ASSERT(FIELD_OFFSET(CMD5, m_pszInput) == 56);
C_ASSERT(sizeof(CMD5) == 60);
C_ASSERT(sizeof(CMD5::CONTEXT) == 0x58);
