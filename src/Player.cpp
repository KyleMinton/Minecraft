#include "Player.h"
#include "BlockParticles.h"
#include "Collision.h"
#include "DroppedItems.h"
#include "Items.h"

#include <algorithm>
#include <cmath>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>

namespace {

// Every movement constant below is Minecraft's, in its own units: blocks and
// ticks (1/20 s). Walking works out to 4.317 blocks/s and sprinting 5.612.
constexpr double TICK_SECONDS = 1.0 / 20.0;
constexpr int MAX_TICKS_PER_FRAME = 10; // after a long stall, drop the backlog instead of fast-forwarding

constexpr double HALF_WIDTH = 0.3;
constexpr double HEIGHT = 1.8;
constexpr float EYE_HEIGHT = 1.62f;
constexpr float SNEAK_EYE_DROP = 0.08f;
constexpr double STEP_HEIGHT = 0.5;      // EntityLiving.stepHeight

constexpr double GRAVITY = 0.08;
constexpr double VERTICAL_DRAG = 0.98;
constexpr double JUMP_VELOCITY = 0.42;
constexpr double SPRINT_JUMP_BOOST = 0.2;
constexpr int JUMP_COOLDOWN_TICKS = 10;

constexpr double WALK_SPEED = 0.1;
constexpr double SPRINT_SPEED = 0.13;
constexpr double AIR_ACCEL = 0.02;
constexpr double SPRINT_AIR_ACCEL = 0.026;
constexpr double AIR_FRICTION = 0.91;
constexpr double INPUT_SCALE = 0.98;
constexpr double SNEAK_INPUT_SCALE = 0.3;
constexpr double USING_INPUT_SCALE = 0.2; // walking while eating
constexpr double SPRINT_MIN_FORWARD = 0.8;
constexpr int SPRINT_DOUBLE_TAP_TICKS = 7;
constexpr double SNEAK_EDGE_STEP = 0.05;
constexpr double MIN_MOTION = 0.005;

constexpr double LIQUID_ACCEL = 0.02;
constexpr double WATER_DRAG = 0.8;
constexpr double LAVA_DRAG = 0.5;
constexpr double LIQUID_GRAVITY = 0.02;
constexpr double LIQUID_SWIM_UP = 0.04;
constexpr double LIQUID_CLIMB_OUT = 0.3;
constexpr double WATER_PUSH = 0.014;

constexpr double LADDER_MAX_SPEED = 0.15;
constexpr double LADDER_CLIMB = 0.2;

constexpr double WALK_DISTANCE_SCALE = 0.6; // footstep every 1/0.6 = 1.67 blocks

constexpr float MOUSE_RADIANS_PER_PIXEL = 0.15f * DEG2RAD;
constexpr float SPRINT_FOV_MODIFIER = 1.15f;
constexpr float FLYING_FOV_MODIFIER = 1.1f;
constexpr float SURVIVAL_REACH = 4.5f;
constexpr float CREATIVE_REACH = 5.0f;

// Creative flight.
constexpr double FLY_SPEED = 0.05;
constexpr double FLY_VERTICAL = FLY_SPEED * 3.0;
constexpr double FLY_VERTICAL_DRAG = 0.6;
constexpr int FLY_DOUBLE_TAP_TICKS = 7;
constexpr int CREATIVE_BREAK_DELAY_TICKS = 5;

// Health, hunger (FoodStats) and damage, at Normal difficulty.
constexpr float MAX_HEALTH = 20.0f;
constexpr int MAX_FOOD = 20;
constexpr float START_SATURATION = 5.0f;
constexpr float EXHAUSTION_PER_POINT = 4.0f;
constexpr float MAX_EXHAUSTION = 40.0f;
constexpr int FOOD_TICK_INTERVAL = 80;
constexpr int REGEN_MIN_FOOD = 18;
constexpr float REGEN_EXHAUSTION = 3.0f;
constexpr int SPRINT_MIN_FOOD = 7;
constexpr float JUMP_EXHAUSTION = 0.2f;
constexpr float SPRINT_JUMP_EXHAUSTION = 0.8f;
constexpr float BREAK_EXHAUSTION = 0.025f;
constexpr float HURT_EXHAUSTION = 0.3f;
constexpr float WALK_EXHAUSTION_PER_BLOCK = 0.01f;
constexpr float SPRINT_EXHAUSTION_PER_BLOCK = 0.099999994f;
constexpr float SWIM_EXHAUSTION_PER_BLOCK = 0.015f;
constexpr double SAFE_FALL_DISTANCE = 3.0;
constexpr int HURT_TIME_TICKS = 10;
constexpr int HURT_RESISTANT_TICKS = 20;
constexpr int MAX_AIR = 300;
constexpr int EAT_TICKS = 32;
constexpr int RIGHT_CLICK_REPEAT_TICKS = 4;

// Mining: progress per tick = speed / hardness / 30, or / 100 without the right tool.
constexpr float BREAK_PENALTY = 5.0f;
constexpr int BREAK_HIT_DELAY_TICKS = 5;
constexpr int BREAK_SOUND_INTERVAL_TICKS = 4;

// Dropping items (EntityPlayer.dropItem): thrown from just below the eyes.
constexpr float DROP_BELOW_EYES = 0.3f;
constexpr float THROW_SPEED = 0.3f;
constexpr int THROWN_PICKUP_DELAY = 40;

// First-person hand.
constexpr int SWING_TICKS = 6;
constexpr float EQUIP_STEP = 0.4f;
constexpr float ARM_LAG_EASE = 0.5f;

constexpr size_t MAX_MESSAGES = 10;

Box PlayerBox(double x, double y, double z) {
    return { { x - HALF_WIDTH, y, z - HALF_WIDTH }, { x + HALF_WIDTH, y + HEIGHT, z + HALF_WIDTH } };
}

float RandomFloat() {
    return GetRandomValue(0, 0xFFFFFF) / (float)0x1000000;
}

double StepTowardZero(double v, double step) {
    if (v < step && v >= -step) return 0.0;
    return v > 0.0 ? v - step : v + step;
}

// Block IDs of this game before it had every Beta block (for old player.dat files).
int LegacyItem(int old) {
    static const int map[18] = {
        BLOCK_AIR, BLOCK_GRASS, BLOCK_DIRT, BLOCK_STONE, BLOCK_SAND, BLOCK_WATER, BLOCK_LOG, BLOCK_LEAVES, BLOCK_COBBLESTONE,
        BLOCK_GRAVEL, BLOCK_BEDROCK, BLOCK_COAL_ORE, BLOCK_IRON_ORE, BLOCK_GOLD_ORE, BLOCK_DIAMOND_ORE, BLOCK_SANDSTONE,
        BLOCK_PLANKS, BLOCK_BRICKS,
    };
    return old >= 0 && old < 18 ? map[old] : 0;
}

bool SameStack(const ItemStack& a, const ItemStack& b) {
    return a.id == b.id && a.damage == b.damage && a.IsEmpty() == b.IsEmpty();
}

} // namespace

Player::Player(Vector3 spawnPosition)
    : posX(spawnPosition.x), posY(spawnPosition.y), posZ(spawnPosition.z),
      prevX(spawnPosition.x), prevY(spawnPosition.y), prevZ(spawnPosition.z),
      motionX(0.0), motionY(0.0), motionZ(0.0), yaw(0.0f), pitch(0.0f), spawnPoint(spawnPosition),
      camera{ 0 }, target{} {
    camera.projection = CAMERA_PERSPECTIVE;
    UpdateCamera(0.0f);
}

// ---- Saving

