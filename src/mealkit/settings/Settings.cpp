#include "mealkit/settings/Settings.h"

#include "mealkit/audio/Audio.h"
#include "mealkit/core/Clock.h"
#include "mealkit/core/Log.h"
#include "mealkit/input/Actions.h"
#include "mealkit/loc/Strings.h"
#include "mealkit/platform/Input.h"
#include "mealkit/platform/Paths.h"
#include "mealkit/platform/Window.h"
#include "mealkit/render/Renderer.h"
#include "mealkit/ui/Ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mealkit::settings {

// 표준 설정 화면의 뷰 모델 (assets/ui/settings.ui가 바인딩한다)
MEALKIT_STRUCT(SettingsTabVm) {
    MEALKIT_FIELD(std::string, label, {}, "Name");
    MEALKIT_FIELD(bool, active, false, "Selected");
};

MEALKIT_STRUCT(SettingsRowVm) {
    MEALKIT_FIELD(std::string, label, {}, "Name");
    MEALKIT_FIELD(std::string, value, {}, "Value text");
    MEALKIT_FIELD(bool, on, false, "On (toggle)");
    MEALKIT_FIELD(f32, value01, 0.0f, "Value (slider, 0 to 1)");
    MEALKIT_FIELD(bool, isToggle, false, "Toggle row");
    MEALKIT_FIELD(bool, isSlider, false, "Slider row");
    MEALKIT_FIELD(bool, isChoice, false, "Choice row");
    MEALKIT_FIELD(bool, isKey, false, "Key row");
    MEALKIT_FIELD(bool, warning, false, "Warning");
};

MEALKIT_VIEWMODEL(SettingsVm) {
    MEALKIT_LIST(SettingsTabVm, tabs, 8, "Tabs");
    MEALKIT_LIST(SettingsRowVm, rows, 40, "Rows");
    MEALKIT_FIELD(bool, confirm, false, "Display change waits for confirmation");
    MEALKIT_FIELD(std::string, confirmText, {}, "Confirmation text");
};

namespace {

enum class RowKind : u8 { Toggle, Slider, Choice, Key };

struct RowDef {
    const char* path; // 설정 경로 ("display.vsync"), 해상도는 "display.resolution"
    RowKind kind;
    f32 min = 0, max = 1, step = 0.05f;
    bool percent = false;
};

constexpr const char* kTabs[] = {"display", "audio", "controls", "keys", "gameplay"};
constexpr int kKeysTab = 3;

const RowDef kDisplayRows[] = {
    {"display.windowMode", RowKind::Choice},
    {"display.resolution", RowKind::Choice},
    {"display.vsync", RowKind::Toggle},
    {"display.fpsLimit", RowKind::Choice},
    {"display.renderScale", RowKind::Slider, 0.5f, 1.0f, 0.05f, true},
    {"display.preset", RowKind::Choice},
    {"display.fov", RowKind::Slider, 50.0f, 110.0f, 1.0f},
    {"display.brightness", RowKind::Slider, 0.5f, 1.5f, 0.05f, true},
};
const RowDef kAudioRows[] = {
    {"audio.master", RowKind::Slider, 0, 1, 0.05f, true}, {"audio.music", RowKind::Slider, 0, 1, 0.05f, true},
    {"audio.sfx", RowKind::Slider, 0, 1, 0.05f, true},    {"audio.ui", RowKind::Slider, 0, 1, 0.05f, true},
    {"audio.voice", RowKind::Slider, 0, 1, 0.05f, true},  {"audio.muteUnfocused", RowKind::Toggle},
};
const RowDef kControlRows[] = {
    {"controls.mouseSensitivity", RowKind::Slider, 0.1f, 3.0f, 0.05f},
    {"controls.invertY", RowKind::Toggle},
    {"controls.padSensitivity", RowKind::Slider, 0.1f, 3.0f, 0.05f},
    {"controls.padInvertY", RowKind::Toggle},
};
const RowDef kGameplayRows[] = {
    {"gameplay.language", RowKind::Choice},
    {"gameplay.uiScale", RowKind::Slider, 0.5f, 2.0f, 0.05f, true},
    {"gameplay.subtitles", RowKind::Toggle},
    {"gameplay.showNetwork", RowKind::Toggle},
};
constexpr u32 kFpsChoices[] = {0, 30, 60, 120, 144, 240};

struct Row {
    RowDef def;
    i32 action = -1; // 키 줄: 입력 액션 번호
};

} // namespace

