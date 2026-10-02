#ifndef DLL_INJECTOR_GL_VERSION_HPP_
#define DLL_INJECTOR_GL_VERSION_HPP_

#include <cstddef>
#include <string_view>

namespace gl 
{

struct Version 
{
    int major{0};
    int minor{0};
    friend constexpr bool operator==(const Version &, const Version &) = default;
};

namespace detail 
{

constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

constexpr int read_int(std::string_view text, std::size_t &pos) noexcept 
{
    int value = 0;
    while (pos < text.size() && is_digit(text[pos])) 
    {
        if (value < 10000) 
        { 
            value = value * 10 + (text[pos] - '0');
        }
        ++pos;
    }
    return value;
}

}  // namespace detail

// "2.1.2 NVIDIA 391.35" -> {2, 1}; "OpenGL ES 3.0" -> {3, 0}; мусор -> {0, 0}.
[[nodiscard]] constexpr Version parse_version(std::string_view text) noexcept 
{
    std::size_t pos = 0;
    while (pos < text.size() && !detail::is_digit(text[pos])) 
    {
        ++pos;
    }
    if (pos == text.size()) 
    {
        return {};
    }

    Version version{};
    version.major = detail::read_int(text, pos);
    if (pos < text.size() && text[pos] == '.') 
    {
        ++pos;
        version.minor = detail::read_int(text, pos);
    }
    return version;
}

// Строка для ImGui_ImplOpenGL3_Init. Бэкенд ImGui знает шейдеры для 120, 130 и 150+.
// GL 2.x -> GLSL 1.20, GL 3.0/3.1 -> 1.30, GL 3.2+ -> 1.50.
// Неизвестная версия ({0,0}) трактуется как самый консервативный вариант (120).
[[nodiscard]] constexpr std::string_view glsl_header_for(Version version) noexcept 
{
    if (version.major < 3) 
    {
        return "#version 120";
    }
    if (version.major == 3 && version.minor < 2) 
    {
        return "#version 130";
    }
    return "#version 150";
}

}  // namespace gl

#endif  // DLL_INJECTOR_GL_VERSION_HPP_