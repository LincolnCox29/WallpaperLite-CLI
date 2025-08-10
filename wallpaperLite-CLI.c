/*******************************************************************************
 *                                                                             *
 *                      WPL - Video Background Engine                          *
 *                      =============================                          *
 *                                                                             *
 *                        ██╗    ██╗██████╗ ██╗                                *
 *                        ██║    ██║██╔══██╗██║                                *
 *                        ██║ █╗ ██║██████╔╝██║                                *
 *                        ██║███╗██║██╔═══╝ ██║                                *
 *                        ╚███╔███╔╝██║     ███████╗                           *
 *                         ╚══╝╚══╝ ╚═╝     ╚══════╝                           *
 *                                                                             *
 *  [License Information]                                                      *
 *  ---------------------                                                      *
 *  • Uses VLC media player (libvlc.dll) under GNU LGPL v2.1+                  *
 *  • VLC is a registered trademark of VideoLAN                                *
 *  • LGPL v2.1+ terms: www.gnu.org/licenses/old-licenses/lgpl-2.1.html        *
 *  • VLC source code: github.com/videolan/vlc                                 *
 *                                                                             *
 *  [Technical Implementation]                                                 *
 *  --------------------------                                                 *
 *  • Dynamically loads VLC DLLs via WinAPI (LoadLibrary/GetProcAddress)       *
 *  • Isolates dependencies in /VLCmin directory                               *
 *  • Desktop background integration via WorkerW injection                     *
 *  • Optimized media playback with hardware acceleration                      *
 *                                                                             *
 *  [Important Usage Notes]                                                    *
 *  -----------------------                                                    *
 *  ! Production requires:                                                     *
 *      - Full error handling for all DLL functions                            *
 *      - Buffer overflow protection in user inputs                            *
 *      - Validation of all function pointers                                  *
 *                                                                             *
 *  ! Distribution requires:                                                   *
 *      - Original VLC licenses in /VLCmin                                     *
 *      - Unmodified VLC DLL structure                                         *
 *      - Clear attribution to VideoLAN                                        *
 *                                                                             *
 *  © 2025 LincolnCox29, MHSPlay | github.com/LincolnCox29/WallpaperLite-CLI   *
 *                                                                             *
 *******************************************************************************/

#include <stdio.h>
#include <windows.h>

typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
typedef void* (*libvlc_new_t)(int, const char**);
typedef void* (*libvlc_media_new_path_t)(void*, const char*);
typedef void* (*libvlc_media_player_new_from_media_t)(void*);
typedef void (*libvlc_media_player_set_hwnd_t)(void*, void*);
typedef void (*libvlc_media_player_play_t)(void*);
typedef void (*libvlc_media_player_stop_t)(void*);
typedef void (*libvlc_media_release_t)(void*);
typedef void (*libvlc_media_player_release_t)(void*);
typedef void (*libvlc_release_t)(void*);

typedef struct EnumWindowsProcParams
{
    int osVersion;
    HWND progman;
    HWND hWorkerW;
} EnumWindowsProcParams;

typedef struct VLC
{
    char videoPath[MAX_PATH];
    void* player;
    void* media;
    void* inst;
    HMODULE libvlc;
    libvlc_new_t libvlc_new;
    libvlc_media_new_path_t libvlc_media_new_path;
    libvlc_media_player_new_from_media_t libvlc_media_player_new_from_media;
    libvlc_media_player_set_hwnd_t libvlc_media_player_set_hwnd;
    libvlc_media_player_play_t libvlc_media_player_play;
    libvlc_media_player_stop_t libvlc_media_player_stop;
    libvlc_media_release_t libvlc_media_release;
    libvlc_media_player_release_t libvlc_media_player_release;
    libvlc_release_t libvlc_release;
} VLC;

VLC vlc = { 0 };

int getWindowsVersion()
{
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (!hMod) 
        return FALSE;

    // Hooking RtlGetVersion() from ntdll.dll
    RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
    FreeLibrary(hMod);
    if (!RtlGetVersion) 
        return FALSE;
    
    // Init os struct
    RTL_OSVERSIONINFOW os = { 0 };
    os.dwOSVersionInfoSize = sizeof(os);
    if (RtlGetVersion(&os) != 0)
        return FALSE;

    if (os.dwMajorVersion != 10)
        return 0;

    return os.dwBuildNumber < 22000 ? 10 : 11;
}