struct Service::Impl {
    Targets t;
    std::string file;
    Settings value, applied;
    bool open = false;
    ui::ScreenId screen = ui::kNoScreen;
    SettingsVm vm;
    int tab = 0;
    std::vector<Row> rows;
    // 화면 변경 확인
    bool confirming = false;
    f64 confirmLeft = 0.0;
    DisplaySettings previousDisplay;
    i32 rebinding = -1; // 키를 기다리는 줄

    const std::string& str(const std::string& key) const {
        static const std::string empty;
        return t.strings ? t.strings->get(key) : key.empty() ? empty : key;
    }
    std::string label(const std::string& key, const std::string& fallback) const {
        return t.strings && t.strings->has(key) ? t.strings->get(key) : fallback;
    }

    // ---------------------------------------------------------------- 적용·저장

    void applyDisplay(bool force) {
        const DisplaySettings& d = value.display;
        if (t.window && (force || d.windowMode != applied.display.windowMode || d.width != applied.display.width ||
                         d.height != applied.display.height))
            t.window->setMode(static_cast<WindowMode>(d.windowMode), d.width, d.height);
        if (t.renderer) t.renderer->setVsync(d.vsync);
        if (t.limiter) t.limiter->setTargetFps(static_cast<f64>(d.fpsLimit));
    }

    void apply(bool force) {
        applyDisplay(force);
        const AudioSettings& a = value.audio;
        if (t.audio) {
            t.audio->setBusVolume("master", a.master);
            t.audio->setBusVolume("music", a.music);
            t.audio->setBusVolume("sfx", a.sfx);
            t.audio->setBusVolume("ui", a.ui);
            t.audio->setBusVolume("voice", a.voice);
        }
        const GameplaySettings& g = value.gameplay;
        if (t.strings) {
            const std::string lang = g.language.empty() ? t.projectLanguage : g.language;
            if (lang != t.strings->language()) t.strings->load(lang, t.projectLanguage);
        }
        if (t.ui) t.ui->setScale(g.uiScale);
        const ControlSettings& c = value.controls;
        if (t.actions) t.actions->setLookSettings(c.mouseSensitivity, c.padSensitivity, c.invertY, c.padInvertY);
        applied = value;
    }

    void save() {
        if (file.empty()) return;
        createDirectories(parentDirectory(file));
        if (!reflect::saveTextFile(reflect::typeOf<Settings>(), &value, file, "# MealKit player settings (the game's settings screen writes this)"))
            MEALKIT_WARN("Settings: cannot write %s", file.c_str());
    }

    void change() {
        const bool displayChanged = value.display.windowMode != applied.display.windowMode ||
                                    value.display.width != applied.display.width || value.display.height != applied.display.height;
        if (displayChanged && t.window && !confirming) {
            confirming = true;
            confirmLeft = kConfirmSeconds;
            previousDisplay = applied.display;
        }
        apply(false);
        save();
    }

    // ---------------------------------------------------------------- 줄

    void buildRows() {
        rows.clear();
        auto add = [&](const RowDef* defs, size_t n) {
            for (size_t i = 0; i < n; ++i) rows.push_back({defs[i], -1});
        };
        switch (tab) {
        case 0: add(kDisplayRows, std::size(kDisplayRows)); break;
        case 1: add(kAudioRows, std::size(kAudioRows)); break;
        case 2: add(kControlRows, std::size(kControlRows)); break;
        case 4: add(kGameplayRows, std::size(kGameplayRows)); break;
        default:
            if (t.actions)
                for (u32 i = 0; i < t.actions->count(); ++i) {
                    const std::string& id = t.actions->id(i);
                    if (t.actions->binding(i).kind != input::ActionKind::Button || id.rfind("ui.", 0) == 0) continue;
                    rows.push_back({{"", RowKind::Key}, static_cast<i32>(i)});
                }
            break;
        }
    }

