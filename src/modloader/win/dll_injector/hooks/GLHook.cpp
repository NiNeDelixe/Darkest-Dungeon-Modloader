#include "GLHook.hpp"

namespace 
{

constexpr std::size_t kSdlSwapWindow = 0;
constexpr std::size_t kGdiSwapBuffers = 1;
constexpr std::size_t kWglSwapBuffers = 2;
constexpr std::size_t kTargetCount = 3;

constexpr unsigned int kGlVersion = 0x1F02;  // GL_VERSION

struct Target 
{
    const char *name{nullptr};
    void *address{nullptr};     // Настоящая функция.
    void *trampoline{nullptr};  // Оригинал после MH_CreateHook.
    bool created{false};
    bool enabled{false};
};

// Состояние живёт на уровне модуля, а не объекта: детур может сработать, пока GLHook уже удаляется.
std::array<Target, kTargetCount> g_targets{};
std::atomic<GLHook *> g_instance{nullptr};
thread_local bool t_inside_swap{false};

template <typename Fn>
Fn load_fn(HMODULE module, const char *name) noexcept 
{
    // Двойной каст через void*: FARPROC -> Fn напрямую даёт -Wcast-function-type.
    return reinterpret_cast<Fn>(reinterpret_cast<void *>(GetProcAddress(module, name)));
}

void *find_export(const wchar_t *module_name, const char *proc) noexcept 
{
    // GetModuleHandle, а не LoadLibrary: не форсируем загрузку чужих DLL.
    const HMODULE module = GetModuleHandleW(module_name);
    return module != nullptr ? reinterpret_cast<void *>(GetProcAddress(module, proc)) : nullptr;
}

// Если хук уже снят, trampoline == nullptr, а address снова указывает на живую функцию.
template <typename Fn>
Fn original_of(std::size_t index) noexcept 
{
    const Target &target = g_targets[index];
    return reinterpret_cast<Fn>(target.trampoline != nullptr ? target.trampoline : target.address);
}

bool install_target(std::size_t index, void *detour) 
{
    Target &target = g_targets[index];
    if (target.address == nullptr) 
    {
        spdlog::warn("[GLHook] {} not found, skipping", target.name);
        return false;
    }

    MH_STATUS status = MH_CreateHook(target.address, detour, &target.trampoline);
    if (status != MH_OK) 
    {
        spdlog::error("[GLHook] MH_CreateHook({}) failed: {}", target.name, MH_StatusToString(status));
        target.trampoline = nullptr;
        return false;
    }
    target.created = true;

    status = MH_EnableHook(target.address);
    if (status != MH_OK) 
    {
        spdlog::error("[GLHook] MH_EnableHook({}) failed: {}", target.name, MH_StatusToString(status));
        MH_RemoveHook(target.address);
        target.created = false;
        target.trampoline = nullptr;
        return false;
    }
    target.enabled = true;

    spdlog::info("Hook installed at 0x{:X} ({})", reinterpret_cast<std::uintptr_t>(target.address), target.name);
    return true;
}

// Оверлей рисуется только во внешнем (самом первом) вызове swap на этом потоке.
class SwapScope 
{
public:
    SwapScope(GLHook *hook, HDC hdc) noexcept 
    {
        if (hook == nullptr || hdc == nullptr || t_inside_swap) 
        {
            return;
        }
        t_inside_swap = true;
        m_hook = hook;
        m_hook->begin_swap(hdc);
    }

    ~SwapScope() 
    {
        if (m_hook != nullptr) 
        {
            t_inside_swap = false;
            m_hook->end_swap();
        }
    }

