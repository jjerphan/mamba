// Copyright (c) 2019, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#ifndef MAMBA_FS_FILESYSTEM_HPP
#define MAMBA_FS_FILESYSTEM_HPP

#include <concepts>
#include <filesystem>
#include <string>
#include <string_view>

#include <fmt/format.h>

namespace mamba::fs
{
    using std::filesystem::file_status;
    using std::filesystem::file_time_type;
    using std::filesystem::perms;
    using std::filesystem::space_info;
    using std::filesystem::directory_iterator;
    using std::filesystem::directory_entry;
    using std::filesystem::copy_options;

    // sentinel argument for indicating the current time to last_write_time
    class now
    {
    };

    struct Utf8Options
    {
        bool normalize_sep = true;
    };

    // Maintain `\` on Windows, `/` on other platforms
    std::filesystem::path normalized_separators(std::filesystem::path path);

    // Returns a UTF-8 string given a standard path.
    std::string to_utf8(const std::filesystem::path& path, Utf8Options utf8_options = {});

    // Returns standard path given a UTF-8 string.
    std::filesystem::path from_utf8(std::string_view u8string);

    // Same as std::filesystem::path except we only accept and output UTF-8 paths
    class u8path
    {
    public:

        using value_type = char;
        using string_type = std::string;
        u8path() = default;
        u8path(const u8path&) = default;
        u8path(u8path&&) = default;
        u8path& operator=(const u8path&) = default;
        u8path& operator=(u8path&&) = default;

        u8path(const std::string& u8string)
            : m_path(u8string)
        {
        }

        u8path(std::string_view u8string)
            : m_path(u8string)
        {
        }

        u8path(const char* u8string)
            : m_path(u8string)
        {
        }

        u8path(const std::filesystem::path& p)
            : m_path(p)
        {
        }

        u8path& operator=(const std::string& u8string)
        {
            m_path = u8string;
            return *this;
        }

        u8path& operator=(std::string_view u8string)
        {
            m_path = u8string;
            return *this;
        }

        u8path& operator=(const char* u8string)
        {
            m_path = u8string;
            return *this;
        }

        u8path& operator=(const std::filesystem::path& p)
        {
            m_path = p;
            return *this;
        }

        u8path operator/(const u8path& p) const
        {
            return u8path(m_path / p.m_path);
        }

        u8path operator/(const std::filesystem::path& p) const
        {
            return u8path(m_path / p);
        }

        u8path operator/(const std::string& str) const
        {
            return u8path(m_path / str);
        }

        u8path operator/(std::string_view str) const
        {
            return u8path(m_path / str);
        }

        u8path operator/(const char* str) const
        {
            return u8path(m_path / str);
        }

        u8path& operator+=(const std::string& to_append)
        {
            m_path += to_append;
            return *this;
        }

        u8path& operator+=(std::string_view to_append)
        {
            m_path += to_append;
            return *this;
        }

        u8path& operator+=(const char* to_append)
        {
            m_path += to_append;
            return *this;
        }

        u8path& operator+=(char to_append)
        {
            m_path += to_append;
            m_path = normalized_separators(std::move(m_path));
            return *this;
        }

        bool empty() const noexcept
        {
            return m_path.empty();
        }

        static u8path empty_path()
        {
            return u8path();
        }

        const std::filesystem::path& path() const
        {
            return m_path;
        }

        std::filesystem::path& path()
        {
            return m_path;
        }

        std::string string() const
        {
            return m_path.string();
        }

        // Add wrappers for std::filesystem::path methods
        u8path stem() const
        {
            return u8path(m_path.stem());
        }

        u8path parent_path() const
        {
            return u8path(m_path.parent_path());
        }

        u8path root_name() const
        {
            return u8path(m_path.root_name());
        }

        u8path root_directory() const
        {
            return u8path(m_path.root_directory());
        }

        u8path root_path() const
        {
            return u8path(m_path.root_path());
        }

        u8path filename() const
        {
            return u8path(m_path.filename());
        }

        u8path extension() const
        {
            return u8path(m_path.extension());
        }

        u8path lexically_normal() const
        {
            return u8path(m_path.lexically_normal());
        }

        u8path lexically_relative(const u8path& base) const
        {
            return u8path(m_path.lexically_relative(base.m_path));
        }

