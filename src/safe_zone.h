#pragma once

namespace cs4x3ui
{
struct SafeZone
{
    float screen_width;
    float screen_height;
    float width;
    float height;
    float offset_x;
    float scale_x;
    float scale_y;
    bool active;

    [[nodiscard]] float ui_to_screen_x(float x) const noexcept;
    [[nodiscard]] float ui_to_screen_y(float y) const noexcept;
    [[nodiscard]] float screen_to_ui_x(float x) const noexcept;
    [[nodiscard]] float screen_to_ui_y(float y) const noexcept;
};

[[nodiscard]] SafeZone make_safe_zone(float screen_width, float screen_height) noexcept;
} // namespace cs4x3ui