    SwapScope(const SwapScope &) = delete;
    SwapScope &operator=(const SwapScope &) = delete;

private:
    GLHook *m_hook{nullptr};
};

// void SDL_GL_SwapWindow(SDL_Window *window); SDLCALL == __cdecl.
void __cdecl detour_sdl_gl_swap_window(void *window) 
{
    std::scoped_lock lock{GLHook::swap_mutex()};
    const auto original = original_of<void(__cdecl *)(void *)>(kSdlSwapWindow);

    GLHook *hook = g_instance.load(std::memory_order_acquire);
    // SDL не передаёт HDC, берём его у текущего контекста.
    const SwapScope scope{hook, hook != nullptr ? hook->query_current_dc() : nullptr};
    original(window);
}

// BOOL SwapBuffers(HDC hdc); (gdi32)
BOOL WINAPI detour_gdi_swap_buffers(HDC hdc) 
{
    std::scoped_lock lock{GLHook::swap_mutex()};
    const auto original = original_of<BOOL(WINAPI *)(HDC)>(kGdiSwapBuffers);

    const SwapScope scope{g_instance.load(std::memory_order_acquire), hdc};
    return original(hdc);
}

// BOOL wglSwapBuffers(HDC hdc); (opengl32)
BOOL WINAPI detour_wgl_swap_buffers(HDC hdc) 
{
    std::scoped_lock lock{GLHook::swap_mutex()};
    const auto original = original_of<BOOL(WINAPI *)(HDC)>(kWglSwapBuffers);

    const SwapScope scope{g_instance.load(std::memory_order_acquire), hdc};
    return original(hdc);
}

}  // namespace

// ---------------------------------------------------------------------------

std::recursive_mutex &GLHook::swap_mutex() 
{
    // Намеренно не освобождается: детуры могут сработать при завершении процесса.
    static auto *mutex = new std::recursive_mutex{};
    return *mutex;
}

GLHook::~GLHook() 
{ 
    unhook(); 
}

