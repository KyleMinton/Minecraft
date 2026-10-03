#include "Sky.h"
#include "Chunk.h"
#include "JavaRandom.h"

#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

constexpr double TICK_SECONDS = 1.0 / 20.0;
constexpr int MAX_TICKS_PER_FRAME = 10;

// Minecraft Alpha's colors (before Alpha 1.2 made the sky depend on the biome).
constexpr Vector3 ALPHA_SKY = { 0x88 / 255.0f, 0xBB / 255.0f, 0xFF / 255.0f };
constexpr Vector3 ALPHA_FOG = { 0xC0 / 255.0f, 0xD8 / 255.0f, 0xFF / 255.0f };
constexpr Vector3 UNDERWATER_FOG = { 0.02f, 0.02f, 0.2f };
constexpr float UNDERWATER_FOG_DENSITY = 0.1f;

// Sky geometry, relative to the camera, in blocks.
constexpr float SKY_PLANE_HEIGHT = 16.0f;
constexpr float SKY_PLANE_EXTENT = 384.0f;
constexpr float CELESTIAL_DISTANCE = 100.0f;
constexpr float SUN_HALF_SIZE = 30.0f;
constexpr float MOON_HALF_SIZE = 20.0f;
constexpr int MOON_PHASES = 8;

// Clouds: Alpha drew them at y=120, 56 above its sea level of 64; keep that gap.
constexpr float CLOUD_HEIGHT = Chunk::SEA_LEVEL + 56.0f;
constexpr float CLOUD_EXTENT = 256.0f;
constexpr double CLOUD_TEXTURE_BLOCKS = 2048.0; // one 256px texture spans 2048 blocks (8 per pixel)
constexpr double CLOUD_SPEED = 0.03;            // blocks per tick
constexpr float CLOUD_ALPHA = 0.8f;

constexpr int STAR_SEED = 10842;
constexpr int STAR_ATTEMPTS = 1500;

// Minecraft's generateStars(): random points on a sphere of radius 100, each a
// small quad facing the center, sized and rotated randomly.
std::vector<Vector3> GenerateStars() {
    std::vector<Vector3> vertices;
    JavaRandom random(STAR_SEED);
    for (int i = 0; i < STAR_ATTEMPTS; i++) {
        double x = random.NextFloat() * 2.0f - 1.0f;
        double y = random.NextFloat() * 2.0f - 1.0f;
        double z = random.NextFloat() * 2.0f - 1.0f;
        double size = 0.25f + random.NextFloat() * 0.25f;
        double lengthSq = x * x + y * y + z * z;
        if (lengthSq >= 1.0 || lengthSq <= 0.01) continue;

        double inv = 1.0 / sqrt(lengthSq);
        x *= inv; y *= inv; z *= inv;
        double px = x * 100.0, py = y * 100.0, pz = z * 100.0;
        double yawAngle = atan2(x, z);
        double sinYaw = sin(yawAngle), cosYaw = cos(yawAngle);
        double pitchAngle = atan2(sqrt(x * x + z * z), y);
        double sinPitch = sin(pitchAngle), cosPitch = cos(pitchAngle);
        double spin = random.NextDouble() * PI * 2.0;
        double sinSpin = sin(spin), cosSpin = cos(spin);

        for (int j = 0; j < 4; j++) {
            double a = (double)((j & 2) - 1) * size;
            double b = (double)(((j + 1) & 2) - 1) * size;
            double c = a * cosSpin - b * sinSpin;
            double d = b * cosSpin + a * sinSpin;
            double up = c * sinPitch;
            double e = -c * cosPitch;
            double side = e * sinYaw - d * cosYaw;
            double depth = d * sinYaw + e * cosYaw;
            vertices.push_back(Vector3{ (float)(px + side), (float)(py + up), (float)(pz + depth) });
        }
    }
    return vertices;
}

// 0.0 = noon, 0.25 = sunset, 0.5 = midnight, 0.75 = sunrise; eased so days
// spend a little longer near noon and midnight.
float CelestialAngleAt(long long worldTime, float partialTick) {
    float t = ((float)(worldTime % Sky::TICKS_PER_DAY) + partialTick) / Sky::TICKS_PER_DAY - 0.25f;
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    float linear = t;
    t = 1.0f - (cosf(t * PI) + 1.0f) / 2.0f;
    return linear + (t - linear) / 3.0f;
}

