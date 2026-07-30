#include <Windows.h>
#include <dinput.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace
{
using DirectInput8CreateFn =
    HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
}

int wmain(const int argc, wchar_t** argv)
{
    if (argc != 2)
    {
        std::wcerr << L"Usage: proxy_smoke_test <proxy-dinput8.dll>\n";
        return 2;
    }

    const std::filesystem::path proxy_path(argv[1]);
    const HMODULE proxy = LoadLibraryW(proxy_path.c_str());
    if (proxy == nullptr)
    {
        std::wcerr << L"LoadLibraryW failed: " << GetLastError() << L'\n';
        return 3;
    }

    constexpr std::array<std::string_view, 6> export_names{
        "DirectInput8Create",
        "DllCanUnloadNow",
        "DllGetClassObject",
        "DllRegisterServer",
        "DllUnregisterServer",
        "GetdfDIJoystick",
    };
    for (const std::string_view name : export_names)
    {
        if (GetProcAddress(proxy, name.data()) == nullptr)
        {
            std::wcerr << L"Required export is missing: "
                       << name.data() << L'\n';
            FreeLibrary(proxy);
            return 4;
        }
    }

    const auto create = reinterpret_cast<DirectInput8CreateFn>(
        GetProcAddress(proxy, "DirectInput8Create"));

    IDirectInput8W* direct_input = nullptr;
    const HRESULT result = create(
        GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
        IID_IDirectInput8W, reinterpret_cast<void**>(&direct_input), nullptr);
    if (FAILED(result) || direct_input == nullptr)
    {
        std::wcerr << L"DirectInput8Create failed: 0x" << std::hex << result << L'\n';
        FreeLibrary(proxy);
        return 5;
    }

    direct_input->Release();
    Sleep(250);
    FreeLibrary(proxy);
    return 0;
}