    bool resolve(const char* path, const reflect::ValueInfo*& v, void*& data) {
        return reflect::resolvePath(reflect::typeOf<Settings>(), &value, path, v, data);
    }

    std::vector<std::string> choices(const Row& r) {
        std::vector<std::string> out;
        const std::string path = r.def.path;
        if (path == "display.resolution") {
            if (t.window)
                for (const DisplaySize& s : t.window->displaySizes()) out.push_back(std::to_string(s.width) + " x " + std::to_string(s.height));
            if (out.empty()) out.push_back(std::to_string(value.display.width) + " x " + std::to_string(value.display.height));
        } else if (path == "display.fpsLimit") {
            for (u32 f : kFpsChoices) out.push_back(f == 0 ? str("settings.display.fpsLimit.none") : std::to_string(f));
        } else if (path == "gameplay.language") {
            if (t.strings)
                for (const std::string& lang : t.strings->languages()) out.push_back(label("language." + lang, lang));
        } else {
            const reflect::ValueInfo* v = nullptr;
            void* data = nullptr;
            if (resolve(r.def.path, v, data))
                for (const std::string& name : v->enumNames) out.push_back(label("settings." + path + "." + name, name));
        }
        return out;
    }

    int choiceIndex(const Row& r) {
        const std::string path = r.def.path;
        if (path == "display.resolution") {
            const std::string cur = std::to_string(value.display.width) + " x " + std::to_string(value.display.height);
            const std::vector<std::string> list = choices(r);
            for (size_t i = 0; i < list.size(); ++i)
                if (list[i] == cur) return static_cast<int>(i);
            return 0;
        }
        if (path == "display.fpsLimit") {
            for (size_t i = 0; i < std::size(kFpsChoices); ++i)
                if (kFpsChoices[i] == value.display.fpsLimit) return static_cast<int>(i);
            return 0;
        }
        if (path == "gameplay.language") {
            if (!t.strings) return 0;
            const std::vector<std::string> langs = t.strings->languages();
            const std::string cur = value.gameplay.language.empty() ? t.projectLanguage : value.gameplay.language;
            for (size_t i = 0; i < langs.size(); ++i)
                if (langs[i] == cur) return static_cast<int>(i);
            return 0;
        }
        const reflect::ValueInfo* v = nullptr;
        void* data = nullptr;
        return resolve(r.def.path, v, data) ? static_cast<int>(*static_cast<u8*>(data)) : 0;
    }

    void setChoice(const Row& r, int index) {
        const std::string path = r.def.path;
        const std::vector<std::string> list = choices(r);
        if (list.empty()) return;
        index = ((index % static_cast<int>(list.size())) + static_cast<int>(list.size())) % static_cast<int>(list.size());
        if (path == "display.resolution") {
            if (!t.window) return;
            const std::vector<DisplaySize> sizes = t.window->displaySizes();
            if (index < static_cast<int>(sizes.size())) {
                value.display.width = sizes[static_cast<size_t>(index)].width;
                value.display.height = sizes[static_cast<size_t>(index)].height;
            }
        } else if (path == "display.fpsLimit") {
            value.display.fpsLimit = kFpsChoices[static_cast<size_t>(index)];
        } else if (path == "gameplay.language") {
            const std::vector<std::string> langs = t.strings ? t.strings->languages() : std::vector<std::string>{};
            if (index < static_cast<int>(langs.size())) value.gameplay.language = langs[static_cast<size_t>(index)];
        } else {
            const reflect::ValueInfo* v = nullptr;
            void* data = nullptr;
            if (resolve(r.def.path, v, data)) *static_cast<u8*>(data) = static_cast<u8>(index);
        }
        change();
    }

    std::string keysText(const input::Action& a) const {
        return a.keys.empty() && a.mouse.empty() ? str("settings.keys.none") : a.keys + (a.mouse.empty() ? "" : (a.keys.empty() ? "" : " / ") + std::string("Mouse ") + a.mouse);
    }

