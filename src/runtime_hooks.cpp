#include "runtime_hooks.h"

#include "log.h"
#include "safe_zone.h"
#include "signature.h"

#include <Windows.h>
#include <MinHook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string_view>
#include <vector>

namespace
{
using OnDeviceResetFn = void (*)(void*);
using UiCoreConstructorFn = void* (*)(void*);
using SetScissorFn = void (*)(void*, const void*);
using PushPointFn =
    void (*)(void*, float, float, float, std::uint32_t, float, float);
using StartPrimitiveFn = void (*)(void*, std::uint32_t, std::int32_t, std::int32_t);
using FlushPrimitiveFn = void (*)(void*);
using FontOnRenderFn = void (*)(void*, void*);
using CursorUpdateFn = void (*)(void*);
using WeaponRenderItemUiFn = void (*)(void*);

struct IntRect
{
    std::int32_t x1;
    std::int32_t y1;
    std::int32_t x2;
    std::int32_t y2;
};

struct Float2
{
    float x;
    float y;
};
static_assert(sizeof(Float2) == sizeof(std::uint64_t));

struct UiPoint
{
    float x;
    float y;
    float z;
    std::uint32_t color;
    float u;
    float v;
};

constexpr std::ptrdiff_t kScaleOffset = 0x170;
constexpr float kUiWidth = 1024.0F;
constexpr float kUiHeight = 768.0F;
constexpr std::int32_t kPointTypeTl = 0;
constexpr std::int32_t kPrimitiveTypeTriList = 0;
constexpr std::int32_t kPrimitiveTypeLineList = 3;
constexpr std::ptrdiff_t kFontStringsBeginOffset = 0x48;
constexpr std::ptrdiff_t kFontStringsEndOffset = 0x50;
// Enhanced Edition stores the per-font X/Y render scale after CGameFont.
constexpr std::ptrdiff_t kFontHorizontalScaleOffset = 0x70;
constexpr std::ptrdiff_t kFontStringXOffset = 0x400;
constexpr std::size_t kFontStringStride = 0x414;
constexpr std::ptrdiff_t kUiCoreVtableRva = 0x995F60;
constexpr std::ptrdiff_t kUiCoreVtableOffset = 0x0;
constexpr std::ptrdiff_t kInputModeCheckRva = 0x436B0;
constexpr std::ptrdiff_t kInputModeRva = 0xCE3130;

std::atomic<void*> g_ui_core = nullptr;
std::atomic<float> g_offset_x = 0.0F;
std::atomic<float> g_screen_width = 0.0F;
std::atomic<float> g_safe_width = 0.0F;
std::atomic<float> g_horizontal_ratio = 1.0F;
std::atomic<bool> g_mouse_cursor_path_logged = false;
std::atomic<bool> g_controller_cursor_path_logged = false;
thread_local std::int32_t g_point_type = -1;
thread_local bool g_crosshair_primitive = false;
thread_local bool g_preserve_local_width = false;
thread_local bool g_native_weapon_ui = false;
thread_local std::vector<UiPoint> g_buffered_points;
const std::int32_t* g_input_mode = nullptr;

OnDeviceResetFn g_on_device_reset = nullptr;
UiCoreConstructorFn g_ui_core_constructor = nullptr;
SetScissorFn g_set_scissor = nullptr;
PushPointFn g_push_point = nullptr;
StartPrimitiveFn g_start_primitive = nullptr;
FlushPrimitiveFn g_flush_primitive = nullptr;
FontOnRenderFn g_font_on_render = nullptr;
CursorUpdateFn g_cursor_update = nullptr;
WeaponRenderItemUiFn g_weapon_render_item_ui = nullptr;
bool g_initialized = false;

constexpr std::array<std::uint8_t, 20> kOnDeviceResetBytes{
    0x48, 0x83, 0xEC, 0x38, 0x8B, 0x05, 0x7E, 0x04, 0xC5, 0x00,
    0x48, 0x8D, 0x54, 0x24, 0x20, 0xC5, 0xF8, 0x57, 0xC0, 0xC4};
constexpr std::array<std::uint8_t, 20> kUiCoreConstructorBytes{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
    0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8D, 0x05, 0xDA, 0x12};
constexpr std::array<std::uint8_t, 20> kSetScissorBytes{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x0D, 0xAB,
    0x80, 0x64, 0x00, 0x48, 0x8B, 0xDA, 0xE8, 0x5B, 0x3F, 0xFA};
constexpr std::array<std::uint8_t, 20> kPushPointBytes{
    0x4C, 0x8B, 0xC1, 0x8B, 0x49, 0x1C, 0x85, 0xC9, 0x74, 0x3A,
    0x83, 0xF9, 0x01, 0x75, 0x72, 0x49, 0x8B, 0x50, 0x40, 0xC5};
constexpr std::array<std::uint8_t, 20> kStartPrimitiveBytes{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x89, 0x51, 0x20, 0x48,
    0x8B, 0xD9, 0x44, 0x89, 0x41, 0x18, 0x44, 0x89, 0x49, 0x1C};
constexpr std::array<std::uint8_t, 20> kFlushPrimitiveBytes{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x83, 0x79, 0x1C, 0x00,
    0x48, 0x8B, 0xD9, 0x48, 0x89, 0x74, 0x24, 0x30, 0x48, 0x89};
constexpr std::array<std::uint8_t, 20> kFontOnRenderBytes{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
    0x57, 0x48, 0x83, 0xEC, 0x20, 0x45, 0x33, 0xC9, 0xC6, 0x41};
constexpr std::array<std::uint8_t, 20> kCursorUpdateBytes{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9, 0xE8,
    0x32, 0x36, 0xF2, 0xFF, 0x83, 0x78, 0x20, 0x02, 0x0F, 0x84};
constexpr std::array<std::uint8_t, 20> kWeaponRenderItemUiBytes{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20,
    0x48, 0x8B, 0x01, 0x48, 0x8B, 0xD9, 0xFF, 0x90, 0x88, 0x02};
constexpr std::array<std::uint8_t, 11> kInputModeCheckBytes{
    0x83, 0x3D, 0x79, 0xFA, 0xC9, 0x00, 0x02, 0x0F, 0x94, 0xC0, 0xC3};
constexpr std::ptrdiff_t kPushPointExpectedRva = 0x743120;

template <std::size_t Size>
bool process_bytes_match(
    const std::uint8_t* address,
    const std::array<std::uint8_t, Size>& expected) noexcept
{
    std::array<std::uint8_t, Size> current{};
    SIZE_T bytes_read = 0;
    return ReadProcessMemory(
               GetCurrentProcess(), address, current.data(), current.size(),
               &bytes_read) &&
        bytes_read == current.size() &&
        current == expected;
}

void activate_ui_core(void* self)
{
    auto scales = reinterpret_cast<float*>(
        static_cast<std::byte*>(self) + kScaleOffset);
    const float screen_width = scales[0] * kUiWidth;
    const float screen_height = scales[1] * kUiHeight;
    const cs4x3ui::SafeZone zone =
        cs4x3ui::make_safe_zone(screen_width, screen_height);
    g_ui_core.store(self);
    g_offset_x.store(zone.offset_x);
    g_screen_width.store(screen_width);
    g_safe_width.store(zone.width);
    g_horizontal_ratio.store(zone.width / screen_width);

    cs4x3ui::log::write(std::format(
        "UI reset: core=0x{:X}, screen {:.0f}x{:.0f}, "
        "native scale X/Y {:.6f}/{:.6f}, safe width {:.0f}, "
        "ratio {:.6f}, offset X {:.1f}",
        reinterpret_cast<std::uintptr_t>(self), screen_width, screen_height,
        scales[0], scales[1], zone.width, zone.width / screen_width,
        zone.offset_x));
}

void on_device_reset_hook(void* self)
{
    g_on_device_reset(self);
    activate_ui_core(self);
}

void* ui_core_constructor_hook(void* self)
{
    void* const result = g_ui_core_constructor(self);
    activate_ui_core(result != nullptr ? result : self);
    return result;
}

void* find_ui_core(
    const HMODULE executable, std::size_t* candidate_count)
{
    const auto expected_vtable =
        reinterpret_cast<std::uintptr_t>(executable) + kUiCoreVtableRva;
    std::vector<std::byte> buffer(1024 * 1024);
    std::vector<void*> candidates;

    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    auto address = reinterpret_cast<std::uintptr_t>(
        system_info.lpMinimumApplicationAddress);
    const auto maximum = reinterpret_cast<std::uintptr_t>(
        system_info.lpMaximumApplicationAddress);

    while (address < maximum)
    {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(
                reinterpret_cast<const void*>(address), &region,
                sizeof(region)) == 0)
        {
            break;
        }

        const auto region_base =
            reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        const auto region_end = region_base + region.RegionSize;
        const DWORD protection = region.Protect & 0xFF;
        const bool writable =
            protection == PAGE_READWRITE ||
            protection == PAGE_WRITECOPY ||
            protection == PAGE_EXECUTE_READWRITE ||
            protection == PAGE_EXECUTE_WRITECOPY;

        if (region.State == MEM_COMMIT && writable &&
            (region.Protect & PAGE_GUARD) == 0)
        {
            for (std::uintptr_t chunk = region_base; chunk < region_end;)
            {
                const SIZE_T requested = static_cast<SIZE_T>(std::min(
                    static_cast<std::uintptr_t>(buffer.size()),
                    region_end - chunk));
                SIZE_T bytes_read = 0;
                if (ReadProcessMemory(
                        GetCurrentProcess(),
                        reinterpret_cast<const void*>(chunk), buffer.data(),
                        requested, &bytes_read))
                {
                    for (SIZE_T offset = 0;
                         offset + sizeof(expected_vtable) <= bytes_read;
                         offset += alignof(std::uintptr_t))
                    {
                        std::uintptr_t value = 0;
                        std::memcpy(
                            &value, buffer.data() + offset, sizeof(value));
                        if (value != expected_vtable)
                            continue;

                        if (chunk + offset < kUiCoreVtableOffset)
                            continue;
                        const auto candidate_address =
                            chunk + offset - kUiCoreVtableOffset;
                        std::array<std::byte, kScaleOffset + sizeof(float) * 2>
                            object{};
                        SIZE_T object_bytes_read = 0;
                        if (!ReadProcessMemory(
                                GetCurrentProcess(),
                                reinterpret_cast<const void*>(
                                    candidate_address),
                                object.data(), object.size(),
                                &object_bytes_read) ||
                            object_bytes_read != object.size())
                        {
                            continue;
                        }

                        float scale_x = 0.0F;
                        float scale_y = 0.0F;
                        std::memcpy(
                            &scale_x, object.data() + kScaleOffset,
                            sizeof(scale_x));
                        std::memcpy(
                            &scale_y,
                            object.data() + kScaleOffset + sizeof(float),
                            sizeof(scale_y));
                        if (std::isfinite(scale_x) &&
                            std::isfinite(scale_y) &&
                            scale_x > 0.1F && scale_x < 16.0F &&
                            scale_y > 0.1F && scale_y < 16.0F)
                        {
                            candidates.push_back(
                                reinterpret_cast<void*>(candidate_address));
                        }
                    }
                }
                chunk += requested;
            }
        }

        if (region_end <= address)
            break;
        address = region_end;
    }

