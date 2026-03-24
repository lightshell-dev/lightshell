/* gpu_metal.m - Metal GPU backend for LightShell
 *
 * Renders filled and stroked rectangles with optional rounded corners using
 * instanced drawing. Supports scissor clipping and opacity groups.
 * Each rect is a unit quad (6 vertices) instanced with
 * position/size/color/border_radius/stroke_width. Rounded corners and
 * stroke outlines use SDF in the fragment shader.
 */

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include "gpu.h"
#include <stdio.h>
#include <string.h>

/* ---- Embedded Metal Shading Language source ---- */
static const char *shader_source =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct RectInstance {\n"
    "    float2 position;\n"
    "    float2 size;\n"
    "    float4 color;\n"
    "    float  border_radius;\n"
    "    float  stroke_width;\n"
    "    float  _pad[2];\n"
    "};\n"
    "\n"
    "struct VertexOut {\n"
    "    float4 position [[position]];\n"
    "    float4 color;\n"
    "    float2 local_pos;\n"
    "    float2 rect_size;\n"
    "    float  border_radius;\n"
    "    float  stroke_width;\n"
    "};\n"
    "\n"
    "constant float2 quad_verts[6] = {\n"
    "    {0,0}, {1,0}, {0,1},\n"
    "    {1,0}, {1,1}, {0,1},\n"
    "};\n"
    "\n"
    "vertex VertexOut rect_vertex(\n"
    "    uint vid [[vertex_id]],\n"
    "    uint iid [[instance_id]],\n"
    "    constant RectInstance *rects [[buffer(0)]],\n"
    "    constant float2 *viewport [[buffer(1)]]\n"
    ") {\n"
    "    RectInstance r = rects[iid];\n"
    "    float2 uv = quad_verts[vid];\n"
    "    float2 px = r.position + uv * r.size;\n"
    "\n"
    "    VertexOut out;\n"
    "    out.position = float4(\n"
    "        px.x / viewport->x * 2.0 - 1.0,\n"
    "        1.0 - px.y / viewport->y * 2.0,\n"
    "        0.0, 1.0\n"
    "    );\n"
    "    out.color = r.color;\n"
    "    out.local_pos = uv * r.size;\n"
    "    out.rect_size = r.size;\n"
    "    out.border_radius = r.border_radius;\n"
    "    out.stroke_width = r.stroke_width;\n"
    "    return out;\n"
    "}\n"
    "\n"
    "float roundedBoxSDF(float2 p, float2 half_size, float r) {\n"
    "    float2 q = abs(p) - (half_size - float2(r));\n"
    "    return length(max(q, 0.0)) - r;\n"
    "}\n"
    "\n"
    "fragment float4 rect_fragment(VertexOut in [[stage_in]]) {\n"
    "    float2 half_size = in.rect_size * 0.5;\n"
    "    float2 p = in.local_pos - half_size;\n"
    "    float r = min(in.border_radius, min(half_size.x, half_size.y));\n"
    "\n"
    "    if (in.stroke_width > 0.0) {\n"
    "        /* Stroke: outer SDF - inner SDF */\n"
    "        float d_outer = roundedBoxSDF(p, half_size, r);\n"
    "        float inner_r = max(r - in.stroke_width, 0.0);\n"
    "        float2 inner_half = half_size - float2(in.stroke_width);\n"
    "        float d_inner = roundedBoxSDF(p, inner_half, inner_r);\n"
    "        if (d_outer > 0.5) discard_fragment();\n"
    "        if (d_inner < -0.5) discard_fragment();\n"
    "        float a_outer = 1.0 - smoothstep(-0.5, 0.5, d_outer);\n"
    "        float a_inner = smoothstep(-0.5, 0.5, d_inner);\n"
    "        float a = a_outer * a_inner;\n"
    "        return float4(in.color.rgb * a, in.color.a * a);\n"
    "    }\n"
    "\n"
    "    if (r > 0.0) {\n"
    "        float d = roundedBoxSDF(p, half_size, r);\n"
    "        if (d > 0.5) discard_fragment();\n"
    "        float a = 1.0 - smoothstep(-0.5, 0.5, d);\n"
    "        return float4(in.color.rgb * a, in.color.a * a);\n"
    "    }\n"
    "    return in.color;\n"
    "}\n";

/* ---- C-side instance struct (must match shader RectInstance layout) ---- */
typedef struct __attribute__((aligned(16))) {
    float position[2];   /* offset 0  */
    float size[2];        /* offset 8  */
    float color[4];       /* offset 16 (16-byte aligned) */
    float border_radius;  /* offset 32 */
    float stroke_width;   /* offset 36 */
    float _pad[2];        /* offset 40, total = 48 bytes */
} MetalRectInstance;

/* ---- State ---- */
static id<MTLDevice>              g_device;
static id<MTLCommandQueue>        g_queue;
static id<MTLRenderPipelineState> g_rect_pipeline;
static CAMetalLayer              *g_layer;
static id<MTLBuffer>              g_rect_buf;
static id<MTLBuffer>              g_viewport_buf;
static id<CAMetalDrawable>        g_drawable;
static id<MTLCommandBuffer>       g_cmd_buf;

#define MAX_RECTS 4096

/* ---- Clip stack ---- */
#define MAX_CLIP_DEPTH 16

typedef struct {
    MTLScissorRect rect;
} ClipEntry;

/* ---- Opacity stack ---- */
#define MAX_OPACITY_DEPTH 16

/* ---- Helpers ---- */

static void unpack_color(uint32_t c, float *rgba) {
    rgba[0] = ((c >> 16) & 0xFF) / 255.0f;  /* R */
    rgba[1] = ((c >> 8)  & 0xFF) / 255.0f;  /* G */
    rgba[2] = (c & 0xFF) / 255.0f;           /* B */
    rgba[3] = ((c >> 24) & 0xFF) / 255.0f;   /* A */
}

static void unpack_color_with_opacity(uint32_t c, float opacity, float *rgba) {
    unpack_color(c, rgba);
    rgba[3] *= opacity;
}

/* ---- Backend functions ---- */

static int metal_init(void *layer) {
    g_layer = (__bridge CAMetalLayer *)layer;
    g_device = g_layer.device;
    if (!g_device) {
        g_device = MTLCreateSystemDefaultDevice();
    }
    if (!g_device) {
        fprintf(stderr, "[lightshell] No Metal device available\n");
        return -1;
    }
    g_layer.device = g_device;
    g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    g_queue = [g_device newCommandQueue];

    /* Compile shaders from embedded source */
    NSError *err = nil;
    NSString *src = [NSString stringWithUTF8String:shader_source];
    id<MTLLibrary> lib = [g_device newLibraryWithSource:src options:nil error:&err];
    if (!lib) {
        fprintf(stderr, "[lightshell] Shader compile error: %s\n",
                [[err description] UTF8String]);
        return -1;
    }

    id<MTLFunction> vfn = [lib newFunctionWithName:@"rect_vertex"];
    id<MTLFunction> ffn = [lib newFunctionWithName:@"rect_fragment"];
    if (!vfn || !ffn) {
        fprintf(stderr, "[lightshell] Could not find shader functions\n");
        return -1;
    }

    /* Render pipeline */
    MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction = vfn;
    pd.fragmentFunction = ffn;
    pd.colorAttachments[0].pixelFormat = g_layer.pixelFormat;

    /* Alpha blending */
    pd.colorAttachments[0].blendingEnabled = YES;
    pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    g_rect_pipeline = [g_device newRenderPipelineStateWithDescriptor:pd error:&err];
    if (!g_rect_pipeline) {
        fprintf(stderr, "[lightshell] Pipeline error: %s\n",
                [[err description] UTF8String]);
        return -1;
    }

    /* Allocate buffers */
    g_rect_buf = [g_device newBufferWithLength:MAX_RECTS * sizeof(MetalRectInstance)
                                       options:MTLResourceStorageModeShared];
    g_viewport_buf = [g_device newBufferWithLength:sizeof(float) * 2
                                           options:MTLResourceStorageModeShared];

    fprintf(stderr, "[lightshell] Metal initialized: %s\n",
            [[g_device name] UTF8String]);
    return 0;
}

static void metal_begin_frame(void) {
    g_drawable = [g_layer nextDrawable];
    if (!g_drawable) {
        /* Drawable may be unavailable transiently (e.g. window minimized) */
        return;
    }
    g_cmd_buf = [g_queue commandBuffer];
}

/* Flush pending rect instances as a draw call */
static void flush_rects(id<MTLRenderCommandEncoder> enc, uint32_t *rect_count) {
    if (*rect_count > 0) {
        [enc setRenderPipelineState:g_rect_pipeline];
        [enc setVertexBuffer:g_rect_buf   offset:0 atIndex:0];
        [enc setVertexBuffer:g_viewport_buf offset:0 atIndex:1];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:6
              instanceCount:*rect_count];
        *rect_count = 0;
    }
}

