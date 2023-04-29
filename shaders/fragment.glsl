#version 330

in vec2 v_texCoord;
flat in int instanceID;

layout(location = 0) out vec4 fragmentColor;

uniform sampler2D textureSampler;

struct Material
{
    vec4 color;
    int useTexture;
};

layout(std140) uniform MaterialData
{
    Material materials[256];
};

void main()
{
    fragmentColor = materials[instanceID].color;
    if (materials[instanceID].useTexture != 0)
    {
        fragmentColor *= texture(textureSampler, v_texCoord);
    }
}
