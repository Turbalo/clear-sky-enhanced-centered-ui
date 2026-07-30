#include "module.h"

#include "log.h"
#include "runtime_hooks.h"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace
{
constexpr std::array<std::uint8_t, 32> kSupportedSha256{
    0x89, 0xBA, 0x7F, 0xC6, 0xB8, 0x4B, 0xB1, 0x8A,
    0x3D, 0x0B, 0x47, 0x93, 0x6B, 0x2E, 0x67, 0xBD,
    0x1B, 0x7C, 0xC8, 0xB6, 0x42, 0xA4, 0xF3, 0x22,
    0xB0, 0x68, 0xC9, 0x77, 0x4A, 0x87, 0x41, 0xE1};

std::wstring read_file_version(const std::filesystem::path& path)
{
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size == 0)
        return {};

    std::vector<std::byte> buffer(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, buffer.data()))
        return {};

    struct Translation
    {
        WORD language;
        WORD code_page;
    };

    Translation* translations = nullptr;
    UINT translation_size = 0;
    if (!VerQueryValueW(
            buffer.data(), L"\\VarFileInfo\\Translation",
            reinterpret_cast<void**>(&translations), &translation_size) ||
        translation_size < sizeof(Translation))
    {
        return {};
    }

    const auto query = std::format(
        L"\\StringFileInfo\\{:04x}{:04x}\\FileVersion",
        translations[0].language, translations[0].code_page);

    wchar_t* value = nullptr;
    UINT value_size = 0;
    if (!VerQueryValueW(
            buffer.data(), query.c_str(), reinterpret_cast<void**>(&value), &value_size) ||
        value == nullptr)
    {
        return {};
    }

    return value;
}

bool sha256_file(
    const std::filesystem::path& path,
    std::array<std::uint8_t, 32>& output)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE;
    bool success = false;

    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
    {
        return false;
    }

    DWORD object_size = 0;
    DWORD result_size = 0;
    if (!BCRYPT_SUCCESS(BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
            &result_size, 0)))
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::vector<std::uint8_t> hash_object(object_size);
    if (!BCRYPT_SUCCESS(BCryptCreateHash(
            algorithm, &hash, hash_object.data(), object_size,
            nullptr, 0, 0)))
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    file = CreateFileW(
        path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        std::array<std::uint8_t, 64 * 1024> buffer{};
        success = true;
        while (true)
        {
            DWORD bytes_read = 0;
            if (!ReadFile(
                    file, buffer.data(), static_cast<DWORD>(buffer.size()),
                    &bytes_read, nullptr))
            {
                success = false;
                break;
            }
            if (bytes_read == 0)
                break;
            if (!BCRYPT_SUCCESS(BCryptHashData(
                    hash, buffer.data(), bytes_read, 0)))
            {
                success = false;
                break;
            }
        }
    }

    if (success)
    {
        success = BCRYPT_SUCCESS(BCryptFinishHash(
            hash, output.data(), static_cast<ULONG>(output.size()), 0));
    }

    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return success;
}

std::string sha256_text(const std::array<std::uint8_t, 32>& hash)
{
    std::string result;
    result.reserve(hash.size() * 2);
    for (const std::uint8_t value : hash)
        result += std::format("{:02X}", value);
    return result;
}

std::string utf8_text(const std::wstring_view text)
{
    if (text.empty())
        return "<unknown>";
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return "<unreadable>";
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}
} // namespace

namespace cs4x3ui
{
void initialize(const std::filesystem::path& proxy_path)
{
    log::initialize(proxy_path);
    log::write("Clear Sky centered UI runtime initializing");

    std::array<wchar_t, 32768> executable_buffer{};
    const DWORD length = GetModuleFileNameW(
        nullptr, executable_buffer.data(), static_cast<DWORD>(executable_buffer.size()));
    if (length == 0 || length == executable_buffer.size())
    {
        log::write("Fail-safe: unable to resolve executable path");
        return;
    }

    const std::filesystem::path executable_path(
        std::wstring_view(executable_buffer.data(), length));
    if (_wcsicmp(executable_path.filename().c_str(), L"xrEngine.exe") != 0)
    {
        log::write("Fail-safe: host executable is not xrEngine.exe");
        return;
    }

    const std::wstring version = read_file_version(executable_path);

    std::array<std::uint8_t, 32> sha256{};
    if (!sha256_file(executable_path, sha256))
    {
        log::write("Fail-safe: unable to hash xrEngine.exe");
        return;
    }
    const bool known_hash = sha256 == kSupportedSha256;
    log::write(std::format(
        "xrEngine.exe version: {}; SHA-256: {}; profile: {}",
        utf8_text(version), sha256_text(sha256),
        known_hash ? "Verified" : "Signature-compatible candidate"));

    if (!install_runtime_hooks())
        return;

    if (known_hash)
    {
        log::write(
            "Verified build detected; centered 16:10 runtime active");
    }
    else
    {
        log::write(
            "Unknown SHA accepted after complete runtime signature "
            "validation; centered 16:10 runtime active");
    }
}
} // namespace cs4x3ui
