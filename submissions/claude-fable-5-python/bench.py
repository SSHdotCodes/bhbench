"""GPU frame-time benchmark: repeatedly submits the full frame offscreen and
reports milliseconds per frame. Usage: .venv/bin/python bench.py [WxH steps]..."""

import sys
import time

import blackhole
from blackhole import scene_push_constants, composite_push_constants

blackhole.ensure_vulkan_loadable()

import vulkan as vk  # noqa: E402
import shaders       # noqa: E402
import vkrenderer as vkr  # noqa: E402


def bench(ctx, spirv, size, steps, frames=60):
    engine = vkr.Engine(ctx, spirv, size, vkr.LDR_FORMAT,
                        vk.VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    out = ctx.create_image(size[0], size[1], vkr.LDR_FORMAT,
                           vk.VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                           | vk.VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
    fb = vk.vkCreateFramebuffer(ctx.device, vk.VkFramebufferCreateInfo(
        renderPass=engine.rp_out, attachmentCount=1, pAttachments=[out.view],
        width=size[0], height=size[1], layers=1), None)
    cmd = ctx.alloc_cmd()
    vk.vkBeginCommandBuffer(cmd, vk.VkCommandBufferBeginInfo())
    engine.record(cmd, fb, size,
                  scene_push_constants(0.55, 0.145, 27.0, 38.0, size, steps),
                  composite_push_constants(38.0))
    vk.vkEndCommandBuffer(cmd)

    fence = vk.vkCreateFence(ctx.device, vk.VkFenceCreateInfo(), None)

    def submit_wait():
        vk.vkQueueSubmit(ctx.queue, 1,
                         [vk.VkSubmitInfo(commandBufferCount=1, pCommandBuffers=[cmd])],
                         fence)
        vk.vkWaitForFences(ctx.device, 1, [fence], vk.VK_TRUE, int(10e9))
        vk.vkResetFences(ctx.device, 1, [fence])

    submit_wait()                                # warm-up
    t0 = time.perf_counter()
    for _ in range(frames):
        submit_wait()
    ms = (time.perf_counter() - t0) / frames * 1000.0
    print(f"{size[0]}x{size[1]:<5} steps={steps:<4} {ms:7.2f} ms/frame "
          f"({1000.0/ms:5.0f} fps)")
    return ms


def bench_cached(ctx, spirv, size, steps, frames=120):
    engine = vkr.Engine(ctx, spirv, size, vkr.LDR_FORMAT,
                        vk.VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    out = ctx.create_image(size[0], size[1], vkr.LDR_FORMAT,
                           vk.VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                           | vk.VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
    fb = vk.vkCreateFramebuffer(ctx.device, vk.VkFramebufferCreateInfo(
        renderPass=engine.rp_out, attachmentCount=1, pAttachments=[out.view],
        width=size[0], height=size[1], layers=1), None)
    scene_pc = scene_push_constants(0.55, 0.145, 27.0, 38.0, size, steps)
    comp_pc = composite_push_constants(38.0)

    def make_cmd(rebuild):
        cmd = ctx.alloc_cmd()
        vk.vkBeginCommandBuffer(cmd, vk.VkCommandBufferBeginInfo())
        engine.record_cached(cmd, fb, size, scene_pc, comp_pc,
                             rebuild_cache=rebuild)
        vk.vkEndCommandBuffer(cmd)
        return cmd

    cmd_rebuild = make_cmd(True)
    cmd_reuse = make_cmd(False)
    fence = vk.vkCreateFence(ctx.device, vk.VkFenceCreateInfo(), None)

    def submit_wait(cmd):
        vk.vkQueueSubmit(ctx.queue, 1,
                         [vk.VkSubmitInfo(commandBufferCount=1, pCommandBuffers=[cmd])],
                         fence)
        vk.vkWaitForFences(ctx.device, 1, [fence], vk.VK_TRUE, int(10e9))
        vk.vkResetFences(ctx.device, 1, [fence])

    t0 = time.perf_counter()
    submit_wait(cmd_rebuild)
    rebuild_ms = (time.perf_counter() - t0) * 1000.0
    submit_wait(cmd_reuse)
    t0 = time.perf_counter()
    for _ in range(frames):
        submit_wait(cmd_reuse)
    ms = (time.perf_counter() - t0) / frames * 1000.0
    print(f"{size[0]}x{size[1]:<5} cached    {ms:7.2f} ms/frame "
          f"({1000.0/ms:5.0f} fps)  rebuild {rebuild_ms:.2f} ms")
    return ms


def main():
    ctx = vkr.VulkanContext(window=None)
    print(f"GPU: {ctx.device_name}")
    spirv = shaders.compile_all()
    cases = []
    for arg in sys.argv[1:]:
        wh, st = arg.split()
        w, h = wh.split("x")
        cases.append(((int(w), int(h)), int(st)))
    if not cases:
        cases = [((2880, 1620), 256), ((2880, 1620), 192), ((2880, 1620), 128),
                 ((2160, 1215), 256), ((1920, 1080), 256), ((1440, 810), 256)]
    for size, steps in cases:
        bench(ctx, spirv, size, steps)
        if steps >= 700:
            bench_cached(ctx, spirv, size, steps)


if __name__ == "__main__":
    main()
