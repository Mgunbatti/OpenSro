#include "KeyExchange.h"

#include <objbase.h>
#include <stdlib.h>
#include <string.h>

DWORD KeyExchange_ModPow(DWORD dwBase, DWORD dwExp, DWORD dwMod)
{
    unsigned __int64 qwExp = dwExp;
    if (qwExp == 0)
        return 1;

    unsigned __int64 qwBase = dwBase;
    unsigned __int64 qwResult = 1;
    for (;;)
    {
        if (qwExp & 1)
            qwResult = qwBase * qwResult % dwMod;
        qwExp >>= 1;
        qwBase = qwBase * qwBase % dwMod;
        if (qwExp == 0)
            break;
    }
    return (DWORD)qwResult;
}

BOOL KeyExchange_FermatWitness(DWORD dwN, DWORD dwWitness)
{
    return KeyExchange_ModPow(dwWitness, dwN - 1, dwN) == 1;
}

// BUG(SEC-002): dwN == 3 divides by zero (rand() % 0) and dwN == 1 passes as prime.
BOOL KeyExchange_IsPrime(DWORD dwN)
{
    DWORD dwRange = dwN - 3;
    for (DWORD i = 0; i < 5; ++i)
    {
        DWORD dwWitness = (DWORD)rand() % dwRange + 2;
        if (!KeyExchange_FermatWitness(dwN, dwWitness))
            return FALSE;
    }
    return TRUE;
}

DWORD KeyExchange_RandomPrime()
{
    GUID guid;
    CoCreateGuid(&guid);
    DWORD dwN = guid.Data1 & 0x7FFFFFFF;
    if (!(dwN & 1))
        ++dwN;
    while (!KeyExchange_IsPrime(dwN))
        dwN += 2;
    return dwN;
}

void KEYEXCHANGE_DH::GenerateBaseModulus(DWORD* pdwGenerator, DWORD* pdwPrime)
{
    dwGenerator = KeyExchange_RandomPrime();
    DWORD dw = KeyExchange_RandomPrime();
    DWORD dwFirst = dwGenerator;
    dwPrime = dw;
    if (dwFirst > dw)
    {
        dwGenerator = dw;
        dwPrime = dwFirst;
    }
    *pdwGenerator = dwGenerator;
    *pdwPrime = dwPrime;
}

void KEYEXCHANGE_DH::GenerateServerPublic(DWORD* pdwPublic)
{
    GUID guid;
    CoCreateGuid(&guid);
    dwServerPrivate = guid.Data1 & 0x7FFFFFFF;
    dwServerPublic = KeyExchange_ModPow(dwGenerator, dwServerPrivate, dwPrime);
    *pdwPublic = dwServerPublic;
}

void KEYEXCHANGE_DH::ComputeServerSecret(DWORD dwPeer, DWORD* pdwSecret)
{
    dwClientPublic = dwPeer;
    dwSecret = KeyExchange_ModPow(dwPeer, dwServerPrivate, dwPrime);
    *pdwSecret = dwSecret;
}

void KEYEXCHANGE_DH::GenerateClientPublic(DWORD dwG, DWORD dwP, DWORD* pdwPublic)
{
    GUID guid;
    CoCreateGuid(&guid);
    dwClientPrivate = guid.Data1 & 0x7FFFFFFF;
    dwGenerator = dwG;
    dwPrime = dwP;
    dwClientPublic = KeyExchange_ModPow(dwG, dwClientPrivate, dwP);
    *pdwPublic = dwClientPublic;
}

void KEYEXCHANGE_DH::ComputeClientSecret(DWORD dwPeer, DWORD* pdwSecret)
{
    dwServerPublic = dwPeer;
    dwSecret = KeyExchange_ModPow(dwPeer, dwClientPrivate, dwPrime);
    *pdwSecret = dwSecret;
}

CKeyExchangeBase::CKeyExchangeBase()
{
    m_dwPrime = 0;
    m_dwGenerator = 0;
    m_dwServerPublic = 0;
    m_dwClientPublic = 0;
    m_dwSecret = 0;
    memset(m_InitialKey, 0, sizeof(m_InitialKey));
}

CKeyExchangeBase::~CKeyExchangeBase()
{
}

