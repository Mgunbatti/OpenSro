#pragma once

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

#define TEST_RESULT(name)                                                   \
    (printf(g_nFailed ? name ": %d failed\n" : name ": all passed\n", g_nFailed), \
     g_nFailed ? 1 : 0)
