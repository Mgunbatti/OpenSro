#pragma once

#include <windows.h>

#include "BSNet/security/BlowFish.h"

// Per-connection packet security: Blowfish payload encryption, a pseudo-random count byte and
// a table-driven CRC byte in the packet header.

// Handshake option bits (first byte of the 0x5000 handshake message).
enum
{
    SECURITY_OPT_NONE           = 0x01,     // no security, nothing else is applied
    SECURITY_OPT_BLOWFISH       = 0x02,     // 8-byte Blowfish key follows
    SECURITY_OPT_COUNT_CRC      = 0x04,     // count seed and CRC seed follow
    SECURITY_OPT_KEY_EXCHANGE   = 0x08,     // KEYEXCHANGE_SERVER_PARAM follows
    SECURITY_OPT_CHALLENGE      = 0x10,     // server proof of the key exchange
};

// Security mode of the network engine (CNetEngine::SetSecurityMode).
enum
{
    SECURITY_MODE_BASIC         = 0x01,     // Blowfish and count/CRC without key exchange
    SECURITY_MODE_KEY_EXCHANGE  = 0x02,     // full key exchange
};

// Handshake progress bits of a session (CSockStream +512).
enum
{
    SECURITY_HS_BLOWFISH        = 0x02,
    SECURITY_HS_COUNT_CRC       = 0x04,
    SECURITY_HS_KEY_EXCHANGE    = 0x08,
    SECURITY_HS_CHALLENGE       = 0x10,
};

// Options as passed to CNetEngine::StartHandshake and read from the first handshake message.
struct SECURITY_HANDSHAKE_PARAM
{
    BYTE    bOptions;           // +0  SECURITY_OPT_*
    DWORD   dwCountSeed;        // +4
    DWORD   dwCrcSeed;          // +8  only the low byte is used
    BYTE    BlowFishKey[8];     // +12
};

C_ASSERT(FIELD_OFFSET(SECURITY_HANDSHAKE_PARAM, dwCountSeed) == 4);
C_ASSERT(FIELD_OFFSET(SECURITY_HANDSHAKE_PARAM, BlowFishKey) == 12);
C_ASSERT(sizeof(SECURITY_HANDSHAKE_PARAM) == 20);

struct SECURITY_CRC
{
    DWORD   dwSeed;             // +0  CRC table row, 1..255
    DWORD   dwValue;            // +4  running value of the last computation
};

// Security members of a stream socket (CSockStream +344 .. +367).
struct SECURITY_STATE
{
    CBlowFish       BlowFish;       // +0
    SECURITY_CRC    Crc;            // +12
    BYTE            bCount;         // +20 last count byte
    BYTE            bCountMul;      // +21
    BYTE            bCountAdd;      // +22
    BYTE            bOptions;       // +23 SECURITY_OPT_* of the handshake
};

C_ASSERT(FIELD_OFFSET(SECURITY_STATE, Crc) == 12);
C_ASSERT(FIELD_OFFSET(SECURITY_STATE, bCount) == 20);
C_ASSERT(FIELD_OFFSET(SECURITY_STATE, bOptions) == 23);
C_ASSERT(sizeof(SECURITY_STATE) == 24);

// 256 x 256 CRC table, one row per seed polynomial.
extern DWORD g_SecurityCrcTable[256 * 256];
extern BOOL g_bSecurityCrcTableBuilt;

// 32 steps of the count seed generator.
void Security_LFSR32(DWORD* pdwValue);

// Stores the options and sets up Blowfish and the count/CRC seeds they ask for.
void Security_ApplyHandshakeFlags(SECURITY_STATE* pState, const SECURITY_HANDSHAKE_PARAM* pParam);

// Whether a session's handshake state satisfies the engine's security mode.
BOOL Security_ModeCheck(BYTE bSecurityMode, BYTE bHandshakeState);

// Builds g_SecurityCrcTable from g_SecurityCrcSeeds (done once by the CNetEngine constructor).
void Security_BuildCrcTable();
void Security_BuildCrcTableFrom(const DWORD* pSeeds);

// Seed byte (0 becomes 1); resets the running value.
void Security_InitCrcSeed(DWORD dwSeed, SECURITY_CRC* pCrc);

// CRC over dwLen bytes starting from 0xFFFFFFFF; returns the sum of the four result bytes.
// A NULL buffer returns 0 and leaves the running value alone.
BYTE Security_ComputeCrcByte(const BYTE* pData, SECURITY_CRC* pCrc, DWORD dwLen);

// Next header count byte; inlined at every send and receive in the original.
inline BYTE Security_NextCountByte(SECURITY_STATE* pState)
{
    BYTE b = (BYTE)((BYTE)(~pState->bCount + pState->bCountMul) * pState->bCountAdd);
    b ^= b >> 4;
    pState->bCount = b;
    return b;
}
