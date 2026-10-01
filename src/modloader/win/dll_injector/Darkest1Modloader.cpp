#include "Darkest1Modloader.hpp"

namespace fs = std::filesystem;
using namespace std::literals;

typedef struct _LDR_DLL_UNLOADED_NOTIFICATION_DATA {
    ULONG Flags;                    //Reserved.
    PCUNICODE_STRING FullDllName;   //The full path name of the DLL module.
    PCUNICODE_STRING BaseDllName;   //The base file name of the DLL module.
    PVOID DllBase;                  //A pointer to the base address for the DLL in memory.
    ULONG SizeOfImage;              //The size of the DLL image, in bytes.
} LDR_DLL_UNLOADED_NOTIFICATION_DATA, *PLDR_DLL_UNLOADED_NOTIFICATION_DATA;

typedef struct _LDR_DLL_LOADED_NOTIFICATION_DATA {
    ULONG Flags;                    //Reserved.
    PCUNICODE_STRING FullDllName;   //The full path name of the DLL module.
    PCUNICODE_STRING BaseDllName;   //The base file name of the DLL module.
    PVOID DllBase;                  //A pointer to the base address for the DLL in memory.
    ULONG SizeOfImage;              //The size of the DLL image, in bytes.
} LDR_DLL_LOADED_NOTIFICATION_DATA, *PLDR_DLL_LOADED_NOTIFICATION_DATA;

typedef union _LDR_DLL_NOTIFICATION_DATA {
    LDR_DLL_LOADED_NOTIFICATION_DATA Loaded;
    LDR_DLL_UNLOADED_NOTIFICATION_DATA Unloaded;
} LDR_DLL_NOTIFICATION_DATA, *PLDR_DLL_NOTIFICATION_DATA;

using PLDR_DLL_NOTIFICATION_FUNCTION = void (*)(
  ULONG                       NotificationReason,
  PLDR_DLL_NOTIFICATION_DATA NotificationData,
  PVOID                       Context
);

using LdrRegisterDllNotification_t = NTSTATUS (*) (
    ULONG                          Flags,
    PLDR_DLL_NOTIFICATION_FUNCTION NotificationFunction,
    PVOID                          Context,
    PVOID                          *Cookie
);

#define LDR_DLL_NOTIFICATION_REASON_LOADED 1
#define LDR_DLL_NOTIFICATION_REASON_UNLOADED 2

std::optional<std::filesystem::path> g_current_game_path{};
bool g_success_made_ldr_notification{false};

void CALLBACK ldr_notification_callback(
    ULONG                       NotificationReason,
    PLDR_DLL_NOTIFICATION_DATA NotificationData,
    PVOID                       Context
) 
try {
    // From what I can tell, the PEB entries get filled in by the time this is called
    // so we're good.
    if (NotificationReason == LDR_DLL_NOTIFICATION_REASON_LOADED) {
        if (NotificationData->Loaded.BaseDllName != nullptr && NotificationData->Loaded.BaseDllName->Buffer != nullptr) {
            std::wstring base_dll_name = NotificationData->Loaded.BaseDllName->Buffer;
            std::wstring lower_base_dll_name = base_dll_name;
            std::transform(lower_base_dll_name.begin(), lower_base_dll_name.end(), lower_base_dll_name.begin(), ::towlower);
            spdlog::info("LdrRegisterDllNotification: Loaded: {}", utility::narrow(base_dll_name));

            if (lower_base_dll_name.find(L"sl.dlss_g.dll") != std::wstring::npos) {
                spdlog::info("LdrRegisterDllNotification: Detected DLSS DLL loaded");

                D3D12Hook::hook_streamline((HMODULE)NotificationData->Loaded.DllBase);
            }
        }

        if (g_current_game_path && NotificationData->Loaded.FullDllName != nullptr && NotificationData->Loaded.FullDllName->Buffer != nullptr) {
            std::wstring full_dll_name = NotificationData->Loaded.FullDllName->Buffer;
            std::filesystem::path full_dll_path = full_dll_name;

            if (full_dll_path.parent_path() == *g_current_game_path) {
                spdlog::info("LdrRegisterDllNotification: DLL loaded from game directory: {}", utility::narrow(full_dll_name));

                if (sdk::GameIdentity::get().is_dd2() || sdk::GameIdentity::get().is_mhrise() || sdk::GameIdentity::get().tdb_ver() >= 74) {
                    utility::spoof_module_paths_in_exe_dir();
                }
            }
        } else {
            spdlog::info("LdrRegisterDllNotification: DLL loaded from unknown location");
        }
    }
} catch (const std::exception& e) {
    spdlog::error("ldr_notification_callback: Exception occurred: {}", e.what());
} catch(...) {
    spdlog::error("ldr_notification_callback: Unknown exception occurred");
}

// bool is_device_controller(PDEV_BROADCAST_HDR hdr, WPARAM w_param) 
// {
//     if (hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) 
//     {
//         const auto d_interface = (PDEV_BROADCAST_DEVICEINTERFACE)hdr;

//         if (d_interface->dbcc_classguid == XUSB_INTERFACE_CLASS_GUID) 
//         {
//             spdlog::info("Event {:x}: Relevant device detected", w_param);
//             return true;
//         }
//     }

//     spdlog::info("Event {:x}: No relevant device detected", w_param);

//     return false;
// }

