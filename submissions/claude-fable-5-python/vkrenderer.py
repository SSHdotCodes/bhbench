"""Vulkan (MoltenVK) rendering backend for the black hole simulation.

Layers:
  VulkanContext  — instance, surface, device, queue, command pool, helpers
  Swapchain      — surface swapchain with recreate-on-resize
  Engine         — render graph: scene -> bright -> gaussian bloom x2 -> composite
"""

import ctypes
import os

# The Khronos loader reads these at vkCreateInstance time (not process start),
# so setting them here is fine.
_ICD = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json"
if os.path.exists(_ICD):
    os.environ.setdefault("VK_ICD_FILENAMES", _ICD)
    os.environ.setdefault("VK_DRIVER_FILES", _ICD)

import vulkan as vk

HDR_FORMAT = vk.VK_FORMAT_R16G16B16A16_SFLOAT
LDR_FORMAT = vk.VK_FORMAT_R8G8B8A8_UNORM
CACHE_FORMAT = vk.VK_FORMAT_R16G16B16A16_SFLOAT

PORTABILITY_BIT = getattr(vk, "VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR", 0x1)

SCENE_PC_SIZE = 128     # 8 x vec4
POST_PC_SIZE = 32       # 2 x vec4 (only composite uses the second)

COLOR_SPACE_EXTENDED_SRGB_LINEAR = getattr(
    vk, "VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT", 1000104002)


def _pc(data):
    """bytes -> cffi buffer the binding accepts as `const void* pValues`."""
    return vk.ffi.from_buffer(data)


class Image:
    def __init__(self, handle, memory, view, width, height, fmt):
        self.handle, self.memory, self.view = handle, memory, view
        self.width, self.height, self.format = width, height, fmt


class VulkanContext:
    def __init__(self, window=None):
        self.window = window
        exts = ["VK_KHR_get_physical_device_properties2", "VK_KHR_portability_enumeration"]
        if window is not None:
            import glfw
            exts += list(glfw.get_required_instance_extensions())
            available = [e.extensionName
                         for e in vk.vkEnumerateInstanceExtensionProperties(None)]
            if "VK_EXT_swapchain_colorspace" in available:
                exts.append("VK_EXT_swapchain_colorspace")   # enables EDR/HDR

        app = vk.VkApplicationInfo(
            pApplicationName="gargantua", applicationVersion=1,
            pEngineName="none", engineVersion=1,
            apiVersion=vk.VK_MAKE_VERSION(1, 2, 0))
        self.instance = vk.vkCreateInstance(vk.VkInstanceCreateInfo(
            flags=PORTABILITY_BIT, pApplicationInfo=app,
            enabledExtensionCount=len(exts), ppEnabledExtensionNames=exts), None)

        self.surface = None
        if window is not None:
            import glfw
            surf_p = vk.ffi.new("VkSurfaceKHR *")
            res = glfw.create_window_surface(self.instance, window, None, surf_p)
            if res != vk.VK_SUCCESS:
                raise RuntimeError(f"glfwCreateWindowSurface failed: {res}")
            self.surface = surf_p[0]

        self.physical = vk.vkEnumeratePhysicalDevices(self.instance)[0]
        props = vk.vkGetPhysicalDeviceProperties(self.physical)
        self.device_name = props.deviceName

        self.queue_family = self._pick_queue_family()

        dev_exts = []
        available = [e.extensionName for e in
                     vk.vkEnumerateDeviceExtensionProperties(self.physical, None)]
        if "VK_KHR_portability_subset" in available:
            dev_exts.append("VK_KHR_portability_subset")
        if self.surface is not None:
            dev_exts.append("VK_KHR_swapchain")

        qci = vk.VkDeviceQueueCreateInfo(
            queueFamilyIndex=self.queue_family, queueCount=1, pQueuePriorities=[1.0])
        self.device = vk.vkCreateDevice(self.physical, vk.VkDeviceCreateInfo(
            queueCreateInfoCount=1, pQueueCreateInfos=[qci],
            enabledExtensionCount=len(dev_exts), ppEnabledExtensionNames=dev_exts,
            pEnabledFeatures=vk.VkPhysicalDeviceFeatures()), None)
        self.queue = vk.vkGetDeviceQueue(self.device, self.queue_family, 0)

        self.pool = vk.vkCreateCommandPool(self.device, vk.VkCommandPoolCreateInfo(
            flags=vk.VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            queueFamilyIndex=self.queue_family), None)

        self.mem_props = vk.vkGetPhysicalDeviceMemoryProperties(self.physical)

    def iproc(self, name):
        return vk.vkGetInstanceProcAddr(self.instance, name)

    def dproc(self, name):
        return vk.vkGetDeviceProcAddr(self.device, name)

    def _pick_queue_family(self):
        fams = vk.vkGetPhysicalDeviceQueueFamilyProperties(self.physical)
        get_support = (self.iproc("vkGetPhysicalDeviceSurfaceSupportKHR")
                       if self.surface is not None else None)
        for i, f in enumerate(fams):
            if not (f.queueFlags & vk.VK_QUEUE_GRAPHICS_BIT):
                continue
            if get_support is None or get_support(self.physical, i, self.surface):
                return i
        raise RuntimeError("no graphics(+present) queue family")

    def find_memory_type(self, type_bits, flags):
        for i in range(self.mem_props.memoryTypeCount):
            if (type_bits & (1 << i)) and \
               (self.mem_props.memoryTypes[i].propertyFlags & flags) == flags:
                return i
        raise RuntimeError("no suitable memory type")

    def create_image(self, width, height, fmt, usage, layers=1, cube=False,
                     mips=1):
        img = vk.vkCreateImage(self.device, vk.VkImageCreateInfo(
            flags=vk.VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT if cube else 0,
            imageType=vk.VK_IMAGE_TYPE_2D, format=fmt,
            extent=vk.VkExtent3D(width, height, 1),
            mipLevels=mips, arrayLayers=layers, samples=vk.VK_SAMPLE_COUNT_1_BIT,
            tiling=vk.VK_IMAGE_TILING_OPTIMAL, usage=usage,
            sharingMode=vk.VK_SHARING_MODE_EXCLUSIVE,
            initialLayout=vk.VK_IMAGE_LAYOUT_UNDEFINED), None)
        req = vk.vkGetImageMemoryRequirements(self.device, img)
        mem = vk.vkAllocateMemory(self.device, vk.VkMemoryAllocateInfo(
            allocationSize=req.size,
            memoryTypeIndex=self.find_memory_type(
                req.memoryTypeBits, vk.VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)), None)
        vk.vkBindImageMemory(self.device, img, mem, 0)
        view = self.create_view(
            img, fmt,
            view_type=vk.VK_IMAGE_VIEW_TYPE_CUBE if cube else vk.VK_IMAGE_VIEW_TYPE_2D,
            layer_count=layers, mip_count=mips)
        return Image(img, mem, view, width, height, fmt)

    def create_view(self, img, fmt, view_type=vk.VK_IMAGE_VIEW_TYPE_2D,
                    base_layer=0, layer_count=1, base_mip=0, mip_count=1):
        return vk.vkCreateImageView(self.device, vk.VkImageViewCreateInfo(
            image=img, viewType=view_type, format=fmt,
            subresourceRange=vk.VkImageSubresourceRange(
                aspectMask=vk.VK_IMAGE_ASPECT_COLOR_BIT,
                baseMipLevel=base_mip, levelCount=mip_count,
                baseArrayLayer=base_layer, layerCount=layer_count)), None)

    def destroy_image(self, image):
        vk.vkDestroyImageView(self.device, image.view, None)
        vk.vkDestroyImage(self.device, image.handle, None)
        vk.vkFreeMemory(self.device, image.memory, None)

    def create_buffer(self, size, usage, flags):
        buf = vk.vkCreateBuffer(self.device, vk.VkBufferCreateInfo(
            size=size, usage=usage, sharingMode=vk.VK_SHARING_MODE_EXCLUSIVE), None)
        req = vk.vkGetBufferMemoryRequirements(self.device, buf)
        mem = vk.vkAllocateMemory(self.device, vk.VkMemoryAllocateInfo(
            allocationSize=req.size,
            memoryTypeIndex=self.find_memory_type(req.memoryTypeBits, flags)), None)
        vk.vkBindBufferMemory(self.device, buf, mem, 0)
        return buf, mem

    def alloc_cmd(self):
        return vk.vkAllocateCommandBuffers(self.device, vk.VkCommandBufferAllocateInfo(
            commandPool=self.pool, level=vk.VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            commandBufferCount=1))[0]

    def submit_and_wait(self, cmd):
        fence = vk.vkCreateFence(self.device, vk.VkFenceCreateInfo(), None)
        vk.vkQueueSubmit(self.queue, 1,
                         [vk.VkSubmitInfo(commandBufferCount=1, pCommandBuffers=[cmd])],
                         fence)
        vk.vkWaitForFences(self.device, 1, [fence], vk.VK_TRUE, int(10e9))
        vk.vkDestroyFence(self.device, fence, None)

    def wait_idle(self):
        vk.vkDeviceWaitIdle(self.device)


