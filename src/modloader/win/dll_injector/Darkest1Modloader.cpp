#include "Darkest1Modloader.hpp"

namespace fs = std::filesystem;
using namespace std::literals;


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param);

namespace 
{

constexpr std::chrono::seconds kRehookTimeout{5};
constexpr std::chrono::seconds kHookGrace{5};
constexpr std::chrono::seconds kMessageHookCheckPeriod{5};
constexpr std::chrono::seconds kShutdownTimeout{2};
constexpr std::chrono::milliseconds kLayoutSaveDelay{500};
constexpr std::uint32_t kWarmupFrames = 60;

std::optional<fs::path> g_current_game_path{};

std::optional<std::size_t> get_module_image_size(HMODULE module) 
{
    if (module == nullptr) 
    {
        return std::nullopt;
    }
    const auto *base = reinterpret_cast<const std::uint8_t *>(module);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) 
    {
        return std::nullopt;
    }
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) 
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(nt->OptionalHeader.SizeOfImage);
}

std::optional<fs::path> get_module_dir(HMODULE module) 
{
    wchar_t buffer[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(module, buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) 
    {
        return std::nullopt;
    }
    return fs::path{buffer}.parent_path();
}

// <game_dir>/modloader - configs and ImGui ini.
fs::path get_persistent_dir() 
{
    fs::path dir = g_current_game_path.value_or(fs::current_path()) / "modloader";
    std::error_code ec{};
    fs::create_directories(dir, ec);
    if (ec) 
    {
        spdlog::warn("Cannot create persistent dir {}: {}", dir.string(), ec.message());
    }
    return dir;
}

void log_os_version() 
{
    using RtlGetVersionFn = LONG(WINAPI *)(PRTL_OSVERSIONINFOW);

    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) 
    {
        spdlog::warn("ntdll.dll not found");
        return;
    }

    const auto rtl_get_version =
        reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void *>(GetProcAddress(ntdll, "RtlGetVersion")));
    if (rtl_get_version == nullptr) 
    {
        spdlog::warn("RtlGetVersion not found");
        return;
    }

    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtl_get_version(&info) != 0) 
    {
        spdlog::warn("RtlGetVersion() failed");
        return;
    }
    spdlog::info("Windows {}.{} build {}", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
}

}  // namespace


Darkest1Modloader::Darkest1Modloader(HMODULE module) 
    : m_game_module{GetModuleHandleW(nullptr)} 
{
    s_module = module;

    std::scoped_lock startup_lock{m_startup_mutex};

    spdlog::info("Darkest1Modloader entry (v{}, {})",
        Darkest_Dungeon_Modloader::cmake::project_version,
        Darkest_Dungeon_Modloader::cmake::git_sha);

    if (s_fallback_appdata) 
    {
        spdlog::warn("Failed to write to current directory, falling back to appdata folder");
    }

    spdlog::info("Game module base: 0x{:X}", reinterpret_cast<std::uintptr_t>(m_game_module));
    if (const auto size = get_module_image_size(m_game_module); size.has_value()) 
    {
        spdlog::info("Game module size: 0x{:X}", *size);
    }

    g_current_game_path = get_module_dir(m_game_module);
    if (g_current_game_path.has_value()) 
    {
        spdlog::info("Game path: {}", g_current_game_path->string());
    }

    log_os_version();

    // MinHook: MH_Initialize -> CreateHook -> EnableHook -> DisableHook -> RemoveHook -> MH_Uninitialize.
    const MH_STATUS mh_status = MH_Initialize();
    if (mh_status != MH_OK && mh_status != MH_ERROR_ALREADY_INITIALIZED) 
    {
        spdlog::critical("MH_Initialize failed: {}", MH_StatusToString(mh_status));
        throw std::runtime_error{"MH_Initialize failed"};
    }

    m_last_present_time.store(std::chrono::steady_clock::now());

    m_monitor_thread = std::make_unique<std::jthread>([this](const std::stop_token &stop_token) 
    {
        while (!stop_token.stop_requested() && !m_terminating) 
        {
            hook_monitor();
            std::this_thread::sleep_for(500ms);
        }
    });

    spdlog::info("Hook monitor thread started");
}

