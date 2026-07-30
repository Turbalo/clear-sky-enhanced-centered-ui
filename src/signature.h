#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace cs4x3ui
{
struct Signature
{
    std::span<const std::uint8_t> bytes;
    const char* mask;
};

[[nodiscard]] std::byte* find_signature(
    HMODULE module, const Signature& signature,
    std::size_t* match_count = nullptr) noexcept;
} // namespace cs4x3ui
