#include "installer/runtime_contract.hpp"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace mcfix::installer {
namespace {

template <typename T>
const T* at(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
        return nullptr;
    }
    return reinterpret_cast<const T*>(bytes.data() + offset);
}

std::size_t rva_to_offset(
    std::span<const std::byte> bytes,
    const IMAGE_NT_HEADERS64& nt,
    const IMAGE_SECTION_HEADER* sections,
    DWORD rva) noexcept {
    if (rva < nt.OptionalHeader.SizeOfHeaders && rva < bytes.size()) {
        return rva;
    }
    for (WORD index = 0; index < nt.FileHeader.NumberOfSections; ++index) {
        const auto& section = sections[index];
        const auto span = (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
        const std::uint64_t begin = section.VirtualAddress;
        const std::uint64_t end = begin + span;
        if (rva < begin || rva >= end) {
            continue;
        }
        const std::uint64_t offset =
            static_cast<std::uint64_t>(section.PointerToRawData) + (rva - begin);
        return offset < bytes.size() ? static_cast<std::size_t>(offset) : bytes.size();
    }
    return bytes.size();
}

std::string_view c_string_at(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    if (offset >= bytes.size()) {
        return {};
    }
    const auto* first = reinterpret_cast<const char*>(bytes.data() + offset);
    const auto available = bytes.size() - offset;
    const auto* last = static_cast<const char*>(std::memchr(first, '\0', available));
    return last == nullptr ? std::string_view{} : std::string_view(first, last);
}

}  // namespace

RuntimeContract inspect_runtime_contract(const std::filesystem::path& path) {
    RuntimeContract result{};
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return result;
    }
    const auto end = input.tellg();
    if (end <= 0 || end > 8 * 1024 * 1024) {
        return result;
    }
    std::vector<std::byte> storage(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(storage.data()), static_cast<std::streamsize>(storage.size()));
    if (!input) {
        return result;
    }
    const std::span<const std::byte> bytes(storage);
    const auto* dos = at<IMAGE_DOS_HEADER>(bytes, 0);
    if (dos == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return result;
    }
    const auto nt_offset = static_cast<std::size_t>(dos->e_lfanew);
    const auto* nt = at<IMAGE_NT_HEADERS64>(bytes, nt_offset);
    if (nt == nullptr || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        (nt->FileHeader.Characteristics & IMAGE_FILE_DLL) == 0) {
        return result;
    }
    const auto sections_offset = nt_offset + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
        nt->FileHeader.SizeOfOptionalHeader;
    if (sections_offset > bytes.size() ||
        nt->FileHeader.NumberOfSections >
            (bytes.size() - sections_offset) / sizeof(IMAGE_SECTION_HEADER)) {
        return result;
    }
    const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
        bytes.data() + sections_offset);
    result.valid_x64_dll = true;

    if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
        return result;
    }
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (directory.VirtualAddress == 0 || directory.Size < sizeof(IMAGE_EXPORT_DIRECTORY)) {
        return result;
    }
    const auto export_offset = rva_to_offset(bytes, *nt, sections, directory.VirtualAddress);
    const auto* exports = at<IMAGE_EXPORT_DIRECTORY>(bytes, export_offset);
    if (exports == nullptr || exports->NumberOfNames > 4096 || exports->NumberOfFunctions > 4096) {
        return result;
    }
    result.named_export_count = exports->NumberOfNames;
    const auto names_offset = rva_to_offset(bytes, *nt, sections, exports->AddressOfNames);
    const auto ordinals_offset = rva_to_offset(bytes, *nt, sections, exports->AddressOfNameOrdinals);
    const auto functions_offset = rva_to_offset(bytes, *nt, sections, exports->AddressOfFunctions);
    if (names_offset > bytes.size() || ordinals_offset > bytes.size() ||
        functions_offset > bytes.size() ||
        exports->NumberOfNames > (bytes.size() - names_offset) / sizeof(DWORD) ||
        exports->NumberOfNames > (bytes.size() - ordinals_offset) / sizeof(WORD) ||
        exports->NumberOfFunctions > (bytes.size() - functions_offset) / sizeof(DWORD)) {
        return result;
    }

    const auto* names = reinterpret_cast<const DWORD*>(bytes.data() + names_offset);
    const auto* ordinals = reinterpret_cast<const WORD*>(bytes.data() + ordinals_offset);
    const auto* functions = reinterpret_cast<const DWORD*>(bytes.data() + functions_offset);
    constexpr std::string_view required = "__CxxFrameHandler4";
    const std::uint64_t export_begin = directory.VirtualAddress;
    const std::uint64_t export_end = export_begin + directory.Size;
    for (DWORD index = 0; index < exports->NumberOfNames; ++index) {
        const auto name_offset = rva_to_offset(bytes, *nt, sections, names[index]);
        if (c_string_at(bytes, name_offset) != required) {
            continue;
        }
        ++result.handler_export_count;
        if (ordinals[index] >= exports->NumberOfFunctions) {
            result.valid_x64_dll = false;
            continue;
        }
        const auto function_rva = functions[ordinals[index]];
        result.handler_is_forwarded =
            function_rva >= export_begin && function_rva < export_end;
    }
    return result;
}

}  // namespace mcfix::installer
