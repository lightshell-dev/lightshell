#version 450

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_local_pos;
layout(location = 2) in vec2 v_rect_size;
layout(location = 3) in float v_border_radius;
layout(location = 4) in float v_stroke_width;

layout(location = 0) out vec4 out_color;

float roundedBoxSDF(vec2 p, vec2 half_size, float r) {
    vec2 q = abs(p) - (half_size - vec2(r));
    return length(max(q, 0.0)) - r;
}

void main() {
    vec2 half_size = v_rect_size * 0.5;
    vec2 p = v_local_pos - half_size;
    float r = min(v_border_radius, min(half_size.x, half_size.y));

    if (v_stroke_width > 0.0) {
        /* Stroke: outer SDF - inner SDF */
        float d_outer = roundedBoxSDF(p, half_size, r);
        float inner_r = max(r - v_stroke_width, 0.0);
        vec2 inner_half = half_size - vec2(v_stroke_width);
        float d_inner = roundedBoxSDF(p, inner_half, inner_r);
        if (d_outer > 0.5) discard;
        if (d_inner < -0.5) discard;
        float a_outer = 1.0 - smoothstep(-0.5, 0.5, d_outer);
        float a_inner = smoothstep(-0.5, 0.5, d_inner);
        float a = a_outer * a_inner;
        out_color = vec4(v_color.rgb * a, v_color.a * a);
        return;
    }

    if (r > 0.0) {
        float d = roundedBoxSDF(p, half_size, r);
        if (d > 0.5) discard;
        float a = 1.0 - smoothstep(-0.5, 0.5, d);
        out_color = vec4(v_color.rgb * a, v_color.a * a);
        return;
    }

    out_color = v_color;
}