Darkest1Modloader::~Darkest1Modloader() 
{
    spdlog::info("Darkest1Modloader shutting down...");
    m_terminating = true;

    if (m_monitor_thread != nullptr) 
    {
        m_monitor_thread->request_stop();
        if (m_monitor_thread->joinable()) 
        {
            m_monitor_thread->join();
        }
        m_monitor_thread.reset();
    }

    if (m_initialized) 
    {
        m_wants_shutdown = true;
        const auto deadline = std::chrono::steady_clock::now() + kShutdownTimeout;
        while (m_initialized && std::chrono::steady_clock::now() < deadline) 
        {
            std::this_thread::sleep_for(10ms);
        }
    }

    {
        std::scoped_lock hook_lock{GLHook::swap_mutex()};
        m_windows_message_hook.reset();
        m_gl_hook.reset();  // ~GLHook -> unhook (Disable -> Remove).
    }

    // 4. ImGui.
    if (!m_initialized) 
    {
        std::scoped_lock imgui_lock{m_imgui_mtx};
        if (ImGui::GetCurrentContext() != nullptr) 
        {
            ImGui::DestroyContext();
        }
    } 
    else 
    {
        spdlog::warn("Render thread did not release overlay resources in time; skipping GL cleanup");
    }

    const MH_STATUS status = MH_Uninitialize();
    if (status != MH_OK) 
    {
        spdlog::warn("MH_Uninitialize: {}", MH_StatusToString(status));
    }
}

void Darkest1Modloader::hook_monitor() 
{
    using Clock = std::chrono::steady_clock;

    const auto defer_checks = [this] 
    {
        m_last_present_time.store(Clock::now() + kHookGrace);
        m_last_chance_time = Clock::now() + 1s;
        m_has_last_chance = true;
    };

    if (m_do_not_hook_count.load() > 0) 
    {
        defer_checks();
        return;
    }

    auto &mutex = GLHook::swap_mutex();
    if (!mutex.try_lock()) 
    {
        defer_checks();
        return;
    }
    std::lock_guard lock{mutex, std::adopt_lock};

    if (g_loader == nullptr) 
    {
        return;
    }

    const auto now = Clock::now();

    if (m_gl_hook == nullptr) 
    {
        spdlog::info("Installing initial render hook");
        if (!hook_gl()) 
        {
            spdlog::error("OpenGL swap hooks could not be installed yet, will retry");
        }
        m_last_present_time.store(now + kHookGrace);
        return;
    }

    if (now - m_last_present_time.load() > kRehookTimeout) 
    {
        if (m_has_last_chance) 
        {
            m_has_last_chance = false;
            m_last_chance_time = now;
            spdlog::info("Last chance encountered for hooking");
        } 
        else if (now - m_last_chance_time > 1s) 
        {
            spdlog::warn("No swap for {}s, rehooking", kRehookTimeout.count());
            if (!hook_gl()) 
            {
                spdlog::error("Rehook failed");
            }
            defer_checks();
        }
    } 
    else 
    {
        m_last_chance_time = now;
        m_has_last_chance = true;
    }
}

bool Darkest1Modloader::hook_gl() 
{
    m_gl_hook.reset();

    auto hook = std::make_unique<GLHook>();
    hook->on_present([this](GLHook &h) { on_frame_gl(h); });
    hook->on_post_present([this](GLHook &h) { on_post_present_gl(h); });
    hook->on_context_changed([this](GLHook &h) { on_context_changed(h); });

    if (!hook->hook()) 
    {
        return false;
    }

    m_gl_hook = std::move(hook);
    spdlog::info("Hooked OpenGL");
    return true;
}

void Darkest1Modloader::open_console() 
{

}

