#version 330

// For immediate-mode sky geometry, whose vertices are already in world space.
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;

out vec3 fragWorldPos;
out vec2 fragTexCoord;
out vec4 fragColor;

void main()
{
    fragWorldPos = vertexPosition;
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