        u8path lexically_proximate(const u8path& base) const
        {
            return u8path(m_path.lexically_proximate(base.m_path));
        }

        // Returns a default encoded string.
        decltype(auto) native() const
        {
            return m_path.native();
        }

        // Returns a UTF-8 string.
        operator std::string() const
        {
            return this->string();
        }

        // Returns the native wstring (UTF-16 on Windows).
        std::wstring wstring() const
        {
            return m_path.wstring();
        }

        // Implicitly convert to native wstring (UTF-16 on Windows).
        operator std::wstring() const
        {
            return this->wstring();
        }

        // Returns a UTF-8 string using the ``/`` on all systems.
        std::string generic_string() const
        {
            return to_utf8(m_path.generic_string(), { /*normalize_sep=*/false });
        }

        // Implicit conversion to standard path.
        operator std::filesystem::path() const
        {
            return m_path;
        }

        // Explicit conversion to standard path.
        const std::filesystem::path& std_path() const noexcept
        {
            return m_path;
        }

        //---- Modifiers ----

        void clear() noexcept
        {
            m_path.clear();
        }

        u8path& remove_filename()
        {
            m_path.remove_filename();
            return *this;
        }

        u8path& replace_filename(const u8path replacement)
        {
            m_path.replace_filename(replacement.m_path);
            return *this;
        }

        u8path& replace_extension(const u8path replacement = u8path())
        {
            m_path.replace_extension(replacement.m_path);
            return *this;
        }

        //---- Operators ----

        friend bool operator==(const u8path& left, const u8path& right) noexcept
        {
            return left.m_path == right.m_path;
        }

        friend bool operator!=(const u8path& left, const u8path& right) noexcept
        {
            return left.m_path != right.m_path;
        }

        friend bool operator<(const u8path& left, const u8path& right) noexcept
        {
            return left.m_path < right.m_path;
        }

        friend bool operator<=(const u8path& left, const u8path& right) noexcept
        {
            return left.m_path <= right.m_path;
        }

        friend bool operator>(const u8path& left, const u8path& right) noexcept
        {
            return left.m_path > right.m_path;
        }

        friend bool operator>=(const u8path& left, const u8path& right) noexcept
        {
            return left.m_path >= right.m_path;
        }

        //---- State ----

        bool is_absolute() const
        {
            return m_path.is_absolute();
        }

        bool is_relative() const
        {
            return m_path.is_relative();
        }

        bool has_root_path() const
        {
            return m_path.has_root_path();
        }

        bool has_root_name() const
        {
            return m_path.has_root_name();
        }

        bool has_root_directory() const
        {
            return m_path.has_root_directory();
        }

        bool has_relative_path() const
        {
            return m_path.has_relative_path();
        }

        bool has_parent_path() const
        {
            return m_path.has_parent_path();
        }

        bool has_filename() const
        {
            return m_path.has_filename();
        }

        bool has_stem() const
        {
            return m_path.has_stem();
        }

        bool has_extension() const
        {
            return m_path.has_extension();
        }

        //---- Utility ----

        // Writing to stream always using UTF-8.
        // Note: this will not work well on Windows with std::cout which doesnt know it's UTF-8
        //       In that case use `u8path::std_path()` instead.
        template <typename OutStream>
        friend OutStream& operator<<(OutStream& out, const u8path& path)
        {
            out << std::quoted(path.string());
            return out;
        }

        // Reads stream assuming UTF-8 encoding.
        template <typename InStream>
        friend InStream& operator>>(InStream& in, u8path& path)
        {
            std::string str;
            in >> std::quoted(str);
            path = str;
            return in;
        }

    private:

        std::filesystem::path m_path;
    };

    // Remove custom directory_entry class and use std::filesystem::directory_entry directly
    using directory_entry = std::filesystem::directory_entry;

    //---- Filesystem Operations ----

    // path absolute(const path& p);
    // path absolute(const path& p, error_code& ec);
    template <typename... OtherArgs>
    u8path absolute(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::absolute(path, std::forward<OtherArgs>(args)...);
    }

    // path canonical(const path& p);
    // path canonical(const path& p, error_code& ec);
    template <typename... OtherArgs>
    u8path canonical(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::canonical(path, std::forward<OtherArgs>(args)...);
    }

