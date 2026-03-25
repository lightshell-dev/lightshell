#version 450

layout(set = 0, binding = 0) readonly buffer ImageBuffer {
    /* Each instance is 4 floats = 16 bytes:
     * float2 position, float2 size */
    float data[];
} images;

layout(push_constant) uniform PushConstants {
    vec2 viewport;
} pc;

layout(location = 0) out vec2 v_texcoord;

const vec2 quad_verts[6] = vec2[](
    vec2(0, 0), vec2(1, 0), vec2(0, 1),
    vec2(1, 0), vec2(1, 1), vec2(0, 1)
);

void main() {
    int iid = gl_InstanceIndex;
    int vid = gl_VertexIndex;

    int base = iid * 4;
    vec2 position = vec2(images.data[base + 0], images.data[base + 1]);
    vec2 size     = vec2(images.data[base + 2], images.data[base + 3]);

    vec2 uv = quad_verts[vid];
    vec2 px = position + uv * size;

    gl_Position = vec4(
        px.x / pc.viewport.x * 2.0 - 1.0,
        px.y / pc.viewport.y * 2.0 - 1.0,
        0.0, 1.0
    );

    v_texcoord = uv;
}
