// Waits that keep wall-clock checks away from second and day boundaries.
#pragma once

#include <windows.h>

// Returns just after the local time passed a whole second. A timestamp built (in whole
// seconds) right after this lies close to 1 s before the next second boundary.
inline void WaitForSecondStart()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    WORD wSecond = st.wSecond;
    do
    {
        Sleep(1);
        GetLocalTime(&st);
    } while (st.wSecond == wSecond);
}

// Returns when more than nSeconds are left before local midnight (sleeps past midnight when
// fewer are left), so checks of "today" and of the time to midnight see a single day.
inline void WaitClearOfMidnight(int nSeconds)
{
    for (;;)
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        int nLeft = 86400 - (st.wHour * 3600 + st.wMinute * 60 + st.wSecond);
        if (nLeft > nSeconds)
            return;
        Sleep(250);
    }
}
