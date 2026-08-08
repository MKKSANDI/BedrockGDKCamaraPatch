#pragma once

#include <cstddef>
#include <span>

namespace mcfix {

bool is_pe64_image(std::span<const std::byte> bytes);

}  // namespace mcfix
