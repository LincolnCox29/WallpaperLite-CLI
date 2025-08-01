#include <stdio.h>
#include <windows.h>

typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
typedef void* (*libvlc_new_t)(int, const char**);
typedef void* (*libvlc_media_new_path_t)(void*, const char*);
typedef void* (*libvlc_media_player_new_from_media_t)(void*);
typedef void (*libvlc_media_player_set_hwnd_t)(void*, void*);
typedef void (*libvlc_media_player_play_t)(void*);
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
    const char videoPath[260];
    void* player;
    void* media;
    void* inst;
    HMODULE libvlc;
    libvlc_new_t libvlc_new;
    libvlc_media_new_path_t libvlc_media_new_path;
    libvlc_media_player_new_from_media_t libvlc_media_player_new_from_media;
    libvlc_media_player_set_hwnd_t libvlc_media_player_set_hwnd;
    libvlc_media_player_play_t libvlc_media_player_play;
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

    RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
    FreeLibrary(hMod);
    if (!RtlGetVersion) 
        return FALSE;

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
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

BOOL CALLBACK EnumWindowsProc(HWND tophandle, LPARAM lParam) 
{
    EnumWindowsProcParams* params = (EnumWindowsProcParams*)lParam;

    if (params->osVersion == 10)
    {
        if (FindWindowExA(tophandle, NULL, "SHELLDLL_DefView", NULL) != NULL)
            params->hWorkerW = FindWindowExA(NULL, tophandle, "WorkerW", NULL);
    }
    else
    {
        params->hWorkerW = FindWindowExA(params->progman, NULL, "WorkerW", NULL);
    }

    return TRUE;
}

inline void GetWorkerW(EnumWindowsProcParams* params)
{
    SendMessageTimeoutA(params->progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, NULL);

    EnumWindows(EnumWindowsProc, (LPARAM)params);
}

void libvlcLoad()
{
    SetDllDirectoryA("VLCmin");
    SetEnvironmentVariableA("VLC_PLUGIN_PATH", "VLCmin/plugins");
    vlc.libvlc = LoadLibraryA("VLCmin\\libvlc.dll");
    if (!vlc.libvlc)
    {
        DWORD error = GetLastError();
        printf("Error: %d\n", GetLastError());
        exit(error);
    }

    #define GET_FUNC(name) \
        vlc.name = (name##_t)GetProcAddress(vlc.libvlc, #name); \
        if (!vlc.name) { \
            printf("Failed to get " #name "\n"); \
            cleanup(); \
            exit(1); \
        }

    GET_FUNC(libvlc_new);
    GET_FUNC(libvlc_media_new_path);
    GET_FUNC(libvlc_media_player_new_from_media);
    GET_FUNC(libvlc_media_player_set_hwnd);
    GET_FUNC(libvlc_media_player_play);
    GET_FUNC(libvlc_media_release);
    GET_FUNC(libvlc_media_player_release);
    GET_FUNC(libvlc_release);

    #undef GET_FUNC
}

int main()
{
    EnumWindowsProcParams params =
    {
        getWindowsVersion(),
        FindWindowA("Progman", NULL),
        NULL
    };

    GetWorkerW(&params);
    if (params.hWorkerW) 
        printf("WorkerW: 0x%p\n", params.hWorkerW);
    else
        printf("WorkerW not found\n");

    libvlcLoad();

    const char* VLCargs[] = {"--no-audio", "--quiet", "--avcodec-hw=dxva2", "--input-repeat=65535" };
    vlc.inst = vlc.libvlc_new(sizeof(VLCargs) / sizeof(VLCargs[0]), VLCargs);
    if (!vlc.inst)
    {
        printf("Failed to initialize VLC\n");
        cleanup();
        return 1;
    }

    printf("%s", "Path to your video: ");
    scanf_s("%s", vlc.videoPath, 260);
    vlc.media = vlc.libvlc_media_new_path(vlc.inst, vlc.videoPath);
    if (!vlc.media)
    {
        printf("Failed to load media at %s\n", vlc.videoPath);
        cleanup();
        return 1;
    }

    vlc.player = vlc.libvlc_media_player_new_from_media(vlc.media);
    if (!vlc.player)
    {
        printf("Failed to create player\n");
        cleanup();
        return 1;
    }

    vlc.libvlc_media_player_set_hwnd(vlc.player, params.hWorkerW);
    printf("Video attached to window 0x%p\n", params.hWorkerW);

    vlc.libvlc_media_player_play(vlc.player);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) 
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

	return 0;
}