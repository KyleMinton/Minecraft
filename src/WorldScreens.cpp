#include "WorldScreens.h"
#include "Gui.h"
#include "MenuScreen.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace {

constexpr int SLOT_HEIGHT = 36;      // one world in the list
constexpr int LIST_WIDTH = 270;
constexpr int ICON_SIZE = 32;
constexpr int DIRT_TILE = 32;        // GUI pixels per dirt tile
constexpr double DOUBLE_CLICK_SECONDS = 0.25;
const Color LABEL_GRAY = Argb(0xFFA0A0A0);
const Color DETAIL_GRAY = Argb(0xFF808080);

bool Inside(Vector2 point, Rectangle r) {
    return point.x >= r.x && point.y >= r.y && point.x < r.x + r.width && point.y < r.y + r.height;
}

bool KeyTyped(int key) {
    return IsKeyPressed(key) || IsKeyPressedRepeat(key);
}

std::string Trimmed(const std::string& text) {
    size_t start = text.find_first_not_of(' ');
    if (start == std::string::npos) return "";
    return text.substr(start, text.find_last_not_of(' ') - start + 1);
}

const char* GameModeName(GameMode mode) {
    return mode == GameMode::Creative ? "Creative Mode" : "Survival Mode";
}

} // namespace

// ---- TextField ----

void TextField::SetText(const std::string& value) {
    text = value.substr(0, maxLength);
    cursor = text.size();
}

bool TextField::HandleKeys() {
    if (!focused) return false;
    bool changed = false;
    for (int c = GetCharPressed(); c != 0; c = GetCharPressed()) {
        if (c >= 32 && c < 127 && text.size() < maxLength) { // Minecraft's allowed characters, ASCII part
            text.insert(text.begin() + cursor, (char)c);
            cursor++;
            changed = true;
        }
    }
    if (KeyTyped(KEY_BACKSPACE) && cursor > 0) {
        text.erase(--cursor, 1);
        changed = true;
    }
    if (KeyTyped(KEY_DELETE) && cursor < text.size()) {
        text.erase(cursor, 1);
        changed = true;
    }
    if (KeyTyped(KEY_LEFT) && cursor > 0) cursor--;
    if (KeyTyped(KEY_RIGHT) && cursor < text.size()) cursor++;
    if (IsKeyPressed(KEY_HOME)) cursor = 0;
    if (IsKeyPressed(KEY_END)) cursor = text.size();
    return changed;
}

void TextField::Click(Vector2 mouse, const GuiFont& font) {
    focused = Inside(mouse, Rectangle{ (float)x, (float)y, (float)width, (float)height });
    if (!focused) return;
    // Put the cursor where you clicked.
    cursor = text.size();
    int clickX = (int)mouse.x - x - 4;
    for (size_t i = 0; i <= text.size(); i++) {
        if (font.Width(text.substr(0, i).c_str()) > clickX) {
            cursor = i > 0 ? i - 1 : 0;
            return;
        }
    }
}

void TextField::Draw(const GuiFont& font, int s) const {
    DrawGuiNineSlice(focused ? "widget/text_field_highlighted" : "widget/text_field", (float)x, (float)y, (float)width, (float)height, 1, 1, 1, 1, s);
    int textX = x + 4, textY = y + (height - 8) / 2;
    font.Draw(text.c_str(), textX * s, textY * s, s, Argb(0xFFE0E0E0), true);

    // The cursor blinks every 6 ticks: an underscore at the end, a bar anywhere else.
    bool blinkOn = (int)(GetTime() * 20.0) / 6 % 2 == 0;
    if (!focused || !blinkOn) return;
    int cursorX = textX + font.Width(text.substr(0, cursor).c_str());
    if (cursor == text.size()) {
        font.Draw("_", cursorX * s, textY * s, s, Argb(0xFFE0E0E0), true);
    } else {
        DrawRectangle(cursorX * s, (textY - 1) * s, s, 10 * s, Argb(0xFFD0D0D0));
    }
}

