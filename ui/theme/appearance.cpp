// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/theme/appearance.h"
#include <charconv>
#include <fstream>
#include <system_error>
namespace roots::gui::ui::theme {
std::string_view AppearanceId(Appearance a)
{
    switch (a) { case Appearance::Light: return "light"; case Appearance::Dark: return "dark"; case Appearance::System: return "system"; }
    return {};
}
std::optional<Appearance> ParseAppearance(std::string_view value)
{
    if (value == "light") return Appearance::Light;
    if (value == "dark") return Appearance::Dark;
    if (value == "system") return Appearance::System;
    return {};
}
bool IsDark(Appearance choice, bool system_dark) { return choice == Appearance::Dark || (choice == Appearance::System && system_dark); }
namespace {
template <typename Result, typename Parse>
Result LoadPreference(const std::filesystem::path& path, Parse parse)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) && !ec) return {};
    if (ec || !std::filesystem::is_regular_file(path, ec) || ec || std::filesystem::file_size(path, ec) > 16 || ec)
        return {.valid = false};
    std::ifstream file(path, std::ios::binary);
    std::string value{std::istreambuf_iterator<char>(file), {}};
    if (!file || file.bad()) return {.valid = false};
    if (!value.empty() && value.back() == '\n') value.pop_back();
    if (const auto parsed = parse(value)) return {*parsed, true};
    return {.valid = false};
}
bool SavePreference(const std::filesystem::path& path, std::string_view value)
{
    if (value.empty() || path.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
    // One UI-thread writer. A failed write/rename preserves the existing file.
    auto pending = path; pending += ".pending";
    if (std::filesystem::is_symlink(pending, ec)) return false;
    std::ofstream file(pending, std::ios::binary | std::ios::trunc);
    file << value << '\n'; file.close();
    if (!file) return false;
    std::filesystem::rename(pending, path, ec);
    if (ec) { std::filesystem::remove(pending, ec); return false; }
    return true;
}
}

AppearanceLoad LoadAppearance(const std::filesystem::path& path)
{
    return LoadPreference<AppearanceLoad>(path, ParseAppearance);
}

bool SaveAppearance(const std::filesystem::path& path, Appearance choice)
{
    return SavePreference(path, AppearanceId(choice));
}

InterfaceSizeLoad LoadInterfaceSize(const std::filesystem::path& path)
{
    return LoadPreference<InterfaceSizeLoad>(path, [](std::string_view value) -> std::optional<int> {
        int percent{};
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), percent);
        if (error != std::errc{} || end != value.data() + value.size()
            || percent < MIN_INTERFACE_SIZE || percent > MAX_INTERFACE_SIZE) return {};
        return percent;
    });
}

bool SaveInterfaceSize(const std::filesystem::path& path, int percent)
{
    return percent >= MIN_INTERFACE_SIZE && percent <= MAX_INTERFACE_SIZE
        && SavePreference(path, std::to_string(percent));
}
}
