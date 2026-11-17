// Stand-in for a server module started by MachineTest (copied to a temp directory under the
// test module's name). Like a server it creates the single-instance semaphore named after its
// exe. Names containing "NoWnd" only wait; the others open a window whose class and title are the
// name and exit when it gets WM_CLOSE. Either way the program ends by itself after 30 s.

#include <windows.h>
#include <string.h>

static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

int main()
{
    char szPath[MAX_PATH];
    GetModuleFileNameA(NULL, szPath, MAX_PATH);
    const char* pszName = strrchr(szPath, '\\');
    pszName = pszName ? pszName + 1 : szPath;
    char szName[MAX_PATH];
    strcpy_s(szName, pszName);
    char* pszExt = strrchr(szName, '.');
    if (pszExt)
        *pszExt = 0;

    HANDLE hSemaphore = CreateSemaphoreA(NULL, 0, 1, szName);
    if (strstr(szName, "NoWnd"))
    {
        Sleep(30000);
        CloseHandle(hSemaphore);
        return 0;
    }

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = szName;
    RegisterClassA(&wc);
    HWND hWnd = CreateWindowA(szName, szName, WS_OVERLAPPED, 0, 0, 10, 10, NULL, NULL,
                              wc.hInstance, NULL);
    SetTimer(hWnd, 1, 30000, NULL);
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0)
    {
        if (msg.message == WM_TIMER)
            DestroyWindow(hWnd);
        DispatchMessageA(&msg);
    }
    CloseHandle(hSemaphore);
    return 7;
}
