#pragma once

#include "raylib.h"
#include "World.h"
#include "SoundSystem.h"
#include "Inventory.h"

#include <iosfwd>
#include <string>
#include <vector>

class DroppedItems;
class BlockParticles;

struct BreakProgress {
    bool active = false;
    int x = 0, y = 0, z = 0;
    int stage = -1; // crack texture 0-9, or -1 for no crack yet
};

// Everything needed to draw the first-person hand this frame, already
// interpolated between ticks. Angles in degrees, Minecraft's conventions.
struct HandPose {
    ItemStack item;               // what's in hand (empty = bare arm); lags a switch until the hand is lowered
    float equipProgress = 1.0f;   // 1 = raised, 0 = fully lowered while switching items
    float swingProgress = 0.0f;   // 0..1 through a swing
    float bobPhase = 0.0f, bobAmount = 0.0f, tilt = 0.0f; // view bobbing, as applied to the camera
    float lagPitch = 0.0f, lagYaw = 0.0f; // how far the arm trails behind the view
    float pitch = 0.0f, yaw = 0.0f;       // view angles, for the item lighting
    float hurtRoll = 0.0f;                // camera tilt from taking damage
    float useTicks = -1.0f;               // ticks spent eating so far, or -1
    int useDuration = 32;
};

enum class GameMode { Survival, Creative };

// The screens a block can open.
enum class ScreenKind { None, Chest, Furnace, Dispenser, Crafting, Sign };

// What the survival HUD shows.
struct PlayerStats {
    float health = 20.0f;
    float maxHealth = 20.0f;
    int food = 20;
    float saturation = 5.0f;
    float experience = 0.0f; // progress through the current level, 0..1
    int level = 0;
    int armor = 0;            // armor points, 0-20
    int air = 300;            // ticks of breath left underwater
    bool headInWater = false;
    int hurtResistantTime = 0; // ticks of damage immunity left; the HUD flashes hearts while it's set
    long long ticks = 0;       // the player's tick count, the HUD's animation clock
};

// First-person player with Minecraft Beta 1.8's movement: a fixed 20 ticks/second
// simulation (camera interpolated between ticks), momentum with ground/air
// friction, sprinting, sneaking, swimming in water and lava, ladders and vines,
// cobwebs, stepping up half a block, fire, armor, hunger, eating and sleeping.
// Also handles aiming at, breaking, using and placing blocks and items.
class Player {
public:
    explicit Player(Vector3 spawnPosition);

    // Mouse look every frame; physics in fixed 20 Hz ticks.
    void Update(World& world, float dt, SoundSystem& sounds);

    // Aim, then hold left click to mine and right click to use or place; Q drops,
    // and middle click picks the aimed-at block in creative. Call after Update().
    void UpdateInteraction(World& world, SoundSystem& sounds, DroppedItems& drops, BlockParticles& particles);

    // With controls off (a screen is open), keys and the mouse don't move, turn
    // or act, but physics carries on.
    void SetControlsEnabled(bool enabled);

    // Throws a stack forward from the eyes (EntityPlayer.dropItem).
    void DropItem(const ItemStack& stack, DroppedItems& drops);

    BreakProgress GetBreakProgress() const;
    Vector3 GetPosition() const { return Vector3{ (float)posX, (float)posY, (float)posZ }; }
    Vector3 GetRenderPosition() const; // feet, interpolated between ticks for this frame
    Vector3 GetEyePosition() const;
    Vector3 LookVector() const;
    float YawDegrees() const;          // Minecraft's rotationYaw (0 = facing +Z, 90 = facing -X)
    float PitchDegrees() const;        // Minecraft's rotationPitch (positive = looking down)
    float GetTickAlpha() const { return renderAlpha; } // how far this frame is between ticks
    const Camera3D& GetCamera() const { return camera; }
    const RaycastHit& GetTarget() const { return target; }
    int GetSelectedSlot() const { return selectedSlot; }
    const ItemStack& HeldItem() const { return inventory.main[selectedSlot]; }
    ItemStack& HeldItemMutable() { return inventory.main[selectedSlot]; }
    Inventory& GetInventory() { return inventory; }
    const Inventory& GetInventory() const { return inventory; }
    HandPose GetHandPose() const;

