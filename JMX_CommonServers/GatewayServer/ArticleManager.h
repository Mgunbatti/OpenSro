#pragma once

#include <windows.h>
#include <list>

#include "ServerFramework/MassiveMsg.h"

// Notices ("articles") shown by the client at login, rows of exec _GetArticleData.

#pragma pack(push, 1)
struct ARTICLE
{
    BYTE    byType;             // +0   1 or 2, the client asks for one type
    char    szTitle[80];        // +1
    char    szText[1024];       // +51
    BYTE    abTime[16];         // +451 SQL timestamp, sent as is
};
#pragma pack(pop)

C_ASSERT(sizeof(ARTICLE) == 0x461);

class CArticleManager
{
public:
    // CArticleManager::ctor_global (0x401070)
    CArticleManager();
    // 0x401100: frees the articles.
    virtual ~CArticleManager();                                             // 0

    // ClearArticles (0x401360)
    void ClearArticles();
    // WriteArticles (0x4013e0): BYTE count of the articles of the type, then per article WORD
    // length + title, WORD length + text, 16 bytes time.
    void WriteArticles(ServerFramework::CMassiveMsg* pMsg, BYTE byType);
    // CArticleManager_PushOrAllocBuffer (0x4012d0): DB row trigger; a zeroed row buffer before
    // each row, the filled row is kept. 1.
    int OnArticleRow(DWORD dwQuery, DWORD dwUnused);

public:
    std::list<ARTICLE*> m_listArticle;  // +4
};

C_ASSERT(sizeof(CArticleManager) == 0x10);

// 0x53AE00
extern CArticleManager g_ArticleManager;

// CArticleManager_ReloadFromDB (0x401160): replaces the notices with the rows of
// exec _GetArticleData. False when the query fails.
bool CArticleManager_ReloadFromDB();
