#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace mcfix {

struct PatternByte {
    std::byte value{};
    bool wildcard{false};
};

using Pattern = std::vector<PatternByte>;

Pattern parse_pattern(std::string_view text);
std::vector<std::size_t> find_all(std::span<const std::byte> bytes, const Pattern& pattern);
const std::byte* resolve_rel32_call(const std::byte* instruction);

}  // namespace mcfix
