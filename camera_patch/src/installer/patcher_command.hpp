#pragma once

#include <string>
#include <vector>

namespace mcfix::installer {

enum class CommandKind {
    invalid,
    status,
    verify,
    install,
    repair,
    uninstall,
    watch,
    probe_status,
    probe_stage,
    probe_install,
    probe_uninstall,
};

struct ParsedCommand {
    CommandKind kind{CommandKind::invalid};
    bool silent{};
};

ParsedCommand parse_patcher_command(const std::vector<std::wstring>& arguments) noexcept;

}  // namespace mcfix::installer
