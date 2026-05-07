# Evidence

Captured 2026-05-07 on a 2024 14" MacBook Pro (Apple M4 Pro, 24 GB,
macOS 26.4.1) running through Sikarugir wine 10.0 + a fresh prefix.

## Probe results

A small C program that links MoltenVK directly and prints
`VkPhysicalDeviceProperties.limits.maxDescriptorSet*`. Source is in
[`probe/probe.c`](../probe/probe.c).

### Vanilla MoltenVK 1.4.1 privateapi (Khronos release)

```
=== arg-buffers OFF ===
device[0] Apple M4 Pro
  maxDescriptorSetSampledImages: 640 (sbox needs 65536)
  maxDescriptorSetSamplers:      80 (sbox needs 2048)
  maxDescriptorSetStorageImages: 640
  maxPerStageDescriptorSampledImages: 128

=== arg-buffers ON (=1, ALWAYS) ===
device[0] Apple M4 Pro
  maxDescriptorSetSampledImages: 1280 (sbox needs 65536)
  maxDescriptorSetSamplers:      80 (sbox needs 2048)
  maxDescriptorSetStorageImages: 1280
  maxPerStageDescriptorSampledImages: 256
```

Both native arm64 *and* x86_64 Rosetta returned identical numbers
under vanilla MoltenVK. Read-Write Texture Tier 2 detected. So:

- Sikarugir's MoltenVK isn't customized differently from vanilla.
- Rosetta isn't hiding Tier 2 features from Metal.
- The arg-buffers env var **is** engaging (640→1280 sampled images).
- The cap is hardcoded in MoltenVK.

### Patched MoltenVK (this repo)

```
=== arg-buffers ON, patched ===
device[0] Apple M4 Pro
  maxDescriptorSetSampledImages: 1000000 (sbox needs 65536)
  maxDescriptorSetSamplers:      500000 (sbox needs 2048)
  maxDescriptorSetStorageImages: 1000000
  maxPerStageDescriptorSampledImages: 1000000
```

The 500,000 for samplers is the actual M4 Pro
`MTLDevice.maxArgumentBufferSamplerCount` — not a fabricated value.
MoltenVK's own `getMaxSamplerCount()` already returns this number
elsewhere in the same file.

## s&box launch result

With the patched MoltenVK and the wine setup from this repo, s&box
launches cleanly past the descriptor-limit gate. Excerpt from
`sbox.log`:

```
2026/05/07 08:31:17.5754  [engine/Engine] SteamAPI_Init succeeded.  SteamID is [redacted], AppID is 590830
2026/05/07 08:31:17.9344  [engine/RenderSystem] Saved video settings config to 'cfg\video.txt'
2026/05/07 08:31:17.9507  [engine/RenderSystem] Vulkan Physical Device: Apple M4 Pro
2026/05/07 08:31:17.9528  [engine/RenderSystem] Disabling VK_EXT_graphics_pipeline_library dependent extensions.
2026/05/07 08:31:17.9776  [engine/RenderSystem] Vulkan driver version: 0.2.2210
2026/05/07 08:31:22.2883  [PackageManager] Install Package (Already Mounted) local.base#local [local]
2026/05/07 08:31:25.1600  [Generic] Loading startup scene: scenes/menu-main.scene
2026/05/07 08:31:27.5725  [Generic] Bootstrap Networking...
```

No `maxDescriptor* does not meet requirements` lines. No `Your graphics
device does not meet minimum requirements`. The engine accepted the
GPU, loaded the menu scene, brought up networking.

## Reproducing

Anyone with similar hardware can rerun the probe — see
[`docs/BUILD.md`](BUILD.md) section 1 for the build steps and the
probe verification command.

If your MacBook reports something different, please open an issue
with: chip model, macOS version, MoltenVK commit, and the full probe
output for both vanilla and patched dylibs.
