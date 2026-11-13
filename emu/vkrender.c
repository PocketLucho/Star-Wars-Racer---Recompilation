#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#endif
#include "volk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vkrender.h"
#include "shaders/hikaru_vert.h"
#include "shaders/hikaru_frag.h"
#include "shaders/overlay_vert.h"
#include "shaders/overlay_frag.h"
#include "shaders/kept_frag.h"

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "vk: %s failed (%d) at %s:%d\n", #x, r_, __FILE__, __LINE__); return -1; } } while (0)
#define CHECKV(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "vk: %s failed (%d) at %s:%d\n", #x, r_, __FILE__, __LINE__); return; } } while (0)

static struct {
    VkrConfig cfg;
    VkInstance inst; VkPhysicalDevice pd; VkDevice dev; VkQueue q; uint32_t qfam;
    VkPhysicalDeviceMemoryProperties mp; char name[256];
    VkCommandPool pool; VkCommandBuffer cmd; VkFence fence;
    /* TEXRAM */
    VkImage tex; VkDeviceMemory texmem; VkImageView texview; VkSampler samp;
    VkBuffer stage; VkDeviceMemory stagemem; void *stagemap; int tex_dirty, tex_init;
    /* render target */
    int w, h;
    VkImage color, depth; VkDeviceMemory colormem, depthmem; VkImageView colorview, depthview;
    VkRenderPass rp; VkFramebuffer fbo;
    int msaa;                         /* samples (1 = off); colour/depth multisampled, resolved into `color` */
    VkImage mscolor; VkDeviceMemory mscolormem; VkImageView mscolorview;
    VkDescriptorSetLayout dsl; VkDescriptorPool dpool; VkDescriptorSet ds;
    VkPipelineLayout pl; VkPipeline pipe_opaque, pipe_blend, pipe_overlay, pipe_top, pipe_add, pipe_top_add;
    int pending;                      /* a submitted frame whose fence has not been waited yet */
    int wide;                         /* 16:9 render target (3D widened, 2D layers centred) */
    int shot_req, shot_ready;         /* screenshot: copy the next frame into the readback buffer */
    /* 2D layers: 640x960 R16_UINT */
    VkImage lay; VkDeviceMemory laymem; VkImageView layview; VkBuffer laystage; VkDeviceMemory laystagemem; void *laystagemap;
    int lay_dirty, lay_init; uint32_t lay_ena, lay_direct;
    VkPipeline pipe_overlay_behind;
    /* framebuffer blender (181): the 3D picture of the frame before, a pass that continues the frame after it was
       copied (same attachments, colour loaded) and the pipeline that mixes the kept picture over the new one */
    VkImage kept; VkDeviceMemory keptmem; VkImageView keptview; VkRenderPass rp_cont; VkPipeline pipe_kept;
    uint64_t frame_no, kept_frame; int kept_init;
    /* vertices */
    VkBuffer vb; VkDeviceMemory vbmem; void *vbmap; size_t vbcap;
    /* readback */
    VkBuffer rb; VkDeviceMemory rbmem; void *rbmap;
    /* windowed */
    VkSurfaceKHR surf; VkSwapchainKHR sc; VkFormat scfmt; VkExtent2D scext;
    VkImage scimg[8]; uint32_t scn; VkSemaphore sem_acq, sem_done; int win_w, win_h, need_recreate;
} V;

static uint32_t memtype(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < V.mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (V.mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return 0xffffffff;
}

static int make_buffer(VkDeviceSize sz, VkBufferUsageFlags use, VkBuffer *b, VkDeviceMemory *m, void **map) {
    VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 0, 0, sz, use, VK_SHARING_MODE_EXCLUSIVE };
    CHECK(vkCreateBuffer(V.dev, &bi, 0, b));
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(V.dev, *b, &mr);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, 0, mr.size,
        memtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    CHECK(vkAllocateMemory(V.dev, &ai, 0, m));
    CHECK(vkBindBufferMemory(V.dev, *b, *m, 0));
    CHECK(vkMapMemory(V.dev, *m, 0, VK_WHOLE_SIZE, 0, map));
    return 0;
}