// ---- WorldScreens ----

WorldScreens::WorldScreens(const GuiFont& guiFont) : font(guiFont) {
    widgets = LoadPixelTexture("resources/textures/gui/widgets.png");
    dirt = LoadPixelTexture("resources/textures/gui/options_background.png");
    SetTextureWrap(dirt, TEXTURE_WRAP_REPEAT);
    unknownIcon = LoadPixelTexture("resources/textures/gui/unknown_world.png");
    selectionIcons = LoadPixelTexture("resources/textures/gui/world_selection.png");
    nameField.maxLength = 32;
    seedField.maxLength = 32;
}

WorldScreens::~WorldScreens() {
    UnloadTexture(widgets);
    UnloadTexture(dirt);
    UnloadTexture(unknownIcon);
    UnloadTexture(selectionIcons);
}

int WorldScreens::Scale() const { return GuiScale(GetScreenWidth(), GetScreenHeight()); }
int WorldScreens::GuiWidth() const { int s = Scale(); return (GetScreenWidth() + s - 1) / s; }
int WorldScreens::GuiHeight() const { int s = Scale(); return (GetScreenHeight() + s - 1) / s; }

Vector2 WorldScreens::MouseGui() const {
    int s = Scale();
    Vector2 mouse = GetMousePosition();
    return Vector2{ (float)(int)(mouse.x / s), (float)(int)(mouse.y / s) };
}

bool WorldScreens::Hovered(const Button& button) const {
    return Inside(MouseGui(), button.rect);
}

bool WorldScreens::Clicked(const Button& button, SoundSystem& sounds) const {
    if (!button.enabled || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || !Hovered(button)) return false;
    sounds.PlayClick();
    return true;
}

void WorldScreens::DrawButton(const Button& button) const {
    DrawGuiButton(font, widgets, button.rect, button.label, button.enabled, Hovered(button), Scale());
}

void WorldScreens::DrawCentered(const char* text, int y, Color color) const {
    int s = Scale();
    font.Draw(text, (GuiWidth() / 2 - font.Width(text) / 2) * s, y * s, s, color, true);
}

// Dirt from GUI row `top` to `bottom`, tiled continuously down the screen (shifted
// up by `offset`, for the scrolling list), darkened to shade/255.
// The modern menus' backgrounds, where the 1.11 screens drew dirt: the list's
// darker background, the blurred panorama for a whole screen, and nothing
// (just the separator line) for the header and footer around a list.
void WorldScreens::DrawDirt(int top, int bottom, float offset, unsigned char shade) const {
    (void)offset;
    GuiBackdrop* backdrop = GuiBackdrop::Get();
    if (backdrop == nullptr) return;
    int s = Scale();
    if (shade <= 32) {
        backdrop->DrawListBackground(top, bottom, false, s);
    } else if (top == 0 && bottom >= GuiHeight()) {
        backdrop->DrawPanorama(true);
    } else if (top == 0) {
        backdrop->DrawSeparator(bottom - 2, true, false, s);
    } else {
        backdrop->DrawSeparator(top, false, false, s);
    }
}

// ---- Select World (GuiWorldSelection) ----

void WorldScreens::OpenSelect() {
    page = Page::Select;
    worlds = Saves::ListWorlds();
    selected = -1;
    scroll = 0.0f;
    lastClickIndex = -1;
}

std::vector<WorldScreens::Button> WorldScreens::SelectButtons() const {
    float w = (float)GuiWidth(), h = (float)GuiHeight();
    bool any = selected >= 0 && selected < (int)worlds.size();
    return {
        { { w / 2 - 154, h - 52, 150, 20 }, "Play Selected World", any },
        { { w / 2 + 4, h - 52, 150, 20 }, "Create New World", true },
        { { w / 2 - 154, h - 28, 72, 20 }, "Edit", false },
        { { w / 2 - 76, h - 28, 72, 20 }, "Delete", any },
        { { w / 2 + 4, h - 28, 72, 20 }, "Re-Create", false },
        { { w / 2 + 82, h - 28, 72, 20 }, "Cancel", true },
    };
}

