#include "Token.h"

#include "ServerFramework/ServerDataExtern.h"

using namespace ServerFramework;

COverlapJobIDPool g_TokenIDPool;
CTokenManager g_TokenManager;

// ---------------------------------------------------------------------------------------------
// CToken

CToken::CToken()
{
    m_dwTokenID = g_TokenIDPool.Pop();
    m_dwCreateTick = GetTickCount();
}

CToken::~CToken()
{
    g_TokenIDPool.Push(m_dwTokenID);
}

// ---------------------------------------------------------------------------------------------
// CTokenManager

CTokenManager::~CTokenManager()
{
    if (m_mapToken.size())
        BSAssertFail();
}

bool CTokenManager::Add(CToken* pToken)
{
    if (m_mapToken.find(pToken->m_dwTokenID) != m_mapToken.end())
        return false;
    m_mapToken.insert(AgentTokenMap::value_type(pToken->m_dwTokenID, pToken));
    return true;
}

BOOL CTokenManager::GetExtra(DWORD dwTokenID, WORD* pw50, DWORD* pdw54)
{
    *pw50 = 0;
    *pdw54 = 0;
    AgentTokenMap::iterator it = m_mapToken.find(dwTokenID);
    if (it == m_mapToken.end())
        return FALSE;
    CToken* pToken = it->second;
    *pw50 = pToken->m_w50;
    *pdw54 = pToken->m_dw54;
    return TRUE;
}

bool CTokenManager::Consume(DWORD dwTokenID, DWORD dwClientIP, const char* pszID,
                            const char* pszPassword, DWORD* pdwJID, BYTE* pbySecPrimary,
                            BYTE* pbySecContent)
{
    AgentTokenMap::iterator it = m_mapToken.find(dwTokenID);
    if (it == m_mapToken.end())
        return false;
    bool bResult = false;
    CToken* pToken = it->second;
    m_mapToken.erase(it);
    if (pToken->m_dwClientIP == dwClientIP)
    {
        const char* pszTokenID = pToken->m_strID.c_str();
        int nLen = lstrlenA(pszID);
        if (CompareStringA(0x400, 0, pszTokenID, lstrlenA(pszTokenID), pszID, nLen)
            == CSTR_EQUAL)
        {
            const char* pszTokenPassword = pToken->m_strPassword.c_str();
            nLen = lstrlenA(pszPassword);
            if (CompareStringA(0x400, 0, pszTokenPassword, lstrlenA(pszTokenPassword),
                               pszPassword, nLen) == CSTR_EQUAL)
            {
                *pdwJID = pToken->m_dwJID;
                *pbySecPrimary = pToken->m_bySecPrimary;
                *pbySecContent = pToken->m_bySecContent;
                bResult = true;
            }
        }
    }
    delete pToken;
    return bResult;
}

void CTokenManager::RemoveAll()
{
    for (AgentTokenMap::iterator it = m_mapToken.begin(); it != m_mapToken.end(); ++it)
    {
        if (it->second)
            delete it->second;
    }
    m_mapToken.clear();
}

void CTokenManager::CheckTimeout()
{
    if (!m_mapToken.size())
        return;
    DWORD dwNow = GetTickCount();
    AgentTokenMap::iterator it = m_mapToken.begin();
    while (it != m_mapToken.end())
    {
        CToken* pToken = it->second;
        DWORD dwCreated = pToken->m_dwCreateTick;
        if (dwNow - dwCreated <= AGENT_TOKEN_TIMEOUT)
        {
            ++it;
            continue;
        }
        LogPrintf(0x1000000,
                  "Token Timeout! [token id: %d, jid: %d, elapsed: %u ms (created: %u, cur: %u)]",
                  pToken->m_dwTokenID, pToken->m_dwJID, dwNow - dwCreated, dwCreated, dwNow);
        m_mapToken.erase(it++);
        delete pToken;
    }
}
