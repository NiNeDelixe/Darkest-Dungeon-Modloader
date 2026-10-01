#ifndef DLL_INJECTOR_DARKEST1MODLOADER_HPP_
#define DLL_INJECTOR_DARKEST1MODLOADER_HPP_

#include <Windows.h>

#include <memory>
#include <format>
#include <thread>
#include <filesystem>
#include <unordered_set>
#include <map>
#include <array>
#include <chrono>
#include <fstream>
#include <stop_token>

#include <wrl/client.h>

#include <spdlog/spdlog.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <ImGuizmo.h>

//#include <directxtk12-src/Inc/GraphicsMemory.h>
#include <d3d11.h>

#include "utility/Module.hpp"
#include "utility/Patch.hpp"
//#include "utility/PersistentTreeState.hpp"
#include "utility/Scan.hpp"
#include "utility/Thread.hpp"

#include "internal_use_only/config.hpp"

#include "ModLoader.hpp"

#include "WindowsMessageHook.hpp"
#include "D3D11Hook.hpp"

class Darkest1Modloader : public ModLoader
{
public:
    enum class RendererType : uint8_t 
    {
        D3D11,
        D3D12
    };

    struct DoNotHook 
    {
        DoNotHook(std::atomic<uint32_t>& count) 
            : m_count(count) 
        {
            ++m_count;
        }
    
        ~DoNotHook() 
        {
            --m_count;
        }

    private:
        std::atomic<uint32_t>& m_count;
    };

public:
    Darkest1Modloader(HMODULE module);
    virtual ~Darkest1Modloader();

public:
    void hook_monitor();
    bool initialize();
    //bool initialize_game_data();
    bool initialize_windows_message_hook();
    bool first_frame_initialize();

    void run_imgui_frame(bool from_present);

    void on_frame_d3d11();
    void on_post_present_d3d11();
    // void on_frame_d3d12();
    // void on_post_present_d3d12();
    void on_reset();

    //void save_config();
    void consume_input();
    void init_fonts();
    void invalidate_device_objects();

    void set_draw_ui(bool state, bool should_save = true);

    void draw_ui();
    void draw_about();

    //void call_on_frame();

    void ensure_ui_layout_baseline();
    void process_ui_layout_save(bool from_present);
    void track_manual_ui_layout_changes();
    
    void preserve_main_window_position(const char* window_name);
    void scale_font_for_display(float display_height);

public:
    bool hook_d3d11();
    bool hook_d3d12();
    void open_console();

public:
    bool init_d3d11();
    void deinit_d3d11();

public:
    bool on_message(HWND wnd, UINT message, WPARAM w_param, LPARAM l_param);

    DoNotHook acquire_do_not_hook_d3d() 
    {
        return DoNotHook{m_do_not_hook_d3d_count};
    }

    virtual std::recursive_mutex& get_hook_monitor_mutex() override
    {
        return m_hook_monitor_mutex;
    }

public:
    static inline bool s_fallback_appdata{false};
    static inline bool s_checked_file_permissions{false};

private:
    static inline HMODULE s_reframework_module{};
    std::atomic<uint32_t> m_do_not_hook_d3d_count{0};

    bool m_initialized = false;
    bool m_first_frame{true};
    bool m_first_frame_d3d_initialize{true};
    bool m_is_d3d12{false};
    bool m_is_d3d11{false};
    bool m_valid{false};
    bool m_created_default_cfg{false};
    bool m_started_game_data_thread{false};
    std::atomic<bool> m_terminating{false}; // Destructor is called
    std::atomic<bool> m_game_data_initialized{false};
    std::atomic<bool> m_mods_fully_initialized{false};

    std::string m_error{""};

    // UI
    bool m_has_frame{false};
    bool m_wants_device_object_cleanup{false};
    bool m_wants_save_config{false};
    std::atomic<bool> m_wants_save_imgui_config{false};
    bool m_draw_ui{true};
    bool m_last_draw_ui{m_draw_ui};
    bool m_is_ui_focused{false};
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

    struct AdditionalFont 
    {
        std::filesystem::path filepath{};
        float size{16};
        ImFont* font{};
    };

    std::string m_default_font_file = "DEFAULT";
    bool m_fonts_need_init{true};
    float m_font_size{16};
    float m_font_display_height{};
    ImFont* m_default_font;
    std::map<std::string, ImFont*> loaded_fonts{};
    std::vector<AdditionalFont> m_additional_fonts{};

    std::mutex m_input_mutex{};
    std::recursive_mutex m_config_mtx{};
    std::recursive_mutex m_imgui_mtx{};
    std::recursive_mutex m_patch_mtx{};

    HWND m_wnd{0};
    HMODULE m_game_module{0};

    float m_accumulated_mouse_delta[2]{};
    float m_mouse_delta[2]{};
    std::array<uint8_t, 256> m_last_keys{0};
    std::unique_ptr<D3D11Hook> m_d3d11_hook{};
    // std::unique_ptr<D3D12Hook> m_d3d12_hook{};
    std::unique_ptr<WindowsMessageHook> m_windows_message_hook;

    std::recursive_mutex m_hook_monitor_mutex{};
    std::recursive_mutex m_startup_mutex{};
    std::unique_ptr<std::jthread> m_d3d_monitor_thread{};
    std::chrono::steady_clock::time_point m_last_present_time{};
    std::chrono::steady_clock::time_point m_last_message_time{};
    std::chrono::steady_clock::time_point m_last_sendmessage_time{};
    std::chrono::steady_clock::time_point m_last_chance_time{};
    uint32_t m_frames_since_init{0};
    bool m_has_last_chance{true};
    bool m_first_initialize{true};

    bool m_sent_message{false};
    bool m_message_hook_requested{false};
    bool m_console_setup{false};

    RendererType m_renderer_type{RendererType::D3D11};

    template <typename T> 
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct D3D11 
    {
        ComPtr<ID3D11Texture2D> blank_rt{};
		ComPtr<ID3D11Texture2D> rt{};
        ComPtr<ID3D11RenderTargetView> blank_rt_rtv{};
		ComPtr<ID3D11RenderTargetView> rt_rtv{};
		ComPtr<ID3D11ShaderResourceView> rt_srv{};
        uint32_t rt_width{};
        uint32_t rt_height{};
		ComPtr<ID3D11RenderTargetView> bb_rtv{};
    } m_d3d11{};
};

#endif // DLL_INJECTOR_DARKEST1MODLOADER_HPP_