Darkest1Modloader::Darkest1Modloader(HMODULE module) 
    : m_game_module{GetModuleHandle(0)}
{
    s_reframework_module = module;

    std::scoped_lock __{m_startup_mutex};
    const auto& gi = sdk::GameIdentity::get();

    if (s_fallback_appdata) 
    {
        spdlog::warn("Failed to write to current directory, falling back to appdata folder");
    }

    spdlog::info("REFramework entry");

    // spdlog::info("Commit hash: {}", REF_COMMIT_HASH);
    // spdlog::info("Tag: {}", REF_TAG);
    // spdlog::info("Commits past tag: {}", REF_COMMITS_PAST_TAG);
    // spdlog::info("Branch: {}", REF_BRANCH);
    // spdlog::info("Total commits: {}", REF_TOTAL_COMMITS);
    // spdlog::info("Build date: {}", REF_BUILD_DATE);
    // spdlog::info("Build time: {}", REF_BUILD_TIME);
    // spdlog::info("Game name: {}", REFramework::get_game_name());

    const auto module_size = *utility::get_module_size(m_game_module);

    spdlog::info("Game Module Addr: {:x}", (uintptr_t)m_game_module);
    spdlog::info("Game Module Size: {:x}", module_size);

    if (auto current_game_path = utility::get_module_pathw(m_game_module); current_game_path.has_value()) 
    {
        g_current_game_path = *current_game_path;
        g_current_game_path = g_current_game_path->parent_path();
        spdlog::info("Current game path: {}", utility::narrow(g_current_game_path->c_str()));
    }

    // preallocate some memory for minhook to mitigate failures (temporarily at least... this should in theory fail when too many hooks are made)
    // but, 64 slots should be enough for now. 
    // so... TODO: modify minhook to use absolute jumps when failing to allocate memory nearby
    const auto halfway_module = (uintptr_t)m_game_module + (module_size / 2);
    const auto pre_allocated_buffer = (uintptr_t)AllocateBuffer((LPVOID)halfway_module); // minhook function
    spdlog::info("Preallocated buffer: {:x}", pre_allocated_buffer);

    IntegrityCheckBypass::fix_virtual_protect();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

#ifdef _DEBUG
    spdlog::set_level(spdlog::level::debug);
#endif

    // Create the typedef for RtlGetVersion
    typedef LONG (*RtlGetVersionFunc)(PRTL_OSVERSIONINFOW);

    const auto ntdll = GetModuleHandle("ntdll.dll");

    if (ntdll != nullptr) 
    {
        // Manually get RtlGetVersion
        auto rtl_get_version = (RtlGetVersionFunc)GetProcAddress(ntdll, "RtlGetVersion");

        if (rtl_get_version != nullptr) 
        {
            spdlog::info("Getting OS version information...");

            // Create an initial log that prints out the user's Windows OS version information
            // With the major and minor version numbers
            // Using RtlGetVersion()
            OSVERSIONINFOW os_version_info{};
            ZeroMemory(&os_version_info, sizeof(OSVERSIONINFOW));
            os_version_info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
            os_version_info.dwMajorVersion = 0;
            os_version_info.dwMinorVersion = 0;
            os_version_info.dwBuildNumber = 0;
            os_version_info.dwPlatformId = 0;

            if (rtl_get_version(&os_version_info) != 0) 
            {
                spdlog::info("RtlGetVersion() failed");
            } 
            else 
            {
                // Log the Windows version information
                spdlog::info("OS Version Information");
                spdlog::info("\tMajor Version: {}", os_version_info.dwMajorVersion);
                spdlog::info("\tMinor Version: {}", os_version_info.dwMinorVersion);
                spdlog::info("\tBuild Number: {}", os_version_info.dwBuildNumber);
                spdlog::info("\tPlatform Id: {}", os_version_info.dwPlatformId);

                spdlog::info("Disclaimer: REFramework does not send this information to the developers or any other third party.");
                spdlog::info("This information is only used to help with the development of REFramework.");
            }
        } 
        else 
        {
            spdlog::info("RtlGetVersion() not found");
        }

        // Do this at least once before setting up our callback.
        if (gi.is_dd2() || gi.is_mhrise() || gi.tdb_ver() >= 74) 
        {
            // Pre-emptively copy all DLL files in the current game directory into our _storage_ directory.
            if (g_current_game_path.has_value()) 
            {
                const auto dest_path = *g_current_game_path / "_storage_";
                fs::create_directories(dest_path);

                if (std::filesystem::exists(dest_path)) 
                try 
                {
                    std::error_code directory_ec{};
                    // Locate all DLL files in the current game directory
                    for (const auto& entry : fs::directory_iterator(*g_current_game_path, directory_ec)) 
                    try
                    {
                        const auto entry_path = entry.path();
                        
                        if (entry.is_regular_file() && entry_path.extension() == ".dll") 
                        {
                            spdlog::info("Copying DLL file: {}", entry_path.filename().string());
                            spdlog::info(" Full path: {}", entry_path.string());
                            const auto final_dest = dest_path / entry_path.filename().string();
                            spdlog::info(" Destination: {}", final_dest.string());
                            std::error_code ec{};
                            fs::copy_file(entry_path, final_dest, fs::copy_options::overwrite_existing, ec);

                            // check if error occurred
                            if (ec) 
                            {
                                spdlog::error("Failed to copy DLL file: {}", ec.message());
                            }

                            ec.clear();
                        }
                    } 
                    catch (const std::filesystem::filesystem_error& e) 
                    {
                        spdlog::error("Failed to copy DLL file: {}", e.what());
                    } 
                    catch (const std::exception& e) 
                    {
                        spdlog::error("Failed to copy DLL file: {}", e.what());
                    } 
                    catch(...) 
                    {
                        spdlog::error("Failed to copy DLL file: unknown exception occurred");
                    }

                    if (directory_ec) 
                    {
                        spdlog::error("An error occurred while traversing the game directory: {}", directory_ec.message());
                    }

                    // Copy the D3D12/D3D12Core.dll file from the current game directory into our _storage_ directory with the same subdirectory structure.
                    const auto d3d12_path = *g_current_game_path / "D3D12" / "D3D12Core.dll";

                    if (std::filesystem::exists(d3d12_path)) 
                    try 
                    {
                        spdlog::info("Copying D3D12Core.dll file");
                        fs::create_directories(dest_path / "D3D12");

                        if (std::filesystem::exists(d3d12_path)) 
                        {
                            spdlog::info("Copying D3D12Core.dll file");
                            std::error_code ec{};
                            fs::copy_file(d3d12_path, dest_path / "D3D12" / "D3D12Core.dll", fs::copy_options::overwrite_existing, ec);

                            if (ec) 
                            {
                                spdlog::error("Failed to copy D3D12Core.dll file: {}", ec.message());
                            }

                            ec.clear();
                        }
                    } 
                    catch (const std::filesystem::filesystem_error& e) 
                    {
                        spdlog::error("Failed to copy D3D12Core.dll file: {}", e.what());
                    } 
                    catch (const std::exception& e) 
                    {
                        spdlog::error("Failed to copy D3D12Core.dll file: {}", e.what());
                    } 
                    catch(...) 
                    {
                        spdlog::error("Failed to copy D3D12Core.dll file: unknown exception occurred");
                    }
                } 
                catch (const std::filesystem::filesystem_error& e) 
                {
                    spdlog::error("An error occurred while copying DLL files: {}", e.what());
                } 
                catch (const std::exception& e) 
                {
                    spdlog::error("An error occurred while copying DLL files: {}", e.what());
                } 
                catch(...) 
                {
                    spdlog::error("An error occurred while copying DLL files: unknown exception occurred");
                } 
                else 
                {
                    spdlog::error("Failed to create storage directory");
                }
            }

            utility::spoof_module_paths_in_exe_dir();
        }

        // Register our LdrRegisterDllNotification callback
        spdlog::info("Registering LdrRegisterDllNotification callback...");
        const auto ldr_register_dll_notification = (LdrRegisterDllNotification_t)GetProcAddress(ntdll, "LdrRegisterDllNotification");

        if (ldr_register_dll_notification != nullptr) 
        {
            PVOID cookie = nullptr;
            g_success_made_ldr_notification = NT_SUCCESS(ldr_register_dll_notification(0, ldr_notification_callback, nullptr, &cookie));

            if (g_success_made_ldr_notification) 
            {
                spdlog::info("LdrRegisterDllNotification callback registered successfully");
            } 
            else 
            {
                spdlog::info("LdrRegisterDllNotification callback failed to register");
            }
        } 
        else 
        {
            spdlog::info("LdrRegisterDllNotification not found");
        }
    } 
    else 
    {
        spdlog::info("ntdll.dll not found");
    }

    // wait for the game to load (WTF MHRISE??)
    // once this is done, we can assume the process is unpacked.
    if (gi.is_reengine_packed()) 
    {
        auto now = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point next_log = now;

        while (GetModuleHandleA("d3d12.dll") == nullptr) 
        {
            now = std::chrono::steady_clock::now();
            if (now >= next_log) 
            {
                spdlog::info("[REFramework] Waiting for D3D12...");
                next_log = now + 1s;
            }
            Sleep(50);
        }

        while (LoadLibraryA("d3d12.dll") == nullptr) 
        {
            if (now >= next_log) 
            {
                spdlog::info("[REFramework] Waiting for D3D12...");
                next_log = now + 1s;
            }
        }

        spdlog::info("D3D12 loaded");
    }

    if (gi.is_mhrise() || gi.is_dd2() || gi.tdb_ver() >= 74) 
    {
        utility::load_module_from_current_directory(L"openvr_api.dll");
        utility::load_module_from_current_directory(L"openxr_loader.dll");
        LoadLibraryA("dxgi.dll");
        LoadLibraryA("d3d11.dll");

        if (!g_success_made_ldr_notification) 
        {
            utility::spoof_module_paths_in_exe_dir();
        }
    }

    //LooseTextureLoader::get().early_initialize();

    if (gi.tdb_ver() >= 81) 
    {
        //FaultyFileDetector::early_init();
    }

    if (gi.is_re8()) 
    {
        auto startup_lookup_thread = std::make_unique<std::thread>([this]() 
        {
            // Fixes a crash on some machines when starting the game
            // This one has nothing to do with integrity checks
            // it has something to do with the Agility SDK and pipeline state.
            uint32_t times_searched = 0;

            auto startup_patch_addr = utility::scan(m_game_module, "40 53 57 48 83 ec 28 48 83 b9 ? ? ? ? 00");

            while (!startup_patch_addr) 
            {
                startup_patch_addr = utility::scan(m_game_module, "40 53 57 48 83 ec 28 48 83 b9 ? ? ? ? 00");

                if (times_searched++ > 10) 
                {
                    spdlog::error("Failed to find startup patch address");
                    return;
                }
            }

            if (startup_patch_addr) 
            {
                spdlog::info("Found startup patch at {:x}", *startup_patch_addr);
                static auto permanent_patch = Patch::create(*startup_patch_addr, {0xC3});
            } 
            else 
            {
                spdlog::info("Couldn't find RE8 crash fix patch location!");
            }
        });
        startup_lookup_thread->detach();
    }


    if (gi.is_reengine_at()) 
    {
        utility::ThreadSuspender suspender{};
        IntegrityCheckBypass::ignore_application_entries();

        if (gi.is_re8() || gi.is_re4() || gi.is_sf6()) 
        {
            // Also done on RE4 because some of the scans are the same.
            IntegrityCheckBypass::immediate_patch_re8();
        }

        if (gi.is_re4() || gi.is_sf6()) 
        {
            // Fixes new code added in RE4 only.
            IntegrityCheckBypass::immediate_patch_re4();
        }

        if (gi.is_dd2() || gi.tdb_ver() >= 74) 
        {
            // Fixes new code added in DD2 only. Maybe >= TDB73 too. Probably will change.
            IntegrityCheckBypass::immediate_patch_dd2();
        }

        if (gi.tdb_ver() >= 82) 
        {
            // Fixes new code added in RE9 only. Maybe >= TDB83 too. Probably will change.
            // Addendum: Found to be present in MHSTORIES3 (TDB 82) as well, so this is not RE9 exclusive.
            IntegrityCheckBypass::immediate_patch_re9();
        }

        // Seen in SF6
        IntegrityCheckBypass::remove_stack_destroyer();
        suspender.resume();
    }

    // Load the plugins early right after executable unpacking
    PluginLoader::get()->early_init();

    // Wait for TDB and render device to be initialized before allowing D3D hooking
    const auto start_time = std::chrono::high_resolution_clock::now();

    while (true) 
    {
        try 
        {
            if (sdk::VM::get() != nullptr) 
            {
                break;
            }
        } 
        catch(...) 
        {
        }

        if (std::chrono::high_resolution_clock::now() - start_time > std::chrono::seconds(30)) 
        {
            spdlog::error("Timed out waiting for VM to initialize.");
            throw std::runtime_error("Timed out waiting for VM to initialize.");
        }

        //std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::this_thread::yield();
    }

    spdlog::info("VM initialized, waiting for renderer to initialize...");
    sdk::RETypeDefinition* renderer_t = nullptr;
    sdk::renderer::Renderer* renderer = nullptr;
    bool found_renderer = false;
    bool renderer_has_render_frame_fn = false;

    if (sdk::RETypeDB::get() != nullptr) 
    {
        auto& loader = LooseFileLoader::get(); // Initialize this really early
        auto &integrity_bypass = IntegrityCheckBypass::get_shared_instance();

        const bool has_faulty_file_detector = gi.tdb_ver() >= 81;
        if (has_faulty_file_detector) 
        {
            FaultyFileDetector::get(); // Initialize early
        }

        const auto config_path = get_persistent_dir(REFrameworkConfig::REFRAMEWORK_CONFIG_NAME.data()).string();
        if (fs::exists(utility::widen(config_path))) 
        {
            utility::Config cfg{ config_path };
            loader->on_config_load(cfg);

            if (has_faulty_file_detector) 
            {
                FaultyFileDetector::get()->on_config_load(cfg);
            }
            integrity_bypass->on_config_load(cfg);
        }

        if (loader->is_enabled()) 
        {
            loader->hook();
        }
    }

    while (true) 
    try 
    {
        const auto tdb = sdk::RETypeDB::get();

        if (tdb == nullptr) 
        {
            spdlog::error("TypeDB not found");
            break;
        }

        // We have to manually look through the types because get_FullName
        // will crash if we call it this early, which is used in get_type(name)
        if (renderer_t == nullptr) 
        {
            for (auto i = 0; i < tdb->get_num_types(); ++i) 
            {
                const auto t = tdb->get_type(i);

                if (t == nullptr || t->get_name() == nullptr || t->get_namespace() == nullptr) 
                {
                    continue;
                }

                if (std::string_view{t->get_name()} == "Renderer" && std::string_view{t->get_namespace()} == "via.render") 
                {
                    spdlog::info("Renderer type found manually @ {:x}", (uintptr_t)t);
                    renderer_t = t;
                    break;
                }
            }
        }

        if (renderer_t == nullptr) 
        {
            spdlog::error("Renderer type not found");
            break;
        }

        renderer_has_render_frame_fn = renderer_t->get_method("get_RenderFrame") != nullptr;

        const auto renderer_has_instance = renderer_t->get_method("hasInstance");

        if (renderer_has_instance == nullptr) 
        {
            spdlog::error("Renderer::hasInstance not found");
            break;
        }

        if (renderer_has_instance->get_function() == nullptr) 
        {
            continue;
        }

        const auto has_instance = renderer_has_instance->call<bool>(nullptr, nullptr); // static

        if (!has_instance) 
        {
            std::this_thread::yield();
            continue;
        }

        renderer = sdk::renderer::get_renderer();

        if (renderer != nullptr) {
            found_renderer = true;
            break;
        }

        spdlog::info("waiting for renderer");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } 
    catch(...) 
    {
        spdlog::warn("Exception occurred while waiting for renderer");
        continue;
    }
    
    spdlog::info("Found renderer @ {:x} (type: {:x}), waiting for first frame...", (uintptr_t)renderer, (uintptr_t)renderer_t);

    bool valid_render_frame = false;

    if (renderer_has_render_frame_fn) 
    {
        spdlog::info("Renderer has get_RenderFrame function");
    } 
    else 
    {
        spdlog::info("Renderer does not have get_RenderFrame function");
    }

    while (renderer_has_render_frame_fn) 
    try 
    {
        // This function is static so its fine if renderer is null.
        const auto render_frame = renderer->get_render_frame();

        if (!render_frame.has_value()) 
        {
            spdlog::warn("Render frame property not found");
            break;
        }

        if (*render_frame > 0) 
        {
            spdlog::info("Render frame: {}", *render_frame);
            valid_render_frame = true;
            break;
        }

        std::this_thread::yield();
    } 
    catch(...) 
    {
        //spdlog::warn("Exception occurred while waiting for render frame");
        continue;
    }

    // If all is good, we can immediately hook D3D12 very early
    // else, defer to the hook monitor if anything in the chain failed
    if (valid_render_frame) 
    {
        // We can guaranteed hook at this point
        std::scoped_lock _{m_hook_monitor_mutex};
        hook_d3d12();
    }

    std::scoped_lock _{m_hook_monitor_mutex};

    m_last_present_time = std::chrono::steady_clock::now();
    m_last_message_time = std::chrono::steady_clock::now();
    m_d3d_monitor_thread = std::make_unique<std::jthread>([this](std::stop_token stop_token)
    {
        while (!stop_token.stop_requested() && !m_terminating) 
        {
            this->hook_monitor();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });
}

Darkest1Modloader::~Darkest1Modloader() 
{
    spdlog::info("REFramework shutting down...");

    if (m_is_d3d11) 
    {
        //deinit_d3d11();
    }

    ImGui_ImplWin32_Shutdown();

    if (m_initialized) {
        ImGui::DestroyContext();
    }
}

void Darkest1Modloader::hook_monitor() 
{
    if (m_do_not_hook_d3d_count.load() > 0) {
        // Wait until nothing important is happening
        m_last_present_time = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        m_last_chance_time = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        m_has_last_chance = true;
        return;
    }

    if (!m_hook_monitor_mutex.try_lock()) {
        // If this happens then we can assume execution is going as planned
        // so we can just reset the times so we dont break something
        m_last_present_time = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        m_last_chance_time = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        m_has_last_chance = true;
        return;
    }

    // Take ownership of the mutex with adopt_lock
    std::lock_guard _{ m_hook_monitor_mutex, std::adopt_lock };

    if (g_loader == nullptr) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();

    auto& d3d11 = get_d3d11_hook();
    auto& d3d12 = get_d3d12_hook();

    const auto renderer_type = get_renderer_type();

    if (d3d11 == nullptr || d3d12 == nullptr 
        || (renderer_type == REFramework::RendererType::D3D11 && d3d11 != nullptr && !d3d11->is_inside_present()) 
        || (renderer_type == REFramework::RendererType::D3D12 && d3d12 != nullptr && !d3d12->is_inside_present())) 
    {
        // check if present time is more than 5 seconds ago
        if (now - m_last_present_time > std::chrono::seconds(5)) {
            if (m_has_last_chance) {
                // the purpose of this is to make sure that the game is not frozen
                // e.g. if we are debugging the game, so we don't rehook anything on accident
                m_has_last_chance = false;
                m_last_chance_time = now;

                spdlog::info("Last chance encountered for hooking");
            }

            if (!m_has_last_chance && now - m_last_chance_time > std::chrono::seconds(1)) {
                spdlog::info("Sending rehook request for D3D");

                // hook_d3d12 always gets called first.
                if (m_is_d3d11) {
                    hook_d3d11();
                } else {
                    hook_d3d12();
                }

                // so we don't immediately go and hook it again
                // add some additional time to it to give it some leeway
                m_last_present_time = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                m_last_message_time = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                m_last_chance_time = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                m_has_last_chance = true;
            }
        } else {
            m_last_chance_time = std::chrono::steady_clock::now();
            m_has_last_chance = true;
        }

        if (m_initialized && m_wnd != 0 && now - m_last_message_time > std::chrono::seconds(5)) {
            if (m_windows_message_hook != nullptr && m_windows_message_hook->is_hook_intact()) {
                spdlog::info("Windows message hook is still intact, ignoring...");
                m_last_message_time = now;
                m_last_sendmessage_time = now;
                m_sent_message = false;
                return;
            }

            // send dummy message to window to check if our hook is still intact
            if (!m_sent_message) {
                spdlog::info("Sending initial message hook test");

                auto proc = (WNDPROC)GetWindowLongPtr(m_wnd, GWLP_WNDPROC);

                if (proc != nullptr) {
                    const auto ret = CallWindowProc(proc, m_wnd, WM_NULL, 0, 0);

                    spdlog::info("Hook test message sent");
                }

                m_last_sendmessage_time = std::chrono::steady_clock::now();
                m_sent_message = true;
            } else if (now - m_last_sendmessage_time > std::chrono::seconds(1)) {
                spdlog::info("Sending reinitialization request for message hook");

                // if we don't get a message for 5 seconds, assume the hook is broken
                //m_initialized = false; // causes the hook to be re-initialized next frame
                m_message_hook_requested = true;
                m_last_message_time = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                m_last_present_time = std::chrono::steady_clock::now() + std::chrono::seconds(5);

                m_sent_message = false;
            }
        } else {
            m_sent_message = false;
        }
    }
}

bool Darkest1Modloader::initialize() 
{
    if (m_initialized) {
        return true;
    }

    reframework::setup_exception_handler();

    if (m_first_initialize) {
        m_frames_since_init = 0;
        m_first_initialize = false;
    }

    if (m_frames_since_init < 60) {
        m_frames_since_init++;
        return false;
    }

    if (m_is_d3d11) {
        spdlog::info("Attempting to initialize DirectX 11");

        if (!m_d3d11_hook->is_hooked()) {
            return false;
        }

        auto device = m_d3d11_hook->get_device();
        auto swap_chain = m_d3d11_hook->get_swap_chain();

        // Wait.
        if (device == nullptr || swap_chain == nullptr) {
            m_first_initialize = true;

            spdlog::info("Device or SwapChain null. DirectX 12 may be in use. Unhooking D3D11...");

            // We unhook D3D11
            if (m_d3d11_hook->unhook()) {
                spdlog::info("D3D11 unhooked!");
            } else {
                spdlog::error("Cannot unhook D3D11, this might crash.");
            }

            m_is_d3d11 = false;
            m_valid = false;

            // We hook D3D12
            if (!hook_d3d12()) {
                spdlog::error("Failed to hook D3D12 after unhooking D3D11.");
            }
            return false;
        }

        ID3D11DeviceContext* context = nullptr;
        device->GetImmediateContext(&context);

        DXGI_SWAP_CHAIN_DESC swap_desc{};
        swap_chain->GetDesc(&swap_desc);

        m_wnd = swap_desc.OutputWindow;


        spdlog::info("Window Handle: {0:x}", (uintptr_t)m_wnd);
        spdlog::info("Initializing ImGui");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        m_loaded_saved_ui_display_size = false;
        m_saved_ui_display_size = {};
        ImNodes::SetImGuiContext(ImGui::GetCurrentContext());
        ImNodes::CreateContext();

        set_imgui_style();

        static const auto imgui_ini = (get_persistent_dir() / "ref_ui.ini").string();
        ImGui::GetIO().IniFilename = imgui_ini.c_str();
        reframework::ui::initialize_tree_state(get_persistent_dir() / "ref_dropdown_status.ini");

        spdlog::info("Initializing ImGui Win32");

        if (!ImGui_ImplWin32_Init(m_wnd)) {
            spdlog::error("Failed to initialize ImGui.");
            return false;
        }

        spdlog::info("Creating render target");

        if (!init_d3d11()) {
            spdlog::error("Failed to init D3D11");
            return false;
        }
    } else if (m_is_d3d12) {
        spdlog::info("Attempting to initialize DirectX 12");

        if (!m_d3d12_hook->is_hooked()) {
            return false;
        }

        auto device = m_d3d12_hook->get_device();
        auto swap_chain = m_d3d12_hook->get_swap_chain();

        if (device == nullptr || swap_chain == nullptr) {
            m_first_initialize = true;

            spdlog::info("Device: {:x}", (uintptr_t)device);
            spdlog::info("SwapChain: {:x}", (uintptr_t)swap_chain);

            spdlog::info("Device or SwapChain null. DirectX 11 may be in use. Unhooking D3D12...");

            // We unhook D3D12
            if (m_d3d12_hook->unhook())
                spdlog::info("D3D12 unhooked!");
            else
                spdlog::error("Cannot unhook D3D12, this might crash.");

            m_valid = false;
            m_is_d3d12 = false;

            // We hook D3D11
            if (!hook_d3d11()) {
                spdlog::error("Failed to hook D3D11 after unhooking D3D12.");
            }
            return false;
        }

        DXGI_SWAP_CHAIN_DESC swap_desc{};
        swap_chain->GetDesc(&swap_desc);

        m_wnd = swap_desc.OutputWindow;


        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        m_loaded_saved_ui_display_size = false;
        m_saved_ui_display_size = {};
        ImNodes::SetImGuiContext(ImGui::GetCurrentContext());
        ImNodes::CreateContext();

        set_imgui_style();

        static const auto imgui_ini = (get_persistent_dir() / "ref_ui.ini").string();
        ImGui::GetIO().IniFilename = imgui_ini.c_str();
        reframework::ui::initialize_tree_state(get_persistent_dir() / "ref_dropdown_status.ini");
        
        if (!ImGui_ImplWin32_Init(m_wnd)) {
            spdlog::error("Failed to initialize ImGui ImplWin32.");
            return false;
        }

        if (!init_d3d12()) {
            spdlog::error("Failed to init D3D12.");
            return false;
        }
    } else {
        return false;
    }

    initialize_windows_message_hook();

    if (m_first_frame) {
        m_dinput_hook = std::make_unique<DInputHook>(m_wnd);
    } else {
        m_dinput_hook->set_window(m_wnd);
    }

    if (m_first_frame) {
        m_first_frame = false;
        initialize_game_data();
    }

    return true;
}

bool Darkest1Modloader::initialize_windows_message_hook() 
{ 
    if (m_wnd == 0) 
    {
        return false;
    }

    if (m_first_frame || m_message_hook_requested || m_windows_message_hook == nullptr) 
    {
        m_last_message_time = std::chrono::steady_clock::now();
        m_windows_message_hook.reset();
        m_windows_message_hook = std::make_unique<WindowsMessageHook>(m_wnd);
        m_windows_message_hook->on_message = [this](auto wnd, auto msg, auto w_param, auto l_param) 
        {
            return on_message(wnd, msg, w_param, l_param);
        };

        m_message_hook_requested = false;
        return true;
    }

    m_message_hook_requested = false;
    return false;
}

bool Darkest1Modloader::first_frame_initialize() 
{
    const bool is_init_ok = m_error.empty() && m_game_data_initialized;

    if (!is_init_ok || !m_first_frame_d3d_initialize) 
    {
        return is_init_ok;
    }

    auto do_not_hook_d3d = acquire_do_not_hook_d3d();

    spdlog::info("Running first frame D3D initialization of mods...");

    m_first_frame_d3d_initialize = false;
    auto e = false;//m_mods->on_initialize_d3d_thread();

    if (e) 
    {
        // if (e->empty()) 
        // {
        //     m_error = "An unknown error has occurred.";
        // } 
        // else 
        // {
        //     m_error = *e;
        // }

        spdlog::error("Initialization of mods failed. Reason: {}", m_error);
        m_game_data_initialized = false;
        m_mods_fully_initialized = false;
        return false;
    } 
    else 
    {
        // Do an initial config save to set the default values for the frontend
        // save_config();
        m_mods_fully_initialized = true;
    }

    // Troubleshooting by logging loaded modules
    // helps us figure out if someone has conflicting software running
    try 
    {
        spdlog::info("Logging loaded modules...");

        //const auto loaded_modules = utility::get_loaded_module_names();

        // for (const auto& name : loaded_modules) 
        // {
        //     spdlog::info("Loaded module: {}", utility::narrow(name));
        // }
    } 
    catch(...) 
    {
        spdlog::error("Failed to get loaded modules.");
    }


    return true;
}

void Darkest1Modloader::run_imgui_frame(bool from_present) 
{
    std::scoped_lock _{ m_imgui_mtx };

    m_has_frame = false;

    if (!m_initialized) {
        return;
    }

    const bool is_init_ok = m_error.empty() && m_game_data_initialized;

    consume_input();
    init_fonts();
    
    ImGui_ImplWin32_NewFrame();

    // from_present is so we don't accidentally
    // run script/game code within the present thread.
    if (is_init_ok && !from_present) {
        // Run mod frame callbacks.
        //m_mods->on_pre_imgui_frame();
    }

    ImGui::NewFrame();

    if (!from_present) {
        //call_on_frame();
    }

    draw_ui();
    m_last_draw_ui = m_draw_ui;

    ensure_ui_layout_baseline();
    process_ui_layout_save(from_present);

    if (m_wants_save_imgui_config.exchange(false)) 
    {
        if (const auto* ini_filename = ImGui::GetIO().IniFilename; ini_filename != nullptr) 
        {
            ImGui::SaveIniSettingsToDisk(ini_filename);
            spdlog::info("Saved ImGui configuration");
        }
    }

    IMGUIZMO_NAMESPACE::BeginFrame();

    ImGui::EndFrame();
    ImGui::Render();

    m_has_frame = true;

    if (!from_present && m_wants_save_config) {
        //save_config();
        m_wants_save_config = false;
    }
}

void Darkest1Modloader::on_frame_d3d11() 
{
    std::scoped_lock _{ m_imgui_mtx };

    spdlog::debug("on_frame (D3D11)");

    m_renderer_type = RendererType::D3D11;

    if (!m_initialized) 
    {
        if (!initialize()) 
        {
            return;
        }

        spdlog::info("REFramework initialized");
        m_initialized = true;
        return;
    }

    if (m_message_hook_requested) 
    {
        initialize_windows_message_hook();
    }

    auto device = m_d3d11_hook->get_device();
    
    if (device == nullptr) 
    {
        spdlog::error("D3D11 device was null when it shouldn't be, returning...");
        m_initialized = false;
        return;
    }

    bool is_init_ok = m_error.empty() && m_game_data_initialized;

    if (is_init_ok) 
    {
        // Write default config once if it doesn't exist.
        if (!std::exchange(m_created_default_cfg, true)) 
        {
            // if (!fs::exists({utility::widen(get_persistent_dir(REFrameworkConfig::REFRAMEWORK_CONFIG_NAME.data()).string())})) 
            // {
            //     save_config();
            // }
        }
    }

    is_init_ok = first_frame_initialize();

    if (!m_has_frame) 
    {
        if (!is_init_ok) 
        {
            init_fonts();
            invalidate_device_objects();

            ImGui_ImplDX11_NewFrame();
            // hooks don't run until after initialization, so we just render the imgui window while initalizing.
            run_imgui_frame(true);
        } 
        else 
        {   
            return;
        }
    } 
    else 
    {
        invalidate_device_objects();
        ImGui_ImplDX11_NewFrame();
    }

    if (is_init_ok) 
    {
        //m_mods->on_present();
    }

    ComPtr<ID3D11DeviceContext> context{};
    float clear_color[]{0.0f, 0.0f, 0.0f, 0.0f};

    m_d3d11_hook->get_device()->GetImmediateContext(&context);
    context->ClearRenderTargetView(m_d3d11.blank_rt_rtv.Get(), clear_color);

    // Only render this if VR is running.
    // TODO: Instead use this as an SRV to render to the back buffer so we don't render twice.
    // if (VR::get()->is_hmd_active()) {
    //     context->ClearRenderTargetView(m_d3d11.rt_rtv.Get(), clear_color);
    //     context->OMSetRenderTargets(1, m_d3d11.rt_rtv.GetAddressOf(), NULL);
    //     ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());    
    // }

    // Set the back buffer to be the render target.
    context->OMSetRenderTargets(1, m_d3d11.bb_rtv.GetAddressOf(), nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    if (is_init_ok) 
    {
        //m_mods->on_post_frame();
    }
}

void Darkest1Modloader::on_post_present_d3d11() {}

void Darkest1Modloader::on_reset() 
{
    std::scoped_lock _{ m_imgui_mtx };

    spdlog::info("Reset!");

    if (m_initialized) {
        // fixes text boxes not being able to receive input
        //imgui::reset_keystates();
    }

    // Crashes if we don't release it at this point.
    if (m_is_d3d11) {
        deinit_d3d11();
    }

    if (m_is_d3d12) {
        //deinit_d3d12();
    }

    if (m_game_data_initialized) {
        //m_mods->on_device_reset();
    }

    m_has_frame = false;
    m_first_initialize = false;
    m_initialized = false;
}

void Darkest1Modloader::consume_input() 
{

}

void Darkest1Modloader::init_fonts() 
{

}

void Darkest1Modloader::invalidate_device_objects() 
{
    if (!m_wants_device_object_cleanup) 
    {
        return;
    }

    if (m_renderer_type == RendererType::D3D11) 
    {
        ImGui_ImplDX11_InvalidateDeviceObjects();
    } 
    else if (m_renderer_type == RendererType::D3D12) 
    {
        //ImGui_ImplDX12_InvalidateDeviceObjects();
    }

    m_wants_device_object_cleanup = false;
}

void Darkest1Modloader::set_draw_ui(bool state, bool should_save) 
{
    std::scoped_lock _{m_config_mtx};

    bool prev_state = m_draw_ui;
    m_draw_ui = state;

    if (m_game_data_initialized) 
    {
        //REFrameworkConfig::get()->get_menu_open()->value() = state;
    }

    if (state != prev_state && should_save) 
    {
        if (!state) 
        {
            if (m_main_window_display_size.x > 0.0f && m_main_window_display_size.y > 0.0f) 
            {
                // REFrameworkConfig::get()->set_ui_layout_state(
                //     static_cast<int32_t>(m_main_window_display_size.x),
                //     static_cast<int32_t>(m_main_window_display_size.y),
                //     m_font_size);
            }

            m_ui_layout_save_pending = false;
            m_wants_save_imgui_config = true;
        }

        if (m_game_data_initialized) 
        {
            //save_config();
        }
    }
}

void Darkest1Modloader::draw_ui()
{
    std::lock_guard _{m_input_mutex};

    ImGui::GetIO().MouseDrawCursor = m_draw_ui /*&& REFrameworkConfig::get()->is_always_show_cursor()*/;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // causes bugs with the cursor

    if (!m_draw_ui) 
    {
        //remove_set_cursor_pos_patch();

        m_is_ui_focused = false;
        if (m_last_draw_ui) 
        {
            //m_windows_message_hook->window_toggle_cursor(m_cursor_state);
        }
        //m_dinput_hook->acknowledge_input();
        return;
    } 
    else 
    {
        //patch_set_cursor_pos();
    }
    
    // UI Specific code:
    m_is_ui_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);

    if (m_ui_option_transparent) 
    {
        auto& style = ImGui::GetStyle();
        if (m_is_ui_focused) 
        {
            style.Alpha = 1.0f;
        } 
        else 
        {
            if (ImGui::IsWindowHovered(ImGuiFocusedFlags_AnyWindow)) 
            {
                style.Alpha = 0.9f;
            } 
            else 
            {
                style.Alpha = 0.8f;
            }
        }
    } 
    else 
    {
        auto& style = ImGui::GetStyle();
        style.Alpha = 1.0f;
    }

    auto& io = ImGui::GetIO();

    if (io.WantCaptureKeyboard) 
    {
        //m_dinput_hook->ignore_input();
    } 
    else 
    {
        //m_dinput_hook->acknowledge_input();
    }

    if (!m_last_draw_ui || m_cursor_state_changed) 
    {
        m_cursor_state_changed = false;
        //m_windows_message_hook->window_toggle_cursor(true);
    }

    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_::ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 500), ImGuiCond_::ImGuiCond_FirstUseEver);

    ImGui::PushFont(m_default_font, m_font_size);
    static const auto REF_NAME = std::format("REFramework [{}+{}-{:.8}]", Darkest_Dungeon_Modloader::cmake::project_version_tweak,  Darkest_Dungeon_Modloader::cmake::project_version, Darkest_Dungeon_Modloader::cmake::git_sha);
    preserve_main_window_position(REF_NAME.c_str());
    bool is_open = true;
    ImGui::Begin(REF_NAME.c_str(), &is_open);
    const auto* main_window = ImGui::GetCurrentWindow();
    ImGui::Text("Default Menu Key: Insert");
    ImGui::Checkbox("Transparency", &m_ui_option_transparent);
    ImGui::SameLine();
    ImGui::Text("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Makes the UI transparent when not focused.");
    ImGui::Checkbox("Input Passthrough", &m_ui_passthrough);
    ImGui::SameLine();
    ImGui::Text("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Allows mouse and keyboard inputs to register to the game while the UI is focused.");

    // Mods:
    draw_about();

    if (m_error.empty() && m_game_data_initialized) 
    {
        //m_mods->on_draw_ui();
    } 
    else if (!m_game_data_initialized) 
    {
        ImGui::TextWrapped("REFramework is currently initializing...");
        ImGui::TextWrapped("This menu will close after initialization if you have the remember option enabled.");
    } 
    else if (!m_error.empty()) 
    {
        ImGui::TextWrapped("REFramework error: %s", m_error.c_str());
    }

    m_last_window_pos = main_window->Pos;
    m_last_window_size = main_window->Size;

    track_manual_ui_layout_changes();

    ImGui::PopFont();
    ImGui::End();

    // save the menu state in config
    if (!is_open) 
    {
        set_draw_ui(is_open, true);
    } 
    else if (m_draw_ui != m_last_draw_ui) 
    {
        set_draw_ui(m_draw_ui, true);
    }

    // if we pressed the X button to close the menu.
    if (m_last_draw_ui && !m_draw_ui) 
    {
        //m_windows_message_hook->window_toggle_cursor(m_cursor_state);
    }
}

