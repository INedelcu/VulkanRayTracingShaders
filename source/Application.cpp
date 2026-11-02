
#include "Application.h"
#include "Renderer.h"

#define APP_CLASS_NAME "VULKAN_RAY_TRACING_SHADERS"

LRESULT CALLBACK MainWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            OnWindowResized(LOWORD(lParam), HIWORD(lParam));
            Render();
        }
        return 0;

    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lParam)->ptMinTrackSize = { 320, 240 };
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int nCmdShow)
{
    WNDCLASS wc = {};

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpszClassName = APP_CLASS_NAME;
    wc.hInstance = hInstance;
    wc.lpfnWndProc = (WNDPROC)MainWindowProc;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    if (!RegisterClass(&wc))
        return 0;

    RECT rc;
    SetRect(&rc, 0, 0, 1920, 1080);

    DWORD windowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    AdjustWindowRect(&rc, windowStyle, FALSE);

    HWND hWnd = CreateWindow(
        APP_CLASS_NAME,
        "Vulkan Ray Tracing Shaders",
        windowStyle,
        0,
        0,
        rc.right - rc.left,
        rc.bottom - rc.top,
        NULL,
        NULL,
        hInstance,
        NULL);

    if (hWnd == NULL)
        return 0;

    ShowWindow(hWnd, SW_NORMAL);

    if (!InitializeRenderer(hInstance, hWnd))
        return 0;

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else if (IsIconic(hWnd))
        {
            WaitMessage();
        }
        else
        {
            Render();
        }
    }

    ShutdownRenderer();

    UnregisterClass(APP_CLASS_NAME, hInstance);

    return (int)msg.wParam;
}