void Player::Save(std::ostream& out) const {
    out.precision(17);
    out << "version 2\n";
    out << "pos " << posX << " " << posY << " " << posZ << "\n";
    out << "motion " << motionX << " " << motionY << " " << motionZ << "\n";
    out << "look " << yaw << " " << pitch << "\n";
    out << "state " << (int)onGround << " " << fallDistance << " " << (int)flying << " " << selectedSlot << "\n";
    out << "gamemode " << (gameMode == GameMode::Creative ? 1 : 0) << "\n";
    out << "health " << health << "\n";
    out << "food " << foodLevel << " " << saturation << " " << exhaustion << " " << foodTimer << "\n";
    out << "xp " << experience << " " << experienceLevel << "\n";
    out << "air " << air << " " << fireTicks << "\n";
    out << "spawn " << spawnPoint.x << " " << spawnPoint.y << " " << spawnPoint.z << "\n";
    auto writeStack = [&](const char* kind, int slot, const ItemStack& stack) {
        if (!stack.IsEmpty()) out << kind << " " << slot << " " << stack.id << " " << stack.count << " " << stack.damage << "\n";
    };
    for (int i = 0; i < Inventory::MAIN_SIZE; i++) writeStack("slot", i, inventory.main[i]);
    for (int i = 0; i < Inventory::ARMOR_SIZE; i++) writeStack("armor", i, inventory.armor[i]);
    writeStack("offhand", 0, inventory.offhand);
}

void Player::Load(std::istream& in) {
    inventory = Inventory();
    int version = 1;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string key;
        fields >> key;
        if (key == "version") fields >> version;
        else if (key == "pos") fields >> posX >> posY >> posZ;
        else if (key == "motion") fields >> motionX >> motionY >> motionZ;
        else if (key == "look") fields >> yaw >> pitch;
        else if (key == "state") {
            int ground = 0, fly = 0;
            fields >> ground >> fallDistance >> fly >> selectedSlot;
            onGround = ground != 0;
            flying = fly != 0;
            selectedSlot = std::clamp(selectedSlot, 0, Inventory::HOTBAR_SIZE - 1);
        } else if (key == "gamemode") {
            int mode = 0;
            fields >> mode;
            gameMode = mode == 1 ? GameMode::Creative : GameMode::Survival;
        } else if (key == "health") fields >> health;
        else if (key == "food") fields >> foodLevel >> saturation >> exhaustion >> foodTimer;
        else if (key == "xp") fields >> experience >> experienceLevel;
        else if (key == "air") fields >> air >> fireTicks;
        else if (key == "spawn") fields >> spawnPoint.x >> spawnPoint.y >> spawnPoint.z;
        else if (key == "slot" || key == "armor" || key == "offhand") {
            int slot = -1, id = 0, count = 0, damage = 0;
            fields >> slot >> id >> count;
            if (version >= 2) fields >> damage;
            else id = LegacyItem(id);
            if (GetItemDef(id) == nullptr || count <= 0) continue;
            ItemStack stack(id, count, damage);
            stack.count = std::min(count, MaxStackSize(stack));
            if (key == "slot" && slot >= 0 && slot < Inventory::MAIN_SIZE) inventory.main[slot] = stack;
            if (key == "armor" && slot >= 0 && slot < Inventory::ARMOR_SIZE) inventory.armor[slot] = stack;
            if (key == "offhand") inventory.offhand = stack;
        }
    }
    prevX = posX; prevY = posY; prevZ = posZ;
    heldItem = HeldItem();
    UpdateCamera(0.0f);
}

void Player::SetControlsEnabled(bool enabled) {
    controlsEnabled = enabled;
    if (!enabled) {
        StopBreaking();
        attackQueued = false;
        useTicks = -1;
    }
}

// ---- Views of the player

Vector3 Player::GetRenderPosition() const {
    double a = renderAlpha;
    return Vector3{ (float)(prevX + (posX - prevX) * a), (float)(prevY + (posY - prevY) * a), (float)(prevZ + (posZ - prevZ) * a) };
}

Vector3 Player::GetEyePosition() const {
    return Vector3{ (float)posX, (float)posY + EYE_HEIGHT - (sneaking ? SNEAK_EYE_DROP : 0.0f), (float)posZ };
}

Vector3 Player::LookVector() const {
    return Vector3{ cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw) };
}

float Player::YawDegrees() const { return -yaw * RAD2DEG; }
float Player::PitchDegrees() const { return -pitch * RAD2DEG; }

void Player::DropItem(const ItemStack& stack, DroppedItems& drops) {
    if (stack.IsEmpty()) return;
    // Our yaw/pitch are Minecraft's negated: forward is (sin yaw, cos yaw), pitch up is positive.
    double y = posY - DROP_BELOW_EYES + EYE_HEIGHT;
    double mx = sin(yaw) * cos(pitch) * THROW_SPEED;
    double mz = cos(yaw) * cos(pitch) * THROW_SPEED;
    double my = sin(pitch) * THROW_SPEED + 0.1;
    float angle = RandomFloat() * 2.0f * PI;
    float spread = 0.02f * RandomFloat();
    mx += cos(angle) * spread;
    my += (RandomFloat() - RandomFloat()) * 0.1f;
    mz += sin(angle) * spread;
    drops.Spawn(posX, y, posZ, mx, my, mz, stack, THROWN_PICKUP_DELAY);
}

void Player::SetGameMode(GameMode mode) {
    gameMode = mode;
    if (mode == GameMode::Survival) flying = false;
    fallDistance = 0.0;
    fireTicks = 0;
    StopBreaking();
    blockHitDelay = 0;
}

PlayerStats Player::GetStats() const {
    PlayerStats stats;
    stats.health = health;
    stats.maxHealth = MAX_HEALTH;
    stats.food = foodLevel;
    stats.saturation = saturation;
    stats.experience = experience;
    stats.level = experienceLevel;
    stats.armor = inventory.TotalArmor();
    stats.air = air;
    stats.headInWater = worldRef != nullptr && IsHeadInWater(*worldRef);
    stats.hurtResistantTime = hurtResistantTime;
    stats.ticks = tickCount;
    return stats;
}

// ---- Health, hunger, fire

void Player::AddExhaustion(float amount) {
    if (gameMode == GameMode::Creative) return;
    exhaustion = std::min(exhaustion + amount, MAX_EXHAUSTION);
}

void Player::AddMovementExhaustion(const World& world, double dx, double dy, double dz) {
    if (IsHeadInWater(world)) {
        int cm = (int)lround(sqrt(dx * dx + dy * dy + dz * dz) * 100.0);
        if (cm > 0) AddExhaustion(SWIM_EXHAUSTION_PER_BLOCK * cm * 0.01f);
    } else if (inWater) {
        int cm = (int)lround(sqrt(dx * dx + dz * dz) * 100.0);
        if (cm > 0) AddExhaustion(SWIM_EXHAUSTION_PER_BLOCK * cm * 0.01f);
    } else if (onGround) {
        int cm = (int)lround(sqrt(dx * dx + dz * dz) * 100.0);
        if (cm > 0) AddExhaustion((sprinting ? SPRINT_EXHAUSTION_PER_BLOCK : WALK_EXHAUSTION_PER_BLOCK) * cm * 0.01f);
    }
}

