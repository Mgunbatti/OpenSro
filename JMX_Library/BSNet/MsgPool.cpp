#include "MsgPool.h"

#include <map>
#ifdef JMX_FRAMEWORK_2011
#include <stdio.h>
#include <string.h>
#include <strsafe.h>
#endif

#include "BSNet/NetEngineLog.h"

#ifdef JMX_FRAMEWORK_2011
void CMsgPool::DumpEntries(int nThreshold)
{
    typedef std::map<DWORD, DWORD> CountMap;
    CountMap mapCount;
    FILE* pFile = NULL;
    DWORD nLines = 0;
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
        DWORD dwCount = 0;
        if (itCount == mapCount.end())
        {
            mapCount.insert(CountMap::value_type(dwOpcode, 1));
        }
        else
        {
            ++itCount->second;
            dwCount = itCount->second;
        }

        if (nThreshold > 10000 && dwCount > (DWORD)(nThreshold / 2) && dwOpcode == 0x200A &&
            nLines < 1000)
        {
            char szText[504];
            WORD wLen = *(WORD*)pMsg->PtrAt(10);
            BYTE* pText = pMsg->PtrAt(12);
            if (wLen > 500)
                wLen = 500;
            memcpy(szText, pText, wLen);
            szText[wLen] = 0;

            SYSTEMTIME st;
            GetLocalTime(&st);
            if (!pFile)
            {
                char szFile[MAX_PATH];
                StringCchPrintfA(szFile, MAX_PATH, "%02d-%02d-%02d-%02d_LogContent.txt",
                                 st.wMonth, st.wDay, st.wHour, st.wMinute);
                if (fopen_s(&pFile, szFile, "a"))
                    continue;
            }
            fprintf(pFile, "%04d-%02d-%02d\t%02d:%02d:%02d\t%s\n", st.wYear, st.wMonth, st.wDay,
                    st.wHour, st.wMinute, st.wSecond, szText);
            ++nLines;
        }
    }
    if (pFile)
        fclose(pFile);

    DWORD dwTotal = 0;
    for (CountMap::iterator it = mapCount.begin(); it != mapCount.end(); ++it)
    {
        pfnDump(it->first, it->second);
        dwTotal += it->second;
    }
    pfnDump(0xFFFF, dwTotal);
}

BOOL CMsgPool::AllocItemBlock()
{
    if (m_FreeQue.GetSize())
        return FALSE;

    try
    {
        int n = m_nItemsPerBlock;
        CMsg* pBlock = new CMsg[n];
        if (!pBlock)
        {
            NetEngineLog(0x2000000, "CChunkAllocatorMT_For_CMsg::AllocItemBlock() Failed");
            return FALSE;
        }
        m_Blocks.push_back(pBlock);
        for (int i = 0; i < m_nItemsPerBlock; ++i)
            m_FreeQue.Push(&pBlock[i]);
        m_nCapacity = m_nItemsPerBlock * (DWORD)m_Blocks.size();
        return TRUE;
    }
    catch (...)
    {
        WriteAllocatorExceptionLog();
        return FALSE;
    }
}

void CMsgPool::WriteAllocatorExceptionLog()
{
    FILE* pFile = NULL;
    if (fopen_s(&pFile, "MsgAllocatorException.log", "a"))
        return;

    typedef std::map<DWORD, DWORD> CountMap;
    CountMap mapCount;
    for (std::set<CMsg*>::iterator it = m_setInUse.begin(); it != m_setInUse.end(); ++it)
    {
        DWORD dwOpcode = *(*it)->m_pOpcode;
        if (dwOpcode == 0x2209)
            dwOpcode = *(WORD*)(*it)->PtrAt(10);
        CountMap::iterator itCount = mapCount.find(dwOpcode);
        if (itCount == mapCount.end())
            mapCount.insert(CountMap::value_type(dwOpcode, 1));
        else
            ++itCount->second;
    }

    DWORD dwTotal = 0;
    for (CountMap::iterator it = mapCount.begin(); it != mapCount.end(); ++it)
    {
        fprintf_s(pFile, "[%s] \tMsgID : 0x%X \t %d\r\n", m_strName.c_str(), it->first,
                  it->second);
        dwTotal += it->second;
    }
    fprintf_s(pFile, "Total Count %d\r\n\r\n", dwTotal);
    fclose(pFile);
}
#else
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
#endif

CMsg* CMsgPool::AllocFromFreeQueue()
{
    CMsg* pMsg;
    m_FreeQue.Pop(pMsg);
    if (!pMsg)
        return NULL;

    while (pMsg->m_lInUse)
    {
#ifdef JMX_FRAMEWORK_2011
        // AgentServer 0x46fcc0
        NetEngineLog(0x2000001, "someone try to allocate msg which is already in use !!! [0x%x]",
                     *pMsg->m_pOpcode);
#else
        NetEngineLog(0x2000001, "someone about to allocate msg already in use !!! [0x%x]",
                     *pMsg->m_pOpcode);
#endif
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
#ifdef JMX_FRAMEWORK_2011
        // AgentServer 0x46e2f0
        NetEngineLog(0x2000001, "someone try to free msg already freed !!! [0x%x]",
                     *pMsg->m_pOpcode);
#else
        NetEngineLog(0x2000001, "someone about to free msg already freed !!! [0x%x]",
                     *pMsg->m_pOpcode);
#endif
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