float WorldScreens::MaxScroll() const {
    return std::max(0.0f, (float)(worlds.size() * SLOT_HEIGHT) - (ListBottom() - ListTop() - 4));
}

WorldScreens::Result WorldScreens::UpdateSelect(SoundSystem& sounds) {
    Result result;
    Vector2 mouse = MouseGui();

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) scroll += (wheel > 0.0f ? -1.0f : 1.0f) * SLOT_HEIGHT / 2.0f;
    scroll = std::clamp(scroll, 0.0f, MaxScroll());

    // Clicking a world selects it; clicking its icon or double-clicking plays it.
    int listLeft = GuiWidth() / 2 - LIST_WIDTH / 2;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouse.y >= ListTop() && mouse.y < ListBottom() &&
        mouse.x >= listLeft && mouse.x < listLeft + LIST_WIDTH) {
        int index = (int)std::floor((mouse.y - ListTop() + scroll - 4) / SLOT_HEIGHT);
        if (index >= 0 && index < (int)worlds.size()) {
            selected = index;
            int entryX = listLeft + 2;
            double now = GetTime();
            bool doubleClick = index == lastClickIndex && now - lastClickTime < DOUBLE_CLICK_SECONDS;
            lastClickIndex = index;
            lastClickTime = now;
            if (mouse.x - entryX < ICON_SIZE || doubleClick) {
                result.kind = Result::Kind::Play;
                result.world = worlds[index];
                return result;
            }
        }
    }

    std::vector<Button> buttons = SelectButtons();
    if (Clicked(buttons[0], sounds)) {
        result.kind = Result::Kind::Play;
        result.world = worlds[selected];
    } else if (Clicked(buttons[1], sounds)) {
        OpenCreate();
    } else if (Clicked(buttons[3], sounds)) {
        page = Page::ConfirmDelete;
    } else if (Clicked(buttons[5], sounds) || IsKeyPressed(KEY_ESCAPE)) {
        result.kind = Result::Kind::Back;
    }
    return result;
}

void WorldScreens::DrawSelect() const {
    int s = Scale();
    int w = GuiWidth(), h = GuiHeight();
    int top = ListTop(), bottom = ListBottom();
    Vector2 mouse = MouseGui();

    // The blurred panorama, then the list's darker background (AbstractSelectionList).
    DrawDirt(0, h, 0.0f, 64);
    DrawDirt(top, bottom, scroll, 32);

    BeginScissorMode(0, top * s, w * s, (bottom - top) * s);
    int listLeft = w / 2 - LIST_WIDTH / 2;
    for (int i = 0; i < (int)worlds.size(); i++) {
        const WorldInfo& world = worlds[i];
        int x = listLeft + 2;
        int y = top + 4 - (int)scroll + i * SLOT_HEIGHT;
        if (y + SLOT_HEIGHT < top || y > bottom) continue;

        if (i == selected) {
            DrawRectangle(listLeft * s, (y - 2) * s, LIST_WIDTH * s, SLOT_HEIGHT * s, WHITE);
            DrawRectangle((listLeft + 1) * s, (y - 1) * s, (LIST_WIDTH - 2) * s, (SLOT_HEIGHT - 2) * s, BLACK);
        }

        std::string name = world.name.empty() ? "World " + std::to_string(i + 1) : world.name;
        std::string details = world.folder + " (" + Saves::FormatDate(world.lastPlayed) + ")";
        font.Draw(name.c_str(), (x + ICON_SIZE + 3) * s, (y + 1) * s, s, WHITE);
        font.Draw(details.c_str(), (x + ICON_SIZE + 3) * s, (y + 8 + 3) * s, s, DETAIL_GRAY);
        font.Draw(GameModeName(world.gameMode), (x + ICON_SIZE + 3) * s, (y + 8 + 8 + 3) * s, s, DETAIL_GRAY);

        DrawTexturePro(unknownIcon, Rectangle{ 0, 0, (float)unknownIcon.width, (float)unknownIcon.height },
                       Rectangle{ (float)(x * s), (float)(y * s), (float)(ICON_SIZE * s), (float)(ICON_SIZE * s) }, Vector2{ 0, 0 }, 0.0f, WHITE);
        if (i == selected) {
            // The selected world's icon becomes a play button.
            DrawRectangle(x * s, y * s, ICON_SIZE * s, ICON_SIZE * s, Argb(0xA0909090));
            bool overIcon = mouse.x >= x && mouse.x < x + ICON_SIZE && mouse.y >= y && mouse.y < y + ICON_SIZE;
            DrawGuiSprite(overIcon ? "world_list/join_highlighted" : "world_list/join", (float)x, (float)y, s);
        }
    }
    EndScissorMode();

    // Separator lines above and below the list.
    DrawDirt(0, top, 0.0f, 64);
    DrawDirt(bottom, h, 0.0f, 64);

    float maxScroll = MaxScroll();
    if (maxScroll > 0.0f) {
        int barX = w / 2 + 144, barWidth = 6;
        int trackHeight = bottom - top;
        int thumbHeight = std::clamp(trackHeight * trackHeight / (int)(worlds.size() * SLOT_HEIGHT), 32, trackHeight - 8);
        int thumbY = top + (int)(scroll * (trackHeight - thumbHeight) / maxScroll);
        DrawGuiNineSlice("widget/scroller_background", (float)barX, (float)top, (float)barWidth, (float)trackHeight, 1, 1, 1, 1, s);
        DrawGuiNineSlice("widget/scroller", (float)barX, (float)thumbY, (float)barWidth, (float)thumbHeight, 1, 1, 1, 1, s);
    }

    DrawCentered("Select World", 20, WHITE);
    for (const Button& button : SelectButtons()) DrawButton(button);
}