static void metal_render(DisplayList *dl) {
    if (!g_drawable || !g_cmd_buf) return;

    /* Update viewport buffer with drawable size */
    CGSize sz = g_layer.drawableSize;
    float *vp = (float *)[g_viewport_buf contents];
    vp[0] = (float)sz.width;
    vp[1] = (float)sz.height;

    /* Compute backing scale factor for point-to-pixel conversion */
    CGSize bounds = g_layer.bounds.size;
    float scale_x = (bounds.width > 0) ? (float)(sz.width / bounds.width) : 1.0f;
    float scale_y = (bounds.height > 0) ? (float)(sz.height / bounds.height) : 1.0f;

    /* Set up render pass */
    MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture = g_drawable.texture;
    rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpd.colorAttachments[0].clearColor = MTLClearColorMake(0.12, 0.12, 0.14, 1.0);

    id<MTLRenderCommandEncoder> enc =
        [g_cmd_buf renderCommandEncoderWithDescriptor:rpd];

    /* Clip stack */
    ClipEntry clip_stack[MAX_CLIP_DEPTH];
    int clip_depth = 0;

    /* Full-viewport scissor as default */
    MTLScissorRect full_scissor = {
        .x = 0, .y = 0,
        .width = (NSUInteger)sz.width,
        .height = (NSUInteger)sz.height
    };
    MTLScissorRect current_scissor = full_scissor;

    /* Opacity stack */
    float opacity_stack[MAX_OPACITY_DEPTH];
    int opacity_depth = 0;
    float current_opacity = 1.0f;

    /* Walk display list and build instance buffer, flushing on state changes */
    MetalRectInstance *rects = (MetalRectInstance *)[g_rect_buf contents];
    uint32_t rect_count = 0;

    for (uint32_t i = 0; i < dl->count; i++) {
        DisplayCommand *cmd = &dl->commands[i];
        switch (cmd->type) {
            case DL_FILL_RECT: {
                if (rect_count >= MAX_RECTS) {
                    flush_rects(enc, &rect_count);
                }
                MetalRectInstance *inst = &rects[rect_count++];
                inst->position[0] = cmd->fill_rect.x;
                inst->position[1] = cmd->fill_rect.y;
                inst->size[0] = cmd->fill_rect.w;
                inst->size[1] = cmd->fill_rect.h;
                unpack_color_with_opacity(cmd->fill_rect.color, current_opacity, inst->color);
                inst->border_radius = cmd->fill_rect.border_radius;
                inst->stroke_width = 0.0f;
                inst->_pad[0] = 0;
                inst->_pad[1] = 0;
                break;
            }

            case DL_STROKE_RECT: {
                if (rect_count >= MAX_RECTS) {
                    flush_rects(enc, &rect_count);
                }
                MetalRectInstance *inst = &rects[rect_count++];
                inst->position[0] = cmd->stroke_rect.x;
                inst->position[1] = cmd->stroke_rect.y;
                inst->size[0] = cmd->stroke_rect.w;
                inst->size[1] = cmd->stroke_rect.h;
                unpack_color_with_opacity(cmd->stroke_rect.color, current_opacity, inst->color);
                inst->border_radius = cmd->stroke_rect.border_radius;
                inst->stroke_width = cmd->stroke_rect.width;
                inst->_pad[0] = 0;
                inst->_pad[1] = 0;
                break;
            }

            case DL_PUSH_CLIP: {
                /* Flush pending rects before changing scissor */
                flush_rects(enc, &rect_count);

                if (clip_depth < MAX_CLIP_DEPTH) {
                    clip_stack[clip_depth++].rect = current_scissor;
                } else {
                    fprintf(stderr, "[lightshell] Clip stack overflow\n");
                }

                /* Convert point coordinates to pixel coordinates */
                NSUInteger cx = (NSUInteger)(cmd->clip.x * scale_x);
                NSUInteger cy = (NSUInteger)(cmd->clip.y * scale_y);
                NSUInteger cw = (NSUInteger)(cmd->clip.w * scale_x);
                NSUInteger ch = (NSUInteger)(cmd->clip.h * scale_y);

                /* Intersect with current scissor */
                NSUInteger ix = (cx > current_scissor.x) ? cx : current_scissor.x;
                NSUInteger iy = (cy > current_scissor.y) ? cy : current_scissor.y;
                NSUInteger right_new = cx + cw;
                NSUInteger right_cur = current_scissor.x + current_scissor.width;
                NSUInteger bottom_new = cy + ch;
                NSUInteger bottom_cur = current_scissor.y + current_scissor.height;
                NSUInteger ir = (right_new < right_cur) ? right_new : right_cur;
                NSUInteger ib = (bottom_new < bottom_cur) ? bottom_new : bottom_cur;

                if (ir > ix && ib > iy) {
                    current_scissor.x = ix;
                    current_scissor.y = iy;
                    current_scissor.width = ir - ix;
                    current_scissor.height = ib - iy;
                } else {
                    /* Degenerate: zero-area clip */
                    current_scissor.x = 0;
                    current_scissor.y = 0;
                    current_scissor.width = 0;
                    current_scissor.height = 0;
                }

                /* Clamp to texture dimensions to avoid Metal validation errors */
                NSUInteger tex_w = g_drawable.texture.width;
                NSUInteger tex_h = g_drawable.texture.height;
                if (current_scissor.x >= tex_w || current_scissor.y >= tex_h) {
                    current_scissor.x = 0;
                    current_scissor.y = 0;
                    current_scissor.width = 0;
                    current_scissor.height = 0;
                }
                if (current_scissor.x + current_scissor.width > tex_w) {
                    current_scissor.width = tex_w - current_scissor.x;
                }
                if (current_scissor.y + current_scissor.height > tex_h) {
                    current_scissor.height = tex_h - current_scissor.y;
                }

                /* Metal requires width and height > 0 for setScissorRect.
                 * If degenerate, set to 1x1 at origin (effectively clips everything). */
                if (current_scissor.width == 0) current_scissor.width = 1;
                if (current_scissor.height == 0) current_scissor.height = 1;

                [enc setScissorRect:current_scissor];
                break;
            }

            case DL_POP_CLIP: {
                /* Flush pending rects before changing scissor */
                flush_rects(enc, &rect_count);

                if (clip_depth > 0) {
                    current_scissor = clip_stack[--clip_depth].rect;
                } else {
                    current_scissor = full_scissor;
                    fprintf(stderr, "[lightshell] Clip stack underflow\n");
                }
                [enc setScissorRect:current_scissor];
                break;
            }

            case DL_PUSH_OPACITY: {
                /* Flush pending rects since subsequent rects use new opacity */
                flush_rects(enc, &rect_count);

                if (opacity_depth < MAX_OPACITY_DEPTH) {
                    opacity_stack[opacity_depth++] = current_opacity;
                } else {
                    fprintf(stderr, "[lightshell] Opacity stack overflow\n");
                }
                current_opacity *= cmd->opacity.alpha;
                break;
            }

            case DL_POP_OPACITY: {
                /* Flush pending rects before restoring opacity */
                flush_rects(enc, &rect_count);

                if (opacity_depth > 0) {
                    current_opacity = opacity_stack[--opacity_depth];
                } else {
                    current_opacity = 1.0f;
                    fprintf(stderr, "[lightshell] Opacity stack underflow\n");
                }
                break;
            }

            default:
                fprintf(stderr, "[lightshell] Ignoring display command type %d\n",
                        cmd->type);
                break;
        }
    }

    /* Flush remaining rects */
    flush_rects(enc, &rect_count);

    [enc endEncoding];
}

