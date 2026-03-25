/* gpu_metal.m - Metal GPU backend for LightShell
 *
 * Renders filled and stroked rectangles with optional rounded corners using
 * instanced drawing. Supports scissor clipping and opacity groups.
 * Each rect is a unit quad (6 vertices) instanced with
 * position/size/color/border_radius/stroke_width. Rounded corners and
 * stroke outlines use SDF in the fragment shader.
 *
 * Text rendering uses a glyph atlas (R8Unorm texture) with instanced
 * textured quads. The fragment shader samples grayscale as alpha.
 */

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include "gpu.h"
#include "glyph_atlas.h"
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
    "}\n"
    "\n"
    "/* ---- Image (textured quad) shaders ---- */\n"
    "struct ImageInstance {\n"
    "    float2 position;\n"
    "    float2 size;\n"
    "};\n"
    "\n"
    "struct ImageVertexOut {\n"
    "    float4 position [[position]];\n"
    "    float2 texcoord;\n"
    "};\n"
    "\n"
    "vertex ImageVertexOut image_vertex(\n"
    "    uint vid [[vertex_id]],\n"
    "    uint iid [[instance_id]],\n"
    "    constant ImageInstance *images [[buffer(0)]],\n"
    "    constant float2 *viewport [[buffer(1)]]\n"
    ") {\n"
    "    ImageInstance img = images[iid];\n"
    "    float2 uv = quad_verts[vid];\n"
    "    float2 px = img.position + uv * img.size;\n"
    "\n"
    "    ImageVertexOut out;\n"
    "    out.position = float4(px.x / viewport->x * 2.0 - 1.0,\n"
    "                          1.0 - px.y / viewport->y * 2.0, 0.0, 1.0);\n"
    "    out.texcoord = float2(uv.x, uv.y);\n"
    "    return out;\n"
    "}\n"
    "\n"
    "fragment float4 image_fragment(ImageVertexOut in [[stage_in]],\n"
    "                                texture2d<float> tex [[texture(0)]]) {\n"
    "    constexpr sampler s(filter::linear);\n"
    "    return tex.sample(s, in.texcoord);\n"
    "}\n"
    "\n"
    "/* ---- Glyph (text) shaders ---- */\n"
    "struct GlyphInstance {\n"
    "    float2 position;\n"
    "    float2 size;\n"
    "    float2 uv_min;\n"
    "    float2 uv_max;\n"
    "    float4 color;\n"
    "};\n"
    "\n"
    "struct GlyphVertexOut {\n"
    "    float4 position [[position]];\n"
    "    float2 texcoord;\n"
    "    float4 color;\n"
    "};\n"
    "\n"
    "vertex GlyphVertexOut glyph_vertex(\n"
    "    uint vid [[vertex_id]],\n"
    "    uint iid [[instance_id]],\n"
    "    constant GlyphInstance *glyphs [[buffer(0)]],\n"
    "    constant float2 *viewport [[buffer(1)]]\n"
    ") {\n"
    "    GlyphInstance g = glyphs[iid];\n"
    "    float2 uv = quad_verts[vid];\n"
    "    float2 px = g.position + uv * g.size;\n"
    "\n"
    "    GlyphVertexOut out;\n"
    "    out.position = float4(px.x / viewport->x * 2.0 - 1.0,\n"
    "                          1.0 - px.y / viewport->y * 2.0, 0.0, 1.0);\n"
    "    out.texcoord = mix(g.uv_min, g.uv_max, uv);\n"
    "    out.color = g.color;\n"
    "    return out;\n"
    "}\n"
    "\n"
    "fragment float4 glyph_fragment(GlyphVertexOut in [[stage_in]],\n"
    "                                texture2d<float> atlas [[texture(0)]]) {\n"
    "    constexpr sampler s(filter::linear);\n"
    "    float alpha = atlas.sample(s, in.texcoord).r;\n"
    "    return float4(in.color.rgb, in.color.a * alpha);\n"
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

/* ---- C-side image instance struct (must match shader ImageInstance layout) ---- */
typedef struct {
    float position[2];
    float size[2];
} MetalImageInstance;

