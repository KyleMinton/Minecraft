#include "BlockParticles.h"
#include "Collision.h"
#include "Gui.h"
#include "Sky.h"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double TICK_SECONDS = 1.0 / 20.0;
constexpr int MAX_TICKS_PER_FRAME = 10;
constexpr int MAX_PARTICLES = 4000;
constexpr double HALF_WIDTH = 0.1;       // a 0.2 x 0.2 collision box
constexpr double HEIGHT = 0.2;
constexpr float DIGGING_COLOR = 0.6f;    // digging particles are drawn at 60% brightness
constexpr int GRID = 4;
constexpr float HIT_INSET = 0.1f;
constexpr float PIECE = 0.25f;           // a quarter of the tile each way

float RandomFloat() {
    return GetRandomValue(0, 0xFFFFFF) / (float)0x1000000;
}

Color Rgb(float r, float g, float b) {
    return Color{ (unsigned char)(std::clamp(r, 0.0f, 1.0f) * 255), (unsigned char)(std::clamp(g, 0.0f, 1.0f) * 255),
                  (unsigned char)(std::clamp(b, 0.0f, 1.0f) * 255), 255 };
}

} // namespace

BlockParticles::BlockParticles(Texture2D atlasTexture, bool atlasLoaded) : atlas(atlasTexture), hasAtlas(atlasLoaded) {
    shader = LoadShader("resources/shaders/particle.vs", "resources/shaders/particle.fs");
    fogColorLoc = GetShaderLocation(shader, "fogColor");
    fogStartLoc = GetShaderLocation(shader, "fogStart");
    fogEndLoc = GetShaderLocation(shader, "fogEnd");
    fogDensityLoc = GetShaderLocation(shader, "fogDensity");
    cameraPosLoc = GetShaderLocation(shader, "cameraPos");
    particleTexture = LoadPixelTexture("resources/textures/particles.png");
}

BlockParticles::~BlockParticles() {
    UnloadTexture(particleTexture);
    UnloadShader(shader);
}

// EntityFX's constructor then EntityDiggingFX's: the given push plus a random
// one, at a random speed with an upward kick; a random piece of the texture.
BlockParticles::Particle BlockParticles::MakeDigging(const World& world, double x, double y, double z, double mx, double my, double mz,
                                                      int bx, int by, int bz, BlockId block, int meta) const {
    Particle p;
    p.kind = Kind::Digging;
    p.x = p.prevX = x;
    p.y = p.prevY = y;
    p.z = p.prevZ = z;
    mx += (RandomFloat() * 2.0 - 1.0) * 0.4;
    my += (RandomFloat() * 2.0 - 1.0) * 0.4;
    mz += (RandomFloat() * 2.0 - 1.0) * 0.4;
    double speed = (RandomFloat() + RandomFloat() + 1.0) * 0.15;
    double length = sqrt(mx * mx + my * my + mz * mz);
    if (length < 1.0e-6) length = 1.0;
    p.motionX = mx / length * speed * 0.4;
    p.motionY = my / length * speed * 0.4 + 0.1;
    p.motionZ = mz / length * speed * 0.4;
    p.scale = p.baseScale = (RandomFloat() * 0.5f + 0.5f) * 2.0f / 2.0f;
    p.maxAge = (int)(4.0f / (RandomFloat() * 0.9f + 0.1f));
    p.gravity = 1.0f;

    const Block& def = GetBlockDef(block);
    int tex = block == BLOCK_GRASS ? 2 : def.Texture(SIDE_NORTH, meta);
    Vector2 t = { (tex & 15) / 16.0f, (tex >> 4) / 16.0f };
    p.u = t.x + RandomFloat() * 3.0f / GRID / 16.0f;
    p.v = t.y + RandomFloat() * 3.0f / GRID / 16.0f;
    p.tint = block == BLOCK_GRASS ? WHITE : def.WorldTint(world, bx, by, bz, SIDE_NORTH);
    return p;
}

void BlockParticles::Add(const Particle& particle) {
    if (particles.size() >= MAX_PARTICLES) particles.pop_front();
    particles.push_back(particle);
}

void BlockParticles::AddDestroyEffects(const World& world, int x, int y, int z, BlockId block, int meta) {
    if (block == BLOCK_AIR) return;
    for (int i = 0; i < GRID; i++)
        for (int j = 0; j < GRID; j++)
            for (int k = 0; k < GRID; k++) {
                double dx = (i + 0.5) / GRID, dy = (j + 0.5) / GRID, dz = (k + 0.5) / GRID;
                Add(MakeDigging(world, x + dx, y + dy, z + dz, dx - 0.5, dy - 0.5, dz - 0.5, x, y, z, block, meta));
            }
}

