#include <stdio.h>
#include <windows.h>
#include <direct.h> // for _mkdir
#include <errno.h>  // for errno
#include <shlwapi.h> // для PathCombine
#pragma comment(lib, "shlwapi.lib") // линковка с shlwapi.lib

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
    char videoPath[260]; // изменено с const char на char
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
    if (vlc.player) vlc.player_release(vlc.player);
    if (vlc.media) vlc.media_release(vlc.media);
    if (vlc.inst) vlc.libvlc_release(vlc.inst);
    if (vlc.libvlc) FreeLibrary(vlc.libvlc);
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

    // Создание папки для видео
    const char* folderName = "VideoHere";
    if (_mkdir(folderName) == 0 || errno == EEXIST)
    {
        // Сканирование файлов в папке
        char searchPath[MAX_PATH];
        sprintf_s(searchPath, MAX_PATH, "%s\\*", folderName);

        WIN32_FIND_DATAA findData;
        HANDLE hFind = FindFirstFileA(searchPath, &findData);

        char* fileNames[100] = { 0 };
        int fileCount = 0;

        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    fileNames[fileCount] = _strdup(findData.cFileName);
                    if (!fileNames[fileCount])
                    {
                        printf("Memory allocation error\n");
                        break;
                    }
                    fileCount++;
                    if (fileCount >= 100) break;
                }
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);
        }

        // Вывод списка файлов
        if (fileCount > 0)
        {
            printf("\nAvailable videos in '%s':\n", folderName);
            for (int i = 0; i < fileCount; i++)
            {
                printf("%d: %s\n", i + 1, fileNames[i]);
            }

            // Выбор файла пользователем
            int choice = 0;
            do
            {
                printf("\nSelect a video (1-%d) or 0 to enter path manually: ", fileCount);
                if (scanf_s("%d", &choice) != 1)
                {
                    while (getchar() != '\n'); // Очистка буфера ввода
                    continue;
                }

                if (choice == 0) break;

            } while (choice < 1 || choice > fileCount);

            if (choice > 0 && choice <= fileCount)
            {
                // Формирование полного пути
                char fullPath[MAX_PATH];
                PathCombineA(fullPath, folderName, fileNames[choice - 1]);
                strncpy_s(vlc.videoPath, sizeof(vlc.videoPath), fullPath, _TRUNCATE);
            }
            else
            {
                printf("Enter full video path: ");
                scanf_s("%s", vlc.videoPath, (unsigned int)sizeof(vlc.videoPath));
            }
        }
        else
        {
            printf("No files found in '%s'. Enter full video path: ", folderName);
            scanf_s("%s", vlc.videoPath, (unsigned int)sizeof(vlc.videoPath));
        }

        // Освобождение памяти
        for (int i = 0; i < fileCount; i++)
        {
            free(fileNames[i]);
        }
    }
    else
    {
        printf("Failed to create directory '%s'. Enter full video path: ", folderName);
        scanf_s("%s", vlc.videoPath, (unsigned int)sizeof(vlc.videoPath));
    }

    // Инициализация VLC
    libvlcLoad();

    const char* VLCargs[] = { "--no-audio", "--quiet", "--avcodec-hw=dxva2", "--input-repeat=65535" };
    vlc.inst = vlc.libvlc_new(sizeof(VLCargs) / sizeof(VLCargs[0]), VLCargs);
    if (!vlc.inst)
    {
        printf("Failed to initialize VLC\n");
        cleanup();
        return 1;
    }

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
    printf("Playing: %s\n", vlc.videoPath);

    vlc.player_play(vlc.player);

    // Оконный цикл
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "VLCWindowClass";
    RegisterClass(&wc);

    HWND hWnd = CreateWindowEx(0, wc.lpszClassName, "VLC Window", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300, NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hWnd, SW_HIDE); // Скрытое окно для обработки сообщений

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}