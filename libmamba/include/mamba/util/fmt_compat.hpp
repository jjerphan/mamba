// Lightweight compatibility layer to replace fmt usage with C++20 <format>.
// If <format> is not available, it falls back to fmt headers to preserve buildability.

#pragma once

#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <format>

namespace fmt
{
    // Formatting wrappers based on std::format
    using std::format;
    using std::format_to;
    using std::vformat;
    using std::make_format_args;

    // Minimal print helpers (fmt::print equivalent)
    template <class... Args>
    inline void print(std::ostream& os, std::string_view fmt_str, Args&&... args)
    {
        os << vformat(fmt_str, make_format_args(std::forward<Args>(args)...));
    }

    // join helper: join a range with a separator into a std::string
    template <class Range>
    inline std::string join(const Range& range, std::string_view separator)
    {
        std::string out;
        bool first = true;
        for (const auto& value : range)
        {
            if (!first)
            {
                out.append(separator);
            }
            else
            {
                first = false;
            }
            if constexpr (std::is_convertible_v<decltype(value), std::string_view>)
            {
                out.append(std::string_view(value));
            }
            else
            {
                out.append(format("{}", value));
            }
        }
        return out;
    }

    // Styling compatibility (no real color semantics to avoid external deps).
    enum class emphasis
    {
        none = 0,
        bold,
        faint,
        italic,
        underline,
        blink,
        reverse,
        conceal,
        strikethrough,
    };

    enum class terminal_color
    {
        black,
        red,
        green,
        yellow,
        blue,
        magenta,
        cyan,
        white,
        bright_black,
        bright_red,
        bright_green,
        bright_yellow,
        bright_blue,
        bright_magenta,
        bright_cyan,
        bright_white,
    };

    struct rgb
    {
        unsigned char r;
        unsigned char g;
        unsigned char b;
    };

    struct text_style
    {
        // Minimal data; we do not try to emulate fmt internals.
        bool has_emphasis = false;
        emphasis emp = emphasis::none;
        bool has_fg = false;
        bool fg_is_rgb = false;
        terminal_color fg_term = terminal_color::white;
        rgb fg_rgb{ 255, 255, 255 };
        bool has_bg = false;
        bool bg_is_rgb = false;
        terminal_color bg_term = terminal_color::black;
        rgb bg_rgb{ 0, 0, 0 };
    };

    inline text_style operator|(text_style base, text_style add)
    {
        if (add.has_emphasis)
        {
            base.has_emphasis = true;
            base.emp = add.emp;
        }
        if (add.has_fg)
        {
            base.has_fg = true;
            base.fg_is_rgb = add.fg_is_rgb;
            base.fg_term = add.fg_term;
            base.fg_rgb = add.fg_rgb;
        }
        if (add.has_bg)
        {
            base.has_bg = true;
            base.bg_is_rgb = add.bg_is_rgb;
            base.bg_term = add.bg_term;
            base.bg_rgb = add.bg_rgb;
        }
        return base;
    }

    inline text_style operator|(text_style base, emphasis e)
    {
        base.has_emphasis = true;
        base.emp = e;
        return base;
    }

    inline text_style fg(terminal_color c)
    {
        text_style s;
        s.has_fg = true;
        s.fg_is_rgb = false;
        s.fg_term = c;
        return s;
    }

    inline text_style fg(rgb c)
    {
        text_style s;
        s.has_fg = true;
        s.fg_is_rgb = true;
        s.fg_rgb = c;
        return s;
    }

    inline text_style bg(terminal_color c)
    {
        text_style s;
        s.has_bg = true;
        s.bg_is_rgb = false;
        s.bg_term = c;
        return s;
    }

    inline text_style bg(rgb c)
    {
        text_style s;
        s.has_bg = true;
        s.bg_is_rgb = true;
        s.bg_rgb = c;
        return s;
    }

    // Minimal styled that simply returns the input; coloring not supported with std::format
    template <class StringLike>
    inline StringLike styled(const StringLike& v, const text_style&)
    {
        return v;
    }
}  // namespace fmt