void BlockParticles::AddHitEffect(const World& world, int x, int y, int z, int side, BlockId block, int meta) {
    if (block == BLOCK_AIR) return;
    BlockBox b = GetBlockDef(block).Bounds(world, x, y, z);
    double px = x + RandomFloat() * (b.maxX - b.minX - HIT_INSET * 2.0) + HIT_INSET + b.minX;
    double py = y + RandomFloat() * (b.maxY - b.minY - HIT_INSET * 2.0) + HIT_INSET + b.minY;
    double pz = z + RandomFloat() * (b.maxZ - b.minZ - HIT_INSET * 2.0) + HIT_INSET + b.minZ;
    if (side == SIDE_DOWN) py = y + b.minY - HIT_INSET;
    if (side == SIDE_UP) py = y + b.maxY + HIT_INSET;
    if (side == SIDE_NORTH) pz = z + b.minZ - HIT_INSET;
    if (side == SIDE_SOUTH) pz = z + b.maxZ + HIT_INSET;
    if (side == SIDE_WEST) px = x + b.minX - HIT_INSET;
    if (side == SIDE_EAST) px = x + b.maxX + HIT_INSET;

    Particle p = MakeDigging(world, px, py, pz, 0.0, 0.0, 0.0, x, y, z, block, meta);
    p.motionX *= 0.2;
    p.motionY = (p.motionY - 0.1) * 0.2 + 0.1;
    p.motionZ *= 0.2;
    p.scale *= 0.6f;
    p.baseScale = p.scale;
    Add(p);
}

