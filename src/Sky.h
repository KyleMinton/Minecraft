#pragma once

#include "raylib.h"
#include <vector>

// Fog applied to terrain for the current frame.
struct TerrainFog {
    Vector3 color;
    float start;
    float end;
    float density;    // > 0 means exponential fog (underwater) instead of start/end
    float brightness; // brightness of full sky light (15) right now
    float skyDarken;  // light levels night takes off sky light (Minecraft's skylightSubtracted, 0-11)
};

// Minecraft's light level (0-15) to brightness curve, with its 5% floor.
float LightLevelBrightness(int level);

// Minecraft Alpha's sky: day/night cycle, sky and fog colors, sunrise/sunset
// glow, sun, phased moon, stars, and flat ("fast graphics") clouds. The math
// follows Minecraft's own (World.getSkyColor/getFogColor/getCelestialAngle,
// RenderGlobal.renderSky/renderClouds, EntityRenderer.updateFogColor/setupFog).
// Construct after InitWindow() and destroy before CloseWindow().
class Sky {
public:
    static constexpr int TICKS_PER_DAY = 24000; // 20 minutes
    static constexpr long long START_TIME = 0;  // a new Minecraft world starts at sunrise

    // farPlane is the view distance in blocks (Minecraft's 256 >> renderDistance).
    explicit Sky(float farPlane);
    void SetFarPlane(float distance) { farPlane = distance; }
    ~Sky();

    Sky(const Sky&) = delete;
    Sky& operator=(const Sky&) = delete;

    // Advances time (in 20 Hz ticks) and recomputes this frame's colors.
    // `eyeInWater` switches to Minecraft's dark-blue underwater fog.
    // `cameraSkyLight` is the sky light level (0-15) where the camera is; the fog
    // darkens toward it, so it goes dark in caves.
    void Update(float dt, bool eyeInWater, int cameraSkyLight);

    // The world's time in ticks since it was created (saved with the world).
    long long GetTime() const { return worldTime; }
    void SetTime(long long ticks);

    Color ClearColor() const;
    TerrainFog GetTerrainFog() const;

    // Inside BeginMode3D: DrawSky before the terrain, DrawClouds after it.
    void DrawSky(const Camera3D& camera);
    void DrawClouds(const Camera3D& camera);

private:
    float farPlane;
    long long worldTime = START_TIME;
    double tickAccumulator = 0.0;
    float partialTick = 0.0f;
    long long cloudTicks = 0;
    bool underwater = false;
    int skylightSubtracted = 0;
    int cameraLight = 15;

    // Minecraft smooths fog brightness toward the light level at the camera.
    float fogBrightness = 1.0f, prevFogBrightness = 1.0f;

    Vector3 skyColor = { 0, 0, 0 };
    Vector3 fogColor = { 0, 0, 0 };

    Texture2D sunTexture;
    Texture2D moonTexture;
    Texture2D cloudTexture;
    Shader fogShader;
    int cameraPosLoc, fogColorLoc, fogStartLoc, fogEndLoc, fogDensityLoc;

    std::vector<Vector3> starVertices; // 4 per star, on a sphere of radius 100

    void Tick();
    float CelestialAngle() const;
    float DaylightFactor() const;
    int SkylightSubtracted(float celestialAngle) const;
    void SetFogUniforms(const Camera3D& camera, float start, float end);
};
