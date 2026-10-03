#include "OptionsScreen.h"
#include "Gui.h"
#include "MenuScreen.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int BUTTON_WIDTH = 150, BUTTON_HEIGHT = 20;
constexpr int SLIDER_HANDLE = 8;
constexpr int TITLE_Y = 12;

std::string Percent(const char* name, float v) {
    if (v <= 0.0f) return std::string(name) + ": OFF";
    return std::string(name) + ": " + std::to_string((int)lroundf(v * 100.0f)) + "%";
}

// Two columns of 150-wide buttons, as GuiOptions lays them out.
int ColumnX(int i) { return -155 + (i % 2) * 160; }
int RowY(int row) { return -12 + 24 * row; }

} // namespace

OptionsScreen::OptionsScreen(const GuiFont& guiFont) : font(guiFont) {
    widgets = LoadPixelTexture("resources/textures/gui/widgets.png");
    dirt = LoadPixelTexture("resources/textures/gui/options_background.png");
    SetTextureWrap(dirt, TEXTURE_WRAP_REPEAT);
}

OptionsScreen::~OptionsScreen() {
    UnloadTexture(widgets);
    UnloadTexture(dirt);
}

void OptionsScreen::Open(bool fromWorld) {
    open = true;
    overWorld = fromWorld;
    page = Page::Main;
    dragging = -1;
}

int OptionsScreen::Scale() const {
    return GuiScale(GetScreenWidth(), GetScreenHeight());
}

Vector2 OptionsScreen::MouseGui() const {
    int s = Scale();
    Vector2 mouse = GetMousePosition();
    return Vector2{ mouse.x / s, mouse.y / s };
}

Rectangle OptionsScreen::Bounds(const Widget& w) const {
    int s = Scale();
    int guiWidth = (GetScreenWidth() + s - 1) / s, guiHeight = (GetScreenHeight() + s - 1) / s;
    return Rectangle{ (float)(guiWidth / 2 + w.x), (float)(guiHeight / 6 + w.y), (float)w.width, (float)BUTTON_HEIGHT };
}

std::string OptionsScreen::Title() const {
    switch (page) {
        case Page::Main: return "Options";
        case Page::Sounds: return "Music & Sound Options";
        case Page::Video: return "Video Settings";
        case Page::Controls: return "Controls";
    }
    return "";
}

