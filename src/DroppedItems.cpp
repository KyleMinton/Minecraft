#include "DroppedItems.h"
#include "Collision.h"
#include "JavaRandom.h"
#include "Player.h"
#include "Sky.h"

#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>

namespace {

// EntityItem, in blocks and ticks.
constexpr double TICK_SECONDS = 1.0 / 20.0;
constexpr int MAX_TICKS_PER_FRAME = 10;
constexpr double HALF_SIZE = 0.125;       // a 0.25 x 0.25 x 0.25 box
constexpr double SIZE = 0.25;
constexpr double GRAVITY = 0.04;
constexpr double DRAG = 0.98;
constexpr double BOUNCE = -0.5;           // vertical motion flips and halves on the ground
constexpr int BLOCK_DROP_PICKUP_DELAY = 10;
constexpr int DESPAWN_TICKS = 6000;       // 5 minutes
constexpr int MERGE_CHECK_INTERVAL = 25;
constexpr double MERGE_REACH = 0.5;

// Picking up (EntityPlayer.onLivingUpdate): the player's box grown by 1 sideways and 0.5 up and down.
constexpr double PLAYER_HALF_WIDTH = 0.3;
constexpr double PLAYER_HEIGHT = 1.8;
constexpr double PICKUP_REACH_XZ = 1.0;
constexpr double PICKUP_REACH_Y = 0.5;
constexpr int PICKUP_ANIMATION_TICKS = 3;
constexpr float PICKUP_TARGET_HEIGHT = 0.5f;

// Rendering (RenderItem).
constexpr float CUBE_SCALE = 0.25f;
constexpr float MODEL_SCALE = 0.5f;       // blocks that aren't plain cubes (stairs, fences...)
constexpr float SPRITE_SCALE = 0.5f;
constexpr float COPY_SPREAD = 0.2f;

float RandomFloat() {
    return GetRandomValue(0, 0xFFFFFF) / (float)0x1000000;
}

Box ItemBox(double x, double y, double z) {
    return { { x - HALF_SIZE, y, z - HALF_SIZE }, { x + HALF_SIZE, y + SIZE, z + HALF_SIZE } };
}

// Bigger stacks show more copies, like Beta.
int ModelCount(int count) {
    if (count > 20) return 4;
    if (count > 5) return 3;
    if (count > 1) return 2;
    return 1;
}

int LegacyItem(int old) {
    static const int map[18] = {
        BLOCK_AIR, BLOCK_GRASS, BLOCK_DIRT, BLOCK_STONE, BLOCK_SAND, BLOCK_WATER, BLOCK_LOG, BLOCK_LEAVES, BLOCK_COBBLESTONE,
        BLOCK_GRAVEL, BLOCK_BEDROCK, BLOCK_COAL_ORE, BLOCK_IRON_ORE, BLOCK_GOLD_ORE, BLOCK_DIAMOND_ORE, BLOCK_SANDSTONE,
        BLOCK_PLANKS, BLOCK_BRICKS,
    };
    return old >= 0 && old < 18 ? map[old] : 0;
}

} // namespace

DroppedItems::DroppedItems(const ItemModels& itemModels) : models(itemModels) {
    shader = LoadShader("resources/shaders/entity.vs", "resources/shaders/entity.fs");
    light0Loc = GetShaderLocation(shader, "light0");
    light1Loc = GetShaderLocation(shader, "light1");
    brightnessLoc = GetShaderLocation(shader, "brightness");
    fogColorLoc = GetShaderLocation(shader, "fogColor");
    fogStartLoc = GetShaderLocation(shader, "fogStart");
    fogEndLoc = GetShaderLocation(shader, "fogEnd");
    fogDensityLoc = GetShaderLocation(shader, "fogDensity");
    cameraPosLoc = GetShaderLocation(shader, "cameraPos");

    // In the world, item lighting is fixed in world space.
    Vector3 light0 = Vector3Normalize(ITEM_LIGHT0), light1 = Vector3Normalize(ITEM_LIGHT1);
    SetShaderValue(shader, light0Loc, &light0, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, light1Loc, &light1, SHADER_UNIFORM_VEC3);

    blockMaterial = MakeMaterial(shader, models.Atlas());
    itemMaterial = MakeMaterial(shader, models.ItemSheet());
}