    if (candidate_count != nullptr)
        *candidate_count = candidates.size();
    if (candidates.size() != 1)
        return nullptr;
    return candidates.front();
}

void set_scissor_hook(void* self, const void* rect)
{
    if (rect == nullptr || g_native_weapon_ui)
    {
        g_set_scissor(self, rect);
        return;
    }

    IntRect adjusted = *static_cast<const IntRect*>(rect);
    const float offset = g_offset_x.load();
    const float ratio = g_horizontal_ratio.load();
    adjusted.x1 = static_cast<std::int32_t>(
        std::lround(offset + static_cast<float>(adjusted.x1) * ratio));
    adjusted.x2 = static_cast<std::int32_t>(
        std::lround(offset + static_cast<float>(adjusted.x2) * ratio));
    g_set_scissor(self, &adjusted);
}

void push_point_hook(
    void* self, float x, const float y, const float z,
    const std::uint32_t color, const float u, const float v)
{
    if (g_native_weapon_ui)
    {
        g_push_point(self, x, y, z, color, u, v);
        return;
    }

    if (g_preserve_local_width)
    {
        g_buffered_points.push_back({x, y, z, color, u, v});
        return;
    }

    if (g_point_type == kPointTypeTl && !g_crosshair_primitive)
        x = g_offset_x.load() + x * g_horizontal_ratio.load();
    g_push_point(self, x, y, z, color, u, v);
}

