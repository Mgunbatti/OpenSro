#include "MsgPool.h"

#include <map>

#include "BSNet/NetEngineLog.h"

void CMsgPool::DumpEntries()
{
    typedef std::map<DWORD, DWORD> CountMap;
    CountMap mapCount;
    PFN_DUMP pfnDump = (PFN_DUMP)(DWORD_PTR)m_dw4;
    if (!pfnDump)
        return;

    // BUG(NET-010): walks the in-use set without the pool lock while other threads allocate
    // and free messages.
    for (std::set<CMsg*>::iterator it = m_setInUse.begin(); it != m_setInUse.end(); ++it)
    {
        CMsg* pMsg = *it;
        DWORD dwOpcode = *pMsg->m_pOpcode;
        if (dwOpcode == 0x2209)
            dwOpcode = *(WORD*)pMsg->PtrAt(10);
        CountMap::iterator itCount = mapCount.find(dwOpcode);
        if (itCount == mapCount.end())
            mapCount.insert(CountMap::value_type(dwOpcode, 1));
        else
            ++itCount->second;
    }

    DWORD dwTotal = 0;
    for (CountMap::iterator it = mapCount.begin(); it != mapCount.end(); ++it)
    {
        pfnDump(it->first, it->second);
        dwTotal += it->second;
    }
    pfnDump(0xFFFF, dwTotal);
}

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
#ifndef JMX_FRAMEWORK_2007
    // FarmManager 0x4672b0 does not track the message.
    m_setInUse.insert(pMsg);
#endif
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
#ifndef JMX_FRAMEWORK_2007
    // FarmManager 0x466220 does not track the message.
    std::set<CMsg*>::iterator it = m_setInUse.find(pMsg);
    if (it != m_setInUse.end())
        m_setInUse.erase(it);
#endif
}
