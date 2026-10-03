#version 330

// One direction of a Gaussian blur, for the modern menu background.
in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec2 direction; // (1, 0) or (0, 1)
uniform vec2 texel;     // 1 / texture size
uniform float radius;   // pixels

out vec4 finalColor;

void main()
{
    float sigma = max(radius / 2.0, 0.5);
    vec3 sum = vec3(0.0);
    float weights = 0.0;
    // Sample every other pixel (linear filtering blends the pairs) out to the radius.
    for (int i = -24; i <= 24; i++) {
        float x = float(i) * 2.0;
        if (abs(x) > radius * 1.5) continue;
        float w = exp(-(x * x) / (2.0 * sigma * sigma));
        sum += texture(texture0, fragTexCoord + direction * texel * x).rgb * w;
        weights += w;
    }
    finalColor = vec4(sum / weights, 1.0);
}