inline void cleanup()
{
    if (vlc.player)
        vlc.libvlc_media_player_release(vlc.player);
    if (vlc.media)
        vlc.libvlc_media_release(vlc.media);
    if (vlc.inst)
        vlc.libvlc_release(vlc.inst);
    if (vlc.libvlc)
        FreeLibrary(vlc.libvlc);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    switch (msg) 
    {
        case WM_CLOSE:
            cleanup();
            DestroyWindow(hWnd);
            return 0;
        case WM_DESTROY:
            cleanup();
            PostQuitMessage(0);
            return 0;
        case WM_QUIT:
            cleanup();
            return 0;
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

BOOL CALLBACK EnumWindowsProc(HWND tophandle, LPARAM lParam) 
{
    EnumWindowsProcParams* params = (EnumWindowsProcParams*)lParam;

    if (params->osVersion == 10)
    {
        // Look for the SHELLDLL_DefView HWND as a child of the current top handle
        // If found, get the WorkerW HWND
        if (FindWindowExA(tophandle, NULL, "SHELLDLL_DefView", NULL) != NULL)
            params->hWorkerW = FindWindowExA(NULL, tophandle, "WorkerW", NULL);
    }
    else
    {
        params->hWorkerW = FindWindowExA(params->progman, NULL, "WorkerW", NULL);
    }

    // Continue enumeration
    return TRUE;
}

HWND GetWorkerW()
{
    EnumWindowsProcParams params =
    {
        getWindowsVersion(),
        FindWindowA("Progman", NULL),
        NULL
    };

    // Create WorkerW via message 
    SendMessageTimeoutA(params.progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, NULL);

    // Find WorkerW
    EnumWindows(EnumWindowsProc, (LPARAM)&params);
    if (params.hWorkerW)
    {
        printf("WorkerW: 0x%p\n", params.hWorkerW);
        return params.hWorkerW;
    }
    else
    {
        printf("WorkerW not found\n");
    }
}

void libvlcLoad()
{
    // Init VLC lib
    SetDllDirectoryA("VLCmin");
    SetEnvironmentVariableA("VLC_PLUGIN_PATH", "VLCmin/plugins");
    vlc.libvlc = LoadLibraryA("VLCmin\\libvlc.dll");
    if (!vlc.libvlc)
    {
        DWORD error = GetLastError();
        printf("Error: %d\n", GetLastError());
        exit(error);
    }

    // Macro for hooking funcs from libvlc.dll
    #define GET_FUNC(name) \
        vlc.name = (name##_t)GetProcAddress(vlc.libvlc, #name); \
        if (!vlc.name) { \
            printf("Failed to get " #name "\n"); \
            cleanup(); \
            exit(1); \
        }

    // Hooking necessary funcs
    GET_FUNC(libvlc_new);
    GET_FUNC(libvlc_media_new_path);
    GET_FUNC(libvlc_media_player_new_from_media);
    GET_FUNC(libvlc_media_player_set_hwnd);
    GET_FUNC(libvlc_media_player_play);
    GET_FUNC(libvlc_media_player_stop);
    GET_FUNC(libvlc_media_release);
    GET_FUNC(libvlc_media_player_release);
    GET_FUNC(libvlc_release);

    #undef GET_FUNC
}

BOOL isActiveWindowFullScreen()
{
    // Get active window
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || !IsWindowVisible(hwnd))
        return FALSE;

    // Rule out desktop
    char className[256];
    GetClassNameA(hwnd, className, sizeof(className));
    if (strcmp(className, "Progman") == 0 || strcmp(className, "WorkerW") == 0)
        return FALSE;

    // Get monitor from active window hwnd 
    HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!hMonitor)
        return FALSE;

    // Get monitor info (size)
    MONITORINFO monitorInfo = { sizeof(monitorInfo) };
    if (!GetMonitorInfo(hMonitor, &monitorInfo))
        return FALSE;

    // Get window size
    RECT windowRect;
    if (!GetWindowRect(hwnd, &windowRect))
        return FALSE;

    // CMP size
    return (windowRect.left   <= monitorInfo.rcMonitor.left   + 1 &&
            windowRect.top    <= monitorInfo.rcMonitor.top    + 1 &&
            windowRect.right  >= monitorInfo.rcMonitor.right  - 1 &&
            windowRect.bottom >= monitorInfo.rcMonitor.bottom - 1);
}

void setPlayerState(BOOL* wasFullscreen)
{
    BOOL isFullscreen = isActiveWindowFullScreen();

    if (isFullscreen != *wasFullscreen)
    {
        if (isFullscreen)
        {
            vlc.libvlc_media_player_stop(vlc.player);
            Sleep(50);
            #ifdef DEBUG
            printf("Stop playing wallpaper\n");
            #endif // DEBUG

        }
        else 
        {
            vlc.libvlc_media_player_play(vlc.player);
            Sleep(50);
            #ifdef DEBUG
            printf("Playing wallpaper again\n");
            #endif // DEBUG
        }
        *wasFullscreen = isFullscreen;
    }
}

inline void parseCommandLineArguments(int argc, char** argv)
{
    if (!argv[1] || argc < 1)
    {
        printf(
            "Error: arg \"videoPath\" not found\n"
            "wallpaperLite-CLI <path-to-video>\n"
        );
        exit(1);
    }
    strncpy_s(vlc.videoPath, sizeof(vlc.videoPath), argv[1], _TRUNCATE);
}

int main(int argc, char* argv[])
{
    parseCommandLineArguments(argc, argv);

    HWND hWorkerW = GetWorkerW();

    libvlcLoad();

    //Init VLC with args
    const char* VLCargs[] = 
    {
        "--no-audio",                   // Disables audio
        "--quiet",                      // Suppressing non-error messages
        "--avcodec-hw=dxva2",           // HW-accelerated
        "--input-repeat=65535",         // Looping
        "--drop-late-frames",           // Dropped frames due to lack of resources
        "--avcodec-fast"                // Enable optimized decoding
    };
    vlc.inst = vlc.libvlc_new(sizeof(VLCargs) / sizeof(VLCargs[0]), VLCargs);
    if (!vlc.inst)
    {
        printf("Failed to initialize VLC\n");
        cleanup();
        return 1;
    }

    // Init media
    vlc.media = vlc.libvlc_media_new_path(vlc.inst, vlc.videoPath);
    if (!vlc.media)
    {
        printf("Failed to load media at %s\n", vlc.videoPath);
        cleanup();
        return 1;
    }

    // Init player 
    vlc.player = vlc.libvlc_media_player_new_from_media(vlc.media);
    if (!vlc.player)
    {
        printf("Failed to create player\n");
        cleanup();
        return 1;
    }

    // VLC hooking WorkerW
    vlc.libvlc_media_player_set_hwnd(vlc.player, hWorkerW);
    printf("Video attached to window 0x%p\n", hWorkerW);

    // Play
    vlc.libvlc_media_player_play(vlc.player);

    // Main loop
    BOOL wasFullscreen = FALSE;
    MSG msg;
    while (TRUE)
    {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        setPlayerState(&wasFullscreen);

        Sleep(100);
    }

	return 0;
}