#include "BSNet/Msg.h"
#include "BSNet/QueMT.h"

#include <process.h>

#include "TestCheck.h"

static CMsg g_Msgs[1000];
static volatile LONG g_nReceived = 0;

static unsigned __stdcall Consumer(void* p)
{
    IQue<CMsg>* pQue = (IQue<CMsg>*)p;
    for (;;)
    {
        CMsg* pMsg = NULL;
        if (!pQue->Pop(&pMsg, INFINITE, FALSE))
            break;
        if (pMsg)
            InterlockedIncrement(&g_nReceived);
    }
    return 0;
}

static void RunThreads(IQue<CMsg>* pQue)
{
    g_nReceived = 0;
    HANDLE h[4];
    for (int i = 0; i < 4; ++i)
        h[i] = (HANDLE)_beginthreadex(NULL, 0, Consumer, pQue, 0, NULL);
    for (int i = 0; i < 1000; ++i)
        CHECK(pQue->Push(&g_Msgs[i]));
    while (g_nReceived < 1000)
        Sleep(1);
    pQue->Stop();
    WaitForMultipleObjects(4, h, TRUE, 5000);
    for (int i = 0; i < 4; ++i)
        CloseHandle(h[i]);
    CHECK(g_nReceived == 1000);
}

int main()
{
    {
        CQueMT<CMsg> que;
        CHECK(que.m_hIOCP != NULL && que.IsEmpty() && !que.IsFull());

        CMsg* p = NULL;
        CHECK(que.Pop(&p, 10, FALSE) == TRUE && p == NULL);     // timeout
        CHECK(que.Push(&g_Msgs[0]) && que.GetSize() == 1);
        CHECK(que.Pop(&p, 10, FALSE) && p == &g_Msgs[0] && que.GetSize() == 0);

        RunThreads(&que);
        CHECK(que.m_bActive == 0);
        CHECK(!que.Push(&g_Msgs[0]));                           // stopped
        CHECK(que.Start() == 0 && que.m_bActive == 1);
    }

    {
        CQueMT9x<CMsg> que;
        CHECK(que.m_hEvent != NULL && que.IsEmpty() && !que.IsFull());

        CMsg* p = &g_Msgs[0];
        CHECK(que.Pop(&p, 10, FALSE) == TRUE && p == NULL);     // timeout
        que.m_nMaxSize = 1;
        CHECK(que.Push(&g_Msgs[0]) && !que.Push(&g_Msgs[1]) && que.IsFull());
        CHECK(que.Pop(&p, 10, FALSE) && p == &g_Msgs[0]);
        que.m_nMaxSize = -1;

        RunThreads(&que);
        CHECK(que.m_bActive == 0);
        CHECK(que.Push(&g_Msgs[5]));                            // 9x queue still accepts
        CHECK(que.Pop(&p, 10, FALSE) == FALSE && p == NULL);
        CHECK(que.Pop(&p, 10, TRUE) == FALSE && p == &g_Msgs[5]);
    }

    return TEST_RESULT("QueTest");
}