static int mk_samples = 1;   /* sample count for the next make_image */
static int make_image(VkFormat fmt, uint32_t w, uint32_t h, uint32_t layers, VkImageUsageFlags use, VkImageAspectFlags asp,
                      VkImage *img, VkDeviceMemory *mem, VkImageView *view) {
    VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ii.imageType = VK_IMAGE_TYPE_2D; ii.format = fmt; ii.extent = (VkExtent3D){ w, h, 1 };
    ii.mipLevels = 1; ii.arrayLayers = layers; ii.samples = (VkSampleCountFlagBits)mk_samples; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = use; ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    CHECK(vkCreateImage(V.dev, &ii, 0, img));
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(V.dev, *img, &mr);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, 0, mr.size, memtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
    if (ai.memoryTypeIndex == 0xffffffff) ai.memoryTypeIndex = memtype(mr.memoryTypeBits, 0);
    CHECK(vkAllocateMemory(V.dev, &ai, 0, mem));
    CHECK(vkBindImageMemory(V.dev, *img, *mem, 0));
    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = *img; vi.viewType = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
    vi.subresourceRange = (VkImageSubresourceRange){ asp, 0, 1, 0, layers };
    CHECK(vkCreateImageView(V.dev, &vi, 0, view));
    return 0;
}

static void barrier(VkImage img, VkImageAspectFlags asp, uint32_t layers, VkImageLayout from, VkImageLayout to,
                    VkAccessFlags sa, VkAccessFlags da, VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
    VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.srcAccessMask = sa; b.dstAccessMask = da; b.oldLayout = from; b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img; b.subresourceRange = (VkImageSubresourceRange){ asp, 0, 1, 0, layers };
    vkCmdPipelineBarrier(V.cmd, ss, ds, 0, 0, 0, 0, 0, 1, &b);
}

static VkShaderModule shader(const uint32_t *code, size_t sz) {
    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, 0, 0, sz, code };
    VkShaderModule m = 0; vkCreateShaderModule(V.dev, &ci, 0, &m); return m;
}

static int make_pipeline(int blend, VkPipeline *out) {   /* 0 opaque, 1 translucent, 2 2D overlay, 3 on-top, 4 additive, 5 on-top additive, 6 2D behind, 7 kept picture */
    int behind = blend == 6, kept = blend == 7; if (behind || kept) blend = 2;
    int ov = blend == 2, top = blend == 3 || blend == 5, add = blend == 4 || blend == 5;
    if (top || add) blend = 1;
    VkShaderModule vs = ov ? shader(overlay_vert_spv, sizeof overlay_vert_spv) : shader(hikaru_vert_spv, sizeof hikaru_vert_spv);
    VkShaderModule fs = kept ? shader(kept_frag_spv, sizeof kept_frag_spv) : ov ? shader(overlay_frag_spv, sizeof overlay_frag_spv) : shader(hikaru_frag_spv, sizeof hikaru_frag_spv);
    VkPipelineShaderStageCreateInfo st[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 0, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 0, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main" } };
    VkVertexInputBindingDescription bind = { 0, sizeof(GfxVertex), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription at[5] = {
        { 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GfxVertex, clip) },
        { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GfxVertex, uv) },
        { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(GfxVertex, color) },
        { 3, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(GfxVertex, tex) },
        { 4, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(GfxVertex, fog) } };
    VkPipelineVertexInputStateCreateInfo vin = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, 0, 0, ov ? 0u : 1u, &bind, ov ? 0u : 5u, at };
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, 0, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, 0, 0, 1, 0, 1, 0 };
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, 0, 0, (VkSampleCountFlagBits)V.msaa };
    VkPipelineDepthStencilStateCreateInfo dss = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    dss.depthTestEnable = (ov || top) ? VK_FALSE : VK_TRUE; dss.depthWriteEnable = blend ? VK_FALSE : VK_TRUE;
    dss.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;              /* reversed depth: z = near / w */
    VkPipelineColorBlendAttachmentState cba = { 0 };
    cba.colorWriteMask = 0xf;
    if (blend == 1) {
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.dstColorBlendFactor = add ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cba.alphaBlendOp = VK_BLEND_OP_ADD;   /* alpha = 3D coverage */
    }
    if (behind) {   /* 2D layer under the 3D: result = layer * (1 - coverage) + 3D * coverage */
        cba.blendEnable = VK_TRUE; cba.colorWriteMask = 0x7;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA; cba.dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA; cba.colorBlendOp = VK_BLEND_OP_ADD;
    }
    if (kept) {     /* new = kept * k + drawn * (1 - k); the coverage in alpha stays that of the new picture */
        cba.blendEnable = VK_TRUE; cba.colorWriteMask = 0x7;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cba.colorBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, 0, 0, VK_FALSE, 0, 1, &cba };
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, 0, 0, 2, dyn };
    VkGraphicsPipelineCreateInfo pi = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pi.stageCount = 2; pi.pStages = st; pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia; pi.pViewportState = &vp;
    pi.pRasterizationState = &rs; pi.pMultisampleState = &ms; pi.pDepthStencilState = &dss; pi.pColorBlendState = &cb;
    pi.pDynamicState = &dy; pi.layout = V.pl; pi.renderPass = V.rp;
    CHECK(vkCreateGraphicsPipelines(V.dev, 0, 1, &pi, 0, out));
    vkDestroyShaderModule(V.dev, vs, 0); vkDestroyShaderModule(V.dev, fs, 0);
    return 0;
}

