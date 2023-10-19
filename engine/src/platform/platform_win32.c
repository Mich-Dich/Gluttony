#include "platform.h"

#if GL_PLATFORM_WINDOWS

    #include "core/logger.h"
    #include <windows.h>
    #include <windowsx.h>
    #include <stdlib.h>

    typedef struct PLatformInternalState {

        HINSTANCE h_instance;
        HWND hwnd;

    } PLatformInternalState;

    static double clockFrequency;
    static LARGE_INTEGER startTime;

    LRESULT CALLBACK win32ProcessMessage(HWND hwnd, u32 msg, WPARAM w_param, LPARAM l_param);

    bool8 platform_Startup(
        platformState* platState,
        const char* applicationName,
        int32 x,
        int32 y,
        int32 width,
        int32 height) {

            platState->internalState = malloc(sizeof(PLatformInternalState));
            PLatformInternalState *state = (PLatformInternalState *)platState->internalState;

            state->h_instance = GetModuleHandleA(0);

            HICON icon = LoadIcon(state->h_instance, IDI_APPLICATION);
            WNDCLASSA wc;
            memset(&wc, 0, sizeof(wc));
            wc.style = CS_DBLCLKS;
            wc.lpfnWndProc = win32ProcessMessage;
            wc.cbClsExtra = 0;
            wc.cbWndExtra = 0;
            wc.hInstance = state->h_instance;
            wc.hIcon = icon;
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.hbrBackground = NULL;
            wc.lpszClassName = "gluttony_window_class";

            if (!RegisterClassA(&wc)) {
                
                MessageBoxA(0, "Window registration FAILED", "Error", MB_ICONEXCLAMATION | MB_OK);
                return FALSE;
            }

            u32 client_X = x;
            u32 client_Y = y;
            u32 client_width = width;
            u32 client_height = height;

            
            u32 window_X = client_X;
            u32 window_Y = client_Y;
            u32 window_width = client_width;
            u32 window_height = client_height;

            u32 window_style = WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION;
            u32 window_ex_style = WS_EX_APPWINDOW;

            window_style |= WS_MAXIMIZEBOX;
            window_style |= WS_MINIMIZEBOX;
            window_style |= WS_THICKFRAME;

            // Calc size of border
            RECT border_rect = {0, 0, 0, 0};
            AdjustWindowRectEx(&border_rect, window_style, 0, window_ex_style);

            // Adjust window params
            window_X += border_rect.left;
            window_Y += border_rect.top;
            window_width += border_rect.right - border_rect.left;
            window_height += border_rect.bottom - border_rect.top;
            
            HWND handle = CreateWindowExA(window_ex_style, "gluttony_window_class", applicationName, window_style, window_X, window_Y, window_width, window_height, 0, 0, state->h_instance, 0);

            if (!handle) {
                
                MessageBoxA(0, "Window creation FAILED", "Error", MB_ICONEXCLAMATION | MB_OK);
                GL_FATAL("Window creation FAILED");
                return FALSE;
            } else {
                state->hwnd = handle;
            }
            

            // Show the window
            bool32 should_activate = 1;
            int32 show_winow_commend_flags = should_activate ? SW_SHOW : SW_SHOWNOACTIVATE;
            ShowWindow(state->hwnd, show_winow_commend_flags);

            // Set statrTime for Clock
            LARGE_INTEGER frequency;
            QueryPerformanceFrequency(&frequency);
            clockFrequency = 1.0 / (double)frequency.QuadPart;
            QueryPerformanceCounter(&startTime);

            return TRUE;
        };

    void platform_Shutdown(platformState *platState) {

        PLatformInternalState *state = (PLatformInternalState *)platState->internalState;

        if (state->hwnd) {

            DestroyWindow(state->hwnd);
            state->hwnd = 0;
        }
        
    }

    bool8 platform_Push_messages(platformState* state) {

        MSG message;

        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {

            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        return TRUE;
    }

    // TODO: Temporary solution
    void* platform_allocate_memory(u64 size, bool8 aligned) {

        return malloc(size);
    }

    // TODO: Temporary solution
    void  platform_free_memory(void* block, bool8 aligned) {

        return free(block);
    }

    // TODO: Temporary solution
    void* platform_zero_memory(void* block, u64 size) {

        return memset(block, 0, size);
    }

    // TODO: Temporary solution
    void* platform_copy_memory(void* dest, const void* source, u64 size) {

        return memcpy(dest, source, size);
    }

    // TODO: Temporary solution
    void* platform_set_memory(void* dest, int32 value, u64 size) {

        return memset(dest, value, size);
    }

    void platform_console_write(const char* message, u8 color) {

        // SetText Coloring
        HANDLE ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        static u8 level [6] = {64, 4, 6, 2, 1, 8};
        SetConsoleTextAttribute(ConsoleHandle, level[color]);

        // output to DebugConsole
        OutputDebugStringA(message);

        // output to Command-Prompt
        u64 length = strlen(message);
        LPDWORD numberWritten = 0;
        WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), message, (DWORD)length, numberWritten, 0);
    }

    void platform_console_write_error(const char* message, u8 color) {

        // SetText Coloring
        HANDLE ConsoleHandle = GetStdHandle(STD_ERROR_HANDLE);
        static u8 level [6] = {64, 4, 6, 2, 1, 8};
        SetConsoleTextAttribute(ConsoleHandle, level[color]);

        // output to DebugConsole
        OutputDebugStringA(message);

        // output to Command-Prompt
        u64 length = strlen(message);
        LPDWORD numberWritten = 0;
        WriteConsoleA(GetStdHandle(STD_ERROR_HANDLE), message, (DWORD)length, numberWritten, 0);
    }

    double platform_get_absolute_Time() {

        LARGE_INTEGER timeNow;
        QueryPerformanceCounter(&timeNow);
        return (double)timeNow.QuadPart * clockFrequency;
    }


    void platform_sleep(u64 ms) {

        Sleep(ms);
    }


    LRESULT CALLBACK win32ProcessMessage(HWND hwnd, u32 msg, WPARAM w_param, LPARAM l_param) {

        switch (msg) {
            case WM_ERASEBKGND:
                // Notify OS that ersing will be handled by application, to prevent flicker
                return 1;

            case WM_CLOSE:
                // TODO: Fire application close Event
                return 0;

            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;

            case WM_SIZE: {
                
                // RECT r;
                // GetClientRect(hwnd, &r);
                // u32 width = r.right - r.left;
                // u32 height = r.bottom - r.top;

                // TODO: Fire resize Event
            } break;

            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYUP: {

                // bool8 pressed = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
                // TODO: Fire input Event

            } break;

            case WM_MOUSEMOVE: {
                
                // int32 PosX = GET_X_LPARAM(l_param);
                // int32 PosY = GET_Y_LPARAM(l_param);
                // TODO: Fire input Event
            } break;

            case WM_MOUSEWHEEL: {
                
                // int32 zDelta = GET_MOUSEORKEY_LPARAM(l_param);
                // if (zDelta != 0)
                //     zDelta = (zDelta < 0) ? -1 : 1;     // Flatten given amount to abs(1)
                // // TODO: Fire input Event

            } break;

            case WM_LBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_MBUTTONUP:
            case WM_RBUTTONUP: {

                // bool8 pressed = (msg == WM_LBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_RBUTTONDOWN);
                // TODO: Fire input Event

            } break;
            
            default:
                // Something else
                break;
        }

        return DefWindowProcA(hwnd, msg, w_param, l_param);
    }

#endif