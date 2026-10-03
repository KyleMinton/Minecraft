#include "SignEditScreen.h"
#include "Gui.h"
#include "MenuScreen.h"
#include "SoundSystem.h"
#include "TileEntity.h"
#include "World.h"

#include <string>

namespace {

constexpr int MAX_LINE_LENGTH = 15;
// ChatAllowedCharacters: printable ASCII the font has.
bool Allowed(int c) {
    return c >= 32 && c < 127;
}

} // namespace

SignEditScreen::SignEditScreen(const GuiFont& guiFont) : font(guiFont) {
    signTexture = LoadPixelTexture("resources/textures/item/sign.png");
    widgets = LoadPixelTexture("resources/textures/gui/widgets.png");
}

SignEditScreen::~SignEditScreen() {
    UnloadTexture(signTexture);
    UnloadTexture(widgets);
}

void SignEditScreen::Open(World& w, int x, int y, int z) {
    world = &w;
    signX = x;
    signY = y;
    signZ = z;
    line = 0;
    open = true;
    while (GetCharPressed() != 0) {} // don't type the key that placed it
}

void SignEditScreen::Close() {
    open = false;
    world = nullptr;
}

bool SignEditScreen::SignValid() const {
    if (world == nullptr) return false;
    TileEntity* te = world->GetTileEntity(signX, signY, signZ);
    return te != nullptr && te->kind == TileKind::Sign;
}

Rectangle SignEditScreen::DoneButton(int scale) const {
    int guiWidth = GetScreenWidth() / scale, guiHeight = GetScreenHeight() / scale;
    return Rectangle{ (float)(guiWidth / 2 - 100), (float)(guiHeight / 4 + 120), 200.0f, 20.0f };
}

bool SignEditScreen::Update(SoundSystem& sounds) {
    if (!open) return false;
    if (!SignValid()) {
        Close();
        return true;
    }
    tickTimer += GetFrameTime();
    while (tickTimer >= 0.05) {
        tickTimer -= 0.05;
        ticks++;
    }
    SignTile* sign = static_cast<SignTile*>(world->GetTileEntity(signX, signY, signZ));
    std::string& text = sign->lines[line];
    for (int c = GetCharPressed(); c != 0; c = GetCharPressed()) {
        if (Allowed(c) && (int)text.size() < MAX_LINE_LENGTH) text += (char)c;
    }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !text.empty()) text.pop_back();
    if (IsKeyPressed(KEY_UP)) line = (line + 3) % 4;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB)) line = (line + 1) % 4;

    int scale = GuiScale(GetScreenWidth(), GetScreenHeight());
    Vector2 mouse = GetMousePosition();
    Rectangle done = DoneButton(scale);
    bool overDone = mouse.x >= done.x * scale && mouse.y >= done.y * scale && mouse.x < (done.x + done.width) * scale &&
                    mouse.y < (done.y + done.height) * scale;
    if (IsKeyPressed(KEY_ESCAPE) || (overDone && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
        if (overDone) sounds.PlayClick();
        world->MarkForRender(signX, signY, signZ);
        Close();
        return true;
    }
    return false;
}

void SignEditScreen::Draw() const {
    if (!open || !SignValid()) return;
    const SignTile* sign = static_cast<const SignTile*>(world->GetTileEntity(signX, signY, signZ));
    int s = GuiScale(GetScreenWidth(), GetScreenHeight());
    int width = GetScreenWidth();
    int guiWidth = GetScreenWidth() / s;

    (void)width; // main draws the blurred world behind this screen
    const char* title = "Edit Sign Message";
    font.Draw(title, (guiWidth / 2 - font.Width(title) / 2) * s, 40 * s, s, WHITE, true);

    // The sign's face, big, with its text (GuiEditSign draws the model at 93.75x scale).
    const int boardWidth = 24 * 4, boardHeight = 12 * 4;
    int boardX = guiWidth / 2 - boardWidth / 2, boardY = 60;
    DrawTexturePro(signTexture, Rectangle{ 2, 2, 24, 12 }, Rectangle{ (float)(boardX * s), (float)(boardY * s), (float)(boardWidth * s), (float)(boardHeight * s) },
                   Vector2{ 0, 0 }, 0.0f, WHITE);
    for (int i = 0; i < 4; i++) {
        std::string text = sign->lines[i];
        if (i == line && (ticks / 6) % 2 == 0) text = "> " + text + " <";
        int y = boardY + 4 + i * 10;
        font.Draw(text.c_str(), (guiWidth / 2 - font.Width(text.c_str()) / 2) * s, y * s, s, BLACK);
    }

    // "Done".
    Rectangle done = DoneButton(s);
    Vector2 mouse = GetMousePosition();
    bool over = mouse.x >= done.x * s && mouse.y >= done.y * s && mouse.x < (done.x + done.width) * s && mouse.y < (done.y + done.height) * s;
    DrawGuiButton(font, widgets, done, "Done", true, over, s);
}
