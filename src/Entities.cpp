#include "Entities.h"
#include "Collision.h"
#include "Sky.h"
#include "World.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double HALF_SIZE = 0.49; // falling blocks and TNT are 0.98 cubes
constexpr double SIZE = 0.98;

float RandomFloat() {
    return GetRandomValue(0, 0xFFFFFF) / (float)0x1000000;
}

Box EntityBox(double x, double y, double z) {
    return { { x - HALF_SIZE, y, z - HALF_SIZE }, { x + HALF_SIZE, y + SIZE, z + HALF_SIZE } };
}

} // namespace

Entities::Entities(const ItemModels& itemModels) : models(itemModels) {
    shader = LoadShader("resources/shaders/entity.vs", "resources/shaders/entity.fs");
    light0Loc = GetShaderLocation(shader, "light0");
    light1Loc = GetShaderLocation(shader, "light1");
    brightnessLoc = GetShaderLocation(shader, "brightness");
    fogColorLoc = GetShaderLocation(shader, "fogColor");
    fogStartLoc = GetShaderLocation(shader, "fogStart");
    fogEndLoc = GetShaderLocation(shader, "fogEnd");
    fogDensityLoc = GetShaderLocation(shader, "fogDensity");
    cameraPosLoc = GetShaderLocation(shader, "cameraPos");
    Vector3 light0 = Vector3Normalize(ITEM_LIGHT0), light1 = Vector3Normalize(ITEM_LIGHT1);
    SetShaderValue(shader, light0Loc, &light0, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, light1Loc, &light1, SHADER_UNIFORM_VEC3);
    material = MakeMaterial(shader, models.Atlas());
}

Entities::~Entities() {
    MemFree(material.maps);
    UnloadShader(shader);
}

void Entities::SpawnFallingBlock(double x, double y, double z, int id, int meta) {
    Entity e;
    e.id = id;
    e.meta = meta;
    e.x = e.prevX = x;
    e.y = e.prevY = y;
    e.z = e.prevZ = z;
    entities.push_back(e);
}

// EntityTNTPrimed: hops up and a little to one side.
void Entities::SpawnTnt(double x, double y, double z, int fuse) {
    Entity e;
    e.tnt = true;
    e.id = BLOCK_TNT;
    e.x = e.prevX = x;
    e.y = e.prevY = y - 0.49;
    e.z = e.prevZ = z;
    float angle = RandomFloat() * PI * 2.0f;
    e.motionX = -sinf(angle) * 0.02f;
    e.motionY = 0.2;
    e.motionZ = -cosf(angle) * 0.02f;
    e.fuse = fuse;
    entities.push_back(e);
}

void Entities::Tick(World& world) {
    std::vector<Entity> ticking;
    ticking.swap(entities); // blocks landing can spawn more (sand on sand)
    for (Entity& e : ticking) {
        if (e.dead) continue;
        e.prevX = e.x; e.prevY = e.y; e.prevZ = e.z;
        e.age++;
        e.motionY -= 0.04;
        Box box = EntityBox(e.x, e.y, e.z);
        MoveResult r = MoveBox(world, box, e.motionX, e.motionY, e.motionZ, 0.0, e.onGround);
        e.x += r.dx; e.y += r.dy; e.z += r.dz;
        e.onGround = r.onGround;
        if (r.collidedX) e.motionX = 0;
        if (r.collidedY) e.motionY = 0;
        if (r.collidedZ) e.motionZ = 0;
        e.motionX *= 0.98; e.motionY *= 0.98; e.motionZ *= 0.98;

        if (e.tnt) {
            if (e.onGround) {
                e.motionX *= 0.7;
                e.motionZ *= 0.7;
                e.motionY *= -0.5;
            }
            if (e.fuse-- <= 0) {
                e.dead = true;
                world.Explode(e.x, e.y + SIZE / 2.0, e.z, 4.0f, false);
            } else {
                world.SpawnParticle(ParticleType::Smoke, e.x, e.y + 0.5, e.z, 0, 0, 0);
            }
            continue;
        }

        // Falling sand: lands as a block where it stops, or breaks into an item.
        int bx = (int)floor(e.x), by = (int)floor(e.y), bz = (int)floor(e.z);
        if (e.onGround) {
            e.dead = true;
            const Block& def = GetBlockDef((uint8_t)e.id);
            uint8_t there = world.GetBlock(bx, by, bz);
            bool replaceable = there == BLOCK_AIR || GetMaterialInfo(GetBlockDef(there).material).replaceable;
            if (replaceable && def.CanPlaceAt(world, bx, by, bz) && world.SetBlock(bx, by, bz, e.id, e.meta)) continue;
            world.SpawnItem(bx, by, bz, ItemStack(e.id, 1, e.meta));
        } else if (e.age > 100 || by < -10) {
            e.dead = true;
            world.SpawnItem(bx, std::max(by, 0), bz, ItemStack(e.id, 1, e.meta));
        }
    }
    for (Entity& e : ticking) {
        if (!e.dead) entities.push_back(e);
    }
}