DroppedItems::~DroppedItems() {
    for (auto& [key, mesh] : sprites) UnloadMesh(mesh);
    MemFree(blockMaterial.maps); // the textures and shader aren't the materials' to free
    MemFree(itemMaterial.maps);
    UnloadShader(shader);
}

void DroppedItems::Spawn(double x, double y, double z, double motionX, double motionY, double motionZ,
                         const ItemStack& stack, int pickupDelay) {
    if (stack.IsEmpty()) return;
    Item item;
    item.x = item.prevX = x;
    item.y = item.prevY = y;
    item.z = item.prevZ = z;
    item.motionX = motionX;
    item.motionY = motionY;
    item.motionZ = motionZ;
    item.stack = ItemStack(stack.id, stack.count, stack.damage);
    item.pickupDelay = pickupDelay;
    item.hoverStart = RandomFloat() * PI * 2.0f;
    items.push_back(item);
}

void DroppedItems::Save(std::ostream& out) const {
    out.precision(17);
    for (const Item& item : items) {
        out << "item2 " << item.stack.id << " " << item.stack.count << " " << item.stack.damage << " " << item.x << " " << item.y
            << " " << item.z << " " << item.motionX << " " << item.motionY << " " << item.motionZ << " " << item.age << " "
            << item.pickupDelay << "\n";
    }
}

void DroppedItems::Load(std::istream& in) {
    Clear();
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string key;
        fields >> key;
        int id = 0, count = 0, damage = 0, age = 0, delay = 0;
        double x, y, z, mx, my, mz;
        if (key == "item2") {
            if (!(fields >> id >> count >> damage >> x >> y >> z >> mx >> my >> mz >> age >> delay)) continue;
        } else if (key == "item") {
            if (!(fields >> id >> count >> x >> y >> z >> mx >> my >> mz >> age >> delay)) continue;
            id = LegacyItem(id);
        } else {
            continue;
        }
        if (GetItemDef(id) == nullptr || count <= 0) continue;
        ItemStack stack(id, count, damage);
        stack.count = std::min(count, MaxStackSize(stack));
        Spawn(x, y, z, mx, my, mz, stack, delay);
        items.back().age = age;
    }
}

void DroppedItems::Clear() {
    items.clear();
    pickups.clear();
}

void DroppedItems::SpawnFromBlock(int x, int y, int z, const ItemStack& stack) {
    // Block.dropBlockAsItem_do: somewhere in the middle of the block, then
    // EntityItem's constructor gives it a little random pop sideways and 0.2 up.
    Spawn(x + RandomFloat() * 0.7 + 0.15, y + RandomFloat() * 0.7 + 0.15, z + RandomFloat() * 0.7 + 0.15,
          RandomFloat() * 0.2 - 0.1, 0.2, RandomFloat() * 0.2 - 0.1, stack, BLOCK_DROP_PICKUP_DELAY);
}

bool DroppedItems::AnyInBox(const double min[3], const double max[3]) const {
    Box area = { { min[0], min[1], min[2] }, { max[0], max[1], max[2] } };
    for (const Item& item : items) {
        if (!item.dead && Overlaps(area, ItemBox(item.x, item.y, item.z))) return true;
    }
    return false;
}

void DroppedItems::Update(float dt, const World& world, Player& player, SoundSystem& sounds) {
    tickAccumulator += dt;
    int ticks = 0;
    while (tickAccumulator >= TICK_SECONDS && ticks < MAX_TICKS_PER_FRAME) {
        Tick(world, player, sounds);
        tickAccumulator -= TICK_SECONDS;
        ticks++;
    }
    if (tickAccumulator >= TICK_SECONDS) tickAccumulator = 0.0;
    alpha = (float)(tickAccumulator / TICK_SECONDS);
}