void start_primitive_hook(
    void* self, const std::uint32_t max_vertices,
    const std::int32_t primitive_type, const std::int32_t point_type)
{
    g_point_type = point_type;
    if (g_native_weapon_ui)
    {
        g_crosshair_primitive = false;
        g_preserve_local_width = false;
        g_buffered_points.clear();
        g_start_primitive(self, max_vertices, primitive_type, point_type);
        return;
    }

    g_crosshair_primitive =
        point_type == kPointTypeTl &&
        primitive_type == kPrimitiveTypeLineList &&
        max_vertices == 10;
    g_preserve_local_width =
        point_type == kPointTypeTl &&
        primitive_type == kPrimitiveTypeTriList &&
        max_vertices == 32;
    g_buffered_points.clear();
    if (g_preserve_local_width)
        g_buffered_points.reserve(max_vertices);
    g_start_primitive(self, max_vertices, primitive_type, point_type);
}

void flush_primitive_hook(void* self)
{
    if (g_preserve_local_width && !g_buffered_points.empty())
    {
        const auto [minimum, maximum] = std::minmax_element(
            g_buffered_points.begin(), g_buffered_points.end(),
            [](const UiPoint& left, const UiPoint& right)
            {
                return left.x < right.x;
            });
        const float center_x = (minimum->x + maximum->x) * 0.5F;
        const float transformed_center =
            g_offset_x.load() + center_x * g_horizontal_ratio.load();
        for (const UiPoint& point : g_buffered_points)
        {
            const float x = transformed_center + point.x - center_x;
            g_push_point(
                self, x, point.y, point.z, point.color, point.u, point.v);
        }
    }

    g_flush_primitive(self);
    g_buffered_points.clear();
    g_crosshair_primitive = false;
    g_preserve_local_width = false;
}

