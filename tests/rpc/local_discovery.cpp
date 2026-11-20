// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/rpc/transport.h"
#include <charconv>
#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const std::string_view arg(argv[1]);
    uint16_t port{};
    const auto parsed = std::from_chars(arg.data(), arg.data() + arg.size(), port);
    if (parsed.ec != std::errc{} || parsed.ptr != arg.data() + arg.size()) return 2;
    const auto profile = roots::gui::rpc::FindLocalRpcProfile(port);
    if (!profile) return 1;
    if (profile->host != "127.0.0.1" || profile->port != port ||
        profile->proxy != roots::gui::ProxyKind::Direct || profile->tls ||
        profile->credential.kind != roots::gui::Authentication::Cookie) return 2;
    std::cout << profile->credential.reference << '\n';
}
