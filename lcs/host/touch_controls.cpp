#include "touch_controls.hpp"

#include "lcs_key_bindings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace lcs {
namespace {

struct TouchButton {
    const char *label;
    std::uint32_t button;
};

// Same face-button assignment the Xbox-layout pad uses, so muscle memory
// carries over between a pad and the screen.
constexpr std::array<TouchButton, 8> kButtons{{
    {"RUN", kPspCross},
    {"ATK", kPspCircle},
    {"JMP", kPspSquare},
    {"CAR", kPspTriangle},
    {"WPN-", kPspLeft},
    {"WPN+", kPspRight},
    {"PAUSE", kPspStart},
    {"CAM", kPspSelect},
}};

struct Finger {
    bool active{false};
    bool button{false};
    float x{};
    float y{};
    float origin_x{};
    float origin_y{};
};

constexpr int kMaxFingers = 10;
constexpr float kStickRadius = 96.0f;   // pixels at 1080p
constexpr float kStickDeadzone = 0.14f;
constexpr float kButtonRadius = 78.0f;

struct State {
    std::array<Finger, kMaxFingers> fingers{};
    float width{};
    float height{};
    float scale{1.0f};
    bool moved_this_frame{false};
};

State g_touch;

float scale_factor() noexcept {
    // Everything is authored against a 1080p-tall surface.
    return g_touch.height > 1.0f ? g_touch.height / 1080.0f : 1.0f;
}

float stick_radius() noexcept { return kStickRadius * scale_factor(); }
float button_radius() noexcept { return kButtonRadius * scale_factor(); }

// Button cluster occupies the bottom-right corner; two rows of four.
void button_center(int index, float &cx, float &cy) noexcept {
    const float radius = button_radius();
    const int column = index % 4;
    const int row = index / 4;
    const float margin = radius * 1.25f;
    cx = g_touch.width - margin - static_cast<float>(3 - column) * radius * 2.2f;
    cy = g_touch.height - margin - static_cast<float>(1 - row) * radius * 2.2f;
}

int button_at(float x, float y) noexcept {
    const float radius = button_radius();
    for (int index = 0; index < static_cast<int>(kButtons.size()); ++index) {
        float cx = 0.0f;
        float cy = 0.0f;
        button_center(index, cx, cy);
        if (std::hypot(x - cx, y - cy) <= radius) return index;
    }
    return -1;
}

[[nodiscard]] float clamp_unit(float value) noexcept {
    return std::clamp(value, -1.0f, 1.0f);
}

// Reduces a finger offset from its stick origin to an axis pair.
void stick_vector(const Finger &finger, float &out_x, float &out_y) noexcept {
    out_x = 0.0f;
    out_y = 0.0f;
    if (!finger.active || finger.button) return;
    const float radius = stick_radius();
    const float dx = finger.x - finger.origin_x;
    const float dy = finger.y - finger.origin_y;
    const float length = std::hypot(dx, dy);
    if (length <= 0.0001f) return;
    // A drag past the radius keeps tracking at full deflection instead of
    // feeling stuck at the edge.
    const float travel = std::min(length / radius, 1.0f);
    if (travel < kStickDeadzone) return;
    const float strength = (travel - kStickDeadzone) / (1.0f - kStickDeadzone);
    out_x = clamp_unit((dx / length) * strength);
    out_y = clamp_unit((dy / length) * strength);
}

} // namespace

void lcs_touch_note_surface(std::uint32_t width, std::uint32_t height) noexcept {
    g_touch.width = static_cast<float>(std::max(width, 1u));
    g_touch.height = static_cast<float>(std::max(height, 1u));
}

void lcs_touch_note_finger(std::int64_t finger_id, float x, float y, bool down) noexcept {
    if (finger_id < 0) {
        // Negative ids are SDL mouse touches; they have their own path.
        return;
    }
    if (finger_id >= kMaxFingers) return;
    Finger &finger = g_touch.fingers[static_cast<std::size_t>(finger_id)];
    if (down) {
        const int hit = button_at(x, y);
        finger.active = true;
        finger.button = hit >= 0;
        finger.x = x;
        finger.y = y;
        // Sticks are dynamic: the origin follows the first touch of the zone so
        // the player does not have to hit a fixed spot.
        finger.origin_x = x;
        finger.origin_y = y;
    } else {
        finger = Finger{};
    }
}

