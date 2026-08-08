#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace mcfix::installer {

std::wstring directory_security_sddl(const std::filesystem::path& path);
std::wstring file_security_sddl(const std::filesystem::path& path);
bool copy_file_security(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) noexcept;
bool apply_file_security_sddl(
    std::wstring_view sddl,
    const std::filesystem::path& destination) noexcept;

class DirectoryAccessGuard {
public:
    DirectoryAccessGuard() = default;
    ~DirectoryAccessGuard();
    DirectoryAccessGuard(const DirectoryAccessGuard&) = delete;
    DirectoryAccessGuard& operator=(const DirectoryAccessGuard&) = delete;
    DirectoryAccessGuard(DirectoryAccessGuard&& other) noexcept;
    DirectoryAccessGuard& operator=(DirectoryAccessGuard&& other) noexcept;

    static DirectoryAccessGuard acquire(const std::filesystem::path& path);
    bool active() const noexcept { return active_; }
    bool restore() noexcept;
    const std::string& error() const noexcept { return error_; }

private:
    std::filesystem::path path_;
    void* original_descriptor_{};
    bool active_{};
    std::string error_;
};

}  // namespace mcfix::installer