void CKeyExchangeBase::TransformKey(BYTE* pKey, BYTE bSalt, DWORD dwValue)
{
    const BYTE* pValue = (const BYTE*)&dwValue;
    for (int i = 0; i < 8; ++i)
        pKey[i] ^= (BYTE)(pKey[i] + pValue[i % 4] + bSalt);
}

void CKeyExchangeBase::GetFinalKey(BYTE* pKey) const
{
    memcpy(pKey, m_InitialKey, 8);
    TransformKey(pKey, 3, m_dwSecret);
}

void CKeyExchangeBase::DeriveKey(DWORD dwSecret, DWORD dwFirst, DWORD dwSecond)
{
    DWORD key[2];
    key[0] = dwFirst;
    key[1] = dwSecond;
    TransformKey((BYTE*)key, (BYTE)(dwSecret & 3), dwSecret);
    m_BlowFish.Initialize((const BYTE*)key);
}

void CKeyExchangeBase::ComputeValue(BYTE* pOut, DWORD dwSecret, DWORD dwFirst,
                                    DWORD dwSecond) const
{
    ((DWORD*)pOut)[1] = dwSecond;
    ((DWORD*)pOut)[0] = dwFirst;
    TransformKey(pOut, (BYTE)(dwFirst & 7), dwSecret);
    m_BlowFish.Encrypt(pOut, pOut, 8);
}

bool CKeyExchangeBase::CheckValue(DWORD dwFirst, const void* pValue, DWORD dwSecret,
                                  DWORD dwSecond) const
{
    DWORD value[2];
    value[0] = 0;
    value[1] = 0;
    ComputeValue((BYTE*)value, dwSecret, dwFirst, dwSecond);
    return memcmp(pValue, value, 8) == 0;
}

void CKeySender::GenerateServerParams(KEYEXCHANGE_SERVER_PARAM* pOut)
{
    GUID guid;
    CoCreateGuid(&guid);
    memcpy(m_InitialKey, guid.Data4, 8);
    m_DH.GenerateBaseModulus(&m_dwGenerator, &m_dwPrime);
    m_DH.GenerateServerPublic(&m_dwServerPublic);

    memcpy(pOut->InitialKey, m_InitialKey, 8);
    pOut->dwGenerator = m_dwGenerator;
    pOut->dwPrime = m_dwPrime;
    pOut->dwServerPublic = m_dwServerPublic;
}

bool CKeySender::VerifyChallenge(const KEYEXCHANGE_CHALLENGE* pChallenge)
{
    DWORD dwPeer = pChallenge->dwClientPublic;
    m_dwClientPublic = dwPeer;
    m_DH.ComputeServerSecret(dwPeer, &m_dwSecret);
    DeriveKey(m_dwSecret, m_dwServerPublic, m_dwClientPublic);
    return CheckValue(m_dwClientPublic, pChallenge->Proof, m_dwSecret, m_dwServerPublic);
}

void CKeySender::MakeChallenge(BYTE* pOut) const
{
    ComputeValue(pOut, m_dwSecret, m_dwServerPublic, m_dwClientPublic);
}

void CKeyRepicient::AcceptServerParams(const KEYEXCHANGE_SERVER_PARAM* pParam)
{
    memcpy(m_InitialKey, pParam->InitialKey, 8);
    m_dwGenerator = pParam->dwGenerator;
    m_dwPrime = pParam->dwPrime;
    m_dwServerPublic = pParam->dwServerPublic;
    m_DH.GenerateClientPublic(m_dwGenerator, m_dwPrime, &m_dwClientPublic);
    m_DH.ComputeClientSecret(m_dwServerPublic, &m_dwSecret);
    DeriveKey(m_dwSecret, m_dwServerPublic, m_dwClientPublic);
}

void CKeyRepicient::MakeResponse(KEYEXCHANGE_CHALLENGE* pOut) const
{
    pOut->dwClientPublic = m_dwClientPublic;
    ComputeValue(pOut->Proof, m_dwSecret, m_dwClientPublic, m_dwServerPublic);
}

bool CKeyRepicient::CheckPeerValue(const void* pValue) const
{
    return CheckValue(m_dwServerPublic, pValue, m_dwSecret, m_dwClientPublic);
}