bool GLHook::load_api() 
{
    if (m_api.valid()) 
    {
        return true;
    }

    // GetModuleHandle("opengl32.dll") вернул бы НАС (прокси с тем же именем),
    // поэтому грузим оригинал по полному пути.
    wchar_t system_dir[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(system_dir, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) 
    {
        spdlog::error("[GLHook] GetSystemDirectoryW failed: {}", GetLastError());
        return false;
    }

    const std::wstring path = std::wstring{system_dir} + L"\\OPENGL32.dll";
    m_opengl32 = LoadLibraryW(path.c_str());
    if (m_opengl32 == nullptr) 
    {
        spdlog::error("[GLHook] Cannot load original OPENGL32.dll: {}", GetLastError());
        return false;
    }

    m_api.wgl_get_current_dc = load_fn<GLApi::GetCurrentDcFn>(m_opengl32, "wglGetCurrentDC");
    m_api.wgl_get_current_context = load_fn<GLApi::GetCurrentContextFn>(m_opengl32, "wglGetCurrentContext");
    m_api.wgl_make_current = load_fn<GLApi::MakeCurrentFn>(m_opengl32, "wglMakeCurrent");
    m_api.gl_get_string = load_fn<GLApi::GetStringFn>(m_opengl32, "glGetString");

    if (!m_api.valid()) 
    {
        spdlog::error("[GLHook] Required OpenGL exports are missing");
        m_api = {};
        return false;
    }
    return true;
}

void GLHook::resolve_targets() 
{
    g_targets = {};

    g_targets[kSdlSwapWindow] = Target{"SDL_GL_SwapWindow", find_export(L"SDL2.dll", "SDL_GL_SwapWindow")};
    g_targets[kGdiSwapBuffers] = Target{"gdi32!SwapBuffers", find_export(L"gdi32.dll", "SwapBuffers")};
    g_targets[kWglSwapBuffers] = Target{
        "wglSwapBuffers", m_opengl32 != nullptr ? reinterpret_cast<void *>(GetProcAddress(m_opengl32, "wglSwapBuffers")) : nullptr};
}

bool GLHook::hook() 
{
    std::scoped_lock lock{swap_mutex()};

    if (m_hooked) 
    {
        return true;
    }

    GLHook *expected = nullptr;
    if (!g_instance.compare_exchange_strong(expected, this)) 
    {
        spdlog::error("[GLHook] Another GLHook instance is already active");
        return false;
    }

    if (!load_api()) 
    {
        g_instance.store(nullptr);
        return false;
    }

    resolve_targets();

    const std::array<void *, kTargetCount> detours{
        reinterpret_cast<void *>(&detour_sdl_gl_swap_window),
        reinterpret_cast<void *>(&detour_gdi_swap_buffers),
        reinterpret_cast<void *>(&detour_wgl_swap_buffers),
    };

    std::size_t installed = 0;
    for (std::size_t i = 0; i < kTargetCount; ++i) 
    {
        if (install_target(i, detours[i])) 
        {
            ++installed;
        }
    }

    if (installed == 0) 
    {
        spdlog::error("[GLHook] No swap function could be hooked");
        g_instance.store(nullptr);
        return false;
    }

    m_hooked = true;
    spdlog::info("[GLHook] {} of {} swap hooks installed", installed, kTargetCount);
    return true;
}

bool GLHook::unhook() 
{
    std::scoped_lock lock{swap_mutex()};

    if (!m_hooked) 
    {
        GLHook *self = this;
        g_instance.compare_exchange_strong(self, nullptr);
        return true;
    }

    bool ok = true;
    for (auto &target : g_targets) 
    {
        if (target.enabled) 
        {
            const MH_STATUS status = MH_DisableHook(target.address);
            if (status != MH_OK) 
            {
                spdlog::warn("[GLHook] MH_DisableHook({}) failed: {}", target.name, MH_StatusToString(status));
                ok = false;
            }
            target.enabled = false;
        }
        if (target.created) 
        {
            const MH_STATUS status = MH_RemoveHook(target.address);
            if (status != MH_OK) 
            {
                spdlog::warn("[GLHook] MH_RemoveHook({}) failed: {}", target.name, MH_StatusToString(status));
                ok = false;
            }
            target.created = false;
        }
        target.trampoline = nullptr;  // С этого момента детуры зовут target.address напрямую.
    }

    g_instance.store(nullptr);
    m_hooked = false;
    spdlog::info("[GLHook] Hooks removed{}", ok ? "" : " (with errors)");
    return ok;
}

HDC GLHook::query_current_dc() const noexcept {
    return m_api.wgl_get_current_dc != nullptr ? m_api.wgl_get_current_dc() : nullptr;
}

std::string GLHook::query_version_string() const 
{
    if (m_api.gl_get_string == nullptr) 
    {
        return {};
    }
    const unsigned char *text = m_api.gl_get_string(kGlVersion);
    return text != nullptr ? std::string{reinterpret_cast<const char *>(text)} : std::string{};
}

void GLHook::begin_swap(HDC hdc) noexcept 
{
    m_inside_swap = true;

    try 
    {
        const HGLRC glrc = m_api.wgl_get_current_context != nullptr ? m_api.wgl_get_current_context() : nullptr;
        if (glrc == nullptr) 
        {
            return;  // Не GL-кадр (или контекст не привязан): оверлей не рисуем.
        }

        m_dc = hdc;
        m_window = WindowFromDC(hdc);

        if (glrc != m_glrc) 
        {
            m_previous_glrc = m_glrc;
            m_glrc = glrc;
            if (m_previous_glrc != nullptr && m_on_context_changed) 
            {
                m_on_context_changed(*this);
            }
        }

        if (m_on_present) 
        {
            m_on_present(*this);
        }
    } 
    catch (const std::exception &e)
    {
        spdlog::error("[GLHook] Exception in on_present: {}", e.what());
    } 
    catch (...) 
    {
        spdlog::error("[GLHook] Unknown exception in on_present");
    }
}

void GLHook::end_swap() noexcept 
{
    m_inside_swap = false;

    try 
    {
        if (m_on_post_present) 
        {
            m_on_post_present(*this);
        }
    } 
    catch (const std::exception &e) 
    {
        spdlog::error("[GLHook] Exception in on_post_present: {}", e.what());
    } 
    catch (...) 
    {
        spdlog::error("[GLHook] Unknown exception in on_post_present");
    }
}

// ---------------------------------------------------------------------------

ScopedGLContext::ScopedGLContext(const GLApi &api, HDC dc, HGLRC glrc)
    : m_api{api},
        m_previous_dc{api.wgl_get_current_dc != nullptr ? api.wgl_get_current_dc() : nullptr},
        m_previous_glrc{api.wgl_get_current_context != nullptr ? api.wgl_get_current_context() : nullptr} 
{
    m_bound = glrc != nullptr && dc != nullptr && api.wgl_make_current != nullptr && api.wgl_make_current(dc, glrc) != FALSE;
}

ScopedGLContext::~ScopedGLContext() 
{
    if (m_api.wgl_make_current != nullptr) 
    {
        m_api.wgl_make_current(m_previous_dc, m_previous_glrc);
    }
}