bool Darkest1Modloader::initialize(GLHook &hook) 
{
    if (m_initialized) 
    {
        return true;
    }

    if (m_frames_since_init < kWarmupFrames) 
    {
        ++m_frames_since_init;
        return false;
    }

    HWND wnd = hook.get_window();
    if (wnd == nullptr) 
    {
        spdlog::warn("Cannot determine game window from HDC, retrying next frame");
        return false;
    }

    m_gl_version = hook.query_version_string();
    const std::string glsl_version{gl::glsl_header_for(gl::parse_version(m_gl_version))};
    spdlog::info("GL_VERSION: '{}' -> {}", m_gl_version, glsl_version);

    m_wnd = wnd;
    spdlog::info("Window handle: 0x{:X}", reinterpret_cast<std::uintptr_t>(wnd));

    IMGUI_CHECKVERSION();
    if (ImGui::GetCurrentContext() == nullptr) 
    {
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
    }

    m_loaded_saved_ui_display_size = false;
    m_saved_ui_display_size = {};

    static const std::string imgui_ini = (get_persistent_dir() / "ui.ini").string();
    ImGui::GetIO().IniFilename = imgui_ini.c_str();

    if (!ImGui_ImplWin32_Init(wnd)) 
    {
        spdlog::error("ImGui_ImplWin32_Init failed");
        return false;
    }

    if (!ImGui_ImplOpenGL3_Init(glsl_version.c_str())) 
    {
        spdlog::error("ImGui_ImplOpenGL3_Init failed");
        ImGui_ImplWin32_Shutdown();
        return false;
    }

    initialize_windows_message_hook();
    m_first_frame = false;
    m_game_data_initialized = true; // TODO: mods list

    spdlog::info("Darkest1Modloader overlay initialized");
    return true;
}

bool Darkest1Modloader::initialize_windows_message_hook() 
{
    HWND wnd = m_wnd.load();
    if (wnd == nullptr) 
    {
        return false;
    }

    if (m_first_frame || m_message_hook_requested || m_windows_message_hook == nullptr) 
    {
        m_windows_message_hook.reset();
        m_windows_message_hook = std::make_unique<WindowsMessageHook>(wnd);
        m_windows_message_hook->on_message = [this](HWND w, UINT msg, WPARAM w_param, LPARAM l_param) 
        {
            return on_message(w, msg, w_param, l_param);
        };
        m_message_hook_requested = false;
        return true;
    }

    m_message_hook_requested = false;
    return false;
}

bool Darkest1Modloader::first_frame_initialize() 
{
    if (!m_error.empty() || !m_game_data_initialized) 
    {
        return false;
    }

    if (std::exchange(m_first_frame_initialize, false)) 
    {
        spdlog::info("Running first-frame initialization");
        // TODO: m_mods->on_initialize_gl_thread() here
        m_mods_fully_initialized = true;
    }
    return true;
}

void Darkest1Modloader::run_imgui_frame(bool from_present) 
{
    std::scoped_lock lock{m_imgui_mtx};

    m_has_frame = false;
    if (!m_initialized) 
    {
        return;
    }

    consume_input();
    init_fonts();

    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    IMGUIZMO_NAMESPACE::BeginFrame(); 

    draw_ui();
    m_last_draw_ui = m_draw_ui;

    ensure_ui_layout_baseline();
    process_ui_layout_save(from_present);

    if (m_wants_save_imgui_config.exchange(false)) 
    {
        if (const char *ini_filename = ImGui::GetIO().IniFilename; ini_filename != nullptr) 
        {
            ImGui::SaveIniSettingsToDisk(ini_filename);
            spdlog::debug("Saved ImGui configuration");
        }
    }

    ImGui::EndFrame();
    ImGui::Render();
    m_has_frame = true;
}

