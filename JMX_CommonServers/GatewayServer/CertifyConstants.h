#pragma once

// _CertifyUser takes a fixed publisher key as third argument. The key is not part of this
// repository: tools\gen_certify_constants.py extracts it from the original GatewayServer.exe
// into CertifyConstants.inl (ignored by git), which the Makefile compiles in when the file exists
// (GATEWAY_CERTIFY_CONSTANTS). Without it the key is empty and _CertifyUser refuses the logins.
#ifdef GATEWAY_CERTIFY_CONSTANTS
#include "CertifyConstants.inl"
#endif

#ifndef GATEWAY_CERTIFY_USER_KEY
#define GATEWAY_CERTIFY_USER_KEY ""
#endif
