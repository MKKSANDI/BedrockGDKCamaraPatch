#include "loader_args.hpp"

namespace mcfix {

LoaderDecision parse_loader_mode(std::span<const std::string_view> arguments) {
    if (arguments.size() != 1) {
        return {};
    }
    if (arguments.front() == "--inspect") {
        return {LoaderMode::inspect, "read-only inspection"};
    }
    if (arguments.front() == "--launch") {
        return {LoaderMode::launch, "one-shot launch and load"};
    }
    return {};
}

}  // namespace mcfix
