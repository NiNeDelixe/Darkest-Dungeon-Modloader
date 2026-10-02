#ifndef HOOCKS_GLHOOK_HPP_
#define HOOCKS_GLHOOK_HPP_

#include <Windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>

#include <MinHook.h>
#include <spdlog/spdlog.h>

// Функции opengl32, нужные хуку. Резолвятся динамически из ОРИГИНАЛЬНОГО opengl32.dll:
// наш прокси тоже называется OPENGL32.dll, поэтому линковаться с opengl32.lib нельзя
// (импорт разрешился бы в нас самих).
struct GLApi 
{
    using GetCurrentDcFn = HDC(WINAPI *)();
    using GetCurrentContextFn = HGLRC(WINAPI *)();
    using MakeCurrentFn = BOOL(WINAPI *)(HDC, HGLRC);
    using GetStringFn = const unsigned char *(WINAPI *)(unsigned int);

    GetCurrentDcFn wgl_get_current_dc{nullptr};
    GetCurrentContextFn wgl_get_current_context{nullptr};
    MakeCurrentFn wgl_make_current{nullptr};
    GetStringFn gl_get_string{nullptr};

    [[nodiscard]] bool valid() const noexcept 
    {
        return wgl_get_current_dc != nullptr && 
            wgl_get_current_context != nullptr && 
            wgl_make_current != nullptr &&
            gl_get_string != nullptr;
    }
};

// Хук точек swap-буферов OpenGL.
//
// Порядок MinHook: MH_Initialize (делает владелец) -> MH_CreateHook -> MH_EnableHook ->
// MH_DisableHook -> MH_RemoveHook -> MH_Uninitialize (делает владелец).
//
// Хукаются (если найдены):
//   1. SDL2.dll!SDL_GL_SwapWindow  - основной путь DD1 (SDL зовёт gdi32!SwapBuffers, минуя wglSwapBuffers)
//   2. gdi32!SwapBuffers           - страховка для движков без SDL
//   3. OPENGL32!wglSwapBuffers     - страховка для прямых вызовов
// Вложенные вызовы (1 -> 2 -> 3) отсекаются thread_local-флагом: оверлей рисуется один раз.
class GLHook 
{
public:
    using Callback = std::function<void(GLHook &)>;

    GLHook() = default;
    ~GLHook();

    GLHook(const GLHook &) = delete;
    GLHook &operator=(const GLHook &) = delete;
    GLHook(GLHook &&) = delete;
    GLHook &operator=(GLHook &&) = delete;

    [[nodiscard]] static std::recursive_mutex &swap_mutex();

    bool hook();
    bool unhook();

    [[nodiscard]] bool is_hooked() const noexcept { return m_hooked; }
    [[nodiscard]] bool is_inside_swap() const noexcept { return m_inside_swap; }

    void on_present(Callback callback) { m_on_present = std::move(callback); }
    void on_post_present(Callback callback) { m_on_post_present = std::move(callback); }
    // Вызывается ПОСЛЕ смены контекста, но ДО on_present. Новый контекст уже current.
    void on_context_changed(Callback callback) { m_on_context_changed = std::move(callback); }

    [[nodiscard]] HDC get_dc() const noexcept { return m_dc; }
    [[nodiscard]] HGLRC get_glrc() const noexcept { return m_glrc; }
    [[nodiscard]] HGLRC get_previous_glrc() const noexcept { return m_previous_glrc; }
    [[nodiscard]] HWND get_window() const noexcept { return m_window; }
    [[nodiscard]] const GLApi &api() const noexcept { return m_api; }

    // Только на render-потоке с current-контекстом.
    [[nodiscard]] std::string query_version_string() const;
    [[nodiscard]] HDC query_current_dc() const noexcept;

    // Внутренний API детуров (не бросают исключений наружу, в игру).
    void begin_swap(HDC hdc) noexcept;
    void end_swap() noexcept;

private:
    bool load_api();
    void resolve_targets();

    GLApi m_api{};
    HMODULE m_opengl32{nullptr};

    std::atomic<bool> m_hooked{false};
    std::atomic<bool> m_inside_swap{false};

    HDC m_dc{nullptr};
    HGLRC m_glrc{nullptr};
    HGLRC m_previous_glrc{nullptr};
    HWND m_window{nullptr};

    Callback m_on_present{};
    Callback m_on_post_present{};
    Callback m_on_context_changed{};
};

// RAII: временно делает указанный GL-контекст current и возвращает прежний.
// Нужен, чтобы удалить GL-объекты ImGui в том контексте, где они были созданы.
class ScopedGLContext 
{
public:
    ScopedGLContext(const GLApi &api, HDC dc, HGLRC glrc);
    ~ScopedGLContext();

    ScopedGLContext(const ScopedGLContext &) = delete;
    ScopedGLContext &operator=(const ScopedGLContext &) = delete;

    [[nodiscard]] bool valid() const noexcept { return m_bound; }

private:
    const GLApi &m_api;
    HDC m_previous_dc{nullptr};
    HGLRC m_previous_glrc{nullptr};
    bool m_bound{false};
};

#endif // HOOCKS_GLHOOK_HPP_
