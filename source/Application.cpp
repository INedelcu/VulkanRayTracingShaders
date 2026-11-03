
#include "Application.h"
#include "Renderer.h"

#define APP_CLASS_NAME "VULKAN_RAY_TRACING_SHADERS"

bool  g_Dragging = false;
POINT g_LastMouse = {};

LRESULT CALLBACK MainWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
        g_Dragging = true;
        g_LastMouse = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        SetCapture(hWnd);
        return 0;

    case WM_LBUTTONUP:
        g_Dragging = false;
        ReleaseCapture();
        return 0;

    case WM_MOUSEMOVE:
        if (g_Dragging)
        {
            // Physical pixels; scaled to 100% scaling units so the camera turns the same at any display scale.
            const float scale = (float)USER_DEFAULT_SCREEN_DPI / GetDpiForWindow(hWnd);
            POINT mouse = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            OnMouseDrag((mouse.x - g_LastMouse.x) * scale, (mouse.y - g_LastMouse.y) * scale);
            g_LastMouse = mouse;
        }
        return 0;

    case WM_MOUSEWHEEL:
        OnMouseWheel((float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA);
        return 0;

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            OnWindowResized(LOWORD(lParam), HIWORD(lParam));
            Render();
        }
        return 0;

    case WM_GETMINMAXINFO:
    {
        const UINT dpi = GetDpiForWindow(hWnd);
        ((MINMAXINFO*)lParam)->ptMinTrackSize = { MulDiv(320, dpi, USER_DEFAULT_SCREEN_DPI), MulDiv(240, dpi, USER_DEFAULT_SCREEN_DPI) };
        return 0;
    }

    case WM_DPICHANGED:
    {
        // The window moved to a monitor with a different scale; the suggested rectangle keeps its size on screen.
        const RECT* suggested = (const RECT*)lParam;
        SetWindowPos(hWnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int nCmdShow)
{
    // Must happen before any window exists. Sizes, mouse positions and the swapchain are then in physical pixels.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

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

    const UINT dpi = GetDpiForSystem();

    RECT rc;
    SetRect(&rc, 0, 0, MulDiv(1920, dpi, USER_DEFAULT_SCREEN_DPI), MulDiv(1080, dpi, USER_DEFAULT_SCREEN_DPI));

    DWORD windowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    AdjustWindowRectExForDpi(&rc, windowStyle, FALSE, 0, dpi);

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