// The EntityFX subclasses' constructors.
void BlockParticles::Spawn(ParticleType type, double x, double y, double z, double mx, double my, double mz) {
    Particle p;
    p.x = p.prevX = p.startX = x;
    p.y = p.prevY = p.startY = y;
    p.z = p.prevZ = p.startZ = z;
    // EntityFX: a little random motion of its own, rescaled.
    double rx = (RandomFloat() * 2.0 - 1.0) * 0.4, ry = (RandomFloat() * 2.0 - 1.0) * 0.4, rz = (RandomFloat() * 2.0 - 1.0) * 0.4;
    double speed = (RandomFloat() + RandomFloat() + 1.0) * 0.15;
    double len = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (len < 1e-6) len = 1.0;
    p.motionX = rx / len * speed * 0.4;
    p.motionY = ry / len * speed * 0.4 + 0.1;
    p.motionZ = rz / len * speed * 0.4;
    p.scale = (RandomFloat() * 0.5f + 0.5f) * 2.0f;
    p.maxAge = (int)(4.0f / (RandomFloat() * 0.9f + 0.1f));

    switch (type) {
        case ParticleType::Smoke:
        case ParticleType::LargeSmoke: {
            p.kind = Kind::Smoke;
            p.motionX = p.motionX * 0.1 + mx;
            p.motionY = p.motionY * 0.1 + my;
            p.motionZ = p.motionZ * 0.1 + mz;
            float grey = RandomFloat() * 0.3f;
            p.tint = Rgb(grey, grey, grey);
            float size = type == ParticleType::LargeSmoke ? 2.5f : 1.0f;
            p.scale *= 0.75f * size;
            p.maxAge = (int)((int)(8.0f / (RandomFloat() * 0.8f + 0.2f)) * size);
            break;
        }
        case ParticleType::Flame:
            p.kind = Kind::Flame;
            p.motionX = p.motionX * 0.01 + mx;
            p.motionY = p.motionY * 0.01 + my;
            p.motionZ = p.motionZ * 0.01 + mz;
            p.x += (RandomFloat() - RandomFloat()) * 0.05f;
            p.y += (RandomFloat() - RandomFloat()) * 0.05f;
            p.z += (RandomFloat() - RandomFloat()) * 0.05f;
            p.prevX = p.x; p.prevY = p.y; p.prevZ = p.z;
            p.maxAge = (int)(8.0f / (RandomFloat() * 0.8f + 0.2f)) + 4;
            p.texture = 48;
            p.noClip = true;
            break;
        case ParticleType::Lava:
            p.kind = Kind::Lava;
            p.motionX *= 0.8;
            p.motionZ *= 0.8;
            p.motionY = RandomFloat() * 0.4f + 0.05f;
            p.scale *= RandomFloat() * 2.0f + 0.2f;
            p.maxAge = (int)(16.0f / (RandomFloat() * 0.8f + 0.2f));
            p.texture = 49;
            break;
        case ParticleType::Reddust: {
            p.kind = Kind::Reddust;
            p.motionX *= 0.1;
            p.motionY *= 0.1;
            p.motionZ *= 0.1;
            float r = (float)mx, g = (float)my, b = (float)mz;
            if (r == 0.0f) r = 1.0f;
            float f = RandomFloat() * 0.4f + 0.6f;
            p.tint = Rgb((RandomFloat() * 0.2f + 0.8f) * r * f, (RandomFloat() * 0.2f + 0.8f) * g * f, (RandomFloat() * 0.2f + 0.8f) * b * f);
            p.scale *= 0.75f;
            p.maxAge = (int)(8.0f / (RandomFloat() * 0.8f + 0.2f));
            break;
        }
        case ParticleType::Portal: {
            p.kind = Kind::Portal;
            p.motionX = mx;
            p.motionY = my;
            p.motionZ = mz;
            float f = RandomFloat() * 0.6f + 0.4f;
            p.tint = Rgb(f * 0.9f, f * 0.3f, f);
            p.scale = RandomFloat() * 0.2f + 0.5f;
            p.maxAge = (int)(RandomFloat() * 10.0f) + 40;
            p.texture = (int)(RandomFloat() * 8.0f);
            p.noClip = true;
            break;
        }
        case ParticleType::Splash:
            p.kind = Kind::Splash;
            p.motionX *= 0.3;
            p.motionY = RandomFloat() * 0.2f + 0.1f;
            p.motionZ *= 0.3;
            if (my == 0.0 && (mx != 0.0 || mz != 0.0)) {
                p.motionX = mx;
                p.motionY = my + 0.1;
                p.motionZ = mz;
            }
            p.texture = 19 + GetRandomValue(0, 3);
            p.gravity = 0.04f / 0.04f;
            p.maxAge = (int)(8.0f / (RandomFloat() * 0.8f + 0.2f));
            break;
        case ParticleType::Bubble:
            p.kind = Kind::Bubble;
            p.scale *= RandomFloat() * 0.6f + 0.2f;
            p.motionX = mx * 0.2 + (RandomFloat() * 2.0f - 1.0f) * 0.02f;
            p.motionY = my * 0.2 + (RandomFloat() * 2.0f - 1.0f) * 0.02f;
            p.motionZ = mz * 0.2 + (RandomFloat() * 2.0f - 1.0f) * 0.02f;
            p.maxAge = (int)(8.0f / (RandomFloat() * 0.8f + 0.2f));
            p.texture = 32;
            break;
        case ParticleType::Explode: {
            p.kind = Kind::Explode;
            p.motionX = mx + (RandomFloat() * 2.0f - 1.0f) * 0.05f;
            p.motionY = my + (RandomFloat() * 2.0f - 1.0f) * 0.05f;
            p.motionZ = mz + (RandomFloat() * 2.0f - 1.0f) * 0.05f;
            float grey = RandomFloat() * 0.3f + 0.7f;
            p.tint = Rgb(grey, grey, grey);
            p.scale = RandomFloat() * RandomFloat() * 6.0f + 1.0f;
            p.maxAge = (int)(16.0f / (RandomFloat() * 0.8f + 0.2f)) + 2;
            break;
        }
        case ParticleType::Note: {
            p.kind = Kind::Note;
            p.motionX *= 0.01;
            p.motionY = p.motionY * 0.01 + 0.2;
            p.motionZ *= 0.01;
            float f = (float)mx; // note / 24
            p.tint = Rgb(std::sin((f + 0.0f) * PI * 2.0f) * 0.65f + 0.35f, std::sin((f + 1.0f / 3.0f) * PI * 2.0f) * 0.65f + 0.35f,
                         std::sin((f + 2.0f / 3.0f) * PI * 2.0f) * 0.65f + 0.35f);
            p.scale *= 0.75f * 2.0f;
            p.maxAge = 6;
            p.texture = 64;
            p.noClip = false;
            break;
        }
    }
    p.baseScale = p.scale;
    Add(p);
}

