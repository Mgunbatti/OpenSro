#include "CASChat.h"

CCASChatManager g_CASChatManager;

CCASChatManager::CCASChatManager()
    : m_IDPool(1000, 0)
{
    m_pProcess = NULL;
}

CCASChatManager::~CCASChatManager()
{
    for (std::map<DWORD, CASChatSession*>::iterator it = m_mapSession.begin();
         it != m_mapSession.end(); ++it)
    {
        if (it->second)
            delete it->second;
    }
    m_mapSession.clear();
}
