#pragma once

#include <windows.h>

#include "BSNet/security/BlowFish.h"

// Handshake key agreement: 32-bit Diffie-Hellman over a random prime, followed by a Blowfish
// based proof that both sides hold the same secret. The accepting side (server) uses
// CKeySender, the connecting side (client) CKeyRepicient.
//
//   server -> client  KEYEXCHANGE_SERVER_PARAM   (initial key, generator, prime, server public)
//   client -> server  KEYEXCHANGE_CHALLENGE      (client public, proof)
//   server -> client  8-byte proof
//
// Both sides then key the session cipher with GetFinalKey.

// Modular exponentiation with 64-bit intermediates; exponent 0 gives 1.
DWORD KeyExchange_ModPow(DWORD dwBase, DWORD dwExp, DWORD dwMod);

// Fermat check: dwWitness^(n-1) mod n == 1.
BOOL KeyExchange_FermatWitness(DWORD dwN, DWORD dwWitness);

// Five Fermat rounds with witnesses from rand().
BOOL KeyExchange_IsPrime(DWORD dwN);

// Odd 31-bit start value from CoCreateGuid, then the next number that passes IsPrime.
DWORD KeyExchange_RandomPrime();

#pragma pack(push, 1)

// 0x5000 handshake block sent by the server when key exchange is on.
struct KEYEXCHANGE_SERVER_PARAM
{
    BYTE    InitialKey[8];
    DWORD   dwGenerator;
    DWORD   dwPrime;
    DWORD   dwServerPublic;
};

// Client answer: its public value and the proof computed with the derived key.
struct KEYEXCHANGE_CHALLENGE
{
    DWORD   dwClientPublic;
    BYTE    Proof[8];
};

#pragma pack(pop)

C_ASSERT(sizeof(KEYEXCHANGE_SERVER_PARAM) == 20);
C_ASSERT(sizeof(KEYEXCHANGE_CHALLENGE) == 12);

// Diffie-Hellman working values. Each side only fills its own private value.
struct KEYEXCHANGE_DH
{
    DWORD   dwGenerator;        // +0
    DWORD   dwPrime;            // +4
    DWORD   dwServerPrivate;    // +8
    DWORD   dwClientPrivate;    // +12
    DWORD   dwServerPublic;     // +16
    DWORD   dwClientPublic;     // +20
    DWORD   dwSecret;           // +24

    KEYEXCHANGE_DH()
    {
        dwGenerator = 0;
        dwPrime = 0;
        dwServerPrivate = 0;
        dwClientPrivate = 0;
        dwServerPublic = 0;
        dwClientPublic = 0;
        dwSecret = 0;
    }

    // Two random primes; the smaller becomes the generator, the larger the modulus.
    void GenerateBaseModulus(DWORD* pdwGenerator, DWORD* pdwPrime);
    // Server side: private value from CoCreateGuid, public = g^x mod p.
    void GenerateServerPublic(DWORD* pdwPublic);
    // Server side: secret = clientPublic^x mod p.
    void ComputeServerSecret(DWORD dwPeer, DWORD* pdwSecret);
    // Client side: takes g and p, private value from CoCreateGuid, public = g^y mod p.
    void GenerateClientPublic(DWORD dwG, DWORD dwP, DWORD* pdwPublic);
    // Client side: secret = serverPublic^y mod p.
    void ComputeClientSecret(DWORD dwPeer, DWORD* pdwSecret);
};

C_ASSERT(sizeof(KEYEXCHANGE_DH) == 28);

class CKeyExchangeBase
{
public:
    CKeyExchangeBase();
    virtual ~CKeyExchangeBase();

    // pKey[i] ^= bSalt + byte (i % 4) of dwValue + pKey[i], for the 8 key bytes.
    static void TransformKey(BYTE* pKey, BYTE bSalt, DWORD dwValue);

    // Session Blowfish key: initial key transformed with the shared secret.
    void GetFinalKey(BYTE* pKey) const;

protected:
    // Keys the exchange's own cipher from {dwFirst, dwSecond} transformed with dwSecret.
    void DeriveKey(DWORD dwSecret, DWORD dwFirst, DWORD dwSecond);
    // {dwFirst, dwSecond} transformed with dwSecret and encrypted with the exchange cipher.
    void ComputeValue(BYTE* pOut, DWORD dwSecret, DWORD dwFirst, DWORD dwSecond) const;
    bool CheckValue(DWORD dwFirst, const void* pValue, DWORD dwSecret, DWORD dwSecond) const;

public:
    KEYEXCHANGE_DH  m_DH;               // +4
    CBlowFish       m_BlowFish;         // +32
    DWORD           m_dwPrime;          // +44
    DWORD           m_dwGenerator;      // +48
    DWORD           m_dwServerPublic;   // +52
    DWORD           m_dwClientPublic;   // +56
    DWORD           m_dwSecret;         // +60
    BYTE            m_InitialKey[8];    // +64

private:
    CKeyExchangeBase(const CKeyExchangeBase&);
    CKeyExchangeBase& operator=(const CKeyExchangeBase&);
};

C_ASSERT(FIELD_OFFSET(CKeyExchangeBase, m_DH) == 4);
C_ASSERT(FIELD_OFFSET(CKeyExchangeBase, m_BlowFish) == 32);
C_ASSERT(FIELD_OFFSET(CKeyExchangeBase, m_dwPrime) == 44);
C_ASSERT(FIELD_OFFSET(CKeyExchangeBase, m_dwSecret) == 60);
C_ASSERT(FIELD_OFFSET(CKeyExchangeBase, m_InitialKey) == 64);
C_ASSERT(sizeof(CKeyExchangeBase) == 72);

// Accepting side.
class CKeySender : public CKeyExchangeBase
{
public:
    CKeySender()
    {
    }

    virtual ~CKeySender()
    {
    }

    // New initial key, prime pair and server key pair; fills the block sent to the client.
    void GenerateServerParams(KEYEXCHANGE_SERVER_PARAM* pOut);
    // Takes the client's public value, derives the secret and checks the client's proof.
    bool VerifyChallenge(const KEYEXCHANGE_CHALLENGE* pChallenge);
    // Proof sent back to the client.
    void MakeChallenge(BYTE* pOut) const;
};

// Connecting side.
class CKeyRepicient : public CKeyExchangeBase
{
public:
    CKeyRepicient()
    {
    }

    virtual ~CKeyRepicient()
    {
    }

    // Stores the server block, creates the client key pair and derives the secret.
    void AcceptServerParams(const KEYEXCHANGE_SERVER_PARAM* pParam);
    // Answer for the server: client public value and proof.
    void MakeResponse(KEYEXCHANGE_CHALLENGE* pOut) const;
    // Checks the server's proof.
    bool CheckPeerValue(const void* pValue) const;
};

C_ASSERT(sizeof(CKeySender) == 72);
C_ASSERT(sizeof(CKeyRepicient) == 72);