float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// Minecraft's light level to brightness curve (0.05 floor).
float LightBrightness(int level) {
    float f = 1.0f - level / 15.0f;
    return (1.0f - f) / (f * 3.0f + 1.0f) * (1.0f - 0.05f) + 0.05f;
}

// Minecraft's render distance setting index (0 = Far 256, 1 = Normal 128, 2 = Short 64, 3 = Tiny 32).
int RenderDistanceSetting(float farPlane) {
    int index = (int)lroundf(log2f(256.0f / farPlane));
    return std::clamp(index, 0, 3);
}

Vector3 Scale(Vector3 v, float s) { return Vector3{ v.x * s, v.y * s, v.z * s }; }

} // namespace

Sky::Sky(float farPlaneDistance) : farPlane(farPlaneDistance) {
    sunTexture = LoadTexture("resources/textures/environment/sun.png");
    moonTexture = LoadTexture("resources/textures/environment/moon_phases.png");
    cloudTexture = LoadTexture("resources/textures/environment/clouds.png");
    SetTextureFilter(sunTexture, TEXTURE_FILTER_POINT);
    SetTextureFilter(moonTexture, TEXTURE_FILTER_POINT);
    SetTextureFilter(cloudTexture, TEXTURE_FILTER_POINT);
    SetTextureWrap(cloudTexture, TEXTURE_WRAP_REPEAT);

    fogShader = LoadShader("resources/shaders/fog.vs", "resources/shaders/fog.fs");
    cameraPosLoc = GetShaderLocation(fogShader, "cameraPos");
    fogColorLoc = GetShaderLocation(fogShader, "fogColor");
    fogStartLoc = GetShaderLocation(fogShader, "fogStart");
    fogEndLoc = GetShaderLocation(fogShader, "fogEnd");
    fogDensityLoc = GetShaderLocation(fogShader, "fogDensity");

    starVertices = GenerateStars();

    // Start with fog brightness already settled instead of fading in from black.
    skylightSubtracted = SkylightSubtracted(CelestialAngleAt(worldTime, 1.0f));
    float keep = (3 - RenderDistanceSetting(farPlane)) / 3.0f;
    fogBrightness = prevFogBrightness = LightBrightness(15 - skylightSubtracted) * (1.0f - keep) + keep;

    Update(0.0f, false, 15);
}

Sky::~Sky() {
    UnloadShader(fogShader);
    UnloadTexture(sunTexture);
    UnloadTexture(moonTexture);
    UnloadTexture(cloudTexture);
}

float Sky::CelestialAngle() const {
    return CelestialAngleAt(worldTime, partialTick);
}

// 1 in full daylight, 0 at night, ramping around sunrise and sunset.
float Sky::DaylightFactor() const {
    return Clamp01(cosf(CelestialAngle() * PI * 2.0f) * 2.0f + 0.5f);
}

// How far below full sky light (15) it is right now, in whole light levels (0-11).
int Sky::SkylightSubtracted(float celestialAngle) const {
    float dark = Clamp01(1.0f - (cosf(celestialAngle * PI * 2.0f) * 2.0f + 0.5f));
    return (int)(dark * 11.0f);
}

void Sky::Tick() {
    worldTime++;
    cloudTicks++;
    skylightSubtracted = SkylightSubtracted(CelestialAngleAt(worldTime, 1.0f));

    // Fog dims toward the light level at the camera; the farther the view distance,
    // the less it dims (EntityRenderer.updateRenderer).
    prevFogBrightness = fogBrightness;
    float keep = (3 - RenderDistanceSetting(farPlane)) / 3.0f;
    float target = LightBrightness(std::max(cameraLight - skylightSubtracted, 0)) * (1.0f - keep) + keep;
    fogBrightness += (target - fogBrightness) * 0.1f;
}