// FoodStats.onUpdate: exhaustion eats saturation then food; a nearly full bar
// slowly heals you, an empty one slowly hurts you (never below half a heart on Normal).
void Player::TickFood() {
    // Peaceful: a heart back every second (EntityPlayer.onLivingUpdate).
    if (difficulty == 0 && health > 0.0f && health < MAX_HEALTH && tickCount % 20 == 0) health = std::min(health + 1.0f, MAX_HEALTH);
    if (exhaustion > EXHAUSTION_PER_POINT) {
        exhaustion -= EXHAUSTION_PER_POINT;
        if (saturation > 0.0f) saturation = std::max(saturation - 1.0f, 0.0f);
        else foodLevel = std::max(foodLevel - 1, 0);
    }
    if (foodLevel >= REGEN_MIN_FOOD && health > 0.0f && health < MAX_HEALTH) {
        if (++foodTimer >= FOOD_TICK_INTERVAL) {
            health = std::min(health + 1.0f, MAX_HEALTH);
            AddExhaustion(REGEN_EXHAUSTION);
            foodTimer = 0;
        }
    } else if (foodLevel <= 0) {
        // Starving stops at 5 hearts on Easy and half a heart on Normal; Hard has no floor.
        if (++foodTimer >= FOOD_TICK_INTERVAL) {
            if (health > 10.0f || difficulty >= 3 || (health > 1.0f && difficulty >= 2)) Damage(1.0f, false);
            foodTimer = 0;
        }
    } else {
        foodTimer = 0;
    }
}

void Player::Hurt(float amount) {
    Damage(amount, true);
}

// EntityLiving.attackEntityFrom: a hit grants a second of immunity; a bigger
// hit during that time only deals the difference. Armor soaks up to 80%
// (EntityPlayer.applyArmorCalculations: 25 - armor points out of 25).
void Player::Damage(float amount, bool armor) {
    if (gameMode == GameMode::Creative || health <= 0.0f || sleeping) {
        if (sleeping && gameMode != GameMode::Creative) {
            // Hurt in bed: get up beside it rather than staying at bed height.
            if (worldRef != nullptr) WakeUp(*worldRef, false);
            else sleeping = false;
        } else {
            return;
        }
    }
    int damage = (int)amount;
    if (armor && damage > 0) {
        int total = damage * (25 - inventory.TotalArmor()) + armorCarry;
        inventory.DamageArmor(damage);
        damage = total / 25;
        armorCarry = total % 25;
    }

    if (hurtResistantTime > HURT_RESISTANT_TICKS / 2) {
        if (damage <= lastDamage) return;
        health -= damage - lastDamage;
        lastDamage = (float)damage;
    } else {
        lastDamage = (float)damage;
        hurtResistantTime = HURT_RESISTANT_TICKS;
        hurtTime = HURT_TIME_TICKS;
        health -= damage;
        if (worldRef != nullptr) {
            worldRef->PlaySound("random.hurt", posX, posY, posZ, 1.0f, (RandomFloat() - RandomFloat()) * 0.2f + 1.0f);
        }
    }
    AddExhaustion(HURT_EXHAUSTION);
    if (health <= 0.0f) Respawn();
}

void Player::SetFire(int ticks) {
    if (gameMode == GameMode::Creative) return;
    fireTicks = std::max(fireTicks, ticks);
}

void Player::SetInPortal() {
    inPortal = true;
}

// No death screen: dying scatters your items where you fell and puts you
// straight back at spawn, fully restored.
void Player::Respawn() {
    deathDrops.clear();
    deathPosition = Vector3{ (float)posX, (float)posY, (float)posZ };
    for (ItemStack& stack : inventory.main) {
        if (!stack.IsEmpty()) deathDrops.push_back(stack);
        stack.Clear();
    }
    for (ItemStack& stack : inventory.armor) {
        if (!stack.IsEmpty()) deathDrops.push_back(stack);
        stack.Clear();
    }
    if (!inventory.offhand.IsEmpty()) deathDrops.push_back(inventory.offhand);
    inventory.offhand.Clear();

    posX = prevX = spawnPoint.x;
    posY = prevY = spawnPoint.y;
    posZ = prevZ = spawnPoint.z;
    motionX = motionY = motionZ = 0.0;
    fallDistance = 0.0;
    flying = false;
    sleeping = false;
    health = MAX_HEALTH;
    foodLevel = MAX_FOOD;
    saturation = START_SATURATION;
    exhaustion = 0.0f;
    foodTimer = 0;
    hurtTime = 0;
    hurtResistantTime = 0;
    fireTicks = 0;
    air = MAX_AIR;
    useTicks = -1;
    StopBreaking();
}

// Entity.onEntityUpdate: burning, drowning, lava, water putting you out.
void Player::TickFireAndAir(World& world, SoundSystem& sounds) {
    (void)sounds;
    if (gameMode == GameMode::Creative) {
        fireTicks = 0;
        air = MAX_AIR;
        return;
    }
    if (inWater) {
        if (fireTicks > 0) {
            world.PlaySound("random.fizz", posX, posY, posZ, 0.7f, 1.6f + (RandomFloat() - RandomFloat()) * 0.4f);
        }
        fireTicks = 0;
    }
    if (inLava) {
        Damage(4.0f, true);
        fireTicks = 600;
    }
    if (fireTicks > 0) {
        if (fireTicks % 20 == 0) Damage(1.0f, true);
        fireTicks--;
    }

    if (IsHeadInWater(world)) {
        air--;
        if (air == -20) {
            air = 0;
            for (int i = 0; i < 8; i++) {
                world.SpawnParticle(ParticleType::Bubble, posX + RandomFloat() - RandomFloat(), posY + EYE_HEIGHT + RandomFloat() - RandomFloat(),
                                    posZ + RandomFloat() - RandomFloat(), motionX, motionY, motionZ);
            }
            Damage(2.0f, true);
        }
    } else {
        air = MAX_AIR;
    }

    // Standing in a portal: the view swirls; Beta would take you to the Nether.
    if (inPortal) {
        if (portalTime == 0.0f) world.PlaySound("portal.trigger", posX, posY, posZ, 1.0f, RandomFloat() * 0.4f + 0.8f);
        portalTime += 0.0125f;
        if (portalTime >= 1.0f) {
            portalTime = 1.0f;
        }
        inPortal = false;
    } else if (portalTime > 0.0f) {
        portalTime = std::max(0.0f, portalTime - 0.05f);
    }
}

// ---- Things blocks and items call

void Player::MultiplyMotion(double horizontal, double vertical) {
    motionX *= horizontal;
    motionY *= vertical;
    motionZ *= horizontal;
}

void Player::AddVelocity(double x, double y, double z) {
    motionX += x;
    motionY += y;
    motionZ += z;
}

bool Player::Overlaps(const double min[3], const double max[3]) const {
    Box box = PlayerBox(posX, posY, posZ);
    return box.min[0] < max[0] && box.max[0] > min[0] && box.min[1] < max[1] && box.max[1] > min[1] && box.min[2] < max[2] &&
           box.max[2] > min[2];
}

void Player::Push(World& world, double dx, double dy, double dz) {
    Box box = PlayerBox(posX, posY, posZ);
    MoveResult r = MoveBox(world, box, dx, dy, dz, 0.0, onGround);
    posX += r.dx;
    posY += r.dy;
    posZ += r.dz;
}

void Player::OpenContainer(ScreenKind kind, int x, int y, int z) {
    screenRequest = kind;
    screenX = x;
    screenY = y;
    screenZ = z;
}

bool Player::TakeScreenRequest(ScreenKind& kind, int& x, int& y, int& z) {
    if (screenRequest == ScreenKind::None) return false;
    kind = screenRequest;
    x = screenX;
    y = screenY;
    z = screenZ;
    screenRequest = ScreenKind::None;
    return true;
}

void Player::ShowMessage(const std::string& text) {
    messages.push_back(Message{ text, GetTime() });
    if (messages.size() > MAX_MESSAGES) messages.erase(messages.begin());
}

bool Player::CanEat(bool alwaysEdible) const {
    return (alwaysEdible || foodLevel < MAX_FOOD) && gameMode != GameMode::Creative;
}

void Player::AddFood(int hunger, float saturationModifier) {
    foodLevel = std::min(foodLevel + hunger, MAX_FOOD);
    saturation = std::min(saturation + hunger * saturationModifier * 2.0f, (float)foodLevel);
}