void Darkest1Modloader::on_frame_gl(GLHook &hook) 
{
    std::scoped_lock lock{m_imgui_mtx};

    const auto now = std::chrono::steady_clock::now();
    m_last_present_time.store(now);

    if (m_wants_shutdown) 
    {
        if (m_initialized) 
        {
            release_render_backends(hook, nullptr);
        }
        return;
    }

    if (!m_initialized) 
    {
        if (initialize(hook)) 
        {
            m_initialized = true;
        }
        return;
    }
\
    if (hook.get_window() != m_wnd.load())
    {
        spdlog::warn("Game window changed, reinitializing overlay");
        release_render_backends(hook, nullptr);
        m_message_hook_requested = true;
        return;
    }

    if (now - m_last_message_hook_check > kMessageHookCheckPeriod) 
    {
        m_last_message_hook_check = now;
        if (m_windows_message_hook != nullptr && !m_windows_message_hook->is_hook_intact()) 
        {
            spdlog::warn("WndProc is no longer ours (another overlay subclassed the window?)");
        }
    }

    if (m_message_hook_requested) 
    {
        initialize_windows_message_hook();
    }

    if (!first_frame_initialize() && m_error.empty()) 
    {
        return;
    }

    if (!m_draw_ui && !m_last_draw_ui)
    {
        return;
    }

    invalidate_device_objects();
    ImGui_ImplOpenGL3_NewFrame();
    run_imgui_frame(true);

    if (!m_has_frame) 
    {
        return;
    }

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Darkest1Modloader::on_post_present_gl(GLHook & /*hook*/) {}

void Darkest1Modloader::on_context_changed(GLHook &hook) 
{
    std::scoped_lock lock{m_imgui_mtx};

    spdlog::warn("GL context changed (0x{:X} -> 0x{:X}), recreating overlay GL resources",
        reinterpret_cast<std::uintptr_t>(hook.get_previous_glrc()),
        reinterpret_cast<std::uintptr_t>(hook.get_glrc()));

    if (m_initialized) 
    {
        release_render_backends(hook, hook.get_previous_glrc());
    }
}

void Darkest1Modloader::release_render_backends(GLHook &hook, HGLRC owner_context) 
{
    std::scoped_lock lock{m_imgui_mtx};

    std::optional<ScopedGLContext> scope;
    if (owner_context != nullptr) 
    {
        scope.emplace(hook.api(), hook.get_dc(), owner_context);
        if (!scope->valid()) 
        {
            spdlog::warn("Old GL context cannot be bound (destroyed?); cleanup runs in the current one");
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    scope.reset();

    m_has_frame = false;
    m_initialized = false;
    spdlog::info("Overlay backends released");
}

void Darkest1Modloader::invalidate_device_objects() 
{
    if (!m_wants_device_object_cleanup) 
    {
        return;
    }
    ImGui_ImplOpenGL3_DestroyDeviceObjects();
    m_wants_device_object_cleanup = false;
}

void Darkest1Modloader::consume_input() 
{
    std::lock_guard lock{m_input_mutex};
    m_mouse_delta[0] = std::exchange(m_accumulated_mouse_delta[0], 0.0f);
    m_mouse_delta[1] = std::exchange(m_accumulated_mouse_delta[1], 0.0f);
}

void Darkest1Modloader::init_fonts() 
{
    if (!m_fonts_need_init) 
    {
        return;
    }
    // ImGui 1.92+: dynamic fonts.
    // nullptr in PushFont(font, size) means default font.
    m_default_font = nullptr;
    m_fonts_need_init = false;
}

void Darkest1Modloader::set_draw_ui(bool state, bool should_save) 
{
    std::scoped_lock lock{m_config_mtx};

    const bool prev_state = m_draw_ui;
    m_draw_ui = state;

    if (state != prev_state && should_save && !state) 
    {
        m_ui_layout_save_pending = false;
        m_wants_save_imgui_config = true;
    }
}

void Darkest1Modloader::draw_ui() 
{
    std::lock_guard lock{m_input_mutex};

    auto &io = ImGui::GetIO();
    io.MouseDrawCursor = m_draw_ui;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    if (!m_draw_ui) 
    {
        m_is_ui_focused = false;
        return;
    }

    m_is_ui_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);

    auto &style = ImGui::GetStyle();
    if (m_ui_option_transparent && !m_is_ui_focused) 
    {
        style.Alpha = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) ? 0.9f : 0.8f;
    } 
    else 
    {
        style.Alpha = 1.0f;
    }

    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 500), ImGuiCond_FirstUseEver);

    static const std::string title = std::format("Darkest Modloader [{}+{:.8}]",
        Darkest_Dungeon_Modloader::cmake::project_version,
        Darkest_Dungeon_Modloader::cmake::git_sha);

    ImGui::PushFont(m_default_font, m_font_size);
    preserve_main_window_position(title.c_str());

    bool is_open = true;
    if (ImGui::Begin(title.c_str(), &is_open)) 
    {
        ImGui::Text("Menu key: Insert / F1");
        ImGui::Checkbox("Transparency", &m_ui_option_transparent);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) 
        {
            ImGui::SetTooltip("Makes the UI transparent when not focused.");
        }

        ImGui::Checkbox("Input Passthrough", &m_ui_passthrough);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) 
        {
            ImGui::SetTooltip("Allows mouse and keyboard input to reach the game while the UI is focused.");
        }

        draw_about();

        if (!m_error.empty()) 
        {
            ImGui::TextWrapped("Modloader error: %s", m_error.c_str());
        } 
        else if (!m_game_data_initialized) 
        {
            ImGui::TextWrapped("Modloader is initializing...");
        } 
        else 
        {
            ImGui::TextDisabled("No mods loaded.");  // TODO: mods list.
        }

        m_last_window_pos = ImGui::GetWindowPos();
        m_last_window_size = ImGui::GetWindowSize();
    }
    ImGui::End();
    ImGui::PopFont();

    track_manual_ui_layout_changes();

    if (!is_open) 
    {
        set_draw_ui(false, true);
    }
}