void DroppedItems::Tick(const World& world, Player& player, SoundSystem& sounds) {
    for (Pickup& pickup : pickups) pickup.age++;
    pickups.erase(std::remove_if(pickups.begin(), pickups.end(), [](const Pickup& p) { return p.age >= PICKUP_ANIMATION_TICKS; }),
                  pickups.end());

    // EntityItem.onUpdate.
    for (Item& item : items) {
        if (item.dead) continue;
        item.prevX = item.x; item.prevY = item.y; item.prevZ = item.z;
        if (item.pickupDelay > 0) item.pickupDelay--;

        item.motionY -= GRAVITY;
        // Lava burns items up with a fizz.
        if (world.GetMaterial((int)floor(item.x), (int)floor(item.y), (int)floor(item.z)) == BlockMaterial::Lava) {
            sounds.Play("random.fizz", (float)item.x, (float)item.y, (float)item.z, 0.4f, 2.0f + RandomFloat() * 0.4f);
            item.dead = true;
            continue;
        }
        PushOutOfBlocks(item, world);
        Move(item, world);

        double friction = DRAG;
        if (item.onGround) {
            BlockId below = world.GetBlock((int)floor(item.x), (int)floor(item.y) - 1, (int)floor(item.z));
            friction = (below == BLOCK_AIR ? 0.6f : GetBlockDef(below).slipperiness) * DRAG;
        }
        item.motionX *= friction;
        item.motionY *= DRAG;
        item.motionZ *= friction;
        if (item.onGround) item.motionY *= BOUNCE;

        item.age++;
        bool changedBlock = (int)floor(item.prevX) != (int)floor(item.x) || (int)floor(item.prevY) != (int)floor(item.y) ||
                            (int)floor(item.prevZ) != (int)floor(item.z);
        if (changedBlock || item.age % MERGE_CHECK_INTERVAL == 0) MergeNearby(item);
        if (item.age >= DESPAWN_TICKS) item.dead = true;
    }

    // Walking into items picks them up, as much as fits.
    Vector3 feet = player.GetPosition();
    Box reach = { { feet.x - PLAYER_HALF_WIDTH - PICKUP_REACH_XZ, feet.y - PICKUP_REACH_Y, feet.z - PLAYER_HALF_WIDTH - PICKUP_REACH_XZ },
                  { feet.x + PLAYER_HALF_WIDTH + PICKUP_REACH_XZ, feet.y + PLAYER_HEIGHT + PICKUP_REACH_Y, feet.z + PLAYER_HALF_WIDTH + PICKUP_REACH_XZ } };
    for (Item& item : items) {
        if (item.dead || item.pickupDelay > 0 || !Overlaps(reach, ItemBox(item.x, item.y, item.z))) continue;
        int before = item.stack.count;
        int left = player.GetInventory().Add(item.stack, player.GetSelectedSlot());
        if (left == before) continue; // inventory full

        Pickup pickup;
        pickup.item = item;
        pickup.item.stack.count = before - left;
        pickups.push_back(pickup);
        sounds.PlayPickup();

        item.stack.count = left;
        if (left == 0) item.dead = true;
    }

    items.erase(std::remove_if(items.begin(), items.end(), [](const Item& i) { return i.dead; }), items.end());
}

void DroppedItems::Move(Item& item, const World& world) {
    Box box = ItemBox(item.x, item.y, item.z);
    MoveResult r = MoveBox(world, box, item.motionX, item.motionY, item.motionZ, 0.0, item.onGround);
    item.x += r.dx;
    item.y += r.dy;
    item.z += r.dz;
    item.onGround = r.onGround;
    if (r.collidedX) item.motionX = 0.0;
    if (r.collidedY) item.motionY = 0.0;
    if (r.collidedZ) item.motionZ = 0.0;
}