class Swapchain:
    def __init__(self, ctx):
        self.ctx = ctx
        d = ctx.dproc
        i = ctx.iproc
        self._create = d("vkCreateSwapchainKHR")
        self._destroy = d("vkDestroySwapchainKHR")
        self._images = d("vkGetSwapchainImagesKHR")
        self._acquire = d("vkAcquireNextImageKHR")
        self._present = d("vkQueuePresentKHR")
        self._caps = i("vkGetPhysicalDeviceSurfaceCapabilitiesKHR")
        self._formats = i("vkGetPhysicalDeviceSurfaceFormatsKHR")
        self.handle = None
        self.views = []
        self.is_hdr = False
        self.recreate()

    def hdr_supported(self):
        return any(f.format == vk.VK_FORMAT_R16G16B16A16_SFLOAT
                   and f.colorSpace == COLOR_SPACE_EXTENDED_SRGB_LINEAR
                   for f in self._formats(self.ctx.physical, self.ctx.surface))

    def recreate(self, hdr=False):
        ctx = self.ctx
        caps = self._caps(ctx.physical, ctx.surface)
        fmts = self._formats(ctx.physical, ctx.surface)
        chosen = None
        if hdr:
            # macOS EDR: float16 + extended linear sRGB (1.0 = SDR white)
            chosen = next((f for f in fmts
                           if f.format == vk.VK_FORMAT_R16G16B16A16_SFLOAT
                           and f.colorSpace == COLOR_SPACE_EXTENDED_SRGB_LINEAR),
                          None)
        self.is_hdr = chosen is not None
        if chosen is None:
            # SDR: prefer UNORM (composite outputs gamma-encoded values itself)
            chosen = next((f for f in fmts
                           if f.format == vk.VK_FORMAT_B8G8R8A8_UNORM), fmts[0])
        self.format = chosen.format
        self.extent = (caps.currentExtent.width, caps.currentExtent.height)
        old = self.handle
        self.handle = self._create(ctx.device, vk.VkSwapchainCreateInfoKHR(
            surface=ctx.surface,
            minImageCount=min(caps.minImageCount + 1,
                              caps.maxImageCount or caps.minImageCount + 1),
            imageFormat=chosen.format, imageColorSpace=chosen.colorSpace,
            imageExtent=caps.currentExtent, imageArrayLayers=1,
            imageUsage=vk.VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            imageSharingMode=vk.VK_SHARING_MODE_EXCLUSIVE,
            preTransform=caps.currentTransform,
            compositeAlpha=vk.VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            presentMode=vk.VK_PRESENT_MODE_FIFO_KHR,
            clipped=vk.VK_TRUE, oldSwapchain=old), None)
        for v in self.views:
            vk.vkDestroyImageView(ctx.device, v, None)
        if old is not None:
            self._destroy(ctx.device, old, None)
        self.images = self._images(ctx.device, self.handle)
        self.views = [ctx.create_view(im, self.format) for im in self.images]

    def acquire(self, semaphore):
        return self._acquire(self.ctx.device, self.handle, int(1e9),
                             semaphore, vk.VK_NULL_HANDLE)

    def present(self, index, wait_semaphore):
        self._present(self.ctx.queue, vk.VkPresentInfoKHR(
            waitSemaphoreCount=1, pWaitSemaphores=[wait_semaphore],
            swapchainCount=1, pSwapchains=[self.handle],
            pImageIndices=[index]))

    def destroy(self):
        for v in self.views:
            vk.vkDestroyImageView(self.ctx.device, v, None)
        self.views = []
        if self.handle is not None:
            self._destroy(self.ctx.device, self.handle, None)
            self.handle = None