std::vector<OptionsScreen::Widget> OptionsScreen::Widgets() const {
    std::vector<Widget> list;
    auto button = [&](int x, int y, int width, bool enabled, std::string text, int goTo = -1) {
        Widget w{ Kind::Button, x, y, width, enabled, [text](const GameOptions&) { return text; }, nullptr, nullptr };
        w.goTo = goTo;
        list.push_back(w);
    };
    auto done = [&]() {
        Widget w{ Kind::Button, -100, 168, 200, true, [](const GameOptions&) { return std::string("Done"); }, nullptr, nullptr };
        w.done = true;
        list.push_back(w);
    };

    switch (page) {
        case Page::Main: {
            list.push_back({ Kind::Slider, ColumnX(0), RowY(0), BUTTON_WIDTH, true,
                [](const GameOptions& o) {
                    int fov = (int)lroundf(o.fov);
                    if (fov == 70) return std::string("FOV: Normal");
                    if (fov == 110) return std::string("FOV: Quake Pro");
                    return "FOV: " + std::to_string(fov);
                },
                [](const GameOptions& o) { return (o.fov - 30.0f) / 80.0f; },
                [](GameOptions& o, float v) { o.fov = roundf(30.0f + v * 80.0f); } });
            list.push_back({ Kind::Button, ColumnX(1), RowY(0), BUTTON_WIDTH, true,
                [](const GameOptions& o) {
                    static const char* names[4] = { "Peaceful", "Easy", "Normal", "Hard" };
                    return std::string("Difficulty: ") + names[o.difficulty & 3];
                },
                nullptr, [](GameOptions& o, float) { o.difficulty = (o.difficulty + 1) % 4; } });
            button(ColumnX(0), 48 - 6, BUTTON_WIDTH, false, "Skin Customization...");
            button(ColumnX(1), 48 - 6, BUTTON_WIDTH, true, "Music & Sounds...", (int)Page::Sounds);
            button(ColumnX(0), 72 - 6, BUTTON_WIDTH, true, "Video Settings...", (int)Page::Video);
            button(ColumnX(1), 72 - 6, BUTTON_WIDTH, true, "Controls...", (int)Page::Controls);
            button(ColumnX(0), 96 - 6, BUTTON_WIDTH, false, "Language...");
            button(ColumnX(1), 96 - 6, BUTTON_WIDTH, false, "Chat Settings...");
            button(ColumnX(0), 120 - 6, BUTTON_WIDTH, false, "Resource Packs...");
            button(ColumnX(1), 120 - 6, BUTTON_WIDTH, false, "Snooper Settings...");
            done();
            break;
        }
        case Page::Sounds:
            list.push_back({ Kind::Slider, ColumnX(0), RowY(1), BUTTON_WIDTH, true,
                [](const GameOptions& o) { return Percent("Music", o.music); },
                [](const GameOptions& o) { return o.music; },
                [](GameOptions& o, float v) { o.music = roundf(v * 100.0f) / 100.0f; } });
            list.push_back({ Kind::Slider, ColumnX(1), RowY(1), BUTTON_WIDTH, true,
                [](const GameOptions& o) { return Percent("Sound", o.sound); },
                [](const GameOptions& o) { return o.sound; },
                [](GameOptions& o, float v) { o.sound = roundf(v * 100.0f) / 100.0f; } });
            done();
            break;
        case Page::Video:
            list.push_back({ Kind::Slider, ColumnX(0), RowY(1), BUTTON_WIDTH, true,
                [](const GameOptions& o) { return "Render Distance: " + std::to_string(o.renderDistance) + " chunks"; },
                [](const GameOptions& o) { return (o.renderDistance - 2) / 14.0f; },
                [](GameOptions& o, float v) { o.renderDistance = 2 + (int)lroundf(v * 14.0f); } });
            list.push_back({ Kind::Slider, ColumnX(1), RowY(1), BUTTON_WIDTH, true,
                [](const GameOptions& o) {
                    if (o.maxFramerate == 0) return std::string("Max Framerate: Unlimited");
                    return "Max Framerate: " + std::to_string(o.maxFramerate) + " fps";
                },
                [](const GameOptions& o) { return o.maxFramerate == 0 ? 1.0f : (o.maxFramerate - 10) / 250.0f; },
                [](GameOptions& o, float v) {
                    int fps = 10 + (int)lroundf(v * 25.0f) * 10;
                    o.maxFramerate = fps >= 260 ? 0 : fps;
                } });
            list.push_back({ Kind::Button, ColumnX(0), RowY(2), BUTTON_WIDTH, true,
                [](const GameOptions& o) { return std::string("View Bobbing: ") + (o.viewBobbing ? "ON" : "OFF"); },
                nullptr, [](GameOptions& o, float) { o.viewBobbing = !o.viewBobbing; } });
            list.push_back({ Kind::Button, ColumnX(1), RowY(2), BUTTON_WIDTH, true,
                [](const GameOptions& o) {
                    static const char* names[4] = { "Auto", "Small", "Normal", "Large" };
                    return std::string("GUI Scale: ") + names[o.guiScale & 3];
                },
                nullptr, [](GameOptions& o, float) { o.guiScale = (o.guiScale + 1) % 4; } });
            done();
            break;
        case Page::Controls:
            list.push_back({ Kind::Button, ColumnX(0), RowY(1), BUTTON_WIDTH, true,
                [](const GameOptions& o) { return std::string("Invert Mouse: ") + (o.invertMouse ? "ON" : "OFF"); },
                nullptr, [](GameOptions& o, float) { o.invertMouse = !o.invertMouse; } });
            list.push_back({ Kind::Slider, ColumnX(1), RowY(1), BUTTON_WIDTH, true,
                [](const GameOptions& o) {
                    if (o.sensitivity <= 0.0f) return std::string("Sensitivity: *yawn*");
                    if (o.sensitivity >= 1.0f) return std::string("Sensitivity: HYPERSPEED!!!");
                    return "Sensitivity: " + std::to_string((int)lroundf(o.sensitivity * 200.0f)) + "%";
                },
                [](const GameOptions& o) { return o.sensitivity; },
                [](GameOptions& o, float v) { o.sensitivity = v; } });
            done();
            break;
    }
    return list;
}