// EntityPlayer.sleepInBedAt: lie down in the bed until morning.
void Player::TrySleep(int x, int y, int z, int direction) {
    (void)direction;
    if (sleeping) return;
    if (std::fabs(posX - x) > 3.0 || std::fabs(posY - y) > 2.0 || std::fabs(posZ - z) > 3.0) return;
    sleeping = true;
    sleepTimer = 0;
    bedX = x;
    bedY = y;
    bedZ = z;
    posX = prevX = x + 0.5;
    posY = prevY = y + 0.5625;
    posZ = prevZ = z + 0.5;
    motionX = motionY = motionZ = 0.0;
    StopBreaking();
}

// Wakes up standing beside the bed (BlockBed.getNearestEmptyChunkCoordinates),
// which also becomes the spawn point.
void Player::WakeUp(World& world, bool setSpawn) {
    if (!sleeping) return;
    sleeping = false;
    sleepTimer = 0;
    bool found = false;
    for (int r = 1; r <= 2 && !found; r++) {
        for (int dx = -r; dx <= r && !found; dx++) {
            for (int dz = -r; dz <= r && !found; dz++) {
                int x = bedX + dx, z = bedZ + dz;
                if (world.IsNormalCube(x, bedY - 1, z) && !world.IsSolidMaterial(x, bedY, z) && !world.IsSolidMaterial(x, bedY + 1, z)) {
                    posX = x + 0.5;
                    posY = bedY;
                    posZ = z + 0.5;
                    found = true;
                }
            }
        }
    }
    if (!found) {
        posX = bedX + 0.5;
        posY = bedY + 0.5625;
        posZ = bedZ + 0.5;
    }
    prevX = posX; prevY = posY; prevZ = posZ;
    if (!setSpawn) return;
    spawnPoint = Vector3{ (float)posX, (float)posY, (float)posZ };
    ShowMessage("Respawn point set");
}

// ---- Ticking

void Player::UpdateLook() {
    if (!controlsEnabled || sleeping) return;
    // EntityRenderer: sensitivity s turns by (0.6s + 0.2)^3 x 8 x 0.15 degrees a pixel.
    Vector2 mouseDelta = GetMouseDelta();
    float f = mouseSensitivity * 0.6f + 0.2f;
    float perPixel = f * f * f * 8.0f * MOUSE_RADIANS_PER_PIXEL;
    yaw -= mouseDelta.x * perPixel;
    pitch -= mouseDelta.y * perPixel * (invertMouse ? -1.0f : 1.0f);
    const float pitchLimit = 90.0f * DEG2RAD;
    pitch = std::clamp(pitch, -pitchLimit, pitchLimit);
}

void Player::Update(World& world, float dt, SoundSystem& sounds) {
    worldRef = &world;
    UpdateLook();
    // Checked every frame: most frames run no tick, so a press there would be missed.
    if (sleeping && controlsEnabled && IsKeyPressed(KEY_LEFT_SHIFT)) WakeUp(world);

    tickAccumulator += dt;
    int ticks = 0;
    while (tickAccumulator >= TICK_SECONDS && ticks < MAX_TICKS_PER_FRAME) {
        Tick(world, sounds);
        tickAccumulator -= TICK_SECONDS;
        ticks++;
    }
    if (tickAccumulator >= TICK_SECONDS) tickAccumulator = 0.0;
    ticksThisFrame = ticks;

    UpdateCamera((float)(tickAccumulator / TICK_SECONDS));
    sounds.SetListener(camera.position);
}

void Player::Tick(World& world, SoundSystem& sounds) {
    tickCount++;
    prevX = posX; prevY = posY; prevZ = posZ;
    prevDistanceWalked = distanceWalked;
    prevBob = bob;
    prevTilt = tilt;
    prevFovModifier = fovModifier;
    stepOffset *= 0.4;
    if (hurtTime > 0) hurtTime--;
    if (hurtResistantTime > 0) hurtResistantTime--;
    inventory.TickAnimations();

    if (sleeping) {
        sleepTimer = std::min(sleepTimer + 1, 100);
        TickHand();
        return;
    }

    // Like Minecraft, nothing moves in a chunk that isn't there yet (after
    // respawning far away, say), so the player can't fall into ungenerated terrain.
    if (!world.IsLoaded((int)floor(posX), (int)floor(posZ))) {
        motionX = motionY = motionZ = 0.0;
        TickHand();
        return;
    }

    // Beta's handleWaterMovement: in water up to the waist-ish box, pushed by the current.
    Box box = PlayerBox(posX, posY, posZ);
    Vector3 flow = { 0, 0, 0 };
    inWater = InLiquid(world, Box{ { box.min[0], box.min[1] + 0.4, box.min[2] }, { box.max[0], box.max[1] - 0.4, box.max[2] } }.Contract(0.001, 0.001, 0.001),
                       BlockMaterial::Water, &flow);
    if (inWater && !wasInWater && !flying) {
        // Entity.handleWaterMovement: jumping in splashes, with bubbles and spray.
        float strength = std::min(1.0f, (float)sqrt(motionX * motionX * 0.2 + motionY * motionY + motionZ * motionZ * 0.2) * 0.2f);
        world.PlaySound("random.splash", posX, posY, posZ, strength, 1.0f + (RandomFloat() - RandomFloat()) * 0.4f);
        double surface = floor(box.min[1]) + 1.0;
        for (int i = 0; i < 1 + (int)(HALF_WIDTH * 2 * 20); i++) {
            double ox = (RandomFloat() * 2.0 - 1.0) * HALF_WIDTH * 2, oz = (RandomFloat() * 2.0 - 1.0) * HALF_WIDTH * 2;
            world.SpawnParticle(ParticleType::Bubble, posX + ox, surface, posZ + oz, motionX, motionY - RandomFloat() * 0.2f, motionZ);
            ox = (RandomFloat() * 2.0 - 1.0) * HALF_WIDTH * 2;
            oz = (RandomFloat() * 2.0 - 1.0) * HALF_WIDTH * 2;
            world.SpawnParticle(ParticleType::Splash, posX + ox, surface, posZ + oz, motionX, motionY, motionZ);
        }
    }
    wasInWater = inWater;
    if (inWater) {
        fallDistance = 0.0;
        float len = sqrtf(flow.x * flow.x + flow.y * flow.y + flow.z * flow.z);
        if (len > 0.0f && !flying) {
            motionX += flow.x / len * WATER_PUSH;
            motionY += flow.y / len * WATER_PUSH;
            motionZ += flow.z / len * WATER_PUSH;
        }
    }
    inLava = TouchesMaterial(world, Box{ { box.min[0] + 0.1, box.min[1] + 0.4, box.min[2] + 0.1 }, { box.max[0] - 0.1, box.max[1] - 0.4, box.max[2] - 0.1 } },
                             BlockMaterial::Lava);
    TickFireAndAir(world, sounds);

    // Input is sampled once per tick, like Minecraft. With a screen open nothing is held.
    bool keys = controlsEnabled;
    double forward = (keys && IsKeyDown(KEY_W) ? 1.0 : 0.0) - (keys && IsKeyDown(KEY_S) ? 1.0 : 0.0);
    double strafe = (keys && IsKeyDown(KEY_D) ? 1.0 : 0.0) - (keys && IsKeyDown(KEY_A) ? 1.0 : 0.0); // positive = right
    bool jumpHeld = keys && IsKeyDown(KEY_SPACE);
    sneaking = keys && IsKeyDown(KEY_LEFT_SHIFT);
    if (sneaking) {
        forward *= SNEAK_INPUT_SCALE;
        strafe *= SNEAK_INPUT_SCALE;
    }
    if (useTicks >= 0) {
        forward *= USING_INPUT_SCALE;
        strafe *= USING_INPUT_SCALE;
    }

    // Creative: double-tap space to start or stop flying; space rises, shift descends.
    if (flyToggleTimer > 0) flyToggleTimer--;
    if (gameMode == GameMode::Creative && jumpHeld && !prevJumpHeld) {
        if (flyToggleTimer == 0) {
            flyToggleTimer = FLY_DOUBLE_TAP_TICKS;
        } else {
            flying = !flying;
            flyToggleTimer = 0;
        }
    }
    prevJumpHeld = jumpHeld;
    if (flying) {
        if (sneaking) motionY -= FLY_VERTICAL;
        if (jumpHeld) motionY += FLY_VERTICAL;
    }

    // Sprinting (new in Beta 1.8): double-tap W on the ground while moving
    // forward. Too hungry to sprint at 3 drumsticks or fewer.
    bool forwardHeld = forward >= SPRINT_MIN_FORWARD;
    bool enoughFood = gameMode == GameMode::Creative || foodLevel >= SPRINT_MIN_FOOD;
    if (sprintToggleTimer > 0) sprintToggleTimer--;
    if (onGround && !prevForwardHeld && forwardHeld && !sprinting && enoughFood && useTicks < 0) {
        if (sprintToggleTimer > 0) sprinting = true;
        else sprintToggleTimer = SPRINT_DOUBLE_TAP_TICKS;
    }
    if (!sprinting && forwardHeld && enoughFood && keys && IsKeyDown(KEY_LEFT_CONTROL)) sprinting = true;
    if (sprinting && (!forwardHeld || collidedHorizontally || !enoughFood)) sprinting = false;
    prevForwardHeld = forwardHeld;

    if (jumpTicks > 0) jumpTicks--;
    if (fabs(motionX) < MIN_MOTION) motionX = 0.0;
    if (fabs(motionY) < MIN_MOTION) motionY = 0.0;
    if (fabs(motionZ) < MIN_MOTION) motionZ = 0.0;

    // Holding space keeps jumping each time you land; in water and lava it swims upward.
    if (jumpHeld && !flying) {
        if (inWater || inLava) {
            motionY += LIQUID_SWIM_UP;
        } else if (onGround && jumpTicks == 0) {
            motionY = JUMP_VELOCITY;
            if (sprinting) {
                motionX += sin(yaw) * SPRINT_JUMP_BOOST;
                motionZ += cos(yaw) * SPRINT_JUMP_BOOST;
            }
            AddExhaustion(sprinting ? SPRINT_JUMP_EXHAUSTION : JUMP_EXHAUSTION);
            jumpTicks = JUMP_COOLDOWN_TICKS;
        }
    } else {
        jumpTicks = 0;
    }

    double startX = posX, startY = posY, startZ = posZ;
    MoveWithHeading(world, strafe * INPUT_SCALE, forward * INPUT_SCALE, sounds);
    AddMovementExhaustion(world, posX - startX, posY - startY, posZ - startZ);

    if (flying && onGround) flying = false; // touching the ground ends flight
    if (flying) fallDistance = 0.0;
    if (gameMode == GameMode::Survival) TickFood();

    // View bobbing and fall tilt follow this tick's motion after friction.
    float speed = (float)sqrt(motionX * motionX + motionZ * motionZ);
    float tiltTarget = atanf((float)(-motionY * 0.2)) * 15.0f;
    if (speed > 0.1f) speed = 0.1f;
    if (!onGround) speed = 0.0f;
    if (onGround) tiltTarget = 0.0f;
    bob += (speed - bob) * 0.4f;
    tilt += (tiltTarget - tilt) * 0.8f;

    float fovTarget = (sprinting ? SPRINT_FOV_MODIFIER : 1.0f) * (flying ? FLYING_FOV_MODIFIER : 1.0f);
    fovModifier += (fovTarget - fovModifier) * 0.5f;

    TickUsing(world, sounds);
    TickHand();
}

