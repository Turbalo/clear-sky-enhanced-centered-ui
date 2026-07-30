#include "signature.h"

#include <algorithm>
#include <cstring>

namespace cs4x3ui
{
std::byte* find_signature(
    const HMODULE module, const Signature& signature,
    std::size_t* const match_count) noexcept
{
    if (match_count != nullptr)
        *match_count = 0;
    if (module == nullptr || signature.bytes.empty() || signature.mask == nullptr)
        return nullptr;

    const auto base = reinterpret_cast<std::byte*>(module);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;

    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return nullptr;

    if (std::strlen(signature.mask) != signature.bytes.size())
    {
        return nullptr;
    }

    const bool exact = std::all_of(
        signature.mask, signature.mask + signature.bytes.size(),
        [](const char value) { return value == 'x'; });
    std::byte* match = nullptr;
    std::size_t count = 0;
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD section_index = 0;
         section_index < nt->FileHeader.NumberOfSections;
         ++section_index, ++section)
    {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
            continue;

        const std::size_t section_size = section->Misc.VirtualSize;
        if (section_size < signature.bytes.size())
            continue;

        const auto section_begin =
            reinterpret_cast<const std::uint8_t*>(base + section->VirtualAddress);
        const auto section_end = section_begin + section_size;
        if (exact)
        {
            auto cursor = section_begin;
            while (cursor < section_end)
            {
                const auto found = std::search(
                    cursor, section_end,
                    signature.bytes.begin(), signature.bytes.end());
                if (found == section_end)
                    break;
                ++count;
                match = base + section->VirtualAddress + (found - section_begin);
                cursor = found + 1;
            }
            continue;
        }

        for (std::size_t offset = 0;
             offset <= section_size - signature.bytes.size(); ++offset)
        {
            bool equal = true;
            for (std::size_t index = 0; index < signature.bytes.size(); ++index)
            {
                if (signature.mask[index] == 'x' &&
                    static_cast<std::uint8_t>(
                        base[section->VirtualAddress + offset + index]) !=
                        signature.bytes[index])
                {
                    equal = false;
                    break;
                }
            }

            if (!equal)
                continue;
            ++count;
            match = base + section->VirtualAddress + offset;
        }
    }

    if (match_count != nullptr)
        *match_count = count;
    return count == 1 ? match : nullptr;
}
} // namespace cs4x3ui