/* ---- C-side glyph instance struct (must match shader GlyphInstance layout) ---- */
typedef struct {
    float position[2];   /* top-left in pixels */
    float size[2];       /* glyph bitmap size in pixels */
    float uv_min[2];     /* atlas UV top-left */
    float uv_max[2];     /* atlas UV bottom-right */
    float color[4];      /* text color with alpha */
} MetalGlyphInstance;

/* ---- State ---- */
static id<MTLDevice>              g_device;
static id<MTLCommandQueue>        g_queue;
static id<MTLRenderPipelineState> g_rect_pipeline;
static id<MTLRenderPipelineState> g_image_pipeline;
static id<MTLRenderPipelineState> g_glyph_pipeline;
static CAMetalLayer              *g_layer;
static id<MTLBuffer>              g_rect_buf;
static id<MTLBuffer>              g_viewport_buf;
static id<CAMetalDrawable>        g_drawable;
static id<MTLCommandBuffer>       g_cmd_buf;

#define MAX_RECTS 4096

/* ---- Texture storage ---- */
#define MAX_TEXTURES 256
static id<MTLTexture> g_textures[MAX_TEXTURES];
static uint32_t       g_texture_count = 0;

/* ---- Image instance buffer ---- */
#define MAX_IMAGES 1024
static id<MTLBuffer> g_image_buf;

/* ---- Glyph instance buffer and atlas texture ---- */
#define MAX_GLYPHS 4096
static id<MTLBuffer>  g_glyph_buf;
static id<MTLTexture> g_atlas_texture;

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

/* ---- Glyph atlas texture management ---- */

static void ensure_atlas_texture(void) {
    uint32_t aw = ls_glyph_atlas_width();
    uint32_t ah = ls_glyph_atlas_height();
    if (!g_atlas_texture && aw > 0 && ah > 0) {
        MTLTextureDescriptor *desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                              width:aw
                                                             height:ah
                                                           mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        g_atlas_texture = [g_device newTextureWithDescriptor:desc];
        fprintf(stderr, "[lightshell] Created glyph atlas texture %ux%u\n", aw, ah);
    }
}