    // player.dat: everything about the player a save keeps, as "key value..." lines.
    void Save(std::ostream& out) const;
    void Load(std::istream& in);

    void SetGameMode(GameMode mode);
    GameMode GetGameMode() const { return gameMode; }
    bool IsCreative() const { return gameMode == GameMode::Creative; }
    PlayerStats GetStats() const;

    // ---- What blocks do to the player
    void Hurt(float amount);                   // attackEntityFrom (armor applies)
    void SetFire(int ticks);                   // burn for at least this long
    bool IsBurning() const { return fireTicks > 0; }
    void SetInWeb() { inWeb = true; }
    void SetInPortal();
    float PortalTime() const { return portalTime; } // 0..1, the purple swirl
    void MultiplyMotion(double horizontal, double vertical);
    void AddVelocity(double x, double y, double z);
    // Is any of the player inside the box?
    bool Overlaps(const double min[3], const double max[3]) const;
    // Moved by a piston.
    void Push(World& world, double dx, double dy, double dz);

    // ---- Screens, messages, food, sleep
    void OpenContainer(ScreenKind kind, int x, int y, int z);
    // A screen a block asked for since last time, if any.
    bool TakeScreenRequest(ScreenKind& kind, int& x, int& y, int& z);
    void ShowMessage(const std::string& text);
    struct Message { std::string text; double time; };
    const std::vector<Message>& Messages() const { return messages; }
    bool CanEat(bool alwaysEdible) const;
    void AddFood(int hunger, float saturation);
    void TrySleep(int x, int y, int z, int direction);
    bool IsSleeping() const { return sleeping; }
    int SleepTimer() const { return sleepTimer; }
    // Beds: once asleep for long enough, the night is skipped (main does that) and you wake.
    bool SleptThroughNight() const { return sleeping && sleepTimer >= 100; }
    // Stands up beside the bed; with `setSpawn` (not when woken by damage) it becomes the spawn point.
    void WakeUp(World& world, bool setSpawn = true);
    void SetSpawnPoint(Vector3 point) { spawnPoint = point; }

    // ---- Settings from the options menu
    // Beta's mouse sensitivity (0..1, 0.5 = normal) and invert mouse.
    void SetMouse(float sensitivity, bool invert) { mouseSensitivity = sensitivity; invertMouse = invert; }
    void SetFov(float degrees) { baseFov = degrees; }
    void SetViewBobbing(bool on) { viewBobbing = on; }
    // 0 Peaceful (heals over time), 1 Easy, 2 Normal, 3 Hard: how far hunger can hurt you.
    void SetDifficulty(int level) { difficulty = level; }

private:
    // Feet position (bottom-center of the 0.6 x 1.8 collision box) and motion,
    // in blocks and blocks-per-tick. Doubles, as in Minecraft.
    double posX, posY, posZ;
    double prevX, prevY, prevZ; // position at the start of the tick, for interpolation
    double motionX, motionY, motionZ;

    float yaw;   // radians
    float pitch; // radians, positive = looking up

    bool onGround = false;
    bool collidedHorizontally = false;
    bool inWater = false;
    bool inLava = false;
    bool inWeb = false;
    bool sprinting = false;
    bool sneaking = false;
    bool prevForwardHeld = false; // for double-tap-W sprinting
    int sprintToggleTimer = 0;
    int jumpTicks = 0;
    double fallDistance = 0.0;
    double stepOffset = 0.0;      // eyes lag behind a step up, then catch up (Beta's ySize)

    // Distance walked, scaled by 0.6 like Minecraft; drives footsteps and view bobbing.
    double distanceWalked = 0.0;
    double prevDistanceWalked = 0.0;
    int nextStepDistance = 1;

    // View bobbing amount, fall/jump camera tilt (degrees), and sprint FOV multiplier.
    float bob = 0.0f, prevBob = 0.0f;
    float tilt = 0.0f, prevTilt = 0.0f;
    float fovModifier = 1.0f, prevFovModifier = 1.0f;

    double tickAccumulator = 0.0;
    int ticksThisFrame = 0; // how many physics ticks the last Update() ran; mining steps on the same clock