    // void copy(const path& from, const path& to);
    // void copy(const path& from, const path& to, error_code& ec);
    // void copy(const path& from, const path& to, copy_options options);
    // void copy(const path& from, const path& to, copy_options options, error_code& ec);
    template <typename... OtherArgs>
    void copy(const u8path& from, const u8path& to, OtherArgs&&... args)
    {
        std::filesystem::copy(from, to, std::forward<OtherArgs>(args)...);
    }

    // bool copy_file(const path& from, const path& to);
    // bool copy_file(const path& from, const path& to, error_code& ec);
    // bool copy_file(const path& from, const path& to, copy_options option);
    // bool copy_file(const path& from, const path& to, copy_options option, error_code& ec);
    template <typename... OtherArgs>
    bool copy_file(const u8path& from, const u8path& to, OtherArgs&&... args)
    {
        return std::filesystem::copy_file(from, to, std::forward<OtherArgs>(args)...);
    }

    // void copy_symlink(const path& existing_symlink, const path& new_symlink);
    // void copy_symlink(const path& existing_symlink,
    //                   const path& new_symlink,
    //                   error_code& ec) noexcept;
    template <typename... OtherArgs>
    void copy_symlink(const u8path& existing_symlink, const u8path& new_symlink, OtherArgs&&... args)
    {
        std::filesystem::copy_symlink(existing_symlink, new_symlink, std::forward<OtherArgs>(args)...);
    }

    // bool create_directories(const path& p);
    // bool create_directories(const path& p, error_code& ec);
    template <typename... OtherArgs>
    bool create_directories(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::create_directories(path, std::forward<OtherArgs>(args)...);
    }

