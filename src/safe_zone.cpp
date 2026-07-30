#include "safe_zone.h"

#include <algorithm>

namespace
{
constexpr float kUiWidth = 1024.0F;
constexpr float kUiHeight = 768.0F;
constexpr float kTargetAspect = 16.0F / 10.0F;
} // namespace

namespace cs4x3ui
{
float SafeZone::ui_to_screen_x(const float x) const noexcept
{
    return offset_x + x * scale_x;
}

float SafeZone::ui_to_screen_y(const float y) const noexcept
{
    return y * scale_y;
}

float SafeZone::screen_to_ui_x(const float x) const noexcept
{
    return std::clamp((x - offset_x) / scale_x, 0.0F, kUiWidth);
}

float SafeZone::screen_to_ui_y(const float y) const noexcept
{
    return std::clamp(y / scale_y, 0.0F, kUiHeight);
}

SafeZone make_safe_zone(const float screen_width, const float screen_height) noexcept
{
    if (screen_width <= 0.0F || screen_height <= 0.0F)
        return {};

    const bool active = screen_width / screen_height > kTargetAspect;
    const float width = active ? screen_height * kTargetAspect : screen_width;
    const float height = screen_height;
    const float offset_x = (screen_width - width) * 0.5F;
    const float scale_x = width / kUiWidth;
    const float scale_y = height / kUiHeight;

    return {
        screen_width,
        screen_height,
        width,
        height,
        offset_x,
        scale_x,
        scale_y,
        active,
    };
}
} // namespace cs4x3ui