    // Mining, as in Minecraft's PlayerControllerSP.
    bool attackQueued = false; // left click pressed since the last tick
    bool breaking = false;
    int breakX = 0, breakY = 0, breakZ = 0;
    float breakProgress = 0.0f;
    int breakSoundCounter = 0;
    int blockHitDelay = 0;
    int rightClickDelay = 0;

    // Eating (EntityPlayer.setItemInUse).
    int useTicks = -1;
    int useSlot = -1;

    // First-person hand.
    bool swinging = false;
    int swingTicks = 0;
    float swingProgress = 0.0f, prevSwingProgress = 0.0f;
    ItemStack heldItem; // what the hand shows; catches up to the selection once lowered
    float equipProgress = 1.0f, prevEquipProgress = 1.0f;
    float armPitch = 0.0f, prevArmPitch = 0.0f, armYaw = 0.0f, prevArmYaw = 0.0f; // degrees, trail the view
    float renderAlpha = 0.0f; // fraction between ticks for this frame
    float renderBobPhase = 0.0f, renderBobAmount = 0.0f, renderTilt = 0.0f, renderHurtRoll = 0.0f;

    // Game mode, creative flight, survival health/hunger/XP.
    GameMode gameMode = GameMode::Survival;
    bool flying = false;
    bool prevJumpHeld = false; // for double-tap-space flying
    int flyToggleTimer = 0;
    float health = 20.0f;
    int foodLevel = 20;
    float saturation = 5.0f;
    float exhaustion = 0.0f;
    int foodTimer = 0;
    float experience = 0.0f;
    int experienceLevel = 0;
    int hurtTime = 0;          // counts down after taking damage; drives the camera tilt
    int hurtResistantTime = 0; // damage immunity after a hit
    float lastDamage = 0.0f;
    int armorCarry = 0;        // Beta's carryoverDamage
    int air = 300;
    int fireTicks = 0;
    bool inPortal = false;
    float portalTime = 0.0f;
    long long tickCount = 0;
    Vector3 spawnPoint;

    float mouseSensitivity = 0.5f;
    bool invertMouse = false;
    float baseFov = 110.0f;
    bool viewBobbing = true;
    int difficulty = 2;
    bool wasInWater = true; // no splash on the first tick

    bool sleeping = false;
    int sleepTimer = 0;
    int bedX = 0, bedY = 0, bedZ = 0;

    ScreenKind screenRequest = ScreenKind::None;
    int screenX = 0, screenY = 0, screenZ = 0;
    std::vector<Message> messages;

    Camera3D camera;
    RaycastHit target;
    Inventory inventory;
    int selectedSlot = 0;
    bool controlsEnabled = true;
    std::vector<ItemStack> deathDrops; // spilled on the next UpdateInteraction, which has the item list
    Vector3 deathPosition = { 0.0f, 0.0f, 0.0f };
    World* worldRef = nullptr;         // the world of the last update, for sounds from block hooks

    void UpdateLook();
    void PickBlock(const ItemStack& stack);
    void Tick(World& world, SoundSystem& sounds);
    void MoveWithHeading(World& world, double strafe, double forward, SoundSystem& sounds);
    void MoveWithHeadingNormal(World& world, double strafe, double forward, double airAccel, SoundSystem& sounds);
    void AddExhaustion(float amount);
    void AddMovementExhaustion(const World& world, double dx, double dy, double dz);
    void TickFood();
    void TickFireAndAir(World& world, SoundSystem& sounds);
    void Damage(float amount, bool armor);
    void Respawn();
    bool IsHeadInWater(const World& world) const;
    bool IsOnLadder(const World& world) const;
    void MoveFlying(double strafe, double forward, double accel);
    void MoveEntity(World& world, double dx, double dy, double dz, SoundSystem& sounds);
    void CollideWithBlocks(World& world);
    void UpdateCamera(float alpha);
    void BreakTick(World& world, SoundSystem& sounds, DroppedItems& drops, BlockParticles& particles);
    void BreakBlock(World& world, int x, int y, int z, DroppedItems& drops);
    void UseItem(World& world, SoundSystem& sounds);
    void TickUsing(World& world, SoundSystem& sounds);
    void StopBreaking();
    void Swing();
    void TickHand();
    float BreakDamagePerTick(const World& world, uint8_t block, int meta) const;
};
