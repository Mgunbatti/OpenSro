#include "Security.h"

#include <stdlib.h>

#include "BSNet/security/SecurityConstants.h"

DWORD g_SecurityCrcTable[256 * 256];
BOOL g_bSecurityCrcTableBuilt = FALSE;

void Security_LFSR32(DWORD* pdwValue)
{
    DWORD v = *pdwValue;
    for (int i = 0; i < 32; ++i)
    {
        DWORD dwBit = (v ^ (v >> 1) ^ (v >> 2) ^ (v >> 3) ^ (v >> 5) ^ (v >> 7)) & 1;
        v = (_rotr(v, 1) & 0xFFFFFFFE) | dwBit;
    }
    *pdwValue = v;
}

void Security_ApplyHandshakeFlags(SECURITY_STATE* pState, const SECURITY_HANDSHAKE_PARAM* pParam)
{
    pState->bOptions = pParam->bOptions;
    if (pParam->bOptions & SECURITY_OPT_NONE)
        return;

    if (pParam->bOptions & SECURITY_OPT_BLOWFISH)
        pState->BlowFish.Initialize(pParam->BlowFishKey);

    if (pParam->bOptions & SECURITY_OPT_COUNT_CRC)
    {
        DWORD dwSeed = pParam->dwCountSeed;
        if (dwSeed == 0)
            dwSeed = g_dwSecurityDefaultCountSeed;

        Security_LFSR32(&dwSeed);
        DWORD dw1 = dwSeed;
        Security_LFSR32(&dwSeed);
        DWORD dw2 = dwSeed;
        Security_LFSR32(&dwSeed);
        DWORD dw3 = dwSeed;
        Security_LFSR32(&dwSeed);
        DWORD dw4 = dwSeed;

        BYTE bMul = (BYTE)(dw1 ^ dw2);
        if (bMul == 0)
            bMul = 1;
        BYTE bAdd = (BYTE)(dw4 ^ dw3);
        if (bAdd == 0)
            bAdd = 1;

        pState->bCount = bMul ^ bAdd;
        pState->bCountMul = bMul;
        pState->bCountAdd = bAdd;
        Security_InitCrcSeed(pParam->dwCrcSeed, &pState->Crc);
    }
}

BOOL Security_ModeCheck(BYTE bSecurityMode, BYTE bHandshakeState)
{
    if ((bSecurityMode & SECURITY_MODE_BASIC) && !(bHandshakeState & SECURITY_HS_CHALLENGE)
        && (bHandshakeState & SECURITY_HS_BLOWFISH) && (bHandshakeState & SECURITY_HS_COUNT_CRC))
    {
        return TRUE;
    }
    if ((bSecurityMode & SECURITY_MODE_KEY_EXCHANGE) && (bHandshakeState & SECURITY_HS_BLOWFISH)
        && (bHandshakeState & SECURITY_HS_COUNT_CRC)
        && (bHandshakeState & SECURITY_HS_KEY_EXCHANGE)
        && (bHandshakeState & SECURITY_HS_CHALLENGE))
    {
        return TRUE;
    }
    return FALSE;
}

void Security_BuildCrcTable()
{
    Security_BuildCrcTableFrom(g_SecurityCrcSeeds);
}

void Security_BuildCrcTableFrom(const DWORD* pSeeds)
{
    DWORD* pOut = g_SecurityCrcTable;
    for (int nRow = 0; nRow < 256; ++nRow)
    {
        DWORD dwPoly = pSeeds[nRow];
        for (DWORD i = 0; i < 256; ++i)
        {
            DWORD v = i;
            for (int nBit = 0; nBit < 8; ++nBit)
            {
                if (v & 1)
                    v = (v >> 1) ^ dwPoly;
                else
                    v >>= 1;
            }
            *pOut++ = v;
        }
    }
    g_bSecurityCrcTableBuilt = TRUE;
}

void Security_InitCrcSeed(DWORD dwSeed, SECURITY_CRC* pCrc)
{
    pCrc->dwValue = 0xFFFFFFFF;
    pCrc->dwSeed = (BYTE)dwSeed;
    if ((BYTE)dwSeed == 0)
        pCrc->dwSeed = 1;
}

BYTE Security_ComputeCrcByte(const BYTE* pData, SECURITY_CRC* pCrc, DWORD dwLen)
{
    if (!pData)
        return 0;

    pCrc->dwValue = 0xFFFFFFFF;
    if (dwLen)
    {
        const DWORD* pRow = g_SecurityCrcTable + (pCrc->dwSeed << 8);
        do
        {
            pCrc->dwValue = pRow[(BYTE)(pCrc->dwValue ^ *pData++)] ^ (pCrc->dwValue >> 8);
        } while (--dwLen);
    }

    DWORD v = pCrc->dwValue;
    return (BYTE)((BYTE)v + (BYTE)(v >> 8) + (BYTE)(v >> 16) + (BYTE)(v >> 24));
}
