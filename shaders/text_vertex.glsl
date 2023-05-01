#version 330

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

out vec2 v_texCoord;
flat out int instanceID;

layout(std140) uniform TransformData
{
    mat4 transforms[256];
};

void main()
{
    gl_Position = transforms[gl_InstanceID] * vec4(position, 0, 1);
    v_texCoord = texCoord;
    instanceID = gl_InstanceID;
}
