// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <filesystem>
#include <optional>
#include <string_view>
namespace roots::gui::ui::theme {
enum class Appearance { Light, Dark, System };
std::string_view AppearanceId(Appearance);
std::optional<Appearance> ParseAppearance(std::string_view);
bool IsDark(Appearance, bool system_dark);
// A small public preference, independent of wallet metadata. Missing defaults Light;
// malformed/unreadable files remain an observable failure, never silently overwritten.
struct AppearanceLoad { Appearance choice{Appearance::Light}; bool valid{true}; };
AppearanceLoad LoadAppearance(const std::filesystem::path&);
bool SaveAppearance(const std::filesystem::path&, Appearance);

inline constexpr int MIN_INTERFACE_SIZE = 50;
inline constexpr int DEFAULT_INTERFACE_SIZE = 100;
inline constexpr int MAX_INTERFACE_SIZE = 200;
struct InterfaceSizeLoad { int percent{}; bool valid{true}; }; // Zero means no saved size.
InterfaceSizeLoad LoadInterfaceSize(const std::filesystem::path&);
bool SaveInterfaceSize(const std::filesystem::path&, int percent);
}