void BlockParticles::Update(float dt, const World& world) {
    tickAccumulator += dt;
    int ticks = 0;
    while (tickAccumulator >= TICK_SECONDS && ticks < MAX_TICKS_PER_FRAME) {
        Tick(world);
        tickAccumulator -= TICK_SECONDS;
        ticks++;
    }
    if (tickAccumulator >= TICK_SECONDS) tickAccumulator = 0.0;
    alpha = (float)(tickAccumulator / TICK_SECONDS);
}

// Each kind's onUpdate.
void BlockParticles::Tick(const World& world) {
    std::vector<Particle> spawned;
    auto move = [&](Particle& p) {
        if (p.noClip) {
            p.x += p.motionX; p.y += p.motionY; p.z += p.motionZ;
            return;
        }
        Box box = { { p.x - HALF_WIDTH, p.y, p.z - HALF_WIDTH }, { p.x + HALF_WIDTH, p.y + HEIGHT, p.z + HALF_WIDTH } };
        MoveResult r = MoveBox(world, box, p.motionX, p.motionY, p.motionZ, 0.0, false);
        p.x += r.dx; p.y += r.dy; p.z += r.dz;
        p.onGround = r.onGround;
        if (r.collidedX) p.motionX = 0.0;
        if (r.collidedY) p.motionY = 0.0;
        if (r.collidedZ) p.motionZ = 0.0;
    };

    for (Particle& p : particles) {
        p.prevX = p.x; p.prevY = p.y; p.prevZ = p.z;
        p.age++;
        switch (p.kind) {
            case Kind::Digging:
            case Kind::Splash:
                p.motionY -= 0.04 * p.gravity;
                move(p);
                p.motionX *= 0.98; p.motionY *= 0.98; p.motionZ *= 0.98;
                if (p.onGround) {
                    p.motionX *= 0.7; p.motionZ *= 0.7;
                    if (p.kind == Kind::Splash && RandomFloat() < 0.5f) p.age = p.maxAge;
                }
                break;
            case Kind::Smoke:
            case Kind::Reddust:
            case Kind::Explode: {
                p.texture = 7 - p.age * 8 / std::max(1, p.maxAge);
                p.motionY += 0.004;
                double y0 = p.y;
                move(p);
                if (p.y == y0) { p.motionX *= 1.1; p.motionZ *= 1.1; }
                double drag = p.kind == Kind::Explode ? 0.9 : 0.96;
                p.motionX *= drag; p.motionY *= drag; p.motionZ *= drag;
                if (p.onGround) { p.motionX *= 0.7; p.motionZ *= 0.7; }
                break;
            }
            case Kind::Flame:
                move(p);
                p.motionX *= 0.96; p.motionY *= 0.96; p.motionZ *= 0.96;
                break;
            case Kind::Lava: {
                float f = (float)p.age / p.maxAge;
                if (RandomFloat() > f) {
                    Particle smoke = p;
                    smoke.kind = Kind::Smoke;
                    float grey = RandomFloat() * 0.3f;
                    smoke.tint = Rgb(grey, grey, grey);
                    smoke.age = 0;
                    smoke.maxAge = (int)(8.0f / (RandomFloat() * 0.8f + 0.2f));
                    smoke.scale = smoke.baseScale = (RandomFloat() * 0.5f + 0.5f) * 2.0f * 0.75f;
                    spawned.push_back(smoke);
                }
                p.motionY -= 0.03;
                move(p);
                p.motionX *= 0.999; p.motionY *= 0.999; p.motionZ *= 0.999;
                if (p.onGround) { p.motionX *= 0.7; p.motionZ *= 0.7; }
                break;
            }
            case Kind::Portal: {
                float f = (float)p.age / p.maxAge, f1 = f;
                f = -f + f * f * 2.0f;
                f = 1.0f - f;
                p.x = p.startX + p.motionX * f;
                p.y = p.startY + p.motionY * f + (1.0f - f1);
                p.z = p.startZ + p.motionZ * f;
                break;
            }
            case Kind::Bubble: {
                p.motionY += 0.002;
                move(p);
                p.motionX *= 0.85; p.motionY *= 0.85; p.motionZ *= 0.85;
                if (world.GetMaterial((int)floor(p.x), (int)floor(p.y), (int)floor(p.z)) != BlockMaterial::Water) p.age = p.maxAge;
                break;
            }
            case Kind::Note:
                move(p);
                if (p.y == p.prevY) { p.motionX *= 1.1; p.motionZ *= 1.1; }
                p.motionX *= 0.66; p.motionY *= 0.66; p.motionZ *= 0.66;
                if (p.onGround) { p.motionX *= 0.7; p.motionZ *= 0.7; }
                break;
        }
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.age >= p.maxAge; }),
                    particles.end());
    for (const Particle& p : spawned) Add(p);
}

