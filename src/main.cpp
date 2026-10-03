#include "raylib.h"
#include "raymath.h"
#include "World.h"
#include "Player.h"
#include "SoundSystem.h"
#include "BreakOverlay.h"
#include "Hud.h"
#include "Sky.h"
#include "Hand.h"
#include "GuiFont.h"
#include "ItemIcons.h"
#include "DroppedItems.h"
#include "BlockParticles.h"
#include "Entities.h"
#include "MenuScreen.h"
#include "Options.h"
#include "OptionsScreen.h"
#include "Gui.h"
#include "WorldScreens.h"
#include "Saves.h"
#include "InventoryScreen.h"
#include "SignEditScreen.h"
#include "TextureFX.h"
#include "TileEntityRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

namespace {

constexpr double AUTOSAVE_SECONDS = 45.0; // Minecraft saves every 900 ticks
constexpr int SPAWN_ORIGIN_X = 8;
constexpr int SPAWN_ORIGIN_Z = 8;
constexpr int SPAWN_SEARCH_RADIUS = 60; // blocks; stays inside the area LoadAround() generates
constexpr long long DAY_TICKS = 24000;

// Height of the topmost non-air block in a column, or -1 if it's all air.
int SurfaceY(const World& world, int x, int z) {
    for (int y = Chunk::HEIGHT - 1; y >= 0; y--) {
        if (world.GetBlock(x, y, z) != BLOCK_AIR) return y;
    }
    return -1;
}

bool IsGrass(uint8_t block) { return block == BLOCK_GRASS; }

// Dry, solid ground: not water, and not the top of a tree.
bool IsDryGround(uint8_t block) {
    return GetBlockDef(block).BlocksMovement() && block != BLOCK_LEAVES && block != BLOCK_LOG && block != BLOCK_CACTUS;
}

// Checks columns in rings outward from the origin; returns the first whose
// topmost block passes `accept`.
bool SearchSpawn(const World& world, bool (*accept)(uint8_t), int& x, int& y, int& z) {
    for (int radius = 0; radius <= SPAWN_SEARCH_RADIUS; radius++) {
        for (int dx = -radius; dx <= radius; dx++) {
            for (int dz = -radius; dz <= radius; dz++) {
                if (abs(dx) != radius && abs(dz) != radius) continue; // just this ring
                int wx = SPAWN_ORIGIN_X + dx, wz = SPAWN_ORIGIN_Z + dz;
                int top = SurfaceY(world, wx, wz);
                if (top >= 0 && accept(world.GetBlock(wx, top, wz))) {
                    x = wx; y = top; z = wz;
                    return true;
                }
            }
        }
    }
    return false;
}

// Spawns standing on the ground: grass if there's any nearby, otherwise any dry
// surface like a beach, and as a last resort (all ocean) the sea floor at the origin.
Vector3 FindSpawn(const World& world) {
    int x = SPAWN_ORIGIN_X, z = SPAWN_ORIGIN_Z, y = 0;
    if (!SearchSpawn(world, IsGrass, x, y, z) && !SearchSpawn(world, IsDryGround, x, y, z)) {
        x = SPAWN_ORIGIN_X;
        z = SPAWN_ORIGIN_Z;
        y = 0;
        for (int yy = Chunk::HEIGHT - 1; yy >= 0; yy--) {
            if (GetBlockDef(world.GetBlock(x, yy, z)).BlocksMovement()) { y = yy; break; }
        }
    }
    TraceLog(LOG_INFO, "Spawning on %s at (%d, %d, %d)", GetBlockDef(world.GetBlock(x, y, z)).name.c_str(), x, y + 1, z);
    return Vector3{ x + 0.5f, y + 1.0f, z + 0.5f }; // feet exactly on top of the block
}

// Owns everything that uses the GPU or audio device, so it's all released
// here -- before main() closes the audio device and the window.
void RunGame(Texture2D atlasTexture, bool hasTexture) {
    SoundSystem sounds;
    BreakOverlay breakOverlay;
    GuiFont font;
    ItemModels itemModels(atlasTexture);
    ItemIcons itemIcons(itemModels);
    Hud hud(itemIcons, font, atlasTexture);
    InventoryScreen inventoryScreen(itemIcons, font);
    SignEditScreen signScreen(font);
    DroppedItems drops(itemModels);
    BlockParticles particles(atlasTexture, hasTexture);
    Entities entities(itemModels);
    TileEntityRenderer tileRenderer(itemModels, font);
    TextureFX textureFX(atlasTexture);
    // PauseScreen (Minecraft 1.21's layout; x from the center, y from a quarter height).
    MenuScreen pauseMenu(font, "Game Menu", {
        { -102, 8, 204, 20, "Back to Game", true, MenuAction::BackToGame },
        { -102, 32, 98, 20, "Advancements", false, MenuAction::None },
        { 4, 32, 98, 20, "Statistics", false, MenuAction::None },
        { -102, 56, 98, 20, "Give Feedback", false, MenuAction::None },
        { 4, 56, 98, 20, "Report Bugs", false, MenuAction::None },
        { -102, 80, 98, 20, "Options...", true, MenuAction::Options },
        { 4, 80, 98, 20, "Open to LAN", false, MenuAction::None },
        { -102, 104, 204, 20, "Save and Quit to Title", true, MenuAction::QuitToTitle },
    }, MenuScreen::Background::DimmedWorld);
    // TitleScreen (1.21): the panorama, logo and splash behind these.
    MenuScreen titleMenu(font, "", {
        { -100, 48, 200, 20, "Singleplayer", true, MenuAction::Singleplayer },
        { -100, 72, 200, 20, "Multiplayer", false, MenuAction::None },
        { -100, 96, 200, 20, "Minecraft Realms", false, MenuAction::None },
        { -100, 132, 98, 20, "Options...", true, MenuAction::Options },
        { 2, 132, 98, 20, "Quit Game", true, MenuAction::QuitGame },
    }, MenuScreen::Background::Title);
    Sky sky(World::ViewDistance());
    HandRenderer hand(itemModels);
    OptionsScreen optionsScreen(font);
    GuiBackdrop backdrop; // the title panorama and menu blur

    // The options menu's settings, applied to everything they touch.
    GameOptions options;
    options.Load();
    auto applyOptions = [&]() {
        sounds.SetVolumes(options.music, options.sound);
        g_guiScaleSetting = options.guiScale;
        SetTargetFPS(options.maxFramerate);
        World::SetRenderDistance(options.renderDistance);
        sky.SetFarPlane(World::ViewDistance());
    };
    auto applyPlayerOptions = [&](Player& p) {
        p.SetMouse(options.sensitivity, options.invertMouse);
        p.SetFov(options.fov);
        p.SetViewBobbing(options.viewBobbing);
        p.SetDifficulty(options.difficulty);
    };
    applyOptions();

    WorldScreens worldScreens(font);

    // The world being played, if any. Worlds live in saves/ (see Saves.h): opening
    // one loads it, and quitting to the title saves it and closes it.
    std::unique_ptr<World> worldPtr;
    std::unique_ptr<Player> playerPtr;
    WorldInfo currentWorld;
    double lastSave = 0.0;

    auto saveWorld = [&]() {
        if (!worldPtr) return;
        if (!Saves::SaveWorld(currentWorld, *worldPtr, *playerPtr, drops, sky.GetTime())) {
            TraceLog(LOG_WARNING, "Couldn't save world '%s'", currentWorld.folder.c_str());
        }
        lastSave = GetTime();
    };

    auto openWorld = [&](const WorldInfo& info) {
        BeginDrawing();
            titleMenu.DrawMessage("Building terrain");
        EndDrawing();

        currentWorld = info;
        if (info.generatorVersion != Chunk::GENERATOR_VERSION) {
            TraceLog(LOG_WARNING, "World '%s' was made with terrain generator %d, this is %d: unedited terrain may differ from before",
                     info.folder.c_str(), info.generatorVersion, Chunk::GENERATOR_VERSION);
        }
        worldPtr = std::make_unique<World>(info.seed);
        worldPtr->SetTexture(atlasTexture, hasTexture);
        worldPtr->LoadEdits(Saves::EditsPath(info));
        Saves::LoadTileEntities(info, *worldPtr);
        worldPtr->SetServices(&sounds, &drops, &particles, &entities, nullptr);

        // A saved player picks up where they left off; a new one spawns on the ground.
        bool returning = Saves::HasPlayer(info);
        Vector3 center = { (float)SPAWN_ORIGIN_X, 0.0f, (float)SPAWN_ORIGIN_Z };
        if (returning) {
            playerPtr = std::make_unique<Player>(center);
            Saves::LoadPlayer(info, *playerPtr);
            center = playerPtr->GetPosition();
        }
        worldPtr->LoadAround(center);
        if (!returning) {
            playerPtr = std::make_unique<Player>(FindSpawn(*worldPtr));
            playerPtr->SetGameMode(info.gameMode);
        }
        worldPtr->SetServices(&sounds, &drops, &particles, &entities, playerPtr.get());
        applyPlayerOptions(*playerPtr);

        Saves::LoadItems(info, drops);
        particles.Clear();
        entities.Clear();
        sky.SetTime(info.time);
        saveWorld(); // a new world is on disk from the start
        playerPtr->SetControlsEnabled(true);
        DisableCursor();
    };

    auto closeWorld = [&]() {
        BeginDrawing();
            titleMenu.DrawMessage("Saving world");
        EndDrawing();
        saveWorld();
        sounds.StopAll();
        worldPtr.reset();
        playerPtr.reset();
        drops.Clear();
        particles.Clear();
        entities.Clear();
    };

    titleMenu.Open();
    SetExitKey(KEY_NULL); // Esc opens the menu or closes the inventory; handled below

    bool quit = false;
    bool showOverlay = true; // the controls line and FPS; F3 toggles them
    while (!WindowShouldClose() && !quit) {
        sounds.Update(GetFrameTime());
        backdrop.Update(GetFrameTime());
        if (optionsScreen.IsOpen() && !worldPtr) {
            if (optionsScreen.Update(sounds, options)) applyOptions();
            BeginDrawing();
                optionsScreen.Draw(options);
            EndDrawing();
            continue;
        }
        if (titleMenu.IsOpen()) {
            MenuAction choice = titleMenu.Update(sounds);
            if (choice == MenuAction::Options) optionsScreen.Open(false);
            if (choice == MenuAction::QuitGame) break;
            if (choice == MenuAction::Singleplayer) {
                titleMenu.Close();
                worldScreens.OpenSelect();
            }
            BeginDrawing();
                titleMenu.Draw();
                worldScreens.Draw();
            EndDrawing();
            continue;
        }
        if (worldScreens.IsOpen()) {
            WorldScreens::Result result = worldScreens.Update(sounds);
            if (result.kind == WorldScreens::Result::Kind::Back) {
                worldScreens.Close();
                titleMenu.Open();
            } else if (result.kind == WorldScreens::Result::Kind::Play || result.kind == WorldScreens::Result::Kind::Create) {
                worldScreens.Close();
                openWorld(result.world);
                continue; // start next frame, so the click that picked the world doesn't hit a block
            }
            if (worldScreens.IsOpen() || titleMenu.IsOpen()) {
                BeginDrawing();
                    worldScreens.Draw();
                    titleMenu.Draw();
                EndDrawing();
                continue;
            }
        }
        World& world = *worldPtr;
        Player& player = *playerPtr;

        // Esc opens the game menu, which pauses the game (as does clicking away from
        // the window, like Minecraft); Esc or "Back to Game" resumes. E opens the
        // inventory, and E or Esc closes it. Blocks open their own screens. While
        // any is open the mouse is free and the player doesn't take input.
        auto resume = [&]() {
            pauseMenu.Close();
            player.SetControlsEnabled(true);
            DisableCursor();
        };
        auto closeScreen = [&]() {
            if (inventoryScreen.IsOpen()) inventoryScreen.Close(player, drops);
            if (signScreen.IsOpen()) signScreen.Close();
            player.SetControlsEnabled(true);
            DisableCursor();
        };
        if (optionsScreen.IsOpen()) {
            if (optionsScreen.Update(sounds, options)) {
                applyOptions();
                applyPlayerOptions(player);
            }
        } else if (pauseMenu.IsOpen()) {
            if (IsKeyPressed(KEY_ESCAPE)) resume();
        } else if (signScreen.IsOpen()) {
            // Handled by its Update below (Esc and Done close it).
        } else if (inventoryScreen.IsOpen()) {
            if ((IsKeyPressed(KEY_E) && !inventoryScreen.CapturesKeyboard()) || IsKeyPressed(KEY_ESCAPE)) closeScreen();
        } else if (IsKeyPressed(KEY_ESCAPE) || !IsWindowFocused()) {
            pauseMenu.Open();
            player.SetControlsEnabled(false);
            EnableCursor();
        } else if (IsKeyPressed(KEY_E) && !player.IsSleeping()) {
            inventoryScreen.Open(player);
            player.SetControlsEnabled(false);
            EnableCursor();
        } else {
            if (IsKeyPressed(KEY_F3)) showOverlay = !showOverlay;
            if (IsKeyPressed(KEY_C)) player.SetGameMode(GameMode::Creative);
            if (IsKeyPressed(KEY_V)) player.SetGameMode(GameMode::Survival);
        }

        MenuAction choice = optionsScreen.IsOpen() ? MenuAction::None : pauseMenu.Update(sounds);
        if (choice == MenuAction::BackToGame) resume();
        if (choice == MenuAction::Options) optionsScreen.Open(true);
        if (choice == MenuAction::QuitToTitle) {
            pauseMenu.Close();
            if (inventoryScreen.IsOpen()) inventoryScreen.Close(player, drops);
            if (signScreen.IsOpen()) signScreen.Close();
            closeWorld();
            titleMenu.Open(); // the cursor is already free
            continue;
        }

        // Minecraft autosaves every 45 seconds of play.
        if (!pauseMenu.IsOpen() && GetTime() - lastSave >= AUTOSAVE_SECONDS) saveWorld();

        // Paused: time stands still for everything in the world.
        float dt = pauseMenu.IsOpen() ? 0.0f : GetFrameTime();
        world.Update(player.GetPosition(), dt);
        player.Update(world, dt, sounds);
        player.UpdateInteraction(world, sounds, drops, particles);

        // A block asked for a screen (a chest opened, a sign placed...).
        ScreenKind screenKind;
        int sx, sy, sz;
        if (player.TakeScreenRequest(screenKind, sx, sy, sz)) {
            if (screenKind == ScreenKind::Sign) signScreen.Open(world, sx, sy, sz);
            else inventoryScreen.OpenContainer(screenKind, sx, sy, sz, world, player);
            if (signScreen.IsOpen() || inventoryScreen.IsOpen()) {
                player.SetControlsEnabled(false);
                EnableCursor();
            }
        }
        if (signScreen.IsOpen() && !pauseMenu.IsOpen() && signScreen.Update(sounds)) closeScreen();

        // A night slept through: morning comes.
        if (player.SleptThroughNight()) {
            long long time = sky.GetTime();
            sky.SetTime(time + DAY_TICKS - time % DAY_TICKS);
            player.WakeUp(world);
        }

        particles.Update(dt, world);
        textureFX.Update(dt);
        if (inventoryScreen.IsOpen()) {
            inventoryScreen.Update(player, drops);
            if (!inventoryScreen.IsOpen()) closeScreen(); // its block went away
        }
        drops.Update(dt, world, player, sounds);
        itemIcons.Prepare();

        const Camera3D& camera = player.GetCamera();
        int cx = (int)floorf(camera.position.x), cy = (int)floorf(camera.position.y), cz = (int)floorf(camera.position.z);
        bool eyeInWater = world.GetMaterial(cx, cy, cz) == BlockMaterial::Water;
        int eyeLight = world.GetSkyLight(cx, cy, cz);
        sky.Update(dt, eyeInWater, eyeLight);
        TerrainFog fog = sky.GetTerrainFog();
        world.SetSkyDarken((int)fog.skyDarken);
        world.SetFog(fog.color, fog.start, fog.end, fog.density, fog.skyDarken, camera.position);
        drops.SetFog(fog.color, fog.start, fog.end, fog.density, fog.skyDarken, camera.position);
        particles.SetFog(fog.color, fog.start, fog.end, fog.density, fog.skyDarken, camera.position);
        entities.SetFog(fog.color, fog.start, fog.end, fog.density, fog.skyDarken, camera.position);
        tileRenderer.SetFog(fog.color, fog.start, fog.end, fog.density, fog.skyDarken, camera.position);
        // The hand and the player in the inventory are lit by the light where you stand.
        int eyeLevel = std::max(eyeLight - (int)fog.skyDarken, world.GetBlockLight(cx, cy, cz));
        float playerBrightness = LightLevelBrightness(eyeLevel);
        inventoryScreen.Prepare(player, playerBrightness);
        float worldPartial = world.GetTickAlpha();

        // Modern menus blur the game behind them: draw it into a texture first.
        bool blurWorld = pauseMenu.IsOpen() || optionsScreen.IsOpen() || signScreen.IsOpen();
        auto drawGame = [&](const RenderTexture2D* sceneTarget) {
            // Like Minecraft, the background is the fog color; the sky is drawn over it.
            ClearBackground(sky.ClearColor());

            BeginMode3D(camera);
                sky.DrawSky(camera);
                world.Draw();
                tileRenderer.Draw(world, camera.position, worldPartial);
                entities.Draw(world, worldPartial);
                drops.Draw(player, world, camera);
                particles.Draw(camera, world);

                const RaycastHit& target = player.GetTarget();
                BreakProgress breaking = player.GetBreakProgress();
                if (breaking.active) {
                    uint8_t id = world.GetBlock(breaking.x, breaking.y, breaking.z);
                    BlockBox bounds = GetBlockDef(id).Bounds(world, breaking.x, breaking.y, breaking.z);
                    breakOverlay.Draw(breaking.x, breaking.y, breaking.z, breaking.stage, Vector3Subtract(camera.target, camera.position), bounds);
                }

                world.DrawTranslucent(camera.position);

                // The block outline (RenderGlobal.drawSelectionBox), around its real shape.
                if (target.hit) {
                    uint8_t id = world.GetBlock(target.x, target.y, target.z);
                    BlockBox b = GetBlockDef(id).Bounds(world, target.x, target.y, target.z);
                    const float grow = 0.002f;
                    Vector3 center = { target.x + (b.minX + b.maxX) / 2, target.y + (b.minY + b.maxY) / 2, target.z + (b.minZ + b.maxZ) / 2 };
                    DrawCubeWires(center, b.maxX - b.minX + grow * 2, b.maxY - b.minY + grow * 2, b.maxZ - b.minZ + grow * 2, Color{ 0, 0, 0, 102 });
                }

                sky.DrawClouds(camera);
            EndMode3D();

            hand.Draw(player.GetHandPose(), playerBrightness, sceneTarget);

            hud.Draw(player);
        };

        BeginDrawing();
            if (blurWorld) {
                RenderTexture2D& scene = backdrop.SceneTarget();
                BeginTextureMode(scene);
                    drawGame(&scene);
                EndTextureMode();
                backdrop.DrawBlurredScene();
            } else {
                drawGame(nullptr);
            }
            inventoryScreen.Draw(player);
            signScreen.Draw();
            if (optionsScreen.IsOpen()) optionsScreen.Draw(options);
            else pauseMenu.Draw();

            // Only over the game itself, not over menus and screens.
            bool anyScreen = pauseMenu.IsOpen() || optionsScreen.IsOpen() || inventoryScreen.IsOpen() || signScreen.IsOpen();
            if (showOverlay && !anyScreen) {
                DrawText("WASD move, SPACE jump, double-tap W sprint, SHIFT sneak, LMB mine, RMB use/place, 1-9 select, E inventory, Q drop, MMB pick, C creative, V survival, ESC menu, F3 hide this",
                         20, 20, 20, DARKGRAY);
                DrawFPS(20, GetScreenHeight() - 30);
            }
        EndDrawing();
    }

    // Closing the window mid-game still saves, like Minecraft.
    if (worldPtr) {
        if (inventoryScreen.IsOpen()) inventoryScreen.Close(*playerPtr, drops);
        saveWorld();
    }
    EnableCursor();
    UnloadGuiTextures();
}

} // namespace