void Player::TickHand() {
    float viewPitch = -pitch * RAD2DEG, viewYaw = -yaw * RAD2DEG; // Minecraft's angle conventions
    prevArmPitch = armPitch;
    prevArmYaw = armYaw;
    armPitch += (viewPitch - armPitch) * ARM_LAG_EASE;
    armYaw += (viewYaw - armYaw) * ARM_LAG_EASE;

    prevSwingProgress = swingProgress;
    if (swinging) {
        swingTicks++;
        if (swingTicks >= SWING_TICKS) {
            swingTicks = 0;
            swinging = false;
        }
    } else {
        swingTicks = 0;
    }
    swingProgress = (float)swingTicks / SWING_TICKS;

    // Switching items lowers the hand, swaps what it shows once it's out of view, then raises it.
    prevEquipProgress = equipProgress;
    const ItemStack& selected = HeldItem();
    float raised = SameStack(heldItem, selected) ? 1.0f : 0.0f;
    equipProgress += std::clamp(raised - equipProgress, -EQUIP_STEP, EQUIP_STEP);
    if (equipProgress < 0.1f) heldItem = selected;
    if (SameStack(heldItem, selected)) heldItem = selected; // counts and wear change without re-equipping
}

void Player::Swing() {
    if (!swinging || swingTicks >= SWING_TICKS / 2 || swingTicks < 0) {
        swingTicks = -1;
        swinging = true;
    }
}

HandPose Player::GetHandPose() const {
    float a = renderAlpha;
    HandPose pose;
    pose.item = heldItem;
    pose.equipProgress = prevEquipProgress + (equipProgress - prevEquipProgress) * a;
    float swingDelta = swingProgress - prevSwingProgress;
    if (swingDelta < 0.0f) swingDelta += 1.0f;
    pose.swingProgress = prevSwingProgress + swingDelta * a;
    pose.bobPhase = renderBobPhase;
    pose.bobAmount = renderBobAmount;
    pose.tilt = renderTilt;
    pose.pitch = -pitch * RAD2DEG;
    pose.yaw = -yaw * RAD2DEG;
    pose.lagPitch = pose.pitch - (prevArmPitch + (armPitch - prevArmPitch) * a);
    pose.lagYaw = pose.yaw - (prevArmYaw + (armYaw - prevArmYaw) * a);
    pose.hurtRoll = renderHurtRoll;
    pose.useTicks = useTicks >= 0 ? useTicks + a : -1.0f;
    pose.useDuration = EAT_TICKS;
    return pose;
}

bool Player::IsOnLadder(const World& world) const {
    int x = (int)floor(posX), y = (int)floor(posY), z = (int)floor(posZ);
    BlockId id = world.GetBlock(x, y, z);
    return id != BLOCK_AIR && GetBlockDef(id).IsLadder(world.GetMeta(x, y, z));
}

void Player::MoveWithHeading(World& world, double strafe, double forward, SoundSystem& sounds) {
    if (flying) {
        double keepMotionY = motionY;
        MoveWithHeadingNormal(world, strafe, forward, FLY_SPEED * (sprinting ? 2.0 : 1.0), sounds);
        motionY = keepMotionY * FLY_VERTICAL_DRAG;
        return;
    }
    MoveWithHeadingNormal(world, strafe, forward, sprinting ? SPRINT_AIR_ACCEL : AIR_ACCEL, sounds);
}