static int make_swapchain(void) {
    VkSurfaceCapabilitiesKHR caps; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(V.pd, V.surf, &caps);
    uint32_t nf = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(V.pd, V.surf, &nf, 0);
    VkSurfaceFormatKHR fmts[64]; if (nf > 64) nf = 64; vkGetPhysicalDeviceSurfaceFormatsKHR(V.pd, V.surf, &nf, fmts);
    VkSurfaceFormatKHR f = fmts[0];
    for (uint32_t i = 0; i < nf; i++) if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) f = fmts[i];
    V.scfmt = f.format;
    V.scext = caps.currentExtent;
    if (V.scext.width == 0xffffffff) { V.scext.width = V.win_w; V.scext.height = V.win_h; }
    if (!V.scext.width || !V.scext.height) return 1;   /* minimized */
    VkSwapchainCreateInfoKHR sci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    sci.surface = V.surf; sci.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && sci.minImageCount > caps.maxImageCount) sci.minImageCount = caps.maxImageCount;
    sci.imageFormat = f.format; sci.imageColorSpace = f.colorSpace; sci.imageExtent = V.scext; sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = V.cfg.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    { uint32_t np = 0; VkPresentModeKHR pm[8]; vkGetPhysicalDeviceSurfacePresentModesKHR(V.pd, V.surf, &np, 0); if (np > 8) np = 8;
      vkGetPhysicalDeviceSurfacePresentModesKHR(V.pd, V.surf, &np, pm); int ok = 0;
      for (uint32_t i = 0; i < np; i++) if (pm[i] == sci.presentMode) ok = 1;
      if (!ok) sci.presentMode = VK_PRESENT_MODE_FIFO_KHR; }
    sci.clipped = VK_TRUE; sci.oldSwapchain = V.sc;
    VkSwapchainKHR nsc; CHECK(vkCreateSwapchainKHR(V.dev, &sci, 0, &nsc));
    if (V.sc) vkDestroySwapchainKHR(V.dev, V.sc, 0);
    V.sc = nsc;
    V.scn = 8; vkGetSwapchainImagesKHR(V.dev, V.sc, &V.scn, V.scimg);
    return 0;
}

static void kept_descriptor(void) {
    VkDescriptorImageInfo di = { V.samp, V.keptview, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 0, V.ds, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &di };
    vkUpdateDescriptorSets(V.dev, 1, &w, 0, 0);
}
/* render target (colour + depth + framebuffer + readback buffer); recreated when scale / aspect / MSAA change.
 * With MSAA the pass draws into multisampled colour/depth and resolves into `color` (what gets blitted/read back). */