// The whole game, exported from RaylibGame.dll; RaylibApp.exe just calls this.
extern "C" __declspec(dllexport) int RaylibAppMain() {
    // Installed, the game's resources sit next to the exe; find them wherever it's run from.
    const char* appDir = GetApplicationDirectory();
    if (DirectoryExists(TextFormat("%sresources", appDir))) ChangeDirectory(appDir);

    const int screenWidth = 1920;
    const int screenHeight = 1080;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Minecraft");
    // On a screen too small for 1920x1080, shrink to 85% of it (same shape) and center.
    int monitor = GetCurrentMonitor();
    int monitorWidth = GetMonitorWidth(monitor), monitorHeight = GetMonitorHeight(monitor);
    if (monitorWidth > 0 && monitorHeight > 0 && (screenWidth > monitorWidth * 0.9f || screenHeight > monitorHeight * 0.9f)) {
        float scale = std::min(monitorWidth * 0.85f / screenWidth, monitorHeight * 0.85f / screenHeight);
        int width = (int)(screenWidth * scale), height = (int)(screenHeight * scale);
        SetWindowSize(width, height);
        Vector2 origin = GetMonitorPosition(monitor);
        SetWindowPosition((int)origin.x + (monitorWidth - width) / 2, (int)origin.y + (monitorHeight - height) / 2);
    }
    InitAudioDevice();
    SetTargetFPS(0);

    const char* texturePath = "resources/textures/terrain.png";
    Texture2D atlasTexture = { 0 };
    bool hasTexture = FileExists(texturePath);
    if (hasTexture) {
        atlasTexture = LoadTexture(texturePath);
        SetTextureFilter(atlasTexture, TEXTURE_FILTER_POINT); // avoid bleeding between atlas tiles
    }

    RunGame(atlasTexture, hasTexture);

    if (hasTexture) UnloadTexture(atlasTexture);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