// Done (or Esc) on a sub-page goes back to Options; on Options it closes, saving.
void OptionsScreen::Back(GameOptions& options) {
    dragging = -1;
    if (page != Page::Main) {
        page = Page::Main;
        return;
    }
    options.Save();
    open = false;
}

bool OptionsScreen::Update(SoundSystem& sounds, GameOptions& options) {
    if (!open) return false;
    if (IsKeyPressed(KEY_ESCAPE)) {
        Back(options);
        return false;
    }
    std::vector<Widget> list = Widgets();
    Vector2 mouse = MouseGui();
    bool changed = false;

    if (dragging >= 0) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) || dragging >= (int)list.size()) {
            dragging = -1;
        } else {
            const Widget& w = list[dragging];
            Rectangle r = Bounds(w);
            float v = std::clamp((mouse.x - (r.x + SLIDER_HANDLE / 2.0f)) / (r.width - SLIDER_HANDLE), 0.0f, 1.0f);
            GameOptions before = options;
            w.set(options, v);
            changed = w.label(before) != w.label(options);
        }
        return changed;
    }

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    for (int i = 0; i < (int)list.size(); i++) {
        const Widget& w = list[i];
        if (!w.enabled || !CheckCollisionPointRec(mouse, Bounds(w))) continue;
        sounds.PlayClick();
        if (w.done) {
            Back(options);
        } else if (w.goTo >= 0) {
            page = (Page)w.goTo;
        } else if (w.kind == Kind::Slider) {
            dragging = i;
            Rectangle r = Bounds(w);
            w.set(options, std::clamp((mouse.x - (r.x + SLIDER_HANDLE / 2.0f)) / (r.width - SLIDER_HANDLE), 0.0f, 1.0f));
            changed = true;
        } else if (w.set) {
            w.set(options, 0.0f);
            changed = true;
        }
        break;
    }
    return changed;
}

void OptionsScreen::Draw(const GameOptions& options) const {
    if (!open) return;
    int s = Scale();
    int width = GetScreenWidth(), height = GetScreenHeight();
    (void)height;
    // In the game, main has already drawn the blurred world behind us.
    if (!overWorld) MenuScreen::DrawDirtBackground(dirt);
    if (GuiBackdrop::Get() != nullptr) {
        int guiHeight = (GetScreenHeight() + s - 1) / s;
        GuiBackdrop::Get()->DrawSeparator(33 - 2, true, overWorld, s);
        GuiBackdrop::Get()->DrawSeparator(guiHeight - 33, false, overWorld, s);
    }

    int guiWidth = (width + s - 1) / s;
    std::string title = Title();
    font.Draw(title.c_str(), (guiWidth / 2 - font.Width(title.c_str()) / 2) * s, TITLE_Y * s, s, WHITE, true);

    Vector2 mouse = MouseGui();
    std::vector<Widget> list = Widgets();
    for (int i = 0; i < (int)list.size(); i++) {
        const Widget& w = list[i];
        Rectangle r = Bounds(w);
        std::string label = w.label(options);
        if (w.kind == Kind::Button) {
            DrawGuiButton(font, widgets, r, label, w.enabled, CheckCollisionPointRec(mouse, r), s);
            continue;
        }
        // AbstractSliderButton: the slider track, then its handle.
        bool hovered = CheckCollisionPointRec(mouse, r) || dragging == i;
        DrawGuiNineSlice(hovered ? "widget/slider_highlighted" : "widget/slider", r.x, r.y, r.width, r.height, 1, 1, 1, 1, s);
        float handleX = floorf(r.x + w.value(options) * (r.width - SLIDER_HANDLE));
        DrawGuiNineSlice(hovered ? "widget/slider_handle_highlighted" : "widget/slider_handle", handleX, r.y, (float)SLIDER_HANDLE, r.height,
                         2, 2, 2, 3, s);
        Color text = WHITE;
        int textX = (int)r.x + (int)r.width / 2 - font.Width(label.c_str()) / 2;
        font.Draw(label.c_str(), textX * s, ((int)r.y + 6) * s, s, text, true);
    }
}