static void upload_glyph_atlas_if_dirty(void) {
    if (!ls_glyph_atlas_dirty()) return;

    ensure_atlas_texture();
    if (!g_atlas_texture) return;

    const uint8_t *data = ls_glyph_atlas_data();
    uint32_t aw = ls_glyph_atlas_width();
    uint32_t ah = ls_glyph_atlas_height();
    if (!data) return;

    [g_atlas_texture replaceRegion:MTLRegionMake2D(0, 0, aw, ah)
                       mipmapLevel:0
                         withBytes:data
                       bytesPerRow:aw];
    ls_glyph_atlas_clear_dirty();
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

    /* Image pipeline (textured quads) */
    id<MTLFunction> img_vfn = [lib newFunctionWithName:@"image_vertex"];
    id<MTLFunction> img_ffn = [lib newFunctionWithName:@"image_fragment"];
    if (!img_vfn || !img_ffn) {
        fprintf(stderr, "[lightshell] Could not find image shader functions\n");
        return -1;
    }

    MTLRenderPipelineDescriptor *img_pd = [[MTLRenderPipelineDescriptor alloc] init];
    img_pd.vertexFunction = img_vfn;
    img_pd.fragmentFunction = img_ffn;
    img_pd.colorAttachments[0].pixelFormat = g_layer.pixelFormat;

    /* Alpha blending for images */
    img_pd.colorAttachments[0].blendingEnabled = YES;
    img_pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    img_pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    img_pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    img_pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    g_image_pipeline = [g_device newRenderPipelineStateWithDescriptor:img_pd error:&err];
    if (!g_image_pipeline) {
        fprintf(stderr, "[lightshell] Image pipeline error: %s\n",
                [[err description] UTF8String]);
        return -1;
    }

    /* Glyph pipeline (text rendering) */
    id<MTLFunction> glyph_vfn = [lib newFunctionWithName:@"glyph_vertex"];
    id<MTLFunction> glyph_ffn = [lib newFunctionWithName:@"glyph_fragment"];
    if (!glyph_vfn || !glyph_ffn) {
        fprintf(stderr, "[lightshell] Could not find glyph shader functions\n");
        return -1;
    }

    MTLRenderPipelineDescriptor *glyph_pd = [[MTLRenderPipelineDescriptor alloc] init];
    glyph_pd.vertexFunction = glyph_vfn;
    glyph_pd.fragmentFunction = glyph_ffn;
    glyph_pd.colorAttachments[0].pixelFormat = g_layer.pixelFormat;

    /* Alpha blending for text (premultiplied-style: src alpha * atlas alpha) */
    glyph_pd.colorAttachments[0].blendingEnabled = YES;
    glyph_pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    glyph_pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    glyph_pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    glyph_pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    g_glyph_pipeline = [g_device newRenderPipelineStateWithDescriptor:glyph_pd error:&err];
    if (!g_glyph_pipeline) {
        fprintf(stderr, "[lightshell] Glyph pipeline error: %s\n",
                [[err description] UTF8String]);
        return -1;
    }

    /* Allocate buffers */
    g_rect_buf = [g_device newBufferWithLength:MAX_RECTS * sizeof(MetalRectInstance)
                                       options:MTLResourceStorageModeShared];
    g_viewport_buf = [g_device newBufferWithLength:sizeof(float) * 2
                                           options:MTLResourceStorageModeShared];
    g_image_buf = [g_device newBufferWithLength:MAX_IMAGES * sizeof(MetalImageInstance)
                                        options:MTLResourceStorageModeShared];
    g_glyph_buf = [g_device newBufferWithLength:MAX_GLYPHS * sizeof(MetalGlyphInstance)
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

/* Draw a single textured image quad */
static void draw_image(id<MTLRenderCommandEncoder> enc,
                        float x, float y, float w, float h,
                        uint32_t texture_id) {
    if (texture_id == 0 || texture_id >= MAX_TEXTURES || !g_textures[texture_id]) {
        return;
    }

    MetalImageInstance *imgs = (MetalImageInstance *)[g_image_buf contents];
    imgs[0].position[0] = x;
    imgs[0].position[1] = y;
    imgs[0].size[0] = w;
    imgs[0].size[1] = h;

    [enc setRenderPipelineState:g_image_pipeline];
    [enc setVertexBuffer:g_image_buf    offset:0 atIndex:0];
    [enc setVertexBuffer:g_viewport_buf offset:0 atIndex:1];
    [enc setFragmentTexture:g_textures[texture_id] atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle
            vertexStart:0
            vertexCount:6
          instanceCount:1];
}

/* Draw text glyphs as instanced textured quads from the glyph atlas */
static void draw_text(id<MTLRenderCommandEncoder> enc,
                       DisplayCommand *cmd, float opacity) {
    if (!cmd->fill_text.glyphs || cmd->fill_text.glyph_count == 0) return;
    if (!g_atlas_texture) {
        ensure_atlas_texture();
        if (!g_atlas_texture) return;
    }

    /* Upload atlas if modified */
    upload_glyph_atlas_if_dirty();

    float text_color[4];
    unpack_color_with_opacity(cmd->fill_text.color, opacity, text_color);

    MetalGlyphInstance *instances = (MetalGlyphInstance *)[g_glyph_buf contents];
    uint32_t glyph_count = 0;

    float pen_x = cmd->fill_text.x;
    float pen_y = cmd->fill_text.y;
    float font_size = cmd->fill_text.font_size;

    for (uint32_t r = 0; r < cmd->fill_text.glyph_count; r++) {
        R8EGlyphRun *run = &cmd->fill_text.glyphs[r];
        for (uint32_t g = 0; g < run->count; g++) {
            R8EGlyphInfo *gi = &run->glyphs[g];

            /* Look up glyph in atlas (this may rasterize it) */
            GlyphAtlasEntry *entry = ls_glyph_atlas_get(gi->glyph_id, font_size);
            if (!entry || (entry->width == 0 && entry->height == 0)) {
                /* Space or missing glyph - just advance pen */
                pen_x += gi->x_advance;
                continue;
            }

            /* Re-upload atlas if new glyphs were rasterized */
            if (ls_glyph_atlas_dirty()) {
                upload_glyph_atlas_if_dirty();
            }

            if (glyph_count >= MAX_GLYPHS) break;

            MetalGlyphInstance *inst = &instances[glyph_count++];
            /* Position: pen + bearing offset (bearing_y is from baseline up) */
            inst->position[0] = pen_x + gi->x_offset + entry->bearing_x;
            inst->position[1] = pen_y + gi->y_offset - entry->bearing_y;
            inst->size[0] = entry->width;
            inst->size[1] = entry->height;
            inst->uv_min[0] = entry->u0;
            inst->uv_min[1] = entry->v0;
            inst->uv_max[0] = entry->u1;
            inst->uv_max[1] = entry->v1;
            inst->color[0] = text_color[0];
            inst->color[1] = text_color[1];
            inst->color[2] = text_color[2];
            inst->color[3] = text_color[3];

            pen_x += gi->x_advance;
        }
    }

    if (glyph_count > 0) {
        [enc setRenderPipelineState:g_glyph_pipeline];
        [enc setVertexBuffer:g_glyph_buf    offset:0 atIndex:0];
        [enc setVertexBuffer:g_viewport_buf offset:0 atIndex:1];
        [enc setFragmentTexture:g_atlas_texture atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:6
              instanceCount:glyph_count];
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

            case DL_FILL_TEXT: {
                /* Flush pending rects before switching to glyph pipeline */
                flush_rects(enc, &rect_count);
                draw_text(enc, cmd, current_opacity);
                break;
            }

            case DL_DRAW_IMAGE: {
                /* Flush pending rects before switching pipeline */
                flush_rects(enc, &rect_count);
                draw_image(enc,
                           cmd->draw_image.x, cmd->draw_image.y,
                           cmd->draw_image.w, cmd->draw_image.h,
                           cmd->draw_image.texture_id);
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
    /* Free textures */
    for (uint32_t i = 1; i <= g_texture_count && i < MAX_TEXTURES; i++) {
        g_textures[i] = nil;
    }
    g_texture_count = 0;

    g_atlas_texture = nil;
    g_rect_pipeline = nil;
    g_image_pipeline = nil;
    g_glyph_pipeline = nil;
    g_rect_buf = nil;
    g_image_buf = nil;
    g_glyph_buf = nil;
    g_viewport_buf = nil;
    g_queue = nil;
    g_device = nil;
    g_layer = nil;
    fprintf(stderr, "[lightshell] Metal destroyed\n");
}

static uint32_t metal_load_texture(const uint8_t *data, uint32_t w, uint32_t h) {
    if (!data || w == 0 || h == 0) return 0;
    if (g_texture_count + 1 >= MAX_TEXTURES) {
        fprintf(stderr, "[lightshell] Texture storage full\n");
        return 0;
    }

    MTLTextureDescriptor *desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                          width:w
                                                         height:h
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> tex = [g_device newTextureWithDescriptor:desc];
    if (!tex) {
        fprintf(stderr, "[lightshell] Failed to create texture %ux%u\n", w, h);
        return 0;
    }
    [tex replaceRegion:MTLRegionMake2D(0, 0, w, h)
           mipmapLevel:0
             withBytes:data
           bytesPerRow:w * 4];

    uint32_t tex_id = ++g_texture_count;
    if (tex_id < MAX_TEXTURES) {
        g_textures[tex_id] = tex;
    }
    fprintf(stderr, "[lightshell] Loaded texture id=%u (%ux%u)\n", tex_id, w, h);
    return tex_id;
}

static void metal_free_texture(uint32_t texture_id) {
    if (texture_id > 0 && texture_id < MAX_TEXTURES) {
        g_textures[texture_id] = nil;
    }
}

static void metal_update_glyph_atlas(const uint8_t *data,
                                      uint32_t x, uint32_t y,
                                      uint32_t w, uint32_t h) {
    /* Full atlas upload is handled by upload_glyph_atlas_if_dirty().
     * This entry point supports partial region updates if needed. */
    if (!data || w == 0 || h == 0) return;
    ensure_atlas_texture();
    if (!g_atlas_texture) return;

    [g_atlas_texture replaceRegion:MTLRegionMake2D(x, y, w, h)
                       mipmapLevel:0
                         withBytes:data
                       bytesPerRow:w];
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