void Darkest1Modloader::draw_about() 
{
//     if (!ImGui::CollapsingHeader("About")) {
//         return;
//     }

//     ImGui::TreePush("About");

//     ImGui::Text("Author: praydog");
//     ImGui::Text("Inspired by the Kanan project.");
//     ImGui::Text("https://github.com/praydog/REFramework");
//     ImGui::Text("http://praydog.com");
//     ImGui::Text("Branch: %s", REF_BRANCH);
//     ImGui::Text("Commits: %i", REF_TOTAL_COMMITS);
//     ImGui::Text("Commit hash: %s", std::format("{:.8}", REF_COMMIT_HASH).c_str());
//     ImGui::Text("Tag: %s", REF_TAG);
// #ifdef REF_COMMITS_PAST_TAG
//     ImGui::Text("Commits past tag: %i", REF_COMMITS_PAST_TAG);
// #endif
//     ImGui::Text("Build date: %s", REF_BUILD_DATE);
//     ImGui::Text("Build time: %s", REF_BUILD_TIME);

//     if (ImGui::TreeNode("Licenses")) 
//     {
//         struct License 
//         {
//             std::string name;
//             std::string text;
//         };

//         static std::array<License, 16> licenses
//         {
//             License{ "glm", license::glm },
//             License{ "imgui", license::imgui },
//             License{ "cimgui", license::cimgui },
//             License{ "minhook", license::minhook },
//             License{ "spdlog", license::spdlog },
//             License{ "robotocjksc", license::roboto_cjk },
//             License{ "openvr", license::openvr },
//             License{ "lua", license::lua },
//             License{ "sol", license::sol },
//             License{ "json", license::json },
//             License{ "asmjit", license::asmjit },
//             License{ "bddisasm", utility::narrow(license::bddisasm) },
//             License{ "openxr", license::openxr },
//             License{ "imguizmo", license::imguizmo },
//             License{ "DirectXTK", license::directxtk },
//             License{ "DirectXTK12", license::directxtk },
//         };

//         for (const auto& license : licenses) 
//         {
//             if (ImGui::CollapsingHeader(license.name.c_str())) 
//             {
//                 ImGui::TextWrapped(license.text.c_str());
//             }
//         }

//         ImGui::TreePop();
//     }

//     ImGui::Separator();

//     if (m_game_data_initialized && m_error.empty()) 
//     {
//         try 
//         {
//             static auto version_t = sdk::find_type_definition("via.version");
//             static std::string clean_version{};
//             static std::string engine_config{};
//             static auto tdb_version = sdk::RETypeDB::get()->get_version();

//             if (version_t != nullptr && clean_version.empty()) {
//                 auto m = version_t->get_method("getPrettyVersionString");

//                 if (m != nullptr) {
//                     auto pretty_string = m->call<::SystemString*>(sdk::get_thread_context(), nullptr);

//                     if (pretty_string != nullptr) {
//                         clean_version = utility::re_string::get_string(pretty_string);
//                     }
//                 }
//             }

//             if (version_t != nullptr && engine_config.empty()) {
//                 auto m = version_t->get_method("getConfigName");

//                 if (m != nullptr) {
//                     auto config_name = m->call<::SystemString*>(sdk::get_thread_context(), nullptr);

//                     if (config_name != nullptr) {
//                         engine_config = utility::re_string::get_string(config_name);
//                     }
//                 }
//             }

//             ImGui::Text("Engine information");
//             ImGui::Text(" Config: %s", engine_config.c_str());
//             ImGui::Text(" Version: %s", clean_version.c_str());
//             ImGui::Text(" TDB Version: %i", tdb_version);
//         } 
//         catch(...) 
//         {
//             ImGui::Text("Unable to determine engine version.");
//         }
//     } 
//     else 
//     {
//         ImGui::Text("Unable to determine engine version.");
//     }

//     ImGui::TreePop();
}

