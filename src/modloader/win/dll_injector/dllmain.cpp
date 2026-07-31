#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>

// ========== Вспомогательная часть для работы с модами ==========
namespace fs = std::filesystem;

// Оригинальный SDL2, загруженный нами вручную
HMODULE g_OriginalSDL2 = nullptr;

// Функция, которую должен экспортировать каждый мод
typedef void (*ModInitFunc)();

void LoadMods()
{
    // Создаём консоль для вывода отладочной информации (можно убрать в релизе)
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    std::string modsDir = "mods";
    if (!fs::exists(modsDir) || !fs::is_directory(modsDir))
    {
        printf("[ModLoader] Folder 'mods' not found, mods not loaded.\n");
        return;
    }

    for (const auto& entry : fs::directory_iterator(modsDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".dll")
        {
            std::string dllPath = entry.path().string();
            HMODULE mod = LoadLibraryA(dllPath.c_str());
            if (mod)
            {
                ModInitFunc init = (ModInitFunc)GetProcAddress(mod, "ModInit");
                if (init)
                {
                    printf("[ModLoader] Loaded mod: %s\n", entry.path().filename().string().c_str());
                    init();
                }
                else
                {
                    printf("[ModLoader] Warn: %s not export ModInit()\n",
                        entry.path().filename().string().c_str());
                }
            }
            else
            {
                printf("[ModLoader] Failed to load %s\n", entry.path().filename().string().c_str());
            }
        }
    }
}

// ========== Точка входа DLL ==========
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        // Отключаем уведомления о подключении потоков (оптимизация)
        DisableThreadLibraryCalls(hModule);

        // Загружаем настоящую SDL2, переименованную в SDL2_orig.dll
        g_OriginalSDL2 = LoadLibraryA("SDL2_orig.dll");
        if (!g_OriginalSDL2)
        {
            MessageBoxA(NULL, "Failed to load SDL2_orig.dll! "
                "Verified to SDL2.dll was renamed in SDL2_orig.dll and located in folder with exe.",
                "ModLoader", MB_ICONERROR);
            return FALSE;
        }

        // Загружаем все моды
        LoadMods();
        break;
    }
    case DLL_PROCESS_DETACH:
        if (g_OriginalSDL2)
            FreeLibrary(g_OriginalSDL2);
        break;
    }
    return TRUE;
}