void Sky::Update(float dt, bool eyeInWater, int cameraSkyLight) {
    underwater = eyeInWater;
    cameraLight = cameraSkyLight;

    tickAccumulator += dt;
    int ticks = 0;
    while (tickAccumulator >= TICK_SECONDS && ticks < MAX_TICKS_PER_FRAME) {
        Tick();
        tickAccumulator -= TICK_SECONDS;
        ticks++;
    }
    if (tickAccumulator >= TICK_SECONDS) tickAccumulator = 0.0;
    partialTick = (float)(tickAccumulator / TICK_SECONDS);

    float day = DaylightFactor();
    skyColor = Scale(ALPHA_SKY, day);

    Vector3 fog = {
        ALPHA_FOG.x * (day * 0.94f + 0.06f),
        ALPHA_FOG.y * (day * 0.94f + 0.06f),
        ALPHA_FOG.z * (day * 0.91f + 0.09f)
    };
    // Fog leans toward the sky color, more so at shorter view distances.
    float toSky = 1.0f - powf(1.0f / (4 - RenderDistanceSetting(farPlane)), 0.25f);
    fog.x += (skyColor.x - fog.x) * toSky;
    fog.y += (skyColor.y - fog.y) * toSky;
    fog.z += (skyColor.z - fog.z) * toSky;
    if (underwater) fog = UNDERWATER_FOG;

    fogColor = Scale(fog, prevFogBrightness + (fogBrightness - prevFogBrightness) * partialTick);
}

void Sky::SetTime(long long ticks) {
    worldTime = ticks;
    skylightSubtracted = SkylightSubtracted(CelestialAngleAt(worldTime, 1.0f));
    float keep = (3 - RenderDistanceSetting(farPlane)) / 3.0f;
    fogBrightness = prevFogBrightness = LightBrightness(15 - skylightSubtracted) * (1.0f - keep) + keep;
}

float LightLevelBrightness(int level) {
    return LightBrightness(std::clamp(level, 0, 15));
}

Color Sky::ClearColor() const {
    return Color{ (unsigned char)(fogColor.x * 255.0f), (unsigned char)(fogColor.y * 255.0f),
                  (unsigned char)(fogColor.z * 255.0f), 255 };
}

TerrainFog Sky::GetTerrainFog() const {
    TerrainFog fog;
    fog.color = fogColor;
    fog.start = farPlane * 0.25f;
    fog.end = farPlane;
    fog.density = underwater ? UNDERWATER_FOG_DENSITY : 0.0f;
    fog.brightness = LightBrightness(15 - skylightSubtracted);
    fog.skyDarken = (float)skylightSubtracted;
    return fog;
}