void Darkest1Modloader::ensure_ui_layout_baseline() 
{
    if (!m_mods_fully_initialized )//|| REFrameworkConfig::get()->has_ui_layout_state()) 
    {
        return;
    }

    const auto display_size = ImGui::GetIO().DisplaySize;

    if (display_size.x <= 0.0f || display_size.y <= 0.0f) 
    {
        return;
    }

    // REFrameworkConfig::get()->set_ui_layout_state(
    //     static_cast<int32_t>(display_size.x),
    //     static_cast<int32_t>(display_size.y),
    //     m_font_size);
    // request_save_config();
}

void Darkest1Modloader::process_ui_layout_save(bool from_present) 
{
    if (!m_ui_layout_save_pending || from_present ||
        std::chrono::steady_clock::now() - m_ui_layout_last_changed < std::chrono::milliseconds{500}) 
    {
        return;
    }

    m_ui_layout_save_pending = false;
    //request_save_config();
    m_wants_save_imgui_config = true;
    m_saved_ui_display_size = m_main_window_display_size;
}

void Darkest1Modloader::track_manual_ui_layout_changes() 
{
    auto& context = *ImGui::GetCurrentContext();
    const bool left_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool left_mouse_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    for (const auto* window : context.Windows) 
    {
        if (window->LastFrameActive != context.FrameCount ||
            (window->Flags & (ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_ChildWindow)) != 0) 
        {
            continue;
        }

        const UIWindowGeometry geometry{window->Pos, window->SizeFull};
        const auto [entry, inserted] = m_ui_window_geometries.try_emplace(window->ID, geometry);

        if (inserted) 
        {
            continue;
        }

        const bool geometry_changed =
            entry->second.position.x != geometry.position.x ||
            entry->second.position.y != geometry.position.y ||
            entry->second.size.x != geometry.size.x ||
            entry->second.size.y != geometry.size.y;

        entry->second = geometry;

        if (geometry_changed && (left_mouse_down || left_mouse_released)) 
        {
            m_manual_ui_geometry_dirty = true;
        }
    }

    if (m_ui_layout_save_pending) 
    {
        m_manual_ui_geometry_dirty = false;
    } 
    else if (left_mouse_released && std::exchange(m_manual_ui_geometry_dirty, false)) 
    {
        m_wants_save_imgui_config = true;
    }
}

