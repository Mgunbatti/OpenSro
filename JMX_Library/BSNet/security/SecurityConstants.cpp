#include "SecurityConstants.h"

// SecurityConstants.inl is generated from the original binary at build time and defines
// SECURITY_CRC_SEEDS (a brace-enclosed list of 256 DWORDs) and SECURITY_DEFAULT_COUNT_SEED.
#ifdef BSNET_SECURITY_CONSTANTS
#include "SecurityConstants.inl"
#else
#define SECURITY_CRC_SEEDS          { 0 }
#define SECURITY_DEFAULT_COUNT_SEED 0
#endif

const DWORD g_SecurityCrcSeeds[256] = SECURITY_CRC_SEEDS;
const DWORD g_dwSecurityDefaultCountSeed = SECURITY_DEFAULT_COUNT_SEED;
