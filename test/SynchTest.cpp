#include "BSLib/thread/SynchObject.h"

#include <process.h>
#include <stdio.h>

static int g_nFailed = 0;

#define CHECK(expr)                                                         \
    do                                                                      \
    {                                                                       \
        if (!(expr))                                                        \
        {                                                                   \
            printf("FAILED line %d: %s\n", __LINE__, #expr);                \
            ++g_nFailed;                                                    \
        }                                                                   \
    } while (0)

static CCriticalSectionBS g_cs;
static volatile LONG g_nCounter = 0;

static unsigned __stdcall Worker(void*)
{
    for (int i = 0; i < 100000; ++i)
    {
        CSingleLockBS lock(&g_cs, TRUE);
        LONG n = g_nCounter;
        g_nCounter = n + 1;
    }
    return 0;
}

int main()
{
    CCriticalSectionBS cs;
    CHECK(cs.IsCriticalSection());
    CHECK(cs.m_hObject == NULL);
    CHECK(cs.Unlock(1, NULL) == TRUE);      // base no-op overload

    {
        CSingleLockBS lock(&cs);
        CHECK(!lock.IsLocked());
        CHECK(lock.Lock());
        CHECK(lock.IsLocked());
        CHECK(lock.Unlock());
        CHECK(!lock.IsLocked());
    }

    HANDLE h[4];
    for (int i = 0; i < 4; ++i)
        h[i] = (HANDLE)_beginthreadex(NULL, 0, Worker, NULL, 0, NULL);
    WaitForMultipleObjects(4, h, TRUE, INFINITE);
    for (int i = 0; i < 4; ++i)
        CloseHandle(h[i]);
    CHECK(g_nCounter == 400000);

    printf(g_nFailed ? "SynchTest: %d failed\n" : "SynchTest: all passed\n", g_nFailed);
    return g_nFailed ? 1 : 0;
}