    // bool create_directory(const path& p);
    // bool create_directory(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    bool create_directory(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::create_directory(path, std::forward<OtherArgs>(args)...);
    }

    // bool create_directory(const path& p, const path& attributes);
    // bool create_directory(const path& p, const path& attributes, error_code& ec) noexcept;
    template <typename... OtherArgs>
    bool create_directory(const u8path& path, const u8path& attributes, OtherArgs&&... args)
    {
        return std::filesystem::create_directory(path, attributes, std::forward<OtherArgs>(args)...);
    }

    // void create_directory_symlink(const path& to, const path& new_symlink);
    // void create_directory_symlink(const path& to, const path& new_symlink, error_code& ec)
    // noexcept;
    template <typename... OtherArgs>
    void create_directory_symlink(const u8path& to, const u8path& new_symlink, OtherArgs&&... args)
    {
        std::filesystem::create_directory_symlink(to, new_symlink, std::forward<OtherArgs>(args)...);
    }

    // void create_hard_link(const path& to, const path& new_hard_link);
    // void create_hard_link(const path& to, const path& new_hard_link, error_code& ec) noexcept;
    template <typename... OtherArgs>
    void create_hard_link(const u8path& to, const u8path& new_hard_link, OtherArgs&&... args)
    {
        std::filesystem::create_hard_link(to, new_hard_link, std::forward<OtherArgs>(args)...);
    }

    // void create_symlink(const path& to, const path& new_symlink);
    // void create_symlink(const path& to, const path& new_symlink, error_code& ec) noexcept;
    template <typename... OtherArgs>
    void create_symlink(const u8path& to, const u8path& new_symlink, OtherArgs&&... args)
    {
        std::filesystem::create_symlink(to, new_symlink, std::forward<OtherArgs>(args)...);
    }

    // path current_path();
    inline u8path current_path()
    {
        return u8path(std::filesystem::current_path());
    }

    // path current_path(error_code& ec);
    inline u8path current_path(std::error_code& ec)
    {
        return u8path(std::filesystem::current_path(ec));
    }

    // void current_path(const path& p);
    // void current_path(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    void current_path(const u8path& path, OtherArgs&&... args)
    {
        std::filesystem::current_path(path, std::forward<OtherArgs>(args)...);
    }

    // bool equivalent(const path& p1, const path& p2);
    // bool equivalent(const path& p1, const path& p2, error_code& ec) noexcept;
    template <typename... OtherArgs>
    bool equivalent(const u8path& p1, const u8path& p2, OtherArgs&&... args)
    {
        return std::filesystem::equivalent(p1, p2, std::forward<OtherArgs>(args)...);
    }

    // bool exists(file_status s) noexcept;
    inline bool exists(file_status s) noexcept
    {
        return std::filesystem::exists(s);
    }

    // bool exists(const path& p);
    // bool exists(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    bool exists(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::exists(path.path(), std::forward<OtherArgs>(args)...);
    }

    // uintmax_t file_size(const path& p);
    // uintmax_t file_size(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    uintmax_t file_size(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::file_size(path, std::forward<OtherArgs>(args)...);
    }

    // uintmax_t hard_link_count(const path& p);
    // uintmax_t hard_link_count(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    uintmax_t hard_link_count(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::hard_link_count(path, std::forward<OtherArgs>(args)...);
    }

    // bool is_block_file(file_status s) noexcept;
    inline bool is_block_file(file_status s) noexcept
    {
        return std::filesystem::is_block_file(s);
    }

    // bool is_block_file(const path& p);
    // bool is_block_file(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    bool is_block_file(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::is_block_file(path.path(), std::forward<OtherArgs>(args)...);
    }

    // bool is_character_file(file_status s) noexcept;
    inline bool is_character_file(file_status s) noexcept
    {
        return std::filesystem::is_character_file(s);
    }

    // bool is_character_file(const path& p);
    // bool is_character_file(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    bool is_character_file(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::is_character_file(path.path(), std::forward<OtherArgs>(args)...);
    }

    // bool is_directory(file_status s) noexcept;
    inline bool is_directory(file_status s) noexcept
    {
        return std::filesystem::is_directory(s);
    }

    // bool is_directory(const path& p);
    // bool is_directory(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    bool is_directory(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::is_directory(path.path(), std::forward<OtherArgs>(args)...);
    }

    // bool is_empty(const path& p);
    // bool is_empty(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    bool is_empty(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::is_empty(path, std::forward<OtherArgs>(args)...);
    }

    // bool is_fifo(file_status s) noexcept;
    inline bool is_fifo(file_status s) noexcept
    {
        return std::filesystem::is_fifo(s);
    }

    // bool is_fifo(const path& p);
    // bool is_fifo(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    bool is_fifo(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::is_fifo(path.path(), std::forward<OtherArgs>(args)...);
    }

    // bool is_other(file_status s) noexcept;
    inline bool is_other(file_status s) noexcept
    {
        return std::filesystem::is_other(s);
    }

    // bool is_other(const path& p);
    // bool is_other(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    bool is_other(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::is_other(path.path(), std::forward<OtherArgs>(args)...);
    }

    // bool is_regular_file(file_status s) noexcept;
    inline bool is_regular_file(file_status s) noexcept
    {
        return std::filesystem::is_regular_file(s);
    }

    // bool is_regular_file(const path& p);
    // bool is_regular_file(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    bool is_regular_file(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::is_regular_file(path.path(), std::forward<OtherArgs>(args)...);
    }

    // bool is_socket(file_status s) noexcept;
    inline bool is_socket(file_status s) noexcept
    {
        return std::filesystem::is_socket(s);
    }

    // bool is_socket(const path& p);
    // bool is_socket(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    bool is_socket(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::is_socket(path.path(), std::forward<OtherArgs>(args)...);
    }

    // bool is_symlink(file_status s) noexcept;
    inline bool is_symlink(file_status s) noexcept
    {
        return std::filesystem::is_symlink(s);
    }

    // bool is_symlink(const path& p);
    // bool is_symlink(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    bool is_symlink(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::is_symlink(path.path(), std::forward<OtherArgs>(args)...);
    }

    // file_time_type last_write_time(const path& p);
    // file_time_type last_write_time(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    file_time_type last_write_time(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::last_write_time(path.path(), std::forward<OtherArgs>(args)...);
    }

    // Overload for last_write_time with fs::now sentinel
    void last_write_time(const u8path& path, now, std::error_code& ec) noexcept;

    // void last_write_time(const path& p, file_time_type new_time);
    // void last_write_time(const path& p, file_time_type new_time, error_code& ec) noexcept;
    template <typename... OtherArgs>
    void last_write_time(const u8path& path, file_time_type new_time, OtherArgs&&... args)
    {
        std::filesystem::last_write_time(path.path(), new_time, std::forward<OtherArgs>(args)...);
    }

    // void permissions(const path& p, perms prms, perm_options opts = perm_options::replace);
    // void permissions(const path& p, perms prms, error_code& ec) noexcept;
    // void permissions(const path& p, perms prms, perm_options opts, error_code& ec) noexcept;
    template <typename... OtherArgs>
    void permissions(const u8path& path, perms prms, OtherArgs&&... args)
    {
        std::filesystem::permissions(path.path(), prms, std::forward<OtherArgs>(args)...);
    }

    // path proximate(const path& p, error_code& ec);
    // path proximate(const path& p, const path& base = current_path());
    template <typename... OtherArgs>
    u8path proximate(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::proximate(path, std::forward<OtherArgs>(args)...);
    }

    // path proximate(const path& p, const path& base, error_code& ec);
    template <typename... OtherArgs>
    u8path proximate(const u8path& path, const u8path& base, OtherArgs&&... args)
    {
        return std::filesystem::proximate(path, base, std::forward<OtherArgs>(args)...);
    }

    // path read_symlink(const path& p);
    // path read_symlink(const path& p, error_code& ec);
    template <typename... OtherArgs>
    u8path read_symlink(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::read_symlink(path, std::forward<OtherArgs>(args)...);
    }

    // path relative(const path& p, error_code& ec);
    // path relative(const path& p, const path& base = current_path());
    template <typename... OtherArgs>
    u8path relative(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::relative(path, std::forward<OtherArgs>(args)...);
    }

    // path relative(const path& p, const path& base, error_code& ec);
    template <typename... OtherArgs>
    u8path relative(const u8path& path, const u8path& base, OtherArgs&&... args)
    {
        return std::filesystem::relative(path, base, std::forward<OtherArgs>(args)...);
    }

    // bool remove(const path& p);
    // bool remove(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    bool remove(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::remove(path, std::forward<OtherArgs>(args)...);
    }

    // uintmax_t remove_all(const path& p);
    // uintmax_t remove_all(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    uintmax_t remove_all(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::remove_all(path, std::forward<OtherArgs>(args)...);
    }

    // void rename(const path& from, const path& to);
    // void rename(const path& from, const path& to, error_code& ec) noexcept;
    template <typename... OtherArgs>
    void rename(const u8path& from, const u8path& to, OtherArgs&&... args)
    {
        std::filesystem::rename(from, to, std::forward<OtherArgs>(args)...);
    }

    // void resize_file(const path& p, uintmax_t size);
    // void resize_file(const path& p, uintmax_t size, error_code& ec) noexcept;
    template <typename... OtherArgs>
    void resize_file(const u8path& path, OtherArgs&&... args)
    {
        std::filesystem::resize_file(path, std::forward<OtherArgs>(args)...);
    }

    // space_info space(const path& p);
    // space_info space(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    space_info space(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::space(path.path(), std::forward<OtherArgs>(args)...);
    }

    // file_status status(const path& p);
    // file_status status(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    file_status status(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::status(path.path(), std::forward<OtherArgs>(args)...);
    }

    // bool status_known(file_status s) noexcept;
    inline bool status_known(file_status s) noexcept
    {
        return std::filesystem::status_known(s);
    }

    // file_status symlink_status(const path& p);
    // file_status symlink_status(const path& p, error_code& ec) noexcept;
    template <typename... OtherArgs>
    file_status symlink_status(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::symlink_status(path.path(), std::forward<OtherArgs>(args)...);
    }

    // path temp_directory_path();
    // path temp_directory_path(error_code& ec);
    template <typename... OtherArgs>
    u8path temp_directory_path(OtherArgs&&... args)
    {
        return std::filesystem::temp_directory_path(std::forward<OtherArgs>(args)...);
    }

    // path weakly_canonical(const path& p);
    // path weakly_canonical(const path& p, error_code& ec);
    template <typename... OtherArgs>
    u8path weakly_canonical(const u8path& path, OtherArgs&&... args)
    {
        return std::filesystem::weakly_canonical(path, std::forward<OtherArgs>(args)...);
    }

}  // namespace mamba::fs

namespace fmt
{
    template <>
    struct formatter<mamba::fs::u8path> : formatter<std::string>
    {
        template <typename FormatContext>
        auto format(const mamba::fs::u8path& p, FormatContext& ctx) const
        {
            return formatter<std::string>::format(p.string(), ctx);
        }
    };
}

namespace std
{
    template <>
    struct hash<mamba::fs::u8path>
    {
        std::size_t operator()(const mamba::fs::u8path& p) const noexcept
        {
            return std::hash<std::string>{}(p.string());
        }
    };
}

#endif
