#version 330

const vec2[4] corners = vec2[4](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(1, 1));

out vec2 v_texCoord;
flat out int instanceID;

layout(std140) uniform TransformData
{
    mat4 transforms[256];
};

void main()
{
    gl_Position = transforms[gl_InstanceID] * vec4(corners[gl_VertexID] - 0.5, 0, 1);
    v_texCoord = vec2(corners[gl_VertexID].x, 1.0 - corners[gl_VertexID].y);
    instanceID = gl_InstanceID;
}