void Darkest1Modloader::draw_about() 
{
    if (!ImGui::CollapsingHeader("About")) 
    {
        return;
    }
    ImGui::Text("Darkest Dungeon Modloader");
    ImGui::Text("Version: %s", std::string{Darkest_Dungeon_Modloader::cmake::project_version}.c_str());
    ImGui::Text("Commit: %s", std::format("{:.8}", Darkest_Dungeon_Modloader::cmake::git_sha).c_str());
    ImGui::Text("Renderer: OpenGL %s", m_gl_version.c_str());
    ImGui::Text("Architecture inspired by REFramework (praydog).");
}

void Darkest1Modloader::ensure_ui_layout_baseline() 
{
    if (!m_mods_fully_initialized || (m_saved_ui_display_size.x > 0.0f && m_saved_ui_display_size.y > 0.0f)) 
    {
        return;
    }

    const auto display_size = ImGui::GetIO().DisplaySize;
    if (display_size.x <= 0.0f || display_size.y <= 0.0f)
    {
        return;
    }
    m_saved_ui_display_size = display_size;
}

void Darkest1Modloader::process_ui_layout_save(bool from_present) 
{
    if (!m_ui_layout_save_pending || from_present ||
        std::chrono::steady_clock::now() - m_ui_layout_last_changed < kLayoutSaveDelay) 
    {
        return;
    }

    m_ui_layout_save_pending = false;
    m_wants_save_imgui_config = true;
    m_saved_ui_display_size = m_main_window_display_size;
}