// ---- Create New World (GuiCreateWorld) ----

void WorldScreens::OpenCreate() {
    page = Page::Create;
    nameField.x = GuiWidth() / 2 - 100;
    nameField.y = 60;
    nameField.SetText("New World");
    nameField.focused = true;
    seedField = TextField{ nameField.x, 60, 200, 20 };
    seedField.maxLength = 32;
    newMode = GameMode::Survival;
    moreOptions = false;
    folderName = Saves::FolderFor(Trimmed(nameField.text));
    while (GetCharPressed() != 0) {} // don't type the click that got us here
}

std::vector<WorldScreens::Button> WorldScreens::CreateButtons() const {
    float w = (float)GuiWidth(), h = (float)GuiHeight();
    return {
        { { w / 2 - 155, h - 28, 150, 20 }, "Create New World", !Trimmed(nameField.text).empty() },
        { { w / 2 + 5, h - 28, 150, 20 }, "Cancel", true },
        { { w / 2 - 75, 115, 150, 20 }, std::string("Game Mode: ") + (newMode == GameMode::Creative ? "Creative" : "Survival"), true },
        { { w / 2 - 75, 187, 150, 20 }, moreOptions ? "Done" : "More World Options...", true },
    };
}

WorldScreens::Result WorldScreens::UpdateCreate(SoundSystem& sounds) {
    Result result;
    TextField& field = moreOptions ? seedField : nameField;
    field.x = GuiWidth() / 2 - 100;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) field.Click(MouseGui(), font);
    if (field.HandleKeys() && !moreOptions) folderName = Saves::FolderFor(Trimmed(nameField.text));

    std::vector<Button> buttons = CreateButtons();
    bool create = Clicked(buttons[0], sounds) || (buttons[0].enabled && IsKeyPressed(KEY_ENTER));
    if (create) {
        WorldInfo info;
        info.name = Trimmed(nameField.text);
        info.folder = Saves::FolderFor(info.name);
        info.seed = Saves::SeedFromText(seedField.text);
        info.gameMode = newMode;
        info.lastPlayed = (long long)time(nullptr);
        if (Saves::CreateWorld(info)) {
            result.kind = Result::Kind::Create;
            result.world = info;
        }
    } else if (Clicked(buttons[1], sounds) || IsKeyPressed(KEY_ESCAPE)) {
        OpenSelect();
    } else if (!moreOptions && Clicked(buttons[2], sounds)) {
        newMode = newMode == GameMode::Creative ? GameMode::Survival : GameMode::Creative;
    } else if (Clicked(buttons[3], sounds)) {
        moreOptions = !moreOptions;
        nameField.focused = !moreOptions;
        seedField.focused = moreOptions;
    }
    return result;
}