// EntityLiving.moveEntityWithHeading.
void Player::MoveWithHeadingNormal(World& world, double strafe, double forward, double airAccel, SoundSystem& sounds) {
    if ((inWater || inLava) && !flying) {
        double startY = posY;
        MoveFlying(strafe, forward, LIQUID_ACCEL);
        MoveEntity(world, motionX, motionY, motionZ, sounds);
        double drag = inWater ? WATER_DRAG : LAVA_DRAG;
        motionX *= drag;
        motionY *= drag;
        motionZ *= drag;
        motionY -= LIQUID_GRAVITY;

        // Swimming into a ledge with open, dry space above boosts you up and out.
        if (collidedHorizontally) {
            Box probe = PlayerBox(posX, posY, posZ).Offset(motionX, motionY + 0.6 - posY + startY, motionZ);
            if (!AnyCollision(world, probe) && !TouchesMaterial(world, probe, BlockMaterial::Water) && !TouchesMaterial(world, probe, BlockMaterial::Lava)) {
                motionY = LIQUID_CLIMB_OUT;
            }
        }
        return;
    }

    double friction = AIR_FRICTION;
    if (onGround) {
        BlockId below = world.GetBlock((int)floor(posX), (int)floor(posY) - 1, (int)floor(posZ));
        friction = (below == BLOCK_AIR ? 0.6f : GetBlockDef(below).slipperiness) * AIR_FRICTION;
    }
    double accel = onGround ? (sprinting ? SPRINT_SPEED : WALK_SPEED) * (0.16277136 / (friction * friction * friction)) : airAccel;
    MoveFlying(strafe, forward, accel);

    // Ladders and vines: slow sideways, no fast falls, and pushing into them climbs.
    bool ladder = IsOnLadder(world) && !flying;
    if (ladder) {
        fallDistance = 0.0;
        motionX = std::clamp(motionX, -LADDER_MAX_SPEED, LADDER_MAX_SPEED);
        motionZ = std::clamp(motionZ, -LADDER_MAX_SPEED, LADDER_MAX_SPEED);
        if (motionY < -LADDER_MAX_SPEED) motionY = -LADDER_MAX_SPEED;
        if (sneaking && motionY < 0.0) motionY = 0.0;
    }

    MoveEntity(world, motionX, motionY, motionZ, sounds);
    if (collidedHorizontally && IsOnLadder(world) && !flying) motionY = LADDER_CLIMB;

    motionY -= GRAVITY;
    motionY *= VERTICAL_DRAG;
    motionX *= friction;
    motionZ *= friction;
}

void Player::MoveFlying(double strafe, double forward, double accel) {
    double length = strafe * strafe + forward * forward;
    if (length < 1.0e-4) return;
    length = sqrt(length);
    if (length < 1.0) length = 1.0;
    length = accel / length;
    strafe *= length;
    forward *= length;
    double s = sin(yaw), c = cos(yaw);
    motionX += forward * s - strafe * c;
    motionZ += forward * c + strafe * s;
}

// Entity.moveEntity.
void Player::MoveEntity(World& world, double dx, double dy, double dz, SoundSystem& sounds) {
    (void)sounds;
    if (inWeb) {
        inWeb = false;
        dx *= 0.25;
        dy *= 0.05;
        dz *= 0.25;
        motionX = motionY = motionZ = 0.0;
    }

    Box box = PlayerBox(posX, posY, posZ);
    bool sneakingOnGround = onGround && sneaking;

    // Sneaking: pull horizontal movement back until there's still ground within a
    // block below, so you can't walk off an edge.
    if (sneakingOnGround) {
        while (dx != 0.0 && !AnyCollision(world, box.Offset(dx, -1.0, 0.0))) dx = StepTowardZero(dx, SNEAK_EDGE_STEP);
        while (dz != 0.0 && !AnyCollision(world, box.Offset(0.0, -1.0, dz))) dz = StepTowardZero(dz, SNEAK_EDGE_STEP);
        while (dx != 0.0 && dz != 0.0 && !AnyCollision(world, box.Offset(dx, -1.0, dz))) {
            dx = StepTowardZero(dx, SNEAK_EDGE_STEP);
            dz = StepTowardZero(dz, SNEAK_EDGE_STEP);
        }
    }

    MoveResult r = MoveBox(world, box, dx, dy, dz, flying ? 0.0 : STEP_HEIGHT, onGround);
    posX += r.dx;
    posY += r.dy;
    posZ += r.dz;
    stepOffset += r.stepped;

    collidedHorizontally = r.collidedX || r.collidedZ;
    onGround = r.onGround;
    if (r.collidedX) motionX = 0.0;
    if (r.collidedY) motionY = 0.0;
    if (r.collidedZ) motionZ = 0.0;

    int fx = (int)floor(posX), fy = (int)floor(posY - 0.2), fz = (int)floor(posZ);
    BlockId under = world.GetBlock(fx, fy, fz);
    if (under == BLOCK_AIR && world.GetBlock(fx, fy - 1, fz) == BLOCK_FENCE) under = BLOCK_FENCE;

    if (onGround) {
        // Landing (Entity.updateFallState): the block feels it, and each block
        // fallen past 3 costs half a heart.
        if (fallDistance > 0.0) {
            if (under != BLOCK_AIR) GetBlockDef(under).OnFallenUpon(world, fx, fy, fz, *this, (float)fallDistance);
            int damage = (int)ceilf((float)fallDistance - (float)SAFE_FALL_DISTANCE);
            if (gameMode == GameMode::Survival && damage > 0) {
                Damage((float)damage, true);
                if (under != BLOCK_AIR) {
                    const Block& def = GetBlockDef(under);
                    world.PlaySound(StepSoundEvent(def.sound), posX, posY, posZ, StepSoundVolume(def.sound) * 0.5f, StepSoundPitch(def.sound) * 0.75f);
                }
            }
        }
        fallDistance = 0.0;
    } else if (r.dy < 0.0) {
        fallDistance -= r.dy;
    }

    // Footsteps: one each time the scaled walk distance passes a whole number.
    // Sneaking on the ground is silent. The block underfoot hears it too.
    if (!sneakingOnGround && !IsOnLadder(world)) {
        distanceWalked += sqrt(r.dx * r.dx + r.dz * r.dz) * WALK_DISTANCE_SCALE;
        if (distanceWalked > nextStepDistance && under != BLOCK_AIR) {
            nextStepDistance = (int)distanceWalked + 1;
            const Block& def = GetBlockDef(under);
            StepSound sound = def.sound;
            if (world.GetBlock(fx, fy + 1, fz) == BLOCK_SNOW_LAYER) sound = GetBlockDef(BLOCK_SNOW_LAYER).sound;
            if (!def.IsLiquid() && !inWater) {
                world.PlaySound(StepSoundEvent(sound), posX, posY, posZ, StepSoundVolume(sound) * 0.15f, StepSoundPitch(sound));
            }
            def.OnPlayerWalking(world, fx, fy, fz, *this);
        }
    }

    CollideWithBlocks(world);
}

// Entity.doBlockCollisions: every block the player's box is in feels it (webs,
// soul sand, cactus, fire, portals, pressure plates).
void Player::CollideWithBlocks(World& world) {
    Box box = PlayerBox(posX, posY, posZ).Contract(0.001, 0.001, 0.001);
    int x0 = (int)floor(box.min[0]), x1 = (int)floor(box.max[0]);
    int y0 = (int)floor(box.min[1]), y1 = (int)floor(box.max[1]);
    int z0 = (int)floor(box.min[2]), z1 = (int)floor(box.max[2]);
    for (int x = x0; x <= x1; x++)
        for (int y = y0; y <= y1; y++)
            for (int z = z0; z <= z1; z++) {
                BlockId id = world.GetBlock(x, y, z);
                if (id != BLOCK_AIR) GetBlockDef(id).OnPlayerCollided(world, x, y, z, *this);
            }
}

