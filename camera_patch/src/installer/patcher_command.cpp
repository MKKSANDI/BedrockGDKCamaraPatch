#include "installer/patcher_command.hpp"

namespace mcfix::installer {

ParsedCommand parse_patcher_command(const std::vector<std::wstring>& arguments) noexcept {
    if (arguments.size() == 1) {
        if (arguments[0] == L"status") {
            return {.kind = CommandKind::status};
        }
        if (arguments[0] == L"verify") {
            return {.kind = CommandKind::verify};
        }
        if (arguments[0] == L"install") {
            return {.kind = CommandKind::install};
        }
        if (arguments[0] == L"repair") {
            return {.kind = CommandKind::repair};
        }
        if (arguments[0] == L"uninstall") {
            return {.kind = CommandKind::uninstall};
        }
        if (arguments[0] == L"probe-status") {
            return {.kind = CommandKind::probe_status};
        }
        if (arguments[0] == L"probe-stage") {
            return {.kind = CommandKind::probe_stage};
        }
    }
    if (arguments.size() == 2 && arguments[1] == L"--silent") {
        if (arguments[0] == L"verify") {
            return {.kind = CommandKind::verify, .silent = true};
        }
        if (arguments[0] == L"repair") {
            return {.kind = CommandKind::repair, .silent = true};
        }
        if (arguments[0] == L"watch") {
            return {.kind = CommandKind::watch, .silent = true};
        }
    }
    if (arguments.size() == 2 && arguments[1] == L"--confirm") {
        if (arguments[0] == L"probe-install") {
            return {.kind = CommandKind::probe_install};
        }
        if (arguments[0] == L"probe-uninstall") {
            return {.kind = CommandKind::probe_uninstall};
        }
    }
    return {};
}

}  // namespace mcfix::installer