static int make_targets(void) {
    V.w = (V.wide ? 854 : 640) * V.cfg.scale; V.h = 480 * V.cfg.scale;
    if (make_image(VK_FORMAT_R8G8B8A8_UNORM, V.w, V.h, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                   VK_IMAGE_ASPECT_COLOR_BIT, &V.color, &V.colormem, &V.colorview)) return -1;
    if (make_image(VK_FORMAT_R8G8B8A8_UNORM, V.w, V.h, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                   VK_IMAGE_ASPECT_COLOR_BIT, &V.kept, &V.keptmem, &V.keptview)) return -1;
    V.kept_init = 0; V.kept_frame = 0;
    if (V.ds) kept_descriptor();
    mk_samples = V.msaa;
    if (V.msaa > 1 && make_image(VK_FORMAT_R8G8B8A8_UNORM, V.w, V.h, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                   VK_IMAGE_ASPECT_COLOR_BIT, &V.mscolor, &V.mscolormem, &V.mscolorview)) { mk_samples = 1; return -1; }
    int r = make_image(VK_FORMAT_D32_SFLOAT, V.w, V.h, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
                   &V.depth, &V.depthmem, &V.depthview);
    mk_samples = 1;
    if (r) return -1;
    VkImageView fbv[3] = { V.msaa > 1 ? V.mscolorview : V.colorview, V.depthview, V.colorview };
    VkFramebufferCreateInfo fbi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, 0, 0, V.rp, V.msaa > 1 ? 3u : 2u, fbv, (uint32_t)V.w, (uint32_t)V.h, 1 };
    CHECK(vkCreateFramebuffer(V.dev, &fbi, 0, &V.fbo));
    if (make_buffer((VkDeviceSize)V.w * V.h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &V.rb, &V.rbmem, &V.rbmap)) return -1;
    return 0;
}
static void destroy_targets(void) {
    vkDestroyFramebuffer(V.dev, V.fbo, 0);
    vkDestroyImageView(V.dev, V.colorview, 0); vkDestroyImage(V.dev, V.color, 0); vkFreeMemory(V.dev, V.colormem, 0);
    vkDestroyImageView(V.dev, V.keptview, 0); vkDestroyImage(V.dev, V.kept, 0); vkFreeMemory(V.dev, V.keptmem, 0);
    if (V.mscolor) { vkDestroyImageView(V.dev, V.mscolorview, 0); vkDestroyImage(V.dev, V.mscolor, 0); vkFreeMemory(V.dev, V.mscolormem, 0); V.mscolor = 0; }
    vkDestroyImageView(V.dev, V.depthview, 0); vkDestroyImage(V.dev, V.depth, 0); vkFreeMemory(V.dev, V.depthmem, 0);
    vkUnmapMemory(V.dev, V.rbmem); vkDestroyBuffer(V.dev, V.rb, 0); vkFreeMemory(V.dev, V.rbmem, 0);
}
/* render pass + pipelines depend on the sample count */
static int make_pass(void) {
    VkSampleCountFlagBits ns = (VkSampleCountFlagBits)V.msaa;
    VkAttachmentDescription att[3] = {
        { 0, VK_FORMAT_R8G8B8A8_UNORM, ns, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
          VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
          V.msaa > 1 ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL },
        { 0, VK_FORMAT_D32_SFLOAT, ns, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
          VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL },
        { 0, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
          VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL } };
    VkAttachmentReference cref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }, dref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL },
                          rref = { 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = { 0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 0, 1, &cref, V.msaa > 1 ? &rref : 0, &dref, 0, 0 };
    VkRenderPassCreateInfo rpi = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, 0, 0, V.msaa > 1 ? 3u : 2u, att, 1, &sub, 0, 0 };
    CHECK(vkCreateRenderPass(V.dev, &rpi, 0, &V.rp));
    /* the same pass going on with what the first one left (compatible: same pipelines and framebuffer) */
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; att[0].initialLayout = V.msaa > 1 ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    CHECK(vkCreateRenderPass(V.dev, &rpi, 0, &V.rp_cont));
    return 0;
}
static int make_pipes(void) {
    return make_pipeline(0, &V.pipe_opaque) || make_pipeline(1, &V.pipe_blend) || make_pipeline(2, &V.pipe_overlay) || make_pipeline(3, &V.pipe_top) ||
           make_pipeline(4, &V.pipe_add) || make_pipeline(5, &V.pipe_top_add) || make_pipeline(6, &V.pipe_overlay_behind) ||
           make_pipeline(7, &V.pipe_kept) ? -1 : 0;
}
static void destroy_pass(void) {
    VkPipeline p[8] = { V.pipe_opaque, V.pipe_blend, V.pipe_overlay, V.pipe_top, V.pipe_add, V.pipe_top_add, V.pipe_overlay_behind, V.pipe_kept };
    for (int i = 0; i < 8; i++) if (p[i]) vkDestroyPipeline(V.dev, p[i], 0);
    vkDestroyRenderPass(V.dev, V.rp, 0); vkDestroyRenderPass(V.dev, V.rp_cont, 0);
}
static int clamp_msaa(int n) {   /* highest supported count <= n (1/2/4/8) */
    VkPhysicalDeviceProperties pp; vkGetPhysicalDeviceProperties(V.pd, &pp);
    VkSampleCountFlags ok = pp.limits.framebufferColorSampleCounts & pp.limits.framebufferDepthSampleCounts;
    int s = 1; for (int c = 2; c <= 8; c *= 2) if (c <= n && (ok & (VkSampleCountFlags)c)) s = c;
    return s;
}

int vkr_init(const VkrConfig *cfg) {
    memset(&V, 0, sizeof V);
    V.cfg = *cfg; if (V.cfg.scale < 1) V.cfg.scale = 1; if (V.cfg.scale > 8) V.cfg.scale = 8;
    V.wide = cfg->wide;
    if (volkInitialize() != VK_SUCCESS) { fprintf(stderr, "vk: no Vulkan loader\n"); return -1; }
    const char *iext[16]; uint32_t niext = 0;
    if (!cfg->headless && cfg->create_surface) {          /* surface from the frontend (SDL): its extension list */
        for (uint32_t i = 0; i < cfg->n_inst_ext && niext < 16; i++) iext[niext++] = cfg->inst_ext[i];
    } else if (!cfg->headless) {
        iext[niext++] = VK_KHR_SURFACE_EXTENSION_NAME;
#ifdef _WIN32
        iext[niext++] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#endif
    }
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, 0, "Star Wars Racer Arcade Recomp", 1, "swracer-recomp", 1, VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, 0, 0, &app, 0, 0, niext, iext };
    CHECK(vkCreateInstance(&ici, 0, &V.inst));
    volkLoadInstance(V.inst);
    if (!cfg->headless && cfg->create_surface) {
        if (cfg->create_surface(V.inst, cfg->surface_user, &V.surf) || !V.surf) { fprintf(stderr, "vk: could not create the window surface\n"); return -1; }
    }