// Explosion.doExplosionA's push on entities nearby.
void Entities::ExplosionPush(World& world, double x, double y, double z, float power) {
    (void)world;
    double radius = power * 2.0;
    for (Entity& e : entities) {
        double dx = e.x - x, dy = e.y - y, dz = e.z - z;
        double len = std::sqrt(dx * dx + dy * dy + dz * dz);
        double dist = len / radius;
        if (dist > 1.0 || len < 1e-4) continue;
        double impact = 1.0 - dist;
        e.motionX += dx / len * impact;
        e.motionY += dy / len * impact;
        e.motionZ += dz / len * impact;
    }
}

bool Entities::AnyInBox(const double min[3], const double max[3]) const {
    Box area = { { min[0], min[1], min[2] }, { max[0], max[1], max[2] } };
    for (const Entity& e : entities) {
        if (!e.dead && Overlaps(area, EntityBox(e.x, e.y, e.z))) return true;
    }
    return false;
}

void Entities::SetFog(Vector3 color, float start, float end, float density, float darken, Vector3 cameraPos) {
    skyDarken = darken;
    SetShaderValue(shader, fogColorLoc, &color, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, fogStartLoc, &start, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, fogEndLoc, &end, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, fogDensityLoc, &density, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, cameraPosLoc, &cameraPos, SHADER_UNIFORM_VEC3);
}

void Entities::Draw(const World& world, float partial) const {
    for (const Entity& e : entities) {
        float x = (float)(e.prevX + (e.x - e.prevX) * partial);
        float y = (float)(e.prevY + (e.y - e.prevY) * partial);
        float z = (float)(e.prevZ + (e.z - e.prevZ) * partial);
        int bx = (int)floorf(x), by = (int)floorf(y + 0.5f), bz = (int)floorf(z);
        int level = std::max(world.GetSkyLight(bx, by, bz) - (int)skyDarken, world.GetBlockLight(bx, by, bz));
        float brightness = LightLevelBrightness(level);
        float scale = 1.0f;
        if (e.tnt) {
            // RenderTNTPrimed: swells in its last half second and flashes white.
            float left = e.fuse - partial + 1.0f;
            if (left < 10.0f) {
                float f = std::clamp(1.0f - left / 10.0f, 0.0f, 1.0f);
                f *= f;
                f *= f;
                scale = 1.0f + f * 0.3f;
            }
            if ((e.fuse / 5) % 2 == 0) brightness = brightness + (1.0f - left / 100.0f) * 0.8f * 3.0f;
        }
        SetShaderValue(shader, brightnessLoc, &brightness, SHADER_UNIFORM_FLOAT);
        GlMatrixStack t;
        t.Translate(x, y + 0.5f, z);
        t.Scale(scale);
        DrawMesh(models.BlockModel(e.id, e.meta), material, t.m);
    }
}