// Entity.pushOutOfBlocks: an item stuck inside a block is nudged out through
// the nearest open side, preferring up.
void DroppedItems::PushOutOfBlocks(Item& item, const World& world) {
    Box box = ItemBox(item.x, item.y, item.z);
    if (!AnyCollision(world, box)) return;

    double centerY = item.y + SIZE / 2.0;
    int bx = (int)floor(item.x), by = (int)floor(centerY), bz = (int)floor(item.z);
    if (!world.IsNormalCube(bx, by, bz)) return;
    double fx = item.x - bx, fy = centerY - by, fz = item.z - bz;

    int axis = 1, direction = 1; // up
    double nearest = 1.0e9;
    if (!world.IsNormalCube(bx - 1, by, bz) && fx < nearest) { nearest = fx; axis = 0; direction = -1; }
    if (!world.IsNormalCube(bx + 1, by, bz) && 1.0 - fx < nearest) { nearest = 1.0 - fx; axis = 0; direction = 1; }
    if (!world.IsNormalCube(bx, by, bz - 1) && fz < nearest) { nearest = fz; axis = 2; direction = -1; }
    if (!world.IsNormalCube(bx, by, bz + 1) && 1.0 - fz < nearest) { nearest = 1.0 - fz; axis = 2; direction = 1; }
    if (!world.IsNormalCube(bx, by + 1, bz) && 1.0 - fy < nearest) { nearest = 1.0 - fy; axis = 1; direction = 1; }

    double push = (RandomFloat() * 0.2 + 0.1) * direction;
    double* motion[3] = { &item.motionX, &item.motionY, &item.motionZ };
    for (int a = 0; a < 3; a++) {
        if (a == axis) *motion[a] = push;
        else *motion[a] *= 0.75;
    }
}

// The smaller stack joins the bigger one, if they're the same item and fit in one stack.
void DroppedItems::MergeNearby(Item& item) {
    Box area = ItemBox(item.x, item.y, item.z);
    area.min[0] -= MERGE_REACH; area.max[0] += MERGE_REACH;
    area.min[2] -= MERGE_REACH; area.max[2] += MERGE_REACH;

    for (Item& other : items) {
        if (item.dead) return;
        if (&other == &item || other.dead || !Overlaps(area, ItemBox(other.x, other.y, other.z))) continue;
        if (!SameItem(other.stack, item.stack) || other.stack.count + item.stack.count > MaxStackSize(item.stack)) continue;

        Item& into = (other.stack.count < item.stack.count) ? item : other;
        Item& from = (&into == &item) ? other : item;
        into.stack.count += from.stack.count;
        into.pickupDelay = std::max(into.pickupDelay, from.pickupDelay);
        into.age = std::min(into.age, from.age);
        from.dead = true;
    }
}

void DroppedItems::SetFog(Vector3 color, float start, float end, float density, float darken, Vector3 cameraPos) {
    skyDarken = darken;
    SetShaderValue(shader, fogColorLoc, &color, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, fogStartLoc, &start, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, fogEndLoc, &end, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, fogDensityLoc, &density, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, cameraPosLoc, &cameraPos, SHADER_UNIFORM_VEC3);
}

// A flat item's camera-facing quad: 1 wide, from 0.25 below its middle to 0.75 above.
const Mesh& DroppedItems::Sprite(const ItemStack& stack) const {
    int key = stack.id | (stack.damage & 0x7FFF) << 16;
    auto it = sprites.find(key);
    if (it != sprites.end()) return it->second;
    Rectangle uv = models.IconUV(stack);
    MeshData mesh;
    Vector3 corners[4] = { { -0.5f, -0.25f, 0 }, { 0.5f, -0.25f, 0 }, { 0.5f, 0.75f, 0 }, { -0.5f, 0.75f, 0 } };
    Vector2 uvs[4] = { { uv.x, uv.y + uv.height }, { uv.x + uv.width, uv.y + uv.height }, { uv.x + uv.width, uv.y }, { uv.x, uv.y } };
    mesh.AddQuad(corners, uvs, Vector3{ 0, 1, 0 }, models.Tint(stack)); // normal up: evenly lit, as Beta draws them
    return sprites.emplace(key, mesh.Upload()).first->second;
}

