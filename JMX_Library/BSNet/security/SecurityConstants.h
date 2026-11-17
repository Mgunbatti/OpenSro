#pragma once

#include <windows.h>

// Protocol values that the original binary hard-codes and that are not public algorithm
// constants. They are not part of the source tree: SecurityConstants.cpp takes them from a
// generated SecurityConstants.inl when built with BSNET_SECURITY_CONSTANTS, otherwise they are
// zero and the session security is not compatible with the original peers.

// 256 polynomials; row n of the CRC table is built from entry n (Security_BuildCrcTable).
extern const DWORD g_SecurityCrcSeeds[256];

// Count seed used when the handshake carries 0 (Security_ApplyHandshakeFlags).
extern const DWORD g_dwSecurityDefaultCountSeed;
