#include "mcfix/signature.hpp"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace mcfix {
namespace {

int hex_value(char value) {
    const auto c = static_cast<unsigned char>(value);
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

Pattern parse_pattern(std::string_view text) {
    Pattern pattern;
    std::size_t position = 0;

    while (position < text.size()) {
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) {
            ++position;
        }
        if (position == text.size()) {
            break;
        }

        const auto start = position;
        while (position < text.size() && !std::isspace(static_cast<unsigned char>(text[position]))) {
            ++position;
        }
        const auto token = text.substr(start, position - start);
        if (token == "?" || token == "??") {
            pattern.push_back(PatternByte{std::byte{0}, true});
            continue;
        }
        if (token.size() != 2) {
            throw std::invalid_argument("signature tokens must be two hex digits or a wildcard");
        }
        const auto high = hex_value(token[0]);
        const auto low = hex_value(token[1]);
        if (high < 0 || low < 0) {
            throw std::invalid_argument("signature contains a non-hex token");
        }
        pattern.push_back(PatternByte{static_cast<std::byte>((high << 4) | low), false});
    }

    if (pattern.empty()) {
        throw std::invalid_argument("signature cannot be empty");
    }
    return pattern;
}

std::vector<std::size_t> find_all(std::span<const std::byte> bytes, const Pattern& pattern) {
    if (pattern.empty()) {
        throw std::invalid_argument("signature cannot be empty");
    }

    std::vector<std::size_t> matches;
    if (pattern.size() > bytes.size()) {
        return matches;
    }

    for (std::size_t start = 0; start <= bytes.size() - pattern.size(); ++start) {
        bool matched = true;
        for (std::size_t index = 0; index < pattern.size(); ++index) {
            if (!pattern[index].wildcard && bytes[start + index] != pattern[index].value) {
                matched = false;
                break;
            }
        }
        if (matched) {
            matches.push_back(start);
        }
    }
    return matches;
}

const std::byte* resolve_rel32_call(const std::byte* instruction) {
    if (instruction == nullptr || instruction[0] != std::byte{0xE8}) {
        return nullptr;
    }
    std::int32_t displacement = 0;
    std::memcpy(&displacement, instruction + 1, sizeof(displacement));
    return instruction + 5 + displacement;
}

}  // namespace mcfix