void WorldScreens::DrawCreate() const {
    int s = Scale();
    int w = GuiWidth(), h = GuiHeight();
    DrawDirt(0, h, 0.0f, 64);
    DrawCentered("Create New World", 20, WHITE);

    int left = w / 2 - 100;
    std::vector<Button> buttons = CreateButtons();
    if (moreOptions) {
        font.Draw("Seed for the World Generator", left * s, 47 * s, s, LABEL_GRAY, true);
        seedField.Draw(font, s);
        font.Draw("Leave blank for a random seed", left * s, 85 * s, s, LABEL_GRAY, true);
    } else {
        font.Draw("World Name", left * s, 47 * s, s, LABEL_GRAY, true);
        nameField.Draw(font, s);
        font.Draw(("Will be saved in: " + folderName).c_str(), left * s, 85 * s, s, LABEL_GRAY, true);
        DrawButton(buttons[2]);
        const char* line1 = newMode == GameMode::Creative ? "Unlimited resources, free flying and" : "Search for resources, crafting, gain";
        const char* line2 = newMode == GameMode::Creative ? "destroy blocks instantly" : "levels, health and hunger";
        font.Draw(line1, left * s, 137 * s, s, LABEL_GRAY, true);
        font.Draw(line2, left * s, 149 * s, s, LABEL_GRAY, true);
    }
    DrawButton(buttons[0]);
    DrawButton(buttons[1]);
    DrawButton(buttons[3]);
}

// ---- Delete confirmation (GuiYesNo) ----

std::vector<WorldScreens::Button> WorldScreens::DeleteButtons() const {
    float w = (float)GuiWidth(), h = (float)GuiHeight();
    return {
        { { w / 2 - 155, h / 6 + 96, 150, 20 }, "Delete", true },
        { { w / 2 - 155 + 160, h / 6 + 96, 150, 20 }, "Cancel", true },
    };
}

WorldScreens::Result WorldScreens::UpdateDelete(SoundSystem& sounds) {
    std::vector<Button> buttons = DeleteButtons();
    if (Clicked(buttons[0], sounds)) {
        if (selected >= 0 && selected < (int)worlds.size()) Saves::DeleteWorld(worlds[selected]);
        OpenSelect();
    } else if (Clicked(buttons[1], sounds) || IsKeyPressed(KEY_ESCAPE)) {
        page = Page::Select;
    }
    return Result();
}

void WorldScreens::DrawDelete() const {
    DrawDirt(0, GuiHeight(), 0.0f, 64);
    DrawCentered("Are you sure you want to delete this world?", 70, WHITE);
    std::string name = (selected >= 0 && selected < (int)worlds.size()) ? worlds[selected].name : "";
    DrawCentered(("'" + name + "' will be lost forever! (A long time!)").c_str(), 90, WHITE);
    for (const Button& button : DeleteButtons()) DrawButton(button);
}

// ----

WorldScreens::Result WorldScreens::Update(SoundSystem& sounds) {
    switch (page) {
        case Page::Select:        return UpdateSelect(sounds);
        case Page::Create:        return UpdateCreate(sounds);
        case Page::ConfirmDelete: return UpdateDelete(sounds);
        default:                  return Result();
    }
}

void WorldScreens::Draw() const {
    switch (page) {
        case Page::Select:        DrawSelect(); break;
        case Page::Create:        DrawCreate(); break;
        case Page::ConfirmDelete: DrawDelete(); break;
        default: break;
    }
}
