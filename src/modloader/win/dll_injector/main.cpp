#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

#include <Windows.h>

#include <boost/dll/shared_library.hpp>
#include <boost/filesystem/path.hpp>

#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <cpptrace/cpptrace.hpp>
#include <cpptrace/from_current.hpp>

#include "export_forwarders.h"

#include "Darkest1Modloader.hpp"

namespace {

std::shared_ptr<spdlog::logger> g_logger;
std::unique_ptr<boost::dll::shared_library> g_core;

HMODULE g_opengl32 = 0;
HMODULE g_original_opengl32 = nullptr;
std::mutex g_load_mutex{};
extern bool g_success_made_ldr_notification;

void failed() 
{
    MessageBox(0, TEXT("Darkest1Loader: Unable to load the original opengl32.dll. Please report this to the developer."), TEXT("Darkest1Loader"), 0);
    ExitProcess(0);
}

void initialize_logger() {
    std::filesystem::create_directories("logs");

    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "logs/modloader.log",
        5 * 1024 * 1024,
        3);

    auto msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();

    std::vector<spdlog::sink_ptr> sinks = {
        file_sink,
        msvc_sink
    };

    g_logger = std::make_shared<spdlog::logger>(
        "ModLoader",
        sinks.begin(),
        sinks.end());

    g_logger->flush();
    g_logger->set_level(spdlog::level::trace);
    g_logger->flush_on(spdlog::level::info);

    spdlog::set_default_logger(g_logger);

    spdlog::info("ModLoader logger initialized");
}

void initialize_core(HMODULE modul) 
{
    CPPTRACE_TRY 
    {
        g_loader = std::make_unique<Darkest1Modloader>(modul);
    } 
    CPPTRACE_CATCH (const std::exception& exception) 
    {
        spdlog::error(
            "Failed to load ModLoaderCore: {}\n{}",
            exception.what(), cpptrace::from_current_exception().to_string());

        g_core.reset();
    }
}

void shutdown_core() {

    g_core.reset();

    spdlog::info("ModLoaderCore unloaded");
}

bool load_opengl32() 
{
    std::scoped_lock _{g_load_mutex};

    if (g_opengl32) {
        return true;
    }

    wchar_t buffer[MAX_PATH]{0};
    if (GetSystemDirectoryW(buffer, MAX_PATH) != 0) {
        // Load the original dinput8.dll
        if ((g_opengl32 = LoadLibraryW((std::wstring{buffer} + L"\\OPENGL32.dll").c_str())) == NULL) {
            failed();
            return false;
        }

        // Cache the original proc address immediately before any overlay (e.g. EOS) can hook it
        //g_original_opengl32 = GetProcAddress(g_opengl32, "DirectInput8Create");

        return true;
    }

    failed();
    return false;
}

DWORD WINAPI initialization_thread(HMODULE module) 
{
    initialize_logger();
    initialize_core(module);

    return 0;
}

}  // namespace

// extern "C" {

// FARPROC get_original_export(const char* name) {
//     if (g_original_opengl32 == nullptr) {
//         return nullptr;
//     }

//     return GetProcAddress(g_original_opengl32, name);
// }

// }  // extern "C"

BOOL APIENTRY DllMain(HANDLE handle, DWORD reason, LPVOID reserved) 
{
    HMODULE module = (HMODULE)handle;
    if (reason == DLL_PROCESS_ATTACH) 
    {
        DisableThreadLibraryCalls(module);

#ifdef _DEBUG
        AllocConsole();

        FILE* stream = nullptr;
        freopen_s(&stream, "CONIN$", "r", stdin);
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
#endif

        HANDLE thread = CreateThread(
            nullptr,
            0,
            (LPTHREAD_START_ROUTINE)initialization_thread,
            handle,
            0,
            nullptr);

        if (thread != nullptr) 
        {
            CloseHandle(thread);
        }
    } 
    else if (reason == DLL_PROCESS_DETACH) 
    {
        if (reserved == nullptr) 
        {
            shutdown_core();
        }
    }

    return TRUE;
}