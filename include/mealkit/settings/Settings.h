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

// 플레이어 설정 (ROADMAP 1단계 "설정 시스템 + 표준 설정 화면"): 화면·소리·조작·게임플레이.
//   필드 선언이 스키마다. 파일은 사용자 폴더의 settings.ini("경로 값" 줄, game::userFolder), 콘텐츠 해시와 무관.
//   표준 설정 화면 assets/ui/settings.ui를 엔진이 연다(어느 .ui의 버튼이든 "settings.open" 액션, 게임은 ctx.settings.open()).
//   항목 줄은 이 선언에서 목록 바인딩으로 채운다. 해상도·창 모드를 바꾸면 15초 안에 확인하지 않으면 되돌린다.
// 시뮬레이션은 설정을 읽지 않는다 (SimCheck가 mealkit/settings/ include를 막는다). 감도·반전은 입력 액션에서 양자화 전에 적용한다.
namespace mealkit::settings {

MEALKIT_ENUM(WindowModeSetting, u8, Windowed, Borderless, Fullscreen);
MEALKIT_ENUM(GraphicsPreset, u8, Low, Medium, High);

// 필드 설명은 에디터 글자(영어, ko.txt). 게임 화면의 글자는 문자열 표 "settings.<경로>"
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
    // 포스트 효과 끄기 (render::PostOptions, 씬의 값보다 먼저)
    MEALKIT_FIELD(bool, bloom, true, "Bloom");
    MEALKIT_FIELD(bool, antiAliasing, true, "Anti-aliasing (FXAA)");
    MEALKIT_FIELD(bool, filmGrain, true, "Film grain");
    MEALKIT_FIELD(bool, chromaticAberration, true, "Chromatic aberration");
};

MEALKIT_ENUM(ColorBlindSetting, u8, Off, Protanopia, Deuteranopia, Tritanopia); // render::ColorBlindMode 순서

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
    // 접근성: 연출 표의 흔들림·번쩍임(화면 색)·화면 정지를 줄이거나 끈다 (화면만, 시뮬레이션과 무관)
    MEALKIT_FIELD(f32, shakeScale, 1.0f, "Screen shake");
    MEALKIT_FIELD(f32, flashScale, 1.0f, "Screen flashes");
    MEALKIT_FIELD(bool, hitStop, true, "Hit stop (brief screen freeze)");
    MEALKIT_FIELD(bool, showNetwork, true, "Show network status (ping, waiting for a player)");
    MEALKIT_FIELD(ColorBlindSetting, colorBlind, ColorBlindSetting::Off, "Color blindness correction"); // 화면 색 LUT
};

MEALKIT_STRUCT(Settings) {
    MEALKIT_FIELD(DisplaySettings, display, {}, "Display");
    MEALKIT_FIELD(AudioSettings, audio, {}, "Audio");
    MEALKIT_FIELD(ControlSettings, controls, {}, "Controls");
    MEALKIT_FIELD(GameplaySettings, gameplay, {}, "Gameplay");
};

inline constexpr const char* kSettingsFile = "settings.ini";     // 사용자 폴더
inline constexpr const char* kSettingsScreen = "ui/settings.ui"; // assets 기준
inline constexpr f64 kConfirmSeconds = 15.0;                       // 화면 변경 확인

// 설정을 갖고, 읽고 쓰고, 엔진에 적용하고, 표준 설정 화면을 연다. 엔진(출시 exe, 에디터 플레이, 참가 창)이 하나 만든다.
class Service {
public:
    struct Targets { // 적용할 곳 (없는 것은 건너뛴다)
        Window* window = nullptr; // 에디터 플레이에서는 nullptr (에디터 창의 모드를 바꾸지 않는다)
        render::Renderer* renderer = nullptr;
        FrameLimiter* limiter = nullptr;
        audio::Engine* audio = nullptr;
        feedback::Player* feedback = nullptr; // 접근성 배율 (흔들림·번쩍임·화면 정지)
        loc::Strings* strings = nullptr;
        ui::System* ui = nullptr;
        input::Actions* actions = nullptr;
        std::string projectLanguage = "ko";
    };

    Service();
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    // 파일(빈 문자열이면 저장하지 않는다)을 읽고 모두 적용한다
    void init(const Targets& targets, const std::string& filePath);
    const Settings& values() const;
    // 값을 바꾸고 적용·저장한다 (화면 크기·모드가 바뀌면 확인 창)
    void set(const Settings& value);

    void open();  // 표준 설정 화면
    void close();
    bool isOpen() const;
    // 매 프레임 (ui.update 다음, 게임 update 전): 설정 화면 액션, 키 바꾸기 입력, 확인 시간
    void update(const Input* input, f64 dt);

private:
    struct Impl;
    Impl* m;
};

} // namespace mealkit::settings
