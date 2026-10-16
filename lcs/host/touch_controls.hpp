#pragma once

#include "display_window.hpp"

#include <cstdint>

namespace lcs {

// On-screen controls for touch devices.  Fingers on the left half drive the
// move stick, fingers on the right half drive the camera stick, and the fixed
// buttons along the bottom edge map to the same PSP buttons the keyboard and
// gamepad paths produce.
//
// The module is fed raw touch events by the display backend and queried once
// per host input poll, so it stays independent of SDL and of the renderer.

void lcs_touch_note_surface(std::uint32_t width, std::uint32_t height) noexcept;

// finger_id: -1 lifts the touch.
void lcs_touch_note_finger(std::int64_t finger_id, float x, float y, bool down) noexcept;

// Merges the current touch state into a host input poll.  Buttons and sticks
// from other sources win; touch only fills what they leave alone.
void lcs_touch_merge_input(HostInputState &input) noexcept;

// True while any control is visible, i.e. the overlay should be drawn.
[[nodiscard]] bool lcs_touch_overlay_active() noexcept;

// Writes the control overlay as RGBA8888 covering the whole surface.
void lcs_touch_rasterize_overlay(std::uint8_t *rgba, std::uint32_t width,
                                 std::uint32_t height) noexcept;

} // namespace lcs