// Interpolates between the last two ticks and applies Minecraft's view bobbing.
void Player::UpdateCamera(float alpha) {
    float x = (float)(prevX + (posX - prevX) * alpha);
    float y = (float)(prevY + (posY - prevY) * alpha) + EYE_HEIGHT - (sneaking ? SNEAK_EYE_DROP : 0.0f);
    float z = (float)(prevZ + (posZ - prevZ) * alpha);
    y -= (float)stepOffset;
    if (sleeping) y = (float)(bedY + 0.7);

    float walkedThisTick = (float)(distanceWalked - prevDistanceWalked);
    float phase = -(float)(distanceWalked + walkedThisTick * alpha) * PI;
    float bobAmount = (sleeping || !viewBobbing) ? 0.0f : prevBob + (bob - prevBob) * alpha;
    float tiltAmount = viewBobbing ? prevTilt + (tilt - prevTilt) * alpha : 0.0f;

    float hurtRoll = 0.0f;
    float hurt = (float)hurtTime - alpha;
    if (hurt >= 0.0f) {
        hurt /= HURT_TIME_TICKS;
        hurtRoll = -sinf(hurt * hurt * hurt * hurt * PI) * 14.0f;
    }

    renderAlpha = alpha;
    renderBobPhase = phase;
    renderBobAmount = bobAmount;
    renderTilt = tiltAmount;
    renderHurtRoll = hurtRoll;

    float bobSide = sinf(phase) * bobAmount * 0.5f;
    float bobUp = fabsf(cosf(phase) * bobAmount);
    float rollRadians = (sinf(phase) * bobAmount * 3.0f + hurtRoll) * DEG2RAD;
    float pitchDownRadians = (fabsf(cosf(phase - 0.2f) * bobAmount) * 5.0f + tiltAmount) * DEG2RAD;

    float viewPitch = pitch - pitchDownRadians;
    Vector3 forward = { cosf(viewPitch) * sinf(yaw), sinf(viewPitch), cosf(viewPitch) * cosf(yaw) };
    Vector3 right = { -cosf(yaw), 0.0f, sinf(yaw) };
    Vector3 up = {
        right.y * forward.z - right.z * forward.y,
        right.z * forward.x - right.x * forward.z,
        right.x * forward.y - right.y * forward.x
    };

    x += -right.x * bobSide + up.x * bobUp;
    y += -right.y * bobSide + up.y * bobUp;
    z += -right.z * bobSide + up.z * bobUp;

    float cr = cosf(rollRadians), sr = sinf(rollRadians);
    camera.position = Vector3{ x, y, z };
    camera.target = Vector3{ x + forward.x, y + forward.y, z + forward.z };
    camera.up = Vector3{ up.x * cr + right.x * sr, up.y * cr + right.y * sr, up.z * cr + right.z * sr };
    camera.fovy = baseFov * (prevFovModifier + (fovModifier - prevFovModifier) * alpha);
}

// Entity.isInsideOfMaterial: eyes below the water's surface in their block.
bool Player::IsHeadInWater(const World& world) const {
    double eye = posY + EYE_HEIGHT;
    int x = (int)floor(posX), y = (int)floor(eye), z = (int)floor(posZ);
    if (world.GetMaterial(x, y, z) != BlockMaterial::Water) return false;
    double surface = (y + 1) - (FluidHeightPercent(world.GetMeta(x, y, z)) - 0.1111111);
    return eye < surface;
}

// ---- Using things

void Player::UpdateInteraction(World& world, SoundSystem& sounds, DroppedItems& drops, BlockParticles& particles) {
    worldRef = &world;
    for (const ItemStack& stack : deathDrops) {
        float speed = RandomFloat() * 0.5f;
        float angle = RandomFloat() * 2.0f * PI;
        drops.Spawn(deathPosition.x, deathPosition.y - DROP_BELOW_EYES + EYE_HEIGHT, deathPosition.z,
                    -sin(angle) * speed, 0.2, cos(angle) * speed, stack, THROWN_PICKUP_DELAY);
    }
    deathDrops.clear();

    if (!controlsEnabled || sleeping) return;

    for (int i = 0; i < Inventory::HOTBAR_SIZE; i++) {
        if (IsKeyPressed(KEY_ONE + i)) selectedSlot = i;
    }
    float wheel = GetMouseWheelMove();
    if (wheel < 0.0f) selectedSlot = (selectedSlot + 1) % Inventory::HOTBAR_SIZE;
    if (wheel > 0.0f) selectedSlot = (selectedSlot + Inventory::HOTBAR_SIZE - 1) % Inventory::HOTBAR_SIZE;

    // Q throws one of the held item, Ctrl+Q the whole stack.
    if (IsKeyPressed(KEY_Q)) {
        ItemStack& held = inventory.main[selectedSlot];
        if (!held.IsEmpty()) {
            bool wholeStack = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            DropItem(held.Split(wholeStack ? held.count : 1), drops);
        }
    }

    Vector3 look = { camera.target.x - camera.position.x, camera.target.y - camera.position.y, camera.target.z - camera.position.z };
    float reach = (gameMode == GameMode::Creative) ? CREATIVE_REACH : SURVIVAL_REACH;
    target = world.Raycast(camera.position, look, reach);

    if (gameMode == GameMode::Creative && target.hit && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
        BlockId id = world.GetBlock(target.x, target.y, target.z);
        PickBlock(GetBlockDef(id).PickItem(world, target.x, target.y, target.z));
    }

    // Right click: once on press, then every 4 ticks while held (Minecraft.rightClickDelayTimer).
    if (rightClickDelay > 0) rightClickDelay = std::max(0, rightClickDelay - ticksThisFrame);
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && rightClickDelay == 0 && useTicks < 0)) {
        rightClickDelay = RIGHT_CLICK_REPEAT_TICKS;
        UseItem(world, sounds);
        target = world.Raycast(camera.position, look, reach);
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        attackQueued = true;
        Swing();
    }
    for (int i = 0; i < ticksThisFrame; i++) {
        BreakTick(world, sounds, drops, particles);
        if (!breaking) target = world.Raycast(camera.position, look, reach);
    }
}

// Minecraft.clickMouse(1): use the block (a door, a chest...), else use the item
// on the block (place it), else use the item in the air (buckets, food).
void Player::UseItem(World& world, SoundSystem& sounds) {
    (void)sounds;
    if (useTicks >= 0) return;
    ItemStack& held = inventory.main[selectedSlot];
    if (target.hit) {
        BlockId id = world.GetBlock(target.x, target.y, target.z);
        if (id != BLOCK_AIR && !sneaking && GetBlockDef(id).OnActivated(world, target.x, target.y, target.z, *this)) {
            Swing();
            return;
        }
        if (!held.IsEmpty()) {
            const ItemDef* def = GetItemDef(held.id);
            if (def != nullptr && def->UseOnBlock(held, *this, world, target.x, target.y, target.z, target.side)) {
                if (held.count <= 0) held.Clear();
                Swing();
                return;
            }
        }
    }
    if (held.IsEmpty()) return;
    const ItemDef* def = GetItemDef(held.id);
    if (def == nullptr) return;
    if (def->IsFood()) {
        if (CanEat(def->alwaysEdible)) {
            useTicks = 0;
            useSlot = selectedSlot;
        }
        return;
    }
    if (def->UseInAir(held, *this, world)) {
        if (held.count <= 0) held.Clear();
        Swing();
    }
}

// Eating: 32 ticks of holding right click, then the food is gone.
void Player::TickUsing(World& world, SoundSystem& sounds) {
    (void)world;
    (void)sounds;
    if (useTicks < 0) return;
    ItemStack& held = inventory.main[selectedSlot];
    const ItemDef* def = held.IsEmpty() ? nullptr : GetItemDef(held.id);
    if (selectedSlot != useSlot || def == nullptr || !def->IsFood() || !controlsEnabled || !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        useTicks = -1;
        return;
    }
    if (++useTicks < EAT_TICKS) return;
    useTicks = -1;
    AddFood(def->foodHunger, def->foodSaturation);
    if (held.id == ITEM_MUSHROOM_STEW) {
        held = ItemStack(ITEM_BOWL, 1);
    } else {
        held.Split(1);
    }
    rightClickDelay = RIGHT_CLICK_REPEAT_TICKS;
}

