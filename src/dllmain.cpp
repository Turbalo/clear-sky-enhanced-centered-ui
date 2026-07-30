#include "module.h"
#include "log.h"

#include <Windows.h>
#include <Unknwn.h>

#include <array>
#include <filesystem>
#include <string_view>

extern "C"
{
FARPROC real_DirectInput8Create = nullptr;
FARPROC real_DllCanUnloadNow = nullptr;
FARPROC real_DllGetClassObject = nullptr;
FARPROC real_DllRegisterServer = nullptr;
FARPROC real_DllUnregisterServer = nullptr;
FARPROC real_GetdfDIJoystick = nullptr;
}

namespace
{
HMODULE g_real_dinput8 = nullptr;
HMODULE g_proxy_module = nullptr;
INIT_ONCE g_initialize_once = INIT_ONCE_STATIC_INIT;

enum class BackendStatus
{
    system,
    chain,
    chain_failed,
};

BackendStatus g_backend_status = BackendStatus::system;

std::filesystem::path current_module_path(HMODULE module);

bool resolve_exports()
{
    real_DirectInput8Create = GetProcAddress(g_real_dinput8, "DirectInput8Create");
    real_DllCanUnloadNow = GetProcAddress(g_real_dinput8, "DllCanUnloadNow");
    real_DllGetClassObject = GetProcAddress(g_real_dinput8, "DllGetClassObject");
    real_DllRegisterServer = GetProcAddress(g_real_dinput8, "DllRegisterServer");
    real_DllUnregisterServer = GetProcAddress(g_real_dinput8, "DllUnregisterServer");
    real_GetdfDIJoystick = GetProcAddress(g_real_dinput8, "GetdfDIJoystick");

    return
        real_DirectInput8Create != nullptr &&
        real_DllCanUnloadNow != nullptr &&
        real_DllGetClassObject != nullptr &&
        real_DllRegisterServer != nullptr &&
        real_DllUnregisterServer != nullptr &&
        real_GetdfDIJoystick != nullptr;
}

bool load_real_dinput8(const HMODULE proxy_module)
{
    const std::filesystem::path proxy_path = current_module_path(proxy_module);
    const std::filesystem::path chain_path =
        proxy_path.parent_path() / L"dinput8_chain.dll";
    const DWORD chain_attributes = GetFileAttributesW(chain_path.c_str());
    if (chain_attributes != INVALID_FILE_ATTRIBUTES &&
        (chain_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        g_real_dinput8 = LoadLibraryExW(
            chain_path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        g_backend_status = g_real_dinput8 != nullptr
            ? BackendStatus::chain
            : BackendStatus::chain_failed;
        if (g_real_dinput8 != nullptr && resolve_exports())
            return true;
        if (g_real_dinput8 != nullptr)
        {
            FreeLibrary(g_real_dinput8);
            g_real_dinput8 = nullptr;
            g_backend_status = BackendStatus::chain_failed;
        }
    }

    std::array<wchar_t, MAX_PATH> system_directory{};
    const UINT length = GetSystemDirectoryW(
        system_directory.data(), static_cast<UINT>(system_directory.size()));
    if (length == 0 || length >= system_directory.size())
        return false;

    const std::filesystem::path system_path =
        std::filesystem::path(
            std::wstring_view(system_directory.data(), length)) /
        L"dinput8.dll";
    g_real_dinput8 = LoadLibraryW(system_path.c_str());

    if (g_real_dinput8 == nullptr)
        return false;

    if (!resolve_exports())
    {
        FreeLibrary(g_real_dinput8);
        g_real_dinput8 = nullptr;
        return false;
    }
    return true;
}

std::filesystem::path current_module_path(const HMODULE module)
{
    std::array<wchar_t, 32768> buffer{};
    const DWORD length =
        GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size())
        return {};
    return std::filesystem::path(std::wstring_view(buffer.data(), length));
}

DWORD WINAPI initialize_worker(void* module_reference)
{
    cs4x3ui::initialize(current_module_path(g_proxy_module));
    switch (g_backend_status)
    {
    case BackendStatus::system:
        cs4x3ui::log::write("DirectInput backend: system32");
        break;
    case BackendStatus::chain:
        cs4x3ui::log::write("DirectInput backend: dinput8_chain.dll");
        break;
    case BackendStatus::chain_failed:
        cs4x3ui::log::write(
            "DirectInput backend: chain load failed; using system32");
        break;
    }

    FreeLibraryAndExitThread(
        static_cast<HMODULE>(module_reference), 0);
}

BOOL CALLBACK initialize_once(
    PINIT_ONCE, void*, void**)
{
    HMODULE module_reference = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&initialize_worker),
            &module_reference))
    {
        cs4x3ui::log::initialize(current_module_path(g_proxy_module));
        cs4x3ui::log::write(
            "Fail-safe: unable to retain proxy module for initialization");
        return TRUE;
    }

    const HANDLE thread = CreateThread(
        nullptr, 0, initialize_worker, module_reference, 0, nullptr);
    if (thread != nullptr)
        CloseHandle(thread);
    else
    {
        FreeLibrary(module_reference);
        cs4x3ui::log::initialize(current_module_path(g_proxy_module));
        cs4x3ui::log::write(
            "Fail-safe: unable to create runtime initialization worker");
    }
    return TRUE;
}
} // namespace

extern "C" HRESULT WINAPI wrapped_DirectInput8Create(
    const HINSTANCE instance, const DWORD version, REFIID interface_id,
    void** output, IUnknown* outer)
{
    using DirectInput8CreateFn =
        HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
    const auto create =
        reinterpret_cast<DirectInput8CreateFn>(real_DirectInput8Create);
    const HRESULT result =
        create(instance, version, interface_id, output, outer);
    InitOnceExecuteOnce(&g_initialize_once, initialize_once, nullptr, nullptr);
    return result;
}

BOOL APIENTRY DllMain(const HMODULE module, const DWORD reason, void* reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        g_proxy_module = module;
        if (!load_real_dinput8(module))
            return FALSE;
    }
    else if (
        reason == DLL_PROCESS_DETACH && reserved == nullptr &&
        g_real_dinput8 != nullptr)
    {
        FreeLibrary(g_real_dinput8);
        g_real_dinput8 = nullptr;
        g_proxy_module = nullptr;
    }

    return TRUE;
}
