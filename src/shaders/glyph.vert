#version 450

layout(set = 0, binding = 0) readonly buffer GlyphBuffer {
    /* Each instance is 12 floats = 48 bytes:
     * float2 position, float2 size, float2 uv_min, float2 uv_max, float4 color */
    float data[];
} glyphs;

layout(push_constant) uniform PushConstants {
    vec2 viewport;
} pc;

layout(location = 0) out vec2 v_texcoord;
layout(location = 1) out vec4 v_color;

const vec2 quad_verts[6] = vec2[](
    vec2(0, 0), vec2(1, 0), vec2(0, 1),
    vec2(1, 0), vec2(1, 1), vec2(0, 1)
);

void main() {
    int iid = gl_InstanceIndex;
    int vid = gl_VertexIndex;

    int base = iid * 12;
    vec2 position = vec2(glyphs.data[base + 0], glyphs.data[base + 1]);
    vec2 size     = vec2(glyphs.data[base + 2], glyphs.data[base + 3]);
    vec2 uv_min   = vec2(glyphs.data[base + 4], glyphs.data[base + 5]);
    vec2 uv_max   = vec2(glyphs.data[base + 6], glyphs.data[base + 7]);
    vec4 color    = vec4(glyphs.data[base + 8], glyphs.data[base + 9],
                         glyphs.data[base + 10], glyphs.data[base + 11]);

    vec2 uv = quad_verts[vid];
    vec2 px = position + uv * size;

    gl_Position = vec4(
        px.x / pc.viewport.x * 2.0 - 1.0,
        px.y / pc.viewport.y * 2.0 - 1.0,
        0.0, 1.0
    );

    v_texcoord = mix(uv_min, uv_max, uv);
    v_color = color;
}