#ifdef _WIN32
    else if (!cfg->headless) {
        VkWin32SurfaceCreateInfoKHR si = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, 0, 0, (HINSTANCE)cfg->win32_hinstance, (HWND)cfg->win32_hwnd };
        CHECK(vkCreateWin32SurfaceKHR(V.inst, &si, 0, &V.surf));
    }
#endif
    uint32_t npd = 0; vkEnumeratePhysicalDevices(V.inst, &npd, 0);
    if (!npd) { fprintf(stderr, "vk: no physical device\n"); return -1; }
    VkPhysicalDevice pds[16]; if (npd > 16) npd = 16; vkEnumeratePhysicalDevices(V.inst, &npd, pds);
    int best = -1, bestscore = -1; uint32_t bestq = 0;
    for (uint32_t i = 0; i < npd; i++) {
        VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pds[i], &p);
        uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &nq, 0);
        VkQueueFamilyProperties qp[16]; if (nq > 16) nq = 16; vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &nq, qp);
        for (uint32_t k = 0; k < nq; k++) {
            if (!(qp[k].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 pres = VK_TRUE;
            if (V.surf) vkGetPhysicalDeviceSurfaceSupportKHR(pds[i], k, V.surf, &pres);
            if (!pres) continue;
            int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > bestscore) { bestscore = score; best = i; bestq = k; }
            break;
        }
    }
    if (best < 0) { fprintf(stderr, "vk: no suitable device\n"); return -1; }
    V.pd = pds[best]; V.qfam = bestq;
    { VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(V.pd, &p); snprintf(V.name, sizeof V.name, "%s", p.deviceName); }
    vkGetPhysicalDeviceMemoryProperties(V.pd, &V.mp);
    float prio = 1;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, 0, 0, V.qfam, 1, &prio };
    const char *dext[1] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, 0, 0, 1, &qci, 0, 0, V.surf ? 1u : 0u, dext, 0 };
    CHECK(vkCreateDevice(V.pd, &dci, 0, &V.dev));
    volkLoadDevice(V.dev);
    vkGetDeviceQueue(V.dev, V.qfam, 0, &V.q);
    VkCommandPoolCreateInfo cpi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, 0, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, V.qfam };
    CHECK(vkCreateCommandPool(V.dev, &cpi, 0, &V.pool));
    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, 0, V.pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
    CHECK(vkAllocateCommandBuffers(V.dev, &cai, &V.cmd));
    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    CHECK(vkCreateFence(V.dev, &fci, 0, &V.fence));
    /* TEXRAM: 2 layers of 2048x1024 R16_UINT, fed from an 8 MB staging buffer */
    if (make_image(VK_FORMAT_R16_UINT, 2048, 1024, 2, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                   &V.tex, &V.texmem, &V.texview)) return -1;
    if (make_buffer(0x800000, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &V.stage, &V.stagemem, &V.stagemap)) return -1;
    memset(V.stagemap, 0, 0x800000); V.tex_dirty = 1;
    VkSamplerCreateInfo sci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sci.magFilter = sci.minFilter = VK_FILTER_NEAREST; sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    CHECK(vkCreateSampler(V.dev, &sci, 0, &V.samp));
    V.msaa = clamp_msaa(cfg->msaa);
    if (make_pass()) return -1;
    if (make_targets()) return -1;
    /* descriptors */
    /* 2D layers image + staging */
    if (make_image(VK_FORMAT_R16_UINT, 640, 960, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                   &V.lay, &V.laymem, &V.layview)) return -1;
    if (make_buffer(640 * 960 * 2, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &V.laystage, &V.laystagemem, &V.laystagemap)) return -1;
    memset(V.laystagemap, 0, 640 * 960 * 2); V.lay_dirty = 1;
    VkDescriptorSetLayoutBinding b0[3] = { { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, 0 },
                                           { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, 0 },
                                           { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, 0 } };
    VkDescriptorSetLayoutCreateInfo dli = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, 0, 0, 3, b0 };
    CHECK(vkCreateDescriptorSetLayout(V.dev, &dli, 0, &V.dsl));
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 };
    VkDescriptorPoolCreateInfo dpi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, 0, 0, 1, 1, &ps };
    CHECK(vkCreateDescriptorPool(V.dev, &dpi, 0, &V.dpool));
    VkDescriptorSetAllocateInfo dai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, 0, V.dpool, 1, &V.dsl };
    CHECK(vkAllocateDescriptorSets(V.dev, &dai, &V.ds));
    VkDescriptorImageInfo dii[2] = { { V.samp, V.texview, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                                     { V.samp, V.layview, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } };
    VkWriteDescriptorSet wds[2] = { { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 0, V.ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &dii[0] },
                                    { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 0, V.ds, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &dii[1] } };
    vkUpdateDescriptorSets(V.dev, 2, wds, 0, 0);
    kept_descriptor();
    VkPushConstantRange pcr = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, 12 };
    VkPipelineLayoutCreateInfo pli = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, 0, 0, 1, &V.dsl, 1, &pcr };
    CHECK(vkCreatePipelineLayout(V.dev, &pli, 0, &V.pl));
    if (make_pipes()) return -1;
    if (!cfg->headless) {
        VkSemaphoreCreateInfo smi = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        CHECK(vkCreateSemaphore(V.dev, &smi, 0, &V.sem_acq)); CHECK(vkCreateSemaphore(V.dev, &smi, 0, &V.sem_done));
        if (make_swapchain() < 0) return -1;
    }
    return 0;
}