void Darkest1Modloader::preserve_main_window_position(const char *window_name)
{
    const auto previous_display_size = m_main_window_display_size;
    const auto current_display_size = ImGui::GetIO().DisplaySize;

    if (current_display_size.x <= 0.0f || current_display_size.y <= 0.0f) {
        return;
    }

    m_main_window_display_size = current_display_size;

    const bool display_size_changed =
        previous_display_size.x > 0.0f && previous_display_size.y > 0.0f &&
        (previous_display_size.x != m_main_window_display_size.x ||
         previous_display_size.y != m_main_window_display_size.y);

    scale_font_for_display(m_main_window_display_size.y);

    if (!m_loaded_saved_ui_display_size) 
    {
        m_loaded_saved_ui_display_size = true;
        // const auto config_path = get_persistent_dir(REFrameworkConfig::REFRAMEWORK_CONFIG_NAME.data()).string();

        // if (fs::exists(utility::widen(config_path))) 
        // {
        //     const utility::Config cfg{config_path};
        //     m_saved_ui_display_size = ImVec2{
        //         static_cast<float>(cfg.get<int32_t>(REFrameworkConfig::UI_MONITOR_WIDTH_CONFIG_NAME.data()).value_or(0)),
        //         static_cast<float>(cfg.get<int32_t>(REFrameworkConfig::UI_MONITOR_HEIGHT_CONFIG_NAME.data()).value_or(0))};
        // }
    }

    ImVec2 position{};
    ImVec2 window_size{};
    bool restoring_scaled_size = false;
    bool returned_to_saved_display = false;

    const auto* settings = ImGui::FindWindowSettingsByID(ImHashStr(window_name));
    const bool current_display_is_saved_display =
        m_saved_ui_display_size.x > 0.0f && m_saved_ui_display_size.y > 0.0f &&
        m_saved_ui_display_size.x == m_main_window_display_size.x &&
        m_saved_ui_display_size.y == m_main_window_display_size.y;

    if (display_size_changed && m_ui_layout_save_pending && current_display_is_saved_display && settings != nullptr) {
        position = ImVec2{static_cast<float>(settings->Pos.x), static_cast<float>(settings->Pos.y)};
        window_size = ImVec2{static_cast<float>(settings->Size.x), static_cast<float>(settings->Size.y)};
        restoring_scaled_size = true;
        returned_to_saved_display = true;
        m_ui_layout_save_pending = false;
    } else if (const auto* window = ImGui::FindWindowByName(window_name); window != nullptr) {
        position = window->Pos;
        window_size = window->Size;

        if (display_size_changed) {
            const ImVec2 display_scale{
                m_main_window_display_size.x / previous_display_size.x,
                m_main_window_display_size.y / previous_display_size.y};
            position = ImVec2{position.x * display_scale.x, position.y * display_scale.y};
            window_size = ImVec2{window_size.x * display_scale.x, window_size.y * display_scale.y};
            restoring_scaled_size = true;
        }
    } else if (settings != nullptr) {
        position = ImVec2{static_cast<float>(settings->Pos.x), static_cast<float>(settings->Pos.y)};
        window_size = ImVec2{static_cast<float>(settings->Size.x), static_cast<float>(settings->Size.y)};

        if (display_size_changed && m_last_window_size.x > 0.0f && m_last_window_size.y > 0.0f) {
            const ImVec2 display_scale{
                m_main_window_display_size.x / previous_display_size.x,
                m_main_window_display_size.y / previous_display_size.y};
            position = ImVec2{m_last_window_pos.x * display_scale.x, m_last_window_pos.y * display_scale.y};
            window_size = ImVec2{m_last_window_size.x * display_scale.x, m_last_window_size.y * display_scale.y};
            restoring_scaled_size = true;
        } else if (m_saved_ui_display_size.x > 0.0f && m_saved_ui_display_size.y > 0.0f &&
            (m_saved_ui_display_size.x != m_main_window_display_size.x ||
             m_saved_ui_display_size.y != m_main_window_display_size.y)) {
            const ImVec2 display_scale{
                m_main_window_display_size.x / m_saved_ui_display_size.x,
                m_main_window_display_size.y / m_saved_ui_display_size.y};
            position = ImVec2{position.x * display_scale.x, position.y * display_scale.y};
            window_size = ImVec2{window_size.x * display_scale.x, window_size.y * display_scale.y};
            restoring_scaled_size = true;
        }
    } else {
        return;
    }

    if (m_main_window_display_size.x > 0.0f && m_main_window_display_size.y > 0.0f) {
        const auto visibility_padding = ImMax(ImGui::GetStyle().DisplayWindowPadding, ImGui::GetStyle().DisplaySafeAreaPadding);

        if (position.x > m_main_window_display_size.x - visibility_padding.x) {
            position.x = ImMax(0.0f, m_main_window_display_size.x - window_size.x);
        } else if (position.x + window_size.x < visibility_padding.x) {
            position.x = 0.0f;
        }

        if (position.y > m_main_window_display_size.y - visibility_padding.y) {
            position.y = ImMax(0.0f, m_main_window_display_size.y - window_size.y);
        } else if (position.y + window_size.y < visibility_padding.y) {
            position.y = 0.0f;
        }
    }

    ImGui::SetNextWindowPos(position, ImGuiCond_Always);

    if (restoring_scaled_size) {
        ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
        // REFrameworkConfig::get()->set_ui_layout_state(
        //     static_cast<int32_t>(m_main_window_display_size.x),
        //     static_cast<int32_t>(m_main_window_display_size.y),
        //     m_font_size);

        if (!returned_to_saved_display) {
            m_ui_layout_save_pending = true;
            m_ui_layout_last_changed = std::chrono::steady_clock::now();
        }
    }
}