    void fillVm() {
        vm.tabsCount = static_cast<u32>(std::size(kTabs));
        for (u32 i = 0; i < vm.tabsCount; ++i) {
            vm.tabs[i].label = str(std::string("settings.tab.") + kTabs[i]);
            vm.tabs[i].active = static_cast<int>(i) == tab;
        }
        vm.rowsCount = static_cast<u32>(std::min<size_t>(rows.size(), vm.rows.size()));
        for (u32 i = 0; i < vm.rowsCount; ++i) {
            const Row& r = rows[i];
            SettingsRowVm& row = vm.rows[i];
            row = {};
            row.isToggle = r.def.kind == RowKind::Toggle;
            row.isSlider = r.def.kind == RowKind::Slider;
            row.isChoice = r.def.kind == RowKind::Choice;
            row.isKey = r.def.kind == RowKind::Key;
            if (row.isKey) {
                const input::Action& a = t.actions->binding(static_cast<u32>(r.action));
                const std::string& id = t.actions->id(static_cast<u32>(r.action));
                row.label = label("action." + id, a.label.empty() ? id : a.label);
                row.value = rebinding == static_cast<i32>(i) ? str("settings.keys.press") : keysText(a);
                for (u32 j = 0; j < t.actions->count(); ++j) // 같은 키를 쓰는 다른 버튼 액션
                    if (static_cast<i32>(j) != r.action && !a.keys.empty() && t.actions->binding(j).keys == a.keys &&
                        t.actions->binding(j).kind == input::ActionKind::Button && t.actions->id(j).rfind("ui.", 0) != 0)
                        row.warning = true;
                if (row.warning) row.value += "  " + str("settings.keys.duplicate");
                continue;
            }
            row.label = str(std::string("settings.") + r.def.path);
            const reflect::ValueInfo* v = nullptr;
            void* data = nullptr;
            if (row.isToggle && resolve(r.def.path, v, data)) row.on = *static_cast<bool*>(data);
            if (row.isSlider && resolve(r.def.path, v, data)) {
                const f32 x = *static_cast<f32*>(data);
                row.value01 = std::clamp((x - r.def.min) / (r.def.max - r.def.min), 0.0f, 1.0f);
                char buf[32];
                if (r.def.percent) std::snprintf(buf, sizeof(buf), "%.0f%%", x * 100.0f);
                else if (r.def.step >= 1.0f) std::snprintf(buf, sizeof(buf), "%.0f", x);
                else std::snprintf(buf, sizeof(buf), "%.2f", x);
                row.value = buf;
            }
            if (row.isChoice) {
                const std::vector<std::string> list = choices(r);
                const int c = choiceIndex(r);
                row.value = c < static_cast<int>(list.size()) ? list[static_cast<size_t>(c)] : std::string();
            }
        }
        vm.confirm = confirming;
        vm.confirmText = t.strings ? t.strings->format("settings.confirm", {{"seconds", std::to_string(static_cast<int>(std::ceil(confirmLeft)))}}) : "";
    }

    void readRow(u32 index, const std::string& field) { // 위젯이 vm에 쓴 값 → 설정
        if (index >= rows.size()) return;
        const Row& r = rows[index];
        const SettingsRowVm& row = vm.rows[index];
        const reflect::ValueInfo* v = nullptr;
        void* data = nullptr;
        if (!resolve(r.def.path, v, data)) return;
        if (field == "on" && r.def.kind == RowKind::Toggle) *static_cast<bool*>(data) = row.on;
        if (field == "value01" && r.def.kind == RowKind::Slider) {
            f32 x = r.def.min + row.value01 * (r.def.max - r.def.min);
            if (r.def.step > 0) x = r.def.min + std::round((x - r.def.min) / r.def.step) * r.def.step;
            *static_cast<f32*>(data) = std::clamp(x, r.def.min, r.def.max);
        }
        change();
    }

    void resetTab() {
        const Settings defaults;
        switch (tab) {
        case 0: value.display = defaults.display; break;
        case 1: value.audio = defaults.audio; break;
        case 2: value.controls = defaults.controls; break;
        case 4: value.gameplay = defaults.gameplay; break;
        default:
            if (t.actions) {
                for (u32 i = 0; i < t.actions->count(); ++i) t.actions->resetBinding(t.actions->id(i));
                t.actions->saveUser();
            }
            break;
        }
        change();
    }

