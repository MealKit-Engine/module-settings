#pragma once

#include "mealkit/core/Reflect.h"
#include "mealkit/core/Types.h"

#include <string>

namespace mealkit {
class Input;
class Window;
class FrameLimiter;
namespace render {
class Renderer;
}
namespace audio {
class Engine;
}
namespace feedback {
class Player;
}
namespace loc {
class Strings;
}
namespace ui {
class System;
}
namespace input {
class Actions;
}
} // namespace mealkit

// Player settings (ROADMAP stage 1 "Settings system + standard settings screen"): display, sound, controls, gameplay.
//   The field declarations are the schema. The file is settings.ini in the user folder ("path value" lines, game::userFolder), unrelated to the content hash.
//   The engine opens the standard settings screen assets/ui/settings.ui (the "settings.open" action on a button in any .ui; games use ctx.settings.open()).
//   Item rows are filled from these declarations via list bindings. Changing resolution/window mode reverts unless confirmed within 15 seconds.
// The simulation doesn't read settings (SimCheck blocks including mealkit/settings/). Sensitivity/invert are applied in input actions before quantization.
namespace mealkit::settings {

MEALKIT_ENUM(WindowModeSetting, u8, Windowed, Borderless, Fullscreen);
MEALKIT_ENUM(GraphicsPreset, u8, Low, Medium, High);

// Field descriptions are editor text (English, ko.txt). Game screen text uses the string table "settings.<path>"
MEALKIT_STRUCT(DisplaySettings) {
    MEALKIT_FIELD(WindowModeSetting, windowMode, WindowModeSetting::Windowed, "Window mode");
    MEALKIT_FIELD(u32, width, 1280, "Window width");
    MEALKIT_FIELD(u32, height, 720, "Window height");
    MEALKIT_FIELD(bool, vsync, true, "Vertical sync");
    MEALKIT_FIELD(u32, fpsLimit, 0, "Frame limit (0 is none)");
    MEALKIT_FIELD(f32, renderScale, 1.0f, "Render scale");
    MEALKIT_FIELD(GraphicsPreset, preset, GraphicsPreset::Low, "Graphics preset");
    MEALKIT_FIELD(f32, fov, 60.0f, "Field of view (degrees)");
    MEALKIT_FIELD(f32, brightness, 1.0f, "Brightness");
    // Disabling post effects (render::PostOptions, before the scene's values)
    MEALKIT_FIELD(bool, bloom, true, "Bloom");
    MEALKIT_FIELD(bool, antiAliasing, true, "Anti-aliasing (FXAA)");
    MEALKIT_FIELD(bool, filmGrain, true, "Film grain");
    MEALKIT_FIELD(bool, chromaticAberration, true, "Chromatic aberration");
};

MEALKIT_ENUM(ColorBlindSetting, u8, Off, Protanopia, Deuteranopia, Tritanopia); // render::ColorBlindMode order

MEALKIT_STRUCT(AudioSettings) {
    MEALKIT_FIELD(f32, master, 1.0f, "Master volume");
    MEALKIT_FIELD(f32, music, 0.8f, "Music volume");
    MEALKIT_FIELD(f32, sfx, 1.0f, "Effects volume");
    MEALKIT_FIELD(f32, ui, 1.0f, "Interface volume");
    MEALKIT_FIELD(f32, voice, 1.0f, "Voice volume");
    MEALKIT_FIELD(bool, muteUnfocused, true, "Mute when the window is in the background");
};

MEALKIT_STRUCT(ControlSettings) {
    MEALKIT_FIELD(f32, mouseSensitivity, 1.0f, "Mouse sensitivity");
    MEALKIT_FIELD(bool, invertY, false, "Invert mouse Y");
    MEALKIT_FIELD(f32, padSensitivity, 1.0f, "Gamepad sensitivity");
    MEALKIT_FIELD(bool, padInvertY, false, "Invert gamepad Y");
};

MEALKIT_STRUCT(GameplaySettings) {
    MEALKIT_FIELD(std::string, language, {}, "Language (empty is the project default)");
    MEALKIT_FIELD(f32, uiScale, 1.0f, "Interface scale");
    MEALKIT_FIELD(bool, subtitles, true, "Subtitles");
    // Accessibility: reduce or turn off the feedback table's shake/flash (screen color)/hitstop (view only, unrelated to the simulation)
    MEALKIT_FIELD(f32, shakeScale, 1.0f, "Screen shake");
    MEALKIT_FIELD(f32, flashScale, 1.0f, "Screen flashes");
    MEALKIT_FIELD(bool, hitStop, true, "Hit stop (brief screen freeze)");
    MEALKIT_FIELD(bool, showNetwork, true, "Show network status (ping, waiting for a player)");
    MEALKIT_FIELD(bool, crossplay, true, "Crossplay (play with other operating systems)");
    MEALKIT_FIELD(ColorBlindSetting, colorBlind, ColorBlindSetting::Off, "Color blindness correction"); // screen color LUT
};

MEALKIT_STRUCT(Settings) {
    MEALKIT_FIELD(DisplaySettings, display, {}, "Display");
    MEALKIT_FIELD(AudioSettings, audio, {}, "Audio");
    MEALKIT_FIELD(ControlSettings, controls, {}, "Controls");
    MEALKIT_FIELD(GameplaySettings, gameplay, {}, "Gameplay");
};

inline constexpr const char* kSettingsFile = "settings.ini";     // user folder
inline constexpr const char* kSettingsScreen = "ui/settings.ui"; // relative to assets
inline constexpr f64 kConfirmSeconds = 15.0;                       // display change confirmation

// Holds settings, reads/writes them, applies them to the engine, and opens the standard settings screen. The engine (shipping exe, editor play, join windows) makes one.
class Service {
public:
    struct Targets { // where to apply (missing ones are skipped)
        Window* window = nullptr; // nullptr in editor play (doesn't change the editor window's mode)
        render::Renderer* renderer = nullptr;
        FrameLimiter* limiter = nullptr;
        audio::Engine* audio = nullptr;
        feedback::Player* feedback = nullptr; // accessibility scales (shake/flash/hitstop)
        loc::Strings* strings = nullptr;
        ui::System* ui = nullptr;
        input::Actions* actions = nullptr;
        std::string projectLanguage = "ko";
    };

    Service();
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    // Reads the file (not saved if empty string) and applies everything
    void init(const Targets& targets, const std::string& filePath);
    const Settings& values() const;
    // Changes a value, applies, and saves (a confirmation window when display size/mode change)
    void set(const Settings& value);

    void open();  // standard settings screen
    void close();
    bool isOpen() const;
    // Every frame (after ui.update, before game update): settings screen actions, key rebinding input, confirmation timer
    void update(const Input* input, f64 dt);

private:
    struct Impl;
    Impl* m;
};

} // namespace mealkit::settings
