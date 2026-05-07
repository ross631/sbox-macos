# MoltenVK descriptor-limits patch

`descriptor-limits.patch` is a small (~15-line) source patch against
[KhronosGroup/MoltenVK](https://github.com/KhronosGroup/MoltenVK).

## What it changes

In `MoltenVK/MoltenVK/GPUObjects/MVKDevice.mm`,
`MVKPhysicalDevice::initLimits()` computes the *base*
`VkPhysicalDeviceProperties.limits.maxDescriptorSet*` values from
Metal's *legacy argument-table* model (16 samplers per stage × 5 stages
= 80, 256 textures per stage × 5 stages = 1280 on Tier 2).

Modern Apple Silicon GPUs with Metal **argument buffers** can pack
millions of descriptors into a buffer object — but unmodified MoltenVK
only surfaces those high numbers in
`VkPhysicalDeviceDescriptorIndexingProperties.maxDescriptorSetUpdateAfterBind*`,
which is part of the optional `VK_EXT_descriptor_indexing` extension.

Some Vulkan-1.0 apps query the *base* limits directly and refuse to
launch when the numbers look low. Source 2 / s&box is the most visible
example:

> `[engine/RenderSystem] maxDescriptorSetSampledImages: 1280 does not meet requirements 65536`
> `[engine/RenderSystem] maxDescriptorSetSamplers: 80 does not meet requirements 2048`
> `[engine/Localization System] Your graphics device does not meet minimum requirements...`

The patch adds a stanza after the legacy assignments:

```cpp
if (_isUsingMetalArgumentBuffers && isTier2MetalArgumentBuffers()) {
    uint32_t argBufSamplerMax = (uint32_t)_mtlDevice.maxArgumentBufferSamplerCount;
    _properties.limits.maxPerStageDescriptorSampledImages = 1000000;
    _properties.limits.maxPerStageDescriptorStorageImages = 1000000;
    _properties.limits.maxPerStageDescriptorSamplers      = argBufSamplerMax;
    _properties.limits.maxDescriptorSetSampledImages      = 1000000;
    _properties.limits.maxDescriptorSetStorageImages      = 1000000;
    _properties.limits.maxDescriptorSetSamplers           = argBufSamplerMax;
}
```

## Why this isn't lying about hardware

`_mtlDevice.maxArgumentBufferSamplerCount` is the *actual* Metal
argument-buffer sampler count — exactly what MoltenVK's own
`MVKPhysicalDevice::getMaxSamplerCount()` returns elsewhere in the
file. The patch makes the base properties consistent with the
arg-buffer numbers MoltenVK already trusts internally.

`1,000,000` for sampled and storage images is well within Tier 2
arg-buffer capacity (descriptors are bounded by buffer-object size,
not by a hard count). On an M4 Pro the actual sampler value reported
is 500,000, which is what you'll see when you run the probe.

## Applies to

| Limit | Vanilla MoltenVK 1.4.x | Patched | s&box minimum |
|---|---|---|---|
| `maxDescriptorSetSampledImages` | 1,280 | 1,000,000 | 65,536 |
| `maxDescriptorSetSamplers` | 80 | up to ~500,000 | 2,048 |
| `maxDescriptorSetStorageImages` | 1,280 | 1,000,000 | — |
| `maxPerStageDescriptorSampledImages` | 256 | 1,000,000 | — |

## Building

Patch was authored against MoltenVK
[`dd34067`](https://github.com/KhronosGroup/MoltenVK/commit/dd34067)
(post-1.4.1 main branch). Should apply cleanly to nearby commits;
file the issue [here](https://github.com/ross631/sbox-macos/issues)
if a future MoltenVK refactor moves the patch site.

```sh
git clone https://github.com/KhronosGroup/MoltenVK.git
cd MoltenVK
git apply ../sbox-macos/moltenvk/descriptor-limits.patch
./fetchDependencies --macos
make macos
# output: Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib
```

That dylib is universal (x86_64 + arm64) and drops in wherever your
wine/Whisky/CrossOver/whatever picks up MoltenVK.

## Risks / caveats

- Limits exist for a reason. Metal must be able to back what we
  advertise. On Tier 2 + arg-buffers it can; on Tier 1 the patch
  short-circuits and does nothing.
- If you see GPU faults at runtime under heavy descriptor pressure,
  please file an issue.
- Patch only takes effect when `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=1`
  and the device is Tier 2 (M-series Apple Silicon).
