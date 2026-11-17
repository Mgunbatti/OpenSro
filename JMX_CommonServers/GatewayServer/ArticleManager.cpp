#include "ArticleManager.h"

#include <string.h>

#include "BSLib/DB/DBQuery.h"
#include "GatewayServer.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/Trigger.h"

#include <strsafe.h>

using namespace ServerFramework;

CArticleManager g_ArticleManager;

CArticleManager::CArticleManager()
{
}

CArticleManager::~CArticleManager()
{
    ClearArticles();
}

void CArticleManager::ClearArticles()
{
    for (std::list<ARTICLE*>::iterator it = m_listArticle.begin(); it != m_listArticle.end(); ++it)
        operator delete(*it);
    m_listArticle.clear();
}

// BUG(GW-006): the count is a BYTE; with more than 255 articles of the type it wraps while all of
// them are written.
void CArticleManager::WriteArticles(CMassiveMsg* pMsg, BYTE byType)
{
    BYTE byCount = 0;
    std::list<ARTICLE*>::iterator it;
    for (it = m_listArticle.begin(); it != m_listArticle.end(); ++it)
    {
        if ((*it)->byType == byType)
            ++byCount;
    }
    pMsg->Write(&byCount, 1);
    for (it = m_listArticle.begin(); it != m_listArticle.end(); ++it)
    {
        ARTICLE* pArticle = *it;
        if (pArticle->byType != byType)
            continue;
        DWORD dwLen = (WORD)lstrlenA(pArticle->szTitle);
        pMsg->Write(&dwLen, 2);
        if ((WORD)dwLen)
            pMsg->Write(pArticle->szTitle, (WORD)dwLen);
        dwLen = (WORD)lstrlenA(pArticle->szText);
        pMsg->Write(&dwLen, 2);
        if ((WORD)dwLen)
            pMsg->Write(pArticle->szText, (WORD)dwLen);
        pMsg->Write(pArticle->abTime, 16);
    }
}

int CArticleManager::OnArticleRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (!pQuery->m_bRowFetched)
    {
        void* pRow = operator new(sizeof(ARTICLE));
        pQuery->m_pBuffer = (BYTE*)pRow;
        pQuery->m_nRowSize = sizeof(ARTICLE);
        memset(pRow, 0, sizeof(ARTICLE));
        return 1;
    }
    m_listArticle.push_back((ARTICLE*)pQuery->m_pBuffer);
    return 1;
}

bool CArticleManager_ReloadFromDB()
{
    g_ArticleManager.ClearArticles();
    CClassTrigger<CArticleManager> trigger(&g_ArticleManager);
    trigger.m_pfnTrigger2 = &CArticleManager::OnArticleRow;
    CDBQuery query;
    query.m_pSession = g_pGatewayDBSession;
    query.m_pTrigger = &trigger;
    query.m_nSession = 0;
    if (FAILED(StringCchCopyA(query.m_szQuery, sizeof(query.m_szQuery), "exec _GetArticleData")))
    {
        BSAssertFail();
        return false;
    }
    if (query.Execute() < 0)
    {
        LogPrintf(0x2000000, "article load failed !!");
        return false;
    }
    LogPrintf(0, "article loaded : %d", g_ArticleManager.m_listArticle.size());
    return true;
}
