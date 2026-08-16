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
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <windows.h>

 /* -------------------------------------------------------------------------
  * libvlc function pointer typedefs
  * ---------------------------------------------------------------------- */
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
typedef void (*libvlc_audio_set_mute_t)(void*, int);
typedef int (*libvlc_audio_set_volume_t)(void*, int);
typedef int (*libvlc_media_player_is_playing_t)(void*);
typedef int (*libvlc_audio_get_track_count_t)(void*);
typedef int (*libvlc_audio_get_track_t)(void*);
typedef int (*libvlc_audio_set_track_t)(void*, int);
typedef void (*libvlc_log_cb)(void*, int, const void*, const char*, va_list);
typedef void (*libvlc_log_set_t)(void*, libvlc_log_cb, void*);

typedef struct libvlc_track_description_t
{
    int id;
    char* name;
    struct libvlc_track_description_t* next;
} libvlc_track_description_t;

typedef libvlc_track_description_t* (*libvlc_audio_get_track_description_t)(void*);
typedef void (*libvlc_track_description_list_release_t)(void*);

typedef struct EnumWindowsProcParams
{
    int  osVersion;
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

    libvlc_new_t                            libvlc_new;
    libvlc_media_new_path_t                 libvlc_media_new_path;
    libvlc_media_player_new_from_media_t    libvlc_media_player_new_from_media;
    libvlc_media_player_set_hwnd_t          libvlc_media_player_set_hwnd;
    libvlc_media_player_play_t              libvlc_media_player_play;
    libvlc_media_player_stop_t              libvlc_media_player_stop;
    libvlc_media_release_t                  libvlc_media_release;
    libvlc_media_player_release_t           libvlc_media_player_release;
    libvlc_release_t                        libvlc_release;
    libvlc_audio_set_mute_t                 libvlc_audio_set_mute;
    libvlc_audio_set_volume_t               libvlc_audio_set_volume;
    libvlc_media_player_is_playing_t        libvlc_media_player_is_playing;
    libvlc_audio_get_track_count_t          libvlc_audio_get_track_count;
    libvlc_audio_get_track_t                libvlc_audio_get_track;
    libvlc_audio_set_track_t                libvlc_audio_set_track;
    libvlc_audio_get_track_description_t    libvlc_audio_get_track_description;
    libvlc_track_description_list_release_t libvlc_track_description_list_release;
    libvlc_log_set_t                        libvlc_log_set;

    int  volume;
    BOOL muted;
} VLC;

/* Windows message used to ask Progman to spawn the WorkerW window.
 * Undocumented but stable/widely relied on (0x052C == "spawn worker"). */
#define MSG_SPAWN_WORKERW 0x052C

 /* How long to wait (ms) for the player to report "playing" / for audio
  * tracks to be demuxed, expressed as a poll count at 50ms per tick. */
#define PLAYING_POLL_TICKS      40   /* 40 * 50ms = ~2s   */
#define AUDIO_TRACK_POLL_TICKS  60   /* 60 * 50ms = ~3s   */
#define POLL_INTERVAL_MS        50

static VLC vlc = { 0 };
static BOOL g_cleanedUp = FALSE;

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

 /* Returns 10 for Windows 10, 11 for Windows 11+, 0 if the build number
  * couldn't be determined (RtlGetVersion missing/failed). */
static int getWindowsVersion(void)
{
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (!hMod)
        return 0;

    RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
    if (!RtlGetVersion)
        return 0;

    RTL_OSVERSIONINFOW os = { 0 };
    os.dwOSVersionInfoSize = sizeof(os);
    if (RtlGetVersion(&os) != 0)
        return 0;

    if (os.dwMajorVersion != 10)
        return 0;

    return os.dwBuildNumber < 22000 ? 10 : 11;
}

/* Idempotent: safe to call more than once (e.g. an error path followed by
 * the console handler on exit). */
static void cleanup(void)
{
    if (g_cleanedUp)
        return;
    g_cleanedUp = TRUE;

    if (vlc.player)
        vlc.libvlc_media_player_release(vlc.player);
    if (vlc.media)
        vlc.libvlc_media_release(vlc.media);
    if (vlc.inst)
        vlc.libvlc_release(vlc.inst);
    if (vlc.libvlc)
        FreeLibrary(vlc.libvlc);
}

/* Handles console close / Ctrl+C / logoff so VLC and the DLL get released
 * instead of the process being torn down mid-playback. */
static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType)
{
    (void)ctrlType;
    cleanup();
    return TRUE;
}

