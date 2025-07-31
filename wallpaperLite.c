#include <stdio.h>
#include <windows.h>

typedef void* (*libvlc_new_t)(int, const char**);
typedef void* (*libvlc_media_new_path_t)(void*, const char*);
typedef void* (*libvlc_media_player_new_from_media_t)(void*);
typedef void (*libvlc_media_player_set_hwnd_t)(void*, void*);
typedef void (*libvlc_media_player_play_t)(void*);
typedef void (*libvlc_media_release_t)(void*);
typedef void (*libvlc_media_player_release_t)(void*);
typedef void (*libvlc_release_t)(void*);
typedef void (*libvlc_media_player_set_media)(void*, void*);
typedef struct libvlc_event_manager_t libvlc_event_manager_t; 
typedef struct libvlc_event_t libvlc_event_t;
typedef enum libvlc_event_e {
    libvlc_MediaPlayerEndReached = 265
} libvlc_event_e;
typedef void (*libvlc_event_callback_t)(const libvlc_event_t*, void*);
typedef libvlc_event_manager_t* (*libvlc_media_player_event_manager_t)(void*);
typedef int (*libvlc_event_attach_t)(libvlc_event_manager_t*, libvlc_event_e, libvlc_event_callback_t, void*);

typedef struct VLC
{
    const char videoPath[260];
    void* player;
    void* media;
    void* inst;
    HMODULE libvlc;
    libvlc_new_t libvlc_new;
    libvlc_media_new_path_t libvlc_media_new_path;
    libvlc_media_player_new_from_media_t player_new;
    libvlc_media_player_set_hwnd_t set_hwnd;
    libvlc_media_player_play_t player_play;
    libvlc_media_release_t media_release;
    libvlc_media_player_release_t player_release;
    libvlc_release_t libvlc_release;
    libvlc_media_player_event_manager_t event_manager;
    libvlc_event_attach_t event_attach;
    libvlc_media_player_set_media player_set_media;
} VLC;

VLC vlc = { 0 };

HWND hWorkerW;

inline void cleanup()
{
    vlc.player_release(vlc.player);
    vlc.media_release(vlc.media);
    vlc.libvlc_release(vlc.inst);
    FreeLibrary(vlc.libvlc);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) 
    {
        case WM_CLOSE:
            cleanup();
            DestroyWindow(hWnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

BOOL CALLBACK EnumWindowsProc(HWND tophandle, LPARAM lParam) 
{
    HWND shellView = FindWindowExA(tophandle, NULL, "SHELLDLL_DefView", NULL);

    if (shellView != NULL)
        hWorkerW = FindWindowExA(NULL, tophandle, "WorkerW", NULL);

    return TRUE;
}

HWND GetWorkerW()
{
    HWND progman = FindWindowA("Progman", NULL);

    SendMessageTimeoutA(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, NULL);

    EnumWindows(EnumWindowsProc, 0);
    return hWorkerW;
}

void libvlcLoad()
{
    SetDllDirectoryA("VLCmin");
    SetEnvironmentVariableA("VLC_PLUGIN_PATH", "VLCmin/plugins");
    vlc.libvlc = LoadLibraryA("VLCmin\\libvlc.dll");
    if (!vlc.libvlc)
    {
        DWORD error = GetLastError();
        printf("Error: %d\n", error);
        exit(error);
    }
    vlc.libvlc_new = (libvlc_new_t)GetProcAddress(vlc.libvlc, "libvlc_new");
    vlc.libvlc_media_new_path = (libvlc_media_new_path_t)GetProcAddress(vlc.libvlc, "libvlc_media_new_path");
    vlc.player_new = (libvlc_media_player_new_from_media_t)GetProcAddress(vlc.libvlc, "libvlc_media_player_new_from_media");
    vlc.set_hwnd = (libvlc_media_player_set_hwnd_t)GetProcAddress(vlc.libvlc, "libvlc_media_player_set_hwnd");
    vlc.player_play = (libvlc_media_player_play_t)GetProcAddress(vlc.libvlc, "libvlc_media_player_play");
    vlc.media_release = (libvlc_media_release_t)GetProcAddress(vlc.libvlc, "libvlc_media_release");
    vlc.player_release = (libvlc_media_player_release_t)GetProcAddress(vlc.libvlc, "libvlc_media_player_release");
    vlc.libvlc_release = (libvlc_release_t)GetProcAddress(vlc.libvlc, "libvlc_release");
    vlc.event_manager = (libvlc_media_player_event_manager_t)GetProcAddress(vlc.libvlc, "libvlc_media_player_event_manager");
    vlc.event_attach = (libvlc_event_attach_t)GetProcAddress(vlc.libvlc, "libvlc_event_attach");
    vlc.player_set_media = (libvlc_media_player_set_media)GetProcAddress(vlc.libvlc, "libvlc_media_player_set_media");
}

int main()
{
    HWND hWorkerW = GetWorkerW();
    if (hWorkerW) 
        printf("WorkerW: 0x%p\n", hWorkerW);
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

    vlc.player = vlc.player_new(vlc.media);
    if (!vlc.player)
    {
        printf("Failed to create player\n");
        cleanup();
        return 1;
    }

    vlc.set_hwnd(vlc.player, hWorkerW);
    printf("Video attached to window 0x%p\n", hWorkerW);

    vlc.player_play(vlc.player);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) 
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

	return 0;
}