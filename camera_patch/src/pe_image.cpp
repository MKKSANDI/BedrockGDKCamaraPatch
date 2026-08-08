#include "pe_image.hpp"

#include <cstdint>
#include <cstring>

namespace mcfix {

bool is_pe64_image(std::span<const std::byte> bytes) {
    if (bytes.size() < 0x40 || bytes[0] != std::byte{'M'} || bytes[1] != std::byte{'Z'}) {
        return false;
    }

    std::int32_t pe_offset = 0;
    std::memcpy(&pe_offset, bytes.data() + 0x3C, sizeof(pe_offset));
    if (pe_offset < 0) {
        return false;
    }
    const auto offset = static_cast<std::size_t>(pe_offset);
    constexpr std::size_t optional_magic_offset = 4 + 20;
    if (offset > bytes.size() || bytes.size() - offset < optional_magic_offset + 2) {
        return false;
    }

    std::uint32_t signature = 0;
    std::uint16_t machine = 0;
    std::uint16_t optional_magic = 0;
    std::memcpy(&signature, bytes.data() + offset, sizeof(signature));
    std::memcpy(&machine, bytes.data() + offset + 4, sizeof(machine));
    std::memcpy(&optional_magic, bytes.data() + offset + optional_magic_offset, sizeof(optional_magic));
    return signature == 0x00004550 && machine == 0x8664 && optional_magic == 0x020B;
}

}  // namespace mcfix