void lcs_touch_merge_input(HostInputState &input) noexcept {
    if (g_touch.width <= 0.0f || g_touch.height <= 0.0f) return;

    std::uint32_t buttons = 0u;
    float move_x = 0.0f;
    float move_y = 0.0f;
    float camera_x = 0.0f;
    float camera_y = 0.0f;
    bool any = false;

    for (const Finger &finger : g_touch.fingers) {
        if (!finger.active) continue;
        any = true;
        if (finger.button) {
            const int hit = button_at(finger.x, finger.y);
            if (hit >= 0) buttons |= kButtons[static_cast<std::size_t>(hit)].button;
            continue;
        }
        float x = 0.0f;
        float y = 0.0f;
        stick_vector(finger, x, y);
        if (finger.x < g_touch.width * 0.5f) {
            if (x != 0.0f || y != 0.0f) {
                move_x = x;
                move_y = y;
            }
        } else {
            camera_x = std::max(camera_x, std::abs(x)) * (x < 0.0f ? -1.0f : 1.0f);
            camera_y = std::max(camera_y, std::abs(y)) * (y < 0.0f ? -1.0f : 1.0f);
        }
    }
    if (!any) return;

    // Sticks only contribute when the keyboard/pad is idle, so both can be used
    // on the same device.
    if (input.analog_x == 128u && input.analog_y == 128u && (move_x != 0.0f || move_y != 0.0f)) {
        input.analog_x = static_cast<std::uint8_t>(std::clamp(128 + move_x * 127.0f, 0, 255));
        input.analog_y = static_cast<std::uint8_t>(std::clamp(128 + move_y * 127.0f, 0, 255));
    }
    if (input.camera_x == 0 && input.camera_y == 0 && (camera_x != 0.0f || camera_y != 0.0f)) {
        input.camera_x = static_cast<int>(camera_x * 127.0f);
        input.camera_y = static_cast<int>(-camera_y * 127.0f);
    }
    input.buttons |= buttons;
}

bool lcs_touch_overlay_active() noexcept {
    return g_touch.width > 0.0f && g_touch.height > 0.0f;
}

void lcs_touch_rasterize_overlay(std::uint8_t *rgba, std::uint32_t width,
                                 std::uint32_t height) noexcept {
    if (rgba == nullptr || width == 0u || height == 0u) return;
    if (g_touch.width <= 0.0f || g_touch.height <= 0.0f) return;
    // The overlay is authored for the surface it was sized from; skip if the
    // window changed under us rather than scaling.
    if (static_cast<std::uint32_t>(g_touch.width) != width ||
        static_cast<std::uint32_t>(g_touch.height) != height)
        return;

    // Transparent black everywhere, then paint controls with alpha.
    for (std::uint32_t i = 0; i < width * height; ++i) {
        rgba[i * 4 + 0] = 0u;
        rgba[i * 4 + 1] = 0u;
        rgba[i * 4 + 2] = 0u;
        rgba[i * 4 + 3] = 0u;
    }

    const auto blend = [&](float cx, float cy, float radius, std::uint8_t r, std::uint8_t g,
                           std::uint8_t b, float alpha) {
        const int x0 = std::max(0, static_cast<int>(cx - radius) - 2);
        const int y0 = std::max(0, static_cast<int>(cy - radius) - 2);
        const int x1 = std::min(static_cast<int>(width), static_cast<int>(cx + radius) + 2);
        const int y1 = std::min(static_cast<int>(height), static_cast<int>(cy + radius) + 2);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const float distance = std::hypot(static_cast<float>(x) + 0.5f - cx,
                                                  static_cast<float>(y) + 0.5f - cy);
                // One-pixel soft edge keeps the circles from looking jagged.
                const float coverage = std::clamp(radius - distance + 0.5f, 0.0f, 1.0f);
                if (coverage <= 0.0f) continue;
                std::uint8_t *pixel = rgba + (static_cast<std::size_t>(y) * width + x) * 4u;
                const float a = alpha * coverage;
                const float inv = 1.0f - a;
                pixel[0] = static_cast<std::uint8_t>(pixel[0] * inv + r * a);
                pixel[1] = static_cast<std::uint8_t>(pixel[1] * inv + g * a);
                pixel[2] = static_cast<std::uint8_t>(pixel[2] * inv + b * a);
                pixel[3] = static_cast<std::uint8_t>(std::min(255.0f, pixel[3] + 255.0f * a));
            }
        }
    };

    // Sticks: show every live stick, dimmed when idle.
    for (const Finger &finger : g_touch.fingers) {
        if (!finger.active || finger.button) continue;
        const float active = (finger.x != finger.origin_x || finger.y != finger.origin_y) ? 1.0f : 0.45f;
        blend(finger.origin_x, finger.origin_y, stick_radius(), 232, 248, 255, 0.16f * active);
        blend(finger.x, finger.y, stick_radius() * 0.38f, 232, 248, 255, 0.30f * active);
    }

    // Buttons, brightened while held.
    std::array<bool, kButtons.size()> held{};
    for (const Finger &finger : g_touch.fingers) {
        if (!finger.active || !finger.button) continue;
        const int hit = button_at(finger.x, finger.y);
        if (hit >= 0) held[static_cast<std::size_t>(hit)] = true;
    }
    for (std::size_t index = 0; index < kButtons.size(); ++index) {
        float cx = 0.0f;
        float cy = 0.0f;
        button_center(static_cast<int>(index), cx, cy);
        if (held[index]) {
            blend(cx, cy, button_radius(), 255, 214, 120, 0.55f);
        } else {
            blend(cx, cy, button_radius(), 210, 226, 246, 0.22f);
        }
    }
}

} // namespace lcs