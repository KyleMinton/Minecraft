#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matNormal;

// Minecraft's item lighting: two directional lights (view space) at 60% plus 40% ambient.
uniform vec3 light0;
uniform vec3 light1;

out vec2 fragTexCoord;
out vec4 fragColor;
out float fragLight;

void main()
{
    vec3 n = normalize((matNormal * vec4(vertexNormal, 0.0)).xyz);
    fragLight = min(0.4 + 0.6 * (max(dot(n, light0), 0.0) + max(dot(n, light1), 0.0)), 1.0);
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