static void metal_present(void) {
    if (!g_drawable || !g_cmd_buf) return;
    [g_cmd_buf presentDrawable:g_drawable];
    [g_cmd_buf commit];
    g_drawable = nil;
    g_cmd_buf = nil;
}

static void metal_resize(uint32_t width, uint32_t height) {
    (void)width;
    (void)height;
    /* drawableSize is already managed by platform_darwin.m windowDidResize */
}

static void metal_destroy(void) {
    g_rect_pipeline = nil;
    g_rect_buf = nil;
    g_viewport_buf = nil;
    g_queue = nil;
    g_device = nil;
    g_layer = nil;
    fprintf(stderr, "[lightshell] Metal destroyed\n");
}

static uint32_t metal_load_texture(const uint8_t *data, uint32_t w, uint32_t h) {
    (void)data; (void)w; (void)h;
    return 0; /* stub */
}

static void metal_free_texture(uint32_t texture_id) {
    (void)texture_id; /* stub */
}

static void metal_update_glyph_atlas(const uint8_t *data,
                                      uint32_t x, uint32_t y,
                                      uint32_t w, uint32_t h) {
    (void)data; (void)x; (void)y; (void)w; (void)h; /* stub */
}

/* ---- Public API ---- */

static GPUBackend g_metal_backend = {
    .init              = metal_init,
    .begin_frame       = metal_begin_frame,
    .render            = metal_render,
    .present           = metal_present,
    .resize            = metal_resize,
    .destroy           = metal_destroy,
    .load_texture      = metal_load_texture,
    .free_texture      = metal_free_texture,
    .update_glyph_atlas = metal_update_glyph_atlas,
};

GPUBackend *gpu_metal_create(void) {
    return &g_metal_backend;
}
