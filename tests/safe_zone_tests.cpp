#include "safe_zone.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
void expect_near(const float actual, const float expected, const char* message)
{
    if (std::abs(actual - expected) > 0.01F)
    {
        std::cerr << message << ": expected " << expected << ", got " << actual << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void verify(
    const float screen_width,
    const float screen_height,
    const float expected_width,
    const float expected_offset,
    const bool expected_active)
{
    const auto zone = cs4x3ui::make_safe_zone(screen_width, screen_height);
    if (zone.active != expected_active)
    {
        std::cerr << "active state: expected " << expected_active
                  << ", got " << zone.active << '\n';
        std::exit(EXIT_FAILURE);
    }
    expect_near(zone.width, expected_width, "safe width");
    expect_near(zone.height, screen_height, "safe height");
    expect_near(zone.offset_x, expected_offset, "horizontal offset");
    expect_near(zone.ui_to_screen_x(0.0F), expected_offset, "left edge");
    expect_near(
        zone.ui_to_screen_x(1024.0F), expected_offset + expected_width, "right edge");
    expect_near(zone.screen_to_ui_x(expected_offset), 0.0F, "inverse left edge");
    expect_near(
        zone.screen_to_ui_x(expected_offset + expected_width), 1024.0F, "inverse right edge");

    for (const float ui_x : {0.0F, 128.0F, 512.0F, 896.0F, 1024.0F})
    {
        expect_near(
            zone.screen_to_ui_x(zone.ui_to_screen_x(ui_x)),
            ui_x,
            "horizontal round trip");

    }

    for (const float ui_y : {0.0F, 96.0F, 384.0F, 672.0F, 768.0F})
    {
        expect_near(
            zone.screen_to_ui_y(zone.ui_to_screen_y(ui_y)),
            ui_y,
            "vertical round trip");
    }

    expect_near(
        zone.screen_to_ui_x(expected_offset - 100.0F),
        0.0F,
        "left clamp");
    expect_near(
        zone.screen_to_ui_x(expected_offset + expected_width + 100.0F),
        1024.0F,
        "right clamp");
    expect_near(zone.screen_to_ui_y(-100.0F), 0.0F, "top clamp");
    expect_near(zone.screen_to_ui_y(screen_height + 100.0F), 768.0F, "bottom clamp");
}
} // namespace

int main()
{
    verify(1024.0F, 768.0F, 1024.0F, 0.0F, false);
    verify(1920.0F, 1200.0F, 1920.0F, 0.0F, false);
    verify(1920.0F, 1080.0F, 1728.0F, 96.0F, true);
    verify(2560.0F, 1080.0F, 1728.0F, 416.0F, true);
    verify(3440.0F, 1440.0F, 2304.0F, 568.0F, true);
    verify(3840.0F, 1600.0F, 2560.0F, 640.0F, true);
    verify(5120.0F, 1440.0F, 2304.0F, 1408.0F, true);

    const auto invalid_width = cs4x3ui::make_safe_zone(0.0F, 1080.0F);
    const auto invalid_height = cs4x3ui::make_safe_zone(1920.0F, -1.0F);
    expect_near(invalid_width.width, 0.0F, "invalid width");
    expect_near(invalid_height.height, 0.0F, "invalid height");
    return EXIT_SUCCESS;
}
