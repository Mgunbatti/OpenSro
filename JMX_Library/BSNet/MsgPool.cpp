#include "MsgPool.h"

#include "BSNet/NetEngineLog.h"

CMsg* CMsgPool::AllocFromFreeQueue()
{
    CMsg* pMsg;
    m_FreeQue.Pop(pMsg);
    if (!pMsg)
        return NULL;

    while (pMsg->m_lInUse)
    {
        NetEngineLog(0x2000001, "someone about to allocate msg already in use !!! [0x%x]",
                     *pMsg->m_pOpcode);
        m_FreeQue.Pop(pMsg);
        if (!pMsg)
            return NULL;
    }

    pMsg->Reset(0);
    InterlockedExchange(&pMsg->m_lInUse, 1);
    m_setInUse.insert(pMsg);
    return pMsg;
}

CMsg* CMsgPool::Alloc()
{
    CSingleLockBS lock(&m_cs, TRUE);
    CMsg* pMsg = AllocFromFreeQueue();
    if (!pMsg)
    {
        AllocItemBlock();
        pMsg = AllocFromFreeQueue();
    }
    ++m_nInUse;
    return pMsg;
}

void CMsgPool::Free(CMsg* pMsg)
{
    CSingleLockBS lock(&m_cs, TRUE);
    if (!pMsg)
        return;

    if (!pMsg->m_lInUse)
    {
        NetEngineLog(0x2000001, "someone about to free msg already freed !!! [0x%x]",
                     *pMsg->m_pOpcode);
        return;
    }

    InterlockedExchange(&pMsg->m_lInUse, 0);
    m_FreeQue.Push(pMsg);
    --m_nInUse;
    std::set<CMsg*>::iterator it = m_setInUse.find(pMsg);
    if (it != m_setInUse.end())
        m_setInUse.erase(it);
}
