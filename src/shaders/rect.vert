#version 450

layout(set = 0, binding = 0) readonly buffer RectBuffer {
    /* Each instance is 12 floats = 48 bytes:
     * float2 position, float2 size, float4 color,
     * float border_radius, float stroke_width, float2 _pad */
    float data[];
} rects;

layout(push_constant) uniform PushConstants {
    vec2 viewport;
} pc;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_local_pos;
layout(location = 2) out vec2 v_rect_size;
layout(location = 3) out float v_border_radius;
layout(location = 4) out float v_stroke_width;

const vec2 quad_verts[6] = vec2[](
    vec2(0, 0), vec2(1, 0), vec2(0, 1),
    vec2(1, 0), vec2(1, 1), vec2(0, 1)
);

void main() {
    int iid = gl_InstanceIndex;
    int vid = gl_VertexIndex;

    /* Read instance data (12 floats per instance) */
    int base = iid * 12;
    vec2 position     = vec2(rects.data[base + 0], rects.data[base + 1]);
    vec2 size         = vec2(rects.data[base + 2], rects.data[base + 3]);
    vec4 color        = vec4(rects.data[base + 4], rects.data[base + 5],
                             rects.data[base + 6], rects.data[base + 7]);
    float border_radius = rects.data[base + 8];
    float stroke_width  = rects.data[base + 9];

    vec2 uv = quad_verts[vid];
    vec2 px = position + uv * size;

    gl_Position = vec4(
        px.x / pc.viewport.x * 2.0 - 1.0,
        px.y / pc.viewport.y * 2.0 - 1.0,
        0.0, 1.0
    );

    v_color = color;
    v_local_pos = uv * size;
    v_rect_size = size;
    v_border_radius = border_radius;
    v_stroke_width = stroke_width;
}
