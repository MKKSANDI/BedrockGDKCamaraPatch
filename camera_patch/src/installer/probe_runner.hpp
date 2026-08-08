#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mcfix::installer {

struct ProcessRequest {
    std::filesystem::path executable;
    std::vector<std::wstring> arguments;
    std::uint32_t timeout_ms{};
};

struct ProcessResult {
    bool launched{};
    bool timed_out{};
    bool output_truncated{};
    std::uint32_t exit_code{0xFFFFFFFFU};
    std::uint32_t win32_error{};
    std::string output;
};

ProcessResult run_hidden(const ProcessRequest& request);

}  // namespace mcfix::installer