void DroppedItems::Draw(const Player& player, const World& world, const Camera3D& camera) const {
    for (const Item& item : items) {
        double a = alpha;
        DrawItem(item, item.prevX + (item.x - item.prevX) * a, item.prevY + (item.y - item.prevY) * a,
                 item.prevZ + (item.z - item.prevZ) * a, alpha, world, camera);
    }

    // Picked-up items fly into the player, speeding up.
    Vector3 target = player.GetRenderPosition();
    target.y += PICKUP_TARGET_HEIGHT;
    for (const Pickup& pickup : pickups) {
        float f = (pickup.age + alpha) / PICKUP_ANIMATION_TICKS;
        f *= f;
        const Item& item = pickup.item;
        DrawItem(item, item.x + (target.x - item.x) * f, item.y + (target.y - item.y) * f, item.z + (target.z - item.z) * f, 0.0f,
                 world, camera);
    }
}

void DroppedItems::DrawItem(const Item& item, double x, double y, double z, float partial, const World& world,
                            const Camera3D& camera) const {
    const ItemStack& stack = item.stack;
    if (GetItemDef(stack.id) == nullptr) return;

    int bx = (int)floor(x), by = (int)floor(y + SIZE / 2.0), bz = (int)floor(z);
    int level = std::max(world.GetSkyLight(bx, by, bz) - (int)skyDarken, world.GetBlockLight(bx, by, bz));
    float brightness = LightLevelBrightness(level);
    SetShaderValue(shader, brightnessLoc, &brightness, SHADER_UNIFORM_FLOAT);

    float time = item.age + partial;
    float bob = sinf(time / 10.0f + item.hoverStart) * 0.1f + 0.1f;
    float spin = (time / 20.0f + item.hoverStart) * RAD2DEG;
    JavaRandom random(187);
    int copies = ModelCount(stack.count);

    if (ItemModels::Is3D(stack)) {
        const Block& def = GetBlockDef((BlockId)stack.id);
        bool cube = def.shape == Shape::Cube || def.shape == Shape::PistonBase;
        float scale = cube ? CUBE_SCALE : MODEL_SCALE;
        const Mesh& mesh = models.BlockModel(stack.id, ItemRenderMeta(stack));
        for (int i = 0; i < copies; i++) {
            GlMatrixStack t;
            t.Translate((float)x, (float)y + bob + 0.1f, (float)z);
            t.Rotate(spin, 0, 1, 0);
            if (i > 0) {
                float ox = (random.NextFloat() * 2.0f - 1.0f) * COPY_SPREAD / scale * CUBE_SCALE;
                float oy = (random.NextFloat() * 2.0f - 1.0f) * COPY_SPREAD / scale * CUBE_SCALE;
                float oz = (random.NextFloat() * 2.0f - 1.0f) * COPY_SPREAD / scale * CUBE_SCALE;
                t.Translate(ox, oy, oz);
            }
            t.Scale(scale);
            DrawMesh(mesh, blockMaterial, t.m);
        }
        return;
    }

    // Flat items face the camera (RenderItem's billboards).
    const Mesh& mesh = Sprite(stack);
    Vector3 toCamera = { camera.position.x - (float)x, 0.0f, camera.position.z - (float)z };
    float facing = atan2f(toCamera.x, toCamera.z) * RAD2DEG;
    Material& material = const_cast<Material&>(models.Sheet(stack).id == models.ItemSheet().id ? itemMaterial : blockMaterial);
    for (int i = 0; i < copies; i++) {
        GlMatrixStack t;
        t.Translate((float)x, (float)y + bob + 0.1f, (float)z);
        if (i > 0) {
            float ox = (random.NextFloat() * 2.0f - 1.0f) * 0.3f * SPRITE_SCALE;
            float oy = (random.NextFloat() * 2.0f - 1.0f) * 0.3f * SPRITE_SCALE;
            float oz = (random.NextFloat() * 2.0f - 1.0f) * 0.3f * SPRITE_SCALE;
            t.Translate(ox, oy, oz);
        }
        t.Rotate(facing, 0, 1, 0);
        t.Scale(SPRITE_SCALE);
        DrawMesh(mesh, material, t.m);
    }
}