void Sky::SetFogUniforms(const Camera3D& camera, float start, float end) {
    float density = underwater ? UNDERWATER_FOG_DENSITY : 0.0f;
    SetShaderValue(fogShader, cameraPosLoc, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(fogShader, fogColorLoc, &fogColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(fogShader, fogStartLoc, &start, SHADER_UNIFORM_FLOAT);
    SetShaderValue(fogShader, fogEndLoc, &end, SHADER_UNIFORM_FLOAT);
    SetShaderValue(fogShader, fogDensityLoc, &density, SHADER_UNIFORM_FLOAT);
}

// RenderGlobal.renderSky: the sky plane above (fading into fog toward the horizon),
// the sunrise/sunset glow, then the sun, moon and stars added on top, and a dark
// plane below the horizon. Nothing here writes depth, so terrain draws over it.
void Sky::DrawSky(const Camera3D& camera) {
    Vector3 cam = camera.position;
    float angle = CelestialAngle();

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    // Sky plane: fog starts at the camera and ends at 80% of the view distance.
    BeginShaderMode(fogShader);
        SetFogUniforms(camera, 0.0f, farPlane * 0.8f);
        rlSetTexture(rlGetTextureIdDefault());
        rlBegin(RL_QUADS);
            rlColor4f(skyColor.x, skyColor.y, skyColor.z, 1.0f);
            rlVertex3f(cam.x - SKY_PLANE_EXTENT, cam.y + SKY_PLANE_HEIGHT, cam.z - SKY_PLANE_EXTENT);
            rlVertex3f(cam.x - SKY_PLANE_EXTENT, cam.y + SKY_PLANE_HEIGHT, cam.z + SKY_PLANE_EXTENT);
            rlVertex3f(cam.x + SKY_PLANE_EXTENT, cam.y + SKY_PLANE_HEIGHT, cam.z + SKY_PLANE_EXTENT);
            rlVertex3f(cam.x + SKY_PLANE_EXTENT, cam.y + SKY_PLANE_HEIGHT, cam.z - SKY_PLANE_EXTENT);
        rlEnd();
        rlDrawRenderBatchActive();
    EndShaderMode();

    // Sunrise/sunset: a fan of warm color on the horizon the sun is near, fading out upward.
    float nearHorizon = cosf(angle * PI * 2.0f);
    const float glowWidth = 0.4f;
    if (nearHorizon >= -glowWidth && nearHorizon <= glowWidth) {
        float f = nearHorizon / glowWidth * 0.5f + 0.5f;
        float strength = 1.0f - (1.0f - sinf(f * PI)) * 0.99f;
        strength *= strength;
        float r = f * 0.3f + 0.7f;
        float g = f * f * 0.7f + 0.2f;
        float b = 0.2f;

        rlPushMatrix();
            rlTranslatef(cam.x, cam.y, cam.z);
            rlRotatef(90.0f, 1.0f, 0.0f, 0.0f);
            rlRotatef(angle <= 0.5f ? 0.0f : 180.0f, 0.0f, 0.0f, 1.0f);
            rlSetTexture(rlGetTextureIdDefault());
            rlBegin(RL_TRIANGLES);
                const int segments = 16;
                for (int j = 0; j < segments; j++) {
                    float a0 = j * PI * 2.0f / segments, a1 = (j + 1) * PI * 2.0f / segments;
                    rlColor4f(r, g, b, strength);
                    rlVertex3f(0.0f, 100.0f, 0.0f);
                    rlColor4f(r, g, b, 0.0f);
                    rlVertex3f(sinf(a0) * 120.0f, cosf(a0) * 120.0f, -cosf(a0) * 40.0f * strength);
                    rlVertex3f(sinf(a1) * 120.0f, cosf(a1) * 120.0f, -cosf(a1) * 40.0f * strength);
                }
            rlEnd();
        rlPopMatrix();
        rlDrawRenderBatchActive();
    }

    // Sun, moon and stars are added on top (black texels add nothing) and turn with the time of day.
    BeginBlendMode(BLEND_ADDITIVE);
        rlPushMatrix();
            rlTranslatef(cam.x, cam.y, cam.z);
            rlRotatef(angle * 360.0f, 1.0f, 0.0f, 0.0f);

            rlSetTexture(sunTexture.id);
            rlBegin(RL_QUADS);
                rlColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                rlTexCoord2f(0.0f, 0.0f); rlVertex3f(-SUN_HALF_SIZE, CELESTIAL_DISTANCE, -SUN_HALF_SIZE);
                rlTexCoord2f(1.0f, 0.0f); rlVertex3f(SUN_HALF_SIZE, CELESTIAL_DISTANCE, -SUN_HALF_SIZE);
                rlTexCoord2f(1.0f, 1.0f); rlVertex3f(SUN_HALF_SIZE, CELESTIAL_DISTANCE, SUN_HALF_SIZE);
                rlTexCoord2f(0.0f, 1.0f); rlVertex3f(-SUN_HALF_SIZE, CELESTIAL_DISTANCE, SUN_HALF_SIZE);
            rlEnd();

            // The moon sheet is 4x2 phases; one phase per day.
            int phase = (int)((worldTime / TICKS_PER_DAY) % MOON_PHASES);
            float u0 = (phase % 4) / 4.0f, v0 = (phase / 4 % 2) / 2.0f;
            float u1 = u0 + 0.25f, v1 = v0 + 0.5f;
            rlSetTexture(moonTexture.id);
            rlBegin(RL_QUADS);
                rlColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                rlTexCoord2f(u1, v1); rlVertex3f(-MOON_HALF_SIZE, -CELESTIAL_DISTANCE, MOON_HALF_SIZE);
                rlTexCoord2f(u0, v1); rlVertex3f(MOON_HALF_SIZE, -CELESTIAL_DISTANCE, MOON_HALF_SIZE);
                rlTexCoord2f(u0, v0); rlVertex3f(MOON_HALF_SIZE, -CELESTIAL_DISTANCE, -MOON_HALF_SIZE);
                rlTexCoord2f(u1, v0); rlVertex3f(-MOON_HALF_SIZE, -CELESTIAL_DISTANCE, -MOON_HALF_SIZE);
            rlEnd();
            rlSetTexture(rlGetTextureIdDefault());

            float starBrightness = Clamp01(1.0f - (cosf(angle * PI * 2.0f) * 2.0f + 0.75f));
            starBrightness = starBrightness * starBrightness * 0.5f;
            if (starBrightness > 0.0f) {
                rlBegin(RL_QUADS);
                    rlColor4f(starBrightness, starBrightness, starBrightness, starBrightness);
                    for (const Vector3& v : starVertices) rlVertex3f(v.x, v.y, v.z);
                rlEnd();
            }
        rlPopMatrix();
        rlDrawRenderBatchActive();
    EndBlendMode();

    // Dark plane below the horizon, also fading into fog.
    BeginShaderMode(fogShader);
        SetFogUniforms(camera, 0.0f, farPlane * 0.8f);
        rlSetTexture(rlGetTextureIdDefault());
        rlBegin(RL_QUADS);
            rlColor4f(skyColor.x * 0.2f + 0.04f, skyColor.y * 0.2f + 0.04f, skyColor.z * 0.6f + 0.1f, 1.0f);
            rlVertex3f(cam.x - SKY_PLANE_EXTENT, cam.y - SKY_PLANE_HEIGHT, cam.z - SKY_PLANE_EXTENT);
            rlVertex3f(cam.x + SKY_PLANE_EXTENT, cam.y - SKY_PLANE_HEIGHT, cam.z - SKY_PLANE_EXTENT);
            rlVertex3f(cam.x + SKY_PLANE_EXTENT, cam.y - SKY_PLANE_HEIGHT, cam.z + SKY_PLANE_EXTENT);
            rlVertex3f(cam.x - SKY_PLANE_EXTENT, cam.y - SKY_PLANE_HEIGHT, cam.z + SKY_PLANE_EXTENT);
        rlEnd();
        rlDrawRenderBatchActive();
    EndShaderMode();
    rlSetTexture(0);

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

// RenderGlobal.renderClouds with fast graphics: one flat, drifting layer, fogged
// like terrain, 80% opaque. Empty texels are discarded by the shader's alpha test.
void Sky::DrawClouds(const Camera3D& camera) {
    Vector3 cam = camera.position;
    float day = DaylightFactor();
    float r = day * 0.9f + 0.1f;
    float g = day * 0.9f + 0.1f;
    float b = day * 0.85f + 0.15f;

    // Texture offset: the camera position plus the drift, wrapped to one texture's span.
    double offsetX = cam.x + (cloudTicks + partialTick) * CLOUD_SPEED;
    double offsetZ = cam.z;
    offsetX -= floor(offsetX / CLOUD_TEXTURE_BLOCKS) * CLOUD_TEXTURE_BLOCKS;
    offsetZ -= floor(offsetZ / CLOUD_TEXTURE_BLOCKS) * CLOUD_TEXTURE_BLOCKS;
    float u = (float)(offsetX / CLOUD_TEXTURE_BLOCKS);
    float v = (float)(offsetZ / CLOUD_TEXTURE_BLOCKS);
    float span = (float)(CLOUD_EXTENT / CLOUD_TEXTURE_BLOCKS);
    float y = CLOUD_HEIGHT + 0.33f;

    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    BeginShaderMode(fogShader);
        SetFogUniforms(camera, farPlane * 0.25f, farPlane);
        rlSetTexture(cloudTexture.id);
        rlBegin(RL_QUADS);
            rlColor4f(r, g, b, CLOUD_ALPHA);
            rlTexCoord2f(u - span, v + span); rlVertex3f(cam.x - CLOUD_EXTENT, y, cam.z + CLOUD_EXTENT);
            rlTexCoord2f(u + span, v + span); rlVertex3f(cam.x + CLOUD_EXTENT, y, cam.z + CLOUD_EXTENT);
            rlTexCoord2f(u + span, v - span); rlVertex3f(cam.x + CLOUD_EXTENT, y, cam.z - CLOUD_EXTENT);
            rlTexCoord2f(u - span, v - span); rlVertex3f(cam.x - CLOUD_EXTENT, y, cam.z - CLOUD_EXTENT);
        rlEnd();
        rlSetTexture(0);
        rlDrawRenderBatchActive();
    EndShaderMode();
    rlEnableBackfaceCulling();
}