static BOOL CALLBACK EnumWindowsProc(HWND tophandle, LPARAM lParam)
{
    EnumWindowsProcParams* params = (EnumWindowsProcParams*)lParam;

    if (FindWindowExA(tophandle, NULL, "SHELLDLL_DefView", NULL) != NULL)
    {
        /* tophandle is the WorkerW (or Progman, on older builds) hosting
         * desktop icons. The WorkerW we actually want is its next
         * sibling in z-order. */
        params->hWorkerW = FindWindowExA(NULL, tophandle, "WorkerW", NULL);
    }
    return TRUE; /* keep enumerating */
}

static HWND GetWorkerW(void)
{
    HWND progman = FindWindowA("Progman", NULL);
    if (!progman)
    {
        printf("Error: could not find Progman window\n");
        return NULL;
    }

    EnumWindowsProcParams params = { progman, NULL };
    SendMessageTimeoutA(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, NULL);
    EnumWindows(EnumWindowsProc, (LPARAM)&params);

    if (!params.hWorkerW)
    {
        /* Fallback: some builds never spawn a second WorkerW and just
         * keep SHELLDLL_DefView directly under Progman. In that case
         * Progman itself is the correct target to parent into. */
        if (FindWindowExA(progman, NULL, "SHELLDLL_DefView", NULL) != NULL)
            params.hWorkerW = progman;
    }

    if (params.hWorkerW)
        printf("WorkerW: 0x%p\n", (void*)params.hWorkerW);
    else
        printf("WorkerW not found\n");

    return params.hWorkerW;
}

static void libvlcLoad(void)
{
    SetDllDirectoryA("VLCmin");
    SetEnvironmentVariableA("VLC_PLUGIN_PATH", "VLCmin/plugins");

    vlc.libvlc = LoadLibraryA("VLCmin\\libvlc.dll");
    if (!vlc.libvlc)
    {
        DWORD error = GetLastError();
        printf("Error: failed to load libvlc.dll (code %lu)\n", error);
        exit((int)error);
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
    GET_FUNC(libvlc_media_player_stop);
    GET_FUNC(libvlc_media_release);
    GET_FUNC(libvlc_media_player_release);
    GET_FUNC(libvlc_release);
    GET_FUNC(libvlc_audio_set_mute);
    GET_FUNC(libvlc_audio_set_volume);
    GET_FUNC(libvlc_media_player_is_playing);
    GET_FUNC(libvlc_audio_get_track_count);
    GET_FUNC(libvlc_audio_get_track);
    GET_FUNC(libvlc_audio_set_track);
    GET_FUNC(libvlc_audio_get_track_description);
    GET_FUNC(libvlc_track_description_list_release);
    GET_FUNC(libvlc_log_set);

#undef GET_FUNC
}

static void vlcLogCallback(void* data, int level, const void* ctx, const char* fmt, va_list args)
{
    (void)data; (void)ctx;
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    printf("[VLC log %d] %s\n", level, buf);
}

static BOOL isActiveWindowFullScreen(void)
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || !IsWindowVisible(hwnd))
        return FALSE;

    char className[256];
    GetClassNameA(hwnd, className, sizeof(className));
    if (strcmp(className, "Progman") == 0 || strcmp(className, "WorkerW") == 0)
        return FALSE;

    HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!hMonitor)
        return FALSE;

    MONITORINFO monitorInfo = { sizeof(monitorInfo) };
    if (!GetMonitorInfo(hMonitor, &monitorInfo))
        return FALSE;

    RECT windowRect;
    if (!GetWindowRect(hwnd, &windowRect))
        return FALSE;

    return (windowRect.left <= monitorInfo.rcMonitor.left + 1 &&
        windowRect.top <= monitorInfo.rcMonitor.top + 1 &&
        windowRect.right >= monitorInfo.rcMonitor.right - 1 &&
        windowRect.bottom >= monitorInfo.rcMonitor.bottom - 1);
}

static void setPlayerState(BOOL* wasFullscreen)
{
    BOOL isFullscreen = isActiveWindowFullScreen();

    if (isFullscreen != *wasFullscreen)
    {
        if (isFullscreen)
        {
            vlc.libvlc_media_player_stop(vlc.player);
#ifdef DEBUG
            printf("Stop playing wallpaper\n");
#endif
        }
        else
        {
            vlc.libvlc_media_player_play(vlc.player);
#ifdef DEBUG
            printf("Playing wallpaper again\n");
#endif
        }
        *wasFullscreen = isFullscreen;
    }
}

static void printUsage(void)
{
    printf(
        "Error: arg \"videoPath\" not found\n"
        "wallpaperLite-CLI <path-to-video> [--volume=0-100] [--mute]\n"
    );
}

