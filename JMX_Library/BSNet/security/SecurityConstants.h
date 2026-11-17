#pragma once

#include <windows.h>

// Protocol values the original binaries hard-code. The vSRO set is in
// SecurityConstantsVSRO.inl; see SecurityConstants.cpp for using another set.

// 256 polynomials; row n of the CRC table is built from entry n (Security_BuildCrcTable).
// For vSRO this is the standard CRC-32 table with a different first entry.
extern const DWORD g_SecurityCrcSeeds[256];

// Count seed used when the handshake carries 0 (Security_ApplyHandshakeFlags).
extern const DWORD g_dwSecurityDefaultCountSeed;