void BlockParticles::SetFog(Vector3 color, float start, float end, float density, float darken, Vector3 cameraPos) {
    skyDarken = darken;
    SetShaderValue(shader, fogColorLoc, &color, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, fogStartLoc, &start, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, fogEndLoc, &end, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, fogDensityLoc, &density, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, cameraPosLoc, &cameraPos, SHADER_UNIFORM_VEC3);
}

void BlockParticles::DrawBatch(const Camera3D& camera, const World& world, bool digging) const {
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = Vector3CrossProduct(right, forward);

    rlSetTexture(digging ? (hasAtlas ? atlas.id : rlGetTextureIdDefault()) : particleTexture.id);
    rlBegin(RL_QUADS);
    for (const Particle& p : particles) {
        if ((p.kind == Kind::Digging) != digging) continue;
        Vector3 pos = { (float)(p.prevX + (p.x - p.prevX) * alpha), (float)(p.prevY + (p.y - p.prevY) * alpha),
                        (float)(p.prevZ + (p.z - p.prevZ) * alpha) };
        float age = (p.age + alpha) / std::max(1, p.maxAge);
        float scale = p.scale;
        float brightness;
        if (p.kind == Kind::Flame || p.kind == Kind::Lava) {
            brightness = 1.0f;
            if (p.kind == Kind::Flame) scale = p.baseScale * (1.0f - age * age * 0.5f);
            else scale = p.baseScale * (1.0f - age * age);
        } else {
            int bx = (int)floorf(pos.x), by = (int)floorf(pos.y), bz = (int)floorf(pos.z);
            int level = std::max(world.GetSkyLight(bx, by, bz) - (int)skyDarken, world.GetBlockLight(bx, by, bz));
            brightness = LightLevelBrightness(level);
            if (p.kind == Kind::Portal) brightness = std::min(1.0f, brightness + (1.0f - brightness) * age * age * age * age);
            if (p.kind == Kind::Smoke || p.kind == Kind::Reddust) scale = p.baseScale * std::clamp(age * 32.0f, 0.0f, 1.0f);
            if (p.kind == Kind::Note) scale = p.baseScale * std::clamp(age * 32.0f, 0.0f, 1.0f);
            if (p.kind == Kind::Portal) scale = p.baseScale * (1.0f - (1.0f - age) * (1.0f - age));
        }
        if (p.kind == Kind::Digging) brightness *= DIGGING_COLOR;
        rlColor4ub((unsigned char)(p.tint.r * brightness), (unsigned char)(p.tint.g * brightness), (unsigned char)(p.tint.b * brightness), 255);

        float half = 0.1f * scale;
        Vector3 r = Vector3Scale(right, half), u = Vector3Scale(up, half);
        Vector3 corners[4] = {
            Vector3Subtract(Vector3Subtract(pos, r), u), Vector3Subtract(Vector3Add(pos, r), u),
            Vector3Add(Vector3Add(pos, r), u), Vector3Add(Vector3Subtract(pos, r), u),
        };
        float u0, v0, u1, v1;
        if (digging) {
            u0 = p.u; v0 = p.v; u1 = p.u + PIECE / 16.0f; v1 = p.v + PIECE / 16.0f;
        } else {
            u0 = (p.texture % 16) / 16.0f; v0 = (p.texture / 16) / 16.0f;
            u1 = u0 + 1.0f / 16.0f; v1 = v0 + 1.0f / 16.0f;
        }
        float uvs[4][2] = { { u0, v1 }, { u1, v1 }, { u1, v0 }, { u0, v0 } };
        for (int i = 0; i < 4; i++) {
            rlTexCoord2f(uvs[i][0], uvs[i][1]);
            rlVertex3f(corners[i].x, corners[i].y, corners[i].z);
        }
    }
    rlEnd();
    rlDrawRenderBatchActive();
}

void BlockParticles::Draw(const Camera3D& camera, const World& world) const {
    if (particles.empty()) return;
    rlDrawRenderBatchActive();
    BeginShaderMode(shader);
    DrawBatch(camera, world, true);
    DrawBatch(camera, world, false);
    rlSetTexture(rlGetTextureIdDefault());
    EndShaderMode();
}