static void parseCommandLineArguments(int argc, char** argv)
{
    if (argc < 2 || !argv[1])
    {
        printUsage();
        exit(1);
    }

    strncpy_s(vlc.videoPath, sizeof(vlc.videoPath), argv[1], _TRUNCATE);

    vlc.volume = 100;
    vlc.muted = FALSE;

    for (int i = 2; i < argc; i++)
    {
        if (strncmp(argv[i], "--volume=", 9) == 0)
        {
            const char* valStr = argv[i] + 9;
            char* endPtr = NULL;
            long vol = strtol(valStr, &endPtr, 10);

            if (endPtr == valStr || *endPtr != '\0')
            {
                printf("Warning: \"%s\" is not a valid volume, ignoring\n", argv[i]);
                continue;
            }

            if (vol < 0)   vol = 0;
            if (vol > 100) vol = 100;
            vlc.volume = (int)vol;
        }
        else if (strcmp(argv[i], "--mute") == 0)
        {
            vlc.muted = TRUE;
        }
        else
        {
            printf("Warning: unknown argument \"%s\"\n", argv[i]);
        }
    }
}

int main(int argc, char* argv[])
{
    parseCommandLineArguments(argc, argv);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    HWND hWorkerW = GetWorkerW();
    if (!hWorkerW)
    {
        printf("Error: could not locate WorkerW; cannot attach video to desktop\n");
        return 1;
    }

    libvlcLoad();

    const char* VLCargs[] =
    {
        "-vv",
        //"--quiet",                  /* suppress non-error messages */
        "--avcodec-hw=dxva2",        /* hardware-accelerated decoding */
        "--input-repeat=65535",      /* effectively loop forever */
        "--drop-late-frames",        /* drop frames we can't keep up with */
        "--avcodec-fast"             /* enable optimized/less-strict decoding */
    };

    vlc.inst = vlc.libvlc_new(sizeof(VLCargs) / sizeof(VLCargs[0]), VLCargs);
    if (!vlc.inst)
    {
        printf("Failed to initialize VLC\n");
        cleanup();
        return 1;
    }

    vlc.libvlc_log_set(vlc.inst, vlcLogCallback, NULL);

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

    vlc.libvlc_media_player_set_hwnd(vlc.player, hWorkerW);
    printf("Video attached to window 0x%p\n", (void*)hWorkerW);

    vlc.libvlc_media_player_play(vlc.player);

    /* Wait until playback actually starts. */
    for (int i = 0; i < PLAYING_POLL_TICKS; i++)
    {
        if (vlc.libvlc_media_player_is_playing(vlc.player))
            break;
        Sleep(POLL_INTERVAL_MS);
    }

    /* Audio tracks are demuxed asynchronously; poll separately for them. */
    int trackCount = 0;
    for (int i = 0; i < AUDIO_TRACK_POLL_TICKS; i++)
    {
        trackCount = vlc.libvlc_audio_get_track_count(vlc.player);
        if (trackCount > 0)
            break;
        Sleep(POLL_INTERVAL_MS);
    }
    printf("Audio tracks found: %d (after polling)\n", trackCount);

    int currentTrack = vlc.libvlc_audio_get_track(vlc.player);
    printf("Current audio track: %d\n", currentTrack);

    libvlc_track_description_t* trackDesc = vlc.libvlc_audio_get_track_description(vlc.player);
    int firstRealId = -1;

    for (libvlc_track_description_t* iter = trackDesc; iter; iter = iter->next)
    {
        printf("Track id=%d, name=%s\n", iter->id, iter->name ? iter->name : "(null)");
        if (iter->id != -1 && firstRealId == -1)
            firstRealId = iter->id;
    }

    if (trackDesc)
        vlc.libvlc_track_description_list_release(trackDesc);

    if (currentTrack == -1 && firstRealId != -1)
    {
        int setRes = vlc.libvlc_audio_set_track(vlc.player, firstRealId);
        printf("Explicitly selected track id=%d, result: %d\n", firstRealId, setRes);
    }

    /* libvlc's volume scale is perceptually non-linear; apply a cube-root
     * compensation curve so --volume=50 sounds like "half", not "quiet". */
    double normalized = vlc.volume / 100.0;
    double compensated = cbrt(normalized) * 100.0;
    int    vlcVolume = (int)(compensated + 0.5);

    Sleep(100);
    int volRes = vlc.libvlc_audio_set_volume(vlc.player, vlcVolume);
    printf("set_volume(%d) after track select: %d\n", vlcVolume, volRes);

    if (vlc.muted)
        vlc.libvlc_audio_set_mute(vlc.player, 1);

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

        MsgWaitForMultipleObjects(0, NULL, FALSE, 500, QS_ALLINPUT);
    }

    return 0;
}