void font_on_render_hook(void* self, void* owner)
{
    float* horizontal_scale = nullptr;
    float original_horizontal_scale = 0.0F;
    if (owner != nullptr)
    {
        auto owner_bytes = static_cast<std::byte*>(owner);
        horizontal_scale = reinterpret_cast<float*>(
            owner_bytes + kFontHorizontalScaleOffset);
        original_horizontal_scale = *horizontal_scale;
        const float ratio = g_horizontal_ratio.load();
        if (std::isfinite(original_horizontal_scale) &&
            original_horizontal_scale > 0.0F)
        {
            *horizontal_scale = original_horizontal_scale * ratio;
        }

        auto begin = *reinterpret_cast<std::byte**>(
            owner_bytes + kFontStringsBeginOffset);
        auto end = *reinterpret_cast<std::byte**>(
            owner_bytes + kFontStringsEndOffset);

        if (begin != nullptr && end >= begin)
        {
            const std::size_t byte_count =
                static_cast<std::size_t>(end - begin);
            if (byte_count % kFontStringStride == 0 &&
                byte_count / kFontStringStride < 10'000)
            {
                const float offset_x = g_offset_x.load();
                for (std::byte* item = begin; item < end;
                     item += kFontStringStride)
                {
                    auto& x = *reinterpret_cast<float*>(
                        item + kFontStringXOffset);
                    x = offset_x + x * ratio;
                }
            }
        }
    }

    g_font_on_render(self, owner);
    if (horizontal_scale != nullptr &&
        std::isfinite(original_horizontal_scale) &&
        original_horizontal_scale > 0.0F)
    {
        *horizontal_scale = original_horizontal_scale;
    }
}

void cursor_update_hook(void* self)
{
    g_cursor_update(self);

    // Controller mode updates the logical UI cursor through a separate path.
    if (g_input_mode != nullptr && *g_input_mode == 2)
    {
        if (!g_controller_cursor_path_logged.exchange(true))
        {
            cs4x3ui::log::write(
                "Input diagnostic: controller logical cursor path active");
        }
        return;
    }

    auto& position = *reinterpret_cast<Float2*>(
        static_cast<std::byte*>(self) + 0x14);
    const float screen_width = g_screen_width.load();
    const float safe_width = g_safe_width.load();
    if (screen_width <= 0.0F || safe_width <= 0.0F)
        return;

    if (!g_mouse_cursor_path_logged.exchange(true))
    {
        cs4x3ui::log::write(
            "Input diagnostic: physical mouse safe-zone mapping active");
    }
    const float physical_x = position.x * screen_width / kUiWidth;
    position.x = std::clamp(
        (physical_x - g_offset_x.load()) * kUiWidth / safe_width,
        0.0F, kUiWidth);
}

void weapon_render_item_ui_hook(void* self)
{
    const bool previous = g_native_weapon_ui;
    g_native_weapon_ui = true;
    g_weapon_render_item_ui(self);
    g_native_weapon_ui = previous;
}

template <typename Function>
bool create_hook(
    const char* name, void* target, void* detour, Function& original)
{
    const MH_STATUS status = MH_CreateHook(
        target, detour, reinterpret_cast<void**>(&original));
    if (status != MH_OK)
    {
        cs4x3ui::log::write(std::format(
            "Fail-safe: unable to create '{}' hook ({})",
            name, static_cast<int>(status)));
        return false;
    }
    return true;
}
} // namespace