def _make_render_pass(device, fmt, final_layout):
    att = vk.VkAttachmentDescription(
        format=fmt, samples=vk.VK_SAMPLE_COUNT_1_BIT,
        loadOp=vk.VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        storeOp=vk.VK_ATTACHMENT_STORE_OP_STORE,
        stencilLoadOp=vk.VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        stencilStoreOp=vk.VK_ATTACHMENT_STORE_OP_DONT_CARE,
        initialLayout=vk.VK_IMAGE_LAYOUT_UNDEFINED,
        finalLayout=final_layout)
    ref = vk.VkAttachmentReference(
        attachment=0, layout=vk.VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    sub = vk.VkSubpassDescription(
        pipelineBindPoint=vk.VK_PIPELINE_BIND_POINT_GRAPHICS,
        colorAttachmentCount=1, pColorAttachments=[ref])
    deps = [
        vk.VkSubpassDependency(
            srcSubpass=vk.VK_SUBPASS_EXTERNAL, dstSubpass=0,
            srcStageMask=vk.VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                       | vk.VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            srcAccessMask=0,
            dstStageMask=vk.VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            dstAccessMask=vk.VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT),
        vk.VkSubpassDependency(
            srcSubpass=0, dstSubpass=vk.VK_SUBPASS_EXTERNAL,
            srcStageMask=vk.VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            srcAccessMask=vk.VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            dstStageMask=vk.VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                       | vk.VK_PIPELINE_STAGE_TRANSFER_BIT,
            dstAccessMask=vk.VK_ACCESS_SHADER_READ_BIT
                        | vk.VK_ACCESS_TRANSFER_READ_BIT),
    ]
    return vk.vkCreateRenderPass(device, vk.VkRenderPassCreateInfo(
        attachmentCount=1, pAttachments=[att],
        subpassCount=1, pSubpasses=[sub],
        dependencyCount=len(deps), pDependencies=deps), None)


def _make_render_pass_multi(device, fmt, count, final_layout):
    atts = [vk.VkAttachmentDescription(
        format=fmt, samples=vk.VK_SAMPLE_COUNT_1_BIT,
        loadOp=vk.VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        storeOp=vk.VK_ATTACHMENT_STORE_OP_STORE,
        stencilLoadOp=vk.VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        stencilStoreOp=vk.VK_ATTACHMENT_STORE_OP_DONT_CARE,
        initialLayout=vk.VK_IMAGE_LAYOUT_UNDEFINED,
        finalLayout=final_layout)
        for _ in range(count)]
    refs = [vk.VkAttachmentReference(
        attachment=i, layout=vk.VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        for i in range(count)]
    sub = vk.VkSubpassDescription(
        pipelineBindPoint=vk.VK_PIPELINE_BIND_POINT_GRAPHICS,
        colorAttachmentCount=count, pColorAttachments=refs)
    deps = [
        vk.VkSubpassDependency(
            srcSubpass=vk.VK_SUBPASS_EXTERNAL, dstSubpass=0,
            srcStageMask=vk.VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                       | vk.VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            srcAccessMask=0,
            dstStageMask=vk.VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            dstAccessMask=vk.VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT),
        vk.VkSubpassDependency(
            srcSubpass=0, dstSubpass=vk.VK_SUBPASS_EXTERNAL,
            srcStageMask=vk.VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            srcAccessMask=vk.VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            dstStageMask=vk.VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            dstAccessMask=vk.VK_ACCESS_SHADER_READ_BIT),
    ]
    return vk.vkCreateRenderPass(device, vk.VkRenderPassCreateInfo(
        attachmentCount=count, pAttachments=atts,
        subpassCount=1, pSubpasses=[sub],
        dependencyCount=len(deps), pDependencies=deps), None)


class Engine:
    """Owns pipelines and offscreen targets; records the full frame."""

    def __init__(self, ctx, spirv, render_size, out_format, out_final_layout):
        self.ctx = ctx
        d = ctx.device

        self.rp_hdr = _make_render_pass(d, HDR_FORMAT,
                                        vk.VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        self.rp_cache = _make_render_pass_multi(
            d, CACHE_FORMAT, 5, vk.VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        self.rp_out = _make_render_pass(d, out_format, out_final_layout)

        self.sampler = vk.vkCreateSampler(d, vk.VkSamplerCreateInfo(
            magFilter=vk.VK_FILTER_LINEAR, minFilter=vk.VK_FILTER_LINEAR,
            mipmapMode=vk.VK_SAMPLER_MIPMAP_MODE_NEAREST,
            addressModeU=vk.VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            addressModeV=vk.VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            addressModeW=vk.VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            maxAnisotropy=1.0), None)
        # disk noise texture: u = azimuth (wraps), v = log radius (clamps);
        # trilinear across the baked mip chain so minification cannot moire
        self.sampler_wrap = vk.vkCreateSampler(d, vk.VkSamplerCreateInfo(
            magFilter=vk.VK_FILTER_LINEAR, minFilter=vk.VK_FILTER_LINEAR,
            mipmapMode=vk.VK_SAMPLER_MIPMAP_MODE_LINEAR,
            addressModeU=vk.VK_SAMPLER_ADDRESS_MODE_REPEAT,
            addressModeV=vk.VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            addressModeW=vk.VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            maxLod=float(self.NOISE_MIPS - 1),
            maxAnisotropy=1.0), None)

        binds = [vk.VkDescriptorSetLayoutBinding(
                    binding=i, descriptorType=vk.VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    descriptorCount=1, stageFlags=vk.VK_SHADER_STAGE_FRAGMENT_BIT)
                 for i in range(3)]
        self.dset_layout = vk.vkCreateDescriptorSetLayout(
            d, vk.VkDescriptorSetLayoutCreateInfo(bindingCount=3, pBindings=binds), None)
        # scene set: binding 0 = baked sky cubemap, binding 1 = baked disk noise
        self.dset_layout_scene = vk.vkCreateDescriptorSetLayout(
            d, vk.VkDescriptorSetLayoutCreateInfo(bindingCount=2, pBindings=binds[:2]),
            None)
        cache_binds = [vk.VkDescriptorSetLayoutBinding(
                        binding=i,
                        descriptorType=vk.VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        descriptorCount=1,
                        stageFlags=vk.VK_SHADER_STAGE_FRAGMENT_BIT)
                       for i in range(7)]
        self.dset_layout_cache = vk.vkCreateDescriptorSetLayout(
            d, vk.VkDescriptorSetLayoutCreateInfo(
                bindingCount=len(cache_binds), pBindings=cache_binds), None)

        pc_scene = vk.VkPushConstantRange(
            stageFlags=vk.VK_SHADER_STAGE_FRAGMENT_BIT, offset=0, size=SCENE_PC_SIZE)
        pc_post = vk.VkPushConstantRange(
            stageFlags=vk.VK_SHADER_STAGE_FRAGMENT_BIT, offset=0, size=POST_PC_SIZE)
        self.layout_scene = vk.vkCreatePipelineLayout(d, vk.VkPipelineLayoutCreateInfo(
            setLayoutCount=1, pSetLayouts=[self.dset_layout_scene],
            pushConstantRangeCount=1, pPushConstantRanges=[pc_scene]), None)
        self.layout_cache = vk.vkCreatePipelineLayout(d, vk.VkPipelineLayoutCreateInfo(
            setLayoutCount=1, pSetLayouts=[self.dset_layout_cache],
            pushConstantRangeCount=1, pPushConstantRanges=[pc_scene]), None)
        self.layout_post = vk.vkCreatePipelineLayout(d, vk.VkPipelineLayoutCreateInfo(
            setLayoutCount=1, pSetLayouts=[self.dset_layout],
            pushConstantRangeCount=1, pPushConstantRanges=[pc_post]), None)

        mods = {name: vk.vkCreateShaderModule(
                    d, vk.VkShaderModuleCreateInfo(codeSize=len(code), pCode=code), None)
                for name, code in spirv.items()}
        self._modules = mods

        self.pipe_scene = self._pipeline(mods["fullscreen.vert"], mods["scene.frag"],
                                         self.layout_scene, self.rp_hdr)
        self.pipe_transport = self._pipeline(
            mods["fullscreen.vert"], mods["transport.frag"],
            self.layout_scene, self.rp_cache, color_attachments=5)
        self.pipe_shade_cache = self._pipeline(
            mods["fullscreen.vert"], mods["shadecache.frag"],
            self.layout_cache, self.rp_hdr)
        self.pipe_bright = self._pipeline(mods["fullscreen.vert"], mods["bright.frag"],
                                           self.layout_post, self.rp_hdr)
        self.pipe_blur = self._pipeline(mods["fullscreen.vert"], mods["blur.frag"],
                                        self.layout_post, self.rp_hdr)
        self.pipe_comp = self._pipeline(mods["fullscreen.vert"], mods["composite.frag"],
                                        self.layout_post, self.rp_out)

        self.dpool = vk.vkCreateDescriptorPool(d, vk.VkDescriptorPoolCreateInfo(
            flags=vk.VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            maxSets=32,
            poolSizeCount=1,
            pPoolSizes=[vk.VkDescriptorPoolSize(
                type=vk.VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                descriptorCount=96)]), None)

        self._bake(mods)
        self._targets = None
        self.fb_cache = None
        self._out = (out_format, out_final_layout)
        self.rebuild(render_size)

    # -- startup bake: nebula sky cubemap + advected disk noise texture --------
    SKY_FACE = 1024          # 90 deg/face; nebulae are low-frequency
    NOISE_W, NOISE_H = 2048, 2048   # square: the radial axis needs the same
                                    # fidelity as azimuth once magnified
    NOISE_MIPS = 9                  # 2048x2048 ... 8x8

    def _bake(self, mods):
        ctx = self.ctx
        d = ctx.device
        usage = (vk.VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                 | vk.VK_IMAGE_USAGE_SAMPLED_BIT)
        self.sky_cube = ctx.create_image(self.SKY_FACE, self.SKY_FACE, HDR_FORMAT,
                                         usage, layers=6, cube=True)
        self.disk_noise = ctx.create_image(self.NOISE_W, self.NOISE_H, HDR_FORMAT,
                                           usage, mips=self.NOISE_MIPS)

        pipe_sky = self._pipeline(mods["fullscreen.vert"], mods["bakesky.frag"],
                                  self.layout_post, self.rp_hdr)
        pipe_disk = self._pipeline(mods["fullscreen.vert"], mods["bakedisk.frag"],
                                   self.layout_post, self.rp_hdr)
        pipe_down = self._pipeline(mods["fullscreen.vert"], mods["downsample.frag"],
                                   self.layout_post, self.rp_hdr)

        import struct
        face_views, fbs = [], []
        for face in range(6):
            v = ctx.create_view(self.sky_cube.handle, HDR_FORMAT, base_layer=face)
            face_views.append(v)
            fbs.append(vk.vkCreateFramebuffer(d, vk.VkFramebufferCreateInfo(
                renderPass=self.rp_hdr, attachmentCount=1, pAttachments=[v],
                width=self.SKY_FACE, height=self.SKY_FACE, layers=1), None))

        # per-mip views of the noise texture: level 0 is rendered by the bake
        # shader, each further level box-downsampled from the previous one
        mip_views = [ctx.create_view(self.disk_noise.handle, HDR_FORMAT,
                                     base_mip=lv)
                     for lv in range(self.NOISE_MIPS)]
        mip_fbs, mip_sets = [], []
        for lv in range(self.NOISE_MIPS):
            lw = max(1, self.NOISE_W >> lv)
            lh = max(1, self.NOISE_H >> lv)
            mip_fbs.append(vk.vkCreateFramebuffer(d, vk.VkFramebufferCreateInfo(
                renderPass=self.rp_hdr, attachmentCount=1,
                pAttachments=[mip_views[lv]],
                width=lw, height=lh, layers=1), None))
            if lv == 0:
                continue
            s = vk.vkAllocateDescriptorSets(d, vk.VkDescriptorSetAllocateInfo(
                descriptorPool=self.dpool, descriptorSetCount=1,
                pSetLayouts=[self.dset_layout]))[0]
            writes = [vk.VkWriteDescriptorSet(
                        dstSet=s, dstBinding=i, descriptorCount=1,
                        descriptorType=vk.VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        pImageInfo=[vk.VkDescriptorImageInfo(
                            sampler=self.sampler, imageView=mip_views[lv - 1],
                            imageLayout=vk.VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)])
                      for i in range(3)]
            vk.vkUpdateDescriptorSets(d, len(writes), writes, 0, None)
            mip_sets.append(s)

        cmd = ctx.alloc_cmd()
        vk.vkBeginCommandBuffer(cmd, vk.VkCommandBufferBeginInfo())
        for face in range(6):
            self._begin(cmd, self.rp_hdr, fbs[face], self.SKY_FACE, self.SKY_FACE)
            vk.vkCmdBindPipeline(cmd, vk.VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_sky)
            pc = struct.pack("4f", float(face), 0, 0, 0)
            vk.vkCmdPushConstants(cmd, self.layout_post,
                                  vk.VK_SHADER_STAGE_FRAGMENT_BIT, 0, len(pc), _pc(pc))
            vk.vkCmdDraw(cmd, 3, 1, 0, 0)
            vk.vkCmdEndRenderPass(cmd)
        self._begin(cmd, self.rp_hdr, mip_fbs[0], self.NOISE_W, self.NOISE_H)
        vk.vkCmdBindPipeline(cmd, vk.VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_disk)
        vk.vkCmdDraw(cmd, 3, 1, 0, 0)
        vk.vkCmdEndRenderPass(cmd)
        for lv in range(1, self.NOISE_MIPS):
            self._barrier(cmd)
            self._begin(cmd, self.rp_hdr, mip_fbs[lv],
                        max(1, self.NOISE_W >> lv), max(1, self.NOISE_H >> lv))
            vk.vkCmdBindPipeline(cmd, vk.VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 pipe_down)
            vk.vkCmdBindDescriptorSets(cmd, vk.VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       self.layout_post, 0, 1,
                                       [mip_sets[lv - 1]], 0, None)
            vk.vkCmdDraw(cmd, 3, 1, 0, 0)
            vk.vkCmdEndRenderPass(cmd)
        vk.vkEndCommandBuffer(cmd)
        ctx.submit_and_wait(cmd)

        for fb in mip_fbs + fbs:
            vk.vkDestroyFramebuffer(d, fb, None)
        for s in mip_sets:
            vk.vkFreeDescriptorSets(d, self.dpool, 1, [s])
        for v in mip_views + face_views:
            vk.vkDestroyImageView(d, v, None)
        vk.vkDestroyPipeline(d, pipe_sky, None)
        vk.vkDestroyPipeline(d, pipe_disk, None)
        vk.vkDestroyPipeline(d, pipe_down, None)

    def set_output(self, out_format, out_final_layout):
        """Switch the output render pass format (SDR <-> HDR swapchain).
        Caller must vkDeviceWaitIdle first."""
        if (out_format, out_final_layout) == self._out:
            return
        d = self.ctx.device
        vk.vkDestroyPipeline(d, self.pipe_comp, None)
        vk.vkDestroyRenderPass(d, self.rp_out, None)
        self.rp_out = _make_render_pass(d, out_format, out_final_layout)
        self.pipe_comp = self._pipeline(self._modules["fullscreen.vert"],
                                        self._modules["composite.frag"],
                                        self.layout_post, self.rp_out)
        self._out = (out_format, out_final_layout)

    # -- pipeline factory -----------------------------------------------------
    def _pipeline(self, vert, frag, layout, render_pass, color_attachments=1):
        stages = [
            vk.VkPipelineShaderStageCreateInfo(
                stage=vk.VK_SHADER_STAGE_VERTEX_BIT, module=vert, pName="main"),
            vk.VkPipelineShaderStageCreateInfo(
                stage=vk.VK_SHADER_STAGE_FRAGMENT_BIT, module=frag, pName="main"),
        ]
        blend_att = vk.VkPipelineColorBlendAttachmentState(
            blendEnable=vk.VK_FALSE,
            colorWriteMask=vk.VK_COLOR_COMPONENT_R_BIT | vk.VK_COLOR_COMPONENT_G_BIT
                         | vk.VK_COLOR_COMPONENT_B_BIT | vk.VK_COLOR_COMPONENT_A_BIT)
        blend_atts = [blend_att for _ in range(color_attachments)]
        ci = vk.VkGraphicsPipelineCreateInfo(
            stageCount=2, pStages=stages,
            pVertexInputState=vk.VkPipelineVertexInputStateCreateInfo(),
            pInputAssemblyState=vk.VkPipelineInputAssemblyStateCreateInfo(
                topology=vk.VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
            pViewportState=vk.VkPipelineViewportStateCreateInfo(
                viewportCount=1, scissorCount=1),
            pRasterizationState=vk.VkPipelineRasterizationStateCreateInfo(
                polygonMode=vk.VK_POLYGON_MODE_FILL,
                cullMode=vk.VK_CULL_MODE_NONE,
                frontFace=vk.VK_FRONT_FACE_COUNTER_CLOCKWISE,
                lineWidth=1.0),
            pMultisampleState=vk.VkPipelineMultisampleStateCreateInfo(
                rasterizationSamples=vk.VK_SAMPLE_COUNT_1_BIT),
            pColorBlendState=vk.VkPipelineColorBlendStateCreateInfo(
                attachmentCount=color_attachments, pAttachments=blend_atts),
            pDynamicState=vk.VkPipelineDynamicStateCreateInfo(
                dynamicStateCount=2,
                pDynamicStates=[vk.VK_DYNAMIC_STATE_VIEWPORT,
                                vk.VK_DYNAMIC_STATE_SCISSOR]),
            layout=layout, renderPass=render_pass, subpass=0)
        return vk.vkCreateGraphicsPipelines(self.ctx.device, None, 1, [ci], None)[0]

    # -- offscreen targets ------------------------------------------------------
    def rebuild(self, render_size):
        ctx = self.ctx
        d = ctx.device
        if self._targets:
            if self.fb_cache is not None:
                vk.vkDestroyFramebuffer(d, self.fb_cache, None)
                self.fb_cache = None
            for img, fb in self._targets:
                if fb is not None:
                    vk.vkDestroyFramebuffer(d, fb, None)
                ctx.destroy_image(img)
            vk.vkResetDescriptorPool(d, self.dpool, 0)

        w, h = max(8, render_size[0]), max(8, render_size[1])
        self.render_size = (w, h)
        hw, hh = max(4, w // 2), max(4, h // 2)
        qw, qh = max(4, w // 4), max(4, h // 4)
        usage = (vk.VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                 | vk.VK_IMAGE_USAGE_SAMPLED_BIT)

        def target(tw, th):
            img = ctx.create_image(tw, th, HDR_FORMAT, usage)
            fb = vk.vkCreateFramebuffer(d, vk.VkFramebufferCreateInfo(
                renderPass=self.rp_hdr, attachmentCount=1, pAttachments=[img.view],
                width=tw, height=th, layers=1), None)
            return img, fb

        self.t_scene = target(w, h)
        self.t_bright = target(hw, hh)
        self.t_blur_a = target(hw, hh)
        self.t_blur_b = target(hw, hh)
        self.t_blur2_a = target(qw, qh)
        self.t_blur2_b = target(qw, qh)
        cache_usage = (vk.VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                       | vk.VK_IMAGE_USAGE_SAMPLED_BIT)
        self.t_cache = [ctx.create_image(w, h, CACHE_FORMAT, cache_usage)
                        for _ in range(5)]
        self.fb_cache = vk.vkCreateFramebuffer(d, vk.VkFramebufferCreateInfo(
            renderPass=self.rp_cache, attachmentCount=5,
            pAttachments=[img.view for img in self.t_cache],
            width=w, height=h, layers=1), None)
        self._targets = [self.t_scene, self.t_bright, self.t_blur_a,
                         self.t_blur_b, self.t_blur2_a, self.t_blur2_b]
        self._targets += [(img, None) for img in self.t_cache]

        def dset(*images):
            images = (images + (images[-1],) * 3)[:3]
            s = vk.vkAllocateDescriptorSets(d, vk.VkDescriptorSetAllocateInfo(
                descriptorPool=self.dpool, descriptorSetCount=1,
                pSetLayouts=[self.dset_layout]))[0]
            writes = [vk.VkWriteDescriptorSet(
                        dstSet=s, dstBinding=i, descriptorCount=1,
                        descriptorType=vk.VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        pImageInfo=[vk.VkDescriptorImageInfo(
                            sampler=self.sampler, imageView=img.view,
                            imageLayout=vk.VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)])
                      for i, img in enumerate(images)]
            vk.vkUpdateDescriptorSets(d, len(writes), writes, 0, None)
            return s

        self.ds_scene = dset(self.t_scene[0])
        self.ds_bright = dset(self.t_bright[0])
        self.ds_blur_a = dset(self.t_blur_a[0])
        self.ds_blur_b = dset(self.t_blur_b[0])
        self.ds_blur2_a = dset(self.t_blur2_a[0])
        self.ds_blur2_b = dset(self.t_blur2_b[0])
        self.ds_comp = dset(self.t_scene[0], self.t_blur_b[0], self.t_blur2_b[0])

        # scene set: baked sky cubemap + baked disk noise (pool was reset above)
        s = vk.vkAllocateDescriptorSets(d, vk.VkDescriptorSetAllocateInfo(
            descriptorPool=self.dpool, descriptorSetCount=1,
            pSetLayouts=[self.dset_layout_scene]))[0]
        infos = [(self.sampler, self.sky_cube.view),
                 (self.sampler_wrap, self.disk_noise.view)]
        writes = [vk.VkWriteDescriptorSet(
                    dstSet=s, dstBinding=i, descriptorCount=1,
                    descriptorType=vk.VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    pImageInfo=[vk.VkDescriptorImageInfo(
                        sampler=smp, imageView=view,
                        imageLayout=vk.VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)])
                  for i, (smp, view) in enumerate(infos)]
        vk.vkUpdateDescriptorSets(d, len(writes), writes, 0, None)
        self.ds_scene_tex = s

        s = vk.vkAllocateDescriptorSets(d, vk.VkDescriptorSetAllocateInfo(
            descriptorPool=self.dpool, descriptorSetCount=1,
            pSetLayouts=[self.dset_layout_cache]))[0]
        infos = [(self.sampler, self.sky_cube.view),
                 (self.sampler_wrap, self.disk_noise.view)]
        infos += [(self.sampler, img.view) for img in self.t_cache]
        writes = [vk.VkWriteDescriptorSet(
                    dstSet=s, dstBinding=i, descriptorCount=1,
                    descriptorType=vk.VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    pImageInfo=[vk.VkDescriptorImageInfo(
                        sampler=smp, imageView=view,
                        imageLayout=vk.VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)])
                  for i, (smp, view) in enumerate(infos)]
        vk.vkUpdateDescriptorSets(d, len(writes), writes, 0, None)
        self.ds_cache_tex = s

    # -- frame recording ----------------------------------------------------------
    def _begin(self, cmd, render_pass, fb, w, h):
        clear = vk.VkClearValue(color=vk.VkClearColorValue(float32=[0, 0, 0, 1]))
        vk.vkCmdBeginRenderPass(cmd, vk.VkRenderPassBeginInfo(
            renderPass=render_pass, framebuffer=fb,
            renderArea=vk.VkRect2D(offset=vk.VkOffset2D(x=0, y=0),
                                   extent=vk.VkExtent2D(width=w, height=h)),
            clearValueCount=1, pClearValues=[clear]),
            vk.VK_SUBPASS_CONTENTS_INLINE)
        vk.vkCmdSetViewport(cmd, 0, 1, [vk.VkViewport(
            x=0, y=0, width=float(w), height=float(h), minDepth=0.0, maxDepth=1.0)])
        vk.vkCmdSetScissor(cmd, 0, 1, [vk.VkRect2D(
            offset=vk.VkOffset2D(x=0, y=0),
            extent=vk.VkExtent2D(width=w, height=h))])

    def _begin_cache(self, cmd):
        w, h = self.render_size
        clears = [vk.VkClearValue(
            color=vk.VkClearColorValue(float32=[0, 0, 0, 1])) for _ in range(5)]
        vk.vkCmdBeginRenderPass(cmd, vk.VkRenderPassBeginInfo(
            renderPass=self.rp_cache, framebuffer=self.fb_cache,
            renderArea=vk.VkRect2D(offset=vk.VkOffset2D(x=0, y=0),
                                   extent=vk.VkExtent2D(width=w, height=h)),
            clearValueCount=len(clears), pClearValues=clears),
            vk.VK_SUBPASS_CONTENTS_INLINE)
        vk.vkCmdSetViewport(cmd, 0, 1, [vk.VkViewport(
            x=0, y=0, width=float(w), height=float(h), minDepth=0.0, maxDepth=1.0)])
        vk.vkCmdSetScissor(cmd, 0, 1, [vk.VkRect2D(
            offset=vk.VkOffset2D(x=0, y=0),
            extent=vk.VkExtent2D(width=w, height=h))])

    def _barrier(self, cmd):
        """Make prior color-attachment writes visible to fragment sampling.
        MoltenVK's handling of subpass EXTERNAL dependencies alone proved
        unreliable here (NaN tile garbage), so be explicit between passes."""
        vk.vkCmdPipelineBarrier(
            cmd,
            vk.VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            vk.VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            1, [vk.VkMemoryBarrier(
                srcAccessMask=vk.VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                dstAccessMask=vk.VK_ACCESS_SHADER_READ_BIT)],
            0, None, 0, None)

    def _post_pass(self, cmd, pipe, target, dset, pc_bytes):
        self._barrier(cmd)
        img, fb = target
        self._begin(cmd, self.rp_hdr, fb, img.width, img.height)
        vk.vkCmdBindPipeline(cmd, vk.VK_PIPELINE_BIND_POINT_GRAPHICS, pipe)
        vk.vkCmdBindDescriptorSets(cmd, vk.VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   self.layout_post, 0, 1, [dset], 0, None)
        vk.vkCmdPushConstants(cmd, self.layout_post,
                              vk.VK_SHADER_STAGE_FRAGMENT_BIT, 0, len(pc_bytes),
                              _pc(pc_bytes))
        vk.vkCmdDraw(cmd, 3, 1, 0, 0)
        vk.vkCmdEndRenderPass(cmd)

    def _post_and_composite(self, cmd, out_fb, out_extent, comp_pc, bloom=True):
        import struct

        if bloom:
            self._post_pass(cmd, self.pipe_bright, self.t_bright, self.ds_scene,
                            struct.pack("4f", 1.25, 0, 0, 0))

            def blur_chain(src_ds, a, b, a_ds, b_ds, radius):
                iw, ih = a[0].width, a[0].height
                cur_ds = src_ds
                for _ in range(2):
                    self._post_pass(cmd, self.pipe_blur, a, cur_ds,
                                    struct.pack("4f", radius / iw, 0, 0, 0))
                    self._post_pass(cmd, self.pipe_blur, b, a_ds,
                                    struct.pack("4f", 0, radius / ih, 0, 0))
                    cur_ds = b_ds

            blur_chain(self.ds_bright, self.t_blur_a, self.t_blur_b,
                       self.ds_blur_a, self.ds_blur_b, 1.4)
            blur_chain(self.ds_blur_b, self.t_blur2_a, self.t_blur2_b,
                       self.ds_blur2_a, self.ds_blur2_b, 2.2)

        self._barrier(cmd)
        self._begin(cmd, self.rp_out, out_fb, out_extent[0], out_extent[1])
        vk.vkCmdBindPipeline(cmd, vk.VK_PIPELINE_BIND_POINT_GRAPHICS, self.pipe_comp)
        vk.vkCmdBindDescriptorSets(cmd, vk.VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   self.layout_post, 0, 1, [self.ds_comp], 0, None)
        vk.vkCmdPushConstants(cmd, self.layout_post,
                              vk.VK_SHADER_STAGE_FRAGMENT_BIT, 0, len(comp_pc),
                              _pc(comp_pc))
        vk.vkCmdDraw(cmd, 3, 1, 0, 0)
        vk.vkCmdEndRenderPass(cmd)

    def record(self, cmd, out_fb, out_extent, scene_pc, comp_pc, bloom=True):
        # 1. geodesic scene pass
        img, fb = self.t_scene
        self._begin(cmd, self.rp_hdr, fb, img.width, img.height)
        vk.vkCmdBindPipeline(cmd, vk.VK_PIPELINE_BIND_POINT_GRAPHICS, self.pipe_scene)
        vk.vkCmdBindDescriptorSets(cmd, vk.VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   self.layout_scene, 0, 1, [self.ds_scene_tex],
                                   0, None)
        vk.vkCmdPushConstants(cmd, self.layout_scene,
                              vk.VK_SHADER_STAGE_FRAGMENT_BIT, 0, len(scene_pc),
                              _pc(scene_pc))
        vk.vkCmdDraw(cmd, 3, 1, 0, 0)
        vk.vkCmdEndRenderPass(cmd)

        self._post_and_composite(cmd, out_fb, out_extent, comp_pc, bloom)

    def record_cached(self, cmd, out_fb, out_extent, scene_pc, comp_pc,
                      rebuild_cache=False, bloom=True):
        if rebuild_cache:
            self._begin_cache(cmd)
            vk.vkCmdBindPipeline(cmd, vk.VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 self.pipe_transport)
            vk.vkCmdBindDescriptorSets(cmd, vk.VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       self.layout_scene, 0, 1, [self.ds_scene_tex],
                                       0, None)
            vk.vkCmdPushConstants(cmd, self.layout_scene,
                                  vk.VK_SHADER_STAGE_FRAGMENT_BIT, 0, len(scene_pc),
                                  _pc(scene_pc))
            vk.vkCmdDraw(cmd, 3, 1, 0, 0)
            vk.vkCmdEndRenderPass(cmd)

        self._barrier(cmd)
        img, fb = self.t_scene
        self._begin(cmd, self.rp_hdr, fb, img.width, img.height)
        vk.vkCmdBindPipeline(cmd, vk.VK_PIPELINE_BIND_POINT_GRAPHICS,
                             self.pipe_shade_cache)
        vk.vkCmdBindDescriptorSets(cmd, vk.VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   self.layout_cache, 0, 1, [self.ds_cache_tex],
                                   0, None)
        vk.vkCmdPushConstants(cmd, self.layout_cache,
                              vk.VK_SHADER_STAGE_FRAGMENT_BIT, 0, len(scene_pc),
                              _pc(scene_pc))
        vk.vkCmdDraw(cmd, 3, 1, 0, 0)
        vk.vkCmdEndRenderPass(cmd)

        self._post_and_composite(cmd, out_fb, out_extent, comp_pc, bloom)