void Darkest1Modloader::scale_font_for_display(float display_height) 
{
    if (display_height <= 0.0f) 
    {
        return;
    }

    if (m_font_display_height > 0.0f && m_font_display_height != display_height) 
    {
        m_font_size *= display_height / m_font_display_height;
    }

    m_font_display_height = display_height;
}

bool Darkest1Modloader::hook_d3d11() 
{ 
    //if (m_d3d11_hook == nullptr) 
    {
        m_d3d11_hook.reset();
        m_d3d11_hook = std::make_unique<D3D11Hook>();
        m_d3d11_hook->on_present([this](D3D11Hook& hook) { on_frame_d3d11(); });
        m_d3d11_hook->on_post_present([this](D3D11Hook& hook) { on_post_present_d3d11(); });
        m_d3d11_hook->on_resize_buffers([this](D3D11Hook& hook) { on_reset(); });
    }

    // Making sure D3D12 is not hooked
    if (!m_is_d3d12) 
    {
        if (m_d3d11_hook->hook()) 
        {
            spdlog::info("Hooked DirectX 11");
            m_valid = true;
            m_is_d3d11 = true;
            return true;
        }
        // We make sure to no unhook any unwanted hooks if D3D11 didn't get hooked properly
        if (m_d3d11_hook->unhook()) 
        {
            spdlog::info("D3D11 unhooked!");
        } 
        else 
        {
            spdlog::info("Cannot unhook D3D11, this might crash.");
        }

        m_valid = false;
        m_is_d3d11 = false;
        return false;
    }

    return false;
}

