#ifndef DLL_INJECTOR_DARKEST1MODLOADER_HPP_
#define DLL_INJECTOR_DARKEST1MODLOADER_HPP_

#include <Windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>  // TODO: create configs
#include <thread>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <utility>

#include <MinHook.h>

#include <spdlog/spdlog.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include "internal_use_only/config.hpp"

#include "hooks/GLHook.hpp"
#include "ModLoader.hpp"
#include "hooks/WindowsMessageHook.hpp"
#include "hooks/GLVersion.hpp"

class Darkest1Modloader : public ModLoader
{
public:
    struct DoNotHook
    {
        explicit DoNotHook(std::atomic<uint32_t>& count)
            : m_count(count)
        {
            ++m_count;
        }

        ~DoNotHook()
        {
            --m_count;
        }

        DoNotHook(const DoNotHook&) = delete;
        DoNotHook& operator=(const DoNotHook&) = delete;

    private:
        std::atomic<uint32_t>& m_count;
    };

public:
    explicit Darkest1Modloader(HMODULE module);
    virtual ~Darkest1Modloader();

public:
    void hook_monitor();
    bool hook_gl();
    void open_console();

    bool initialize(GLHook& hook);
    bool initialize_windows_message_hook();
    bool first_frame_initialize();

    void run_imgui_frame(bool from_present);

    // Колбэки GLHook (вызываются на render-потоке игры, GL-контекст current).
    void on_frame_gl(GLHook& hook);
    void on_post_present_gl(GLHook& hook);
    void on_context_changed(GLHook& hook);

    // Освобождает ImGui-бэкенды. owner_context: контекст, где созданы GL-объекты
    // (nullptr - текущий).
    void release_render_backends(GLHook& hook, HGLRC owner_context);

    void consume_input();
    void init_fonts();
    void invalidate_device_objects();

    void set_draw_ui(bool state, bool should_save = true);

    void draw_ui();
    void draw_about();

    void ensure_ui_layout_baseline();
    void process_ui_layout_save(bool from_present);
    void track_manual_ui_layout_changes();

    void preserve_main_window_position(const char* window_name);
    void scale_font_for_display(float display_height);

    bool on_message(HWND wnd, UINT message, WPARAM w_param, LPARAM l_param);

    DoNotHook acquire_do_not_hook()
    {
        return DoNotHook{m_do_not_hook_count};
    }

    // Единый мьютекс детуров живёт в GLHook (статический): он переживает любой объект.
    virtual std::recursive_mutex& get_hook_monitor_mutex() override
    {
        return GLHook::swap_mutex();
    }

public:
    static inline bool s_fallback_appdata{false};
    static inline bool s_checked_file_permissions{false};

private:
    using AtomicTime = std::atomic<std::chrono::steady_clock::time_point>;

    static inline HMODULE s_module{};
    std::atomic<uint32_t> m_do_not_hook_count{0};

    // Состояние жизненного цикла.
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_wants_shutdown{false};
    std::atomic<bool> m_terminating{false};
    std::atomic<bool> m_game_data_initialized{false};
    std::atomic<bool> m_mods_fully_initialized{false};
    bool m_first_frame{true};
    bool m_first_frame_initialize{true};
    bool m_message_hook_requested{false};  // Только render-поток.

    std::string m_error{};
    std::string m_gl_version{};

    // UI
    bool m_has_frame{false};
    bool m_wants_device_object_cleanup{false};
    std::atomic<bool> m_wants_save_imgui_config{false};
    std::atomic<bool> m_draw_ui{true};
    bool m_last_draw_ui{true};
    std::atomic<bool> m_is_ui_focused{false};
    bool m_cursor_state{false};
    bool m_cursor_state_changed{true};
    bool m_ui_option_transparent{true};
    bool m_ui_passthrough{false};

    ImVec2 m_last_window_pos{};
    ImVec2 m_last_window_size{};
    ImVec2 m_main_window_display_size{};
    bool m_loaded_saved_ui_display_size{false};
    ImVec2 m_saved_ui_display_size{};
    bool m_ui_layout_save_pending{false};
    std::chrono::steady_clock::time_point m_ui_layout_last_changed{};

    struct UIWindowGeometry
    {
        ImVec2 position{};
        ImVec2 size{};
    };

    std::map<ImGuiID, UIWindowGeometry> m_ui_window_geometries{};
    bool m_manual_ui_geometry_dirty{false};

    float m_font_size{16};
    float m_font_display_height{};
    ImFont* m_default_font{nullptr};
    bool m_fonts_need_init{true};

    std::mutex m_input_mutex{};
    std::recursive_mutex m_config_mtx{};
    std::recursive_mutex m_imgui_mtx{};
    std::recursive_mutex m_startup_mutex{};

    std::atomic<HWND> m_wnd{nullptr};
    HMODULE m_game_module{nullptr};

    float m_accumulated_mouse_delta[2]{};
    float m_mouse_delta[2]{};
    std::array<uint8_t, 256> m_last_keys{};

    std::unique_ptr<GLHook> m_gl_hook{};
    std::unique_ptr<WindowsMessageHook> m_windows_message_hook{};
    std::unique_ptr<std::jthread> m_monitor_thread{};

    // Монитор и render-поток читают/пишут это одновременно.
    AtomicTime m_last_present_time{};
    std::chrono::steady_clock::time_point m_last_chance_time{};  // Только монитор.
    std::chrono::steady_clock::time_point m_last_message_hook_check{};  // Только render-поток.
    uint32_t m_frames_since_init{0};
    bool m_has_last_chance{true};
};

#endif // DLL_INJECTOR_DARKEST1MODLOADER_HPP_