#pragma once

#include <initializer_list>
#include <span>
#include <string_view>

namespace mcfix {

enum class LoaderMode {
    invalid,
    inspect,
    launch,
};

struct LoaderDecision {
    LoaderMode mode{LoaderMode::invalid};
    std::string_view reason{"expected exactly one of --inspect or --launch"};
};

LoaderDecision parse_loader_mode(std::span<const std::string_view> arguments);

inline LoaderDecision parse_loader_mode(std::initializer_list<std::string_view> arguments) {
    return parse_loader_mode(std::span<const std::string_view>(arguments.begin(), arguments.size()));
}

}  // namespace mcfix