bool Darkest1Modloader::hook_d3d12() 
{ 
    // windows 7?
    // if (LoadLibraryA("d3d12.dll") == nullptr) 
    // {
    //     spdlog::info("d3d12.dll not found, user is probably running Windows 7.");
    //     spdlog::info("Falling back to hooking D3D11.");

    //     m_is_d3d12 = false;
    //     return hook_d3d11();
    // }

    // //if (m_d3d12_hook == nullptr) 
    // {
    //     m_d3d12_hook.reset();
    //     m_d3d12_hook = std::make_unique<D3D12Hook>();
    //     m_d3d12_hook->on_present([this](D3D12Hook& hook) { on_frame_d3d12(); });
    //     m_d3d12_hook->on_post_present([this](D3D12Hook& hook) { on_post_present_d3d12(); });
    //     m_d3d12_hook->on_resize_buffers([this](D3D12Hook& hook) { on_reset(); });
    //     m_d3d12_hook->on_resize_target([this](D3D12Hook& hook) { on_reset(); });
    // }
    // //m_d3d12_hook->on_create_swap_chain([this](D3D12Hook& hook) { m_d3d12.command_queue = m_d3d12_hook->get_command_queue(); });

    // // Making sure D3D11 is not hooked
    // if (!m_is_d3d11) 
    // {
    //     if (m_d3d12_hook->hook()) 
    //     {
    //         spdlog::info("Hooked DirectX 12");
    //         m_valid = true;
    //         m_is_d3d12 = true;
    //         return true;
    //     }
    //     // We make sure to no unhook any unwanted hooks if D3D12 didn't get hooked properly
    //     if (m_d3d12_hook->unhook()) 
    //     {
    //         spdlog::info("D3D12 Unhooked!");
    //     } 
    //     else 
    //     {
    //         spdlog::info("Cannot unhook D3D12, this might crash.");
    //     }

    //     m_valid = false;
    //     m_is_d3d12 = false;

    //     // Try to hook d3d11 instead
    //     return hook_d3d11();
    // }

    return false;
}