void Darkest1Modloader::track_manual_ui_layout_changes() 
{
    auto *context = ImGui::GetCurrentContext();
    if (context == nullptr) 
    {
        return;
    }

    const bool left_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool left_mouse_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    for (const auto *window : context->Windows) 
    {
        if (window->LastFrameActive != context->FrameCount ||
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

        const bool changed = entry->second.position.x != geometry.position.x ||
                            entry->second.position.y != geometry.position.y ||
                            entry->second.size.x != geometry.size.x ||
                            entry->second.size.y != geometry.size.y;
        entry->second = geometry;

        if (changed && (left_mouse_down || left_mouse_released)) 
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

    if (current_display_size.x <= 0.0f || current_display_size.y <= 0.0f) 
    {
        return;
    }

    m_main_window_display_size = current_display_size;

    const bool display_size_changed = previous_display_size.x > 0.0f && previous_display_size.y > 0.0f &&
                                        (previous_display_size.x != m_main_window_display_size.x ||
                                        previous_display_size.y != m_main_window_display_size.y);

    scale_font_for_display(m_main_window_display_size.y);

    m_loaded_saved_ui_display_size = true;  // TODO: config file load.

    ImVec2 position{};
    ImVec2 window_size{};
    bool restoring_scaled_size = false;
    bool returned_to_saved_display = false;

    const auto *settings = ImGui::FindWindowSettingsByID(ImHashStr(window_name));
    const bool current_display_is_saved_display = m_saved_ui_display_size.x > 0.0f &&
                                                    m_saved_ui_display_size.y > 0.0f &&
                                                    m_saved_ui_display_size.x == m_main_window_display_size.x &&
                                                    m_saved_ui_display_size.y == m_main_window_display_size.y;

    const auto scale_by = [](ImVec2 value, ImVec2 from, ImVec2 to) 
    {
        return ImVec2{ value.x * (to.x / from.x), value.y * (to.y / from.y) };
    };

    if (display_size_changed && m_ui_layout_save_pending && current_display_is_saved_display && settings != nullptr) 
    {
        position = ImVec2{ static_cast<float>(settings->Pos.x), static_cast<float>(settings->Pos.y) };
        window_size = ImVec2{ static_cast<float>(settings->Size.x), static_cast<float>(settings->Size.y) };
        restoring_scaled_size = true;
        returned_to_saved_display = true;
        m_ui_layout_save_pending = false;
    } 
    else if (const auto *window = ImGui::FindWindowByName(window_name); window != nullptr) 
    {
        position = window->Pos;
        window_size = window->Size;

        if (display_size_changed) 
        {
            position = scale_by(position, previous_display_size, m_main_window_display_size);
            window_size = scale_by(window_size, previous_display_size, m_main_window_display_size);
            restoring_scaled_size = true;
        }
    } 
    else if (settings != nullptr) 
    {
        position = ImVec2{ static_cast<float>(settings->Pos.x), static_cast<float>(settings->Pos.y) };
        window_size = ImVec2{ static_cast<float>(settings->Size.x), static_cast<float>(settings->Size.y) };

        if (display_size_changed && m_last_window_size.x > 0.0f && m_last_window_size.y > 0.0f) 
        {
            position = scale_by(m_last_window_pos, previous_display_size, m_main_window_display_size);
            window_size = scale_by(m_last_window_size, previous_display_size, m_main_window_display_size);
            restoring_scaled_size = true;
        } 
        else if (m_saved_ui_display_size.x > 0.0f && m_saved_ui_display_size.y > 0.0f &&
                (m_saved_ui_display_size.x != m_main_window_display_size.x ||
                    m_saved_ui_display_size.y != m_main_window_display_size.y)) 
        {
            position = scale_by(position, m_saved_ui_display_size, m_main_window_display_size);
            window_size = scale_by(window_size, m_saved_ui_display_size, m_main_window_display_size);
            restoring_scaled_size = true;
        }
    } 
    else 
    {
        return;
    }


    const auto padding = ImMax(ImGui::GetStyle().DisplayWindowPadding, ImGui::GetStyle().DisplaySafeAreaPadding);

    if (position.x > m_main_window_display_size.x - padding.x) 
    {
        position.x = ImMax(0.0f, m_main_window_display_size.x - window_size.x);
    } 
    else if (position.x + window_size.x < padding.x) 
    {
        position.x = 0.0f;
    }

    if (position.y > m_main_window_display_size.y - padding.y) 
    {
        position.y = ImMax(0.0f, m_main_window_display_size.y - window_size.y);
    } 
    else if (position.y + window_size.y < padding.y) 
    {
        position.y = 0.0f;
    }

    ImGui::SetNextWindowPos(position, ImGuiCond_Always);

    if (restoring_scaled_size) 
    {
        ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);

        if (!returned_to_saved_display) 
        {
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

bool Darkest1Modloader::on_message(HWND wnd, UINT message, WPARAM w_param, LPARAM l_param) 
{
    if (!m_initialized) 
    {
        return true;
    }

    const auto set_key = [this](WPARAM key, bool down) 
    {
        if (key < m_last_keys.size()) 
        {
            m_last_keys[key] = down ? 1 : 0;
        }
    };

    bool is_mouse_moving = false;

    switch (message) 
    {
    case WM_LBUTTONDOWN:
        set_key(VK_LBUTTON, true);
        break;
    case WM_LBUTTONUP:
        set_key(VK_LBUTTON, false);
        break;
    case WM_RBUTTONDOWN:
        set_key(VK_RBUTTON, true);
        break;
    case WM_RBUTTONUP:
        set_key(VK_RBUTTON, false);
        break;
    case WM_MBUTTONDOWN:
        set_key(VK_MBUTTON, true);
        break;
    case WM_MBUTTONUP:
        set_key(VK_MBUTTON, false);
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: 
    {
        const bool is_menu_key = w_param == VK_INSERT || w_param == VK_F1;
        if (is_menu_key && w_param < m_last_keys.size() && m_last_keys[w_param] == 0) 
        {
            std::lock_guard lock{m_input_mutex};
            set_draw_ui(!m_draw_ui);
        }
        set_key(w_param, true);
        break;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP:
        set_key(w_param, false);
        break;
    case WM_KILLFOCUS:
        m_last_keys.fill(0);
        break;
    case WM_INPUT: 
    {
        // RIM_INPUT - window in focus.
        if (GET_RAWINPUT_CODE_WPARAM(w_param) == RIM_INPUT) 
        {
            RAWINPUT raw{};
            UINT size = sizeof(raw);
            const UINT copied = GetRawInputData(reinterpret_cast<HRAWINPUT>(l_param), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER));

            if (copied != static_cast<UINT>(-1) && raw.header.dwType == RIM_TYPEMOUSE) 
            {
                std::lock_guard lock{m_input_mutex};
                m_accumulated_mouse_delta[0] += static_cast<float>(raw.data.mouse.lLastX);
                m_accumulated_mouse_delta[1] += static_cast<float>(raw.data.mouse.lLastY);
                is_mouse_moving = raw.data.mouse.lLastX != 0 || raw.data.mouse.lLastY != 0;
            }
        }
        break;
    }
    case RE_TOGGLE_CURSOR: 
    {
        const bool is_internal_message = l_param != 0;
        if (!is_internal_message) 
        {
            m_cursor_state = static_cast<bool>(w_param);
            m_cursor_state_changed = true;
        }
        return is_internal_message || !m_draw_ui;
    }
    default:
        break;
    }

    if (!m_draw_ui) 
    {
        return true;
    }

    ImGui_ImplWin32_WndProcHandler(wnd, message, w_param, l_param);

    if (m_ui_passthrough) 
    {
        return true;
    }

    if (message == WM_INPUT && GET_RAWINPUT_CODE_WPARAM(w_param) == RIM_INPUTSINK) 
    {
        return false;
    }

    static const std::unordered_set<UINT> forcefully_allowed_messages
    {
        WM_DEVICECHANGE,
        WM_SHOWWINDOW,
        WM_ACTIVATE,
        WM_ACTIVATEAPP,
        WM_CLOSE,
        WM_DPICHANGED,
        WM_SIZE,
        WM_SIZING,
        WM_SYSCOMMAND,
        WM_MOUSEACTIVATE,
    };

    if (forcefully_allowed_messages.contains(message)) 
    {
        return true;
    }

    const auto &io = ImGui::GetIO();
    const bool ui_wants_input = io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput;

    if (m_is_ui_focused) 
    {
        return !ui_wants_input;
    }

    return is_mouse_moving || !ui_wants_input;
}