namespace cs4x3ui
{
bool install_runtime_hooks()
{
    if (g_initialized)
        return true;

    const HMODULE executable = GetModuleHandleW(nullptr);
    log::write(std::format(
        "Runtime executable base: 0x{:X}",
        reinterpret_cast<std::uintptr_t>(executable)));
    const auto resolve = [executable](
                             const Signature& signature,
                             std::size_t& count) -> std::byte*
    {
        return find_signature(executable, signature, &count);
    };

    constexpr DWORD kReadinessPollMs = 50;
    constexpr DWORD kReadinessTimeoutMs = 60'000;
    const auto expected_push_point =
        reinterpret_cast<const std::uint8_t*>(executable) +
        kPushPointExpectedRva;
    std::array<std::uint8_t, 4> initial_bytes{};
    SIZE_T initial_bytes_read = 0;
    const BOOL initial_read = ReadProcessMemory(
        GetCurrentProcess(), expected_push_point, initial_bytes.data(),
        initial_bytes.size(), &initial_bytes_read);
    log::write(std::format(
        "Waiting for runtime image at RVA 0x{:X} "
        "(read={}, bytes={}, observed={:02X}-{:02X}-{:02X}-{:02X})",
        kPushPointExpectedRva, initial_read != FALSE, initial_bytes_read,
        initial_bytes[0], initial_bytes[1], initial_bytes[2],
        initial_bytes[3]));
    bool image_ready = false;
    DWORD readiness_elapsed = 0;
    for (; readiness_elapsed <= kReadinessTimeoutMs;
         readiness_elapsed += kReadinessPollMs)
    {
        if (process_bytes_match(expected_push_point, kPushPointBytes))
        {
            image_ready = true;
            break;
        }
        Sleep(kReadinessPollMs);
    }

    if (!image_ready)
    {
        std::array<std::uint8_t, kPushPointBytes.size()> observed{};
        SIZE_T bytes_read = 0;
        const BOOL read = ReadProcessMemory(
            GetCurrentProcess(), expected_push_point, observed.data(),
            observed.size(), &bytes_read);
        log::write(std::format(
            "Fail-safe: runtime image readiness timed out "
            "(read={}, bytes={}, observed={:02X}-{:02X}-{:02X}-{:02X})",
            read != FALSE, bytes_read, observed[0], observed[1],
            observed[2], observed[3]));
        return false;
    }

    const auto input_mode_check =
        reinterpret_cast<const std::uint8_t*>(executable) +
        kInputModeCheckRva;
    if (!process_bytes_match(input_mode_check, kInputModeCheckBytes))
    {
        log::write(
            "Fail-safe: controller input-mode check does not match "
            "the supported build");
        return false;
    }
    g_input_mode = reinterpret_cast<const std::int32_t*>(
        reinterpret_cast<const std::byte*>(executable) + kInputModeRva);

    log::write(std::format(
        "Runtime image ready after {} ms", readiness_elapsed));

    std::array<std::size_t, 6> counts{};
    std::byte* const on_device_reset = resolve(
        {kOnDeviceResetBytes, "xxxxxxxxxxxxxxxxxxxx"}, counts[0]);
    std::byte* const ui_core_constructor = resolve(
        {kUiCoreConstructorBytes, "xxxxxxxxxxxxxxxxxxxx"}, counts[1]);
    std::byte* const set_scissor = resolve(
        {kSetScissorBytes, "xxxxxxxxxxxxxxxxxxxx"}, counts[2]);
    std::byte* const push_point = resolve(
        {kPushPointBytes, "xxxxxxxxxxxxxxxxxxxx"}, counts[3]);
    std::byte* const start_primitive = resolve(
        {kStartPrimitiveBytes, "xxxxxxxxxxxxxxxxxxxx"}, counts[4]);
    std::byte* const flush_primitive = resolve(
        {kFlushPrimitiveBytes, "xxxxxxxxxxxxxxxxxxxx"}, counts[5]);
    std::size_t font_count = 0;
    std::byte* const font_on_render = resolve(
        {kFontOnRenderBytes, "xxxxxxxxxxxxxxxxxxxx"}, font_count);
    std::size_t cursor_update_count = 0;
    std::byte* const cursor_update = resolve(
        {kCursorUpdateBytes, "xxxxxxxxxxxxxxxxxxxx"}, cursor_update_count);
    std::size_t weapon_render_item_ui_count = 0;
    std::byte* const weapon_render_item_ui = resolve(
        {kWeaponRenderItemUiBytes, "xxxxxxxxxxxxxxxxxxxx"},
        weapon_render_item_ui_count);

    if (on_device_reset == nullptr || ui_core_constructor == nullptr ||
        set_scissor == nullptr || push_point == nullptr ||
        start_primitive == nullptr || flush_primitive == nullptr ||
        font_on_render == nullptr ||
        cursor_update == nullptr ||
        weapon_render_item_ui == nullptr)
    {
        constexpr std::array<std::string_view, 6> names{
            "UICore::OnDeviceReset", "UICore::UICore",
            "dxUIRender::SetScissor", "dxUIRender::PushPoint",
            "dxUIRender::StartPrimitive", "dxUIRender::FlushPrimitive"};
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            if (counts[index] != 1)
            {
                log::write(std::format(
                    "Fail-safe: signature '{}' has {} matches after timeout",
                    names[index], counts[index]));
            }
        }
        if (font_count != 1)
        {
            log::write(std::format(
                "Fail-safe: signature 'dxFontRender::OnRender' has {} matches after timeout",
                font_count));
        }
        if (cursor_update_count != 1)
        {
            log::write(std::format(
                "Fail-safe: signature 'CUICursor::UpdateCursorPosition' has {} matches",
                cursor_update_count));
        }
        if (weapon_render_item_ui_count != 1)
        {
            log::write(std::format(
                "Fail-safe: signature 'CWeapon::render_item_ui' has {} matches",
                weapon_render_item_ui_count));
        }
        return false;
    }