// Creative middle click (InventoryPlayer.setCurrentItem).
void Player::PickBlock(const ItemStack& stack) {
    if (stack.IsEmpty()) return;
    for (int i = 0; i < Inventory::HOTBAR_SIZE; i++) {
        if (SameItem(inventory.main[i], stack)) {
            selectedSlot = i;
            return;
        }
    }
    for (int i = 0; i < Inventory::HOTBAR_SIZE; i++) {
        int slot = (selectedSlot + i) % Inventory::HOTBAR_SIZE;
        if (inventory.main[slot].IsEmpty()) {
            selectedSlot = slot;
            break;
        }
    }
    inventory.main[selectedSlot] = ItemStack(stack.id, 1, stack.damage);
}

// ---- Mining

void Player::StopBreaking() {
    breaking = false;
    breakProgress = 0.0f;
    breakSoundCounter = 0;
}

// Block.blockStrength: the held tool's speed on it, over its hardness; blocks
// that need a tool you don't have take more than three times as long.
float Player::BreakDamagePerTick(const World& world, BlockId block, int meta) const {
    const Block& def = GetBlockDef(block);
    if (def.hardness < 0.0f) return 0.0f;
    const ItemStack& held = HeldItem();
    if (!CanHarvest(held, block)) return 1.0f / def.hardness / 100.0f;
    float speed = ToolSpeed(held, block, meta);
    if (IsHeadInWater(world)) speed /= BREAK_PENALTY;
    if (!onGround) speed /= BREAK_PENALTY;
    return speed / def.hardness / 30.0f;
}

// PlayerControllerSP.sendBlockRemoved and onPlayerDestroyBlock.
void Player::BreakBlock(World& world, int x, int y, int z, DroppedItems& drops) {
    (void)drops;
    BlockId id = world.GetBlock(x, y, z);
    int meta = world.GetMeta(x, y, z);
    if (id == BLOCK_AIR) return;
    const Block& def = GetBlockDef(id);
    world.BreakEffects(x, y, z, id, meta);
    bool removed = world.SetBlock(x, y, z, BLOCK_AIR);
    if (removed) def.OnDestroyedByPlayer(world, x, y, z, meta, *this);
    if (gameMode == GameMode::Creative) return;

    ItemStack& held = inventory.main[selectedSlot];
    bool harvest = CanHarvest(held, id);
    const ItemDef* tool = held.IsEmpty() ? nullptr : GetItemDef(held.id);
    bool shears = tool != nullptr && tool->tool == ToolType::Shears;
    if (tool != nullptr) {
        if (tool->tool == ToolType::Sword) DamageItem(held, 2);
        else if (tool->tool == ToolType::Shears) {
            if (id == BLOCK_LEAVES || id == BLOCK_WEB || id == BLOCK_TALL_GRASS || id == BLOCK_VINE) DamageItem(held, 1);
        } else if (tool->tool != ToolType::None && tool->tool != ToolType::Hoe) DamageItem(held, 1);
    }
    if (removed && harvest) {
        // Shears take leaves, grass and vines themselves (BlockLeaves/BlockTallGrass/BlockVine.harvestBlock).
        if (shears && (id == BLOCK_LEAVES || id == BLOCK_TALL_GRASS || id == BLOCK_VINE || id == BLOCK_DEAD_BUSH)) {
            world.SpawnItem(x, y, z, ItemStack(id, 1, id == BLOCK_LEAVES ? (meta & 3) : id == BLOCK_TALL_GRASS ? meta : 0));
        } else {
            world.DropBlockAsItem(x, y, z, id, meta);
        }
    }
    AddExhaustion(BREAK_EXHAUSTION);
}

// One tick of Beta's block-mining logic (PlayerControllerSP).
void Player::BreakTick(World& world, SoundSystem& sounds, DroppedItems& drops, BlockParticles& particles) {
    (void)sounds;
    bool justClicked = attackQueued;
    attackQueued = false;

    if (!(IsMouseButtonDown(MOUSE_BUTTON_LEFT) || justClicked) || !target.hit || useTicks >= 0) {
        StopBreaking();
        return;
    }
    Swing();

    // Creative: a click breaks the block instantly; holding breaks another every 5 ticks.
    if (gameMode == GameMode::Creative) {
        auto breakTarget = [&]() {
            BlockId id = world.GetBlock(target.x, target.y, target.z);
            if (id == BLOCK_AIR) return;
            GetBlockDef(id).OnClicked(world, target.x, target.y, target.z, *this);
            if (world.GetBlock(target.x, target.y, target.z) != id) return;
            BreakBlock(world, target.x, target.y, target.z, drops);
            blockHitDelay = CREATIVE_BREAK_DELAY_TICKS;
        };
        if (justClicked) breakTarget();
        if (blockHitDelay > 0) {
            blockHitDelay--;
            return;
        }
        breakTarget();
        return;
    }

    if (blockHitDelay > 0) {
        blockHitDelay--;
        return;
    }

    bool sameBlock = breaking && breakX == target.x && breakY == target.y && breakZ == target.z;
    if (!sameBlock) {
        StopBreaking();
        breaking = true;
        breakX = target.x;
        breakY = target.y;
        breakZ = target.z;
        if (!justClicked) return;
        // Clicking a block (Block.onBlockClicked): doors open, TNT lights, notes play...
        BlockId id = world.GetBlock(breakX, breakY, breakZ);
        if (id != BLOCK_AIR) {
            GetBlockDef(id).OnClicked(world, breakX, breakY, breakZ, *this);
            if (world.GetBlock(breakX, breakY, breakZ) != id) {
                StopBreaking();
                return;
            }
            // Blocks that break at once (flowers, torches) go on the first hit.
            if (BreakDamagePerTick(world, id, world.GetMeta(breakX, breakY, breakZ)) >= 1.0f) {
                BreakBlock(world, breakX, breakY, breakZ, drops);
                StopBreaking();
                blockHitDelay = BREAK_HIT_DELAY_TICKS;
                return;
            }
        }
    }

    BlockId id = world.GetBlock(breakX, breakY, breakZ);
    if (id == BLOCK_AIR) {
        StopBreaking();
        return;
    }
    int meta = world.GetMeta(breakX, breakY, breakZ);
    const Block& block = GetBlockDef(id);
    breakProgress += BreakDamagePerTick(world, id, meta);
    particles.AddHitEffect(world, breakX, breakY, breakZ, target.side, id, meta);
    if (breakSoundCounter % BREAK_SOUND_INTERVAL_TICKS == 0 && block.sound != StepSound::None) {
        world.PlaySound(StepSoundEvent(block.sound), breakX + 0.5, breakY + 0.5, breakZ + 0.5, (StepSoundVolume(block.sound) + 1.0f) / 8.0f,
                        StepSoundPitch(block.sound) * 0.5f);
    }
    breakSoundCounter++;

    if (breakProgress >= 1.0f) {
        BreakBlock(world, breakX, breakY, breakZ, drops);
        StopBreaking();
        blockHitDelay = BREAK_HIT_DELAY_TICKS;
    }
}

BreakProgress Player::GetBreakProgress() const {
    BreakProgress progress;
    if (!breaking) return progress;
    progress.active = true;
    progress.x = breakX;
    progress.y = breakY;
    progress.z = breakZ;
    progress.stage = std::min((int)(breakProgress * 10.0f) - 1, 9);
    return progress;
}