void Darkest1Modloader::open_console() {}

bool Darkest1Modloader::init_d3d11() 
{ 
    deinit_d3d11();

    auto swapchain = m_d3d11_hook->get_swap_chain();
    auto device = m_d3d11_hook->get_device();

    // Get back buffer.
    spdlog::info("[D3D11] Creating RTV of back buffer...");

    ComPtr<ID3D11Texture2D> backbuffer{};

    if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)))) {
        spdlog::error("[D3D11] Failed to get back buffer!");
        return false;
    }

    // Create a render target view of the back buffer.
    if (FAILED(device->CreateRenderTargetView(backbuffer.Get(), nullptr, &m_d3d11.bb_rtv))) {
        spdlog::error("[D3D11] Failed to create back buffer render target view!");
        return false;
    }

    // Get backbuffer description.
    D3D11_TEXTURE2D_DESC backbuffer_desc{};

    backbuffer->GetDesc(&backbuffer_desc);
    backbuffer_desc.BindFlags |= D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    spdlog::info("[D3D11] Back buffer format is {}", backbuffer_desc.Format);

    // Create our blank render target.
    spdlog::info("[D3D11] Creating render targets...");
    {
        // Create our blank render target.
        auto d3d11_rt_desc = backbuffer_desc;
        d3d11_rt_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // For VR

        if (FAILED(device->CreateTexture2D(&d3d11_rt_desc, nullptr, &m_d3d11.blank_rt))) {
            spdlog::error("[D3D11] Failed to create render target texture!");
            return false;
        }

        // Create our render target
        if (FAILED(device->CreateTexture2D(&d3d11_rt_desc, nullptr, &m_d3d11.rt))) {
            spdlog::error("[D3D11] Failed to create render target texture!");
            return false;
        }
    }

    // Create our blank render target view.
    spdlog::info("[D3D11] Creating rtvs...");

    if (FAILED(device->CreateRenderTargetView(m_d3d11.blank_rt.Get(), nullptr, &m_d3d11.blank_rt_rtv))) {
        spdlog::error("[D3D11] Failed to create render terget view!");
        return false;
    }


    // Create our render target view.
    if (FAILED(device->CreateRenderTargetView(m_d3d11.rt.Get(), nullptr, &m_d3d11.rt_rtv))) {
        spdlog::error("[D3D11] Failed to create render terget view!");
        return false;
    }

    // Create our render target shader resource view.
    spdlog::info("[D3D11] Creating srvs...");

    if (FAILED(device->CreateShaderResourceView(m_d3d11.rt.Get(), nullptr, &m_d3d11.rt_srv))) {
        spdlog::error("[D3D11] Failed to create shader resource view!");
        return false;
    }

    m_d3d11.rt_width = backbuffer_desc.Width;
    m_d3d11.rt_height = backbuffer_desc.Height;

    spdlog::info("[D3D11] Initializing ImGui D3D11...");

    ComPtr<ID3D11DeviceContext> context{};

    device->GetImmediateContext(&context);

    if (!ImGui_ImplDX11_Init(device, context.Get())) {
        spdlog::error("[D3D11] Failed to initialize ImGui.");
        return false;
    }

    return true;
}

void Darkest1Modloader::deinit_d3d11() 
{
    ImGui_ImplDX11_Shutdown();
    m_d3d11 = {};
}

bool Darkest1Modloader::on_message(HWND wnd, UINT message, WPARAM w_param, LPARAM l_param) 
{ 
    m_last_message_time = std::chrono::steady_clock::now();

    if (!m_initialized) {
        return true;
    }

    bool is_mouse_moving{false};
    switch (message) {
    case WM_LBUTTONDOWN:
        m_last_keys[VK_LBUTTON] = true;
        break;
    case WM_LBUTTONUP:
        m_last_keys[VK_LBUTTON] = false;
        break;
    case WM_RBUTTONDOWN:
        m_last_keys[VK_RBUTTON] = true;
        break;
    case WM_RBUTTONUP:
        m_last_keys[VK_RBUTTON] = false;
        break;
    case WM_MBUTTONDOWN:
        m_last_keys[VK_MBUTTON] = true;
        break;
    case WM_MBUTTONUP:
        m_last_keys[VK_MBUTTON] = false;
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: 
    {
        const auto menu_key = true;//REFrameworkConfig::get()->get_menu_key()->value();

        if (w_param == menu_key && !m_last_keys[w_param]) 
        {
            std::lock_guard _{m_input_mutex};

            set_draw_ui(!m_draw_ui);
        }

        m_last_keys[w_param] = true;
        
        break;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP:
        m_last_keys[w_param] = false;
        break;
    case WM_KILLFOCUS:
        std::fill(std::begin(m_last_keys), std::end(m_last_keys), false);
        break;
    case WM_INPUT: 
    {
        // RIM_INPUT means the window has focus
        if (GET_RAWINPUT_CODE_WPARAM(w_param) == RIM_INPUT) 
        {
            uint32_t size = sizeof(RAWINPUT);
            RAWINPUT raw{};
            
            // obtain size
            GetRawInputData((HRAWINPUT)l_param, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));

            auto result = GetRawInputData((HRAWINPUT)l_param, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER));

            if (raw.header.dwType == RIM_TYPEMOUSE) {
                m_accumulated_mouse_delta[0] += (float)raw.data.mouse.lLastX;
                m_accumulated_mouse_delta[1] += (float)raw.data.mouse.lLastY;

                // Allowing camera movement when the UI is hovered while not focused
                if (raw.data.mouse.lLastX || raw.data.mouse.lLastY) 
                {
                    is_mouse_moving = true;
                }
            }
        }
    } break;

    // Fixes for stuttering when USB devices are removed/inserted
    // Causes all sorts of random things to happen like DXGI ResizeTarget to get called...
    // Maybe they forgot to add a break statement for WM_DEVICECHANGE and it falls through to some window resizing case?
    // https://github.com/PGGB/DeviceStutterFix
    case WM_DEVICECHANGE:
        switch (w_param) 
        {
            // case DBT_DEVICEARRIVAL:
            // case DBT_DEVICEREMOVECOMPLETE:
            //     return is_device_controller((PDEV_BROADCAST_HDR)l_param, w_param);
            default:
                spdlog::info("Event {:x}: skipping", w_param);
                return false;
        }
        
        break;
    case RE_TOGGLE_CURSOR: 
    {
        const auto is_internal_message = l_param != 0;
        const auto return_value = is_internal_message || !m_draw_ui;

        if (!is_internal_message) 
        {
            m_cursor_state = (bool)w_param;
            m_cursor_state_changed = true;
        }

        return return_value;
    } 
        break;
    default:
        break;
    }

    //ImGui_ImplWin32_WndProcHandler(wnd, message, w_param, l_param);

    {
        // If the user is interacting with the UI we block the message from going to the game.
        const auto& io = ImGui::GetIO();
        if (m_draw_ui && !m_ui_passthrough) 
        {
            // Fix of a bug that makes the input key down register but the key up will never register
            // when clicking on the ui while the game is not focused
            if (message == WM_INPUT && GET_RAWINPUT_CODE_WPARAM(w_param) == RIM_INPUTSINK)
                return false;

            static std::unordered_set<UINT> forcefully_allowed_messages 
            {
                WM_DEVICECHANGE,
                WM_SHOWWINDOW,
                WM_ACTIVATE,
                WM_ACTIVATEAPP,
                WM_CLOSE,
                WM_DPICHANGED,
                WM_SIZING,
                WM_MOUSEACTIVATE
            };

            if (forcefully_allowed_messages.find(message) == forcefully_allowed_messages.end()) 
            {
                if (m_is_ui_focused) 
                {
                    if (io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput)
                        return false;
                } 
                else 
                {
                    if (!is_mouse_moving && (io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput))
                        return false;
                }
            }
        }
    }

    bool any_false = false;

    if (m_game_data_initialized) 
    {
        // for (auto& mod : m_mods->get_mods()) 
        // {
        //     if (!mod->on_message(wnd, message, w_param, l_param)) 
        //     {
        //         any_false = true;
        //     }
        // }
    }

    return !any_false;
}
