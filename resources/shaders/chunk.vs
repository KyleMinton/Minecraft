#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
in vec3 vertexNormal;   // x: sky light, y: block light (levels 0-15, smoothed per corner)
in vec4 vertexColor;    // tint x ambient occlusion x face shade

uniform mat4 mvp;
uniform mat4 matModel;

out vec2 fragTexCoord;
out vec2 fragTileOrigin;
out vec4 fragColor;
out vec3 fragWorldPos;
out vec2 fragLight;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragTileOrigin = vertexTexCoord2;
    fragColor = vertexColor;
    fragLight = vertexNormal.xy;
    fragWorldPos = (matModel * vec4(vertexPosition, 1.0)).xyz;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