    void handle(const ui::Action& a) {
        const u32 index = a.listIndex >= 0 ? static_cast<u32>(a.listIndex) : 0u;
        if (a.name == "settings.close") {
            closeScreen();
        } else if (a.name == "settings.tab" && a.listIndex >= 0) {
            tab = std::clamp(a.listIndex, 0, static_cast<int>(std::size(kTabs)) - 1);
            rebinding = -1;
            buildRows();
        } else if (a.name == "settings.changed") {
            readRow(index, a.field);
        } else if ((a.name == "settings.prev" || a.name == "settings.next") && index < rows.size()) {
            setChoice(rows[index], choiceIndex(rows[index]) + (a.name == "settings.next" ? 1 : -1));
        } else if (a.name == "settings.rebind" && index < rows.size() && rows[index].def.kind == RowKind::Key) {
            rebinding = static_cast<i32>(index);
        } else if (a.name == "settings.reset") {
            resetTab();
        } else if (a.name == "settings.keep") {
            confirming = false;
        } else if (a.name == "settings.revert") {
            revert();
        }
    }

    void revert() {
        confirming = false;
        value.display = previousDisplay;
        apply(false);
        save();
    }

    void openScreen() {
        if (open || !t.ui) return;
        screen = t.ui->open(kSettingsScreen, 100);
        t.ui->bind(screen, vm);
        open = true;
        tab = 0;
        rebinding = -1;
        buildRows();
        fillVm();
    }

    void closeScreen() {
        if (!open) return;
        if (t.ui) t.ui->close(screen);
        screen = ui::kNoScreen;
        open = false;
        rebinding = -1;
    }
};

Service::Service() : m(new Impl()) {}
Service::~Service() { delete m; }

void Service::init(const Targets& targets, const std::string& filePath) {
    m->t = targets;
    m->file = filePath;
    m->value = {};
    if (!filePath.empty() && fileExists(filePath)) {
        std::string error;
        if (!reflect::loadTextFile(reflect::typeOf<Settings>(), &m->value, filePath, &error))
            MEALKIT_WARN("Settings: %s: %s", filePath.c_str(), error.c_str());
    }
    m->applied = m->value;
    m->apply(true);
}

const Settings& Service::values() const { return m->value; }

void Service::set(const Settings& value) {
    m->value = value;
    m->change();
}

void Service::open() { m->openScreen(); }
void Service::close() { m->closeScreen(); }
bool Service::isOpen() const { return m->open; }

void Service::update(const Input* input, f64 dt) {
    if (m->t.ui) {
        for (const ui::Action& a : m->t.ui->actions()) {
            if (a.name == "settings.open") m->openScreen();
            else if (m->open && a.screen == m->screen) m->handle(a);
        }
    }
    if (m->confirming) {
        m->confirmLeft -= dt;
        if (m->confirmLeft <= 0.0) m->revert(); // 확인하지 않으면 되돌린다
    }
    // 키 바꾸기: 다음에 누른 키 (Esc는 취소)
    if (m->rebinding >= 0 && input && m->t.actions && static_cast<size_t>(m->rebinding) < m->rows.size()) {
        for (u32 vk = 1; vk < 256; ++vk) {
            if (!input->keyPressed(static_cast<u8>(vk))) continue;
            if (vk == key::Escape || vk == key::Enter) break; // 메뉴 키는 바꾸지 않는다
            const char* name = input::keyName(static_cast<u8>(vk));
            if (!name) continue;
            const u32 index = static_cast<u32>(m->rows[static_cast<size_t>(m->rebinding)].action);
            input::Action b = m->t.actions->binding(index);
            b.keys = name;
            m->t.actions->setBinding(m->t.actions->id(index), b);
            m->t.actions->saveUser();
            m->rebinding = -1;
            break;
        }
        if (input->keyPressed(key::Escape)) m->rebinding = -1;
    }
    if (m->open) m->fillVm();
}

} // namespace mealkit::settings