    if (MH_Initialize() != MH_OK)
    {
        log::write("Fail-safe: MinHook initialization failed");
        return false;
    }

    const bool created =
        create_hook(
            "UICore::OnDeviceReset", on_device_reset,
            reinterpret_cast<void*>(&on_device_reset_hook), g_on_device_reset) &&
        create_hook(
            "UICore::UICore", ui_core_constructor,
            reinterpret_cast<void*>(&ui_core_constructor_hook),
            g_ui_core_constructor) &&
        create_hook(
            "dxUIRender::SetScissor", set_scissor,
            reinterpret_cast<void*>(&set_scissor_hook), g_set_scissor) &&
        create_hook(
            "dxUIRender::PushPoint", push_point,
            reinterpret_cast<void*>(&push_point_hook), g_push_point) &&
        create_hook(
            "dxUIRender::StartPrimitive", start_primitive,
            reinterpret_cast<void*>(&start_primitive_hook), g_start_primitive) &&
        create_hook(
            "dxUIRender::FlushPrimitive", flush_primitive,
            reinterpret_cast<void*>(&flush_primitive_hook), g_flush_primitive) &&
        create_hook(
            "dxFontRender::OnRender", font_on_render,
            reinterpret_cast<void*>(&font_on_render_hook), g_font_on_render) &&
        create_hook(
            "CUICursor::UpdateCursorPosition", cursor_update,
            reinterpret_cast<void*>(&cursor_update_hook), g_cursor_update) &&
        create_hook(
            "CWeapon::render_item_ui", weapon_render_item_ui,
            reinterpret_cast<void*>(&weapon_render_item_ui_hook),
            g_weapon_render_item_ui);

    if (!created || MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        log::write("Fail-safe: hook transaction rolled back");
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        return false;
    }

    g_initialized = true;
    log::write("Runtime hooks installed");

    std::size_t candidate_count = 0;
    if (g_ui_core.load() == nullptr)
    {
        if (void* const ui_core =
                find_ui_core(executable, &candidate_count);
            ui_core != nullptr)
        {
            activate_ui_core(ui_core);
        }
    }

    if (g_ui_core.load() == nullptr)
    {
        log::write(std::format(
            "UICore bootstrap pending constructor ({} candidates)",
            candidate_count));
    }
    return true;
}

void remove_runtime_hooks() noexcept
{
    if (!g_initialized)
        return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    g_initialized = false;
}
} // namespace cs4x3ui
