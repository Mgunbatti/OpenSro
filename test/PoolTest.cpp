#include "BSNet/MsgPool.h"
#include "BSNet/NetEngineLog.h"

#include <string.h>

#include "TestCheck.h"

static char g_szLastLog[1024];
static int g_nLogs = 0;

// NetEngineLog always passes "%s" plus the formatted text.
static int CapturingLog(DWORD dwFlags, const char* pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    const char* pszText = va_arg(args, const char*);
    va_end(args);
    strcpy_s(g_szLastLog, pszText);
    ++g_nLogs;
    return (int)dwFlags;
}

struct CItem
{
    int nInit;
    void Init(DWORD dw) { nInit = (int)dw; }
};

int main()
{
    g_pfnNetEngineLog = CapturingLog;

    // generic allocator: grows by one block, runs the init callback, counts usage
    {
        CChunkAllocatorST<CItem> alloc(&CItem::Init, 7, 3);
        CItem* a = alloc.Alloc();
        CItem* b = alloc.Alloc();
        CItem* c = alloc.Alloc();
        CHECK(a && b && c && a->nInit == 7);
        CHECK(alloc.m_Blocks.size() == 1 && alloc.m_nCapacity == 3 && alloc.m_nInUse == 3);
        CItem* d = alloc.Alloc();
        CHECK(d && alloc.m_Blocks.size() == 2 && alloc.m_nCapacity == 6);
        alloc.Free(a);
        CHECK(alloc.m_nInUse == 3 && alloc.m_FreeQue.GetSize() == 3);
        CHECK(alloc.AllocItemBlock() == FALSE);     // free list not empty
        DWORD t, u, f;
        alloc.GetStats(t, u, f);
        CHECK(t == 6 && u == 3 && f == 3);
    }

    // free-list limit drops objects once full
    {
        CChunkAllocatorST<CItem> alloc(NULL, 0, 2);
        alloc.m_FreeQue.m_nMaxSize = 1;
        alloc.AllocItemBlock();
        CHECK(alloc.m_FreeQue.GetSize() == 1);
    }

    // CMsg pool
    {
        CMsgPool pool;
        CMsg* p = pool.Alloc();
        CHECK(p && p->m_lInUse == 1 && p->m_wWritePos == MSG_HEADER_SIZE);
        CHECK(pool.m_setInUse.size() == 1 && pool.m_nInUse == 1);
        CHECK(pool.m_nCapacity == 500);

        p->SetOpcode(0x1234);
        pool.Free(p);
        CHECK(p->m_lInUse == 0 && pool.m_setInUse.empty() && pool.m_nInUse == 0);

        g_nLogs = 0;
        pool.Free(p);
        CHECK(g_nLogs == 1 && strcmp(g_szLastLog, "someone about to free msg already freed !!! [0x1234]") == 0);

        // a message still flagged in use is skipped when it comes out of the free list again
        CMsg* q = pool.m_FreeQue.m_Que.front();
        q->m_lInUse = 1;
        q->SetOpcode(0x5678);
        g_nLogs = 0;
        CMsg* r = pool.Alloc();
        CHECK(r != q);
        CHECK(g_nLogs == 1 && strcmp(g_szLastLog, "someone about to allocate msg already in use !!! [0x5678]") == 0);
        q->m_lInUse = 0;
        pool.Free(r);
    }

    return TEST_RESULT("PoolTest");
}
