# Why s&box doesn't run on Apple Silicon (and how to fix it)

s&box is the standout Source 2 / Vulkan-native title that refuses to
launch on macOS Apple Silicon under Wineskin/Sikarugir/Whisky/CrossOver.
This document captures the diagnostic journey end to end.

## Symptom

Steam launches, you click Play, and the game prints:

```
[engine/RenderSystem] Vulkan Physical Device: Apple M4 Pro
[engine/RenderSystem] maxDescriptorSetSampledImages: 1280 does not meet requirements 65536
[engine/RenderSystem] maxDescriptorSetSamplers: 80 does not meet requirements 2048
[engine/Localization System] Your graphics device "Apple M4 Pro" does not meet minimum requirements...
```

## Walls hit + walls knocked down before the real one

Enumerated chronologically. Each was real and necessary, but only the
last is the wall everyone gets stuck on.

### Wall 1: AVX

s&box requires AVX. Rosetta 2 supports AVX/AVX2 emulation since
macOS 14 but doesn't advertise it in CPUID by default. Set
`ROSETTA_ADVERTISE_AVX=1` per-process and the engine sees an AVX-capable
CPU.

### Wall 2: Wineskin's launcher env-var filter

Sikarugir's `Contents/MacOS/launcher` Mach-O whitelists only four
`MVK_CONFIG_*` variables and silently drops the rest, including the
critical `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS`. Bypass: launch wine
directly via your own Mach-O wrapper (see `wine-wrapper/`) instead of
through the bundle's launcher binary.

### Wall 3: descriptor limits

Even with arg-buffers correctly enabled, MoltenVK 1.4.x reports:

| Limit | Reports | s&box wants |
|---|---|---|
| `maxDescriptorSetSampledImages` | 1,280 | 65,536 |
| `maxDescriptorSetSamplers` | 80 | 2,048 |

This is the wall.

## What the diagnosis ruled out

- **It's not Sikarugir's MoltenVK customization.** Vanilla MoltenVK 1.4.1
  release (`MoltenVK-macos-privateapi.tar` from Khronos releases)
  reports the same numbers.
- **It's not Rosetta hiding Tier 2 from Metal.** Native arm64 process
  with the same MoltenVK reports the same numbers. Read-Write Texture
  Tier 2 is correctly detected. M4 Pro is properly identified.
- **It's not the env-var being silently dropped.** `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0`
  drops the limit further (640/80), proving the variable is active and
  arg-buffers are engaging.

The actual wall is in MoltenVK source.

## Root cause

`MoltenVK/MoltenVK/GPUObjects/MVKDevice.mm`,
`MVKPhysicalDevice::initLimits()`:

```cpp
// Around line 2868 of HEAD dd34067
_properties.limits.maxPerStageDescriptorSamplers      = _metalFeatures.maxPerStageSamplerCount;     // 16
_properties.limits.maxPerStageDescriptorSampledImages = _metalFeatures.maxPerStageTextureCount;     // 256 on Tier 2
// ...
_properties.limits.maxDescriptorSetSamplers      = _properties.limits.maxPerStageDescriptorSamplers      * 5;  // 80
_properties.limits.maxDescriptorSetSampledImages = _properties.limits.maxPerStageDescriptorSampledImages * 5;  // 1280
```

These reflect Metal's **traditional argument-table model** (16 samplers
+ 256 textures per stage, × 5 shader stages). MoltenVK already knows
modern Apple Silicon with argument buffers can pack many more
descriptors — `getMaxSamplerCount()` returns
`_mtlDevice.maxArgumentBufferSamplerCount` (500,000 on M4 Pro), and
the descriptor-indexing extension's after-bind properties are bumped
to `1e6`:

```cpp
// Around line 858 of MVKDevice.mm
supportedProps12.maxDescriptorSetUpdateAfterBindSampledImages = isTier2 ? 1e6 : ...;
supportedProps12.maxDescriptorSetUpdateAfterBindSamplers      = isTier2 ? maxSamplerCnt : ...;
```

But the **base** `_properties.limits.*` fields stay legacy. s&box
queries the base fields, requests Vulkan 1.0 without
`VK_EXT_descriptor_indexing`, and rejects the GPU.

This is a MoltenVK design choice, not a bug per se. But it makes any
Vulkan-1.0 app that doesn't enable descriptor-indexing un-launchable
on Apple Silicon, even though Metal can serve the descriptors fine.

## The fix

See [`moltenvk/descriptor-limits.patch`](../moltenvk/descriptor-limits.patch).
~15 lines, applied after the legacy `maxDescriptorSet*` calculations:
when `_isUsingMetalArgumentBuffers && isTier2MetalArgumentBuffers()`,
overwrite the legacy values with the arg-buffer-actual values.

## Verification

[`probe/probe.c`](../probe/probe.c) is a tiny C program that links
MoltenVK and prints the descriptor-set limits.

| Config | maxDescriptorSetSampledImages | maxDescriptorSetSamplers |
|---|---|---|
| arg-buffers OFF | 640 | 80 |
| arg-buffers ON, vanilla 1.4.1 | 1,280 | 80 |
| arg-buffers ON, **patched** | **1,000,000** | **500,000** |
| s&box minimum | 65,536 | 2,048 |

After the patch, s&box passes the gate and proceeds to load the main
menu scene.

## Why now?

This wall has existed since at least 2024 (when s&box raised its
minimum requirements). I'm not the first to notice — there are
forum threads about it going back further. What changed in May 2026:
nothing, really. The patch is small enough that anyone with a couple
of hours and a working build environment can apply it. We just needed
someone to do it and write up the story.

## Related upstream conversations

- MoltenVK [#2220](https://github.com/KhronosGroup/MoltenVK/issues/2220)
  and similar issues track the gap between base and after-bind
  descriptor properties.
- s&box's own minimum-spec gate is upstream; the long-term fix is
  Facepunch enabling `VK_EXT_descriptor_indexing` (or moving to a
  Metal renderer). Until then, this patch.
