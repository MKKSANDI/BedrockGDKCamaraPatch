#include "installer/directory_access.hpp"

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <string>
#include <utility>
#include <vector>

namespace mcfix::installer {
namespace {

bool enable_privilege(const wchar_t* name) noexcept {
    HANDLE token = nullptr;
    if (!OpenProcessToken(
            GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }
    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, name, &luid)) {
        CloseHandle(token);
        return false;
    }
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = luid;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(ERROR_SUCCESS);
    const BOOL adjusted = AdjustTokenPrivileges(
        token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr);
    const DWORD error = GetLastError();
    CloseHandle(token);
    return adjusted && error == ERROR_SUCCESS;
}

std::vector<std::byte> current_user_token() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return {};
    }
    DWORD required = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &required);
    std::vector<std::byte> bytes(required);
    if (required == 0 || !GetTokenInformation(
            token, TokenUser, bytes.data(), required, &required)) {
        bytes.clear();
    }
    CloseHandle(token);
    return bytes;
}

}  // namespace

std::wstring security_sddl(const std::filesystem::path& path) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD status = GetNamedSecurityInfoW(
        const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        nullptr, nullptr, nullptr, nullptr, &descriptor);
    if (status != ERROR_SUCCESS || descriptor == nullptr) {
        return {};
    }
    wchar_t* text = nullptr;
    ULONG length = 0;
    std::wstring result;
    if (ConvertSecurityDescriptorToStringSecurityDescriptorW(
            descriptor, SDDL_REVISION_1,
            OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            &text, &length) && text != nullptr) {
        result.assign(text, length);
        LocalFree(text);
    }
    LocalFree(descriptor);
    return result;
}

std::wstring directory_security_sddl(const std::filesystem::path& path) {
    return security_sddl(path);
}

std::wstring file_security_sddl(const std::filesystem::path& path) {
    return security_sddl(path);
}

bool copy_file_security(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) noexcept {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD status = GetNamedSecurityInfoW(
        const_cast<wchar_t*>(source.c_str()), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        nullptr, nullptr, nullptr, nullptr, &descriptor);
    if (status != ERROR_SUCCESS || descriptor == nullptr) {
        return false;
    }
    static_cast<void>(enable_privilege(L"SeRestorePrivilege"));
    const BOOL applied = SetFileSecurityW(
        destination.c_str(), OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        descriptor);
    LocalFree(descriptor);
    return applied != FALSE;
}

bool apply_file_security_sddl(
    std::wstring_view sddl,
    const std::filesystem::path& destination) noexcept {
    if (sddl.empty()) return false;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            std::wstring(sddl).c_str(), SDDL_REVISION_1, &descriptor, nullptr) ||
        descriptor == nullptr) {
        return false;
    }
    static_cast<void>(enable_privilege(L"SeRestorePrivilege"));
    const BOOL applied = SetFileSecurityW(
        destination.c_str(), OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        descriptor);
    LocalFree(descriptor);
    return applied != FALSE;
}

DirectoryAccessGuard::~DirectoryAccessGuard() {
    static_cast<void>(restore());
}

DirectoryAccessGuard::DirectoryAccessGuard(DirectoryAccessGuard&& other) noexcept
    : path_(std::move(other.path_)),
      original_descriptor_(std::exchange(other.original_descriptor_, nullptr)),
      active_(std::exchange(other.active_, false)),
      error_(std::move(other.error_)) {}

DirectoryAccessGuard& DirectoryAccessGuard::operator=(DirectoryAccessGuard&& other) noexcept {
    if (this != &other) {
        static_cast<void>(restore());
        path_ = std::move(other.path_);
        original_descriptor_ = std::exchange(other.original_descriptor_, nullptr);
        active_ = std::exchange(other.active_, false);
        error_ = std::move(other.error_);
    }
    return *this;
}

DirectoryAccessGuard DirectoryAccessGuard::acquire(const std::filesystem::path& path) {
    DirectoryAccessGuard guard;
    guard.path_ = path;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PSID owner = nullptr;
    PACL original_dacl = nullptr;
    const DWORD read_status = GetNamedSecurityInfoW(
        const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &original_dacl, nullptr, &descriptor);
    if (read_status != ERROR_SUCCESS || descriptor == nullptr) {
        guard.error_ = "could not capture directory security: " + std::to_string(read_status);
        return guard;
    }
    guard.original_descriptor_ = descriptor;

    auto token_bytes = current_user_token();
    if (token_bytes.empty()) {
        guard.error_ = "could not read the current user SID";
        return guard;
    }
    const auto* token_user = reinterpret_cast<const TOKEN_USER*>(token_bytes.data());
    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = GENERIC_ALL;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = static_cast<wchar_t*>(token_user->User.Sid);

    PACL writable_dacl = nullptr;
    const DWORD acl_status = SetEntriesInAclW(1, &access, original_dacl, &writable_dacl);
    if (acl_status != ERROR_SUCCESS || writable_dacl == nullptr) {
        guard.error_ = "could not create temporary directory ACL: " +
            std::to_string(acl_status);
        return guard;
    }

    DWORD write_status = SetNamedSecurityInfoW(
        const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION, nullptr, nullptr, writable_dacl, nullptr);
    if (write_status == ERROR_ACCESS_DENIED) {
        static_cast<void>(enable_privilege(L"SeTakeOwnershipPrivilege"));
        const DWORD owner_status = SetNamedSecurityInfoW(
            const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
            OWNER_SECURITY_INFORMATION, token_user->User.Sid, nullptr, nullptr, nullptr);
        if (owner_status == ERROR_SUCCESS) {
            write_status = SetNamedSecurityInfoW(
                const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION, nullptr, nullptr, writable_dacl, nullptr);
        } else {
            write_status = owner_status;
        }
    }
    LocalFree(writable_dacl);
    if (write_status != ERROR_SUCCESS) {
        guard.error_ = "could not grant temporary directory access: " +
            std::to_string(write_status);
        return guard;
    }
    guard.active_ = true;
    return guard;
}

bool DirectoryAccessGuard::restore() noexcept {
    if (original_descriptor_ == nullptr) {
        active_ = false;
        return true;
    }
    bool restored = true;
    if (active_) {
        PSID owner = nullptr;
        PACL dacl = nullptr;
        BOOL owner_defaulted = FALSE;
        BOOL dacl_present = FALSE;
        BOOL dacl_defaulted = FALSE;
        if (!GetSecurityDescriptorOwner(
                static_cast<PSECURITY_DESCRIPTOR>(original_descriptor_),
                &owner, &owner_defaulted) ||
            !GetSecurityDescriptorDacl(
                static_cast<PSECURITY_DESCRIPTOR>(original_descriptor_),
                &dacl_present, &dacl, &dacl_defaulted)) {
            restored = false;
        } else {
            static_cast<void>(enable_privilege(L"SeRestorePrivilege"));
            const BOOL status = SetFileSecurityW(
                path_.c_str(), OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                static_cast<PSECURITY_DESCRIPTOR>(original_descriptor_));
            restored = status != FALSE;
            if (!restored) {
                error_ = "could not restore directory security: " +
                    std::to_string(GetLastError());
            }
        }
    }
    LocalFree(original_descriptor_);
    original_descriptor_ = nullptr;
    active_ = false;
    return restored;
}

}  // namespace mcfix::installer