/* CPU/GPU overlap: vkr_draw returns right after submitting; whoever touches GPU-visible memory next waits here */
static void vkr_wait(void) {
    if (!V.pending) return;
    vkWaitForFences(V.dev, 1, &V.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(V.dev, 1, &V.fence);
    V.pending = 0;
}

void vkr_texram_upload(const uint8_t *hw) {
    vkr_wait();
    /* TEXRAM as written by IDMA is already linear 16-bit texels (verified on the SEGA logo / font) */
    memcpy(V.stagemap, hw, 0x800000);
    V.tex_dirty = 1;
}

void vkr_layers_upload(const uint16_t *lay640x960, uint32_t ena, uint32_t direct) {
    vkr_wait();
    V.lay_ena = ena; V.lay_direct = direct;
    if (ena) { memcpy(V.laystagemap, lay640x960, 640 * 960 * 2); V.lay_dirty = 1; }
}

void vkr_draw(const GfxFrame *f) {
    vkr_wait();
    size_t need = (size_t)(f->nv ? f->nv : 1) * sizeof(GfxVertex);
    if (need > V.vbcap) {
        if (V.vb) { vkDeviceWaitIdle(V.dev); vkDestroyBuffer(V.dev, V.vb, 0); vkFreeMemory(V.dev, V.vbmem, 0); }
        V.vbcap = need * 2;
        if (make_buffer(V.vbcap, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &V.vb, &V.vbmem, &V.vbmap)) return;
    }
    memcpy(V.vbmap, f->v, (size_t)f->nv * sizeof(GfxVertex));
    uint32_t img = 0; int present = 0;
    if (V.surf) {
        if (V.need_recreate) { vkDeviceWaitIdle(V.dev); V.need_recreate = 0; if (make_swapchain()) return; }
        VkResult r = vkAcquireNextImageKHR(V.dev, V.sc, UINT64_MAX, V.sem_acq, 0, &img);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) { V.need_recreate = 1; return; }
        present = (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR);
    }
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 0, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkResetCommandBuffer(V.cmd, 0);
    vkBeginCommandBuffer(V.cmd, &bi);
    if (V.tex_dirty) {
        barrier(V.tex, VK_IMAGE_ASPECT_COLOR_BIT, 2, V.tex_init ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy c = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2 }, { 0, 0, 0 }, { 2048, 1024, 1 } };
        vkCmdCopyBufferToImage(V.cmd, V.stage, V.tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        barrier(V.tex, VK_IMAGE_ASPECT_COLOR_BIT, 2, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        V.tex_dirty = 0; V.tex_init = 1;
    }
    if (V.lay_dirty) {
        barrier(V.lay, VK_IMAGE_ASPECT_COLOR_BIT, 1, V.lay_init ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy c = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { 640, 960, 1 } };
        vkCmdCopyBufferToImage(V.cmd, V.laystage, V.lay, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        barrier(V.lay, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        V.lay_dirty = 0; V.lay_init = 1;
    }
    VkClearValue cv[3]; memset(cv, 0, sizeof cv);
    cv[0].color.float32[0] = f->clear[0]; cv[0].color.float32[1] = f->clear[1]; cv[0].color.float32[2] = f->clear[2]; cv[0].color.float32[3] = 0;   /* alpha = coverage */
    cv[1].depthStencil.depth = 0.0f;                                   /* reversed depth */
    VkRenderPassBeginInfo rbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, 0, V.rp, V.fbo, { { 0, 0 }, { (uint32_t)V.w, (uint32_t)V.h } }, V.msaa > 1 ? 3u : 2u, cv };
    vkCmdBeginRenderPass(V.cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = { 0, 0, (float)V.w, (float)V.h, 0, 1 };
    VkRect2D sc = { { 0, 0 }, { (uint32_t)V.w, (uint32_t)V.h } };
    vkCmdSetViewport(V.cmd, 0, 1, &vp); vkCmdSetScissor(V.cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(V.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, V.pl, 0, 1, &V.ds, 0, 0);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(V.cmd, 0, 1, &V.vb, &off);
    {   VkPipeline pk[5] = { V.pipe_opaque, V.pipe_blend, V.pipe_add, V.pipe_top, V.pipe_top_add }; VkPipeline cur = 0;
        int16_t cur_clip[4] = { -1, -1, -1, -1 };
        for (int i = 0; i < f->nr; i++) {
            const GfxRange *g = &f->r[i];
            if (g->clear_depth) {   /* next viewport number: a depth buffer of its own */
                VkClearAttachment ca = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, { .depthStencil = { 0.0f, 0 } } };
                VkClearRect cr = { { { 0, 0 }, { (uint32_t)V.w, (uint32_t)V.h } }, 0, 1 };
                vkCmdClearAttachments(V.cmd, 1, &ca, 1, &cr);
            }
            if (memcmp(cur_clip, g->clip, sizeof cur_clip)) {
                memcpy(cur_clip, g->clip, sizeof cur_clip);
                float s = (float)V.cfg.scale, xs = V.wide ? 0.75f : 1.0f, half = V.w * 0.5f;
                int x0, x1;
                if (g->clip[0] <= 0 && g->clip[2] >= 640) { x0 = 0; x1 = V.w; }      /* whole width: the widened 3D too */
                else { x0 = (int)(((g->clip[0] / 320.0f - 1.0f) * xs + 1.0f) * half); x1 = (int)(((g->clip[2] / 320.0f - 1.0f) * xs + 1.0f) * half + 0.999f); }
                int y0 = (int)(g->clip[1] * s), y1 = (int)(g->clip[3] * s);
                if (x0 < 0) x0 = 0; if (x1 > V.w) x1 = V.w; if (y0 < 0) y0 = 0; if (y1 > V.h) y1 = V.h;
                VkRect2D sc = { { x0, y0 }, { (uint32_t)(x1 > x0 ? x1 - x0 : 0), (uint32_t)(y1 > y0 ? y1 - y0 : 0) } };
                vkCmdSetScissor(V.cmd, 0, 1, &sc);
            }
            if (pk[g->kind] != cur) { cur = pk[g->kind]; vkCmdBindPipeline(V.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cur); }
            vkCmdDraw(V.cmd, g->count, 1, g->first, 0);
        }
        {   /* the 2D layers: whole target again */
            VkRect2D sc = { { 0, 0 }, { (uint32_t)V.w, (uint32_t)V.h } }; vkCmdSetScissor(V.cmd, 0, 1, &sc);
        }
    }
    V.frame_no++;
    if (f->blend_rate > 0) {
        /* framebuffer blender: mix the 3D of the frame before over this one, keep the result for the next frame
           (before the 2D layers go on top) and go on in a second pass */
        if (V.kept_init && V.kept_frame + 1 == V.frame_no) {
            float k = (float)(f->blend_rate + (f->blend_rate >> 7)) / 256.0f;
            vkCmdBindPipeline(V.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, V.pipe_kept);
            vkCmdPushConstants(V.cmd, V.pl, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof k, &k);
            vkCmdDraw(V.cmd, 3, 1, 0, 0);
        }
        vkCmdEndRenderPass(V.cmd);
        barrier(V.kept, VK_IMAGE_ASPECT_COLOR_BIT, 1, V.kept_init ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkImageCopy ic = { { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)V.w, (uint32_t)V.h, 1 } };
        vkCmdCopyImage(V.cmd, V.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, V.kept, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ic);
        barrier(V.kept, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        V.kept_init = 1; V.kept_frame = V.frame_no;
        rbi.renderPass = V.rp_cont; rbi.clearValueCount = 0; rbi.pClearValues = 0;
        vkCmdBeginRenderPass(V.cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(V.cmd, 0, 1, &vp); vkCmdSetScissor(V.cmd, 0, 1, &sc);
        vkCmdBindDescriptorSets(V.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, V.pl, 0, 1, &V.ds, 0, 0);
    }
    if (V.lay_ena && V.lay_init) {   /* layers behind the 3D first (blend on coverage), then the direct ones on top */
        for (int pass = 0; pass < 2; pass++) {
            uint32_t ena = (V.lay_ena & (pass ? V.lay_direct : ~V.lay_direct) & 3) | (V.lay_ena & 4);   /* bit 2: layer 1 on top */
            if (!ena) continue;
            struct { uint32_t ena; float scale; float xoff; } pc = { ena, (float)V.cfg.scale, (float)(V.w - 640 * V.cfg.scale) * 0.5f };
            vkCmdBindPipeline(V.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass ? V.pipe_overlay : V.pipe_overlay_behind);
            vkCmdPushConstants(V.cmd, V.pl, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
            vkCmdDraw(V.cmd, 3, 1, 0, 0);
        }
    }
    vkCmdEndRenderPass(V.cmd);   /* color now in TRANSFER_SRC_OPTIMAL */
    if (V.cfg.headless || V.shot_req) {
        if (V.shot_req) { V.shot_req = 0; V.shot_ready = 1; }
        VkBufferImageCopy c = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)V.w, (uint32_t)V.h, 1 } };
        vkCmdCopyImageToBuffer(V.cmd, V.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, V.rb, 1, &c);
    }
    if (present) {
        VkImage si = V.scimg[img];
        barrier(si, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkClearColorValue black = { { 0, 0, 0, 1 } }; VkImageSubresourceRange rr = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdClearColorImage(V.cmd, si, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &rr);
        /* 4:3 letterbox / pillarbox */
        int W = V.scext.width, H = V.scext.height, dw = W, dh = V.wide ? W * 9 / 16 : W * 3 / 4;
        if (dh > H) { dh = H; dw = V.wide ? H * 16 / 9 : H * 4 / 3; }
        int dx = (W - dw) / 2, dy = (H - dh) / 2;
        VkImageBlit bl = { { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { { 0, 0, 0 }, { V.w, V.h, 1 } },
                           { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { { dx, dy, 0 }, { dx + dw, dy + dh, 1 } } };
        vkCmdBlitImage(V.cmd, V.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, si, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
        barrier(si, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
    vkEndCommandBuffer(V.cmd);
    VkPipelineStageFlags wst = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, 0, present ? 1u : 0u, &V.sem_acq, &wst, 1, &V.cmd, present ? 1u : 0u, &V.sem_done };
    vkQueueSubmit(V.q, 1, &si, V.fence);
    if (present) {
        VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, 0, 1, &V.sem_done, 1, &V.sc, &img, 0 };
        VkResult r = vkQueuePresentKHR(V.q, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) V.need_recreate = 1;
    }
    V.pending = 1;
}

int vkr_reconfigure(int scale, int wide, int msaa) {
    if (scale < 1) scale = 1; if (scale > 8) scale = 8;
    msaa = clamp_msaa(msaa);
    if (scale == V.cfg.scale && !!wide == V.wide && msaa == V.msaa) return 0;
    vkr_wait(); vkDeviceWaitIdle(V.dev);
    destroy_targets();
    if (msaa != V.msaa) { destroy_pass(); V.msaa = msaa; if (make_pass() || make_pipes()) return -1; }
    V.cfg.scale = scale; V.wide = !!wide;
    return make_targets();
}
int vkr_msaa(void) { return V.msaa; }
int vkr_is_wide(void) { return V.wide; }
void vkr_request_screenshot(void) { V.shot_req = 1; }
int vkr_screenshot_ready(void) { return V.shot_ready; }
int vkr_take_screenshot(uint8_t *rgba) { if (!V.shot_ready) return -1; V.shot_ready = 0; vkr_readback(rgba); for (size_t i = 3; i < (size_t)V.w * V.h * 4; i += 4) rgba[i] = 255; return 0; }

int vkr_readback(uint8_t *rgba) { vkr_wait(); memcpy(rgba, V.rbmap, (size_t)V.w * V.h * 4); return 0; }
void vkr_size(int *w, int *h) { *w = V.w; *h = V.h; }
void vkr_resize(int w, int h) { V.win_w = w; V.win_h = h; V.need_recreate = 1; }
const char *vkr_device_name(void) { return V.name; }
void vkr_shutdown(void) { if (V.dev) { vkr_wait(); vkDeviceWaitIdle(V.dev); } }
